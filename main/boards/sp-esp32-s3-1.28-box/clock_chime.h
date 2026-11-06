// round-box-clock: ship's bell chime. Strikes 1-8 bells on every half hour through each
// four-hour watch (00:30 = 1 bell ... 04:00 = 8 bells), in pairs, only while XiaoZhi is idle
// and outside quiet hours (ClockSettings). Sounds are synthesized by
// tools/make_ships_bells.py and embedded from boards/sp-esp32-s3-1.28-box/sounds/.
#ifndef CLOCK_CHIME_H
#define CLOCK_CHIME_H

#include <string>

namespace clock_chime {

// Starts the chime task. Call once at boot.
void Init();

// Rings now from the chime task: 1-8 bells, or 0 for the count due this half hour.
// Ignores quiet hours and the enable switch (it's a test); still waits for idle audio.
void RingNow(int bells);

// Plays an arbitrary OGG/Opus clip (16 kHz mono, 60 ms frames) once, from the chime task,
// when audio is idle. For auditioning sounds without a rebuild. False if one is already queued.
bool PlayPreview(std::string ogg);

// Bells struck at hour:minute on the half hour (1-8).
int BellsFor(int hour, int minute);

}  // namespace clock_chime

#endif  // CLOCK_CHIME_H
