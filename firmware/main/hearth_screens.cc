#include "hearth_model.h"

#include <cstdio>

#include "hearth_canvas.h"

namespace {

constexpr int kMargin = 18;

void Footer(HearthCanvas& canvas, const HearthState& state) {
    canvas.HLine(kMargin, 268, HearthCanvas::kWidth - 2 * kMargin);
    char names[80];
    std::snprintf(names, sizeof(names), "Home  Buttons  Radio  Power");
    canvas.TextCentered(274, names, 1);
    // Underline the current screen name roughly by drawing a short bar
    // under the matching word. Positions measured for TRMNL16 at scale 1.
    const char* all = "Home  Buttons  Radio  Power";
    const int total = canvas.TextWidth(all, 1);
    const int start = (HearthCanvas::kWidth - total) / 2;
    int x = start;
    const char* words[] = {"Home", "Buttons", "Radio", "Power"};
    const int index = static_cast<int>(state.screen);
    for (int i = 0; i < 4; ++i) {
        const int w = canvas.TextWidth(words[i], 1);
        if (i == index) {
            canvas.FillRect(x, 292, w, 2, true);
        }
        x += w + canvas.TextWidth("  ", 1);
    }
}

void DrawHome(HearthCanvas& canvas, const HearthState& state) {
    canvas.Text(kMargin, 22, "HEARTH", 4);
    canvas.HLine(kMargin, 92, 220);
    canvas.Text(kMargin, 108, "household working memory", 1);
    canvas.Text(kMargin, 132, "bring-up  -  step 1", 1);

    char line[64];
    if (state.battery_valid) {
        std::snprintf(line, sizeof(line), "battery  %u%%   %u mV",
                      state.battery_percent, state.battery_mv);
    } else {
        std::snprintf(line, sizeof(line), "battery  unknown");
    }
    canvas.Text(kMargin, 176, line, 1);

    canvas.Text(kMargin, 198, state.wifi_status, 1);
    canvas.Text(kMargin, 230, "UP/DOWN  screens     hold DOWN  sleep", 1);
    if (state.note[0] != '\0') {
        canvas.Text(kMargin, 248, state.note, 1);
    }
}

void DrawButtons(HearthCanvas& canvas, const HearthState& state) {
    canvas.Text(kMargin, 18, "Buttons", 2);
    canvas.HLine(kMargin, 56, 140);
    canvas.Text(kMargin, 72, "last event", 1);
    canvas.Text(kMargin, 94, state.last_event, 2);

    char line[64];
    std::snprintf(line, sizeof(line), "UP clicks     %u", state.up_clicks);
    canvas.Text(kMargin, 148, line, 1);
    std::snprintf(line, sizeof(line), "DOWN clicks   %u", state.down_clicks);
    canvas.Text(kMargin, 172, line, 1);
    std::snprintf(line, sizeof(line), "OK clicks     %u", state.ok_clicks);
    canvas.Text(kMargin, 196, line, 1);
    canvas.Text(kMargin, 230, "OK click counts. Hold DOWN 3s sleeps.", 1);
}

void DrawRadio(HearthCanvas& canvas, const HearthState& state) {
    canvas.Text(kMargin, 18, "Radio", 2);
    canvas.HLine(kMargin, 56, 110);
    canvas.Text(kMargin, 72, state.wifi_status, 1);
    if (state.ap_count <= 0) {
        canvas.Text(kMargin, 120, "no access points yet", 1);
        canvas.Text(kMargin, 144, "OK rescan", 1);
        return;
    }
    for (int i = 0; i < state.ap_count; ++i) {
        char line[72];
        std::snprintf(line, sizeof(line), "%-24s  %4d dBm",
                      state.aps[i].ssid, state.aps[i].rssi);
        canvas.Text(kMargin, 100 + i * 22, line, 1);
    }
    canvas.Text(kMargin, 244, "OK rescan", 1);
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

}  // namespace

void HearthDraw(HearthCanvas& canvas, const HearthState& state) {
    canvas.Clear(true);
    switch (state.screen) {
        case HearthScreen::kHome:
            DrawHome(canvas, state);
            break;
        case HearthScreen::kButtons:
            DrawButtons(canvas, state);
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
}
