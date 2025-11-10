#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/PauseLayer.hpp>

#include <vector>
#include <map>
#include <set>
#include <fstream>
#include <algorithm>
#include <windows.h>

using namespace geode::prelude;

// Windows API key simulation
void simulateKeyPress(int vkCode) {
    INPUT ip;
    ip.type = INPUT_KEYBOARD;
    ip.ki.wScan = 0;
    ip.ki.time = 0;
    ip.ki.dwExtraInfo = 0;
    ip.ki.wVk = vkCode;
    ip.ki.dwFlags = 0; // Press
    SendInput(1, &ip, sizeof(INPUT));
}

void simulateKeyRelease(int vkCode) {
    INPUT ip;
    ip.type = INPUT_KEYBOARD;
    ip.ki.wScan = 0;
    ip.ki.time = 0;
    ip.ki.dwExtraInfo = 0;
    ip.ki.wVk = vkCode;
    ip.ki.dwFlags = KEYEVENTF_KEYUP; // Release
    SendInput(1, &ip, sizeof(INPUT));
}

// GDR2 Writer (same as before)
class GDR2Writer {
private:
    std::vector<uint8_t> m_data;
    
    void writeVarint(uint64_t value) {
        while (value > 127) {
            m_data.push_back((value & 0x7F) | 0x80);
            value >>= 7;
        }
        m_data.push_back(value & 0x7F);
    }
    
    void writeString(const std::string& str) {
        for (char c : str) m_data.push_back(c);
        m_data.push_back(0);
    }
    
    void writeBool(bool value) {
        m_data.push_back(value ? 1 : 0);
    }
    
public:
    void writeHeader() {
        m_data.push_back('G'); m_data.push_back('D'); m_data.push_back('R');
        writeVarint(2);
        writeString("Pathfinder");
    }
    
    void writeMetadata(const std::string& author, const std::string& description,
                       float duration, int gameVersion, double framerate, int seed,
                       int coins, bool ldm, bool platformer, const std::string& botName,
                       int botVersion, uint32_t levelID, const std::string& levelName) {
        writeString(author); writeString(description);
        writeVarint(static_cast<uint64_t>(duration * framerate));
        writeVarint(gameVersion); writeVarint(static_cast<uint64_t>(framerate));
        writeVarint(seed); writeVarint(coins);
        writeBool(ldm); writeBool(platformer);
        writeString(botName); writeVarint(botVersion);
        writeVarint(levelID); writeString(levelName);
        writeVarint(0);
    }
    
    void writeDeaths(const std::vector<uint64_t>& deaths) {
        writeVarint(deaths.size());
        uint64_t lastFrame = 0;
        for (uint64_t frame : deaths) {
            writeVarint(frame - lastFrame);
            lastFrame = frame;
        }
    }
    
    void writeInputs(const std::vector<std::pair<uint64_t, bool>>& inputs, bool platformer) {
        writeVarint(inputs.size());
        writeVarint(inputs.size());
        uint64_t lastFrame = 0;
        for (const auto& [frame, down] : inputs) {
            uint64_t frameDelta = frame - lastFrame;
            uint64_t packed = platformer ? ((frameDelta << 4) | (1 << 2) | (down ? 0x2 : 0))
                                         : ((frameDelta << 2) | (down ? 0x2 : 0));
            writeVarint(packed);
            lastFrame = frame;
        }
    }
    
    bool saveToFile(const std::string& filename) {
        std::ofstream file(filename, std::ios::binary);
        if (!file.is_open()) return false;
        file.write(reinterpret_cast<const char*>(m_data.data()), m_data.size());
        file.close();
        return true;
    }
};

// Pathfinder Learner
class PathfinderLearner {
private:
    bool m_active;
    int m_deathFrame;
    std::map<int, std::vector<int>> m_successfulClicks;
    int m_attempts;
    int m_tryingFrame;
    int m_currentOffset;
    std::set<int> m_triedOffsets;
    std::ofstream m_log;
    
public:
    PathfinderLearner() : m_active(false), m_deathFrame(-1), m_attempts(0), 
                          m_tryingFrame(-1), m_currentOffset(-30) {}
    
    void start(const std::string& levelName) {
        m_active = true;
        m_successfulClicks.clear();
        m_attempts = 0;
        m_tryingFrame = -1;
        m_currentOffset = -30;
        m_triedOffsets.clear();
        
        m_log.open(fmt::format("pathfinder_{}.txt", levelName));
        if (m_log.is_open()) {
            m_log << "=== Pathfinder Log ===\n";
            m_log << "Level: " << levelName << "\n\n";
        }
        log::info("[Pathfinder] Started");
    }
    
    void stop() {
        m_active = false;
        if (m_log.is_open()) {
            m_log << "\nAttempts: " << m_attempts << "\n";
            m_log << "Successful clicks: " << m_successfulClicks.size() << "\n";
            m_log.close();
        }
        log::info("[Pathfinder] Stopped - {} attempts", m_attempts);
    }
    
    void onDeath(int frame) {
        if (!m_active) return;
        
        m_attempts++;
        log::info("[Pathfinder] Death #{} at frame {}", m_attempts, frame);
        
        if (m_log.is_open()) {
            m_log << "Attempt " << m_attempts << ": died at " << frame << "\n";
        }
        
        // If we died at the SAME frame we're trying to solve
        if (m_tryingFrame == frame) {
            m_triedOffsets.insert(m_currentOffset);
            log::info("[Pathfinder] Offset {} failed", m_currentOffset);
            
            m_currentOffset += 2;
            
            if (m_currentOffset > 30 || m_triedOffsets.size() >= 30) {
                log::info("[Pathfinder] Exhausted offsets for frame {}", frame);
                if (m_log.is_open()) {
                    m_log << "  Exhausted offsets for frame " << frame << "\n";
                }
                m_tryingFrame = -1;
                m_triedOffsets.clear();
            }
        }
        // If we died at a DIFFERENT (earlier) frame, switch focus to that
        else if (m_tryingFrame == -1 || frame < m_tryingFrame) {
            log::info("[Pathfinder] New focus: frame {} (was trying {})", frame, m_tryingFrame);
            m_tryingFrame = frame;
            m_triedOffsets.clear();
            m_currentOffset = -30;
        }
        
        m_deathFrame = frame;
    }
    
    void onProgress(int frame) {
        if (!m_active || m_tryingFrame == -1) return;
        
        if (frame > m_deathFrame + 30) {
            m_successfulClicks[m_tryingFrame].push_back(m_currentOffset);
            log::info("[Pathfinder] SUCCESS! Offset {} at frame {}", m_currentOffset, m_tryingFrame);
            
            if (m_log.is_open()) {
                m_log << "  SUCCESS: offset " << m_currentOffset << " from frame " << m_tryingFrame << "\n";
            }
            
            m_tryingFrame = -1;
            m_triedOffsets.clear();
            m_deathFrame = -1;
        }
    }
    
    bool shouldClick(int frame) {
        if (!m_active) return false;
        
        onProgress(frame);
        
        // Try current offset
        if (m_tryingFrame != -1) {
            int clickFrame = m_tryingFrame + m_currentOffset;
            if (frame >= clickFrame && frame < clickFrame + 3) {
                if (frame == clickFrame) {
                    log::info("[Pathfinder] >>> Trying click at frame {} (offset {} from death {})", 
                             frame, m_currentOffset, m_tryingFrame);
                }
                return true;
            }
        }
        
        // Use learned clicks
        for (const auto& [deathFrame, offsets] : m_successfulClicks) {
            for (int offset : offsets) {
                int clickFrame = deathFrame + offset;
                if (frame >= clickFrame && frame < clickFrame + 3) {
                    if (frame == clickFrame) {
                        log::info("[Pathfinder] >>> Using learned click at frame {}", frame);
                    }
                    return true;
                }
            }
        }
        
        return false;
    }
    
    std::vector<std::pair<uint64_t, bool>> generateMacro() {
        std::vector<std::pair<uint64_t, bool>> inputs;
        std::set<int> clickFrames;
        
        for (const auto& [deathFrame, offsets] : m_successfulClicks) {
            for (int offset : offsets) {
                clickFrames.insert(deathFrame + offset);
            }
        }
        
        for (int frame : clickFrames) {
            inputs.push_back({frame, true});
            inputs.push_back({frame + 3, false});
        }
        
        std::sort(inputs.begin(), inputs.end());
        return inputs;
    }
    
    bool isActive() const { return m_active; }
    int getAttempts() const { return m_attempts; }
    int getSuccessCount() const { return m_successfulClicks.size(); }
};

static PathfinderLearner* g_learner = nullptr;
static bool g_shouldReset = false;

// Hook PlayerObject to detect death
class $modify(PathfinderPlayerObject, PlayerObject) {
    void playerDestroyed(bool p0) {
        if (g_learner && g_learner->isActive()) {
            auto pl = PlayLayer::get();
            if (pl) {
                int frame = static_cast<int>(pl->m_gameState.m_currentProgress * 240.0f);
                g_learner->onDeath(frame);
                g_shouldReset = true;
            }
        }
        PlayerObject::playerDestroyed(p0);
    }
};

// Hook PlayLayer for input injection and reset
class $modify(PathfinderPlayLayer, PlayLayer) {
    struct Fields {
        int m_lastFrame = -1;
        bool m_isHolding = false;
    };
    
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        m_fields->m_lastFrame = -1;
        m_fields->m_isHolding = false;
        return true;
    }
    
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        
        // DEBUG: Confirm postUpdate is being called
        static int debugCounter = 0;
        debugCounter++;
        if (debugCounter == 1 || debugCounter % 240 == 0) {
            log::info("[Pathfinder] @@@ postUpdate called {} times, learner active: {}", 
                     debugCounter, g_learner ? g_learner->isActive() : false);
        }
        
        if (!g_learner || !g_learner->isActive()) return;
        
        // Handle auto-reset
        if (g_shouldReset && m_player1 && m_player1->m_isDead) {
            this->resetLevel();
            g_shouldReset = false;
            m_fields->m_lastFrame = -1;
            m_fields->m_isHolding = false;
            return;
        }
        
        // Inject clicks using Windows API
        if (m_player1 && !m_player1->m_isDead) {
            int currentFrame = static_cast<int>(m_gameState.m_currentProgress * 240.0f);
            
            // DEBUG: Log frame progression
            static int lastLoggedFrame = -1;
            if (currentFrame / 1000 != lastLoggedFrame / 1000) {
                lastLoggedFrame = currentFrame;
                log::info("[Pathfinder] @@@ Current frame: {}, trying frame: {}, offset: {}", 
                         currentFrame, m_tryingFrame, m_currentOffset);
            }
            
            if (currentFrame != m_fields->m_lastFrame) {
                bool shouldClick = g_learner->shouldClick(currentFrame);
                
                if (shouldClick && !m_fields->m_isHolding) {
                    simulateKeyPress(VK_SPACE);
                    m_fields->m_isHolding = true;
                    log::info("[Pathfinder] *** PRESSED SPACE at frame {}", currentFrame);
                } else if (!shouldClick && m_fields->m_isHolding) {
                    simulateKeyRelease(VK_SPACE);
                    m_fields->m_isHolding = false;
                    log::info("[Pathfinder] *** RELEASED SPACE at frame {}", currentFrame);
                }
                
                m_fields->m_lastFrame = currentFrame;
            }
        }
    }
    
    void onQuit() {
        if (g_learner) {
            g_learner->stop();
            delete g_learner;
            g_learner = nullptr;
        }
        g_shouldReset = false;
        PlayLayer::onQuit();
    }
    
    void levelComplete() {
        if (g_learner && g_learner->isActive()) {
            auto level = this->m_level;
            auto inputs = g_learner->generateMacro();
            
            if (!inputs.empty()) {
                GDR2Writer writer;
                writer.writeHeader();
                writer.writeMetadata("Pathfinder AI", 
                                    fmt::format("Learned in {} attempts", g_learner->getAttempts()),
                                    inputs.back().first / 240.0f, 22074, 240.0, 0, 0, false, false,
                                    "Pathfinder", 1, 
                                    level ? level->m_levelID.value() : 0,
                                    level ? level->m_levelName : "Unknown");
                
                std::vector<uint64_t> deaths;
                writer.writeDeaths(deaths);
                writer.writeInputs(inputs, false);
                
                std::string filename = fmt::format("pathfinder_{}.gdr2", 
                                                   level ? level->m_levelName : "level");
                writer.saveToFile(filename);
                
                FLAlertLayer::create("Pathfinder Complete!",
                    fmt::format("Learned in {} attempts\nSaved: {}", 
                               g_learner->getAttempts(), filename), "OK")->show();
            }
            
            g_learner->stop();
        }
        PlayLayer::levelComplete();
    }
};

// Pause menu button
class $modify(PathfinderPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        
        auto btn = CCMenuItemSpriteExtra::create(
            CCSprite::createWithSpriteFrameName("GJ_starsIcon_001.png"),
            this,
            menu_selector(PathfinderPauseLayer::onPathfinder)
        );
        btn->setScale(1.0f);
        
        auto menu = CCMenu::create();
        menu->addChild(btn);
        menu->setPosition(CCDirector::sharedDirector()->getWinSize().width - 30, 100);
        this->addChild(menu, 10);
    }
    
    void onPathfinder(CCObject*) {
        auto pl = PlayLayer::get();
        if (!pl) return;
        
        if (g_learner && g_learner->isActive()) {
            g_learner->stop();
            delete g_learner;
            g_learner = nullptr;
            FLAlertLayer::create("Pathfinder", "Stopped", "OK")->show();
        } else {
            auto level = pl->m_level;
            g_learner = new PathfinderLearner();
            g_learner->start(level ? level->m_levelName : "Unknown");
            pl->resetLevel();
            this->onResume(nullptr);
        }
    }
};