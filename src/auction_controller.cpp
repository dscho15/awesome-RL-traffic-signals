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

void AuctionController::applyPhaseIfDue(int currentSimStepSeconds) {
  if (phaseDurationSeconds_ <= 0) {
    return;
  }

  if ((currentSimStepSeconds - lastPhaseChangeStep_) < phaseDurationSeconds_) {
    return;
  }

  int winningPhase = selectWinningPhase();
  libtraci::trafficlight::setPhase(trafficLightId_, winningPhase);
  lastPhaseChangeStep_ = currentSimStepSeconds;
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
