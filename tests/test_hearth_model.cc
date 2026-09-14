#include "hearth_model.h"

#include <cassert>
#include <cstdio>

int main() {
    assert(HearthScreenNext(HearthScreen::kHome) == HearthScreen::kButtons);
    assert(HearthScreenNext(HearthScreen::kPower) == HearthScreen::kHome);
    assert(HearthScreenPrev(HearthScreen::kHome) == HearthScreen::kPower);
    assert(HearthScreenPrev(HearthScreen::kButtons) == HearthScreen::kHome);
    assert(HearthScreenName(HearthScreen::kRadio)[0] == 'R');

    HearthState s;
    s.screen = HearthScreen::kHome;
    for (int i = 0; i < 8; ++i) {
        s.screen = HearthScreenNext(s.screen);
    }
    assert(s.screen == HearthScreen::kHome);
    std::puts("hearth_model: ok");
    return 0;
}
