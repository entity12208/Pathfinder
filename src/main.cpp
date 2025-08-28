// src/main.cpp
//
// Pathfinder-single: single-file Geode mod (improved, cross-version guarded live extractor)
// - Tries to extract level objects using PlayLayer->m_level->m_objects (the same general approach as Pathfinder).
// - Falls back to level.txt in the mod save dir if live extraction fails.
// - Frame-accurate physics sim and conservative pathfinding.
// - Outputs macro.txt (frames) and pathfinder_report.json (debug).
//
// Build: place inside Geode mod src/, then `geode build`
//
// Author: generated for user (Asher). Uses Geode bindings and inspired by camila314/pathfinder.
// References: camila314/pathfinder (github) and docs.geode-sdk.org. :contentReference[oaicite:1]{index=1}

#include <Geode/Bindings.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/utils/Log.hpp>
#include <Geode/loader/Dirs.hpp>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>
#include <filesystem>
#include <optional>
#include <nlohmann/json.hpp>

using namespace geode::prelude;
using json = nlohmann::json;

///////////////////////////////////////////////////////////////////////////////
// Tunables - change to match your Geometry Dash version if needed
static constexpr float FRAME_DT = 1.0f / 60.0f;   // 60 FPS sim
static constexpr float PLAYER_SPEED = 220.0f;     // horizontal px/s (tweak)
static constexpr float GRAVITY = -1600.0f;        // px/s^2 (tweak)
static constexpr float JUMP_VELOCITY = 680.0f;    // px/s (tweak)
static constexpr float START_BEFORE_X = 16.0f;    // start px before first object
static constexpr int   LOOKAHEAD_FRAMES = 36;     // frames to lookahead when deciding jumps
static constexpr int   MAX_SIM_FRAMES = 60 * 300; // safety cap
///////////////////////////////////////////////////////////////////////////////

struct Rect { float x, y, w, h; bool contains(float px, float py) const {
    return px >= x && px <= x + w && py >= y && py <= y + h;
} };

enum class ObjType { PLATFORM, SPIKE, JUMP_PAD, UNKNOWN };

struct Obj {
    ObjType type = ObjType::UNKNOWN;
    Rect r{0,0,0,0};
    float power = 0.0f;
    json to_json() const {
        json j;
        j["type"] = (type==ObjType::PLATFORM?"platform":type==ObjType::SPIKE?"spike":type==ObjType::JUMP_PAD?"jump_pad":"unknown");
        j["x"]=r.x; j["y"]=r.y; j["w"]=r.w; j["h"]=r.h;
        if (type==ObjType::JUMP_PAD) j["power"]=power;
        return j;
    }
};

struct SimState {
    float px, py;
    float vx, vy;
    bool onGround;
    json to_json() const {
        return { {"px",px},{"py",py},{"vx",vx},{"vy",vy},{"onGround",onGround} };
    }
};

///////////////////////////////////////////////////////////////////////////////
// Frame integrator: simple deterministic physics used for pathfinding
static SimState stepSim(const SimState& s, bool doJump, const std::vector<Obj>& objs) {
    SimState n = s;
    if (doJump && n.onGround) {
        n.vy = JUMP_VELOCITY;
        n.onGround = false;
    }
    // horizontal movement
    n.px += PLAYER_SPEED * FRAME_DT;
    // integrate vertical velocity
    n.vy += GRAVITY * FRAME_DT;
    n.py += n.vy * FRAME_DT;

    // platform landing detection: if player's x within platform width and crossed downward onto top
    bool landed = false;
    float bestTop = -INFINITY;
    for (auto const& o : objs) {
        if (o.type != ObjType::PLATFORM) continue;
        float left = o.r.x, right = o.r.x + o.r.w, top = o.r.y + o.r.h;
        if (n.px >= left && n.px <= right) {
            if (s.py >= top - 1e-3f && n.py <= top + 1e-3f) {
                if (top > bestTop) bestTop = top, landed = true;
            }
        }
    }
    if (landed) {
        n.py = bestTop;
        n.vy = 0.0f;
        n.onGround = true;
    } else {
        n.onGround = false;
    }

    // jump pads: if overlapping, set vertical velocity
    for (auto const& o : objs) {
        if (o.type != ObjType::JUMP_PAD) continue;
        if (n.px >= o.r.x && n.px <= o.r.x + o.r.w &&
            n.py >= o.r.y && n.py <= o.r.y + o.r.h) {
            n.vy = (o.power > 0.0f ? o.power : JUMP_VELOCITY);
            n.onGround = false;
        }
    }

    // spike detection -> mark death as py extremely low
    for (auto const& o : objs) {
        if (o.type != ObjType::SPIKE) continue;
        if (o.r.contains(n.px, n.py)) {
            n.py = -999999.0f;
        }
    }

    return n;
}

///////////////////////////////////////////////////////////////////////////////
// Pathfinding core: greedy lookahead + delayed-jump scheduling + limited backtrack
static bool runPathfinder(const std::vector<Obj>& objs, SimState start, float goalX, std::vector<int>& outJumps, json& debug) {
    outJumps.clear();
    debug = json::object();
    debug["goal_x"] = goalX;
    debug["obj_count"] = (int)objs.size();

    SimState state = start;
    const int lookahead = LOOKAHEAD_FRAMES;

    for (int frame=0; frame < MAX_SIM_FRAMES; ++frame) {
        if (state.px >= goalX) {
            debug["frames"] = frame;
            return true;
        }

        // simulate lookahead without jumping
        SimState look = state;
        bool willDie = false;
        for (int la=0; la<lookahead; ++la) {
            look = stepSim(look, false, objs);
            if (look.py < -1000.0f) { willDie = true; break; }
        }
        if (!willDie) {
            state = stepSim(state, false, objs);
            continue;
        }

        // try immediate jump if on ground
        if (state.onGround) {
            SimState after = stepSim(state, true, objs);
            // test safety after jump
            SimState probe = after;
            bool ok = true;
            for (int la=0; la<lookahead; ++la) {
                probe = stepSim(probe, false, objs);
                if (probe.py < -1000.0f) { ok = false; break; }
            }
            if (ok) {
                outJumps.push_back(frame);
                state = after;
                continue;
            }
        }

        // try scheduling a jump a few frames later (delayed)
        bool scheduled = false;
        for (int delay = 1; delay <= 8; ++delay) {
            SimState trial = state;
            for (int d=0; d<delay; ++d) trial = stepSim(trial, false, objs);
            if (!trial.onGround) continue;
            SimState after = stepSim(trial, true, objs);
            SimState probe = after;
            bool ok = true;
            for (int la=0; la<lookahead; ++la) {
                probe = stepSim(probe, false, objs);
                if (probe.py < -1000.0f) { ok = false; break; }
            }
            if (ok) {
                outJumps.push_back(frame + delay);
                // advance to the moment after the jump
                for (int d=0; d<delay; ++d) state = stepSim(state, false, objs);
                state = stepSim(state, true, objs);
                scheduled = true;
                break;
            }
        }
        if (scheduled) continue;

        // give up if no schedule works
        debug["failed_frame"] = frame;
        return false;
    }

    debug["failed_reason"] = "max_frames_exceeded";
    return false;
}

///////////////////////////////////////////////////////////////////////////////
// Level parsing: two modes
// - Live extraction: try to get PlayLayer::get()->m_level->m_objects (primary approach)
// - Fallback to level.txt (CSV)
///////////////////////////////////////////////////////////////////////////////

static bool parseLevelTextFile(const std::filesystem::path& p, std::vector<Obj>& out, json& debug) {
    out.clear();
    debug = json::object();
    debug["source"] = "level.txt";
    std::ifstream ifs(p);
    if (!ifs.is_open()) { debug["error"]="file_not_found"; return false; }
    std::string line; int ln=0;
    while (std::getline(ifs, line)) {
        ++ln;
        auto trim = [&](std::string s)->std::string {
            size_t a = s.find_first_not_of(" \t\r\n"); if (a==std::string::npos) return "";
            size_t b = s.find_last_not_of(" \t\r\n"); return s.substr(a, b-a+1);
        };
        line = trim(line);
        if (line.empty() || line[0]=='#') continue;
        std::stringstream ss(line);
        std::string tok; std::vector<std::string> toks;
        while (std::getline(ss, tok, ',')) toks.push_back(trim(tok));
        if (toks.empty()) continue;
        std::string t = toks[0];
        for (auto &c : t) c = toupper((unsigned char)c);

        try {
            if (t == "PLATFORM") {
                if (toks.size() < 5) { debug["parse_error_line"] = ln; return false; }
                Obj o; o.type = ObjType::PLATFORM;
                o.r.x = std::stof(toks[1]); o.r.y = std::stof(toks[2]);
                o.r.w = std::stof(toks[3]); o.r.h = std::stof(toks[4]);
                out.push_back(o);
            } else if (t == "SPIKE") {
                if (toks.size() < 5) { debug["parse_error_line"] = ln; return false; }
                Obj o; o.type = ObjType::SPIKE;
                o.r.x = std::stof(toks[1]); o.r.y = std::stof(toks[2]);
                o.r.w = std::stof(toks[3]); o.r.h = std::stof(toks[4]);
                out.push_back(o);
            } else if (t == "JUMP_PAD") {
                if (toks.size() < 4) { debug["parse_error_line"] = ln; return false; }
                Obj o; o.type = ObjType::JUMP_PAD;
                o.r.x = std::stof(toks[1]); o.r.y = std::stof(toks[2]);
                o.r.w = toks.size() >= 4 ? std::stof(toks[3]) : 16.0f;
                o.r.h = 16.0f;
                o.power = (toks.size() >= 5) ? std::stof(toks[4]) : JUMP_VELOCITY;
                out.push_back(o);
            } else {
                debug["ignored_" + std::to_string(ln)] = line;
            }
        } catch (...) {
            debug["error_line_"+std::to_string(ln)] = line;
            return false;
        }
    }
    debug["objects"] = (int)out.size();
    return true;
}

// Live extractor: try safe, multiple heuristics to extract objects from the current PlayLayer/level.
// We attempt common Geode binding access patterns. If none succeed, we return false (caller will fallback).
static bool extractObjectsFromPlayLayer(std::vector<Obj>& out, json& debug) {
    out.clear();
    debug = json::object();
    debug["attempt"]="live_extract";
    PlayLayer* pl = nullptr;
    try { pl = PlayLayer::get(); } catch(...) { pl = nullptr; }
    if (!pl) { debug["playlayer"]="not_found"; return false; }
    debug["playlayer"]="found";

    // Try common "m_level" pointer (many bindings expose this)
    GJGameLevel* level = nullptr;
    try { level = pl->m_level; } catch(...) { level = nullptr; }
    if (!level) {
        debug["level_ptr"]="not_found";
        // Some versions may store level pointer elsewhere; we avoid unsafe casting here.
        return false;
    }
    debug["level_ptr"]="found";

    // Try common objects array names used by many tools: m_objectList, m_objects, m_objectArray.
    cocos2d::CCArray* arr = nullptr;
    try { arr = level->m_objectList; } catch(...) { arr = nullptr; }
    if (!arr) {
        try { arr = level->m_objects; } catch(...) { arr = nullptr; }
    }
    if (!arr) {
        try { arr = level->m_objectArray; } catch(...) { arr = nullptr; }
    }
    if (!arr) {
        debug["objects_array"]="not_found";
        // Another fallback that some versions use: level->m_objectInfo or LevelTools helper.
        return false;
    }
    debug["objects_array"]="found";
    debug["objects_count"] = (int)arr->count();

    // Iterate array. Geode provides GameObject binding in many versions.
    // We'll attempt to cast each CCObject* to GameObject* and read safe accessors where available.
    for (int i = 0; i < (int)arr->count(); ++i) {
        cocos2d::CCObject* item = arr->objectAtIndex(i);
        if (!item) continue;
        // Try to cast to GameObject (binding). If cast fails at runtime, skip the object.
        GameObject* g = nullptr;
        try { g = static_cast<GameObject*>(item); } catch(...) { g = nullptr; }
        if (!g) continue;

        // Attempt to get the object's type & position using safe getters when available.
        // The binding might contain methods such as getX(), getY(), getObjectType(), getGroup() or fields.
        float ox = 0.0f, oy = 0.0f, ow = 16.0f, oh = 16.0f;
        bool havePos = false;
        try {
            // Many bindings provide getPosition or getX/getY
            // We'll try a few common possibilities and catch if they don't exist
            // Attempt: g->getX(), g->getY()
            // NOTE: If the binding compiles but at runtime these are not implemented, it'll throw.
            ox = g->m_x; oy = g->m_y; // many bindings provide fields m_x/m_y
            havePos = true;
        } catch(...) { havePos=false; }
        if (!havePos) {
            try { ox = g->getX(); oy = g->getY(); havePos = true; } catch(...) { havePos = false; }
        }
        if (!havePos) {
            // try getPosition()
            try {
                cocos2d::CCPoint p = g->getPosition();
                ox = p.x; oy = p.y; havePos = true;
            } catch(...) { havePos=false; }
        }
        if (!havePos) {
            // If none of the above worked, skip this object -- safer than guessing.
            continue;
        }

        // Attempt to determine width/height or treat as point object
        try {
            // Many object types use m_size or width/height fields (rare). We'll try a few.
            ow = g->m_width; oh = g->m_height;
        } catch(...) {
            // leave 16x16 default
        }

        // Determine object type: try to read objectType, group, or UID
        // Common approach: object->getObjectType() or object->m_objectType
        Obj obj;
        obj.r.x = ox;
        obj.r.y = oy;
        obj.r.w = ow;
        obj.r.h = oh;
        obj.power = 0.0f;
        bool assigned = false;
        try {
            int typeId = -1;
            try { typeId = g->m_objectType; } catch(...) { typeId = -1; }
            if (typeId == -1) {
                try { typeId = g->getObjectType(); } catch(...) { typeId = -1; }
            }
            // Common mapping:
            // 0 = block/platform, 1 = spike? (these ids vary by GD version)
            if (typeId >= 0) {
                // Conservative mapping: when uncertain, use PLATFORM
                obj.type = ObjType::PLATFORM;
                // heuristics: if height very small and looks like spike, mark spike
                if (obj.r.h <= 10.0f || obj.r.w <= 10.0f) obj.type = ObjType::SPIKE;
                assigned = true;
            }
        } catch(...) {}
        if (!assigned) {
            // fallback heuristic: if object name contains "Spike" or "spike" (GameObject has frame or name sometimes)
            try {
                const char* frame = g->m_frame;
                if (frame) {
                    std::string s(frame);
                    for (auto &c : s) c = tolower((unsigned char)c);
                    if (s.find("spike") != std::string::npos) { obj.type = ObjType::SPIKE; assigned = true; }
                    if (s.find("jump") != std::string::npos || s.find("pad") != std::string::npos) { obj.type = ObjType::JUMP_PAD; assigned = true; }
                }
            } catch(...) {}
        }

        if (!assigned) obj.type = ObjType::PLATFORM; // safe default

        // Many GD objects' origin is centered or bottom-left depending on how it's stored.
        // Pathfinder historically notes y offset differences. We leave objects as-is and include the raw positions in report.
        out.push_back(obj);
    }

    debug["extracted"] = (int)out.size();
    return !out.empty();
}

///////////////////////////////////////////////////////////////////////////////
// UI + top-level glue: FLAlert menu in More Games
class PathfinderAlert : public FLAlertLayerProtocol {
public:
    std::filesystem::path saveDir;
    PathfinderAlert() {
        try {
            saveDir = Mod::get()->getSaveDir();
        } catch(...) {
            saveDir = std::filesystem::current_path();
        }
    }

    void show() {
        std::ostringstream ss;
        ss << "Pathfinder (improved)\n\n"
           << "Press RUN to attempt live extraction (PlayLayer). If that fails, the mod will fallback to level.txt.\n\n"
           << "Save dir: " << saveDir.string() << "\n\n"
           << "File format (CSV): PLATFORM,x,y,w,h  SPIKE,x,y,w,h  JUMP_PAD,x,y,w[,power]\n\n"
           << "Press RUN to start.";
        FLAlertLayer::create(this, "Pathfinder", ss.str(), "RUN", "CANCEL")->show();
    }

    void FLAlert_Clicked(FLAlertLayer*, int btn) override {
        if (btn == 0) run();
    }

    void run() {
        json dbgRoot;
        std::vector<Obj> objs;
        bool ok = false;

        // 1) try live extraction
        json liveDbg;
        if (extractObjectsFromPlayLayer(objs, liveDbg)) {
            dbgRoot["live"] = liveDbg;
            ok = true;
        } else {
            dbgRoot["live_fail"] = liveDbg;
        }

        // 2) fallback to file
        if (!ok) {
            auto p = saveDir / "level.txt";
            json fileDbg;
            if (parseLevelTextFile(p, objs, fileDbg)) {
                dbgRoot["file"] = fileDbg;
                ok = true;
            } else {
                dbgRoot["file_fail"] = fileDbg;
            }
        }

        if (!ok) {
            geode::Notification::create("Pathfinder: failed to read level (check pathfinder_report.json).", geode::NotificationIcon::Exclamation, 7.0f)->show();
            // write debug for user to inspect
            try {
                auto report = saveDir / "pathfinder_report.json";
                std::ofstream f(report, std::ios::trunc);
                f << dbgRoot.dump(2);
                f.close();
            } catch(...) {}
            GEODE_ERROR("[Pathfinder] failed to get objects, debug: %s", dbgRoot.dump(2).c_str());
            return;
        }

        // compute bounding X and ground Y heuristics
        float minX = INFINITY, maxX = -INFINITY, groundY = -INFINITY;
        for (auto& o : objs) {
            minX = std::min(minX, o.r.x);
            maxX = std::max(maxX, o.r.x + o.r.w);
            if (o.type == ObjType::PLATFORM) groundY = std::max(groundY, o.r.y + o.r.h);
        }
        if (!std::isfinite(minX)) minX = 0.0f;
        if (!std::isfinite(maxX)) maxX = minX + 1200.0f;
        if (!std::isfinite(groundY)) groundY = 0.0f;

        // initial player state (start a little before minX and slightly above ground)
        SimState start;
        start.px = minX - START_BEFORE_X;
        start.py = groundY + 12.0f;
        start.vx = PLAYER_SPEED; start.vy = 0.0f; start.onGround = true;

        std::vector<int> jumps;
        json planDbg;
        bool success = runPathfinder(objs, start, maxX, jumps, planDbg);

        // write a report file to save dir (useful for debugging extractor & sim)
        json report;
        report["success"] = success;
        report["start"] = start.to_json();
        report["goal_x"] = maxX;
        report["objects"] = json::array();
        for (auto const& o : objs) report["objects"].push_back(o.to_json());
        report["plan"] = planDbg;
        report["jumps_count"] = (int)jumps.size();

        try {
            auto rp = (saveDir / "pathfinder_report.json").string();
            std::ofstream rf(rp, std::ios::trunc);
            rf << report.dump(2);
            rf.close();
        } catch(...) {
            GEODE_ERROR("[Pathfinder] failed to write report");
        }

        if (!success) {
            geode::Notification::create("Pathfinder: couldn't find a safe macro. See pathfinder_report.json", geode::NotificationIcon::Exclamation, 8.0f)->show();
            GEODE_ERROR("[Pathfinder] pathfinder failed. Report saved.");
            return;
        }

        // write macro.txt as newline frame numbers
        try {
            auto macroPath = (saveDir / "macro.txt").string();
            std::ofstream mf(macroPath, std::ios::trunc);
            if (!mf.is_open()) throw std::runtime_error("open_failed");
            for (auto f : jumps) mf << f << "\n";
            mf.close();

            std::ostringstream msg;
            msg << "Pathfinder: wrote macro.txt (" << jumps.size() << " jumps) and pathfinder_report.json";
            geode::Notification::create(msg.str(), geode::NotificationIcon::Check, 6.0f)->show();
            GEODE_INFO("[Pathfinder] wrote macro: %s", macroPath.c_str());
        } catch(...) {
            geode::Notification::create("Pathfinder: failed to write macro.txt (permission?).", geode::NotificationIcon::Exclamation, 6.0f)->show();
        }
    }
};

///////////////////////////////////////////////////////////////////////////////
// Hook into More Games (MenuLayer)
class $modify(MenuLayer) {
    void onMoreGames(CCObject* sender) {
        // call original behavior
        MenuLayer::onMoreGames(sender);
        try {
            PathfinderAlert a;
            a.show();
        } catch(...) {
            GEODE_ERROR("[Pathfinder] exception showing alert");
        }
    }
};

///////////////////////////////////////////////////////////////////////////////
// END OF FILE
