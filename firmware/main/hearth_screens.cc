#include "hearth_model.h"

#include <cstdio>
#include <cstring>

#include "hearth_canvas.h"

namespace {

constexpr int kMargin = 18;
constexpr int kBodyWidth = HearthCanvas::kWidth - 2 * kMargin;

void Footer(HearthCanvas& canvas, const HearthState& state) {
    canvas.HLine(kMargin, 268, HearthCanvas::kWidth - 2 * kMargin);
    const char* all = "Home  Heard  Radio  Power";
    canvas.TextCentered(274, all, 1);
    const int total = canvas.TextWidth(all, 1);
    const int start = (HearthCanvas::kWidth - total) / 2;
    int x = start;
    const char* words[] = {"Home", "Heard", "Radio", "Power"};
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
            // Hard-wrap a single overlong token.
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

void DrawHome(HearthCanvas& canvas, const HearthState& state) {
    canvas.Text(kMargin, 18, "HEARTH", 4);
    canvas.HLine(kMargin, 88, 220);
    canvas.Text(kMargin, 100, "household working memory", 1);
    canvas.Text(kMargin, 122, "hold OK to speak", 1);
    canvas.Text(kMargin, 148, state.wifi_status, 1);
    if (state.ip[0] != '\0') {
        canvas.Text(kMargin, 170, state.ip, 1);
    } else {
        canvas.Text(kMargin, 170, "no ip yet", 1);
    }
    if (state.transcript[0] != '\0') {
        DrawWrapped(canvas, kMargin, 196, kBodyWidth, state.transcript, 1, 18,
                    3);
    } else {
        canvas.Text(kMargin, 196, "nothing heard yet", 1);
    }
    char line[64];
    if (state.battery_valid) {
        std::snprintf(line, sizeof(line), "battery  %u%%   %u mV",
                      state.battery_percent, state.battery_mv);
    } else {
        std::snprintf(line, sizeof(line), "battery  unknown");
    }
    canvas.Text(kMargin, 248, line, 1);
}

void DrawHeard(HearthCanvas& canvas, const HearthState& state) {
    canvas.Text(kMargin, 18, "Heard", 2);
    canvas.HLine(kMargin, 56, 110);
    if (state.transcript[0] == '\0') {
        canvas.Text(kMargin, 80, "nothing yet.", 1);
        canvas.Text(kMargin, 104, "hold OK and speak.", 1);
    } else {
        DrawWrapped(canvas, kMargin, 72, kBodyWidth, state.transcript, 1, 20,
                    7);
    }
    char line[80];
    if (state.last_clip_ms > 0) {
        std::snprintf(line, sizeof(line), "clip  %u ms",
                      static_cast<unsigned>(state.last_clip_ms));
        canvas.Text(kMargin, 224, line, 1);
    }
    if (state.hub[0] != '\0') {
        canvas.Text(kMargin, 246, state.hub, 1);
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
        case HearthScreen::kHome:
            DrawHome(canvas, state);
            break;
        case HearthScreen::kHeard:
            DrawHeard(canvas, state);
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
