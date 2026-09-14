#include "hearth_model.h"
#include "hearth_util.h"

#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
    assert(HearthScreenNext(HearthScreen::kToday) == HearthScreen::kBuy);
    assert(HearthScreenNext(HearthScreen::kPower) == HearthScreen::kToday);
    assert(HearthScreenPrev(HearthScreen::kToday) == HearthScreen::kPower);
    assert(HearthScreenPrev(HearthScreen::kBuy) == HearthScreen::kToday);
    assert(HearthScreenName(HearthScreen::kRadio)[0] == 'R');
    assert(std::strcmp(HearthScreenName(HearthScreen::kBuy), "Buy") == 0);

    HearthState s;
    s.screen = HearthScreen::kToday;
    for (int i = 0; i < 8; ++i) {
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
        "{\"date\":\"Mon 14 Sep\",\"weather\":\"29C  fair\",\"meal\":\"dal rice\","
        "\"ack\":\"Added oat milk to Buy.\",\"n_buy\":\"1\",\"n_do\":\"0\","
        "\"n_pack\":\"1\",\"t0\":\"oat milk\",\"t1\":\"\",\"b0\":\"oat milk\","
        "\"b1\":\"\"}";
    HearthApplyPoster(&s, poster);
    assert(std::strcmp(s.date, "Mon 14 Sep") == 0);
    assert(std::strcmp(s.buy[0], "oat milk") == 0);
    assert(s.buy[1][0] == '\0');
    assert(std::strcmp(s.n_buy, "1") == 0);

    char dst[8];
    HearthCopy(dst, sizeof(dst), "overlong-input");
    assert(std::strcmp(dst, "overlon") == 0);

    std::puts("hearth_model: ok");
    return 0;
}
