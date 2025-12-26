#pragma once

#include <string>
#include <unordered_map>
#include <vector>

struct PhaseBidGroup {
  int phaseIndex;
  std::vector<std::string> lanes;
};

struct AuctionWeights {
  double queueWeight;
  double waitWeight;
  double agingWeight;
};

class AuctionController {
 public:
  AuctionController(std::string trafficLightId,
                    std::vector<PhaseBidGroup> phaseGroups,
                    AuctionWeights weights,
                    int phaseDurationSeconds);

  void setWeights(AuctionWeights weights);
  AuctionWeights weights() const;

  int selectWinningPhase() const;
  void applyPhaseIfDue(int currentSimStepSeconds);

 private:
  double scorePhase(const PhaseBidGroup& group) const;

  std::string trafficLightId_;
  std::vector<PhaseBidGroup> phaseGroups_;
  AuctionWeights weights_;
  int phaseDurationSeconds_;
  int lastPhaseChangeStep_;
  int lastAgingUpdateStep_;
  int currentPhase_;
  std::unordered_map<int, int> timeSinceServedByPhase_;
};
