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

struct PhaseSafetyConstraints {
  int minGreen;
  int maxGreen;
  int yellowDuration;
  int allRedDuration;
};

class AuctionController {
 public:
  AuctionController(std::string trafficLightId,
                    std::vector<PhaseBidGroup> phaseGroups,
                    AuctionWeights weights,
                    PhaseSafetyConstraints constraints,
                    std::vector<std::string> programPhaseStates);

  void setWeights(AuctionWeights weights);
  AuctionWeights weights() const;

  int selectWinningPhase() const;
  void applyPhaseIfDue(int currentSimStepSeconds);

 private:
  enum class TransitionState { None, Yellow, AllRed };

  double scorePhase(const PhaseBidGroup& group) const;
  int selectWinningPhaseExcluding(int excludedPhase) const;
  void beginTransition(int nextPhase, int currentSimStepSeconds);

  std::string trafficLightId_;
  std::vector<PhaseBidGroup> phaseGroups_;
  AuctionWeights weights_;
  int currentPhaseIndex_;
  int elapsedPhaseSeconds_;
  int minGreen_;
  int maxGreen_;
  int yellowDuration_;
  int allRedDuration_;
  std::vector<std::string> programPhaseStates_;
  TransitionState transitionState_;
  int pendingPhaseIndex_;
  int transitionAllRedIndex_;
  int lastPhaseChangeStep_;
};
