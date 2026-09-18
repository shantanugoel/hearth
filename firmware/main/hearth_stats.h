// What the sleep-first board loop has actually done since boot. The board loop
// is the only writer; the USB console reads it once per command. Kept in its own
// header so hearth_config.cc can print it without reaching into app_main.
#ifndef HEARTH_STATS_H_
#define HEARTH_STATS_H_

#include <cstdint>

struct HearthStats {
    uint32_t uptime_s = 0;
    uint32_t wakes = 0;           // loop iterations, i.e. sleep deadlines honoured
    uint32_t refreshes = 0;       // panel commands actually issued
    uint32_t paints_skipped = 0;  // frames already on the glass
    uint32_t poster_fetches = 0;
    uint32_t poster_repaints = 0;  // fetches that changed anything
    uint32_t poll_ms = 0;         // current idle poster interval
    uint32_t battery_mv = 0;
    uint8_t battery_percent = 0;
    bool charging = false;
    bool battery_valid = false;
    const char* screen = "";
};

extern HearthStats g_hearth_stats;

#endif  // HEARTH_STATS_H_
