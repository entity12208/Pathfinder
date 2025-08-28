#pragma once
#include <string>
#include <vector>
#include "gd_physics.hpp"

class MacroExporter {
public:
    static bool exportMacro(const std::string& filename, const std::vector<GDPlayerState>& replay, int version);
    static std::string generateMacroString(const std::vector<GDPlayerState>& replay);
    // Add compatibility checks, error handling, and reporting
};
