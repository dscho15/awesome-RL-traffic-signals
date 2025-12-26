#pragma once

#include <string>
#include <vector>

struct PhaseBidGroup {
  int phaseIndex;
  std::vector<std::string> lanes;
  std::vector<std::string> detectors;
};

struct AuctionWeights {
  double queueWeight;
  double waitWeight;
};

enum class ScoringMode {
  QueueWait,
  Occupancy,
  Flow,
};

class AuctionController {
 public:
  AuctionController(std::string trafficLightId,
                    std::vector<PhaseBidGroup> phaseGroups,
                    AuctionWeights weights,
                    ScoringMode scoringMode,
                    int phaseDurationSeconds);

  void setWeights(AuctionWeights weights);
  AuctionWeights weights() const;
  ScoringMode scoringMode() const;

  int selectWinningPhase() const;
  void applyPhaseIfDue(int currentSimStepSeconds);

 private:
  double scorePhase(const PhaseBidGroup& group) const;

  std::string trafficLightId_;
  std::vector<PhaseBidGroup> phaseGroups_;
  AuctionWeights weights_;
  ScoringMode scoringMode_;
  int phaseDurationSeconds_;
  int lastPhaseChangeStep_;
};
