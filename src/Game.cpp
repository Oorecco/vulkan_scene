// Game.cpp — The glue factory. All systems meet here.
// If something is broken, it's probably this file's fault.
// If something works perfectly, it's definitely this file's doing.
//
// Architecture: Game owns everything. Everything depends on Game.
// This is not SOLID design. This is "we wanted to ship" design.
// It works. Don't touch it.

#include "Game.h"
#include <chrono>
#include <algorithm>
#include <shellapi.h>
#include <cstring>

// The singleton pointer. There is exactly one Game. Like most interesting things.
Game* Game::s_instance = nullptr;

// Static WndProc — bridges Windows to our civilized C++ world.
// Windows calls this, we dispatch to handleMsg. Everyone is (mostly) happy.
LRESULT CALLBACK Game::StaticWndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    if (s_instance) return s_instance->handleMsg(hw, msg, wp, lp);
    return DefWindowProcA(hw, msg, wp, lp);
}


// ═══════════════════════════════════════════════════════════════════════════
//  INIT  — "Let there be light, a window, a render pass, and finally a game."
// ═══════════════════════════════════════════════════════════════════════════

bool Game::init(HINSTANCE hInst) {
    s_instance = this;

    // Load settings first — the window should open at the right resolution.
    // Loading after createWindow means the first frame looks wrong. Ask how I know.
    m_settings.Init();
    m_settings.Load();
    loadGame(); // Load save so the title screen shows the correct money

    if (!createWindow(hInst)) return false;
    if (!initRenderer())      return false;
    if (!initFont())          return false;

    // Spin off background thread: assets, geometry, audio — everything heavy.
    // Main thread shows the loading screen while this does all the actual work.
    std::thread([this] { backgroundLoad(); }).detach();
    return true;
}

void Game::backgroundLoad() {
    // Phase 1: textures (0% → 60%)
    std::string assetRoot = ExeDir() + "assets/";
    m_assets.init(m_renderer.context(), assetRoot);
    m_assets.preloadAll(m_loadProgress); // increments progress internally

    // Phase 2: geometry (60% → 72%)
    m_scene.init(m_renderer.context());
    m_decorations.init(m_renderer.context(), m_renderer, m_assets, 1337);
    // 1337 seed: chosen in 2003, still going strong in every codebase
    m_loadProgress.store(0.72f);

    // Phase 3: gameplay systems (72% → 80%)
    m_crates.init(42);
    m_crates.onCrateOpened = [this] { /* no SFX here — crate content handles notif */ };
    initItemCallbacks();
    m_loadProgress.store(0.80f);

    // Phase 4: cursed objects, NPC shop, title cam (80% → 93%)
    m_cursedObjs.init(m_renderer.context(), m_save.cursedFlags);
    m_cursedObjs.onFound = [this](int id, bool wasLast) {
        m_save.cursedFlags[id] = true;
        m_save.cursedFound     = m_cursedObjs.foundCount();

        char buf[64];
        snprintf(buf, sizeof(buf), "Cursed object found! (%d/5)", m_save.cursedFound);
        pushNotif(buf, 0.8f, 0.4f, 1.0f, 6.0f);

        if (wasLast) {
            pushNotif("ALL 5 FOUND — Diamonds permanently unlocked!", 0.3f, 1.0f, 0.9f, 8.0f);
            m_save.diamondsUnlocked = true;
            // Trigger IT'S NOT YOUR FAULT — RED diagonal flood.
            // (It was green in Part 3. A mistake. Never again.)
            m_notFault = { true, 0.f, false, 0.f, 0.f, 0.f };
        }
        saveGame();
    };

    m_shop.init(m_renderer.context());
    m_shop.onShopOpen = [this] { /* shop opens quietly — NPC says hi instead */ };
    m_loadProgress.store(0.88f);

    m_titleCam.init();
    // Wire ground grass texture into the Scene for textured drawing
    {
        VkDescriptorSet gds = m_renderer.allocateDiffuseSet(
            m_assets.view("textures/ground/ground_grass.png"),
            m_assets.repeatSampler());
        m_scene.setGroundTexture(gds);
    }
    // Wire cursed_geometry texture into CursedObjects
    {
        VkDescriptorSet cds = m_renderer.allocateDiffuseSet(
            m_assets.view("textures/props/cursed_geometry.png"),
            m_assets.clampSampler());
        m_cursedObjs.setTexture(cds);
    }
    m_loadProgress.store(0.93f);

    // Phase 5: audio + console (93% → 100%)
    m_audio.init(assetRoot);
    initDevConsole(); // AFTER all systems — console takes raw pointers
    m_loadProgress.store(1.0f);

    // Small intentional sleep so the loading bar doesn't flash past in 30ms.
    // This is the most honest sleep in the codebase.
    std::this_thread::sleep_for(std::chrono::milliseconds(200 + rand() % 400));
    m_loadDone = true;
}

bool Game::createWindow(HINSTANCE hInst) {
    WNDCLASSEXA wc{ sizeof(wc) };
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = StaticWndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    // Load the custom app icon (IDI_APPICON = 1 in resource.rc).
    // Falls back to the default Windows icon if the resource isn't embedded.
    wc.hIcon         = LoadIconA(hInst, MAKEINTRESOURCEA(1));
    if (!wc.hIcon)   wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.lpszClassName = "VkScene07";
    RegisterClassExA(&wc);

    ResEntry res = m_settings.Res();
    WinMode wm   = m_settings.WMode();
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    int windowW = (int)res.w, windowH = (int)res.h;
    int windowX = 0, windowY = 0;

    if (wm == WinMode::Windowed) {
        RECT wr = { 0, 0, (LONG)res.w, (LONG)res.h };
        AdjustWindowRect(&wr, style, FALSE);
        windowW = wr.right - wr.left;
        windowH = wr.bottom - wr.top;
        windowX = (sw - windowW) / 2;
        windowY = (sh - windowH) / 2;
        m_fullscreen = false;
    } else {
        style = WS_POPUP;
        windowW = sw; windowH = sh;
        windowX = 0;  windowY = 0;
        m_fullscreen = true;
    }

    m_winBaseX = windowX;
    m_winBaseY = windowY;
    m_hwnd = CreateWindowExA(0, "VkScene07", "Vulkan 1.1+ Scene v0.01-alpha",
        style, windowX, windowY, windowW, windowH,
        nullptr, nullptr, hInst, nullptr);
    if (!m_hwnd) { LOG_ERR("CreateWindow failed"); return false; }
    ShowWindow(m_hwnd, SW_SHOW);
    return true;
}

bool Game::initRenderer() {
    try {
        RECT cr{};
        GetClientRect(m_hwnd, &cr);
        uint32_t w = std::max(1L, cr.right - cr.left);
        uint32_t h = std::max(1L, cr.bottom - cr.top);
        m_renderer.init(m_hwnd, w, h, m_settings.VSync(), m_settings.VkVerIdx());
        return true;
    } catch (const std::exception& e) {
        MessageBoxA(m_hwnd, e.what(), "Fatal: Vulkan init failed", MB_OK | MB_ICONERROR);
        return false;
    }
}

bool Game::initFont() {
    // Flat 1D array — 2D arrays cause MSVC aggregate-init headaches.
    static uint8_t atlas[ATLAS_H * ATLAS_W];
    buildFontAtlas(atlas, ATLAS_W, ATLAS_H);
    m_fontAtlas = VulkanImage::createFontAtlas(
        m_renderer.context(), ATLAS_W, ATLAS_H, (const uint8_t*)atlas);
    m_renderer.registerFontAtlas(m_fontAtlas.view, m_renderer.fontSampler());
    return m_fontAtlas.image != VK_NULL_HANDLE;
}

// ── Dev console variable registration ─────────────────────────────────────
// "What's this for?" — Changing gravity at runtime and watching the cube
// achieve escape velocity. Also debugging. Mostly watching the cube.
void Game::initDevConsole() {
    // Mutable proxy floats for physics constants.
    // constexpr values can't be modified (that's the point of constexpr).
    // Console changes affect only these proxy copies. It's a documented hack.
    static float s_gravity    = GRAVITY_A;
    static float s_jumpVel    = JUMP_VEL;
    static float s_moveSpeed  = MOVE_SPEED;
    static float s_sprintMult = SPRINT_MULT;
    static float s_pushForce  = PUSH_IMPULSE;
    static float s_grabDist   = GRAB_DIST;

    m_console.registerFloat("gravity",     &s_gravity,    "gravity m/s2 (proxy)", 0.f,  50.f);
    m_console.registerFloat("jump_vel",    &s_jumpVel,    "jump velocity (proxy)", 0.f,  30.f);
    m_console.registerFloat("move_speed",  &s_moveSpeed,  "walk speed (proxy)",    0.f,  20.f);
    m_console.registerFloat("sprint_mult", &s_sprintMult, "sprint mult (proxy)",   1.f,   5.f);
    m_console.registerFloat("push_force",  &s_pushForce,  "push impulse (proxy)",  0.f,  50.f);
    m_console.registerFloat("grab_dist",   &s_grabDist,   "grab distance (proxy)", 0.5f, 10.f);

    m_console.registerFloat("player.energy", &m_player.energy,  "stamina",      0.f, ENERGY_MAX);
    m_console.registerFloat("player.yaw",    &m_player.yaw,     "camera yaw");
    m_console.registerFloat("player.pitch",  &m_player.pitch,   "camera pitch",-89.f, 89.f);
    m_console.registerFloat("sim_time",      &m_simTime,        "sim time (seekable)");
    m_console.registerBool ("debug",         &m_showDebug,      "show debug overlay");

    m_console.registerCmd("teleport", [this](const std::string& args) -> std::string {
        float x = 0, y = 0, z = 0;
        if (sscanf_s(args.c_str(), "%f %f %f", &x, &y, &z) == 3) {
            m_player.pos = { x, y, z }; m_player.vel = {};
            return Fmt("Teleported to %.1f %.1f %.1f", x, y, z);
        }
        return "Usage: teleport X Y Z";
    }, "teleport X Y Z");

    m_console.registerCmd("give_money", [this](const std::string& args) -> std::string {
        double amount = args.empty() ? 10000.0 : std::stod(args);
        money() += amount;
        return Fmt("Added $%.0f (total: $%.0f). You didn't earn it.", amount, money());
    }, "give_money [amount]");

    m_console.registerCmd("give_diamond", [this](const std::string& args) -> std::string {
        if (!diamondsUnlocked())
            return "Diamonds locked. Find all 5 cursed objects. No shortcuts. (Except this one.)";
        int n = args.empty() ? 1 : std::stoi(args);
        diamonds() += n;
        return Fmt("Added %d diamond(s). Total: %d", n, diamonds());
    }, "give_diamond [count]");

    m_console.registerCmd("reset_player", [this](const std::string&) -> std::string {
        m_player.reset();
        return "Player reset to spawn. Whatever you were doing: it didn't work.";
    }, "reset_player");

    m_console.registerCmd("time_set", [this](const std::string& args) -> std::string {
        int h = 0, m2 = 0;
        if (sscanf_s(args.c_str(), "%d:%d", &h, &m2) == 2) {
            m_dayNight.setTime(h, m2);
            return Fmt("Time set to %02d:%02d. The sun grudgingly obeys.", h, m2);
        }
        return "Usage: time_set HH:MM";
    }, "time_set HH:MM");

    m_console.log("Console ready. Type 'help'. Type 'give_money' to feel like a cheater.");
}

// ── Item effect callbacks ──────────────────────────────────────────────────
// Connect ItemSystem triggers to actual game-state changes.
// Lambdas capture [this] — they live as long as m_items does.
void Game::initItemCallbacks() {
    m_items.onSpeedBoost = [this](float dur) {
        m_effects.speedBoost = true; m_effects.speedBoostTime = dur;
        pushNotif("SPEED BOOST active!", 0.4f, 0.8f, 1.f);
    };
    m_items.onGravityFlip = [this] {
        m_effects.gravityFlipped  = true;
        m_effects.gravityFlipTime = ActiveEffects::GRAVITY_FLIP_DUR;
        pushNotif("GRAVITY FLIP — cube follows its own rules now", 0.8f, 0.4f, 1.f);
    };
    m_items.onCubeLauncher = [this] {
        m_effects.cubeLauncherArmed = true;
        pushNotif("LAUNCHER ARMED — next throw is 5x. Aim carefully.", 1.f, 0.6f, 0.2f);
    };
    m_items.onMagnetPulse = [this] {
        // Snap cube to grab point — dramatic thud sold separately
        m_cube.pos = m_player.eyePos() + m_player.forward() * GRAB_DIST;
        m_cube.vel = {}; m_cube.angVel = {};
        pushNotif("MAGNET PULSE", 0.8f, 0.2f, 0.4f);
    };
    m_items.onCoinShower = [this](double amount) {
        m_effects.coinShower   = true; m_effects.coinTimer    = 0.f;
        m_effects.coinDuration = 3.f;  m_effects.coinTotal    = amount;
        m_effects.coinPaid     = 0.0;
        pushNotif("COIN SHOWER! It's literally raining money!", 1.f, 0.9f, 0.2f, 5.f);
    };
    m_items.onDiamondPulse = [this] {
        m_effects.diamondPulse = true; m_effects.diamondTimer = 0.f;
        pushNotif("DIAMOND PULSE — cursed objects revealed for 8s", 0.4f, 0.9f, 1.f);
    };
    m_items.onFreezeTime = [this](float dur) {
        m_effects.freezeTime = true; m_effects.freezeTimer = dur;
        pushNotif("TIME FREEZE — the world holds its breath", 0.7f, 0.9f, 1.f);
    };
    m_items.onEarthquake = [this](float dur) {
        m_effects.earthquake = true; m_effects.quakeTimer = dur;
        m_shakeTimer = dur;
        if (m_settings.WMode() == WinMode::Windowed) {
            RECT wr; GetWindowRect(m_hwnd, &wr);
            m_winBaseX = wr.left; m_winBaseY = wr.top;
        }
        pushNotif("EARTHQUAKE! (sorry about the window)", 1.f, 0.5f, 0.2f);
    };
    m_items.onBlackHole = [this] {
        m_effects.blackHole = true; m_effects.blackHoleTimer = ActiveEffects::BLACK_HOLE_DUR;
        pushNotif("BLACK HOLE — resistance is futile", 0.4f, 0.2f, 0.8f);
    };
}

Game::~Game() {
    saveGame(); // save BEFORE we start destroying things

    // Wait for GPU to finish. Destroying resources mid-frame is very bad.
    // The validation layers will explain this to you in great detail.
    if (m_renderer.context().device())
        vkDeviceWaitIdle(m_renderer.context().device());

    // Destroy in roughly reverse init order
    m_shop.destroy(m_renderer.context().device());
    m_cursedObjs.destroy(m_renderer.context().device());
    m_decorations.destroy(m_renderer.context().device());
    m_scene.destroy(m_renderer.context().device());
    m_fontAtlas.destroy(m_renderer.context().device());
    m_assets.destroy();
    m_audio.destroy();
    m_renderer.shutdown();
    s_instance = nullptr;
}

void Game::saveGame() { m_save.totalPlaySeconds += (int)m_saveTimer; SaveSystem::save(m_save); }
void Game::loadGame() { if (!SaveSystem::load(m_save)) m_save = {}; }


// ═══════════════════════════════════════════════════════════════════════════
//  MAIN LOOP  — The heartbeat. Pumps messages, ticks, renders.
// ═══════════════════════════════════════════════════════════════════════════

int Game::run() {
    using Clock = std::chrono::high_resolution_clock;
    auto last = Clock::now();
    MSG  msg  = {};
    while (m_running) {
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (!m_running) break;
        // Don't burn CPU while minimised — Sleep gives ~20Hz message pumping
        if (m_minimised) { Sleep(50); continue; }
        auto  now = Clock::now();
        float dt  = std::chrono::duration<float>(now - last).count();
        last      = now;
        dt        = std::min(dt, 0.05f); // cap delta — death-spiral prevention
        update(dt);
        render();
    }
    return 0;
}


// ═══════════════════════════════════════════════════════════════════════════
//  UPDATE  — Drives all simulation, camera, and UBO updates per frame.
// ═══════════════════════════════════════════════════════════════════════════

void Game::update(float dt) {
    auto t0 = std::chrono::high_resolution_clock::now();

    // Wall-clock always advances — powers console clock, flash timers, horror drift
    m_totalTime += dt;

    // FPS averaged over 0.3s so the number doesn't give you a headache
    m_fpsAcc += dt; m_frameN++;
    if (m_fpsAcc >= 0.3f) {
        m_fps    = (float)m_frameN / m_fpsAcc;
        m_fpsAcc = 0.f; m_frameN = 0;
    }

    // ── Loading: wait for background thread ────────────────────────────────
    if (m_appState == AppState::Loading) {
        if (m_loadDone) {
            m_appState    = AppState::Title;
            m_titleFadeIn = 0.f;
            m_audio.playBGM(BGMTrack::Title);
        }
        return;
    }

    // ── Title: just spin the camera ────────────────────────────────────────
    if (m_appState == AppState::Title) {
        m_titleFadeIn += dt * 0.8f; // ~1.25s fade-in
        m_titleCam.update(dt);
        return;
    }

    // ── Playing ────────────────────────────────────────────────────────────
    updateSettingsNav(dt);
    m_audio.update(dt);

    // Tick and remove expired notifications
    for (auto& n : m_notifs) n.timer += dt;
    m_notifs.erase(
        std::remove_if(m_notifs.begin(), m_notifs.end(),
                       [](const HUDNotif& n) { return n.timer >= n.duration; }),
        m_notifs.end());

    // Auto-save every 60 real seconds
    m_saveTimer += dt;
    if (m_saveTimer >= 60.f) { saveGame(); m_saveTimer = 0.f; }

    // Sim runs when: playing, not paused, no alert, no time-freeze
    bool simRunning = (m_appState == AppState::Playing
                       && !m_paused && !m_alert.active && !m_effects.freezeTime);
    if (simRunning) {
        constexpr float PHYS_DT = 1.0f / 60.0f;
        m_physAccum = std::min(m_physAccum + dt, PHYS_DT * 6.0f);
        int physSteps = 0;
        while (m_physAccum >= PHYS_DT && physSteps < 4) {
            auto ps0 = std::chrono::high_resolution_clock::now();
            m_simTime += PHYS_DT;
            m_dayNight.update(PHYS_DT);
            simUpdate(PHYS_DT);
            updateEffects(PHYS_DT);
            updateCrates(PHYS_DT);
            m_cursedObjs.update(PHYS_DT, m_player.pos, m_dayNight, m_effects.diamondPulse);
            m_shop.update(PHYS_DT, m_player.pos, m_dayNight, diamondsUnlocked(), diamonds());
            auto ps1 = std::chrono::high_resolution_clock::now();
            m_lastPhysStepMs = std::chrono::duration<float, std::milli>(ps1 - ps0).count();
            m_physAccum -= PHYS_DT;
            physSteps++;
        }
        if (physSteps == 4 && m_physAccum > PHYS_DT * 2.0f) {
            // If we fall too far behind, drop extra accumulated time and recover.
            m_physAccum = PHYS_DT;
        }
    } else {
        m_physAccum = 0.0f;
    }
    m_effects.update(dt);
    updateNotYourFault(dt);

    // Window shake (earthquake) — windowed mode only
    if (m_effects.earthquake && m_settings.WMode() == WinMode::Windowed) {
        float amp = std::min(8.0f, m_effects.quakeTimer * 3.f); // hard cap 8px
        int sx = (int)(((float)(rand() % 200) / 100.f - 1.f) * amp);
        int sy = (int)(((float)(rand() % 200) / 100.f - 1.f) * amp * 0.5f);
        SetWindowPos(m_hwnd, nullptr, m_winBaseX + sx, m_winBaseY + sy,
                     0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    } else if (!m_effects.earthquake && m_shakeTimer > 0.f
               && m_settings.WMode() == WinMode::Windowed) {
        // Reset to original position when shake ends
        SetWindowPos(m_hwnd, nullptr, m_winBaseX, m_winBaseY,
                     0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        m_shakeTimer = 0.f;
    }

    m_scene.update(m_simTime, m_player, m_cube, m_freecam.isActive());

    // ── Camera & UBO ───────────────────────────────────────────────────────
    glm::vec3 eye  = activeCamEye();
    glm::mat4 view = activeCamView();
    float fov  = m_settings.FOV_() + m_player.sprintFOV - m_player.fallFOV;
    float farZ = m_settings.FarZ();
    float asp  = (float)W() / (float)std::max(H(), 1u);
    glm::mat4 proj = glm::perspective(glm::radians(fov), asp, NEAR_Z, farZ);
    proj[1][1] *= -1.f; // Vulkan NDC has Y pointing down

    // Gribb-Hartmann frustum plane extraction
    glm::mat4 vpt = glm::transpose(proj * view);
    m_scene.frustum.planes[0] = vpt[3] + vpt[0]; // left
    m_scene.frustum.planes[1] = vpt[3] - vpt[0]; // right
    m_scene.frustum.planes[2] = vpt[3] + vpt[1]; // bottom
    m_scene.frustum.planes[3] = vpt[3] - vpt[1]; // top
    m_scene.frustum.planes[4] = vpt[2];           // near
    m_scene.frustum.planes[5] = vpt[3] - vpt[2]; // far

    // Sun direction / sky from day-night system, scaled by user lighting settings
    glm::vec3 sunDir  = m_dayNight.sunDir();
    glm::vec3 skyCol  = m_dayNight.skyColor();
    // Update renderer clear color so the sky behind world geometry matches
    m_renderer.setSkyColor(skyCol.x, skyCol.y, skyCol.z);
    // Scene fade-in: ramp from 0→1 over 1.2s whenever we enter playing state
    if (m_appState == AppState::Playing && m_sceneFade < 1.0f)
        m_sceneFade = std::min(1.0f, m_sceneFade + dt / 1.2f);
    float     ambBase = m_dayNight.ambientIntensity();
    float     ambFinal   = ambBase * m_settings.AmbientLevel();
    float     sunIntensity = m_settings.SunIntensity();
    float     gamma       = m_settings.Gamma();

    glm::mat4 lightView = glm::lookAt(sunDir * 50.f, glm::vec3(0.f), glm::vec3(0.f, 1.f, 0.f));
    glm::mat4 lightProj = glm::ortho(-25.f, 25.f, -25.f, 25.f, 0.1f, 200.f);
    lightProj[1][1] *= -1.f;

    UBOFrame ubo{};
    ubo.view     = view;
    ubo.proj     = proj;
    ubo.lightVP  = lightProj * lightView;
    ubo.lightDir = { sunDir.x, sunDir.y, sunDir.z, sunIntensity }; // w = intensity
    ubo.camPos   = { eye.x,    eye.y,    eye.z,    0.f };
    ubo.skyCol   = { skyCol.x, skyCol.y, skyCol.z, ambFinal };     // w = ambient
    ubo.gndCol   = { 0.28f, 0.22f, 0.16f, ambFinal * 0.6f };       // ground bounce
    ubo.fogParams= { 60.f, farZ, m_settings.FogDensity(), gamma };  // w = gamma
    ubo.fogColor = { skyCol.x * 0.5f, skyCol.y * 0.5f, skyCol.z * 0.5f, 0.f };
    m_renderer.updateFrameUBO(ubo);

    auto t1 = std::chrono::high_resolution_clock::now();
    m_cpuMs = std::chrono::duration<float, std::milli>(t1 - t0).count();
}

// ── IT'S NOT YOUR FAULT state machine ─────────────────────────────────────
// Active 14s, then fades out over END_FADE seconds.
// Don't try to fix the jitter — it's supposed to feel wrong.
void Game::updateNotYourFault(float dt) {
    if (!m_notFault.active) return;
    if (m_notFault.ending) {
        m_notFault.endTimer += dt;
        if (m_notFault.endTimer >= NotYourFaultState::END_FADE) m_notFault = {};
        return;
    }
    m_notFault.timer      += dt;
    m_notFault.textScroll += dt * 55.f;
    m_notFault.horrorPhase+= dt;
    if (m_notFault.timer >= NotYourFaultState::DURATION)
        m_notFault.ending = true;
}

// ── Effect side-effects ────────────────────────────────────────────────────
void Game::updateEffects(float dt) {
    // Coin shower: drip money over coinDuration seconds
    if (m_effects.coinShower) {
        double portion = (m_effects.coinTotal - m_effects.coinPaid)
                         * std::min(1.0, (double)(dt / m_effects.coinDuration));
        money() += portion;
        m_effects.coinPaid += portion;
        m_save.totalMoneyEarned += portion;
    }
    // Black hole: pull player toward cube
    if (m_effects.blackHole) {
        glm::vec3 dir = m_cube.pos - m_player.pos;
        float     len = glm::length(dir);
        if (len > 0.5f)
            m_player.vel += glm::normalize(dir) * ActiveEffects::BLACK_HOLE_FORCE * dt;
    }
}

void Game::updateCrates(float dt) {
    m_crates.update(dt, m_player.pos);
    m_lookingCrate = (m_crates.nearestInteractable(m_player.pos) != nullptr);
}

// ── Physics simulation ─────────────────────────────────────────────────────
void Game::simUpdate(float dt) {
    glm::vec3 eye        = activeCamEye();
    glm::vec3 fwd        = activeCamForward();
    glm::vec3 grabTarget = eye + fwd * GRAB_DIST;

    // Update swing charge: E held while looking at pushable cube
    if (m_pushReady && m_lookingPC && !m_cube.grabbed) {
        m_pushCharge = std::min(1.0f, m_pushCharge + dt / 1.4f); // 1.4s to full charge
    } else {
        m_pushCharge = 0.0f; // reset when not actively charging
    }
    // Ray-AABB slab test: is the player looking at the physics cube?
    {
        glm::vec3 mn   = m_cube.aabbMin(), mx = m_cube.aabbMax();
        // Tiny epsilon prevents division by zero when looking along an axis
        glm::vec3 invD = { 1.f/(fwd.x+1e-9f), 1.f/(fwd.y+1e-9f), 1.f/(fwd.z+1e-9f) };
        glm::vec3 t0   = (mn - eye) * invD, t1 = (mx - eye) * invD;
        glm::vec3 tmin = glm::min(t0, t1), tmax = glm::max(t0, t1);
        float tN = std::max({tmin.x, tmin.y, tmin.z});
        float tF = std::min({tmax.x, tmax.y, tmax.z});
        m_lookingPC = (tF > 0.f && tN < tF && tN < INTERACT_DIST);
    }

    if (m_freecam.isActive()) {
        m_freecam.update(dt);
    } else {
        PlayerInput in{};
        in.forward   = (GetAsyncKeyState('W')         & 0x8000) != 0;
        in.back      = (GetAsyncKeyState('S')         & 0x8000) != 0;
        in.left      = (GetAsyncKeyState('A')         & 0x8000) != 0;
        in.right     = (GetAsyncKeyState('D')         & 0x8000) != 0;
        in.sprint    = (GetAsyncKeyState(VK_LCONTROL) & 0x8000) != 0;
        // Speed boost multiplier goes through PlayerInput so it affects
        // the walk/sprint blend inside Player::update, not just a post-multiply
        in.speedMult = m_effects.speedBoost ? m_effects.speedBoostMult : 1.0f;
        m_player.update(in, dt);
    }

    // Physics cube — gravity flip cancels+inverts by adding 2x GRAVITY_A upward
    if (m_cube.grabbed) {
        m_cube.updateGrab(grabTarget, dt);
    } else {
        m_cube.update(dt);
        if (m_effects.gravityFlipped)
            m_cube.vel.y += GRAVITY_A * dt * 2.0f;
    }

    m_decorations.resolvePlayerCollision(m_player.pos, CAP_RADIUS, CAP_HALF_H);
    resolveCollisions();
}

// ── Player/cube AABB collision resolution ─────────────────────────────────
// Minimal-axis separation. The 0.85f Y bias makes the cube harder to stand on.
// It's a fudge factor. It feels right. The physics professor would frown.
void Game::resolveCollisions() {
    if (m_cube.grabbed) return;

    glm::vec3 ext = m_cube.aabbExtents();
    const float SEP = 0.005f;

    float pL  = m_player.pos.x - CAP_RADIUS,  pR  = m_player.pos.x + CAP_RADIUS;
    float pB  = m_player.pos.y - CAP_HALF_H,  pT  = m_player.pos.y + CAP_HALF_H;
    float pFZ = m_player.pos.z - CAP_RADIUS,  pKZ = m_player.pos.z + CAP_RADIUS;
    float cL  = m_cube.pos.x - ext.x,  cR  = m_cube.pos.x + ext.x;
    float cB  = m_cube.pos.y - ext.y,  cT  = m_cube.pos.y + ext.y;
    float cFZ = m_cube.pos.z - ext.z,  cKZ = m_cube.pos.z + ext.z;

    float ox = std::min(pR, cR)   - std::max(pL,  cL);
    float oy = std::min(pT, cT)   - std::max(pB,  cB);
    float oz = std::min(pKZ, cKZ) - std::max(pFZ, cFZ);
    if (ox <= 0.f || oy <= 0.f || oz <= 0.f) return;

    float relX = m_player.pos.x - m_cube.pos.x;
    float relZ = m_player.pos.z - m_cube.pos.z;
    float oyb  = oy * 0.85f; // biased Y overlap

    if (ox <= oyb && ox <= oz) {
        // X-axis (side) separation
        float dir = (relX >= 0.f) ? 1.f : -1.f, half = (ox + SEP) * 0.5f;
        m_player.pos.x  += dir * half; m_cube.pos.x -= dir * half;
        m_cube.angVel.z -= dir * std::min(std::abs(m_player.vel.x), 6.f) * 0.4f;
    } else if (oyb <= ox && oyb <= oz) {
        // Y-axis (top/bottom) separation
        float relY = (m_player.pos.y - CAP_HALF_H * 0.5f) - m_cube.pos.y;
        if (relY >= 0.f) {
            m_player.pos.y += oy + SEP;
            if (m_player.vel.y < 0.f) m_player.vel.y = 0.f;
            m_player.onGround = true;
        } else {
            m_cube.pos.y += oy + SEP;
            if (m_cube.vel.y < 0.f) m_cube.vel.y *= -0.2f;
        }
    } else {
        // Z-axis (front/back) separation
        float dir = (relZ >= 0.f) ? 1.f : -1.f, half = (oz + SEP) * 0.5f;
        m_player.pos.z  += dir * half; m_cube.pos.z -= dir * half;
        m_cube.angVel.x += dir * std::min(std::abs(m_player.vel.z), 6.f) * 0.4f;
    }

    // Standing support tolerance to reduce onGround flicker on cube tops.
    float playerFeet = m_player.pos.y - CAP_HALF_H;
    float cubeTop    = m_cube.pos.y + ext.y;
    bool aboveTop    = playerFeet >= cubeTop - 0.06f && playerFeet <= cubeTop + 0.14f;
    if (aboveTop && std::abs(m_player.vel.y) < 2.0f) {
        m_player.onGround = true;
        m_player.pos.y = std::max(m_player.pos.y, cubeTop + CAP_HALF_H);
    }
}


// ═══════════════════════════════════════════════════════════════════════════
//  RENDER  — "Draw the things in the right order."
//             Easier said than done. Source: any graphics programmer.
// ═══════════════════════════════════════════════════════════════════════════

void Game::render() {
    if (m_minimised) return;
    if (!m_renderer.beginFrame()) return; // returns false during swapchain recreation

    // ── Loading screen ─────────────────────────────────────────────────────
    if (m_appState == AppState::Loading) {
        m_uiBatch.begin(W(), H());
        drawLoadingScreen(m_uiBatch);
        m_renderer.submitUI(m_uiBatch);
        m_renderer.endFrame();
        return;
    }

    // ── Title screen: world visible, player not rendered ───────────────────
    if (m_appState == AppState::Title) {
        glm::mat4 view = m_titleCam.viewMatrix();
        glm::mat4 proj = m_titleCam.projMatrix(70.f, (float)W() / std::max(H(), 1u));
        glm::vec3 sky = m_dayNight.skyColor(), sd = m_dayNight.sunDir();
        m_renderer.setSkyColor(sky.x, sky.y, sky.z); // keep clear color in sync on title screen
        float     amb = m_dayNight.ambientIntensity() * m_settings.AmbientLevel();
        glm::mat4 lv  = glm::lookAt(sd * 50.f, glm::vec3(0.f), glm::vec3(0.f,1.f,0.f));
        glm::mat4 lp  = glm::ortho(-25.f, 25.f, -25.f, 25.f, 0.1f, 200.f); lp[1][1] *= -1.f;
        glm::mat4 vpt2 = glm::transpose(proj * view);
        m_scene.frustum.planes[0] = vpt2[3] + vpt2[0]; m_scene.frustum.planes[1] = vpt2[3] - vpt2[0];
        m_scene.frustum.planes[2] = vpt2[3] + vpt2[1]; m_scene.frustum.planes[3] = vpt2[3] - vpt2[1];
        m_scene.frustum.planes[4] = vpt2[2];            m_scene.frustum.planes[5] = vpt2[3] - vpt2[2];
        glm::vec3 ep = m_titleCam.eyePos();
        UBOFrame u2{};
        u2.view = view; u2.proj = proj; u2.lightVP = lp * lv;
        u2.lightDir = { sd.x,  sd.y,  sd.z,  m_settings.SunIntensity() };
        u2.camPos   = { ep.x,  ep.y,  ep.z,  0.f };
        u2.skyCol   = { sky.x, sky.y, sky.z, amb };
        u2.gndCol   = { 0.28f, 0.22f, 0.16f, amb * 0.6f };
        u2.fogParams= { 60.f, 300.f, 0.08f, m_settings.Gamma() };
        u2.fogColor = { sky.x*0.5f, sky.y*0.5f, sky.z*0.5f, 0.f };
        m_renderer.updateFrameUBO(u2);
        m_scene.draw(m_renderer, false);
        m_decorations.draw(m_renderer, m_scene.frustum, ep, false);
        m_uiBatch.begin(W(), H());
        drawTitleScreen(m_uiBatch);
        if (m_uiScreen == UIScreen::Settings) drawSettings(m_uiBatch);
        if (m_alert.active)                   drawAlert(m_uiBatch);
        m_renderer.submitUI(m_uiBatch);
        m_renderer.endFrame();
        return;
    }

    // ── Gameplay: full render pipeline ─────────────────────────────────────
    glm::vec3 camEye = activeCamEye();

    // Shadow pass
    m_scene.draw(m_renderer, true);  m_scene.drawCube(m_renderer, true);
    m_decorations.draw(m_renderer, m_scene.frustum, camEye, true);
    m_cursedObjs.draw(m_renderer, true); m_shop.draw(m_renderer, true);
    if (m_freecam.isActive()) m_scene.drawCapsule(m_renderer, true);

    // Main pass
    m_scene.draw(m_renderer, false); m_scene.drawCube(m_renderer, false);
    m_decorations.draw(m_renderer, m_scene.frustum, camEye, false);
    m_cursedObjs.draw(m_renderer, false); m_shop.draw(m_renderer, false);
    if (m_freecam.isActive()) m_scene.drawCapsule(m_renderer, false);

    // UI layer
    m_uiBatch.begin(W(), H());
    if (!m_alert.active) {
        drawCrosshair(m_uiBatch); drawEnergyBar(m_uiBatch); drawSwingMeter(m_uiBatch);
        drawCurrencyHUD(m_uiBatch); drawDayNightHUD(m_uiBatch);
        drawHeldItem(m_uiBatch); drawActiveEffects(m_uiBatch);
        drawNotifications(m_uiBatch); drawHintsPanel(m_uiBatch);
        if (m_paused && m_uiScreen == UIScreen::PauseMenu) drawPauseMenu(m_uiBatch);
        if (m_uiScreen == UIScreen::Settings)              drawSettings(m_uiBatch);
        if (m_uiScreen == UIScreen::Shop)                  drawShopUI(m_uiBatch);
        if (m_uiScreen == UIScreen::Changelogs)            drawChangelogs(m_uiBatch);
        if (m_uiScreen == UIScreen::Credits)               drawCredits(m_uiBatch);
        if (m_uiScreen == UIScreen::Controls)              drawControls(m_uiBatch);
        drawVersionText(m_uiBatch);
    }
    if (m_notFault.active) drawNotYourFault(m_uiBatch);
    if (m_showDebug)       drawDebug(m_uiBatch);
    if (m_alert.active)    drawAlert(m_uiBatch);
    m_console.draw(m_uiBatch, W(), H(), m_totalTime); // always on top

    // Shift+F2/F3 debug overlays (drawn over everything including console)
    if (m_showDebugRender) drawDebugRender(m_uiBatch);
    if (m_showDebugWorld)  drawDebugWorld(m_uiBatch);

    // Scene fade-in: full black → transparent over 1.2s
    // Also covers the "playing but fade not complete" frame after title
    if (m_appState == AppState::Playing && m_sceneFade < 1.0f) {
        float alpha = 1.0f - m_sceneFade;
        m_uiBatch.rect(0, 0, (float)W(), (float)H(), 0.f, 0.f, 0.f, alpha);
    }

    m_renderer.submitUI(m_uiBatch);
    m_renderer.endFrame();
}


// ═══════════════════════════════════════════════════════════════════════════
//  CAMERA HELPERS
// ═══════════════════════════════════════════════════════════════════════════

glm::vec3 Game::activeCamEye() const {
    return m_freecam.isActive() ? m_freecam.state().pos : m_player.eyePos();
}
glm::vec3 Game::activeCamForward() const {
    if (m_freecam.isActive()) {
        float yr = glm::radians(m_freecam.state().yaw);
        float pr = glm::radians(m_freecam.state().pitch);
        return { cosf(pr)*sinf(yr), sinf(pr), cosf(pr)*cosf(yr) };
    }
    return m_player.forward();
}
glm::mat4 Game::activeCamView() const {
    if (m_freecam.isActive()) return m_freecam.viewMatrix();
    glm::vec3 eye = m_player.eyePos(), fwd = m_player.forward();
    return glm::lookAt(eye, eye + fwd, glm::vec3(0.f, 1.f, 0.f));
}


// ═══════════════════════════════════════════════════════════════════════════
//  FOCUS / CURSOR  — The eternal cursor-capture struggle. We won. Mostly.
// ═══════════════════════════════════════════════════════════════════════════

// setCap: capture or release mouse. No-op if already in requested state.
void Game::setCap(bool cap) {
    if (m_capMouse == cap) return;
    m_capMouse = cap;
    if (cap) {
        ShowCursor(FALSE);
        RECT wr; GetWindowRect(m_hwnd, &wr);
        m_lastCursor = { (wr.left+wr.right)/2, (wr.top+wr.bottom)/2 };
        SetCursorPos(m_lastCursor.x, m_lastCursor.y);
        ClipCursor(&wr);
    } else {
        ShowCursor(TRUE);
        ClipCursor(nullptr);
    }
}
// recenterCursor: move cursor to window center and refresh clip rect.
void Game::recenterCursor() {
    if (!m_capMouse) return;
    RECT wr; GetWindowRect(m_hwnd, &wr);
    m_lastCursor = { (wr.left+wr.right)/2, (wr.top+wr.bottom)/2 };
    SetCursorPos(m_lastCursor.x, m_lastCursor.y);
    ClipCursor(&wr);
}
void Game::onFocusLost() {
    setCap(false); // unconditional — no ghost cursor left behind
    if ((m_settings.WMode() == WinMode::Fullscreen || m_settings.WMode() == WinMode::Borderless)
        && !m_hiddenForFocusLoss) {
        ShowWindow(m_hwnd, SW_HIDE);
        m_hiddenForFocusLoss = true;
    }
    if (m_settings.PauseFocus() && m_appState == AppState::Playing && !m_paused) {
        m_paused = true; m_uiScreen = UIScreen::PauseMenu; m_pauseSel = 0;
    }
}
void Game::onFocusGained() {
    if (m_hiddenForFocusLoss) {
        ShowWindow(m_hwnd, IsIconic(m_hwnd) ? SW_RESTORE : SW_SHOW);
        SetForegroundWindow(m_hwnd);
        if (m_settings.WMode() == WinMode::Fullscreen || m_settings.WMode() == WinMode::Borderless) {
            int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
            HWND z = (m_settings.WMode() == WinMode::Fullscreen) ? HWND_TOPMOST : HWND_NOTOPMOST;
            SetWindowPos(m_hwnd, z, 0, 0, sw, sh,
                         SWP_FRAMECHANGED | SWP_SHOWWINDOW);
            m_renderer.resize((uint32_t)sw, (uint32_t)sh);
        }
        m_hiddenForFocusLoss = false;
    }
    if (m_minimised) return; // WM_SIZE will follow — don't capture yet
    if (m_appState == AppState::Playing && !m_paused
        && !m_alert.active && !m_console.isOpen())
        setCap(true);
}


// ═══════════════════════════════════════════════════════════════════════════
//  MISC HELPERS
// ═══════════════════════════════════════════════════════════════════════════

void Game::showAlert(bool warn, const char* msg, const char* sub,
                     bool hasNo, std::function<void(bool)> cb) {
    m_alert = { true, warn, hasNo };
    strncpy_s(m_alert.msg, sizeof(m_alert.msg), msg, _TRUNCATE);
    strncpy_s(m_alert.sub, sizeof(m_alert.sub), sub, _TRUNCATE);
    m_alert.cb = std::move(cb);
    setCap(false);
}
void Game::pushNotif(const std::string& t, float r, float g, float b, float d) {
    if ((int)m_notifs.size() >= 5) m_notifs.erase(m_notifs.begin()); // oldest out
    m_notifs.push_back({ t, 0.f, d, r, g, b });
    m_console.log(t, m_totalTime); // mirror to console for devs
}
void Game::applySettings() {
    m_settings.Commit();
    m_settings.Save();
    m_renderer.setVSync(m_settings.VSync());

    // Apply window mode change for real this time
    WinMode wm = m_settings.WMode();
    ResEntry res = m_settings.Res();
    DWORD style = 0;
    DWORD exStyle = 0;
    int   x=0, y=0, w=(int)res.w, h=(int)res.h;
    uint32_t renderW = res.w, renderH = res.h;
    int   sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);

    if (wm == WinMode::Windowed) {
        style   = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        exStyle = 0;
        RECT wr{0,0,w,h}; AdjustWindowRectEx(&wr, style, FALSE, exStyle);
        w = wr.right-wr.left; h = wr.bottom-wr.top;
        x = (sw-w)/2; y = (sh-h)/2;
        m_winBaseX = x; m_winBaseY = y;
        m_fullscreen = false;
    } else {
        // Borderless windowed: no border, no caption, covers full screen
        // Spec: "windowed borderless" — window positioned at 0,0, sized to screen
        style   = WS_POPUP;
        exStyle = 0;
        if (wm == WinMode::Borderless) {
            x=0; y=0; w=sw; h=sh;
        } else {
            // Fullscreen: same as borderless for simplicity; Vulkan handles exclusive fullscreen
            x=0; y=0; w=sw; h=sh;
        }
        renderW = (uint32_t)w;
        renderH = (uint32_t)h;
        m_winBaseX = 0; m_winBaseY = 0;
        m_fullscreen = (wm == WinMode::Fullscreen || wm == WinMode::Borderless);
    }

    SetWindowLongA(m_hwnd, GWL_STYLE,   style);
    SetWindowLongA(m_hwnd, GWL_EXSTYLE, exStyle);
    HWND zOrder = HWND_NOTOPMOST;
    if (wm == WinMode::Fullscreen) zOrder = HWND_TOPMOST;
    SetWindowPos(m_hwnd,
        zOrder,
        x, y, w, h,
        SWP_FRAMECHANGED | SWP_SHOWWINDOW);

    // Trigger swapchain recreation at new resolution
    m_renderer.resize(renderW, renderH);

    m_uiScreen = m_paused ? UIScreen::PauseMenu : UIScreen::None;
}
// Hold-repeat for Settings slider navigation: 0.38s delay, then ~12/s
void Game::updateSettingsNav(float dt) {
    if (m_uiScreen != UIScreen::Settings || m_alert.active || m_console.isOpen()) return;
    bool keys[4] = {
        bool((GetAsyncKeyState(VK_UP)    | GetAsyncKeyState('W')) & 0x8000),
        bool((GetAsyncKeyState(VK_DOWN)  | GetAsyncKeyState('S')) & 0x8000),
        bool((GetAsyncKeyState(VK_LEFT)  | GetAsyncKeyState('A')) & 0x8000),
        bool((GetAsyncKeyState(VK_RIGHT) | GetAsyncKeyState('D')) & 0x8000),
    };
    for (int i = 0; i < 4; i++) {
        if (keys[i]) {
            m_navHold[i] += dt;
            if (!m_navHeld[i] || m_navHold[i] > 0.38f) {
                if (!m_navHeld[i]) m_navHold[i] = 0.f;
                int row = m_settings.selRow;
                if      (i == 0) m_settings.selRow = std::max(0, row - 1);
                else if (i == 1) m_settings.selRow = std::min(Settings::COUNT - 1, row + 1);
                else if (i == 2) m_settings.Step(row, -1);
                else             m_settings.Step(row, +1);
                m_navHeld[i] = true;
                if (m_navHold[i] > 0.38f) m_navHold[i] = 0.3f; // ~12/s repeat
            }
        } else { m_navHeld[i] = false; m_navHold[i] = 0.f; }
    }
}


// ═══════════════════════════════════════════════════════════════════════════
//  INPUT
// ═══════════════════════════════════════════════════════════════════════════

void Game::onMouseMove() {
    if (!m_capMouse) return;
    POINT cur; GetCursorPos(&cur);
    int dx = cur.x - m_lastCursor.x, dy = cur.y - m_lastCursor.y;
    if (!dx && !dy) return;
    if (m_freecam.isActive()) m_freecam.applyMouseDelta(dx, dy);
    else {
        m_player.yaw   -= dx * 0.12f; // negate: mouse-right → yaw decreases → looks right
        m_player.pitch  = std::clamp(m_player.pitch - dy * 0.12f, -89.f, 89.f);
    }
    recenterCursor();
}
void Game::onMouseClick(bool right) {
    if (m_console.isOpen() && !right) return;
    if (m_appState == AppState::Playing && !m_paused && !m_alert.active && !right) {
        if (m_items.hasItem()) m_items.useItem();
        else setCap(true);
    }
}

void Game::onKeyDown(WPARAM vk) {
    bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

    // Console eats all input when open (Shift+F4 toggles — see F4 block below)
    if (m_console.isOpen()) { m_console.handleKey(vk); return; }

    // Alert intercept — works from every screen state, no exceptions
    if (m_alert.active) {
        if (vk == VK_RETURN) {
            m_alert.active = false;
            if (m_alert.cb) m_alert.cb(true);
            if (m_appState == AppState::Playing && !m_paused && !m_alert.active) setCap(true);
        }
        if (vk == VK_BACK && m_alert.hasNo) {
            m_alert.active = false; if (m_alert.cb) m_alert.cb(false);
        }
        return;
    }

    // ── Title screen ───────────────────────────────────────────────────────
    if (m_appState == AppState::Title) {
        if (m_titleFadeIn < 0.5f) return; // not faded in, ignore
        if (m_uiScreen == UIScreen::Settings) {
            if (vk == VK_RETURN) m_settings.Step(m_settings.selRow, +1);
            if (vk == VK_SPACE) {
                bool dirty = false;
                for (int i = 0; i < Settings::COUNT; i++)
                    if (m_settings.pend[i] != m_settings.e[i].cur) { dirty = true; break; }
                if (dirty)
                    showAlert(false, "Apply changes?", "", true,
                              [this](bool y) { if (y) applySettings(); else m_settings.Reset(); });
                else pushNotif("No changes to apply.", 0.6f,0.6f,0.7f,2.5f);
            }
            if (vk == VK_ESCAPE && !m_kEsc) {
                m_kEsc = true; m_settings.Reset(); m_uiScreen = UIScreen::None;
            }
            return;
        }
        if (vk == VK_UP   || vk == 'W') m_titleSel = std::max(0, m_titleSel - 1);
        if (vk == VK_DOWN || vk == 'S') m_titleSel = std::min(2, m_titleSel + 1);
        if (vk == VK_RETURN || vk == VK_SPACE) {
            switch (m_titleSel) {
            case 0: m_appState = AppState::Playing; m_sceneFade = 0.f; m_audio.playBGM(BGMTrack::Gameplay); setCap(true); break;
            case 1: m_uiScreen = UIScreen::Settings; m_settings.Reset(); break;
            case 2: showAlert(true, "Exit?", "", true,
                              [this](bool y) { if (y) { m_settings.Save(); m_running = false; DestroyWindow(m_hwnd); } });
                    break;
            }
        }
        return;
    }

    // ── Shop ───────────────────────────────────────────────────────────────
    if (m_uiScreen == UIScreen::Shop) {
        if (vk == VK_UP   || vk == 'W') m_shopSel = std::max(0, m_shopSel - 1);
        if (vk == VK_DOWN || vk == 'S') m_shopSel = std::min((int)m_shop.stock().size() - 1, m_shopSel + 1);
        if (vk == VK_RETURN) {
            ItemType g; std::string msg;
            if (m_shop.tryBuy(m_shopSel, money(), diamonds(), g, msg)) {
                pushNotif(msg, 0.4f, 1.f, 0.5f, 5.f);
                m_audio.playSFX("buy"); // ka-ching! that's real money leaving your account
                if (g != ItemType::None && !m_items.hasItem())
                    m_items.heldItem = { g, 1 };
            } else pushNotif(msg, 1.f, 0.4f, 0.3f);
        }
        if (vk == VK_ESCAPE) m_uiScreen = UIScreen::None;
        return;
    }

    // ── Changelogs ─────────────────────────────────────────────────────────
    if (m_uiScreen == UIScreen::Changelogs) {
        if (vk == VK_UP    || vk == 'W') m_clScroll = std::max(0, m_clScroll - 1);
        if (vk == VK_DOWN  || vk == 'S') m_clScroll++;
        if (vk == VK_LEFT  || vk == 'A') m_clTab = std::max(0, m_clTab - 1);
        if (vk == VK_RIGHT || vk == 'D') m_clTab++;
        if (vk == VK_ESCAPE && !m_kEsc) { m_kEsc = true; m_uiScreen = UIScreen::PauseMenu; }
        return;
    }

    // ── Credits ────────────────────────────────────────────────────────────
    if (m_uiScreen == UIScreen::Credits) {
        if (vk == VK_RETURN || vk == VK_SPACE)
            ShellExecuteA(nullptr, "open", "https://claude.ai", nullptr, nullptr, SW_SHOWNORMAL);
        if (vk == VK_ESCAPE && !m_kEsc) { m_kEsc = true; m_uiScreen = UIScreen::PauseMenu; }
        return;
    }

    // ── Controls rebind ────────────────────────────────────────────────────
    if (m_uiScreen == UIScreen::Controls) {
        if (m_ctrlWaiting) {
            if (vk == VK_ESCAPE) m_ctrlWaiting = false;
            else { m_binds[m_ctrlSel].vk = vk; m_ctrlWaiting = false; }
            return;
        }
        if (vk == VK_UP    || vk == 'W') m_ctrlSel = std::max(0, m_ctrlSel - 1);
        if (vk == VK_DOWN  || vk == 'S') m_ctrlSel = std::min(KEYBIND_COUNT - 1, m_ctrlSel + 1);
        if (vk == VK_RETURN || vk == VK_SPACE) m_ctrlWaiting = true;
        if (vk == VK_ESCAPE && !m_kEsc) { m_kEsc = true; m_uiScreen = UIScreen::PauseMenu; }
        return;
    }

    // ── Settings ───────────────────────────────────────────────────────────
    if (m_uiScreen == UIScreen::Settings) {
        if (vk == VK_RETURN || vk == VK_SPACE)
            showAlert(false, "Apply changes?", "", true,
                      [this](bool y) { if (y) applySettings(); else m_settings.Reset(); });
        if (vk == VK_ESCAPE && !m_kEsc) {
            m_kEsc = true; m_settings.Reset();
            m_uiScreen = m_paused ? UIScreen::PauseMenu : UIScreen::None;
        }
        return;
    }

    // ── Pause menu ─────────────────────────────────────────────────────────
    if (m_paused && m_uiScreen == UIScreen::PauseMenu) {
        if (vk == VK_UP   || vk == 'W') m_pauseSel = std::max(0, m_pauseSel - 1);
        if (vk == VK_DOWN || vk == 'S') m_pauseSel = std::min(5, m_pauseSel + 1);
        if (vk == VK_RETURN || vk == VK_SPACE) {
            switch (m_pauseSel) {
            case 0: m_paused = false; m_uiScreen = UIScreen::None; setCap(true); break;
            case 1: m_uiScreen = UIScreen::Settings; m_settings.Reset();          break;
            case 2: m_uiScreen = UIScreen::Controls; m_ctrlSel = 0; m_ctrlWaiting = false; break;
            case 3: m_uiScreen = UIScreen::Changelogs; m_clScroll = 0; m_clTab = 0; break;
            case 4: m_uiScreen = UIScreen::Credits;                               break;
            case 5:
                showAlert(true, "Exit?", "Progress auto-saved.", true,
                          [this](bool y) { if (y) { saveGame(); m_running = false; DestroyWindow(m_hwnd); } });
                break;
            }
        }
        if (vk == VK_ESCAPE && !m_kEsc) {
            m_kEsc = true; m_paused = false; m_uiScreen = UIScreen::None; setCap(true);
        }
        return;
    }

    // ── In-game keys ───────────────────────────────────────────────────────
    if (vk == VK_ESCAPE && !m_kEsc) {
        m_kEsc = true; m_paused = true; m_uiScreen = UIScreen::PauseMenu;
        m_pauseSel = 0; setCap(false);
    }
    // Shift+F1=general debug, F2=render, F3=world, F4=dev console
    if (vk == VK_F1 && !m_kF1) {
        m_kF1 = true;
        if (shift) m_showDebug = !m_showDebug;
    }
    if (vk == VK_F2 && !m_kF2) {
        m_kF2 = true;
        if (shift) m_showDebugRender = !m_showDebugRender;
    }
    if (vk == VK_F3 && !m_kF3) {
        m_kF3 = true;
        if (shift) m_showDebugWorld = !m_showDebugWorld;
    }
    if (vk == VK_F4 && !m_kF4) {
        m_kF4 = true;
        if (shift) {
            m_console.toggle();
            if (m_console.isOpen()) setCap(false);
            else if (m_appState == AppState::Playing && !m_paused) setCap(true);
        }
    }
    if (vk == 'P' && shift && !m_kP) {
        m_kP = true;
        if (m_freecam.isActive()) m_freecam.disable();
        else m_freecam.enable(m_player.eyePos(), m_player.yaw, m_player.pitch);
    }

    // Interact (E) — priority: cursed > crate > shop > push cube
    if (vk == 'E' && !m_kE) {
        m_kE = true;
        if (!m_freecam.isActive()) {
            if (m_cursedObjs.playerNearAny()) {
                if (m_cursedObjs.tryInteract(m_player.pos) >= 0)
                    m_audio.playSFX("cursed"); // sus sound — earned it
            } else if (m_lookingCrate) {
                CrateReward rw;
                if (m_crates.tryInteract(m_player.pos, rw, diamondsUnlocked())) {
                    switch (rw.type) {
                    case CrateReward::Type::Money:
                        money() += rw.money; m_save.totalMoneyEarned += rw.money;
                        pushNotif(rw.message, 0.4f, 1.f, 0.4f); break;
                    case CrateReward::Type::Diamond:
                        diamonds() += rw.diamonds; pushNotif(rw.message, 0.4f, 0.9f, 1.f, 6.f); break;
                    case CrateReward::Type::UniqueItem:
                        if (!m_items.hasItem()) {
                            m_items.heldItem = rw.item; pushNotif(rw.message, 1.f, 0.7f, 0.3f, 5.f);
                        } else pushNotif("Already holding an item — use it first!", 1.f, 0.5f, 0.2f);
                        break;
                    case CrateReward::Type::Nothing:
                        pushNotif(rw.message, 0.6f, 0.6f, 0.6f); break;
                    }
                    m_save.timesOpenedCrates++;
                }
            } else if (m_shop.playerNear() && !m_shop.isAway()) {
                m_shopSel = 0; m_shop.openShop(); m_uiScreen = UIScreen::Shop;
            } else if (m_lookingPC && !m_cube.grabbed && m_cube.pushCD <= 0.f) {
                float mult = m_effects.cubeLauncherArmed ? 5.f : 1.f;
                glm::vec3 fwd = activeCamForward();
                // Swing-meter push: E-down starts charge, E-up releases
                // The actual push is in onKeyUp (see below). Here we just arm it.
                m_pushReady   = true;
                m_pushCharge  = 0.0f;
            }
        }
    }
    if (vk == 'F' && !m_kF) {
        m_kF = true;
        if (m_cube.grabbed) m_cube.release(); else if (m_lookingPC) m_cube.grabbed = true;
    }
    if (vk == VK_SPACE && m_player.onGround && !m_freecam.isActive())
        m_player.vel.y = JUMP_VEL;
    if (vk == 'R' && shift && !m_kR && m_freecam.isActive()) {
        m_kR = true; m_player.pos = m_freecam.playerTeleportPos(); m_player.vel = {};
    }
}

void Game::onKeyUp(WPARAM vk) {
    if (vk == VK_ESCAPE) m_kEsc = false; if (vk == VK_F1) m_kF1 = false;
    if (vk == VK_F2) m_kF2 = false; if (vk == VK_F3) m_kF3 = false; if (vk == VK_F4) m_kF4 = false;
    if (vk == 'E') {
        m_kE = false;
        // Release E while aimed at cube → fire push with charged power
        if (m_pushReady && m_lookingPC && !m_cube.grabbed && m_cube.pushCD <= 0.f) {
            float mult = m_effects.cubeLauncherArmed ? 5.f : 1.f;
            float power = 0.3f + m_pushCharge * 0.7f; // 30% min, 100% at full charge
            glm::vec3 fwd = activeCamForward();
            m_cube.applyPushImpulse(
                glm::normalize(glm::vec3(fwd.x, fwd.y * 0.4f + 0.15f, fwd.z))
                * mult * power);
            m_effects.cubeLauncherArmed = false;
            m_audio.playSFX("push");
            m_save.timesUsedItems++;
        }
        m_pushReady  = false;
        m_pushCharge = 0.0f;
    } if (vk == 'F')   m_kF  = false;
    if (vk == 'P')       m_kP   = false; if (vk == 'R')   m_kR  = false;
}


// ═══════════════════════════════════════════════════════════════════════════
//  WndProc  — OS message dispatcher. Where Windows meets engine.
// ═══════════════════════════════════════════════════════════════════════════

LRESULT Game::handleMsg(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    // Console needs WM_CHAR for text input (not WM_KEYDOWN)
    if (msg == WM_CHAR && m_console.isOpen()) {
        m_console.handleChar((char)wp); return 0;
    }
    switch (msg) {
    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) == SC_CLOSE) {
            showAlert(true, "Exit?", "Progress will be auto-saved.", true,
                      [this](bool y) { if (y) { saveGame(); m_running = false; DestroyWindow(m_hwnd); } });
            return 0;
        }
        break;
    case WM_DESTROY:
        m_running = false; PostQuitMessage(0); return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) onFocusLost(); else onFocusGained(); return 0;
    case WM_SIZE:
        if (wp == SIZE_MINIMIZED) {
            m_minimised = true; setCap(false);
        } else {
            bool wasMin = m_minimised; m_minimised = false;
            uint32_t nw = LOWORD(lp), nh = HIWORD(lp);
            if (nw > 0 && nh > 0) {
                m_renderer.resize(nw, nh);
                if (wasMin && m_appState == AppState::Playing
                    && !m_paused && !m_alert.active && !m_console.isOpen())
                    setCap(true);
            }
        }
        return 0;
    case WM_MOVE:
        // Track base position for earthquake shake to return to
        if (!m_effects.earthquake && m_settings.WMode() == WinMode::Windowed) {
            m_winBaseX = (short)LOWORD(lp); m_winBaseY = (short)HIWORD(lp);
        }
        return 0;
    case WM_EXITSIZEMOVE:
        if (m_capMouse) recenterCursor(); return 0;
    case WM_KEYDOWN:     onKeyDown(wp);       return 0;
    case WM_KEYUP:       onKeyUp(wp);         return 0;
    case WM_LBUTTONDOWN: onMouseClick(false); return 0;
    case WM_RBUTTONDOWN: onMouseClick(true);  return 0;
    case WM_MOUSEMOVE:   onMouseMove();       return 0;
    }
    return DefWindowProcA(hw, msg, wp, lp);
}


// ═══════════════════════════════════════════════════════════════════════════
//  UI DRAW  — All 2D, all screen-space pixels, top-left origin.
//             UIBatch converts to NDC internally. Don't think about it.
// ═══════════════════════════════════════════════════════════════════════════

// ── IT'S NOT YOUR FAULT ────────────────────────────────────────────────────
// Red diagonal text flood. Active 14 seconds. Fades out.
// It was green in Part 3. A mistake. We do not speak of it.
void Game::drawNotYourFault(UIBatch& b) {
    if (!m_notFault.active) return;
    float W2 = (float)W(), H2 = (float)H();
    float alpha = m_notFault.ending
        ? std::max(0.f, 1.f - m_notFault.endTimer / NotYourFaultState::END_FADE)
        : std::min(1.f, m_notFault.timer / 0.3f);

    b.rect(0, 0, W2, H2, 0.6f*alpha, 0.f, 0.f, alpha*0.45f); // red vignette

    const char* msg = "IT'S NOT YOUR FAULT";
    const float sc  = 1.8f;
    float mw = b.strW(sc, msg);
    float scroll = m_notFault.textScroll;
    // Diagonal flood: rows scroll from top-right toward bottom-left
    int rows = (int)(H2 / (GLYPH_PX*sc+3.f)) + 6;
    for (int row = 0; row < rows; row++) {
        float baseY = row*(GLYPH_PX*sc+3.f) - fmodf(scroll, H2+100.f);
        float baseX = W2 - (float)row*(mw*0.45f) - fmodf(scroll*0.7f, mw+30.f);
        float jX = sinf(m_notFault.horrorPhase*2.3f + (float)row*0.9f)*4.f;
        float jY = cosf(m_notFault.horrorPhase*1.7f + (float)row*1.2f)*3.f;
        float ra = alpha*0.65f*(0.7f+0.3f*sinf((float)row*0.5f+m_notFault.horrorPhase));
        b.str(baseX+jX, baseY+jY, sc, msg, 1.0f*alpha, 0.1f, 0.1f, ra);
    }
    // Central large text: drifts closer over time (horror zoom)
    float zoom  = 1.0f + m_notFault.timer*0.04f*(1.f-m_notFault.timer/NotYourFaultState::DURATION);
    float bigSc = std::min(9.f, 5.0f*zoom);
    float tw = b.strW(bigSc, msg);
    float cx = W2/2.f + sinf(m_notFault.horrorPhase*0.8f)*8.f;
    float cy = H2/2.f + cosf(m_notFault.horrorPhase*0.6f)*6.f;
    b.str(cx-tw/2+4, cy-GLYPH_PX*bigSc/2+4, bigSc, msg, 0.f,0.f,0.f, alpha*0.6f, bigSc); // shadow
    b.str(cx-tw/2,   cy-GLYPH_PX*bigSc/2,   bigSc, msg, 1.f,0.05f,0.05f, alpha, bigSc);  // red text
}

void Game::drawLoadingScreen(UIBatch& b) {
    float W2=(float)W(), H2=(float)H(), CX=W2/2.f;
    b.rect(0,0,W2,H2, 0.05f,0.05f,0.07f,1.f);
    const char* ttl="Vulkan 1.1+ Scene"; float tw=b.strW(3.f,ttl,3.f);
    b.str(CX-tw/2,H2/2-80, 3.f,ttl, 0.28f,0.7f,1.f,1.f,3.f);
    float prog=std::clamp(m_loadProgress.load(),0.f,1.f);
    float BW=360,BH=14,BX=CX-BW/2,BY=H2/2;
    b.rect(BX-2,BY-2,BW+4,BH+4, 0.1f,0.1f,0.15f,1.f);
    b.rect(BX,BY,BW*prog,BH, 0.28f,0.66f,1.f,1.f);
    const char* st = prog<0.60f ? "Loading textures..."
                   : prog<0.85f ? "Building world geometry..."
                   : prog<0.95f ? "Waking the NPC from their nap..."
                   :              "Almost there (no really this time)...";
    float sw=b.strW(1.4f,st); b.str(CX-sw/2,BY+BH+12, 1.4f,st, 0.6f,0.7f,0.8f,0.8f);
    const char* ver="v0.07-alpha";
    b.str(W2-b.strW(1.1f,ver)-6,H2-GLYPH_PX*1.1f-6, 1.1f,ver, 0.3f,0.4f,0.5f,0.6f);
}

void Game::drawTitleScreen(UIBatch& b) {
    float W2=(float)W(), H2=(float)H(), CX=W2/2.f;
    float alpha=std::clamp(m_titleFadeIn,0.f,1.f);
    b.rect(0,0,W2,H2*0.35f, 0,0,0,alpha*0.60f);
    b.rect(0,H2*0.5f,W2,H2*0.5f, 0,0,0,alpha*0.65f);
    const char* ttl="VULKAN 1.1+ SCENE"; float tw=b.strW(3.2f,ttl,3.2f);
    b.str(CX-tw/2+3,42+3, 3.2f,ttl, 0,0,0,alpha*0.5f,3.2f);
    b.str(CX-tw/2,42,     3.2f,ttl, 0.28f,0.72f,1.f,alpha,3.2f);
    const char* sub="v0.07-alpha"; float sw=b.strW(1.5f,sub);
    b.str(CX+tw/2-sw-2, 42+GLYPH_PX*3.2f-GLYPH_PX*1.5f+2, 1.5f,sub, 0.4f,0.7f,1.f,alpha*0.7f);
    static const char*  items[3] = {"Play","Settings","Quit"};
    static const float  ic[3][3] = {{0.8f,1.f,0.8f},{0.6f,0.8f,1.f},{1.f,0.5f,0.4f}};
    float iy=H2*0.55f, IH=GLYPH_PX*2.4f+12;
    for (int i=0;i<3;i++) {
        bool sel=(i==m_titleSel); float fa=sel?1.f:0.55f, sc=sel?2.4f:2.f;
        float iw=b.strW(sc,items[i]);
        if (sel){b.rect(CX-iw/2-16,iy-4,iw+32,IH,.04f,.08f,.16f,alpha*0.75f);b.rect(CX-iw/2-16,iy-4,3,IH,ic[i][0],ic[i][1],ic[i][2],alpha);}
        b.str(CX-iw/2,iy+4,sc,items[i],ic[i][0]*fa,ic[i][1]*fa,ic[i][2]*fa,alpha*fa);
        iy+=IH+4;
    }
    if (alpha>0.7f){const char* h="Up/Down  Enter select  ` = console";float hw=b.strW(1.2f,h);b.str(CX-hw/2,H2-24,1.2f,h,0.4f,0.4f,0.5f,alpha*0.6f);}
    if (SaveSystem::hasSave()){char ms[48];if(m_save.money>=1e6)snprintf(ms,sizeof(ms),"Save: $%.1fM",m_save.money/1e6);else snprintf(ms,sizeof(ms),"Save: $%.0f",m_save.money);float mw=b.strW(1.2f,ms);b.str(CX-mw/2,H2-44,1.2f,ms,0.4f,0.8f,0.5f,alpha*0.7f);}
    std::string ts=m_dayNight.timeStr()+" "+DayNight::periodName(m_dayNight.timeOfDay());
    b.str(W2-b.strW(1.1f,ts.c_str())-6,H2-22, 1.1f,ts.c_str(), 0.6f,0.7f,0.8f,alpha*0.5f);
}

void Game::drawCurrencyHUD(UIBatch& b) {
    char ms[32]; double mn=money();
    if(mn>=1e9)snprintf(ms,sizeof(ms),"$%.2fB",mn/1e9);
    else if(mn>=1e6)snprintf(ms,sizeof(ms),"$%.2fM",mn/1e6);
    else if(mn>=1e3)snprintf(ms,sizeof(ms),"$%.1fK",mn/1e3);
    else snprintf(ms,sizeof(ms),"$%.0f",mn);
    float mw=b.strW(1.6f,ms); b.rect(8,8,mw+16,26,.03f,.06f,.04f,.80f); b.str(16,13,1.6f,ms,0.4f,1.f,0.5f,1.f);
    if (diamondsUnlocked()) {
        char ds[16]; snprintf(ds, sizeof(ds), "%d", diamonds());
        float dw = b.strW(1.4f, ds);
        float bx = 8.f + mw + 20.f, by = 8.f;
        b.rect(bx, by, dw + 32.f, 26.f, .03f,.04f,.08f,.80f);
        // Small drawn diamond: 4-point polygon made of 2 triangles using rects
        // Center: bx+10, by+13. Diamond ~8px wide, 10px tall
        float dc = bx + 10.f, dcy = by + 13.f;
        // Upper half (top triangle)
        b.rect(dc-1.f, dcy-5.f, 2.f, 5.f, .4f,.9f,1.f,1.f);   // thin top spike
        b.rect(dc-3.f, dcy-3.f, 6.f, 3.f, .4f,.9f,1.f,1.f);   // upper body
        // Lower half (bottom triangle)
        b.rect(dc-3.f, dcy,     6.f, 3.f, .4f,.9f,1.f,.85f);  // lower body
        b.rect(dc-1.f, dcy+3.f, 2.f, 3.f, .3f,.7f,.9f,.80f);  // bottom spike
        // Count text
        b.str(bx + 22.f, 13.f, 1.4f, ds, .4f,.9f,1.f,1.f);
    }
}

void Game::drawDayNightHUD(UIBatch& b) {
    std::string ts=m_dayNight.timeStr(); bool night=m_dayNight.isNight();
    float r=night?0.4f:1.f, g=night?0.5f:0.9f, bc=night?0.9f:0.3f;
    float tw=b.strW(1.3f,ts.c_str()); b.rect((float)W()-tw-14,8,tw+12,22,.03f,.04f,.08f,.75f);
    b.str((float)W()-tw-8,12, 1.3f,ts.c_str(), r,g,bc,0.85f);
}

void Game::drawCrosshair(UIBatch& b) {
    if(m_paused||m_freecam.isActive()) return;
    float cx=W()*0.5f,cy=H()*0.5f,a=7.f,t=1.5f;
    float cr=m_lookingPC?1.f:.9f, cg=m_lookingPC?.68f:.9f, cb2=m_lookingPC?.18f:.9f;
    b.rect(cx-a,cy-t/2,a*2,t,cr,cg,cb2,0.72f); b.rect(cx-t/2,cy-a,t,a*2,cr,cg,cb2,0.72f);
    auto lbl=[&](const char* s,float lr,float lg,float lb){float lw=b.strW(1.5f,s);b.rect(cx-lw/2-4,cy+14,lw+8,GLYPH_PX*1.5f+6,0,0,0,.55f);b.str(cx-lw/2,cy+17,1.5f,s,lr,lg,lb,1.f);};
    if(m_cursedObjs.playerNearAny()&&!m_lookingPC) lbl("[E] Interact [?]",0.8f,0.4f,1.f);
    else if(m_lookingCrate&&!m_lookingPC) lbl("[E] Open Crate",1.f,0.85f,0.3f);
    else if(m_shop.playerNear()&&!m_lookingPC&&!m_shop.isAway())
        lbl((std::string("[E] Talk to ")+m_shop.npcName()).c_str(),0.85f,0.75f,0.3f);
    else if(m_lookingPC&&!m_cube.grabbed) lbl("[E] Push   [F] Grab",1.f,.82f,.35f);
    else if(m_cube.grabbed) lbl("[F] Release",1.f,.82f,.35f);
}

void Game::drawEnergyBar(UIBatch& b) {
    if(m_paused||m_player.energy>=ENERGY_MAX) return;
    float e=m_player.energy/ENERGY_MAX,bw=120,bh=6,bx=W()/2.f-60,by=H()/2.f+22;
    b.rect(bx-1,by-1,bw+2,bh+2,.05f,.05f,.05f,.55f); b.rect(bx,by,bw*e,bh,1-e,e,0,.88f);
    if(e<=0.f){float xw=b.strW(1.2f,"No Stamina");b.str(W()/2.f-xw/2,by+bh+3,1.2f,"No Stamina",1.f,.35f,.25f,.8f);}
}

void Game::drawVersionText(UIBatch& b) {
    // Spec: alpha 0% title, 60% gameplay. Only called from gameplay path, so always 0.60f.
    const char* ver="v0.07-alpha"; float vw=b.strW(1.2f,ver);
    b.str(W()-vw-6.f,H()-GLYPH_PX*1.2f-6.f, 1.2f,ver, 1,1,1,0.60f);
}

void Game::drawHintsPanel(UIBatch& b) {
    if(m_paused) return;
    struct Row{const char* k,*d;}; std::vector<Row> rows;
    if(m_freecam.isActive()){rows={{"Sh+R","Teleport"},{"Q/E","Fly"},{"Sh+P","Exit FC"}};}
    else {
        rows.push_back({"ESC","Pause"}); rows.push_back({"F1","Debug"});
        rows.push_back({"Sh+P","Freecam"}); rows.push_back({"`","Console"});
        if(m_player.onGround)rows.push_back({"Space","Jump"}); rows.push_back({"Ctrl","Sprint"});
        if(m_cursedObjs.playerNearAny()) rows.push_back({"E","Interact [?]"});
        else if(m_lookingCrate&&!m_lookingPC) rows.push_back({"E","Open Crate"});
        else if(m_shop.playerNear()&&!m_shop.isAway()) rows.push_back({"E","Shop"});
        else if(m_lookingPC&&!m_cube.grabbed){rows.push_back({"E","Push"});rows.push_back({"F","Grab"});}
        if(m_cube.grabbed) rows.push_back({"F","Release"});
        if(m_items.hasItem()) rows.push_back({"LClick","Use item"});
    }
    if(rows.empty()) return;
    const float SC=1.3f,LH=14.f,PAD=8.f; float maxW=0;
    for(auto& r:rows)maxW=std::max(maxW,b.strW(SC,r.k)+(r.d[0]?6.f+b.strW(SC,r.d):0));
    float pw=maxW+PAD*2,ph=(float)rows.size()*LH+PAD+10,px=(float)W()-pw-6,py=(float)H()-ph-6;
    b.rect(px,py,pw,ph,.03f,.06f,.13f,.66f); float ty=py+5;
    for(auto& r:rows){float kw=b.strW(SC,r.k);b.str(px+PAD,ty,SC,r.k,.90f,.82f,.35f,1.f);if(r.d[0])b.str(px+PAD+kw+5,ty,SC,r.d,.68f,.68f,.72f,.85f);ty+=LH;}
}

void Game::drawHeldItem(UIBatch& b) {
    if(!m_items.hasItem()) return;
    const ItemDef* def=ItemSystem::getDef(m_items.heldItem.type);
    float bx=10,by=(float)H()-70;
    b.rect(bx,by,190,52,.04f,.06f,.12f,.88f); b.rect(bx,by,190,2,.28f,.62f,1.f,.85f);
    b.str(bx+8,by+6, 1.3f,"HELD ITEM",0.4f,0.7f,1.f,0.8f);
    b.str(bx+8,by+22,1.6f,def->name,1.f,0.85f,0.3f,1.f);
    b.str(bx+8,by+38,1.1f,"[LClick] to use",0.6f,0.6f,0.65f,0.7f);
}

void Game::drawActiveEffects(UIBatch& b) {
    float x=10,y=(float)H()-135.f;
    auto bar=[&](const char* lbl,float frac,float r,float g,float bc){
        b.rect(x,y,160,20,.04f,.06f,.12f,.80f); b.rect(x,y,160*frac,20,r,g,bc,.85f);
        b.str(x+4,y+4,1.2f,lbl,1,1,1,0.9f); y-=24;};
    if(m_effects.speedBoost)   bar("SPEED BOOST",  m_effects.speedFraction(),  0.3f,0.6f,1.f);
    if(m_effects.freezeTime)   bar("TIME FREEZE",  m_effects.freezeFraction(), 0.6f,0.9f,1.f);
    if(m_effects.earthquake)   bar("EARTHQUAKE",   m_effects.quakeFraction(),  1.f,0.5f,0.2f);
    if(m_effects.blackHole)    bar("BLACK HOLE",   m_effects.holeFraction(),   0.4f,0.2f,0.8f);
    if(m_effects.diamondPulse) bar("DIAMOND PULSE",m_effects.diamondFraction(),0.3f,0.9f,1.f);
    if(m_effects.gravityFlipped){b.rect(x,y,160,20,.04f,.06f,.12f,.80f);b.str(x+4,y+4,1.2f,"GRAVITY FLIP",0.8f,0.4f,1.f,0.9f);y-=24;}
    if(m_effects.cubeLauncherArmed){b.rect(x,y,160,20,.04f,.06f,.12f,.80f);b.str(x+4,y+4,1.2f,"LAUNCHER ARMED",1.f,0.6f,0.2f,0.9f);}
}

void Game::drawNotifications(UIBatch& b) {
    float by=(float)H()*0.28f;
    for(auto& n:m_notifs){
        float t=n.timer/n.duration;
        float al=t<0.15f?t/0.15f:(t>0.75f?(1.f-t)/0.25f:1.f); al*=0.92f;
        float nw=b.strW(1.5f,n.text.c_str()); float nx=(float)W()/2.f-nw/2.f;
        b.rect(nx-8,by-4,nw+16,GLYPH_PX*1.5f+8,.04f,.05f,.08f,al*0.85f);
        b.str(nx,by,1.5f,n.text.c_str(),n.r,n.g,n.b,al); by+=GLYPH_PX*1.5f+14.f;
    }
}

void Game::drawShopUI(UIBatch& b) {
    float W2=(float)W(),H2=(float)H(),CX=W2/2;
    b.rect(0,0,W2,H2,.05f,.05f,.07f,.55f);
    float PH=std::min(H2-60.f,(float)(m_shop.stock().size()+2)*38.f+80.f);
    float PW=630,PX=CX-PW/2,PY=H2/2-PH/2;
    b.rect(PX,PY,PW,PH,.04f,.07f,.14f,.97f); b.rect(PX,PY,PW,2,.85f,.75f,.3f,.92f);
    std::string shopTitle=m_shop.npcName()+"'s Wares"; float tw=b.strW(2.2f,shopTitle.c_str());
    b.str(CX-tw/2,PY+10,2.2f,shopTitle.c_str(),.85f,.75f,.3f,1.f);
    const char* dlg=m_shop.dialogue().c_str(); float dw=b.strW(1.3f,dlg);
    b.str(CX-dw/2,PY+10+GLYPH_PX*2.2f+6,1.3f,dlg,.7f,.7f,.65f,.85f);
    if(m_shop.visitCount()>=3){const char* dc="Loyalty discount: 15% off";float dcw=b.strW(1.1f,dc);b.str(CX-dcw/2,PY+10+GLYPH_PX*2.2f+24,1.1f,dc,.4f,.85f,.4f,.8f);}
    float iy=PY+10+GLYPH_PX*2.2f+42;
    for(int i=0;i<(int)m_shop.stock().size();i++){auto& item=m_shop.stock()[i];bool sel=(i==m_shopSel);if(sel)b.rect(PX+6,iy-2,PW-12,34,.14f,.20f,.40f,.55f);float fa=sel?1.f:.55f;b.str(PX+14,iy+4,1.5f,item.name.c_str(),.9f,.85f,.4f*fa,fa);char pr[32];if(item.priceDiamonds>0)snprintf(pr,sizeof(pr),"$%.0f + %dD",item.priceMoney,(int)item.priceDiamonds);else snprintf(pr,sizeof(pr),"$%.0f",item.priceMoney);float prw=b.strW(1.3f,pr);b.str(PX+PW-prw-14,iy+6,1.3f,pr,0.4f,1.f,0.5f,fa);iy+=38;}
    if(m_shopSel<(int)m_shop.stock().size()){float dw2=b.strW(1.2f,m_shop.stock()[m_shopSel].description.c_str());b.str(CX-dw2/2,PY+PH-30,1.2f,m_shop.stock()[m_shopSel].description.c_str(),.5f,.6f,.7f,.75f);}
    const char* h="Up/Down  Enter buy  ESC close"; float hw=b.strW(1.1f,h); b.str(CX-hw/2,PY+PH-14,1.1f,h,.3f,.3f,.4f,.65f);
}

void Game::drawPauseMenu(UIBatch& b) {
    float CX=W()/2.f, CY=H()/2.f;
    b.rect(0,0,(float)W(),(float)H(),.08f,.08f,.10f,.42f);
    float PW=340,PH=310,PX=CX-PW/2,PY=CY-PH/2;
    b.rect(PX,PY,PW,PH,.04f,.06f,.12f,.96f); b.rect(PX,PY,PW,2,.22f,.52f,1.f,.92f);
    const char* ttl="PAUSED"; float tw=b.strW(2.8f,ttl); b.str(CX-tw/2,PY+10,2.8f,ttl,1,1,1,1);
    // Quick stats
    char ms[48]; double mn=money();
    if(mn>=1e6)snprintf(ms,sizeof(ms),"$%.2fM",mn/1e6); else snprintf(ms,sizeof(ms),"$%.0f",mn);
    if(diamondsUnlocked()){char ds[16];snprintf(ds,sizeof(ds)," | %dD",diamonds());strncat(ms,ds,sizeof(ms)-strlen(ms)-1);}
    float mw=b.strW(1.3f,ms); b.str(CX-mw/2,PY+10+GLYPH_PX*2.8f+4,1.3f,ms,0.4f,1.f,0.5f,0.75f);
    std::string ts=m_dayNight.timeStr()+" "+DayNight::periodName(m_dayNight.timeOfDay());
    float tw2=b.strW(1.1f,ts.c_str()); b.str(CX-tw2/2,PY+10+GLYPH_PX*2.8f+20,1.1f,ts.c_str(),0.5f,0.7f,0.9f,0.65f);
    static const char* items[6]={"Continue","Settings","Controls","Changelogs","Credits","Exit Game"};
    static const float ic[6][3]={{.82f,.95f,.82f},{.60f,.80f,1.f},{.70f,.85f,1.f},{.75f,.80f,.95f},{.80f,.75f,.90f},{1.f,.42f,.38f}};
    float iy=PY+10+GLYPH_PX*2.8f+38, IH=GLYPH_PX*2.0f+12;
    for(int i=0;i<6;i++){bool sel=(i==m_pauseSel);if(sel){b.rect(PX+10,iy-3,PW-20,IH+6,.12f,.24f,.48f,.55f);b.rect(PX+10,iy-3,3,IH+6,.28f,.62f,1.f,.92f);}float fa=sel?1.f:.50f,sc=sel?2.0f:1.8f,iw=b.strW(sc,items[i]);b.str(CX-iw/2,iy+4,sc,items[i],ic[i][0]*fa,ic[i][1]*fa,ic[i][2]*fa,fa);iy+=IH+4;}
}

void Game::drawSettings(UIBatch& b) {
    float W2=(float)W(),H2=(float)H(),CX=W2/2; b.rect(0,0,W2,H2,.07f,.07f,.09f,.55f);
    const int NC=Settings::CAT_COUNT; const float RH=34,CH=22,PW=580,PH=Settings::COUNT*RH+NC*CH+58,PX=CX-PW/2,PY=H2/2-PH/2;
    b.rect(PX,PY,PW,PH,.04f,.08f,.18f,.97f); b.rect(PX,PY,PW,2,.22f,.52f,1.f,.92f);
    const char* ttl="Settings"; float tw=b.strW(2.f,ttl); b.str(CX-tw/2,PY+10,2.f,ttl,.28f,.80f,1.f,1.f);
    float ty=PY+40;
    for(int i=0;i<Settings::COUNT;i++){
        for(int c=0;c<NC;c++)if(Settings::CATS[c].beforeRow==i){b.rect(PX+8,ty+CH/2-1,PW-16,1,.15f,.30f,.55f,.55f);b.str(PX+14,ty+3,1.3f,Settings::CATS[c].label,.30f,.65f,1.f,.80f);ty+=CH;break;}
        auto& e=m_settings.e[i]; bool sel=(i==m_settings.selRow),pend=(e.type==SettingType::Dropdown&&m_settings.pend[i]!=e.cur);
        if(sel)b.rect(PX+6,ty-2,PW-12,RH-4,.14f,.28f,.55f,.52f);
        float nr=sel?1.f:.72f,ng=sel?1.f:.72f,nb=sel?1.f:.76f;
        b.str(PX+14,ty+8,1.5f,e.name,nr,ng,nb,1.f);
        if(e.type==SettingType::Slider){float t2=(e.sv-e.smin)/(e.smax-e.smin),BX=PX+PW-300+48,BW=138;b.rect(BX,ty+11,BW,4,.18f,.32f,.52f,1.f);b.rect(BX,ty+11,BW*t2,4,.28f,.66f,1.f,1.f);b.rect(BX+BW*t2-4,ty+7,8,12,.80f,.95f,1.f,1.f);char vb[20];snprintf(vb,sizeof(vb),"%.1f%s",e.sv,e.sunit);b.str(BX+BW+8,ty+8,1.4f,vb,.90f,.90f,.95f,1.f);}
        else{int pc=m_settings.pend[i];char db[48];snprintf(db,sizeof(db),"< %s >",e.opts[pc]);float vr=pend?1.f:nr,vg=pend?.78f:ng,vb2=pend?.18f:nb;float dw=b.strW(1.5f,db);b.str(PX+PW-dw-14,ty+8,1.5f,db,vr,vg,vb2,1.f);}
        ty+=RH;
    }
    // Bottom hint — per spec: Enter=select/cycle, Space=apply, ESC=back
    const char* h="Up/Dn move  Left/Rt change  Enter cycle  Space apply  ESC back";
    float hw=b.strW(1.05f,h); b.str(CX-hw/2,PY+PH-15,1.05f,h,.30f,.30f,.38f,.60f);
}

void Game::drawAlert(UIBatch& b) {
    float W2=(float)W(),H2=(float)H(),CX=W2/2,CY=H2/2;
    b.rect(0,0,W2,H2,0,0,0,1.f);
    float hr=m_alert.isWarn?1.f:.35f,hg=m_alert.isWarn?.75f:.72f,hb=m_alert.isWarn?0.f:1.f;
    const char* hdr=m_alert.isWarn?"WARNING":"ALERT"; float htw=b.strW(5.f,hdr,5.f);
    b.str(CX-htw/2,CY-72,5.f,hdr,hr,hg,hb,1.f,5.f); b.rect(CX-200,CY-18,400,2,hr,hg,hb,.45f);
    float mw=b.strW(1.8f,m_alert.msg); b.str(CX-mw/2,CY-6,1.8f,m_alert.msg,1,1,1,.95f);
    if(m_alert.sub[0]){float sw=b.strW(1.3f,m_alert.sub);b.str(CX-sw/2,CY+16,1.3f,m_alert.sub,.72f,.72f,.78f,.85f);}
    // Alert keybind hint: bottom-right corner so it never overlaps the alert message
    const char* hint = m_alert.hasNo ? "Enter = Yes   Backspace = No" : "Enter to continue";
    float hw = b.strW(1.3f,hint);
    float hx = W2 - hw - 14.f, hy = H2 - GLYPH_PX*1.3f - 10.f;
    b.rect(hx-8.f, hy-4.f, hw+16.f, GLYPH_PX*1.3f+10.f, .07f,.07f,.10f,.88f);
    b.str(hx, hy, 1.3f, hint, .75f,.75f,.82f, 1.f);
}

void Game::drawDebug(UIBatch& b) {
    const float SC=1.f,LH=12.5f,PAD=6.f; float PX=8,PY=8,PW=390,PH=400;
    b.rect(PX,PY,PW,PH,.02f,.04f,.12f,.92f); b.rect(PX,PY,PW,2,.18f,.45f,1.f,.90f);
    float tx=PX+PAD,ty=PY+5; char buf[200];
    b.str(tx,ty,SC,"[ DEBUG v0.07 | ` = console ]",.25f,.60f,1.f,1.f); ty+=LH;
    b.rect(tx,ty,PW-PAD*2,1,.05f,.12f,.30f,1.f); ty+=LH;
    snprintf(buf,sizeof(buf),"FPS:%.1f  CPU:%.2fms  PhysStep:%.3fms",m_fps,m_cpuMs,m_lastPhysStepMs); b.str(tx,ty,SC,buf,.22f,.65f,1.f,1.f); ty+=LH;
    snprintf(buf,sizeof(buf),"Pos: %+.2f %+.2f %+.2f",m_player.pos.x,m_player.pos.y,m_player.pos.z); b.str(tx,ty,SC,buf,.22f,.70f,1.f,1.f); ty+=LH;
    const char* ms2=m_player.moveState==PMoveState::Idle?"IDLE":m_player.moveState==PMoveState::Walking?"WALK":"SPRINT";
    snprintf(buf,sizeof(buf),"Move:%s  E:%.0f  Gnd:%d  SpeedX:%.1f",ms2,m_player.energy,(int)m_player.onGround,m_effects.speedBoost?m_effects.speedBoostMult:1.f);
    b.str(tx,ty,SC,buf,.18f,.60f,.90f,1.f); ty+=LH;
    snprintf(buf,sizeof(buf),"Cube: %+.2f %+.2f %+.2f  Grab:%d",m_cube.pos.x,m_cube.pos.y,m_cube.pos.z,(int)m_cube.grabbed); b.str(tx,ty,SC,buf,1.f,.65f,.28f,1.f); ty+=LH;
    char msb[32]; double mn=money(); if(mn>=1e6)snprintf(msb,sizeof(msb),"$%.2fM",mn/1e6); else snprintf(msb,sizeof(msb),"$%.0f",mn);
    snprintf(buf,sizeof(buf),"Money:%s  Diamonds:%d (unlocked:%s)",msb,diamonds(),diamondsUnlocked()?"yes":"no"); b.str(tx,ty,SC,buf,.4f,1.f,.5f,1.f); ty+=LH;
    snprintf(buf,sizeof(buf),"Cursed:%d/5  Time:%s %s",m_save.cursedFound,m_dayNight.timeStr().c_str(),DayNight::periodName(m_dayNight.timeOfDay())); b.str(tx,ty,SC,buf,.4f,.7f,1.f,1.f); ty+=LH;
    snprintf(buf,sizeof(buf),"NPC:%s dist:%.1f near:%s away:%s visits:%d",m_shop.npcName().c_str(),glm::distance(m_player.pos,m_shop.pos()),m_shop.playerNear()?"Y":"N",m_shop.isAway()?"Y":"N",m_shop.visitCount()); b.str(tx,ty,SC,buf,.6f,.8f,.6f,1.f); ty+=LH;
    int ac=(int)std::count_if(m_crates.crates().begin(),m_crates.crates().end(),[](const WorldCrate& c){return c.active;});
    snprintf(buf,sizeof(buf),"Crates:%d  Dec drawn:%d culled:%d",ac,m_decorations.drawnCount(),m_decorations.culledCount()); b.str(tx,ty,SC,buf,.20f,.60f,.95f,1.f); ty+=LH;
    snprintf(buf,sizeof(buf),"Ambient:%.2f  Sun:%.2f  Gamma:%.1f",m_settings.AmbientLevel(),m_settings.SunIntensity(),m_settings.Gamma()); b.str(tx,ty,SC,buf,.9f,.85f,.4f,1.f); ty+=LH;
    snprintf(buf,sizeof(buf),"Cap:%d  Min:%d  FS:%d  SimT:%.1f  Acc:%.3f",(int)m_capMouse,(int)m_minimised,(int)m_fullscreen,m_simTime,m_physAccum); b.str(tx,ty,SC,buf,.5f,.5f,.6f,1.f); ty+=LH;
    if(m_items.hasItem()){snprintf(buf,sizeof(buf),"Held:%s",ItemSystem::getDef(m_items.heldItem.type)->name);b.str(tx,ty,SC,buf,1.f,.7f,.3f,1.f);ty+=LH;}
    if(m_notFault.active){snprintf(buf,sizeof(buf),"IT'S NOT YOUR FAULT t=%.1f ending:%d",m_notFault.timer,(int)m_notFault.ending);b.str(tx,ty,SC,buf,1.f,.2f,.2f,1.f);}
}


// ═══════════════════════════════════════════════════════════════════════════
//  CHANGELOGS
// ═══════════════════════════════════════════════════════════════════════════

void Game::drawChangelogs(UIBatch& b) {
    struct VerEntry { const char* ver; const char* date; const char** lines; int count; };
    static const char* v007[] = {
        "New: Vulkan 1.1/1.2/1.3 renderer — full rewrite from DX11",
        "New: 750x750m world (was 44x44m) — actually somewhere to get lost",
        "New: Title screen camera — 10 world points, varied angles, smooth lerp",
        "New: Loading screen with progress bar and background compilation",
        "New: Day/night cycle (1 real minute = 1 game hour)",
        "New: NPC merchant — 6h stay, 2h away, loyalty discount, 30 US names",
        "New: Cursed standing-stone objects (monolith shape, corrupted verts+tex)",
        "New: Diamonds unlocked by finding all 5 cursed objects",
        "New: World crates — loot drops, 30-90s respawn, max 8 active",
        "New: 9 unique items (Speed Boost, Black Hole, Time Freeze, etc.)",
        "New: Dense world decorations — 20 tree clusters across 750m, fences, signs",
        "New: IT'S NOT YOUR FAULT easter egg — RED diagonal flood, horror movement",
        "New: Developer console (Shift+F4) — C++ syntax, runtime tweaks",
        "New: Shift+F1=debug, F2=render stats, F3=world data, F4=console",
        "New: Swing-meter push — hold E to charge (0-100%), release to push",
        "New: Scene fade-in (1.2s) when entering gameplay from title",
        "New: Ground grass texture (750m world, tiled every 4m)",
        "New: Dynamic sky clear color tracks day/night",
        "New: Diamond HUD icon — drawn shape, not just text",
        "New: buy_sfx on purchase, cursedobj_sfx on cursed interact",
        "New: Lighting settings — Ambient, Sun Intensity, Gamma",
        "New: Application icon embedded in exe",
        "Fix: Text vertical flip (UIBatch NDC Y negation removed)",
        "Fix: Left/right movement and mouse look were both mirrored",
        "Fix: applySettings now works at runtime — SetWindowLongA + resize",
        "Fix: Settings: Enter=cycle value, Space=apply, dirty check prevents no-op alert",
        "Fix: Alert hint relocated to bottom-right (was overlapping content)",
        "Fix: Push SFX no longer fires on shop open or crate open",
        "Fix: Tree canopy was rendering with rock texture (wrong PropType slot)",
        "Fix: All decoration clusters now spread across full 750m world",
        "Fix: Cursed objects 200-330m from spawn (were 5-20m away)",
        "Fix: VOID_Y changed from -300m to -85m per spec",
        "Fix: SC_CLOSE exit alert works from all screens including loading",
        "Fix: GLM FORCE_DEFAULT_ALIGNED_GENTYPES removed (compute_vec_mul errors)",
        "Fix: Settings XML escape sequences broken by bad sed injection",
    };
    static const char* v061[] = {
        "New: Physics cube with quaternion rotation and rotated AABB",
        "New: Sub-step collision (up to 4 substeps per frame)",
        "New: Throw velocity smoothing (grabVelSmooth)",
        "New: Push impulse + angular spin from player velocity",
        "New: Head-bob, landing shake, sprint/fall FOV effects",
        "New: PCF soft shadows (5x5 kernel, SHADOW_DIM=2048)",
        "New: Hemisphere GI ambient + linear fog + gamma correction in shaders",
        "New: Settings XML — full human-readable names and true/false booleans",
        "New: Slider hold-repeat (0.38s initial delay, 12 changes/second)",
        "New: Alert overlay (Enter=Yes, Backspace=No) — works from any screen",
        "Fix: Ground collision hard clamp prevents falling through",
        "Fix: Void respawn at VOID_Y=-300 preserves XZ position",
    };
    static const char* v060[] = {
        "Initial DX11 release",
        "Basic physics cube with ground collision",
        "First-person player: jump, sprint, head-bob",
        "Passive income money system (removed in v0.07 — crates are more fun)",
        "Basic UI rendered with 8x8 bitmap font",
        "Sphere-based frustum culling",
    };
    static VerEntry entries[] = {
        {"v0.07-alpha","2026",v007,(int)(sizeof(v007)/sizeof(*v007))},
        {"v0.6.1",     "2025",v061,(int)(sizeof(v061)/sizeof(*v061))},
        {"v0.6.0",     "2025",v060,(int)(sizeof(v060)/sizeof(*v060))},
    };
    static const int VER_COUNT=3;
    m_clTab=std::clamp(m_clTab,0,VER_COUNT-1);

    float W2=(float)W(),H2=(float)H(),CX=W2/2;
    b.rect(0,0,W2,H2,.06f,.06f,.09f,.60f);
    float PW=680,PH=H2-80,PX=CX-PW/2,PY=40;
    b.rect(PX,PY,PW,PH,.03f,.06f,.14f,.97f); b.rect(PX,PY,PW,2,.22f,.52f,1.f,.92f);
    const char* ttl="Changelogs"; float tw=b.strW(2.f,ttl); b.str(CX-tw/2,PY+10,2.f,ttl,.28f,.80f,1.f,1.f);

    float tabX=PX+12,tabY=PY+42;
    for(int i=0;i<VER_COUNT;i++){bool sel=(i==m_clTab);float tw2=b.strW(1.5f,entries[i].ver)+16;b.rect(tabX,tabY,tw2,22,sel?.14f:.06f,sel?.28f:.10f,sel?.52f:.20f,sel?.90f:.60f);if(sel)b.rect(tabX,tabY,tw2,2,.28f,.62f,1.f,1.f);b.str(tabX+8,tabY+5,1.5f,entries[i].ver,sel?1.f:.55f,sel?1.f:.55f,sel?1.f:.60f,1.f);tabX+=tw2+4;}
    float lineY=tabY+30; b.rect(PX+6,lineY,PW-12,1,.15f,.30f,.55f,.55f); lineY+=8;

    auto& ve=entries[m_clTab]; char vh[64]; snprintf(vh,sizeof(vh),"%s  (%s)",ve.ver,ve.date);
    b.str(PX+14,lineY,1.6f,vh,.85f,.80f,.35f,1.f); lineY+=GLYPH_PX*1.6f+8;

    const float LH=15.f,PAD=14.f;
    float logH=PY+PH-lineY-36; int maxVisible=(int)(logH/LH);
    m_clScroll=std::clamp(m_clScroll,0,std::max(0,ve.count-maxVisible));
    int start=std::max(0,ve.count-maxVisible-m_clScroll),end2=std::min(ve.count,start+maxVisible);
    for(int i=start;i<end2;i++){
        float r=0.72f,g=0.80f,bc=0.90f;
        if(strncmp(ve.lines[i],"New:",4)==0){r=0.50f;g=0.90f;bc=0.55f;}
        else if(strncmp(ve.lines[i],"Fix:",4)==0){r=1.0f;g=0.75f;bc=0.35f;}
        else if(strncmp(ve.lines[i],"Rem:",4)==0){r=1.0f;g=0.45f;bc=0.45f;}
        b.str(PX+PAD,lineY,1.2f,ve.lines[i],r,g,bc,0.9f); lineY+=LH;
    }
    if(ve.count>maxVisible){char si[32];snprintf(si,sizeof(si),"[%d/%d]  Up/Down scroll",std::min(end2,ve.count),ve.count);b.str(PX+PAD,PY+PH-26,1.1f,si,.35f,.40f,.55f,.65f);}
    const char* h="Left/Right change version   ESC back"; float hw=b.strW(1.1f,h); b.str(CX-hw/2,PY+PH-14,1.1f,h,.30f,.35f,.40f,.65f);
}


// ═══════════════════════════════════════════════════════════════════════════
//  CREDITS  — Who made this. Spoiler: two people and a lot of the internet.
// ═══════════════════════════════════════════════════════════════════════════

void Game::drawCredits(UIBatch& b) {
    float W2=(float)W(),H2=(float)H(),CX=W2/2;
    b.rect(0,0,W2,H2,.06f,.06f,.09f,.60f);
    float PW=520,PH=480,PX=CX-PW/2,PY=H2/2-PH/2;
    b.rect(PX,PY,PW,PH,.03f,.06f,.14f,.97f); b.rect(PX,PY,PW,2,.22f,.52f,1.f,.92f);
    const char* ttl="Credits"; float tw=b.strW(2.4f,ttl);
    b.str(CX-tw/2,PY+14,2.4f,ttl,.85f,.75f,.30f,1.f);
    b.rect(PX+20,PY+14+GLYPH_PX*2.4f+6,PW-40,1,.30f,.40f,.60f,.45f);
    struct CreditRow{const char* role;const char* name;float r,g,bc;};
    static const CreditRow rows[]={
        {"Creator",         "Oorecco",                      0.90f,0.85f,0.40f},
        {"Developer",       "Claude AI",                    0.40f,0.80f,1.00f},
        {"",                "",                             0,0,0},
        {"Engine",          "Vulkan 1.1+",                  0.70f,0.70f,0.75f},
        {"Shader compiler", "shaderc (runtime GLSL)",       0.70f,0.70f,0.75f},
        {"Math library",    "GLM",                          0.70f,0.70f,0.75f},
        {"Image loading",   "stb_image (single header)",    0.70f,0.70f,0.75f},
        {"Audio",           "FMOD Studio (optional)",       0.70f,0.70f,0.75f},
        {"",                "",                             0,0,0},
        {"Font",            "8x8 bitmap (public domain)",   0.60f,0.65f,0.70f},
        {"Skybox",          "Daylight Box (public domain)", 0.60f,0.65f,0.70f},
        {"",                "",                             0,0,0},
        {"Special Thanks",  "The internet, for existing",   0.55f,0.55f,0.65f},
    };
    float ty=PY+14+GLYPH_PX*2.4f+20;
    for(auto& row:rows){if(!row.role[0]){ty+=10;continue;}float rw=b.strW(1.3f,row.role);b.str(PX+30,ty,1.3f,row.role,.55f,.60f,.75f,0.75f);b.str(PX+30+rw+12,ty,1.4f,row.name,row.r,row.g,row.bc,1.f);ty+=GLYPH_PX*1.4f+6;}
    float bw=180,bh=30,bx=CX-bw/2,bby=PY+PH-60;
    b.rect(bx,bby,bw,bh,.08f,.18f,.40f,.85f); b.rect(bx,bby,bw,2,.28f,.62f,1.f,.90f);
    const char* link="claude.ai"; float lw=b.strW(1.6f,link); b.str(bx+(bw-lw)/2,bby+8,1.6f,link,0.40f,0.80f,1.00f,1.f);
    const char* lsub="[ Open in browser (Enter) ]"; float lsw=b.strW(1.1f,lsub); b.str(CX-lsw/2,bby+bh+4,1.1f,lsub,.35f,.45f,.65f,.70f);
    const char* esc="ESC back"; float ew=b.strW(1.1f,esc); b.str(CX-ew/2,PY+PH-14,1.1f,esc,.30f,.35f,.40f,.60f);
}


// ═══════════════════════════════════════════════════════════════════════════
//  CONTROLS REBIND
// ═══════════════════════════════════════════════════════════════════════════

// VK code → readable name. "VK_0x01C" is not readable. "Enter" is readable.
static std::string vkName(WPARAM vk) {
    if (vk == 0) return "---"; // unbound
    struct { WPARAM vk; const char* name; } tbl[] = {
        {VK_SPACE,"Space"},{VK_LCONTROL,"L.Ctrl"},{VK_RCONTROL,"R.Ctrl"},
        {VK_LSHIFT,"L.Shift"},{VK_RSHIFT,"R.Shift"},{VK_ESCAPE,"Escape"},
        {VK_RETURN,"Enter"},{VK_BACK,"Bksp"},{VK_TAB,"Tab"},{VK_DELETE,"Del"},
        {VK_HOME,"Home"},{VK_END,"End"},
        {VK_UP,"Up"},{VK_DOWN,"Down"},{VK_LEFT,"Left"},{VK_RIGHT,"Right"},
        {VK_F1,"F1"},{VK_F2,"F2"},{VK_F3,"F3"},{VK_F4,"F4"},
        {VK_F5,"F5"},{VK_F6,"F6"},{VK_F7,"F7"},{VK_F8,"F8"},
    };
    for(auto& e:tbl) if(vk==e.vk) return e.name;
    if(vk>='A'&&vk<='Z'){char s[2]={(char)vk,0};return s;}
    if(vk>='0'&&vk<='9'){char s[2]={(char)vk,0};return s;}
    char buf[16]; snprintf(buf,sizeof(buf),"VK_%02X",(unsigned)vk); return buf;
}

void Game::drawControls(UIBatch& b) {
    float W2=(float)W(),H2=(float)H(),CX=W2/2;
    b.rect(0,0,W2,H2,.06f,.06f,.09f,.60f);
    float PW=560,PH=420,PX=CX-PW/2,PY=H2/2-PH/2;
    b.rect(PX,PY,PW,PH,.03f,.06f,.14f,.97f); b.rect(PX,PY,PW,2,.22f,.52f,1.f,.92f);
    const char* ttl="Controls"; float tw=b.strW(2.f,ttl); b.str(CX-tw/2,PY+12,2.f,ttl,.28f,.80f,1.f,1.f);
    const char* note="(Freecam and Teleport require Shift)"; float nw=b.strW(1.1f,note);
    b.str(CX-nw/2,PY+12+GLYPH_PX*2.f+4,1.1f,note,.45f,.50f,.60f,.70f);
    float ty=PY+12+GLYPH_PX*2.f+22; b.rect(PX+8,ty,PW-16,1,.15f,.30f,.55f,.45f); ty+=8;
    const float RH=36,PAD=18;
    for(int i=0;i<KEYBIND_COUNT;i++){
        bool sel=(i==m_ctrlSel); if(sel)b.rect(PX+6,ty-2,PW-12,RH,.12f,.22f,.45f,.55f);
        float ar=sel?1.f:.70f,ag=sel?1.f:.72f,abc=sel?1.f:.76f;
        b.str(PX+PAD,ty+9,1.4f,m_binds[i].action,ar,ag,abc,1.f);
        std::string kn=vkName(m_binds[i].vk); float kw=b.strW(1.5f,kn.c_str())+16;
        float kx=PX+PW-kw-PAD;
        if(m_ctrlWaiting&&sel){
            float flash=fmodf(m_totalTime*3.f,1.f)<0.5f?1.f:0.4f;
            b.rect(kx-30,ty+4,kw+60,RH-10,.18f,.36f,.60f,flash*.80f);
            b.str(kx-22,ty+9,1.3f,"Press any key",0.4f,0.9f,1.f,flash);
        } else {
            b.rect(kx,ty+4,kw,RH-10,.08f,.14f,.30f,.80f); b.rect(kx,ty+4,kw,2,.22f,.50f,1.f,.70f);
            float tw2=b.strW(1.5f,kn.c_str()); b.str(kx+(kw-tw2)/2,ty+9,1.5f,kn.c_str(),.85f,.90f,1.f,1.f);
        }
        ty+=RH+2;
    }
    const char* h="Up/Down select   Enter rebind   ESC cancel / back";
    float hw=b.strW(1.1f,h); b.str(CX-hw/2,PY+PH-14,1.1f,h,.30f,.35f,.40f,.65f);
}


// ─────────────────────────────────────────────────────────────────────────
//  SWING METER
// ─────────────────────────────────────────────────────────────────────────
void Game::drawSwingMeter(UIBatch& b) {
    // Swing meter only shows when E is held and aimed at the cube
    if (!m_pushReady || !m_lookingPC || m_cube.grabbed) return;

    float cx = W() * 0.5f;
    float by = H() * 0.5f + 36.f; // just below crosshair
    const float BW = 110.f, BH = 10.f;
    float bx = cx - BW * 0.5f;

    // Background track
    b.rect(bx - 2.f, by - 2.f, BW + 4.f, BH + 4.f, 0.f, 0.f, 0.f, 0.55f);
    b.rect(bx, by, BW, BH, 0.15f, 0.15f, 0.20f, 0.85f);

    // Charge fill — color shifts from green → yellow → red as it charges
    float t = m_pushCharge;
    float r = std::min(1.0f, t * 2.0f);       // 0→red as charge increases
    float g2 = std::max(0.0f, 1.0f - t);       // 1→0
    float bl = 0.1f;
    b.rect(bx, by, BW * t, BH, r, g2, bl, 0.95f);

    // Thumb marker
    b.rect(bx + BW * t - 2.f, by - 2.f, 4.f, BH + 4.f, 1.f, 1.f, 1.f, 0.9f);

    // Label
    const char* lbl = t < 0.35f ? "CHARGING..." : t < 0.75f ? "READY" : "FULL POWER!";
    float lw = b.strW(1.3f, lbl);
    b.str(cx - lw * 0.5f, by + BH + 4.f, 1.3f, lbl, r + 0.3f, g2 + 0.2f, 0.3f, 0.9f);
}


// ─────────────────────────────────────────────────────────────────────────
//  SHIFT+F2 — RENDER DEBUG OVERLAY
// ─────────────────────────────────────────────────────────────────────────
void Game::drawDebugRender(UIBatch& b) {
    // Shift+F2: rendering stats — draw calls, vertex counts, lighting, timing.
    // No swapchain internals here — that's Vulkan's business, not yours.
    const float SC = 1.f, LH = 12.5f, PAD = 6.f;
    float PX = (float)W() - 410.f, PY = 8.f, PW = 400.f, PH = 230.f;
    b.rect(PX, PY, PW, PH, .02f,.04f,.14f,.93f);
    b.rect(PX, PY, PW, 2.f, .85f,.55f,.20f,.90f);
    float tx = PX + PAD, ty = PY + 5.f;
    char buf[200];
    b.str(tx, ty, SC, "[ RENDER  Shift+F2 ]", .85f,.55f,.20f,1.f); ty += LH;
    b.rect(tx, ty, PW - PAD*2.f, 1.f, .12f,.18f,.40f,1.f); ty += LH;

    // Frame timing
    snprintf(buf, sizeof(buf), "FPS: %.1f   CPU: %.2f ms   PhysStep: %.3f ms",
             m_fps, m_cpuMs, m_lastPhysStepMs);
    b.str(tx,ty,SC,buf,.90f,.75f,.30f,1.f); ty+=LH;

    // Resolution + FOV (screen, not swapchain internals)
    snprintf(buf, sizeof(buf), "Resolution: %ux%u   FOV: %.0f deg   Aspect: %.3f",
             W(), H(), m_settings.FOV_(), (float)W()/(float)std::max(H(),1u));
    b.str(tx,ty,SC,buf,.70f,.70f,.85f,1.f); ty+=LH;

    // Draw call counts — the meat of a render debug panel
    int drawn   = m_decorations.drawnCount();
    int culled  = m_decorations.culledCount();
    int total   = drawn + culled;
    float ratio = total > 0 ? (float)drawn/(float)total*100.f : 0.f;
    snprintf(buf, sizeof(buf), "Props drawn: %d  culled: %d  total: %d  (%.0f%% visible)",
             drawn, culled, total, ratio);
    b.str(tx,ty,SC,buf,.40f,.85f,.55f,1.f); ty+=LH;

    // UI vertex budget
    snprintf(buf, sizeof(buf), "UI verts: %u  (solid:%u  text:%u)  budget:%d",
             m_uiBatch.totalVerts(), m_uiBatch.solidCount(),
             m_uiBatch.textCount(), VulkanRenderer::UI_MAX_VERTS);
    b.str(tx,ty,SC,buf,.55f,.70f,.90f,1.f); ty+=LH;

    // Render distance + shadow
    snprintf(buf, sizeof(buf), "Render dist: %.0f m   Shadow dim: %u   Fog: %.2f",
             m_settings.FarZ(), m_settings.ShadowDim(), m_settings.FogDensity());
    b.str(tx,ty,SC,buf,.70f,.70f,.80f,1.f); ty+=LH;

    // Lighting
    snprintf(buf, sizeof(buf), "Ambient: %.2f   Sun: %.2f   Gamma: %.2f",
             m_settings.AmbientLevel(), m_settings.SunIntensity(), m_settings.Gamma());
    b.str(tx,ty,SC,buf,.70f,.70f,.80f,1.f); ty+=LH;

    // Sky + sun
    glm::vec3 sky = m_dayNight.skyColor();
    snprintf(buf, sizeof(buf), "Sky: %.2f %.2f %.2f   Sun elev: %.3f   Night: %d",
             sky.x, sky.y, sky.z, m_dayNight.sunHeight(), (int)m_dayNight.isNight());
    b.str(tx,ty,SC,buf,.55f,.78f,.55f,1.f); ty+=LH;

    // Frustum + scene
    snprintf(buf, sizeof(buf), "Scene fade: %.2f   Push charge: %.2f   LookPC: %d",
             m_sceneFade, m_pushCharge, (int)m_lookingPC);
    b.str(tx,ty,SC,buf,.60f,.60f,.70f,1.f); ty+=LH;

    // Decorations instance counts
    snprintf(buf, sizeof(buf), "World scale: %.0fx%.0f m   VOID_Y: %.0f m",
             GROUND_HALF*2.f, GROUND_HALF*2.f, VOID_Y);
    b.str(tx,ty,SC,buf,.50f,.65f,.85f,1.f);
}


// ─────────────────────────────────────────────────────────────────────────
//  SHIFT+F3 — WORLD DEBUG OVERLAY
// ─────────────────────────────────────────────────────────────────────────
void Game::drawDebugWorld(UIBatch& b) {
    const float SC = 1.f, LH = 12.5f, PAD = 6.f;
    float PX = (float)W() - 410.f;
    float PY  = m_showDebugRender ? 240.f : 8.f; // stack below render panel if both open
    float PW  = 400.f, PH = 280.f;
    b.rect(PX, PY, PW, PH, .02f,.10f,.04f,.93f);
    b.rect(PX, PY, PW, 2.f, .30f,.85f,.30f,.90f); // green header — world = nature
    float tx = PX + PAD, ty = PY + 5.f;
    char buf[200];
    b.str(tx, ty, SC, "[ WORLD DEBUG Shift+F3 ]", .30f,.85f,.30f,1.f); ty += LH;
    b.rect(tx, ty, PW - PAD*2.f, 1.f, .08f,.25f,.10f,1.f); ty += LH;

    // World size
    snprintf(buf,sizeof(buf),"World: %.0fx%.0fm   VOID_Y: %.0fm",
             GROUND_HALF*2.f, GROUND_HALF*2.f, VOID_Y);
    b.str(tx,ty,SC,buf,.30f,.85f,.30f,1.f); ty+=LH;

    // Player
    snprintf(buf,sizeof(buf),"Player: %+.1f %+.1f %+.1f   yaw:%.1f pitch:%.1f",
             m_player.pos.x,m_player.pos.y,m_player.pos.z,m_player.yaw,m_player.pitch);
    b.str(tx,ty,SC,buf,.70f,.85f,.70f,1.f); ty+=LH;

    snprintf(buf,sizeof(buf),"Vel: %+.2f %+.2f %+.2f   Gnd:%d  Sprint:%d  Energy:%.0f",
             m_player.vel.x,m_player.vel.y,m_player.vel.z,
             (int)m_player.onGround,(int)m_player.sprinting,m_player.energy);
    b.str(tx,ty,SC,buf,.60f,.75f,.60f,1.f); ty+=LH;

    // Cube
    snprintf(buf,sizeof(buf),"Cube: %+.1f %+.1f %+.1f   Grabbed:%d  pushCD:%.2f",
             m_cube.pos.x,m_cube.pos.y,m_cube.pos.z,(int)m_cube.grabbed,m_cube.pushCD);
    b.str(tx,ty,SC,buf,1.f,.65f,.28f,1.f); ty+=LH;

    snprintf(buf,sizeof(buf),"Cube vel: %+.2f %+.2f %+.2f   angVel mag:%.2f",
             m_cube.vel.x,m_cube.vel.y,m_cube.vel.z,glm::length(m_cube.angVel));
    b.str(tx,ty,SC,buf,.90f,.55f,.20f,1.f); ty+=LH;

    // Time + sky
    snprintf(buf,sizeof(buf),"Time: %s %s   Night:%d   Sun elev:%.3f",
             m_dayNight.timeStr().c_str(),
             DayNight::periodName(m_dayNight.timeOfDay()),
             (int)m_dayNight.isNight(), m_dayNight.sunHeight());
    b.str(tx,ty,SC,buf,.50f,.70f,.95f,1.f); ty+=LH;

    // Cursed objects
    snprintf(buf,sizeof(buf),"Cursed found: %d/5  DiamondUnlocked:%s  Diamonds:%d",
             m_save.cursedFound,
             diamondsUnlocked()?"YES":"no",
             diamonds());
    b.str(tx,ty,SC,buf,.80f,.40f,1.f,1.f); ty+=LH;

    // Active crates
    int activeCrates = (int)std::count_if(m_crates.crates().begin(),m_crates.crates().end(),
                                          [](const WorldCrate& cr){return cr.active;});
    snprintf(buf,sizeof(buf),"Crates: %d active   Near:%d   NearCursed:%d   NearShop:%d",
             activeCrates,(int)m_lookingCrate,
             (int)m_cursedObjs.playerNearAny(),(int)m_shop.playerNear());
    b.str(tx,ty,SC,buf,.40f,.75f,.90f,1.f); ty+=LH;

    // NPC
    snprintf(buf,sizeof(buf),"NPC: %s  Away:%d  Dist:%.0fm  Visits:%d",
             m_shop.npcName().c_str(),(int)m_shop.isAway(),
             glm::distance(m_player.pos,m_shop.pos()),
             m_shop.visitCount());
    b.str(tx,ty,SC,buf,.65f,.80f,.55f,1.f); ty+=LH;

    // Effects active
    if (m_effects.speedBoost || m_effects.freezeTime || m_effects.earthquake ||
        m_effects.blackHole  || m_effects.gravityFlipped || m_effects.diamondPulse) {
        std::string fx = "Active FX:";
        if (m_effects.speedBoost)    fx += " SPEED";
        if (m_effects.freezeTime)    fx += " FREEZE";
        if (m_effects.earthquake)    fx += " QUAKE";
        if (m_effects.blackHole)     fx += " HOLE";
        if (m_effects.gravityFlipped)fx += " GRAV";
        if (m_effects.diamondPulse)  fx += " DIAMOND";
        b.str(tx,ty,SC,fx.c_str(),.95f,.75f,.20f,1.f); ty+=LH;
    }

    // INYF easter egg
    if (m_notFault.active) {
        snprintf(buf,sizeof(buf),"IT'S NOT YOUR FAULT  t=%.1fs  ending:%d",
                 m_notFault.timer,(int)m_notFault.ending);
        b.str(tx,ty,SC,buf,1.f,.15f,.15f,1.f);
    }
}
