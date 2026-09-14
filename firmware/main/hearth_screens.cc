#include "hearth_model.h"

#include <cstdio>
#include <cstring>

#include "hearth_canvas.h"

namespace {

constexpr int kMargin = 18;
constexpr int kBodyWidth = HearthCanvas::kWidth - 2 * kMargin;

void Footer(HearthCanvas& canvas, const HearthState& state) {
    canvas.HLine(kMargin, 268, HearthCanvas::kWidth - 2 * kMargin);
    const char* all = "Today  Buy  Radio  Power";
    canvas.TextCentered(274, all, 1);
    const int total = canvas.TextWidth(all, 1);
    const int start = (HearthCanvas::kWidth - total) / 2;
    int x = start;
    const char* words[] = {"Today", "Buy", "Radio", "Power"};
    const int index = static_cast<int>(state.screen);
    for (int i = 0; i < 4; ++i) {
        const int w = canvas.TextWidth(words[i], 1);
        if (i == index) {
            canvas.FillRect(x, 292, w, 2, true);
        }
        x += w + canvas.TextWidth("  ", 1);
    }
}

void DrawWrapped(HearthCanvas& canvas, int x, int y, int width, const char* text,
                 int scale, int line_height, int max_lines) {
    if (text == nullptr || text[0] == '\0' || max_lines <= 0) {
        return;
    }
    char line[96];
    int used = 0;
    int px = 0;
    int row = 0;
    auto flush = [&]() {
        line[used] = '\0';
        canvas.Text(x, y + row * line_height, line, scale);
        used = 0;
        px = 0;
        row++;
    };
    const char* cursor = text;
    while (*cursor != '\0' && row < max_lines) {
        while (*cursor == ' ') {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }
        const char* start = cursor;
        while (*cursor != '\0' && *cursor != ' ') {
            cursor++;
        }
        const int wlen = static_cast<int>(cursor - start);
        char word[96];
        const int copy = wlen < 95 ? wlen : 95;
        std::memcpy(word, start, static_cast<size_t>(copy));
        word[copy] = '\0';
        const int word_px = canvas.TextWidth(word, scale);
        const int space_px = used ? canvas.TextWidth(" ", scale) : 0;
        if (used > 0 && px + space_px + word_px > width) {
            flush();
            if (row >= max_lines) {
                break;
            }
        }
        if (word_px > width && used == 0) {
            for (int i = 0; i < copy && row < max_lines; ++i) {
                char ch[2] = {word[i], '\0'};
                const int ch_px = canvas.TextWidth(ch, scale);
                if (used > 0 && px + ch_px > width) {
                    flush();
                    if (row >= max_lines) {
                        break;
                    }
                }
                if (used < 95) {
                    line[used++] = word[i];
                    px += ch_px;
                }
            }
            continue;
        }
        if (used && used < 95) {
            line[used++] = ' ';
            px += space_px;
        }
        if (used + copy < 95) {
            std::memcpy(line + used, word, static_cast<size_t>(copy));
            used += copy;
            px += word_px;
        }
    }
    if (used > 0 && row < max_lines) {
        flush();
    }
}

void DrawToday(HearthCanvas& canvas, const HearthState& state) {
    const char* date = state.date[0] ? state.date : "Today";
    canvas.Text(kMargin, 16, date, 2);
    canvas.HLine(kMargin, 52, 220);
    const char* weather =
        state.weather[0] ? state.weather : "weather unknown";
    canvas.Text(kMargin, 60, weather, 1);

    int y = 86;
    if (state.meal[0] != '\0') {
        DrawWrapped(canvas, kMargin, y, kBodyWidth, state.meal, 2, 28, 2);
        y = 142;
    }

    bool any = false;
    for (int i = 0; i < 6; ++i) {
        if (state.today[i][0] == '\0') {
            continue;
        }
        // Meal is already the hero; skip a duplicate first today line.
        if (state.meal[0] != '\0' &&
            std::strcmp(state.today[i], state.meal) == 0) {
            continue;
        }
        any = true;
        canvas.Text(kMargin, y, state.today[i], 1);
        y += 20;
        if (y > 220) {
            break;
        }
    }
    if (!any && state.meal[0] == '\0') {
        canvas.Text(kMargin, 96, "kitchen is clear.", 1);
        canvas.Text(kMargin, 118, "hold OK to speak.", 1);
    }

    if (state.ack[0] != '\0') {
        DrawWrapped(canvas, kMargin, 230, kBodyWidth, state.ack, 1, 16, 1);
    } else {
        char counts[48];
        std::snprintf(counts, sizeof(counts), "Buy %s  ·  Do %s  ·  Pack %s",
                      state.n_buy[0] ? state.n_buy : "0",
                      state.n_do[0] ? state.n_do : "0",
                      state.n_pack[0] ? state.n_pack : "0");
        canvas.Text(kMargin, 230, counts, 1);
    }
}

void DrawBuy(HearthCanvas& canvas, const HearthState& state) {
    canvas.Text(kMargin, 16, "Buy", 2);
    canvas.HLine(kMargin, 52, 80);
    bool any = false;
    int y = 68;
    for (int i = 0; i < 8; ++i) {
        if (state.buy[i][0] == '\0') {
            continue;
        }
        any = true;
        canvas.Text(kMargin, y, state.buy[i], 1);
        y += 22;
    }
    if (!any) {
        canvas.Text(kMargin, 80, "nothing to buy.", 1);
        canvas.Text(kMargin, 104, "hold OK to speak.", 1);
    }
    if (state.ack[0] != '\0') {
        DrawWrapped(canvas, kMargin, 246, kBodyWidth, state.ack, 1, 16, 1);
    }
}

void DrawRadio(HearthCanvas& canvas, const HearthState& state) {
    canvas.Text(kMargin, 18, "Radio", 2);
    canvas.HLine(kMargin, 56, 110);
    canvas.Text(kMargin, 68, state.wifi_status, 1);
    if (state.ip[0] != '\0') {
        canvas.Text(kMargin, 88, state.ip, 1);
    }
    if (state.ap_count <= 0) {
        canvas.Text(kMargin, 120, "no access points yet", 1);
        canvas.Text(kMargin, 144, "short OK rescan", 1);
        return;
    }
    const int top = state.ip[0] ? 112 : 96;
    for (int i = 0; i < state.ap_count; ++i) {
        char line[72];
        std::snprintf(line, sizeof(line), "%-22s  %4d dBm", state.aps[i].ssid,
                      state.aps[i].rssi);
        canvas.Text(kMargin, top + i * 20, line, 1);
    }
    canvas.Text(kMargin, 248, "short OK rescan", 1);
}

void DrawPower(HearthCanvas& canvas, const HearthState& state) {
    canvas.Text(kMargin, 18, "Power", 2);
    canvas.HLine(kMargin, 56, 110);

    char line[64];
    if (state.battery_valid) {
        std::snprintf(line, sizeof(line), "%u mV", state.battery_mv);
        canvas.Text(kMargin, 80, line, 3);
        std::snprintf(line, sizeof(line), "%u percent", state.battery_percent);
        canvas.Text(kMargin, 140, line, 1);
    } else {
        canvas.Text(kMargin, 80, "no reading", 2);
    }

    const char* charge = "idle";
    if (state.charge_complete) {
        charge = "full";
    } else if (state.charging) {
        charge = "charging";
    }
    std::snprintf(line, sizeof(line), "charger  %s", charge);
    canvas.Text(kMargin, 172, line, 1);
    canvas.Text(kMargin, 204, "GPIO17 holds the battery rail.", 1);
    canvas.Text(kMargin, 228, "Hold DOWN 3s to clear the panel and sleep.", 1);
}

void DrawVoiceOverlay(HearthCanvas& canvas, const HearthState& state) {
    if (state.voice == HearthVoice::kIdle) {
        return;
    }
    canvas.FillRect(36, 86, 328, 108, true);
    const char* title = "listening";
    if (state.voice == HearthVoice::kUploading) {
        title = "sending";
    } else if (state.voice == HearthVoice::kError) {
        title = "hub error";
    }
    canvas.TextCentered(108, title, 2, true);
    if (state.voice_status[0] != '\0') {
        canvas.TextCentered(156, state.voice_status, 1, true);
    }
}

}  // namespace

void HearthDraw(HearthCanvas& canvas, const HearthState& state) {
    canvas.Clear(true);
    switch (state.screen) {
        case HearthScreen::kToday:
            DrawToday(canvas, state);
            break;
        case HearthScreen::kBuy:
            DrawBuy(canvas, state);
            break;
        case HearthScreen::kRadio:
            DrawRadio(canvas, state);
            break;
        case HearthScreen::kPower:
            DrawPower(canvas, state);
            break;
        default:
            break;
    }
    Footer(canvas, state);
    DrawVoiceOverlay(canvas, state);
}
