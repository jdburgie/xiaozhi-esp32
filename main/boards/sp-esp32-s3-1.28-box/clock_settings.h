// round-box-clock: user settings for the clock face, persisted in NVS namespace "clock".
// Thread-safe: written by the web server task, read by the display task once a second.
#ifndef CLOCK_SETTINGS_H
#define CLOCK_SETTINGS_H

#include <atomic>
#include <cstdint>
#include <mutex>

class ClockSettings {
public:
    struct Values {
        uint32_t face = 0xCBB07E;  // day face; also the night ink (night is the inverse of day)
        uint32_t ink = 0x1C1A18;   // day hands/numerals
        int day_start = 7;         // hour [0,23] when the day palette begins
        int night_start = 19;      // hour [0,23] when the night palette begins
        bool show_seconds = true;
    };

    static ClockSettings& Get();

    Values Load() const;
    // Validates, persists, and bumps the version. Returns false (nothing saved) if invalid.
    bool Save(const Values& v);
    // Changes whenever Save() succeeds; readers compare it to skip redundant work.
    uint32_t version() const { return version_.load(); }

private:
    ClockSettings();
    mutable std::mutex mutex_;
    Values values_;
    std::atomic<uint32_t> version_{1};
};

#endif  // CLOCK_SETTINGS_H
