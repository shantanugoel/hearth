#include "hearth_canvas.h"

#include <algorithm>
#include <cstdlib>

#include "zectrix_ascii_font_8x16.h"

void HearthCanvas::Clear(bool white) {
    pixels_.fill(white ? 0xff : 0x00);
}

void HearthCanvas::Pixel(int x, int y, bool black) {
    if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) {
        return;
    }
    uint8_t& byte = pixels_[static_cast<size_t>(y) * kStride + x / 8];
    const uint8_t mask = static_cast<uint8_t>(1U << (7 - (x & 7)));
    if (black) {
        byte &= static_cast<uint8_t>(~mask);
    } else {
        byte |= mask;
    }
}

void HearthCanvas::FillRect(int x, int y, int width, int height, bool black) {
    const int left = std::max(0, x);
    const int top = std::max(0, y);
    const int right = std::min(kWidth, x + width);
    const int bottom = std::min(kHeight, y + height);
    for (int py = top; py < bottom; ++py) {
        for (int px = left; px < right; ++px) {
            Pixel(px, py, black);
        }
    }
}

void HearthCanvas::Rect(int x, int y, int width, int height, bool black) {
    Line(x, y, x + width - 1, y, black);
    Line(x, y + height - 1, x + width - 1, y + height - 1, black);
    Line(x, y, x, y + height - 1, black);
    Line(x + width - 1, y, x + width - 1, y + height - 1, black);
}

void HearthCanvas::HLine(int x, int y, int width, bool black) {
    Line(x, y, x + width - 1, y, black);
}

void HearthCanvas::Line(int x0, int y0, int x1, int y1, bool black) {
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    while (true) {
        Pixel(x0, y0, black);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int twice = error * 2;
        if (twice >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

void HearthCanvas::Text(int x, int y, const char* text, int scale,
                        bool inverted) {
    if (text == nullptr || scale <= 0) {
        return;
    }
    int cursor_x = x;
    for (const unsigned char* cursor =
             reinterpret_cast<const unsigned char*>(text);
         *cursor != '\0'; ++cursor) {
        unsigned char code = *cursor;
        if (code < 0x20 || code > 0x7e) {
            code = '?';
        }
        const size_t glyph_index = code - 0x20;
        const uint16_t* glyph = kZectrixAsciiFont8x16[glyph_index];
        const int glyph_width = kZectrixAsciiFontWidths[glyph_index];
        if (inverted) {
            FillRect(cursor_x, y, glyph_width * scale, 16 * scale, true);
        }
        for (int row = 0; row < 16; ++row) {
            for (int column = 0; column < glyph_width; ++column) {
                const bool set = (glyph[row] & (1U << (15 - column))) != 0;
                if (!set && !inverted) {
                    continue;
                }
                FillRect(cursor_x + column * scale, y + row * scale, scale,
                         scale, inverted ? !set : true);
            }
        }
        cursor_x += glyph_width * scale;
    }
}

void HearthCanvas::TextCentered(int y, const char* text, int scale,
                                bool inverted) {
    Text((kWidth - TextWidth(text, scale)) / 2, y, text, scale, inverted);
}

void HearthCanvas::Icon16(int x, int y, const uint16_t rows[16],
                           bool inverted) {
    if (rows == nullptr) {
        return;
    }
    for (int row = 0; row < 16; ++row) {
        for (int col = 0; col < 16; ++col) {
            const bool set = (rows[row] & static_cast<uint16_t>(0x8000u >> col)) != 0;
            if (inverted) {
                Pixel(x + col, y + row, !set);
            } else if (set) {
                Pixel(x + col, y + row, true);
            }
        }
    }
}

int HearthCanvas::TextWidth(const char* text, int scale) const {
    if (text == nullptr || scale <= 0) {
        return 0;
    }
    int width = 0;
    for (const unsigned char* cursor =
             reinterpret_cast<const unsigned char*>(text);
         *cursor != '\0'; ++cursor) {
        unsigned char code = *cursor;
        if (code < 0x20 || code > 0x7e) {
            code = '?';
        }
        width += kZectrixAsciiFontWidths[code - 0x20] * scale;
    }
    return width;
}
