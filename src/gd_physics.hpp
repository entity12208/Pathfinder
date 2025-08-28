#pragma once
#include "gd_level.hpp"
#include <vector>
#include <memory>
#include <set>

// Player state for duals, all forms, all mechanics
struct GDPlayerState {
    float x, y, vx, vy;
    GDPlayerForm mode;
    bool alive;
    bool dual;
    int speed;
    float gravity;
    int frame;
    bool pressing;
    // Add more for custom modes, rotation, etc.
};

class GDPhysicsSimulator {
public:
    GDLevel* level;
    std::vector<GDPlayerState> players;
    GDPhysicsSimulator(GDLevel* lvl);

    // Run a frame, applying all game mechanics
    void runFrame(std::vector<bool> input);

    // Handle all object interactions
    void handleObject(GDObject& obj, GDPlayerState& player);

    // Rollback, state recording, and fast-forward
    void rollback(int frame);
    void saveState();
    void restoreState();
};
