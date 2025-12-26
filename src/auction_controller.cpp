#include "auction_controller.hpp"

#include <limits>

#include <libtraci/libtraci.h>

AuctionController::AuctionController(std::string trafficLightId,
                                     std::vector<PhaseBidGroup> phaseGroups,
                                     AuctionWeights weights,
                                     int phaseDurationSeconds)
    : trafficLightId_(std::move(trafficLightId)),
      phaseGroups_(std::move(phaseGroups)),
      weights_(weights),
      phaseDurationSeconds_(phaseDurationSeconds),
      lastPhaseChangeStep_(0),
      lastAgingUpdateStep_(0),
      currentPhase_(phaseGroups_.empty() ? 0 : phaseGroups_.front().phaseIndex) {
  for (const auto& group : phaseGroups_) {
    timeSinceServedByPhase_[group.phaseIndex] = 0;
  }
}

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

void AuctionController::applyPhaseIfDue(int currentSimStepSeconds) {
  int delta = currentSimStepSeconds - lastAgingUpdateStep_;
  if (delta > 0) {
    for (auto& entry : timeSinceServedByPhase_) {
      entry.second += delta;
    }
    lastAgingUpdateStep_ = currentSimStepSeconds;
  }

  if (phaseDurationSeconds_ <= 0) {
    return;
  }

  if ((currentSimStepSeconds - lastPhaseChangeStep_) < phaseDurationSeconds_) {
    return;
  }

  int winningPhase = selectWinningPhase();
  if (winningPhase != currentPhase_) {
    libtraci::trafficlight::setPhase(trafficLightId_, winningPhase);
    currentPhase_ = winningPhase;
    timeSinceServedByPhase_[winningPhase] = 0;
  }
  lastPhaseChangeStep_ = currentSimStepSeconds;
}

double AuctionController::scorePhase(const PhaseBidGroup& group) const {
  double score = 0.0;

  for (const auto& laneId : group.lanes) {
    double queue = libtraci::lane::getLastStepVehicleNumber(laneId);
    double wait = libtraci::lane::getWaitingTime(laneId);
    score += weights_.queueWeight * queue + weights_.waitWeight * wait;
  }

  auto agingIt = timeSinceServedByPhase_.find(group.phaseIndex);
  if (agingIt != timeSinceServedByPhase_.end()) {
    score += weights_.agingWeight * static_cast<double>(agingIt->second);
  }

  return score;
}
