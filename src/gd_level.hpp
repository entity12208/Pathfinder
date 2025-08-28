#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <set>

// All GD player forms
enum class GDPlayerForm {
    Cube, Ship, Ball, UFO, Wave, Robot, Spider, Swing
};

// All supported object types
enum class GDObjectType {
    Block, Spike, Portal, Pad, Orb, Trigger, Coin, Decoration, Custom
};

// Full object definition, supporting all versions
struct GDObject {
    GDObjectType type;
    GDPlayerForm form;
    float x, y, rotation, scale;
    std::unordered_map<std::string, float> properties;
    int id;
    bool active;
};

// Level data supporting all GD versions
class GDLevel {
public:
    std::vector<GDObject> objects;
    std::unordered_map<std::string, float> settings;
    int version;
    GDLevel(const std::string& lvlString);
    void parse();
    std::vector<GDObject*> getObjectsAt(float x, float y);
    std::vector<GDObject*> getTriggersForFrame(int frame);
};
