// round-box-clock: see clock_overlay.h.
#include "clock_overlay.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <esp_timer.h>
#include <lvgl.h>
#include <sys/stat.h>

#include <cstdio>

#define TAG "ClockOverlay"

namespace clock_overlay {
namespace {

constexpr size_t kBytes = kSize * kSize * 2;

lv_image_dsc_t dsc_ = {};
uint8_t* pixels_ = nullptr;   // owned by the visible overlay
lv_obj_t* image_ = nullptr;
esp_timer_handle_t timer_ = nullptr;

// Caller holds the LVGL lock. Returns the pixel buffer to free after unlocking.
uint8_t* RemoveLocked() {
    if (image_) {
        lv_obj_delete(image_);
        image_ = nullptr;
    }
    lv_image_cache_drop(&dsc_);
    uint8_t* old = pixels_;
    pixels_ = nullptr;
    return old;
}

void HideTimerCb(void*) {
    if (!lvgl_port_lock(2000)) {
        ESP_LOGW(TAG, "hide: display lock timeout");
        return;
    }
    uint8_t* old = RemoveLocked();
    lvgl_port_unlock();
    heap_caps_free(old);
}

}  // namespace

std::string ShowRgb565File(const std::string& path, int seconds) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return "file not found";
    if (static_cast<size_t>(st.st_size) != kBytes) return "expected a 240x240 RGB565 file (115200 bytes)";

    uint8_t* buf = static_cast<uint8_t*>(heap_caps_malloc(kBytes, MALLOC_CAP_SPIRAM));
    if (!buf) return "out of PSRAM";
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

    if (!timer_) {
        esp_timer_create_args_t args = {};
        args.callback = HideTimerCb;
        args.dispatch_method = ESP_TIMER_TASK;
        args.name = "overlay_hide";
        esp_timer_create(&args, &timer_);
    }
    esp_timer_stop(timer_);

    if (!lvgl_port_lock(2000)) {
        heap_caps_free(buf);
        return "display busy";
    }
    uint8_t* old = RemoveLocked();
    pixels_ = buf;
    dsc_ = {};
    dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
    dsc_.header.w = kSize;
    dsc_.header.h = kSize;
    dsc_.header.stride = kSize * 2;
    dsc_.data_size = kBytes;
    dsc_.data = pixels_;
    image_ = lv_image_create(lv_layer_top());
    lv_image_set_src(image_, &dsc_);
    lv_obj_center(image_);
    lvgl_port_unlock();
    heap_caps_free(old);

    if (seconds < 1) seconds = 1;
    if (seconds > 600) seconds = 600;
    esp_timer_start_once(timer_, static_cast<uint64_t>(seconds) * 1000000ULL);
    ESP_LOGI(TAG, "showing %s for %d s", path.c_str(), seconds);
    return "";
}

}  // namespace clock_overlay
