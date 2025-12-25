#pragma once

#include <string>
#include <vector>

struct PhaseBidGroup {
  int phaseIndex;
  std::vector<std::string> lanes;
};

struct AuctionWeights {
  double queueWeight;
  double waitWeight;
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
};
