// round-box-clock: microSD card on the Spotpear 1.28" box.
// Pins per Spotpear's wiki: CLK 17, CMD 18, D0 21, D3 13 (D1/D2 not wired) -> 1-bit SDMMC.
#ifndef CLOCK_SD_H
#define CLOCK_SD_H

#include <string>

namespace clock_sd {

constexpr const char* kMountPoint = "/sdcard";

// Mounts the card (FAT) at kMountPoint. Never formats. Safe to call once at boot.
bool Mount();
bool IsMounted();
// JSON object describing the card (or the failure), for the status page.
std::string StatusJson();

}  // namespace clock_sd

#endif  // CLOCK_SD_H
