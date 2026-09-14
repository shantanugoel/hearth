#include "hearth_model.h"
#include "hearth_util.h"

#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
    assert(HearthScreenNext(HearthScreen::kHome) == HearthScreen::kHeard);
    assert(HearthScreenNext(HearthScreen::kPower) == HearthScreen::kHome);
    assert(HearthScreenPrev(HearthScreen::kHome) == HearthScreen::kPower);
    assert(HearthScreenPrev(HearthScreen::kHeard) == HearthScreen::kHome);
    assert(HearthScreenName(HearthScreen::kRadio)[0] == 'R');
    assert(std::strcmp(HearthScreenName(HearthScreen::kHeard), "Heard") == 0);

    HearthState s;
    s.screen = HearthScreen::kHome;
    for (int i = 0; i < 8; ++i) {
        s.screen = HearthScreenNext(s.screen);
    }
    assert(s.screen == HearthScreen::kHome);

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

    char dst[8];
    HearthCopy(dst, sizeof(dst), "overlong-input");
    assert(std::strcmp(dst, "overlon") == 0);

    std::puts("hearth_model: ok");
    return 0;
}
