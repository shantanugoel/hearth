/* Host-side renderer for Hearth bring-up screens.
 *
 * Builds the same canvas + screen translation units as the firmware and
 * writes PNGs, so layout work is a sub-second loop instead of a flash cycle.
 */

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "hearth_canvas.h"
#include "hearth_model.h"

namespace {

uint32_t Crc32(const uint8_t* p, size_t n, uint32_t crc = 0xFFFFFFFFu) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            table[i] = c;
        }
        init = true;
    }
    for (size_t i = 0; i < n; ++i) {
        crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc;
}

void PutU32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x >> 24));
    v.push_back(static_cast<uint8_t>(x >> 16));
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x));
}

void Chunk(FILE* f, const char* type, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> len;
    PutU32(len, static_cast<uint32_t>(data.size()));
    fwrite(len.data(), 1, 4, f);
    std::vector<uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    fwrite(body.data(), 1, body.size(), f);
    std::vector<uint8_t> crc;
    PutU32(crc, Crc32(body.data(), body.size()) ^ 0xFFFFFFFFu);
    fwrite(crc.data(), 1, 4, f);
}

void WritePng(const char* path, const uint8_t* gray, int w, int h) {
    FILE* f = fopen(path, "wb");
    if (!f) {
        std::fprintf(stderr, "cannot open %s\n", path);
        return;
    }
    const uint8_t sig[8] = {137, 'P', 'N', 'G', 13, 10, 26, 10};
    fwrite(sig, 1, 8, f);

    std::vector<uint8_t> ihdr;
    PutU32(ihdr, static_cast<uint32_t>(w));
    PutU32(ihdr, static_cast<uint32_t>(h));
    ihdr.push_back(8);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    Chunk(f, "IHDR", ihdr);

    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(h) * (w + 1));
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), gray + static_cast<size_t>(y) * w,
                   gray + static_cast<size_t>(y) * w + w);
    }
    std::vector<uint8_t> z{0x78, 0x01};
    size_t off = 0;
    while (off < raw.size()) {
        const size_t n = std::min<size_t>(65535, raw.size() - off);
        z.push_back(off + n >= raw.size() ? 1 : 0);
        z.push_back(static_cast<uint8_t>(n & 0xFF));
        z.push_back(static_cast<uint8_t>(n >> 8));
        z.push_back(static_cast<uint8_t>(~n & 0xFF));
        z.push_back(static_cast<uint8_t>((~n >> 8) & 0xFF));
        z.insert(z.end(), raw.begin() + off, raw.begin() + off + n);
        off += n;
    }
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    PutU32(z, (b << 16) | a);
    Chunk(f, "IDAT", z);
    Chunk(f, "IEND", {});
    fclose(f);
}

void Save1bpp(const char* path, const HearthCanvas& c, int scale) {
    const int w = HearthCanvas::kWidth * scale;
    const int h = HearthCanvas::kHeight * scale;
    std::vector<uint8_t> img(static_cast<size_t>(w) * h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int sx = x / scale;
            const int sy = y / scale;
            const uint8_t byte =
                c.data()[static_cast<size_t>(sy) * HearthCanvas::kStride +
                         (sx >> 3)];
            const bool white = byte & (0x80u >> (sx & 7));
            img[static_cast<size_t>(y) * w + x] = white ? 0xF2 : 0x14;
        }
    }
    WritePng(path, img.data(), w, h);
}

HearthState MakeToday() {
    HearthState s;
    s.screen = HearthScreen::kToday;
    s.battery_valid = true;
    s.battery_mv = 3912;
    s.battery_percent = 76;
    s.charging = true;
    s.wifi_connected = true;
    s.rssi = -48;
    std::snprintf(s.ssid, sizeof(s.ssid), "Hearth-demo");
    std::snprintf(s.ip, sizeof(s.ip), "192.0.2.40");
    std::snprintf(s.wifi_status, sizeof(s.wifi_status), "Hearth-demo  -48 dBm");
    std::snprintf(s.hub, sizeof(s.hub), "http://hub.local:8790");
    std::snprintf(s.transcript, sizeof(s.transcript),
                  "add oat milk to the shopping list");
    std::snprintf(s.note, sizeof(s.note), "HEARTH v0.9.1-hearth");
    std::snprintf(s.date, sizeof(s.date), "Wed 16 Sep");
    std::snprintf(s.clock, sizeof(s.clock), "2026-09-16T09:42:00");
    std::snprintf(s.weather, sizeof(s.weather), "31C  partly cloudy  21-31");
    std::snprintf(s.wx, sizeof(s.wx), "partly");
    std::snprintf(s.meal, sizeof(s.meal), "Noodle soup");
    std::snprintf(s.ack, sizeof(s.ack), "Added oat milk to Buy.");
    std::snprintf(s.n_buy, sizeof(s.n_buy), "8");
    std::snprintf(s.n_notes, sizeof(s.n_notes), "8");
    constexpr const char* kBuy[] = {"Oat milk", "Eggs", "Bananas", "Rice", "Tomatoes",
                                    "Coffee beans", "Dish soap", "Fresh flowers", "Bread", "Olive oil"};
    for (int i = 0; i < 10; ++i)
        std::snprintf(s.buy[i], sizeof(s.buy[i]), "%s", kBuy[i]);
    s.buy_done[1] = true;
    s.buy_done[8] = true;
    std::snprintf(s.peek_buy[0], sizeof(s.peek_buy[0]), "Oat milk");
    std::snprintf(s.peek_buy[1], sizeof(s.peek_buy[1]), "Bananas");
    std::snprintf(s.peek_menu[0], sizeof(s.peek_menu[0]), "Breakfast: Oats and banana");
    std::snprintf(s.peek_menu[1], sizeof(s.peek_menu[1]), "Lunch: Chickpea bowls");
    constexpr const char* kNotes[] = {
        "Swim kit  Maya  Thursday", "Call school  Arun  Today", "Leave bags by the door",
        "Water the plants  Saturday", "Library books  Maya  Friday",
        "Grandma visits on Sunday  Sunday", "Book dentist appointment  Arun  Next week",
        "Rain jackets  Tomorrow", "Spare key is in the drawer", "Put recycling out  Wednesday"};
    for (int i = 0; i < 10; ++i)
        std::snprintf(s.notes[i], sizeof(s.notes[i]), "%s", kNotes[i]);
    s.notes_done[2] = true;
    s.notes_done[9] = true;
    std::snprintf(s.alarm, sizeof(s.alarm), "07:00 School morning");
    s.alarm_h = 7;
    s.alarm_m = 0;
    constexpr const char* kDays[] = {"mon", "tue", "wed", "thu", "fri", "sat", "sun"};
    constexpr const char* kMeals[][3] = {
        {"Yogurt and berries", "Dal rice", "Pasta and salad"},
        {"Toast and eggs", "Veg wraps", "Paneer curry"},
        {"Oats and banana", "Chickpea bowls", "Noodle soup"},
        {"Idli and chutney", "Lemon rice", "Roast vegetables"},
        {"Fruit and granola", "Sandwiches", "Pizza night"},
        {"Pancakes", "Leftover bowls", "Tacos"},
        {"Poha", "Family lunch", "Khichdi"}};
    constexpr const char* kSlots[] = {"breakfast", "lunch", "dinner"};
    constexpr const char* kLabels[] = {"Breakfast", "Lunch", "Dinner"};
    for (int day = 0; day < 7; ++day)
        for (int slot = 0; slot < 3; ++slot) {
            const int i = day * 3 + slot;
            std::snprintf(s.menu[i], sizeof(s.menu[i]), "%s: %s", kLabels[slot], kMeals[day][slot]);
            std::snprintf(s.menu_id[i], sizeof(s.menu_id[i]), "%s/%s", kDays[day], kSlots[slot]);
        }
    return s;
}

HearthState MakeBuy() {
    HearthState s = MakeToday();
    s.screen = HearthScreen::kBuy;
    s.ack[0] = '\0';
    return s;
}

HearthState MakeMenu() {
    HearthState s = MakeToday();
    s.screen = HearthScreen::kMenu;
    s.ack[0] = '\0';
    return s;
}

HearthState MakeNotes() {
    HearthState s = MakeToday();
    s.screen = HearthScreen::kNotes;
    s.ack[0] = '\0';
    return s;
}

HearthState MakePulse() {
    HearthState s = MakeToday();
    s.screen = HearthScreen::kPulse;
    s.ack[0] = '\0';
    return s;
}

HearthState MakeListen() {
    HearthState s = MakeToday();
    s.ack[0] = '\0';
    s.voice = HearthVoice::kListening;
    std::snprintf(s.voice_status, sizeof(s.voice_status), "listening");
    return s;
}

HearthState MakeFiling() {
    HearthState s = MakeToday();
    s.ack[0] = '\0';
    s.voice = HearthVoice::kFiling;
    std::snprintf(s.voice_status, sizeof(s.voice_status), "filing");
    return s;
}

HearthState MakeRemoved() {
    HearthState s = MakeBuy();
    std::snprintf(s.n_buy, sizeof(s.n_buy), "7");
    for (int i = 0; i < 9; ++i) {
        std::memcpy(s.buy[i], s.buy[i + 1], sizeof(s.buy[i]));
        s.buy_done[i] = s.buy_done[i + 1];
    }
    s.buy[9][0] = '\0';
    std::snprintf(s.ack, sizeof(s.ack), "Removed Oat milk.");
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string out = argc > 1 ? argv[1] : "sim/out";
    const int scale = argc > 2 ? std::atoi(argv[2]) : 2;

    static HearthCanvas canvas;
    auto shot = [&](const char* name, const HearthState& state) {
        const std::string path = out + "/" + name + ".png";
        HearthDraw(canvas, state);
        Save1bpp(path.c_str(), canvas, scale);
        std::printf("  %s\n", path.c_str());
    };

    shot("01-today", MakeToday());
    shot("02-buy", MakeBuy());
    shot("03-menu", MakeMenu());
    shot("04-notes", MakeNotes());
    shot("05-pulse", MakePulse());
    shot("06-listen", MakeListen());
    shot("07-filing", MakeFiling());
    shot("08-removed", MakeRemoved());
    return 0;
}
