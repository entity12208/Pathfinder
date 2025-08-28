#pragma once
#include "gd_physics.hpp"

class GDObjectBehavior {
public:
    static void applyPortal(GDObject& portal, GDPlayerState& player);
    static void applyPad(GDObject& pad, GDPlayerState& player);
    static void applyOrb(GDObject& orb, GDPlayerState& player);
    static void applyTrigger(GDObject& trigger, GDPlayerState& player, GDLevel& level);
    static void applyCustom(GDObject& custom, GDPlayerState& player, GDLevel& level);

    // Add all new objects/mechanics here. Attempt to genericize logic for new/unknown objects.
};
