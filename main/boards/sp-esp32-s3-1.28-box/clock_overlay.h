// round-box-clock: full-screen image overlay (above clock face and XiaoZhi UI).
// Thread-safe: takes the LVGL port lock itself; callable from the web server or slideshow task.
// Do not call while holding the display lock.
#ifndef CLOCK_OVERLAY_H
#define CLOCK_OVERLAY_H

#include <string>

namespace clock_overlay {

constexpr int kSize = 240;  // images are kSize x kSize RGB565 little-endian (LVGL native), 115200 bytes
constexpr size_t kBytes = kSize * kSize * 2;

// Loads `path` (raw RGB565, exactly kBytes) and shows it. `seconds` > 0 hides it after that long;
// `seconds` <= 0 keeps it up until Hide() or the next Show. Returns "" on success, else a short error.
std::string ShowRgb565File(const std::string& path, int seconds);

// Short centred text on black (e.g. "No pictures"), hidden after `seconds`.
void ShowMessage(const char* text, int seconds);

void Hide();
bool IsShowing();  // lock-free

}  // namespace clock_overlay

#endif  // CLOCK_OVERLAY_H
