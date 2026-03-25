#pragma once
// DayNight.h — 1 real minute = 1 game hour.
// 24 real minutes for a full cycle. If you've played that long, go outside.

#include "../Common.h"
#include <string>

enum class TimeOfDay { Dawn, Morning, Afternoon, Evening, Night };

class DayNight {
public:
    DayNight() : m_gameSeconds(8.0f * 3600.0f) {} // start at 08:00

    void update(float dt) {
        m_gameSeconds += dt * 60.0f; // 1 real second = 1 game minute
        if (m_gameSeconds >= 86400.0f)
            m_gameSeconds -= 86400.0f;
    }

    // Set time directly — used by dev console "time_set HH:MM"
    void setTime(int h, int m) {
        m_gameSeconds = std::clamp(h, 0, 23) * 3600.0f
                      + std::clamp(m, 0, 59) * 60.0f;
    }

    float hour()  const { return m_gameSeconds / 3600.0f; }
    int   iHour() const { return (int)hour(); }
    int   iMin()  const { return (int)((m_gameSeconds - iHour() * 3600.0f) / 60.0f); }

    TimeOfDay timeOfDay() const {
        float h = hour();
        if (h >= 5.0f && h < 7.0f)  return TimeOfDay::Dawn;
        if (h >= 7.0f && h < 12.0f) return TimeOfDay::Morning;
        if (h >= 12.0f&& h < 17.0f) return TimeOfDay::Afternoon;
        if (h >= 17.0f&& h < 20.0f) return TimeOfDay::Evening;
        return TimeOfDay::Night;
    }

    bool isNight() const {
        float h = hour();
        return h >= 20.0f || h < 5.0f;
    }

    // 0.0 = midnight/sunset, 1.0 = noon
    float sunHeight() const {
        float t = (hour() - 6.0f) / 12.0f; // 0 at 6am, 1 at 6pm
        if (t < 0.0f || t > 1.0f) return -0.2f;
        return sinf(t * 3.14159f);
    }

    glm::vec3 skyColor() const {
        float h = hour();
        static const glm::vec3 NIGHT   = {0.02f,0.02f,0.08f};
        static const glm::vec3 DAWN    = {0.85f,0.42f,0.18f};
        static const glm::vec3 MORNING = {0.50f,0.65f,0.90f};
        static const glm::vec3 NOON    = {0.45f,0.62f,0.92f};
        static const glm::vec3 DUSK    = {0.78f,0.36f,0.15f};
        auto lrp=[](glm::vec3 a,glm::vec3 b,float t){
            return a+(b-a)*std::clamp(t,0.f,1.f);
        };
        if (h < 5.0f)  return lrp(NIGHT,  DAWN,    h/5.0f);
        if (h < 7.0f)  return lrp(DAWN,   MORNING, (h-5.0f)/2.0f);
        if (h < 12.0f) return lrp(MORNING,NOON,    (h-7.0f)/5.0f);
        if (h < 17.0f) return lrp(NOON,   MORNING, (h-12.0f)/5.0f);
        if (h < 20.0f) return lrp(MORNING,DUSK,    (h-17.0f)/3.0f);
        return lrp(DUSK, NIGHT, (h-20.0f)/4.0f);
    }

    float ambientIntensity() const {
        return std::max(0.06f, sunHeight() * 0.42f + 0.08f);
    }

    glm::vec3 sunDir() const {
        float ang = ((hour() - 6.0f) / 12.0f) * 3.14159f;
        return glm::normalize(glm::vec3(
            cosf(ang) * 0.6f,
            std::max(0.05f, sinf(ang)),
            0.4f));
    }

    std::string timeStr() const {
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:%02d", iHour(), iMin());
        return buf;
    }

    static const char* periodName(TimeOfDay tod) {
        switch (tod) {
        case TimeOfDay::Dawn:      return "Dawn";
        case TimeOfDay::Morning:   return "Morning";
        case TimeOfDay::Afternoon: return "Afternoon";
        case TimeOfDay::Evening:   return "Evening";
        case TimeOfDay::Night:     return "Night";
        }
        return "???";
    }

private:
    float m_gameSeconds;
};
