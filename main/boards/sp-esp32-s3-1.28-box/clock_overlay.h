// round-box-clock: temporary full-screen image overlay (above clock face and XiaoZhi UI).
// Thread-safe: takes the LVGL port lock itself; callable from the web server task.
#ifndef CLOCK_OVERLAY_H
#define CLOCK_OVERLAY_H

#include <string>

namespace clock_overlay {

constexpr int kSize = 240;  // images are kSize x kSize RGB565 little-endian (LVGL native), 115200 bytes

// Loads `path` (raw RGB565, exactly kSize*kSize*2 bytes) and shows it for `seconds`.
// Returns "" on success, otherwise a short error message.
std::string ShowRgb565File(const std::string& path, int seconds);

}  // namespace clock_overlay

#endif  // CLOCK_OVERLAY_H
