\
// main.cpp - Pathfinder Single-file Geode mod
// Drop into a Geode mod's src/ and build with geode build
//
// Behavior:
//  - Adds a "Pathfinder" popup accessible from the More Games menu.
//  - Attempts to extract level objects from PlayLayer->m_level->m_objects (guarded).
//  - Falls back to reading a CSV level file at Mod::get()->getSaveDir()/level.txt
//  - Runs a deterministic frame-based simulator and outputs macro.txt and pathfinder_report.txt
//
// This mod uses only Geode's documented APIs (MenuLayer modify, FLAlertLayer, Mod::get()->getSaveDir).
// If live extraction fails on your GD version, create level.txt in the save dir (format described below).
//
// Level file format (level.txt):
//   PLATFORM,x,y,w,h
//   SPIKE,x,y,w,h
//   JUMP_PAD,x,y,w,h[,power]
//
// Output:
//   macro.txt   - newline-separated frame numbers to press jump
//   pathfinder_report.txt - human-readable debug info
//
// Tune physics constants below to match your GD version if needed.

#include <Geode/Bindings.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/utils/Log.hpp>
#include <Geode/loader/Dirs.hpp>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>
#include <filesystem>
#include <iomanip>

using namespace geode::prelude;

static constexpr float FRAME_DT = 1.0f / 60.0f;   // 60 FPS
static constexpr float PLAYER_SPEED = 220.0f;     // px/s
static constexpr float GRAVITY = -1600.0f;        // px/s^2
static constexpr float JUMP_VELOCITY = 680.0f;    // px/s
static constexpr float START_BEFORE_X = 16.0f;
static constexpr int LOOKAHEAD = 36;
static constexpr int MAX_FRAMES = 60 * 300;

struct Rect { float x, y, w, h; bool contains(float px, float py) const {
    return px >= x && px <= x + w && py >= y && py <= y + h;
} };

enum class ObjType { PLATFORM, SPIKE, JUMP_PAD, UNKNOWN };

struct Obj {
    ObjType type = ObjType::UNKNOWN;
    Rect r{0,0,0,0};
    float power = 0.0f;
};

struct SimState {
    float px, py;
    float vx, vy;
    bool onGround;
};

static SimState stepSim(const SimState& s, bool doJump, const std::vector<Obj>& objs) {
    SimState n = s;
    if (doJump && n.onGround) {
        n.vy = JUMP_VELOCITY;
        n.onGround = false;
    }
    n.px += PLAYER_SPEED * FRAME_DT;
    n.vy += GRAVITY * FRAME_DT;
    n.py += n.vy * FRAME_DT;

    bool landed = false;
    float bestTop = -INFINITY;
    for (auto const& o : objs) {
        if (o.type != ObjType::PLATFORM) continue;
        if (n.px >= o.r.x && n.px <= o.r.x + o.r.w) {
            float top = o.r.y + o.r.h;
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

    for (auto const& o : objs) {
        if (o.type != ObjType::JUMP_PAD) continue;
        if (n.px >= o.r.x && n.px <= o.r.x + o.r.w &&
            n.py >= o.r.y && n.py <= o.r.y + o.r.h) {
            n.vy = (o.power > 0.0f ? o.power : JUMP_VELOCITY);
            n.onGround = false;
        }
    }

    for (auto const& o : objs) {
        if (o.type != ObjType::SPIKE) continue;
        if (o.r.contains(n.px, n.py)) {
            n.py = -999999.0f;
        }
    }

    return n;
}

static bool runPathfinder(const std::vector<Obj>& objs, SimState start, float goalX, std::vector<int>& outJumps, std::string& report) {
    outJumps.clear();
    std::ostringstream rep;
    rep << "Pathfinder run\n";
    rep << "Objects: " << objs.size() << "\n";
    SimState state = start;
    const int lookahead = LOOKAHEAD;
    for (int frame=0; frame<MAX_FRAMES; ++frame) {
        if (state.px >= goalX) {
            rep << "Success at frame " << frame << "\n";
            report = rep.str();
            return true;
        }
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
        if (state.onGround) {
            SimState after = stepSim(state, true, objs);
            SimState probe = after;
            bool ok = true;
            for (int la=0; la<lookahead; ++la) {
                probe = stepSim(probe, false, objs);
                if (probe.py < -1000.0f) { ok = false; break; }
            }
            if (ok) {
                outJumps.push_back(frame);
                rep << "Jump at frame " << frame << "\n";
                state = after;
                continue;
            }
        }
        bool scheduled = false;
        for (int delay=1; delay<=8; ++delay) {
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
                rep << "Delayed jump at frame " << frame + delay << "\n";
                for (int d=0; d<delay; ++d) state = stepSim(state, false, objs);
                state = stepSim(state, true, objs);
                scheduled = true;
                break;
            }
        }
        if (scheduled) continue;
        rep << "Failed at frame " << frame << "\n";
        report = rep.str();
        return false;
    }
    rep << "Failed: max frames exceeded\n";
    report = rep.str();
    return false;
}

// parse level.txt fallback
static bool parseLevelFile(const std::filesystem::path& p, std::vector<Obj>& out, std::string& dbg) {
    out.clear();
    std::ifstream ifs(p);
    if (!ifs.is_open()) { dbg = "file not found"; return false; }
    std::string line; int ln=0;
    std::ostringstream dbgoss;
    while (std::getline(ifs, line)) {
        ++ln;
        auto trim = [&](std::string s)->std::string {
            size_t a = s.find_first_not_of(" \\t\\r\\n");
            if (a==std::string::npos) return "";
            size_t b = s.find_last_not_of(" \\t\\r\\n");
            return s.substr(a, b-a+1);
        };
        line = trim(line);
        if (line.empty() || line[0]=='#') continue;
        std::stringstream ss(line);
        std::string tok; std::vector<std::string> toks;
        while (std::getline(ss, tok, ',')) toks.push_back(trim(tok));
        if (toks.empty()) continue;
        std::string t = toks[0];
        for (auto &c : t) c = (char)toupper((unsigned char)c);
        try {
            if (t == "PLATFORM") {
                if (toks.size() < 5) { dbgoss << "parse error line " << ln; dbg = dbgoss.str(); return false; }
                Obj o; o.type = ObjType::PLATFORM;
                o.r.x = std::stof(toks[1]); o.r.y = std::stof(toks[2]);
                o.r.w = std::stof(toks[3]); o.r.h = std::stof(toks[4]);
                out.push_back(o);
            } else if (t == "SPIKE") {
                if (toks.size() < 5) { dbgoss << "parse error line " << ln; dbg = dbgoss.str(); return false; }
                Obj o; o.type = ObjType::SPIKE;
                o.r.x = std::stof(toks[1]); o.r.y = std::stof(toks[2]);
                o.r.w = std::stof(toks[3]); o.r.h = std::stof(toks[4]);
                out.push_back(o);
            } else if (t == "JUMP_PAD") {
                if (toks.size() < 4) { dbgoss << "parse error line " << ln; dbg = dbgoss.str(); return false; }
                Obj o; o.type = ObjType::JUMP_PAD;
                o.r.x = std::stof(toks[1]); o.r.y = std::stof(toks[2]);
                o.r.w = toks.size() >= 4 ? std::stof(toks[3]) : 16.0f;
                o.r.h = 16.0f;
                o.power = (toks.size() >= 5) ? std::stof(toks[4]) : JUMP_VELOCITY;
                out.push_back(o);
            } else {
                dbgoss << "ignored line " << ln << "\\n";
            }
        } catch (...) {
            dbgoss << "parse exception at line " << ln << "\\n";
            dbg = dbgoss.str();
            return false;
        }
    }
    dbg = dbgoss.str();
    return true;
}

// safe live extractor: attempt PlayLayer->m_level->m_objects guardedly; if not possible, return false
static bool extractLive(std::vector<Obj>& out, std::string& dbg) {
    out.clear();
    dbg.clear();
    PlayLayer* pl = nullptr;
    try { pl = PlayLayer::get(); } catch(...) { pl = nullptr; }
    if (!pl) { dbg = "PlayLayer not found"; return false; }
    GJGameLevel* level = nullptr;
    try { level = pl->m_level; } catch(...) { level = nullptr; }
    if (!level) { dbg = "level ptr not found"; return false; }
    cocos2d::CCArray* arr = nullptr;
    try { arr = level->m_objectList; } catch(...) { arr = nullptr; }
    if (!arr) {
        try { arr = level->m_objects; } catch(...) { arr = nullptr; }
    }
    if (!arr) {
        try { arr = level->m_objectArray; } catch(...) { arr = nullptr; }
    }
    if (!arr) { dbg = "objects array not found"; return false; }
    // attempt to cast to GameObject where available
    for (int i=0;i<(int)arr->count();++i) {
        cocos2d::CCObject* o = arr->objectAtIndex(i);
        if (!o) continue;
        // Try to treat as GameObject if binding present
        GameObject* go = nullptr;
        try { go = static_cast<GameObject*>(o); } catch(...) { go = nullptr; }
        if (!go) continue;
        // attempt to read common fields (m_x,m_y). If not present, skip object.
        float ox=0, oy=0, ow=16, oh=16;
        bool ok=false;
        try { ox = go->m_x; oy = go->m_y; ok = true; } catch(...) { ok=false; }
        if (!ok) {
            try { ox = go->getX(); oy = go->getY(); ok = true; } catch(...) { ok=false; }
        }
        if (!ok) continue;
        Obj obj;
        obj.r.x = ox; obj.r.y = oy; obj.r.w = ow; obj.r.h = oh;
        // heuristic for spikes: small h or w
        if (obj.r.h <= 10.0f || obj.r.w <= 10.0f) obj.type = ObjType::SPIKE;
        else obj.type = ObjType::PLATFORM;
        out.push_back(obj);
    }
    dbg = "extracted " + std::to_string(out.size()) + " objects";
    return !out.empty();
}

class PathfinderPopup : public FLAlertLayerProtocol {
public:
    std::filesystem::path saveDir;
    PathfinderPopup() {
        try { saveDir = Mod::get()->getSaveDir(); } catch(...) { saveDir = std::filesystem::current_path(); }
    }
    void show() {
        std::ostringstream ss;
        ss << "Pathfinder (single-file)\\n\\n";
        ss << "Press RUN to attempt live extraction (PlayLayer). If that fails, fallback to level.txt.\\n\\n";
        ss << "Save dir: " << saveDir.string() << "\\n\\n";
        ss << "Create level.txt in the save dir if needed. Format: PLATFORM,x,y,w,h  SPIKE,x,y,w,h  JUMP_PAD,x,y,w[,power]\\n\\n";
        ss << "Press RUN to start.";
        FLAlertLayer::create(this, "Pathfinder", ss.str(), "RUN", "CANCEL")->show();
    }
    void FLAlert_Clicked(FLAlertLayer*, int btn) override {
        if (btn==0) run();
    }
    void run() {
        std::vector<Obj> objs;
        std::string dbg;
        bool ok = extractLive(objs, dbg);
        if (!ok) {
            // try file fallback
            auto p = (Mod::get()->getSaveDir() / "level.txt");
            std::string filedbg;
            if (!parseLevelFile(p, objs, filedbg)) {
                std::ostringstream oss;
                oss << "Pathfinder: failed to read level. live: " << dbg << " file: " << filedbg;
                geode::Notification::create(oss.str(), geode::NotificationIcon::Exclamation, 6.0f)->show();
                // write report
                auto report = (Mod::get()->getSaveDir() / "pathfinder_report.txt");
                std::ofstream rf(report.string(), std::ios::trunc);
                rf << "live debug:\\n" << dbg << "\\nfile debug:\\n" << filedbg << "\\n";
                rf.close();
                GEODE_ERROR("[Pathfinder] extraction failed: %s | %s", dbg.c_str(), filedbg.c_str());
                return;
            }
        }
        // compute bounding
        float minX = INFINITY, maxX = -INFINITY, groundY = -INFINITY;
        for (auto &o : objs) {
            minX = std::min(minX, o.r.x);
            maxX = std::max(maxX, o.r.x + o.r.w);
            if (o.type == ObjType::PLATFORM) groundY = std::max(groundY, o.r.y + o.r.h);
        }
        if (!std::isfinite(minX)) minX = 0.0f;
        if (!std::isfinite(maxX)) maxX = minX + 1200.0f;
        if (!std::isfinite(groundY)) groundY = 0.0f;
        SimState start;
        start.px = minX - START_BEFORE_X;
        start.py = groundY + 12.0f;
        start.vx = PLAYER_SPEED; start.vy = 0.0f; start.onGround = true;
        std::vector<int> jumps;
        std::string report;
        bool okPlan = runPathfinder(objs, start, maxX, jumps, report);
        // write report and macro
        auto reportPath = (Mod::get()->getSaveDir() / "pathfinder_report.txt");
        try {
            std::ofstream rf(reportPath.string(), std::ios::trunc);
            rf << "extraction debug:\\n" << dbg << "\\n";
            rf << report << "\\n";
            rf << "objects:\\n";
            for (auto &o : objs) {
                rf << (o.type==ObjType::PLATFORM?\"PLATFORM\":o.type==ObjType::SPIKE?\"SPIKE\":\"JUMP_PAD\") << \",\"
                   << o.r.x << \",\" << o.r.y << \",\" << o.r.w << \",\" << o.r.h << \"\\n\";
            }
            rf.close();
        } catch(...) {
            GEODE_ERROR(\"[Pathfinder] failed to write report file\");
        }
        if (!okPlan) {
            geode::Notification::create(\"Pathfinder: couldn't find safe macro. See pathfinder_report.txt\", geode::NotificationIcon::Exclamation, 6.0f)->show();
            return;
        }
        try {
            auto macroPath = (Mod::get()->getSaveDir() / \"macro.txt\").string();
            std::ofstream mf(macroPath, std::ios::trunc);
            for (auto f : jumps) mf << f << \"\\n\";
            mf.close();
            std::ostringstream msg;
            msg << \"Pathfinder: wrote macro.txt (\" << jumps.size() << \" jumps) and pathfinder_report.txt\";
            geode::Notification::create(msg.str(), geode::NotificationIcon::Check, 6.0f)->show();
            GEODE_INFO(\"[Pathfinder] wrote macro: %s\", macroPath.c_str());
        } catch(...) {
            geode::Notification::create(\"Pathfinder: failed to write macro.txt\", geode::NotificationIcon::Exclamation, 6.0f)->show();
        }
    }
};

class $modify(MenuLayer) {
    void onMoreGames(CCObject* sender) {
        MenuLayer::onMoreGames(sender);
        try {
            PathfinderPopup p;
            p.show();
        } catch(...) {
            GEODE_ERROR(\"[Pathfinder] exception showing popup\");
        }
    }
};
