// AudioManager.cpp — Sound without the drama (unless FMOD says otherwise).
#include "AudioManager.h"
#include <algorithm>

AudioManager::~AudioManager() { destroy(); }

void AudioManager::init(const std::string& assetRoot) {
    m_assetRoot = assetRoot;
    if (m_assetRoot.back() != '/' && m_assetRoot.back() != '\\')
        m_assetRoot += '/';

#ifdef HAVE_FMOD
    FMOD_RESULT res = FMOD::System_Create(&m_system);
    if (res != FMOD_OK) {
        LOG_WARN("AudioManager: FMOD::System_Create failed — audio disabled");
        m_system = nullptr; return;
    }
    res = m_system->init(64, FMOD_INIT_NORMAL, nullptr);
    if (res != FMOD_OK) {
        LOG_WARN("AudioManager: FMOD init failed — audio disabled");
        m_system->release(); m_system = nullptr; return;
    }

    // Pre-load all known sounds as streams (BGM) or samples (SFX)
    auto load = [&](FMOD::Sound*& snd, const std::string& path, bool loop) {
        FMOD_MODE mode = FMOD_DEFAULT | (loop ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF);
        mode |= loop ? FMOD_CREATESTREAM : FMOD_CREATESAMPLE;
        FMOD_RESULT r = m_system->createSound(path.c_str(), mode, nullptr, &snd);
        if (r != FMOD_OK) {
            LOG_WARN("AudioManager: could not load '" + path + "'");
            snd = nullptr;
        }
    };

    load(m_sndTitle,  m_assetRoot + "sounds/music/title_screen.mp3",  true);
    load(m_sndGame,   m_assetRoot + "sounds/music/game_scene.mp3",    true);
    load(m_sndPush,   m_assetRoot + "sounds/sfx/push_sfx.mp3",        false);
    load(m_sndBuy,    m_assetRoot + "sounds/sfx/buy_sfx.mp3",         false);
    load(m_sndCursed, m_assetRoot + "sounds/sfx/cursedobj_sfx.mp3",   false);

    m_initialised = true;
    LOG_INFO("AudioManager: FMOD initialised OK");
#else
    LOG_INFO("AudioManager: built without FMOD — audio disabled");
#endif
}

void AudioManager::destroy() {
#ifdef HAVE_FMOD
    if (!m_system) return;
    if (m_sndTitle)  { m_sndTitle->release();  m_sndTitle  = nullptr; }
    if (m_sndGame)   { m_sndGame->release();   m_sndGame   = nullptr; }
    if (m_sndPush)   { m_sndPush->release();   m_sndPush   = nullptr; }
    if (m_sndBuy)    { m_sndBuy->release();    m_sndBuy    = nullptr; }
    if (m_sndCursed) { m_sndCursed->release(); m_sndCursed = nullptr; }
    m_system->close();
    m_system->release();
    m_system = nullptr;
    m_initialised = false;
#endif
}

void AudioManager::update(float dt) {
#ifdef HAVE_FMOD
    if (!m_system) return;

    // Fade in new track
    if (m_fading && m_bgmChannel) {
        m_fadeInTimer += dt;
        float t = std::min(m_fadeInTimer / m_fadeInTime, 1.0f);
        m_bgmChannel->setVolume(t * m_bgmVol);
        if (t >= 1.0f) m_fading = false;
    }

    // Fade out old track
    if (m_fadingOut) {
        m_fadeOutTimer += dt;
        float t = std::min(m_fadeOutTimer / m_fadeOutTime, 1.0f);
        m_fadingOut->setVolume((1.0f - t) * m_bgmVol);
        if (t >= 1.0f) {
            m_fadingOut->stop();
            m_fadingOut = nullptr;
        }
    }

    m_system->update();
#else
    (void)dt;
#endif
}

void AudioManager::playBGM(BGMTrack track, float fadeTime) {
#ifdef HAVE_FMOD
    if (!m_system || track == m_currentTrack) return;

    FMOD::Sound* nextSnd = nullptr;
    switch (track) {
    case BGMTrack::Title:    nextSnd = m_sndTitle; break;
    case BGMTrack::Gameplay: nextSnd = m_sndGame;  break;
    default: break;
    }
    if (!nextSnd) return;

    // Fade out current channel
    if (m_bgmChannel) {
        m_fadingOut    = m_bgmChannel;
        m_fadeOutTimer = 0.f;
        m_fadeOutTime  = fadeTime;
        m_bgmChannel   = nullptr;
    }

    // Start new track at volume 0 and fade in
    m_system->playSound(nextSnd, nullptr, false, &m_bgmChannel);
    if (m_bgmChannel) {
        m_bgmChannel->setVolume(0.0f);
        m_fading      = true;
        m_fadeInTimer = 0.f;
        m_fadeInTime  = fadeTime;
        m_currentTrack = track;
    }
#else
    (void)track; (void)fadeTime;
#endif
}

void AudioManager::stopBGM(float fadeTime) {
#ifdef HAVE_FMOD
    if (!m_system || !m_bgmChannel) return;
    m_fadingOut    = m_bgmChannel;
    m_fadeOutTimer = 0.f;
    m_fadeOutTime  = fadeTime;
    m_bgmChannel   = nullptr;
    m_currentTrack = BGMTrack::None;
#else
    (void)fadeTime;
#endif
}

void AudioManager::setBGMVolume(float vol) {
#ifdef HAVE_FMOD
    m_bgmVol = std::clamp(vol, 0.0f, 1.0f);
    if (m_bgmChannel) m_bgmChannel->setVolume(m_bgmVol);
#else
    (void)vol;
#endif
}

void AudioManager::playSFX(const std::string& name) {
#ifdef HAVE_FMOD
    if (!m_system) return;
    // Map known SFX names to pre-loaded sounds
    FMOD::Sound* snd = nullptr;
    if (name == "push" || name == "push_sfx") snd = m_sndPush;
    if (name == "buy"  || name == "buy_sfx")    snd = m_sndBuy;
    if (name == "cursed" || name == "cursedobj_sfx") snd = m_sndCursed;
    if (!snd) {
        // Try loading on demand (for future SFX)
        std::string path = m_assetRoot + "sounds/sfx/" + name + ".mp3";
        m_system->createSound(path.c_str(), FMOD_DEFAULT, nullptr, &snd);
        if (!snd) return;
        FMOD::Channel* ch = nullptr;
        m_system->playSound(snd, nullptr, false, &ch);
        return;
    }
    FMOD::Channel* ch = nullptr;
    m_system->playSound(snd, nullptr, false, &ch);
    if (ch) ch->setVolume(0.85f);
#else
    (void)name;
    MessageBeep(MB_ICONEXCLAMATION); // at least make some noise
#endif
}
