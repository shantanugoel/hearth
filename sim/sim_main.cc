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
    std::snprintf(s.ssid, sizeof(s.ssid), "orbital-5-iot");
    std::snprintf(s.ip, sizeof(s.ip), "192.168.2.40");
    std::snprintf(s.wifi_status, sizeof(s.wifi_status), "orbital-5-iot  -48 dBm");
    std::snprintf(s.hub, sizeof(s.hub), "http://192.168.2.89:8790");
    std::snprintf(s.transcript, sizeof(s.transcript),
                  "we are out of oat milk");
    std::snprintf(s.note, sizeof(s.note), "HEARTH v0.5.0-kitchen");
    std::snprintf(s.date, sizeof(s.date), "Mon 14 Sep");
    std::snprintf(s.weather, sizeof(s.weather), "21C  drizzle  21-30");
    std::snprintf(s.wx, sizeof(s.wx), "rain");
    std::snprintf(s.meal, sizeof(s.meal), "pasta");
    std::snprintf(s.ack, sizeof(s.ack), "Added oat milk to Buy.");
    std::snprintf(s.n_buy, sizeof(s.n_buy), "4");
    std::snprintf(s.n_notes, sizeof(s.n_notes), "3");
    std::snprintf(s.buy[0], sizeof(s.buy[0]), "oat milk");
    std::snprintf(s.buy[1], sizeof(s.buy[1]), "eggs");
    std::snprintf(s.notes[0], sizeof(s.notes[0]), "swim kit  Maya  Thursday");
    std::snprintf(s.notes[1], sizeof(s.notes[1]), "leave bags by the door");
    std::snprintf(s.alarm, sizeof(s.alarm), "07:00 school");
    s.alarm_h = 7;
    s.alarm_m = 0;
    std::snprintf(s.menu[0], sizeof(s.menu[0]), "*Mon  pasta");
    std::snprintf(s.menu[1], sizeof(s.menu[1]), " Tue");
    std::snprintf(s.menu[2], sizeof(s.menu[2]), " Wed  pasta");
    std::snprintf(s.menu[3], sizeof(s.menu[3]), " Thu");
    std::snprintf(s.menu[4], sizeof(s.menu[4]), " Fri");
    std::snprintf(s.menu[5], sizeof(s.menu[5]), " Sat");
    std::snprintf(s.menu[6], sizeof(s.menu[6]), " Sun");
    return s;
}

HearthState MakeBuy() {
    HearthState s = MakeToday();
    s.screen = HearthScreen::kBuy;
    s.ack[0] = '\0';
    std::snprintf(s.buy[0], sizeof(s.buy[0]), "oat milk");
    std::snprintf(s.buy[1], sizeof(s.buy[1]), "eggs");
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
    std::snprintf(s.notes[0], sizeof(s.notes[0]), "swim kit  Maya  Thursday");
    std::snprintf(s.notes[1], sizeof(s.notes[1]), "leave bags by the door");
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
    s.voice = HearthVoice::kListening;
    std::snprintf(s.voice_status, sizeof(s.voice_status), "release to send");
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
    return 0;
}
