#include "monitor/screenshot.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace monitor {

namespace {

const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Standard base64 of n bytes into out (4 chars per 3 bytes, '=' padded);
// returns the number of characters written
int base64(const uint8_t* in, int n, char* out){
    int o = 0;
    for (int i = 0; i < n; i += 3){
        uint32_t v = static_cast<uint32_t>(in[i]) << 16;
        if (i + 1 < n) v |= static_cast<uint32_t>(in[i + 1]) << 8;
        if (i + 2 < n) v |= static_cast<uint32_t>(in[i + 2]);
        out[o++] = kAlphabet[(v >> 18) & 63];
        out[o++] = kAlphabet[(v >> 12) & 63];
        out[o++] = i + 1 < n ? kAlphabet[(v >> 6) & 63] : '=';
        out[o++] = i + 2 < n ? kAlphabet[v & 63] : '=';
    }
    return o;
}

}  // namespace

void dumpDisplay(lgfx::LGFXBase& display, void (*write)(const char* data, int n)){
    const int w = display.width();
    const int h = display.height();
    std::vector<uint8_t> row(static_cast<size_t>(w) * 3);
    std::vector<char> line(24 + ((w * 3 + 2) / 3) * 4);
    int n = snprintf(line.data(), line.size(), "SCREENSHOT w=%d h=%d fmt=rgb888\n", w, h);
    write(line.data(), n);
    for (int y = 0; y < h; y++){
        display.readRectRGB(0, y, w, 1, row.data());
        n = snprintf(line.data(), line.size(), "ROW %d ", y);
        n += base64(row.data(), w * 3, line.data() + n);
        line[n++] = '\n';
        write(line.data(), n);
    }
    write("END\n", 4);
}

}  // namespace monitor
