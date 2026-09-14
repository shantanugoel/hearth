#include "hearth_model.h"

#include <cstdio>
#include <cstring>

#include "hearth_canvas.h"

namespace {

constexpr int kMargin = 18;
constexpr int kBodyWidth = HearthCanvas::kWidth - 2 * kMargin;

void Footer(HearthCanvas& canvas, const HearthState& state) {
    canvas.HLine(kMargin, 268, HearthCanvas::kWidth - 2 * kMargin);
    const char* all = "Today Buy Menu Do Pack Pulse";
    canvas.TextCentered(274, all, 1);
    const int total = canvas.TextWidth(all, 1);
    const int start = (HearthCanvas::kWidth - total) / 2;
    int x = start;
    const char* words[] = {"Today", "Buy", "Menu", "Do", "Pack", "Pulse"};
    const int index = static_cast<int>(state.screen);
    for (int i = 0; i < 6; ++i) {
        const int w = canvas.TextWidth(words[i], 1);
        if (i == index) {
            canvas.FillRect(x, 292, w, 2, true);
        }
        x += w + canvas.TextWidth(" ", 1);
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

void DrawAckOrCounts(HearthCanvas& canvas, const HearthState& state) {
    if (state.ack[0] != '\0') {
        DrawWrapped(canvas, kMargin, 230, kBodyWidth, state.ack, 1, 16, 1);
        return;
    }
    char counts[48];
    std::snprintf(counts, sizeof(counts), "Buy %s  -  Do %s  -  Pack %s",
                  state.n_buy[0] ? state.n_buy : "0",
                  state.n_do[0] ? state.n_do : "0",
                  state.n_pack[0] ? state.n_pack : "0");
    canvas.Text(kMargin, 230, counts, 1);
}

void DrawList(HearthCanvas& canvas, const char* title,
              const char rows[][48], int n, const char* empty_a,
              const char* empty_b, const HearthState& state) {
    canvas.Text(kMargin, 16, title, 2);
    canvas.HLine(kMargin, 52, canvas.TextWidth(title, 2));
    bool any = false;
    int y = 68;
    for (int i = 0; i < n; ++i) {
        if (rows[i][0] == '\0') {
            continue;
        }
        any = true;
        canvas.Text(kMargin, y, rows[i], 1);
        y += 22;
        if (y > 220) {
            break;
        }
    }
    if (!any) {
        canvas.Text(kMargin, 80, empty_a, 1);
        canvas.Text(kMargin, 104, empty_b, 1);
    }
    DrawAckOrCounts(canvas, state);
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
    DrawAckOrCounts(canvas, state);
}

void DrawMenu(HearthCanvas& canvas, const HearthState& state) {
    canvas.Text(kMargin, 16, "Menu", 2);
    canvas.HLine(kMargin, 52, 80);
    bool any = false;
    for (int i = 0; i < 7; ++i) {
        if (state.menu[i][0] == '\0') {
            continue;
        }
        any = true;
        const char* line = state.menu[i];
        const bool today = line[0] == '*';
        if (today) {
            canvas.FillRect(kMargin - 4, 64 + i * 22, 4, 16, false);
        }
        canvas.Text(kMargin, 64 + i * 22, today ? line + 1 : line, 1);
    }
    if (!any) {
        canvas.Text(kMargin, 80, "no dinners yet.", 1);
        canvas.Text(kMargin, 104, "hold OK to speak.", 1);
    }
}

void DrawPulse(HearthCanvas& canvas, const HearthState& state) {
    canvas.Text(kMargin, 16, "Pulse", 2);
    canvas.HLine(kMargin, 52, 90);
    canvas.Text(kMargin, 68, state.wifi_status, 1);
    if (state.ip[0] != '\0') {
        canvas.Text(kMargin, 88, state.ip, 1);
    }
    char line[64];
    if (state.battery_valid) {
        std::snprintf(line, sizeof(line), "battery  %u%%   %u mV",
                      state.battery_percent, state.battery_mv);
    } else {
        std::snprintf(line, sizeof(line), "battery  unknown");
    }
    canvas.Text(kMargin, 112, line, 1);
    const char* charge = "idle";
    if (state.charge_complete) {
        charge = "full";
    } else if (state.charging) {
        charge = "charging";
    }
    std::snprintf(line, sizeof(line), "charger  %s", charge);
    canvas.Text(kMargin, 132, line, 1);
    if (state.hub[0] != '\0') {
        canvas.Text(kMargin, 156, state.hub, 1);
    }
    if (state.transcript[0] != '\0') {
        DrawWrapped(canvas, kMargin, 180, kBodyWidth, state.transcript, 1, 18,
                    2);
    } else {
        canvas.Text(kMargin, 180, "nothing heard yet", 1);
    }
    canvas.Text(kMargin, 230, "Hold DOWN 3s to sleep.", 1);
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
            DrawList(canvas, "Buy", state.buy, 8, "nothing to buy.",
                     "hold OK to speak.", state);
            break;
        case HearthScreen::kMenu:
            DrawMenu(canvas, state);
            break;
        case HearthScreen::kDo:
            DrawList(canvas, "Do", state.chores, 6, "nothing to do.",
                     "hold OK to speak.", state);
            break;
        case HearthScreen::kPack:
            DrawList(canvas, "Pack", state.pack, 6, "nothing to pack.",
                     "hold OK to speak.", state);
            break;
        case HearthScreen::kPulse:
            DrawPulse(canvas, state);
            break;
        default:
            break;
    }
    Footer(canvas, state);
    DrawVoiceOverlay(canvas, state);
}
