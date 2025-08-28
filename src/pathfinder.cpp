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

    std::random_device rd;
    std::mt19937 rng(rd());

    // Main simulation loop
    while (!stop) {
        std::vector<bool> input(level.objects.size(), false);
        // Randomly generate inputs, or use a smarter search (e.g. A*, genetic algorithms)
        for (auto& player : sim.players) {
            input[0] = rng() % 2; // Placeholder, replace with proper logic
        }
        sim.runFrame(input);

        // Save best run so far
        if (sim.players[0].x > bestFrame) {
            bestFrame = sim.players[0].x;
            bestReplay = sim.players;
        }

        // Progress callback
        if (callback) {
            float progress = std::min((sim.players[0].x / level.settings["levelLength"]) * 100, 100.0f);
            callback(progress);
        }

        // Check for completion
        if (sim.players[0].x >= level.settings["levelLength"])
            break;
    }

    // Export macro
    std::string macroString = MacroExporter::generateMacroString(bestReplay);
    return std::vector<uint8_t>(macroString.begin(), macroString.end());
}
