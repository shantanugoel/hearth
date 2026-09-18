#include "hearth_model.h"
#include "hearth_schedule.h"
#include "hearth_util.h"
#include "zectrix_button_logic.h"

#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
    assert(HearthScreenNext(HearthScreen::kToday) == HearthScreen::kBuy);
    assert(HearthScreenNext(HearthScreen::kBuy) == HearthScreen::kMenu);
    assert(HearthScreenNext(HearthScreen::kMenu) == HearthScreen::kNotes);
    assert(HearthScreenNext(HearthScreen::kPulse) == HearthScreen::kToday);
    assert(HearthScreenPrev(HearthScreen::kToday) == HearthScreen::kPulse);
    assert(HearthScreenPrev(HearthScreen::kBuy) == HearthScreen::kToday);
    assert(HearthScreenName(HearthScreen::kPulse)[0] == 'P');
    assert(std::strcmp(HearthScreenName(HearthScreen::kMenu), "Menu") == 0);
    assert(std::strcmp(HearthScreenName(HearthScreen::kNotes), "Notes") == 0);
    assert(HearthWeekday(2026, 9, 16) == 3);  // Wednesday
    assert(HearthWeekday(2024, 2, 29) == 4);  // Thursday
    assert(HearthWeekday(2000, 1, 1) == 6);   // Saturday
    assert(HearthWeekday(2026, 13, 1) == -1);

    HearthState s;
    s.screen = HearthScreen::kToday;
    for (int i = 0; i < static_cast<int>(HearthScreen::kCount); ++i) {
        s.screen = HearthScreenNext(s.screen);
    }
    assert(s.screen == HearthScreen::kToday);

    char out[64];
    assert(HearthJsonString(
        "{\"text\":\"we are out of oat milk\",\"ms\":321}", "text", out,
        sizeof(out)));
    assert(std::strcmp(out, "we are out of oat milk") == 0);
    assert(HearthJsonString("{\"text\":\"hi\",\"ms\":321}", "ms", out,
                            sizeof(out)));
    assert(std::strcmp(out, "321") == 0);
    assert(HearthJsonString("{\"text\":\"a\\\"b\"}", "text", out, sizeof(out)));
    assert(std::strcmp(out, "a\"b") == 0);

    const char* poster =
        "{\"date\":\"Mon 14 Sep\",\"clock\":\"2026-09-14T09:42:00\","
        "\"weather\":\"29C  fair\",\"wx\":\"sun\","
        "\"meal\":\"dal rice\",\"ack\":\"Added oat milk to Buy.\","
        "\"pending\":\"1\",\"queue\":\"2\",\"heard\":\"set lunch to dal\","
        "\"n_buy\":\"1\",\"n_notes\":\"1\","
        "\"b0\":\"oat milk\",\"b1\":\"\",\"n0\":\"swim kit  Maya\","
        "\"m0\":\"Lunch: dal rice\",\"mid0\":\"mon/lunch\","
        "\"m1\":\"Dinner: pasta\",\"mid1\":\"tue/dinner\","
        "\"alarm\":\"07:00 school\","
        "\"ahh\":\"7\",\"amm\":\"0\"}";
    HearthApplyPoster(&s, poster);
    assert(std::strcmp(s.date, "Mon 14 Sep") == 0);
    assert(std::strcmp(s.clock, "2026-09-14T09:42:00") == 0);
    assert(std::strcmp(s.buy[0], "oat milk") == 0);
    assert(s.buy[1][0] == '\0');
    assert(std::strcmp(s.n_buy, "1") == 0);
    assert(std::strcmp(s.notes[0], "swim kit  Maya") == 0);
    assert(std::strcmp(s.menu[0], "Lunch: dal rice") == 0);
    assert(std::strcmp(s.menu_id[0], "mon/lunch") == 0);
    assert(s.queue_depth == 2);
    assert(std::strcmp(s.transcript, "set lunch to dal") == 0);
    assert(std::strcmp(s.wx, "sun") == 0);
    assert(s.alarm_h == 7);
    assert(s.alarm_m == 0);
    assert(s.alarm_s == 0);
    assert(HearthPending(s));
    s.voice = HearthVoice::kFiling;
    assert(HearthBusy(s));
    s.voice = HearthVoice::kIdle;
    assert(!HearthBusy(s));
    HearthState empty_alarm;
    HearthApplyPoster(&empty_alarm,
                      "{\"date\":\"Mon\",\"ahh\":\"\",\"amm\":\"\"}");
    assert(empty_alarm.alarm_h < 0);
    HearthState timer;
    HearthApplyPoster(&timer,
                     "{\"alarm\":\"12:04:30 Timer\",\"ahh\":\"12\",\"amm\":\"4\",\"asec\":\"30\"}");
    assert(timer.alarm_h == 12);
    assert(timer.alarm_m == 4);
    assert(timer.alarm_s == 30);

    char dst[8];
    HearthCopy(dst, sizeof(dst), "overlong-input");
    assert(std::strcmp(dst, "overlon") == 0);

    // --- poster dedupe must survive the hub's "\":\ `" spacing ---
    // The hub writes json.dumps defaults, so a hardcoded "\"clock\":\"" needle
    // once missed every fetch and repainted the panel each poll.
    assert(HearthSameExcept("{\"a\": \"1\", \"clock\": \"2026-09-18T08:00:00\"}",
                           "{\"a\": \"1\", \"clock\": \"2026-09-18T08:00:01\"}",
                           "clock"));
    assert(HearthSameExcept("{\"a\":\"1\",\"clock\":\"2026-09-18T08:00:00\"}",
                           "{\"a\":\"1\",\"clock\":\"2026-09-18T08:00:02\"}",
                           "clock"));
    assert(!HearthSameExcept("{\"a\": \"1\", \"clock\": \"2026-09-18T08:00:00\"}",
                            "{\"a\": \"2\", \"clock\": \"2026-09-18T08:00:01\"}",
                            "clock"));
    assert(!HearthSameExcept("{\"a\": \"1\"}", "{\"a\": \"2\"}", "clock"));
    assert(HearthSameExcept("{\"a\": \"1\"}", "{\"a\": \"1\"}", "clock"));
    // A value that merely mentions the key must not be read as the field.
    assert(!HearthSameExcept(
        "{\"heard\": \"the clock is ticking\", \"b\": \"1\"}",
        "{\"heard\": \"the clock is tocking\", \"b\": \"1\"}", "clock"));

    // --- front button: a short press must stay a page turn ---
    struct GestureResult {
        bool click;
        bool long_press;
        uint32_t long_at_ms;
    };
    auto press_gesture = [](uint32_t held_ms, uint32_t bounce_ms) {
        const zectrix::ButtonTiming timing;
        zectrix::ButtonTracker tracker;
        zectrix::ResetButtonTracker(tracker, 0, false);
        GestureResult out = {false, false, 0};
        for (uint32_t ms = 0; ms <= 3000; ms += 20) {
            bool pressed = ms >= 100 && ms < 100 + held_ms;
            // Contact bounce in the middle of a press, shorter than the
            // debounce window.
            if (bounce_ms && ms >= 100 + bounce_ms &&
                ms < 100 + bounce_ms + 20) {
                pressed = false;
            }
            const zectrix::ButtonEvent event = zectrix::TrackButtonPress(
                tracker, timing, ms, pressed);
            if (event == zectrix::ButtonEvent::kClick) out.click = true;
            if (event == zectrix::ButtonEvent::kLongPress) {
                out.long_press = true;
                out.long_at_ms = ms;
            }
        }
        return out;
    };

    assert(press_gesture(120, 0).click);
    assert(!press_gesture(120, 0).long_press);
    // The tail of ordinary human short presses: still a page turn. The old
    // 220 ms threshold, counted after the debounce, turned these into speech.
    assert(press_gesture(300, 0).click);
    assert(!press_gesture(300, 0).long_press);
    assert(press_gesture(420, 0).click);
    assert(!press_gesture(420, 0).long_press);
    // A real hold to speak: long press fires near the threshold, and the
    // release must not also turn the page.
    assert(press_gesture(900, 0).long_press);
    assert(!press_gesture(900, 0).click);
    // Press onset is t=100, so the long press lands on the 450 ms threshold
    // plus at most one 20 ms sample of slack.
    assert(press_gesture(900, 0).long_at_ms >= 550);
    assert(press_gesture(900, 0).long_at_ms <= 580);
    // Bounce shorter than the debounce does not split one press into two.
    assert(press_gesture(200, 60).click);
    assert(!press_gesture(200, 60).long_press);
    // A button already held when the board booted swallows its own release.
    {
        zectrix::ButtonTiming timing;
        zectrix::ButtonTracker tracker;
        zectrix::ResetButtonTracker(tracker, 0, true);
        bool saw_event = false;
        for (uint32_t ms = 0; ms <= 400; ms += 20) {
            if (zectrix::TrackButtonPress(tracker, timing, ms, ms < 200) !=
                zectrix::ButtonEvent::kNone) {
                saw_event = true;
            }
        }
        assert(!saw_event);
        assert(press_gesture(600, 0).long_press);
    }

    // --- idle work: a quiet fridge does not fetch every ten seconds ---
    {
        hearth::PollPolicy quiet;
        quiet.since_input_ms = 600000;
        assert(hearth::PosterIntervalMs(quiet) == hearth::kPosterIdleMs);
        assert(hearth::kPosterIdleMs >= 30000);
        quiet.idle_quiet_polls = hearth::kIdleBackoffPolls;
        assert(hearth::PosterIntervalMs(quiet) ==
               hearth::kPosterIdleBackoffMs);

        hearth::PollPolicy filing;
        filing.since_input_ms = 600000;
        filing.filing = true;
        assert(hearth::PosterIntervalMs(filing) == hearth::kPosterFilingMs);

        hearth::PollPolicy broken;
        broken.since_input_ms = 600000;
        broken.idle_quiet_polls = hearth::kIdleBackoffPolls;
        broken.failed = true;
        assert(hearth::PosterIntervalMs(broken) == hearth::kPosterRetryMs);

        hearth::PollPolicy hands_on;
        hands_on.since_input_ms = 30000;
        hands_on.idle_quiet_polls = hearth::kIdleBackoffPolls;
        assert(hearth::PosterIntervalMs(hands_on) == hearth::kPosterActiveMs);

        // The slow ladder only climbs while nothing happens, and resets on a
        // press, a change, or a filing turn.
        assert(hearth::NextQuietPolls(quiet, 3) == 4);
        assert(hearth::NextQuietPolls(filing, 3) == 0);
        assert(hearth::NextQuietPolls(broken, 3) == 0);
        assert(hearth::NextQuietPolls(hands_on, 3) == 0);
        hearth::PollPolicy moved = quiet;
        moved.changed = true;
        assert(hearth::NextQuietPolls(moved, 3) == 0);
    }

    std::puts("hearth_model: ok");
    return 0;
}
