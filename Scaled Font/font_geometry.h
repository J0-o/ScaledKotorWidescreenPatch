#pragma once

#include <cstddef>
#include <cstdint>

namespace FontGeometry {
struct Quad { float left, right, top, bottom; };
struct Vertex {
    float x, y, z;
    std::uint32_t color;
    float u, v;
};
static_assert(sizeof(Vertex) == 24, "KOTOR TextOutA vertex stride changed");
static_assert(offsetof(Vertex, u) == 16, "KOTOR TextOutA UV offset changed");

inline void expandQuad(Quad& q, float dx, float dy) {
    q.left -= dx;
    q.right += dx;
    q.top += dy;
    q.bottom -= dy;
}

inline void expandGlyph(Vertex* vertices, float distance, float uv) {
    // TextOutA writes BL, BR, TL, TL, BR, TR for every glyph.
    static constexpr float sx[6] = {-1, 1, -1, -1, 1, 1};
    static constexpr float sy[6] = {-1, -1, 1, 1, -1, 1};
    for (int i = 0; i < 6; ++i) {
        vertices[i].x += sx[i] * distance;
        vertices[i].y += sy[i] * distance;
        vertices[i].u += sx[i] * uv;
        vertices[i].v += sy[i] * uv;
    }
}
} // namespace FontGeometry
