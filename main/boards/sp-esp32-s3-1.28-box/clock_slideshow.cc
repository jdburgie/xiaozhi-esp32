// round-box-clock: see clock_slideshow.h.
#include "clock_slideshow.h"

#include "application.h"
#include "clock_overlay.h"
#include "clock_sd.h"
#include "clock_settings.h"

#include <dirent.h>
#include <esp_log.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <random>
#include <shared_mutex>
#include <vector>

#define TAG "Slideshow"

namespace clock_slideshow {
namespace {

enum : uint32_t {
    kCmdStart = 1u << 0,
    kCmdStop = 1u << 1,
    kCmdToggle = 1u << 2,
    kCmdState = 1u << 3,
    kCmdNext = 1u << 4,
};
constexpr size_t kMaxPictures = 1000;

TaskHandle_t task_ = nullptr;
std::atomic<bool> active_{false};

// Task-owned state.
std::vector<std::string> list_;
size_t index_ = 0;
bool paused_ = false;

std::mutex current_mutex_;
std::string current_;

void SetCurrent(const std::string& name) {
    std::lock_guard<std::mutex> lock(current_mutex_);
    current_ = name;
}

bool DeviceIdle() {
    return Application::GetInstance().GetDeviceState() == kDeviceStateIdle;
}

void LoadList() {
    list_.clear();
    if (clock_sd::IsMounted()) {
        std::shared_lock<std::shared_mutex> files(clock_sd::FileMutex());
        if (DIR* dir = opendir(kImageDir)) {
            while (dirent* e = readdir(dir)) {
                if (e->d_type == DT_DIR || e->d_name[0] == '.') continue;
                std::string n = e->d_name;
                if (n.size() > 7 && n.compare(n.size() - 7, 7, ".rgb565") == 0) list_.push_back(n);
                if (list_.size() >= kMaxPictures) break;
            }
            closedir(dir);
        }
    }
    if (ClockSettings::Get().Load().slide_shuffle) {
        std::minstd_rand rng(esp_random());
        std::shuffle(list_.begin(), list_.end(), rng);
    } else {
        std::sort(list_.begin(), list_.end());
    }
    index_ = 0;
    ESP_LOGI(TAG, "%u pictures", static_cast<unsigned>(list_.size()));
}

// Shows the picture at index_, skipping unreadable ones and re-reading the folder at the end
// of each pass (so uploads and deletions are picked up). False if there is nothing to show.
bool ShowCurrent() {
    for (int attempts = 0; attempts < 2 * static_cast<int>(kMaxPictures); ++attempts) {
        if (index_ >= list_.size()) {
            LoadList();
            if (list_.empty()) return false;
        }
        const std::string& name = list_[index_];
        const std::string err = clock_overlay::ShowRgb565File(std::string(kImageDir) + "/" + name, 0);
        if (err.empty()) {
            SetCurrent(name);
            return true;
        }
        ESP_LOGW(TAG, "skip %s: %s", name.c_str(), err.c_str());
        list_.erase(list_.begin() + index_);
    }
    return false;
}

void DoStart() {
    active_ = true;
    paused_ = false;
    LoadList();
    if (list_.empty()) {
        active_ = false;
        SetCurrent("");
        ESP_LOGI(TAG, "no pictures in %s", kImageDir);
        clock_overlay::ShowMessage(clock_sd::IsMounted() ? "No pictures" : "No SD card", 3);
        return;
    }
    if (!DeviceIdle()) {
        paused_ = true;  // shows when the conversation ends
        return;
    }
    if (!ShowCurrent()) {
        active_ = false;
        clock_overlay::ShowMessage("No pictures", 3);
    }
}

void DoStop() {
    active_ = false;
    paused_ = false;
    SetCurrent("");
    clock_overlay::Hide();
    ESP_LOGI(TAG, "stopped");
}

void DoAdvance() {
    ++index_;
    if (!ShowCurrent()) DoStop();
}

void Task(void*) {
    for (;;) {
        TickType_t wait = portMAX_DELAY;
        if (active_ && !paused_) {
            wait = pdMS_TO_TICKS(ClockSettings::Get().Load().slide_secs * 1000);
        }
        uint32_t bits = 0;
        const bool notified = xTaskNotifyWait(0, UINT32_MAX, &bits, wait) == pdTRUE;

        if (bits & kCmdToggle) bits |= active_ ? kCmdStop : kCmdStart;
        if (bits & kCmdStop) {
            if (active_) DoStop();
            continue;
        }
        if (bits & kCmdStart) {
            DoStart();
            continue;
        }
        if (!active_) continue;

        const bool idle = DeviceIdle();
        if (!idle) {
            if (!paused_) {
                paused_ = true;
                clock_overlay::Hide();  // get out of the conversation UI's way
            }
            continue;
        }
        if (paused_) {
            paused_ = false;
            if (!ShowCurrent()) DoStop();
            continue;
        }
        if (!notified || (bits & kCmdNext)) DoAdvance();  // interval elapsed, or skip requested
    }
}

void Post(uint32_t bits) {
    if (task_) xTaskNotify(task_, bits, eSetBits);
}

}  // namespace

void Init() {
    if (task_) return;
    // SD reads + small heap work; picture buffers live in PSRAM.
    xTaskCreate(Task, "slideshow", 4096, nullptr, 2, &task_);
}

void Start() { Post(kCmdStart); }
void Stop() { Post(kCmdStop); }
void Toggle() { Post(kCmdToggle); }
void Next() { Post(kCmdNext); }
void OnDeviceStateChanged() { Post(kCmdState); }
bool IsActive() { return active_.load(); }

std::string Current() {
    std::lock_guard<std::mutex> lock(current_mutex_);
    return current_;
}

}  // namespace clock_slideshow
