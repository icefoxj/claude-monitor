#include "monitor/screendiff.h"

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace monitor {

std::vector<DiffRect> changedRects(const uint16_t* cur, const uint16_t* prev, int width, int height,
                                   const DiffRect& region, int maxRows){
    std::vector<DiffRect> out;
    if (!cur || !prev || width <= 0 || height <= 0 || maxRows < 1) return out;
    const int x0 = std::max(region.x, 0);
    const int y0 = std::max(region.y, 0);
    const int x1 = std::min(region.x + region.w, width);
    const int y1 = std::min(region.y + region.h, height);
    if (x0 >= x1 || y0 >= y1) return out;

    const size_t span = static_cast<size_t>(x1 - x0) * sizeof(uint16_t);
    int top = -1, left = 0, right = 0;   // the run of differing rows being collected
    auto flush = [&](int bottom) {
        DiffRect r;
        r.x = left; r.y = top; r.w = right - left; r.h = bottom - top;
        out.push_back(r);
        top = -1;
    };
    for (int y = y0; y < y1; ++y){
        const uint16_t* a = cur  + static_cast<size_t>(y) * width;
        const uint16_t* b = prev + static_cast<size_t>(y) * width;
        if (std::memcmp(a + x0, b + x0, span) == 0){
            if (top >= 0) flush(y);
            continue;
        }
        int l = x0;
        while (a[l] == b[l]) ++l;
        int r = x1;
        while (a[r - 1] == b[r - 1]) --r;
        if (top < 0){
            top = y; left = l; right = r;
        } else {
            left  = std::min(left, l);
            right = std::max(right, r);
        }
        if (y + 1 - top >= maxRows) flush(y + 1);
    }
    if (top >= 0) flush(y1);
    return out;
}

}  // namespace monitor
