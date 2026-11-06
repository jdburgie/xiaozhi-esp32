// round-box-clock: see clock_overlay.h.
#include "clock_overlay.h"

#include "clock_sd.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <esp_timer.h>
#include <lvgl.h>
#include <sys/stat.h>

#include <atomic>
#include <cstdio>
#include <shared_mutex>

#define TAG "ClockOverlay"

namespace clock_overlay {
namespace {

lv_image_dsc_t dsc_ = {};
uint8_t* pixels_ = nullptr;   // owned by the visible overlay
lv_obj_t* root_ = nullptr;    // full-screen black container on the top layer
esp_timer_handle_t timer_ = nullptr;
std::atomic<bool> showing_{false};

// Caller holds the LVGL lock. Returns the pixel buffer to free after unlocking.
uint8_t* RemoveLocked() {
    if (root_) {
        lv_obj_delete(root_);  // deletes the image/label child too
        root_ = nullptr;
    }
    if (dsc_.data) lv_image_cache_drop(&dsc_);
    dsc_ = {};
    uint8_t* old = pixels_;
    pixels_ = nullptr;
    showing_ = false;
    return old;
}

// Caller holds the LVGL lock.
lv_obj_t* CreateRootLocked() {
    lv_obj_t* root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, kSize, kSize);
    lv_obj_center(root);
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    return root;
}

void HideTimerCb(void*) { Hide(); }

void ArmTimer(int seconds) {
    if (!timer_) {
        esp_timer_create_args_t args = {};
        args.callback = HideTimerCb;
        args.dispatch_method = ESP_TIMER_TASK;
        args.name = "overlay_hide";
        esp_timer_create(&args, &timer_);
    }
    esp_timer_stop(timer_);
    if (seconds > 0) {
        if (seconds > 600) seconds = 600;
        esp_timer_start_once(timer_, static_cast<uint64_t>(seconds) * 1000000ULL);
    }
}

}  // namespace

std::string ShowRgb565File(const std::string& path, int seconds) {
    uint8_t* buf = static_cast<uint8_t*>(heap_caps_malloc(kBytes, MALLOC_CAP_SPIRAM));
    if (!buf) return "out of PSRAM";
    {
        // Shared lock: a delete or rename can't run while the file is open.
        std::shared_lock<std::shared_mutex> files(clock_sd::FileMutex());
        struct stat st;
        if (stat(path.c_str(), &st) != 0) {
            heap_caps_free(buf);
            return "file not found";
        }
        if (static_cast<size_t>(st.st_size) != kBytes) {
            heap_caps_free(buf);
            return "expected a 240x240 RGB565 file (115200 bytes)";
        }
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) {
            heap_caps_free(buf);
            return "open failed";
        }
        const size_t n = fread(buf, 1, kBytes, f);
        fclose(f);
        if (n != kBytes) {
            heap_caps_free(buf);
            return "short read";
        }
    }

    ArmTimer(0);  // cancel any pending hide; re-armed below if this one is timed
    if (!lvgl_port_lock(2000)) {
        heap_caps_free(buf);
        return "display busy";
    }
    uint8_t* old = RemoveLocked();
    pixels_ = buf;
    dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
    dsc_.header.w = kSize;
    dsc_.header.h = kSize;
    dsc_.header.stride = kSize * 2;
    dsc_.data_size = kBytes;
    dsc_.data = pixels_;
    root_ = CreateRootLocked();
    lv_obj_t* image = lv_image_create(root_);
    lv_image_set_src(image, &dsc_);
    lv_obj_center(image);
    showing_ = true;
    lvgl_port_unlock();
    heap_caps_free(old);

    ArmTimer(seconds);
    ESP_LOGI(TAG, "showing %s%s", path.c_str(), seconds > 0 ? " (timed)" : "");
    return "";
}

void ShowMessage(const char* text, int seconds) {
    ArmTimer(0);
    if (!lvgl_port_lock(2000)) return;
    uint8_t* old = RemoveLocked();
    root_ = CreateRootLocked();
    lv_obj_t* label = lv_label_create(root_);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
    showing_ = true;
    lvgl_port_unlock();
    heap_caps_free(old);
    ArmTimer(seconds < 1 ? 1 : seconds);
}

void Hide() {
    if (timer_) esp_timer_stop(timer_);
    if (!lvgl_port_lock(2000)) {
        ESP_LOGW(TAG, "hide: display lock timeout");
        return;
    }
    uint8_t* old = RemoveLocked();
    lvgl_port_unlock();
    heap_caps_free(old);
}

bool IsShowing() { return showing_.load(); }

}  // namespace clock_overlay
