#include "auction_controller.hpp"

#include <limits>

#include <libtraci/libtraci.h>

AuctionController::AuctionController(std::string trafficLightId,
                                     std::vector<PhaseBidGroup> phaseGroups,
                                     AuctionWeights weights,
                                     ScoringMode scoringMode,
                                     int phaseDurationSeconds)
    : trafficLightId_(std::move(trafficLightId)),
      phaseGroups_(std::move(phaseGroups)),
      weights_(weights),
      scoringMode_(scoringMode),
      phaseDurationSeconds_(phaseDurationSeconds),
      lastPhaseChangeStep_(0) {}

void AuctionController::setWeights(AuctionWeights weights) { weights_ = weights; }

AuctionWeights AuctionController::weights() const { return weights_; }

ScoringMode AuctionController::scoringMode() const { return scoringMode_; }

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

  switch (scoringMode_) {
    case ScoringMode::QueueWait:
      for (const auto& laneId : group.lanes) {
        double queue = libtraci::lane::getLastStepVehicleNumber(laneId);
        double wait = libtraci::lane::getWaitingTime(laneId);
        score += weights_.queueWeight * queue + weights_.waitWeight * wait;
      }
      break;
    case ScoringMode::Occupancy:
      if (!group.detectors.empty()) {
        for (const auto& detectorId : group.detectors) {
          score += libtraci::lanearea::getLastStepOccupancy(detectorId);
        }
      } else {
        for (const auto& laneId : group.lanes) {
          score += libtraci::lane::getLastStepOccupancy(laneId);
        }
      }
      break;
    case ScoringMode::Flow:
      if (!group.detectors.empty()) {
        for (const auto& detectorId : group.detectors) {
          score += libtraci::lanearea::getLastStepVehicleNumber(detectorId);
        }
      } else {
        for (const auto& laneId : group.lanes) {
          score += libtraci::lane::getLastStepVehicleNumber(laneId);
        }
      }
      break;
  }

  return score;
}
