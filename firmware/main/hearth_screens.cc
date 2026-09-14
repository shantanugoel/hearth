#include "hearth_model.h"

#include <cstdio>
#include <cstring>

#include "hearth_canvas.h"
#include "hearth_icons.h"

namespace {

constexpr int kRail = 40;
constexpr int kLeft = 50;
constexpr int kRight = HearthCanvas::kWidth - 14;
constexpr int kBodyWidth = kRight - kLeft;

const uint16_t* TabIcon(int index) {
    switch (index) {
        case 0:
            return kIconHouse;
        case 1:
            return kIconBasket;
        case 2:
            return kIconPlate;
        case 3:
            return kIconNotes;
        default:
            return kIconRadio;
    }
}

void DrawTabs(HearthCanvas& canvas, const HearthState& state) {
    canvas.FillRect(0, 0, kRail, HearthCanvas::kHeight, true);
    const int index = static_cast<int>(state.screen);
    for (int i = 0; i < 5; ++i) {
        const int y = 16 + i * 56;
        const bool on = i == index;
        if (on) {
            canvas.FillRect(0, y, kRail + 8, 46, false);
            canvas.Icon16(12, y + 15, TabIcon(i), false);
        } else {
            canvas.Icon16(12, y + 15, TabIcon(i), true);
        }
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

void WeatherTemp(const HearthState& state, char* temp, size_t cap,
                 char* rest, size_t rest_cap) {
    temp[0] = '\0';
    rest[0] = '\0';
    const char* w = state.weather[0] ? state.weather : "";
    if (w[0] < '0' || w[0] > '9') {
        std::snprintf(rest, rest_cap, "%s", w);
        return;
    }
    int n = 0;
    while (w[n] && w[n] != ' ' && n + 1 < static_cast<int>(cap)) {
        temp[n] = w[n];
        n++;
    }
    temp[n] = '\0';
    while (w[n] == ' ') {
        n++;
    }
    std::snprintf(rest, rest_cap, "%s", w + n);
}

void DrawAck(HearthCanvas& canvas, const HearthState& state) {
    if (state.ack[0] == '\0') {
        return;
    }
    canvas.HLine(kLeft, 268, kBodyWidth);
    DrawWrapped(canvas, kLeft, 274, kBodyWidth, state.ack, 1, 16, 1);
}

void DrawBucket(HearthCanvas& canvas, int& y, const uint16_t* icon,
                 const char rows[][48], int shown, const char* count) {
    int filled = 0;
    for (int i = 0; i < shown; ++i) {
        if (rows[i][0] != '\0') {
            filled++;
        }
    }
    if (filled == 0) {
        return;
    }
    canvas.Icon16(kLeft, y, icon);
    int row_y = y;
    for (int i = 0; i < shown; ++i) {
        if (rows[i][0] == '\0') {
            continue;
        }
        canvas.Text(kLeft + 22, row_y, rows[i], 1);
        row_y += 18;
    }
    int total = 0;
    if (count != nullptr) {
        for (const char* p = count; *p >= '0' && *p <= '9'; ++p) {
            total = total * 10 + (*p - '0');
        }
    }
    const int extra = total > filled ? total - filled : 0;
    if (extra > 0) {
        char more[16];
        std::snprintf(more, sizeof(more), "+%d", extra);
        canvas.Text(kLeft + 22, row_y, more, 1);
        row_y += 18;
    }
    y = row_y + 8;
}

void DrawToday(HearthCanvas& canvas, const HearthState& state) {
    const char* date = state.date[0] ? state.date : "Today";
    canvas.Text(kLeft, 14, date, 2);

    char temp[12];
    char rest[40];
    WeatherTemp(state, temp, sizeof(temp), rest, sizeof(rest));
    const uint16_t* wx = HearthWeatherIcon(state.wx);
    const int icon_x = kRight - 16;
    canvas.Icon16(icon_x, 14, wx);
    if (temp[0] != '\0') {
        const int tw = canvas.TextWidth(temp, 2);
        canvas.Text(icon_x - 8 - tw, 14, temp, 2);
    }

    int y = 52;
    canvas.HLine(kLeft, y, kBodyWidth);
    y = 60;

    if (state.alarm[0] != '\0') {
        canvas.Icon16(kLeft, y, kIconBell);
        canvas.Text(kLeft + 22, y, state.alarm, 1);
        y += 22;
    }

    if (state.meal[0] != '\0') {
        canvas.Icon16(kLeft, y, kIconPlate);
        DrawWrapped(canvas, kLeft + 22, y, kBodyWidth - 22, state.meal, 2, 26,
                    1);
        y += 32;
    }

    DrawBucket(canvas, y, kIconBasket, state.buy, 2, state.n_buy);
    DrawBucket(canvas, y, kIconNotes, state.notes, 2, state.n_notes);

    if (state.meal[0] == '\0' && state.buy[0][0] == '\0' &&
        state.notes[0][0] == '\0' && state.alarm[0] == '\0') {
        canvas.Text(kLeft, 96, "kitchen is clear.", 1);
        canvas.Text(kLeft, 118, "hold OK to speak.", 1);
    }
    DrawAck(canvas, state);
}

void DrawList(HearthCanvas& canvas, const uint16_t* icon, const char* title,
               const char rows[][48], int n, const char* empty,
               const HearthState& state) {
    canvas.Icon16(kLeft, 16, icon);
    canvas.Text(kLeft + 22, 16, title, 2);
    canvas.HLine(kLeft, 48, canvas.TextWidth(title, 2) + 22);
    bool any = false;
    int y = 62;
    for (int i = 0; i < n; ++i) {
        if (rows[i][0] == '\0') {
            continue;
        }
        any = true;
        canvas.FillRect(kLeft, y + 6, 6, 6, true);
        canvas.Text(kLeft + 14, y, rows[i], 1);
        y += 22;
        if (y > 246) {
            break;
        }
    }
    if (!any) {
        canvas.Text(kLeft, 80, empty, 1);
        canvas.Text(kLeft, 104, "hold OK to speak.", 1);
    }
    DrawAck(canvas, state);
}

void DrawMenu(HearthCanvas& canvas, const HearthState& state) {
    canvas.Icon16(kLeft, 16, kIconPlate);
    canvas.Text(kLeft + 22, 16, "Menu", 2);
    canvas.HLine(kLeft, 48, 86);
    bool any = false;
    for (int i = 0; i < 7; ++i) {
        if (state.menu[i][0] == '\0') {
            continue;
        }
        any = true;
        const char* line = state.menu[i];
        const bool today = line[0] == '*';
        const int y = 58 + i * 26;
        if (today) {
            canvas.FillRect(kLeft - 2, y - 2, kBodyWidth + 4, 22, true);
            canvas.Text(kLeft + 4, y, line[0] == '*' ? line + 1 : line, 1,
                        true);
        } else {
            canvas.Text(kLeft + 4, y, line[0] == ' ' ? line + 1 : line, 1);
        }
    }
    if (!any) {
        canvas.Text(kLeft, 80, "no dinners yet.", 1);
        canvas.Text(kLeft, 104, "hold OK to speak.", 1);
    }
    DrawAck(canvas, state);
}

void DrawPulse(HearthCanvas& canvas, const HearthState& state) {
    canvas.Icon16(kLeft, 16, kIconRadio);
    canvas.Text(kLeft + 22, 16, "Pulse", 2);
    canvas.HLine(kLeft, 48, 90);
    canvas.Text(kLeft, 62, state.wifi_status, 1);
    if (state.ip[0] != '\0') {
        canvas.Text(kLeft, 82, state.ip, 1);
    }
    char line[64];
    if (state.battery_valid) {
        std::snprintf(line, sizeof(line), "battery  %u%%   %u mV",
                      state.battery_percent, state.battery_mv);
    } else {
        std::snprintf(line, sizeof(line), "battery  unknown");
    }
    canvas.Text(kLeft, 106, line, 1);
    const char* charge = "idle";
    if (state.charge_complete) {
        charge = "full";
    } else if (state.charging) {
        charge = "charging";
    }
    std::snprintf(line, sizeof(line), "charger  %s", charge);
    canvas.Text(kLeft, 126, line, 1);
    if (state.alarm[0] != '\0') {
        canvas.Icon16(kLeft, 148, kIconBell);
        canvas.Text(kLeft + 22, 148, state.alarm, 1);
    }
    if (state.hub[0] != '\0') {
        canvas.Text(kLeft, 172, state.hub, 1);
    }
    if (state.transcript[0] != '\0') {
        DrawWrapped(canvas, kLeft, 196, kBodyWidth, state.transcript, 1, 18,
                    2);
    } else {
        canvas.Text(kLeft, 196, "nothing heard yet", 1);
    }
    canvas.Text(kLeft, 250, "Hold DOWN 3s to sleep.", 1);
}

void DrawVoiceOverlay(HearthCanvas& canvas, const HearthState& state) {
    if (state.voice == HearthVoice::kIdle) {
        return;
    }
    canvas.FillRect(58, 86, 310, 108, true);
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
            DrawList(canvas, kIconBasket, "Buy", state.buy, 8,
                     "nothing to buy.", state);
            break;
        case HearthScreen::kMenu:
            DrawMenu(canvas, state);
            break;
        case HearthScreen::kNotes:
            DrawList(canvas, kIconNotes, "Notes", state.notes, 8,
                     "no notes yet.", state);
            break;
        case HearthScreen::kPulse:
            DrawPulse(canvas, state);
            break;
        default:
            break;
    }
    DrawTabs(canvas, state);
    DrawVoiceOverlay(canvas, state);
}
