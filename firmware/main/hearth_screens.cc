#include "hearth_model.h"

#include <cstdio>
#include <cstring>

#include "hearth_canvas.h"
#include "hearth_icons.h"

namespace {

constexpr int kNavHeight = 40;
constexpr int kLeft = 20;
constexpr int kRight = HearthCanvas::kWidth - 20;
constexpr int kBodyWidth = kRight - kLeft;

const char* TabLabel(int index) {
    constexpr const char* kLabels[] = {"Today", "Buy", "Menu", "Notes", "Pulse"};
    return kLabels[index];
}

const uint16_t* TabIcon(int index) {
    switch (index) {
        case 0:
            return kIconHouse;
        case 1:
            return kIconDollar;
        case 2:
            return kIconUtensils;
        case 3:
            return kIconNotes;
        default:
            return kIconRadio;
    }
}

void DrawTabs(HearthCanvas& canvas, const HearthState& state) {
    const int index = static_cast<int>(state.screen);
    for (int i = 0; i < 5; ++i) {
        const int x = i * 80;
        const bool on = i == index;
        canvas.FillRect(x + 1, 2, 78, 34, on);
        canvas.Icon16(x + 8, 11, TabIcon(i), on);
        canvas.Text(x + 29, 11, TabLabel(i), 1, on);
    }
    canvas.HLine(0, kNavHeight - 1, HearthCanvas::kWidth);
}

void DrawWrapped(HearthCanvas& canvas, int x, int y, int width, const char* text,
                 int scale, int line_height, int max_lines, bool inverted = false) {
    if (text == nullptr || text[0] == '\0' || max_lines <= 0) {
        return;
    }
    char line[96];
    int used = 0;
    int px = 0;
    int row = 0;
    auto flush = [&]() {
        line[used] = '\0';
        canvas.Text(x, y + row * line_height, line, scale, inverted);
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

void WeatherTemp(const HearthState& state, char* temp, size_t cap) {
    temp[0] = '\0';
    const char* w = state.weather[0] ? state.weather : "";
    if (w[0] < '0' || w[0] > '9') {
        return;
    }
    int n = 0;
    while (w[n] && w[n] != ' ' && n + 1 < static_cast<int>(cap)) {
        temp[n] = w[n];
        n++;
    }
    temp[n] = '\0';
}

void DrawStatus(HearthCanvas& canvas, const HearthState& state) {
    const char* line = nullptr;
    bool busy = false;
    if (state.voice == HearthVoice::kListening) {
        line = state.voice_status[0] ? state.voice_status : "listening";
        busy = true;
    } else if (state.voice == HearthVoice::kUploading) {
        line = state.voice_status[0] ? state.voice_status : "sending";
        busy = true;
    } else if (state.voice == HearthVoice::kFiling) {
        line = state.voice_status[0] ? state.voice_status : "filing";
        busy = true;
    } else if (state.voice == HearthVoice::kError) {
        line = state.voice_status[0] ? state.voice_status : "hub error";
        busy = true;
    } else if (state.ack[0] != '\0') {
        line = state.ack;
    }
    if (line == nullptr) {
        return;
    }
    const int y = 272;
    const int h = 28;
    canvas.FillRect(0, y, HearthCanvas::kWidth, h, busy);
    canvas.HLine(kLeft, y, kBodyWidth, !busy);
    const uint16_t* icon = busy ? kIconRadio : kIconDot;
    canvas.Icon16(kLeft, y + 6, icon, busy);
    canvas.Text(kLeft + 24, y + 6, line, 1, busy);
}

void DrawToday(HearthCanvas& canvas, const HearthState& state) {
    const char* date = state.date[0] ? state.date : "Today";
    canvas.Text(kLeft, 52, date, 2);

    char temp[12];
    WeatherTemp(state, temp, sizeof(temp));
    const uint16_t* wx = HearthWeatherIcon(state.wx);
    const int icon_x = kRight - 16;
    canvas.Icon16(icon_x, 57, wx);
    if (temp[0] != '\0') {
        const int tw = canvas.TextWidth(temp, 2);
        canvas.Text(icon_x - 8 - tw, 52, temp, 2);
    }

    canvas.HLine(kLeft, 86, kBodyWidth);

    if (state.alarm[0] != '\0') {
        canvas.Icon16(kLeft, 96, kIconBell);
        canvas.Text(kLeft + 22, 96, state.alarm, 1);
    }

    const int heading_y = state.alarm[0] ? 121 : 98;
    canvas.Icon16(kLeft, heading_y, kIconNotes);
    char heading[32];
    std::snprintf(heading, sizeof(heading), "NOTES  %s", state.n_notes);
    canvas.Text(kLeft + 24, heading_y, heading, 1);
    const int first_y = heading_y + 23;
    int shown = 0;
    const int visible_cap = state.alarm[0] ? 3 : 4;
    const int start = state.selected[0] >= visible_cap ? state.selected[0] - visible_cap + 1 : 0;
    for (int i = start; i < 12 && i < start + visible_cap && state.notes[i][0]; ++i) {
        const int y = first_y + (i - start) * 24;
        const bool selected = state.selected[0] == i;
        if (selected) canvas.FillRect(kLeft - 3, y - 3, kBodyWidth + 6, 22, true);
        canvas.Rect(kLeft + 4, y + 1, 13, 13, !selected);
        if (state.notes_done[i]) canvas.Text(kLeft + 6, y - 1, "x", 1, selected);
        DrawWrapped(canvas, kLeft + 25, y, kBodyWidth - 32, state.notes[i], 1, 18, 1, selected);
        shown++;
    }
    if (!shown) {
        canvas.Text(kLeft, first_y + 10, "No notes yet.", 2);
    }
    canvas.HLine(kLeft, 213, kBodyWidth);
    canvas.Line(198, 220, 198, 268);
    canvas.Icon16(kLeft, 221, kIconDollar);
    char count[24];
    std::snprintf(count, sizeof(count), "BUY  %s", state.n_buy);
    canvas.Text(kLeft + 22, 221, count, 1);
    canvas.Icon16(216, 221, kIconUtensils);
    canvas.Text(238, 221, "MENU", 1);
    for (int i = 0; i < 2; ++i) {
        if (state.peek_buy[i][0])
            DrawWrapped(canvas, kLeft, 240 + i * 17, 164,
                        state.peek_buy[i], 1, 17, 1);
        if (state.peek_menu[i][0])
            DrawWrapped(canvas, 216, 240 + i * 17, 164,
                        state.peek_menu[i], 1, 17, 1);
    }
}

void DrawList(HearthCanvas& canvas, const uint16_t* icon, const char* title,
               const char rows[][48], const bool* done, int n, int selected,
               const char* empty) {
    canvas.Icon16(kLeft, 56, icon);
    canvas.Text(kLeft + 24, 52, title, 2);
    canvas.HLine(kLeft, 86, kBodyWidth);
    bool any = false;
    const int start = selected >= 7 ? selected - 6 : 0;
    int y = 98;
    for (int i = start; i < n && i < start + 7; ++i) {
        if (rows[i][0] == '\0') {
            continue;
        }
        any = true;
        const bool on = i == selected;
        if (on) canvas.FillRect(kLeft - 3, y - 3, kBodyWidth + 6, 22, true);
        canvas.Rect(kLeft + 3, y + 2, 12, 12, !on);
        if (done[i]) canvas.Text(kLeft + 5, y, "x", 1, on);
        DrawWrapped(canvas, kLeft + 23, y, kBodyWidth - 30, rows[i], 1, 18, 1, on);
        y += 23;
        if (y > 250) {
            break;
        }
    }
    if (!any) {
        canvas.Text(kLeft, 112, empty, 2);
        canvas.Text(kLeft, 148, "Hold OK to speak.", 1);
    }
}

void DrawMenu(HearthCanvas& canvas, const HearthState& state) {
    canvas.Icon16(kLeft, 56, kIconUtensils);
    canvas.Text(kLeft + 24, 52, "Menu", 2);
    canvas.HLine(kLeft, 86, kBodyWidth);
    bool any = false;
    constexpr const char* kDays[] = {"Monday", "Tuesday", "Wednesday",
                                     "Thursday", "Friday", "Saturday", "Sunday"};
    constexpr const char* kKeys[] = {"mon", "tue", "wed", "thu", "fri", "sat", "sun"};
    // Reserve space for a heading even if each visible meal is on a new day.
    const int start = state.selected[2] >= 3 ? state.selected[2] - 2 : 0;
    int y = 96;
    int previous_day = -1;
    for (int i = start; i < 21 && y < 258; ++i) {
        if (state.menu[i][0] == '\0') {
            break;
        }
        any = true;
        int day = -1;
        for (int d = 0; d < 7; ++d)
            if (std::strncmp(state.menu_id[i], kKeys[d], 3) == 0) day = d;
        if (day != previous_day) {
            if (y > 229) break;
            if (previous_day >= 0) canvas.HLine(kLeft + 8, y - 5, kBodyWidth - 16);
            canvas.Text(kLeft + 8, y, day >= 0 ? kDays[day] : "Day", 2);
            y += 30;
            previous_day = day;
        }
        if (y > 253) break;
        if (i == state.selected[2]) {
            canvas.FillRect(kLeft, y - 2, kBodyWidth, 21, true);
            DrawWrapped(canvas, kLeft + 14, y, kBodyWidth - 24,
                        state.menu[i], 1, 19, 1, true);
        } else {
            DrawWrapped(canvas, kLeft + 14, y, kBodyWidth - 24,
                        state.menu[i], 1, 19, 1);
        }
        y += 25;
    }
    if (!any) {
        canvas.Text(kLeft, 112, "No meals yet.", 2);
        canvas.Text(kLeft, 148, "Hold OK to speak.", 1);
    }
}

void DrawPulse(HearthCanvas& canvas, const HearthState& state) {
    canvas.Icon16(kLeft, 56, kIconRadio);
    canvas.Text(kLeft + 24, 52, "Pulse", 2);
    canvas.HLine(kLeft, 86, kBodyWidth);
    canvas.Text(kLeft, 98, "WI-FI", 1);
    canvas.Text(112, 98, state.wifi_status, 1);
    if (state.ip[0] != '\0') {
        canvas.Text(112, 118, state.ip, 1);
    }
    char line[64];
    if (state.battery_valid) {
        std::snprintf(line, sizeof(line), "battery  %u%%   %u mV",
                      state.battery_percent, state.battery_mv);
    } else {
        std::snprintf(line, sizeof(line), "battery  unknown");
    }
    canvas.Text(kLeft, 144, "POWER", 1);
    canvas.Text(112, 144, line, 1);
    const char* charge = "idle";
    if (state.charge_complete) {
        charge = "full";
    } else if (state.charging) {
        charge = "charging";
    }
    std::snprintf(line, sizeof(line), "charger  %s", charge);
    canvas.Text(112, 164, line, 1);
    if (state.alarm[0] != '\0') {
        canvas.Icon16(kLeft, 188, kIconBell);
        canvas.Text(kLeft + 24, 188, state.alarm, 1);
    }
    if (state.hub[0] != '\0') {
        canvas.Text(204, 188, "hub online", 1);
    }
    if (state.transcript[0] != '\0') {
        canvas.Text(kLeft, 216, "LAST HEARD", 1);
        DrawWrapped(canvas, kLeft, 238, kBodyWidth, state.transcript, 1, 18, 1);
    } else {
        canvas.Text(kLeft, 216, "Nothing heard yet.", 1);
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
            DrawList(canvas, kIconDollar, "Buy", state.buy, state.buy_done,
                     12, state.selected[1],
                     "nothing to buy.");
            break;
        case HearthScreen::kMenu:
            DrawMenu(canvas, state);
            break;
        case HearthScreen::kNotes:
            DrawList(canvas, kIconNotes, "Notes", state.notes,
                     state.notes_done, 12, state.selected[3],
                     "no notes yet.");
            break;
        case HearthScreen::kPulse:
            DrawPulse(canvas, state);
            break;
        default:
            break;
    }
    DrawTabs(canvas, state);
    DrawStatus(canvas, state);
}
