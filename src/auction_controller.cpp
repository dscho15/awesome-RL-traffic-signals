#include "auction_controller.hpp"

#include <limits>

#include <libtraci/libtraci.h>

AuctionController::AuctionController(std::string trafficLightId,
                                     std::vector<PhaseBidGroup> phaseGroups,
                                     AuctionWeights weights,
                                     PhaseSafetyConstraints constraints,
                                     std::vector<std::string> programPhaseStates)
    : trafficLightId_(std::move(trafficLightId)),
      phaseGroups_(std::move(phaseGroups)),
      weights_(weights),
      currentPhaseIndex_(libtraci::trafficlight::getPhase(trafficLightId_)),
      elapsedPhaseSeconds_(0),
      minGreen_(constraints.minGreen),
      maxGreen_(constraints.maxGreen),
      yellowDuration_(constraints.yellowDuration),
      allRedDuration_(constraints.allRedDuration),
      programPhaseStates_(std::move(programPhaseStates)),
      transitionState_(TransitionState::None),
      pendingPhaseIndex_(currentPhaseIndex_),
      transitionAllRedIndex_(-1),
      lastPhaseChangeStep_(0) {}

void AuctionController::setWeights(AuctionWeights weights) { weights_ = weights; }

AuctionWeights AuctionController::weights() const { return weights_; }

int AuctionController::selectWinningPhase() const {
  double bestScore = -std::numeric_limits<double>::infinity();
  int bestPhase = phaseGroups_.empty() ? 0 : phaseGroups_.front().phaseIndex;

  for (const auto& group : phaseGroups_) {
    double score = scorePhase(group);
    if (score > bestScore) {
      bestScore = score;
      bestPhase = group.phaseIndex;
    }
  }

  return bestPhase;
}

int AuctionController::selectWinningPhaseExcluding(int excludedPhase) const {
  double bestScore = -std::numeric_limits<double>::infinity();
  int bestPhase = excludedPhase;
  bool found = false;

  for (const auto& group : phaseGroups_) {
    if (group.phaseIndex == excludedPhase) {
      continue;
    }
    double score = scorePhase(group);
    if (!found || score > bestScore) {
      found = true;
      bestScore = score;
      bestPhase = group.phaseIndex;
    }
  }

  return found ? bestPhase : excludedPhase;
}

namespace {

bool isYellowPhase(const std::string& state) {
  return state.find('y') != std::string::npos || state.find('Y') != std::string::npos;
}

bool isAllRedPhase(const std::string& state) {
  if (state.empty()) {
    return false;
  }
  for (char ch : state) {
    if (ch != 'r' && ch != 'R') {
      return false;
    }
  }
  return true;
}

}  // namespace

void AuctionController::beginTransition(int nextPhase, int currentSimStepSeconds) {
  pendingPhaseIndex_ = nextPhase;
  transitionAllRedIndex_ = -1;
  int yellowPhaseIndex = -1;

  if (!programPhaseStates_.empty()) {
    int totalPhases = static_cast<int>(programPhaseStates_.size());
    for (int offset = 1; offset <= totalPhases; ++offset) {
      int idx = (currentPhaseIndex_ + offset) % totalPhases;
      if (idx == nextPhase) {
        break;
      }
      const auto& state = programPhaseStates_[idx];
      if (yellowPhaseIndex < 0 && isYellowPhase(state)) {
        yellowPhaseIndex = idx;
      }
      if (transitionAllRedIndex_ < 0 && isAllRedPhase(state)) {
        transitionAllRedIndex_ = idx;
      }
    }
  }

  if (yellowDuration_ > 0 && yellowPhaseIndex >= 0) {
    libtraci::trafficlight::setPhase(trafficLightId_, yellowPhaseIndex);
    currentPhaseIndex_ = yellowPhaseIndex;
    transitionState_ = TransitionState::Yellow;
    lastPhaseChangeStep_ = currentSimStepSeconds;
    elapsedPhaseSeconds_ = 0;
    return;
  }

  if (allRedDuration_ > 0 && transitionAllRedIndex_ >= 0) {
    libtraci::trafficlight::setPhase(trafficLightId_, transitionAllRedIndex_);
    currentPhaseIndex_ = transitionAllRedIndex_;
    transitionState_ = TransitionState::AllRed;
    lastPhaseChangeStep_ = currentSimStepSeconds;
    elapsedPhaseSeconds_ = 0;
    return;
  }

  libtraci::trafficlight::setPhase(trafficLightId_, nextPhase);
  currentPhaseIndex_ = nextPhase;
  transitionState_ = TransitionState::None;
  lastPhaseChangeStep_ = currentSimStepSeconds;
  elapsedPhaseSeconds_ = 0;
}

void AuctionController::applyPhaseIfDue(int currentSimStepSeconds) {
  if (currentSimStepSeconds < lastPhaseChangeStep_) {
    lastPhaseChangeStep_ = currentSimStepSeconds;
  }

  elapsedPhaseSeconds_ = currentSimStepSeconds - lastPhaseChangeStep_;

  if (transitionState_ == TransitionState::Yellow) {
    if (elapsedPhaseSeconds_ < yellowDuration_) {
      return;
    }
    if (allRedDuration_ > 0 && transitionAllRedIndex_ >= 0) {
      libtraci::trafficlight::setPhase(trafficLightId_, transitionAllRedIndex_);
      currentPhaseIndex_ = transitionAllRedIndex_;
      transitionState_ = TransitionState::AllRed;
      lastPhaseChangeStep_ = currentSimStepSeconds;
      elapsedPhaseSeconds_ = 0;
      return;
    }
    libtraci::trafficlight::setPhase(trafficLightId_, pendingPhaseIndex_);
    currentPhaseIndex_ = pendingPhaseIndex_;
    transitionState_ = TransitionState::None;
    lastPhaseChangeStep_ = currentSimStepSeconds;
    elapsedPhaseSeconds_ = 0;
    return;
  }

  if (transitionState_ == TransitionState::AllRed) {
    if (elapsedPhaseSeconds_ < allRedDuration_) {
      return;
    }
    libtraci::trafficlight::setPhase(trafficLightId_, pendingPhaseIndex_);
    currentPhaseIndex_ = pendingPhaseIndex_;
    transitionState_ = TransitionState::None;
    lastPhaseChangeStep_ = currentSimStepSeconds;
    elapsedPhaseSeconds_ = 0;
    return;
  }

  if (elapsedPhaseSeconds_ < minGreen_) {
    return;
  }

  bool maxExceeded = maxGreen_ > 0 && elapsedPhaseSeconds_ >= maxGreen_;
  int winningPhase = selectWinningPhase();
  if (maxExceeded && winningPhase == currentPhaseIndex_) {
    winningPhase = selectWinningPhaseExcluding(currentPhaseIndex_);
  }

  if (!maxExceeded && winningPhase == currentPhaseIndex_) {
    return;
  }

  beginTransition(winningPhase, currentSimStepSeconds);
}

double AuctionController::scorePhase(const PhaseBidGroup& group) const {
  double score = 0.0;

  for (const auto& laneId : group.lanes) {
    double queue = libtraci::lane::getLastStepVehicleNumber(laneId);
    double wait = libtraci::lane::getWaitingTime(laneId);
    score += weights_.queueWeight * queue + weights_.waitWeight * wait;
  }

  return score;
}
