#ifndef HEARTH_CANVAS_H_
#define HEARTH_CANVAS_H_

#include <array>
#include <cstddef>
#include <cstdint>

class HearthCanvas {
public:
    static constexpr int kWidth = 400;
    static constexpr int kHeight = 300;
    static constexpr int kStride = kWidth / 8;
    static constexpr size_t kBytes = static_cast<size_t>(kStride * kHeight);

    void Clear(bool white = true);
    void Pixel(int x, int y, bool black);
    void FillRect(int x, int y, int width, int height, bool black);
    void Rect(int x, int y, int width, int height, bool black = true);
    void HLine(int x, int y, int width, bool black = true);
    void Line(int x0, int y0, int x1, int y1, bool black = true);
    void Text(int x, int y, const char* text, int scale = 1,
              bool inverted = false);
    void TextCentered(int y, const char* text, int scale = 1,
                      bool inverted = false);
    int TextWidth(const char* text, int scale = 1) const;

    uint8_t* data() { return pixels_.data(); }
    const uint8_t* data() const { return pixels_.data(); }
    size_t size() const { return pixels_.size(); }

private:
    std::array<uint8_t, kBytes> pixels_ = {};
};

#endif  // HEARTH_CANVAS_H_
