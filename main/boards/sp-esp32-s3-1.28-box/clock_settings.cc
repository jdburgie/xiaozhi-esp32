// round-box-clock: see clock_settings.h.
#include "clock_settings.h"

#include "settings.h"

ClockSettings& ClockSettings::Get() {
    static ClockSettings instance;
    return instance;
}

ClockSettings::ClockSettings() {
    Settings s("clock");
    Values d;
    values_.face = static_cast<uint32_t>(s.GetInt("face", static_cast<int32_t>(d.face)));
    values_.ink = static_cast<uint32_t>(s.GetInt("ink", static_cast<int32_t>(d.ink)));
    values_.day_start = s.GetInt("day_start", d.day_start);
    values_.night_start = s.GetInt("night_start", d.night_start);
    values_.show_seconds = s.GetBool("show_secs", d.show_seconds);
    values_.slide_secs = s.GetInt("slide_secs", d.slide_secs);
    if (values_.slide_secs < kMinSlideSecs || values_.slide_secs > kMaxSlideSecs) values_.slide_secs = d.slide_secs;
    values_.slide_shuffle = s.GetBool("slide_shuf", d.slide_shuffle);
}

ClockSettings::Values ClockSettings::Load() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return values_;
}

bool ClockSettings::Save(const Values& v) {
    if (v.face > 0xFFFFFF || v.ink > 0xFFFFFF) return false;
    if (v.day_start < 0 || v.day_start > 23 || v.night_start < 0 || v.night_start > 23) return false;
    if (v.day_start == v.night_start) return false;
    if (v.slide_secs < kMinSlideSecs || v.slide_secs > kMaxSlideSecs) return false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        values_ = v;
    }
    Settings s("clock", true);
    s.SetInt("face", static_cast<int32_t>(v.face));
    s.SetInt("ink", static_cast<int32_t>(v.ink));
    s.SetInt("day_start", v.day_start);
    s.SetInt("night_start", v.night_start);
    s.SetBool("show_secs", v.show_seconds);
    s.SetInt("slide_secs", v.slide_secs);
    s.SetBool("slide_shuf", v.slide_shuffle);
    version_.fetch_add(1);
    return true;
}
