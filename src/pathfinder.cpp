#include "gd_level.hpp"
#include "gd_physics.hpp"
#include "gd_object_behaviors.hpp"
#include "macro_export.hpp"

#include <random>
#include <atomic>
#include <functional>
#include <algorithm>
#include <string>
#include <vector>

std::vector<uint8_t> pathfind(const std::string& lvlString, std::atomic_bool& stop, std::function<void(double)> callback) {
    GDLevel level(lvlString);
    level.parse();

    GDPhysicsSimulator sim(&level);

    std::vector<GDPlayerState> bestReplay;
    int bestFrame = 0;
    std::vector<GDPlayerState> replayHistory;

    std::random_device rd;
    std::mt19937 rng(rd());

    float levelLength = level.settings.count("levelLength") ? level.settings["levelLength"] : 1.0f;
    if (levelLength <= 0.0f) levelLength = 1.0f;

    while (!stop) {
        if (level.objects.empty() || sim.players.empty())
            break;

        std::vector<bool> input(level.objects.size(), false);
        for (size_t i = 0; i < input.size(); ++i) {
            input[i] = rng() % 2;
        }
        sim.runFrame(input);

        // Store replay history if needed
        replayHistory.insert(replayHistory.end(), sim.players.begin(), sim.players.end());

        if (!sim.players.empty() && sim.players[0].x > bestFrame) {
            bestFrame = sim.players[0].x;
            bestReplay = sim.players;
        }

        if (callback && !sim.players.empty()) {
            float progress = std::min((sim.players[0].x / levelLength) * 100, 100.0f);
            callback(progress);
        }

        if (!sim.players.empty() && sim.players[0].x >= levelLength)
            break;
    }

    std::string macroString = MacroExporter::generateMacroString(bestReplay); // or replayHistory, if full replay needed
    return std::vector<uint8_t>(macroString.begin(), macroString.end());
}
