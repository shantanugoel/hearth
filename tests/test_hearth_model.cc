#include "hearth_model.h"
#include "hearth_util.h"

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
        "{\"date\":\"Mon 14 Sep\",\"weather\":\"29C  fair\",\"wx\":\"sun\","
        "\"meal\":\"dal rice\",\"ack\":\"Added oat milk to Buy.\","
        "\"pending\":\"0\",\"n_buy\":\"1\",\"n_notes\":\"1\","
        "\"b0\":\"oat milk\",\"b1\":\"\",\"n0\":\"swim kit  Maya\","
        "\"m0\":\"*Mon  dal rice\",\"m1\":\" Tue\",\"alarm\":\"07:00 school\","
        "\"ahh\":\"7\",\"amm\":\"0\"}";
    HearthApplyPoster(&s, poster);
    assert(std::strcmp(s.date, "Mon 14 Sep") == 0);
    assert(std::strcmp(s.buy[0], "oat milk") == 0);
    assert(s.buy[1][0] == '\0');
    assert(std::strcmp(s.n_buy, "1") == 0);
    assert(std::strcmp(s.notes[0], "swim kit  Maya") == 0);
    assert(s.menu[0][0] == '*');
    assert(std::strcmp(s.wx, "sun") == 0);
    assert(s.alarm_h == 7);
    assert(s.alarm_m == 0);
    assert(!HearthPending(s));
    s.voice = HearthVoice::kFiling;
    assert(HearthBusy(s));
    s.voice = HearthVoice::kIdle;
    assert(!HearthBusy(s));
    HearthState empty_alarm;
    HearthApplyPoster(&empty_alarm,
                      "{\"date\":\"Mon\",\"ahh\":\"\",\"amm\":\"\"}");
    assert(empty_alarm.alarm_h < 0);

    char dst[8];
    HearthCopy(dst, sizeof(dst), "overlong-input");
    assert(std::strcmp(dst, "overlon") == 0);

    std::puts("hearth_model: ok");
    return 0;
}
