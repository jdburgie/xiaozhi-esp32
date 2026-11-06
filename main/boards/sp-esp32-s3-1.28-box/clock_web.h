// round-box-clock: LAN web settings page at http://roundclock.local/ (mDNS) on port 80.
// No authentication: settings only (colours, hours, brightness). Firmware upload is
// deliberately not exposed here until it has auth.
#ifndef CLOCK_WEB_H
#define CLOCK_WEB_H

namespace clock_web {

constexpr const char* kHostname = "roundclock";

// Spawns a task that waits for the Wi-Fi station to get an IP, then starts mDNS and
// the HTTP server once. Safe to call once from the board constructor.
void StartWhenNetworkReady();

}  // namespace clock_web

#endif  // CLOCK_WEB_H
