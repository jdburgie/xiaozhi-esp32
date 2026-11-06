// round-box-clock: see clock_chime.h.
#include "clock_chime.h"

#include "application.h"
#include "clock_settings.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <ctime>
#include <mutex>
#include <string>
#include <string_view>

#define TAG "ClockChime"

#define BELL_SOUND(n)                                                   \
    extern const char bells_##n##_start[] asm("_binary_bells_" #n "_ogg_start"); \
    extern const char bells_##n##_end[] asm("_binary_bells_" #n "_ogg_end");
BELL_SOUND(1)
BELL_SOUND(2)
BELL_SOUND(3)
BELL_SOUND(4)
BELL_SOUND(5)
BELL_SOUND(6)
BELL_SOUND(7)
BELL_SOUND(8)

namespace clock_chime {
namespace {

std::string_view Sound(int bells) {
    switch (bells) {
        case 1: return {bells_1_start, static_cast<size_t>(bells_1_end - bells_1_start)};
        case 2: return {bells_2_start, static_cast<size_t>(bells_2_end - bells_2_start)};
        case 3: return {bells_3_start, static_cast<size_t>(bells_3_end - bells_3_start)};
        case 4: return {bells_4_start, static_cast<size_t>(bells_4_end - bells_4_start)};
        case 5: return {bells_5_start, static_cast<size_t>(bells_5_end - bells_5_start)};
        case 6: return {bells_6_start, static_cast<size_t>(bells_6_end - bells_6_start)};
        case 7: return {bells_7_start, static_cast<size_t>(bells_7_end - bells_7_start)};
        default: return {bells_8_start, static_cast<size_t>(bells_8_end - bells_8_start)};
    }
}

std::atomic<int> pending_ring_{-1};  // -1 none; 0 = current count; 1-8 explicit
std::mutex preview_mutex_;
std::string preview_;                 // guarded by preview_mutex_
std::atomic<bool> preview_pending_{false};

bool IsQuiet(const ClockSettings::Values& v, int hour) {
    if (v.quiet_from == v.quiet_to) return false;
    if (v.quiet_from < v.quiet_to) return hour >= v.quiet_from && hour < v.quiet_to;
    return hour >= v.quiet_from || hour < v.quiet_to;  // window wraps past midnight
}

bool CanRing() {
    auto& app = Application::GetInstance();
    return app.GetDeviceState() == kDeviceStateIdle && app.GetAudioService().IsIdle();
}

void Ring(int bells) {
    ESP_LOGI(TAG, "%d bell%s", bells, bells == 1 ? "" : "s");
    Application::GetInstance().PlaySound(Sound(bells));  // blocks while the decoder queue is full
}

void ChimeTask(void*) {
    int last_slot = -1;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(500));

        time_t now = time(nullptr);
        struct tm t;
        localtime_r(&now, &t);
        const bool time_set = t.tm_year >= 2025 - 1900;

        if (preview_pending_.load()) {
            if (!CanRing()) continue;
            std::string clip;
            {
                std::lock_guard<std::mutex> lock(preview_mutex_);
                clip.swap(preview_);
            }
            ESP_LOGI(TAG, "preview clip (%u bytes)", static_cast<unsigned>(clip.size()));
            Application::GetInstance().PlaySound(clip);
            preview_pending_.store(false);
            continue;
        }

        const int test = pending_ring_.load();
        if (test >= 0) {
            if (!CanRing()) continue;  // wait for idle audio
            pending_ring_.store(-1);
            Ring(test > 0 ? test : (time_set ? BellsFor(t.tm_hour, t.tm_min - t.tm_min % 30) : 8));
            continue;
        }

        if (!time_set || t.tm_min % 30 != 0 || t.tm_sec >= 30) continue;  // 30 s window per half hour
        const int slot = t.tm_yday * 48 + (t.tm_hour * 60 + t.tm_min) / 30;
        if (slot == last_slot) continue;

        const ClockSettings::Values v = ClockSettings::Get().Load();
        if (!v.chime_enabled || IsQuiet(v, t.tm_hour)) {
            last_slot = slot;
            continue;
        }
        if (!CanRing()) continue;  // a conversation or other sound: retry within the window, else skip
        last_slot = slot;
        Ring(BellsFor(t.tm_hour, t.tm_min));
    }
}

}  // namespace

int BellsFor(int hour, int minute) {
    const int n = ((hour * 60 + minute) / 30) % 8;
    return n == 0 ? 8 : n;
}

void Init() {
    static bool started = false;
    if (started) return;
    started = true;
    xTaskCreate(ChimeTask, "clock_chime", 4096, nullptr, 2, nullptr);
}

bool PlayPreview(std::string ogg) {
    if (preview_pending_.load()) return false;
    {
        std::lock_guard<std::mutex> lock(preview_mutex_);
        preview_ = std::move(ogg);
    }
    preview_pending_.store(true);
    return true;
}

void RingNow(int bells) {
    if (bells < 0 || bells > 8) return;
    pending_ring_.store(bells);
}

}  // namespace clock_chime
