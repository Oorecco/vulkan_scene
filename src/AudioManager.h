#pragma once
// AudioManager.h — Thin FMOD wrapper. BGM fades, SFX one-shots, non-fatal init.
// If FMOD isn't found, everything silently does nothing. Audio is a luxury.

#include "Common.h"
#include <string>

#ifdef HAVE_FMOD
#include <fmod.hpp>
#endif

enum class BGMTrack { None, Title, Gameplay };

class AudioManager {
public:
    AudioManager() = default;
    ~AudioManager();

    // Non-fatal — logs warning if FMOD unavailable
    void init(const std::string& assetRoot);
    void destroy();

    // Called every frame — drives FMOD update + fade logic
    void update(float dt);

    // BGM control (cross-fades between tracks)
    void playBGM(BGMTrack track, float fadeTime = 1.5f);
    void stopBGM(float fadeTime = 0.8f);
    void setBGMVolume(float vol); // 0.0–1.0

    // One-shot SFX
    void playSFX(const std::string& name); // name without extension, looks in sounds/sfx/

    bool isInitialised() const { return m_initialised; }

private:
    bool        m_initialised = false;
    std::string m_assetRoot;

#ifdef HAVE_FMOD
    FMOD::System*  m_system    = nullptr;

    // BGM channels + sounds
    FMOD::Sound*   m_sndTitle    = nullptr;
    FMOD::Sound*   m_sndGame     = nullptr;
    FMOD::Sound*   m_sndPush       = nullptr;
    FMOD::Sound*   m_sndBuy        = nullptr;  // cash register — ka-ching
    FMOD::Sound*   m_sndCursed     = nullptr;  // amogus-adjacent cursed object SFX
    FMOD::Channel* m_bgmChannel  = nullptr;
    FMOD::Channel* m_fadingOut   = nullptr; // previous track fading

    BGMTrack m_currentTrack  = BGMTrack::None;
    float    m_bgmVol        = 0.6f;
    float    m_fadeInTimer   = 0.f;
    float    m_fadeInTime    = 1.5f;
    float    m_fadeOutTimer  = 0.f;
    float    m_fadeOutTime   = 0.8f;
    bool     m_fading        = false;
#endif
};
