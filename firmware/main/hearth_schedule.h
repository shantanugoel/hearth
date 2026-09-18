// Idle-work timing policy for the board loop. Pure and header-only so the
// poll/backoff rules can be checked on a host (see tests/test_hearth_model.cc).
#ifndef HEARTH_SCHEDULE_H_
#define HEARTH_SCHEDULE_H_

#include <cstdint>

namespace hearth {

// Voice filing needs quick turnaround; a poster nobody is looking at does not.
constexpr uint32_t kPosterFilingMs = 2000;
// Somebody pressed a button less than two minutes ago: keep the board fresh.
constexpr uint32_t kPosterActiveMs = 10000;
constexpr uint32_t kActiveWindowMs = 120000;
// Nothing happening: one fetch a minute is enough for weather, alarms, and
// anything spoken at another device.
constexpr uint32_t kPosterIdleMs = 60000;
// After this many idle fetches in a row with no change and no button press,
// settle to the slow lane. Alarms are kept by the PCF8563, so a slow poster
// poll cannot delay a chime.
constexpr uint32_t kIdleBackoffPolls = 10;
constexpr uint32_t kPosterIdleBackoffMs = 300000;
// A failed fetch is not "quiet board" evidence, so do not back off on it.
constexpr uint32_t kPosterRetryMs = 20000;

struct PollPolicy {
    bool filing = false;             // recording, upload, or agent work in flight
    bool failed = false;             // last fetch did not return a poster
    bool changed = false;            // last fetch differed from what is painted
    uint32_t since_input_ms = 0;     // time since the last button press
    uint32_t idle_quiet_polls = 0;   // consecutive idle fetches with no change
};

// How long to wait before the next GET /v1/poster.
inline uint32_t PosterIntervalMs(const PollPolicy& policy) {
    if (policy.filing) return kPosterFilingMs;
    if (policy.failed) return kPosterRetryMs;
    if (policy.since_input_ms < kActiveWindowMs) return kPosterActiveMs;
    if (policy.idle_quiet_polls >= kIdleBackoffPolls) return kPosterIdleBackoffMs;
    return kPosterIdleMs;
}

// Whether this fetch keeps the device on the slow idle ladder.
inline uint32_t NextQuietPolls(const PollPolicy& policy, uint32_t current) {
    if (policy.filing || policy.failed || policy.changed ||
        policy.since_input_ms < kActiveWindowMs) {
        return 0;
    }
    return current + 1;
}

}  // namespace hearth

#endif  // HEARTH_SCHEDULE_H_
