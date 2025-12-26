#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <libtraci/libtraci.h>

#include "auction_controller.hpp"

namespace {

struct TrafficLightConfig {
  std::string id;
  std::string phaseMap;
  bool optimize = false;
  bool optimizeSet = false;
};

struct Options {
  std::string sumoConfig;
  std::string trafficLightId;
  std::string phaseMap;
  std::vector<TrafficLightConfig> trafficLights;
  bool gui = false;
  int stepLength = 1;
  int phaseDuration = 10;
  double queueWeight = 1.0;
  double waitWeight = 0.05;
  bool optimize = false;
  int optimizeWindow = 60;
  double optimizeDelta = 0.2;
};

struct OptionFlags {
  bool sumoConfig = false;
  bool trafficLightId = false;
  bool phaseMap = false;
  bool trafficLights = false;
  bool gui = false;
  bool stepLength = false;
  bool phaseDuration = false;
  bool queueWeight = false;
  bool waitWeight = false;
  bool optimize = false;
  bool optimizeWindow = false;
  bool optimizeDelta = false;
};

std::string trim(std::string value) {
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch) {
                return !std::isspace(ch);
              }));
  value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) {
                return !std::isspace(ch);
              }).base(),
              value.end());
  return value;
}

std::vector<std::string> split(const std::string& input, char delimiter) {
  std::vector<std::string> parts;
  std::stringstream stream(input);
  std::string item;
  while (std::getline(stream, item, delimiter)) {
    item = trim(item);
    if (!item.empty()) {
      parts.push_back(item);
    }
  }
  return parts;
}

bool parseBool(const std::string& value) {
  std::string lowered = value;
  std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return lowered == "true" || lowered == "1" || lowered == "yes" || lowered == "on";
}

std::string stripInlineComment(const std::string& value) {
  bool inQuotes = false;
  for (size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '"') {
      inQuotes = !inQuotes;
    } else if (value[i] == '#' && !inQuotes) {
      return value.substr(0, i);
    }
  }
  return value;
}

std::vector<PhaseBidGroup> parsePhaseMap(const std::string& mapping) {
  std::vector<PhaseBidGroup> groups;
  if (mapping.empty()) {
    return groups;
  }

  for (const auto& groupToken : split(mapping, ';')) {
    auto parts = split(groupToken, ':');
    if (parts.size() != 2) {
      continue;
    }

    int phaseIndex = std::stoi(parts[0]);
    auto lanes = split(parts[1], ',');
    if (!lanes.empty()) {
      groups.push_back(PhaseBidGroup{phaseIndex, lanes});
    }
  }

  return groups;
}

Options loadYamlConfig(const std::string& path, OptionFlags& flags) {
  Options options;
  std::ifstream file(path);
  if (!file) {
    std::cerr << "Failed to open config file: " << path << "\n";
    return options;
  }

  std::string line;
  bool inTrafficLights = false;
  bool hasCurrentTrafficLight = false;
  TrafficLightConfig currentTrafficLight;
  auto finalizeTrafficLight = [&]() {
    if (!hasCurrentTrafficLight) {
      return;
    }
    if (!currentTrafficLight.id.empty() || !currentTrafficLight.phaseMap.empty()) {
      options.trafficLights.push_back(currentTrafficLight);
      flags.trafficLights = true;
    }
    currentTrafficLight = TrafficLightConfig{};
    hasCurrentTrafficLight = false;
  };

  while (std::getline(file, line)) {
    std::string rawLine = line;
    line = trim(line);
    if (line.empty() || line.rfind('#', 0) == 0) {
      continue;
    }

    if (line.rfind("traffic_lights", 0) == 0 && line.find(':') != std::string::npos) {
      finalizeTrafficLight();
      inTrafficLights = true;
      continue;
    }

    auto pos = line.find(':');
    if (pos == std::string::npos) {
      continue;
    }

    std::string key = trim(line.substr(0, pos));
    std::string value = trim(stripInlineComment(line.substr(pos + 1)));
    if (inTrafficLights) {
      if (line.rfind('-', 0) == 0) {
        finalizeTrafficLight();
        hasCurrentTrafficLight = true;
        std::string remainder = trim(line.substr(1));
        if (!remainder.empty()) {
          auto itemPos = remainder.find(':');
          if (itemPos != std::string::npos) {
            std::string itemKey = trim(remainder.substr(0, itemPos));
            std::string itemValue = trim(stripInlineComment(remainder.substr(itemPos + 1)));
            if (!itemValue.empty() && itemValue.front() == '"' && itemValue.back() == '"') {
              itemValue = itemValue.substr(1, itemValue.size() - 2);
            }
            if (itemKey == "id" || itemKey == "traffic_light_id") {
              currentTrafficLight.id = itemValue;
            } else if (itemKey == "phase_map") {
              currentTrafficLight.phaseMap = itemValue;
            } else if (itemKey == "optimize") {
              currentTrafficLight.optimize = parseBool(itemValue);
              currentTrafficLight.optimizeSet = true;
            }
          }
        }
        continue;
      }

      if (key == "id" || key == "traffic_light_id" || key == "phase_map" || key == "optimize") {
        if (!hasCurrentTrafficLight) {
          hasCurrentTrafficLight = true;
        }
        if (!value.empty() && value.front() == '"' && value.back() == '"') {
          value = value.substr(1, value.size() - 2);
        }
        if (key == "id" || key == "traffic_light_id") {
          currentTrafficLight.id = value;
        } else if (key == "phase_map") {
          currentTrafficLight.phaseMap = value;
        } else if (key == "optimize") {
          currentTrafficLight.optimize = parseBool(value);
          currentTrafficLight.optimizeSet = true;
        }
        continue;
      }

      finalizeTrafficLight();
      inTrafficLights = false;
    }

    if (value.empty()) {
      std::cerr << "Empty value for key: " << key << "\n";
      continue;
    }
    if (!value.empty() && value.front() == '"' && value.back() == '"') {
      value = value.substr(1, value.size() - 2);
    }

    try {
      if (key == "sumo_config") {
        options.sumoConfig = value;
        flags.sumoConfig = true;
      } else if (key == "traffic_light_id") {
        options.trafficLightId = value;
        flags.trafficLightId = true;
      } else if (key == "phase_map") {
        options.phaseMap = value;
        flags.phaseMap = true;
      } else if (key == "traffic_lights") {
        flags.trafficLights = true;
      } else if (key == "gui") {
        options.gui = parseBool(value);
        flags.gui = true;
      } else if (key == "step_length") {
        options.stepLength = std::stoi(value);
        flags.stepLength = true;
      } else if (key == "phase_duration") {
        options.phaseDuration = std::stoi(value);
        flags.phaseDuration = true;
      } else if (key == "queue_weight") {
        options.queueWeight = std::stod(value);
        flags.queueWeight = true;
      } else if (key == "wait_weight") {
        options.waitWeight = std::stod(value);
        flags.waitWeight = true;
      } else if (key == "optimize") {
        options.optimize = parseBool(value);
        flags.optimize = true;
      } else if (key == "optimize_window") {
        options.optimizeWindow = std::stoi(value);
        flags.optimizeWindow = true;
      } else if (key == "optimize_delta") {
        options.optimizeDelta = std::stod(value);
        flags.optimizeDelta = true;
      }
    } catch (const std::exception& ex) {
      std::cerr << "Invalid value for key '" << key << "': " << value << " (" << ex.what()
                << ")\n";
    }
  }

  finalizeTrafficLight();
  return options;
}

double computeObjective(const std::vector<PhaseBidGroup>& groups) {
  double total = 0.0;
  for (const auto& group : groups) {
    for (const auto& laneId : group.lanes) {
      total += libtraci::lane::getWaitingTime(laneId);
      total += libtraci::lane::getLastStepVehicleNumber(laneId);
    }
  }
  return total;
}

void printUsage() {
  std::cout
      << "Usage: auction_tsc --sumo-config <cfg.sumocfg> --tl-id <traffic_light_id> --phase-map "
         "\"0:laneA,laneB;1:laneC,laneD\" [options]\n"
         "       auction_tsc --sumo-config <cfg.sumocfg> --tl-id <id1> --phase-map "
         "\"0:laneA,laneB\" --tl-id <id2> --phase-map \"0:laneC\" [options]\n"
         "       auction_tsc --sumo-config <cfg.sumocfg> --phase-map \"tl1:0:laneA\" "
         "--phase-map \"tl2:0:laneB\" [options]\n"
         "Options:\n"
         "  --config <file.yaml>         Load options from a yaml file\n"
         "  --tl-id <id>                 Traffic light id (repeatable)\n"
         "  --phase-map <map>            Phase map (repeatable, supports tl_id:phase_map)\n"
         "  --gui                       Run with sumo-gui\n"
         "  --step-length <seconds>      Simulation step length (default: 1)\n"
         "  --phase-duration <seconds>   Duration to hold a phase (default: 10)\n"
         "  --queue-weight <float>       Weight for queue length (default: 1.0)\n"
         "  --wait-weight <float>        Weight for waiting time (default: 0.05)\n"
         "  --optimize                   Enable simple weight tuning\n"
         "  --optimize-window <steps>    Steps to evaluate each candidate (default: 60)\n"
         "  --optimize-delta <float>     Weight adjustment step (default: 0.2)\n";
}

bool validateOptions(const Options& options) {
  bool ok = true;
  if (options.sumoConfig.empty()) {
    std::cerr << "Missing required --sumo-config.\n";
    ok = false;
  }
  if (options.trafficLights.empty()) {
    std::cerr << "Missing required traffic light configuration. Provide --tl-id and --phase-map.\n";
    ok = false;
  }
  if (options.stepLength <= 0) {
    std::cerr << "--step-length must be positive.\n";
    ok = false;
  }
  if (options.phaseDuration <= 0) {
    std::cerr << "--phase-duration must be positive.\n";
    ok = false;
  }
  if (options.queueWeight < 0.0 || options.waitWeight < 0.0) {
    std::cerr << "Weights must be non-negative.\n";
    ok = false;
  }
  if (options.optimize) {
    if (options.optimizeWindow <= 0) {
      std::cerr << "--optimize-window must be positive when optimization is enabled.\n";
      ok = false;
    }
    if (options.optimizeDelta < 0.0) {
      std::cerr << "--optimize-delta must be non-negative when optimization is enabled.\n";
      ok = false;
    }
  }
  for (const auto& trafficLight : options.trafficLights) {
    if (trafficLight.id.empty()) {
      std::cerr << "Traffic light entry missing id.\n";
      ok = false;
    }
    if (trafficLight.phaseMap.empty()) {
      std::cerr << "Traffic light entry missing phase map for id: " << trafficLight.id << "\n";
      ok = false;
    }
  }
  return ok;
}

bool isAllDigits(const std::string& value) {
  return !value.empty() &&
         std::all_of(value.begin(), value.end(), [](unsigned char ch) { return std::isdigit(ch); });
}

Options parseArgs(int argc, char** argv) {
  Options options;
  std::string configPath;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--config" && i + 1 < argc) {
      configPath = argv[++i];
    }
  }

  OptionFlags yamlFlags;
  if (!configPath.empty()) {
    Options yamlOptions = loadYamlConfig(configPath, yamlFlags);
    if (yamlFlags.sumoConfig) {
      options.sumoConfig = yamlOptions.sumoConfig;
    }
    if (yamlFlags.trafficLights) {
      options.trafficLights = yamlOptions.trafficLights;
    }
    if (yamlFlags.trafficLightId) {
      options.trafficLightId = yamlOptions.trafficLightId;
    }
    if (yamlFlags.phaseMap) {
      options.phaseMap = yamlOptions.phaseMap;
    }
    if (yamlFlags.gui) {
      options.gui = yamlOptions.gui;
    }
    if (yamlFlags.stepLength) {
      options.stepLength = yamlOptions.stepLength;
    }
    if (yamlFlags.phaseDuration) {
      options.phaseDuration = yamlOptions.phaseDuration;
    }
    if (yamlFlags.queueWeight) {
      options.queueWeight = yamlOptions.queueWeight;
    }
    if (yamlFlags.waitWeight) {
      options.waitWeight = yamlOptions.waitWeight;
    }
    if (yamlFlags.optimize) {
      options.optimize = yamlOptions.optimize;
    }
    if (yamlFlags.optimizeWindow) {
      options.optimizeWindow = yamlOptions.optimizeWindow;
    }
    if (yamlFlags.optimizeDelta) {
      options.optimizeDelta = yamlOptions.optimizeDelta;
    }
  }

  auto assignPhaseMap = [&](const std::string& mapValue) {
    auto pos = mapValue.find(':');
    if (pos != std::string::npos) {
      std::string prefix = mapValue.substr(0, pos);
      auto existing = std::find_if(options.trafficLights.begin(),
                                   options.trafficLights.end(),
                                   [&](const TrafficLightConfig& entry) {
                                     return entry.id == prefix;
                                   });
      if (existing != options.trafficLights.end()) {
        existing->phaseMap = mapValue.substr(pos + 1);
        return;
      }
      if (!isAllDigits(prefix)) {
        options.trafficLights.push_back(
            TrafficLightConfig{prefix, mapValue.substr(pos + 1), options.optimize});
        return;
      }
    }

    for (auto it = options.trafficLights.rbegin(); it != options.trafficLights.rend(); ++it) {
      if (it->phaseMap.empty()) {
        it->phaseMap = mapValue;
        return;
      }
    }

    options.phaseMap = mapValue;
  };

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--sumo-config" && i + 1 < argc) {
      options.sumoConfig = argv[++i];
    } else if (arg == "--tl-id" && i + 1 < argc) {
      options.trafficLights.push_back(
          TrafficLightConfig{argv[++i], std::string{}, options.optimize});
    } else if (arg == "--phase-map" && i + 1 < argc) {
      assignPhaseMap(argv[++i]);
    } else if (arg == "--gui") {
      options.gui = true;
    } else if (arg == "--step-length" && i + 1 < argc) {
      options.stepLength = std::stoi(argv[++i]);
    } else if (arg == "--phase-duration" && i + 1 < argc) {
      options.phaseDuration = std::stoi(argv[++i]);
    } else if (arg == "--queue-weight" && i + 1 < argc) {
      options.queueWeight = std::stod(argv[++i]);
    } else if (arg == "--wait-weight" && i + 1 < argc) {
      options.waitWeight = std::stod(argv[++i]);
    } else if (arg == "--optimize") {
      options.optimize = true;
    } else if (arg == "--optimize-window" && i + 1 < argc) {
      options.optimizeWindow = std::stoi(argv[++i]);
    } else if (arg == "--optimize-delta" && i + 1 < argc) {
      options.optimizeDelta = std::stod(argv[++i]);
    }
  }

  if (!options.trafficLightId.empty() || !options.phaseMap.empty()) {
    options.trafficLights.push_back(
        TrafficLightConfig{options.trafficLightId, options.phaseMap, options.optimize});
  }

  for (auto& trafficLight : options.trafficLights) {
    if (!trafficLight.optimizeSet) {
      trafficLight.optimize = options.optimize;
    }
  }
  return options;
}

class SimpleOptimizer {
 public:
  SimpleOptimizer(std::vector<PhaseBidGroup> groups, int windowSteps, double delta)
      : groups_(std::move(groups)),
        windowSteps_(std::max(1, windowSteps)),
        delta_(delta),
        lastWindowStep_(0),
        bestObjective_(std::numeric_limits<double>::infinity()),
        candidateIndex_(0),
        lastWeights_({1.0, 0.05}),
        usingCandidate_(false) {}

  void initialize(const AuctionWeights& weights, int currentStep) {
    lastWeights_ = weights;
    lastWindowStep_ = currentStep;
    bestObjective_ = computeObjective(groups_);
  }

  void step(int currentStep, AuctionController& controller) {
    if ((currentStep - lastWindowStep_) < windowSteps_) {
      return;
    }

    double objective = computeObjective(groups_);
    AuctionWeights currentWeights = controller.weights();

    if (usingCandidate_) {
      if (objective < bestObjective_) {
        bestObjective_ = objective;
        lastWeights_ = currentWeights;
      } else {
        controller.setWeights(lastWeights_);
      }
      usingCandidate_ = false;
    }

    AuctionWeights nextWeights = lastWeights_;
    switch (candidateIndex_ % 4) {
      case 0:
        nextWeights.queueWeight = std::max(0.0, lastWeights_.queueWeight + delta_);
        break;
      case 1:
        nextWeights.queueWeight = std::max(0.0, lastWeights_.queueWeight - delta_);
        break;
      case 2:
        nextWeights.waitWeight = std::max(0.0, lastWeights_.waitWeight + delta_);
        break;
      default:
        nextWeights.waitWeight = std::max(0.0, lastWeights_.waitWeight - delta_);
        break;
    }
    candidateIndex_++;

    controller.setWeights(nextWeights);
    usingCandidate_ = true;
    lastWindowStep_ = currentStep;
  }

 private:
  std::vector<PhaseBidGroup> groups_;
  int windowSteps_;
  double delta_;
  int lastWindowStep_;
  double bestObjective_;
  int candidateIndex_;
  AuctionWeights lastWeights_;
  bool usingCandidate_;
};

}  // namespace

int main(int argc, char** argv) {
  Options options = parseArgs(argc, argv);

  if (!validateOptions(options)) {
    printUsage();
    return 1;
  }

  struct ControllerState {
    AuctionController controller;
    std::optional<SimpleOptimizer> optimizer;
    bool optimizeEnabled = false;
  };

  std::vector<ControllerState> controllers;
  controllers.reserve(options.trafficLights.size());
  for (const auto& trafficLight : options.trafficLights) {
    auto phaseGroups = parsePhaseMap(trafficLight.phaseMap);
    if (phaseGroups.empty()) {
      std::cerr << "Invalid --phase-map for traffic light '" << trafficLight.id
                << "'. Provide at least one phase mapping.\n";
      return 1;
    }

    auto phaseGroupsForOptimizer = phaseGroups;
    controllers.push_back(ControllerState{
        AuctionController(trafficLight.id,
                          std::move(phaseGroups),
                          AuctionWeights{options.queueWeight, options.waitWeight},
                          options.phaseDuration),
        std::nullopt,
        trafficLight.optimize});
    if (trafficLight.optimize) {
      controllers.back().optimizer = SimpleOptimizer(
          std::move(phaseGroupsForOptimizer), options.optimizeWindow, options.optimizeDelta);
    }
  }

  std::vector<std::string> sumoCmd = {options.gui ? "sumo-gui" : "sumo",
                                      "-c",
                                      options.sumoConfig,
                                      "--step-length",
                                      std::to_string(options.stepLength),
                                      "--quit-on-end"};

  libtraci::start(sumoCmd);

  int stepIndex = 0;
  for (auto& state : controllers) {
    if (state.optimizeEnabled && state.optimizer) {
      state.optimizer->initialize(state.controller.weights(), stepIndex);
    }
  }

  while (libtraci::simulation::getMinExpectedNumber() > 0) {
    libtraci::simulation::step();
    int currentTime = static_cast<int>(libtraci::simulation::getTime());
    for (auto& state : controllers) {
      if (state.optimizeEnabled && state.optimizer) {
        state.optimizer->step(stepIndex, state.controller);
      }
      state.controller.applyPhaseIfDue(currentTime);
    }
    ++stepIndex;
  }

  libtraci::close();
  return 0;
}
