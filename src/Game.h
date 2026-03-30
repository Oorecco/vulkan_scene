#pragma once
// Game.h — Part 4. DevConsole added, focus/alt-tab properly fixed,
// IT'S NOT YOUR FAULT now correctly red, NPCShop cleaned up.

#include "Common.h"
#include "Settings.h"
#include "AudioManager.h"
#include "SaveSystem.h"
#include "DevConsole.h"
#include "renderer/VulkanRenderer.h"
#include "renderer/AssetManager.h"
#include "ui/UIBatch.h"
#include "ui/Font.h"
#include "renderer/VulkanImage.h"
#include "game/Player.h"
#include "game/PhysicsCube.h"
#include "game/Freecam.h"
#include "game/ActiveEffects.h"
#include "game/ItemSystem.h"
#include "game/CrateSpawner.h"
#include "game/DayNight.h"
#include "game/CursedObjects.h"
#include "game/NPCShop.h"
#include "game/TitleCamera.h"
#include "scene/Scene.h"
#include "scene/WorldDecorations.h"
#include <atomic>
#include <thread>

struct HUDNotif {
    std::string text;
    float timer=0, duration=4.f;
    float r=1,g=1,b=1;
};

struct AlertState {
    bool active=false, isWarn=false, hasNo=true;
    char msg[256]={}, sub[256]={};
    std::function<void(bool)> cb;
};

// IT'S NOT YOUR FAULT: RED diagonal text flood, horror movement.
// Triggered by finding all 5 cursed objects → diamonds unlocked.
// The green thing in Part 3 was wrong. This is the real one.
struct NotYourFaultState {
    bool  active      = false;
    float timer       = 0.f;
    bool  ending      = false;
    float endTimer    = 0.f;
    float textScroll  = 0.f; // drives diagonal position
    float horrorPhase = 0.f; // drives horror jitter
    static constexpr float DURATION  = 14.0f;
    static constexpr float END_FADE  = 3.5f;
};

enum class UIScreen { None, PauseMenu, Settings, Shop, Changelogs, Credits, Controls };

class Game {
public:
    Game()  = default;
    ~Game();

    bool    init(HINSTANCE hInst);
    int     run();
    LRESULT handleMsg(HWND hw, UINT msg, WPARAM wp, LPARAM lp);

    float loadingProgress() const { return m_loadProgress.load(); }

private:
    bool createWindow(HINSTANCE hInst);
    bool initRenderer();
    bool initFont();
    void initItemCallbacks();
    void initDevConsole();   // register all tweakable variables
    void backgroundLoad();

    void update(float dt);
    void render();
    void simUpdate(float dt);
    void updateEffects(float dt);
    void updateCrates(float dt);
    void updateNotYourFault(float dt);

    glm::vec3 activeCamEye()     const;
    glm::vec3 activeCamForward() const;
    glm::mat4 activeCamView()    const;

    void saveGame();
    void loadGame();

    // ── UI ────────────────────────────────────────────────────────────────
    void drawLoadingScreen(UIBatch& b);
    void drawTitleScreen(UIBatch& b);
    void drawCrosshair(UIBatch& b);
    void drawEnergyBar(UIBatch& b);
    void drawPauseMenu(UIBatch& b);
    void drawSettings(UIBatch& b);
    void drawDebug(UIBatch& b);         // Shift+F1: general
    void drawDebugRender(UIBatch& b);   // Shift+F2: render stats
    void drawDebugWorld(UIBatch& b);    // Shift+F3: world data
    void drawSwingMeter(UIBatch& b);    // push charge UI
    void drawAlert(UIBatch& b);
    void drawVersionText(UIBatch& b);
    void drawHintsPanel(UIBatch& b);
    void drawHeldItem(UIBatch& b);
    void drawActiveEffects(UIBatch& b);
    void drawNotifications(UIBatch& b);
    void drawShopUI(UIBatch& b);
    void drawNotYourFault(UIBatch& b);
    void drawDayNightHUD(UIBatch& b);
    void drawCurrencyHUD(UIBatch& b);
    void drawChangelogs(UIBatch& b);
    void drawCredits(UIBatch& b);
    void drawControls(UIBatch& b);

    void pushNotif(const std::string& t, float r=1,float g=1,float b=1, float dur=4.f);

    void showAlert(bool warn,const char* msg,const char* sub,
                   bool hasNo,std::function<void(bool)> cb);

    // ── Mouse / focus ─────────────────────────────────────────────────────
    // [FIX] Part 4: proper alt-tab, fullscreen restore, cursor management
    void setCap(bool cap);
    void onFocusLost();
    void onFocusGained();
    void recenterCursor();       // re-center without changing capture state
    void applySettings();
    void updateSettingsNav(float dt);
    void resolveCollisions();

    void onKeyDown(WPARAM vk);
    void onKeyUp(WPARAM vk);
    void onMouseMove();
    void onMouseClick(bool right = false);

    uint32_t W() { return m_renderer.width();  }
    uint32_t H() { return m_renderer.height(); }

    // ── Window state ──────────────────────────────────────────────────────
    HWND  m_hwnd       = nullptr;
    int   m_winBaseX   = 0, m_winBaseY = 0;
    bool  m_minimised  = false;  // [FIX] don't try to render while minimised
    bool  m_fullscreen = false;  // [FIX] track actual fullscreen state for restore
    bool  m_hiddenForFocusLoss = false;

    // ── Systems ───────────────────────────────────────────────────────────
    VulkanRenderer    m_renderer;
    AssetManager      m_assets;
    AudioManager      m_audio;
    Settings          m_settings;
    UIBatch           m_uiBatch;
    VulkanImage       m_fontAtlas;
    Player            m_player;
    PhysicsCube       m_cube;
    Freecam           m_freecam;
    Scene             m_scene;
    WorldDecorations  m_decorations;
    ItemSystem        m_items;
    CrateSpawner      m_crates;
    ActiveEffects     m_effects;
    DayNight          m_dayNight;
    CursedObjects     m_cursedObjs;
    NPCShop           m_shop;
    TitleCamera       m_titleCam;
    DevConsole        m_console;

    SaveData m_save;
    double& money()           { return m_save.money; }
    int&    diamonds()        { return m_save.diamonds; }
    bool&   diamondsUnlocked(){ return m_save.diamondsUnlocked; }

    AppState  m_appState  = AppState::Loading;
    UIScreen  m_uiScreen  = UIScreen::None;
    bool      m_paused    = false;
    bool      m_showDebug = false;
    bool      m_running   = true;
    bool      m_capMouse  = false;
    int       m_pauseSel  = 0;
    bool      m_lookingPC = false, m_lookingCrate = false;
    bool      m_titleReady = false;
    int       m_titleSel   = 0;
    float     m_titleFadeIn= 0.f;
    int       m_shopSel    = 0;
    int       m_clScroll   = 0;  // changelog scroll (lines from bottom)
    int       m_clTab      = 0;  // changelog version tab index

    // Controls rebind state
    int       m_ctrlSel    = 0;  // selected keybind row
    bool      m_ctrlWaiting= false; // waiting for key press to rebind
    // Rebindable action names and their current VK codes
    struct KeyBind { const char* action; WPARAM vk; };
    enum { KEYBIND_COUNT = 8 };
    KeyBind   m_binds[KEYBIND_COUNT] = {
        {"Jump",         VK_SPACE},
        {"Sprint",       VK_LCONTROL},
        {"Interact/Push",'E'},
        {"Grab/Release", 'F'},
        {"Freecam",      'P'},      // Shift+P but we store P
        {"Teleport Here",'R'},      // Shift+R
        {"Debug Overlay",VK_F1},
        {"Dev Console",  0},        // Shift+F4 — not rebindable here
    };

    AlertState        m_alert;
    NotYourFaultState m_notFault;
    std::vector<HUDNotif> m_notifs;

    bool m_kEsc=false,m_kF1=false,m_kF2=false,m_kF3=false,m_kF4=false;
    bool m_kE=false,m_kF=false,m_kP=false,m_kR=false;

    // Debug overlays: Shift+F1=general, F2=render, F3=world, F4=console
    bool m_showDebugRender = false;
    bool m_showDebugWorld  = false;

    // Scene fade-in when transitioning from title to play
    float m_sceneFade = 0.0f; // 0=black, 1=full visible

    // Push swing meter state
    bool  m_pushHeld    = false;   // E is held down, charging push
    float m_pushCharge  = 0.0f;   // 0..1 charge level
    bool  m_pushReady   = false;   // looking at cube and E was pressed

    float m_simTime=0.f, m_totalTime=0.f;
    float m_physAccum=0.f;
    float m_lastPhysStepMs=0.f;
    float m_fps=0.f, m_cpuMs=0.f;
    int   m_frameN=0; float m_fpsAcc=0.f;
    float m_saveTimer=0.f;

    POINT m_lastCursor = {};

    float m_navHold[4]={};
    bool  m_navHeld[4]={};

    std::atomic<float> m_loadProgress{0.f};
    bool               m_loadDone = false;

    float m_shakeTimer=0.f;

    static LRESULT CALLBACK StaticWndProc(HWND hw,UINT msg,WPARAM wp,LPARAM lp);
    static Game* s_instance;
};
