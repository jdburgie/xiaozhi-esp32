// round-box-clock: full-screen picture slideshow from /sdcard/images/*.rgb565.
// Started/stopped by a screen tap while idle (or from the web page). Pauses (hides) while a
// conversation runs and resumes when the device is idle again. The interval and shuffle come
// from ClockSettings and are re-read for every picture.
// All functions only post to the slideshow task, so they are safe from any task or timer
// callback, including while the display lock is held.
#ifndef CLOCK_SLIDESHOW_H
#define CLOCK_SLIDESHOW_H

#include <string>

namespace clock_slideshow {

constexpr const char* kImageDir = "/sdcard/images";

void Init();  // creates the task; call once at boot
void Start();
void Stop();
void Toggle();
void Next();                   // skip to the next picture now (no-op when stopped)
void OnDeviceStateChanged();   // call on every device state change
bool IsActive();               // running (possibly paused for a conversation)
std::string Current();         // file name on screen, or "" (racy snapshot, for status display)

}  // namespace clock_slideshow

#endif  // CLOCK_SLIDESHOW_H
