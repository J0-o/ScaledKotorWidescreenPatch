#include "system_font_atlas.h"
#include "font_metrics.h"
#include "font_refresh.h"
#include "GameAPI/GameVersion.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <GL/gl.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <limits>
#include <map>
#include <string>
#include <vector>

#pragma comment(lib, "gdi32.lib")

namespace SystemFontAtlas {
namespace {

constexpr int AtlasEmPixels = 64;
constexpr int PixelRange = 8;
constexpr int CodePage = 1252;
constexpr float UiFontHeight = 16.0f / 100.0f;
constexpr float DescriptionFontHeight = 16.0f / 100.0f;
constexpr int LogicalFontHeightPixels = 16;
constexpr int ExtraVerticalCropPixels = 2;
constexpr float UiHorizontalCropPixels = 1.0f;
constexpr float WrapSafetyPixels = 0.5f;

struct Vec3 { float x, y, z; };
struct PointF { float x, y; };
struct Edge { PointF a, b; int mask; };
struct Glyph {
    int x, y, w, h, advance;
    int inkTop = 0;
    int inkBottom = 0;
    bool hasInk = false;
    std::vector<Edge> edges;
};

enum class GenerationState : unsigned char { Unattempted, Failed, Ready };

struct Atlas {
    Atlas(const wchar_t* fileName, const wchar_t* faceName, int faceWeight)
        : relativeFile(fileName), family(faceName), weight(faceWeight) {}

    const wchar_t* relativeFile;
    const wchar_t* family;
    int weight;
    GenerationState generation = GenerationState::Unattempted;
    int side = 0;
    int lineHeight = 0;
    int ascent = 0;
    int padding;
    std::vector<unsigned char> rgb;
    Vec3 upper[256] = {}, lower[256] = {};
    GLuint glTexture = 0;
};

Atlas g_atlases[2] = {
    Atlas(L"Override\\ui.ttf", L"Squarish Sans CT", 400),
    Atlas(L"Override\\body.ttf", L"Arimo", 700)
};

struct FontState {
    Atlas* atlas = nullptr;
    FontMetrics::Original original;
    Vec3* upper = nullptr;
    Vec3* lower = nullptr;
    unsigned int glyphCount = 0;
};

std::map<void*, FontState> g_fonts;

Atlas* atlasForResource(const char* name) {
    if (!_stricmp(name, "dialogfont10x10") ||
        !_stricmp(name, "dialogfont10x10a") ||
        !_stricmp(name, "dialogfont10x10b") ||
        !_stricmp(name, "dialogfont12x16") ||
        !_stricmp(name, "dialogfont12x16a") ||
        !_stricmp(name, "dialogfont12x16b") ||
        !_stricmp(name, "dialogfont16x16") ||
        !_stricmp(name, "dialogfont16x16a") ||
        !_stricmp(name, "dialogfont16x16b") ||
        !_stricmp(name, "dialogfont32x32") ||
        !_stricmp(name, "dialogfont32x32a") ||
        !_stricmp(name, "dialogfont32x32b")) return &g_atlases[0];
    if (!_stricmp(name, "fnt_d16x16") ||
        !_stricmp(name, "fnt_d16x16a") ||
        !_stricmp(name, "fnt_d16x16b")) return &g_atlases[1];
    return nullptr;
}

FontState* fontState(void* fontInfo) {
    const auto it = g_fonts.find(fontInfo);
    return it == g_fonts.end() ? nullptr : &it->second;
}


DWORD g_numChars=0,g_fontHeight=0,g_baseline=0,g_textureWidth=0;
DWORD g_spacingR=0,g_spacingB=0,g_upper=0,g_lower=0,g_textureFontInfo=0,g_textureName=0;
bool g_offsetsResolved=false;

bool resolveOffsets() {
    if (g_offsetsResolved) return true;
    if (!GameVersion::IsInitialized() && !GameVersion::Initialize()) return false;
    try {
        g_numChars=(DWORD)GameVersion::GetOffset("CAurFontInfo","numchars");
        g_fontHeight=(DWORD)GameVersion::GetOffset("CAurFontInfo","fontheight");
        g_baseline=(DWORD)GameVersion::GetOffset("CAurFontInfo","baselineheight");
        g_textureWidth=(DWORD)GameVersion::GetOffset("CAurFontInfo","texturewidth");
        g_spacingR=(DWORD)GameVersion::GetOffset("CAurFontInfo","spacingR");
        g_spacingB=(DWORD)GameVersion::GetOffset("CAurFontInfo","spacingB");
        g_upper=(DWORD)GameVersion::GetOffset("CAurFontInfo","upperleftcoords");
        g_lower=(DWORD)GameVersion::GetOffset("CAurFontInfo","lowerrightcoords");
        g_textureFontInfo=(DWORD)GameVersion::GetOffset("CAurTextureBasic","font_info");
        g_textureName=(DWORD)GameVersion::GetOffset("CAurTextureBasic","name");
    } catch(const GameVersionException&) { return false; }
    g_offsetsResolved=true;
    return true;
}

bool gameFilePath(const wchar_t* relative, std::wstring& path) {
    wchar_t executable[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, executable, MAX_PATH);
    if (!length || length >= MAX_PATH) return false;
    wchar_t* slash = std::wcsrchr(executable, L'\\');
    if (!slash) return false;
    slash[1] = L'\0';
    path.assign(executable);
    path.append(relative);
    return true;
}

WCHAR byteToWide(unsigned char b) {
    char c=(char)b; WCHAR w=0;
    if(MultiByteToWideChar(CodePage,0,&c,1,&w,1)!=1 || w<0x20) return L'?';
    return w;
}

WCHAR existingGlyphOrFallback(HDC dc, WCHAR ch) {
    WORD glyph = 0xFFFF;
    const DWORD MissingGlyph = 0x0001; // GGI_MARK_NONEXISTING_GLYPHS
    if (GetGlyphIndicesW(dc, &ch, 1, &glyph, MissingGlyph) == GDI_ERROR ||
        glyph == 0xFFFF) return L'?';
    return ch;
}

float fixedToFloat(const FIXED& v) {
    return (float)v.value + (float)v.fract / 65536.0f;
}

PointF pointFx(const POINTFX& p) {
    return { fixedToFloat(p.x), fixedToFloat(p.y) };
}

int edgeMask(const PointF& a, const PointF& b) {
    double angle=std::atan2((double)(b.y-a.y),(double)(b.x-a.x));
    if(angle<0) angle+=3.14159265358979323846;
    if(angle>=3.14159265358979323846) angle-=3.14159265358979323846;
    int sector=std::min(2,(int)(angle/(3.14159265358979323846/3.0)));
    return sector==0 ? 0x3 : (sector==1 ? 0x6 : 0x5);
}

void addEdge(std::vector<Edge>& edges, PointF a, PointF b) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    if (dx * dx + dy * dy < 0.0001f) return;
    edges.push_back({a, b, 0});
}

constexpr int MaximumAtlasSide = 4096;
constexpr DWORD MaximumOutlineBytes = 1024 * 1024;
constexpr size_t MaximumGlyphEdges = 8192;

// Bound quadratic-to-polyline geometry error in atlas pixels, rather than
// assuming eight chords fit every curve/AtlasEmSize. For a quadratic Bezier,
// its maximum displacement from the parameter-matched endpoint chord is
// |2*control-start-end|/4. De Casteljau splitting reduces this bound by 4.
constexpr double MaximumQuadraticError = 1.0 / 32.0;
constexpr int MaximumQuadraticDepth = 16;

bool appendQuadratic(std::vector<Edge>& edges, const PointF& start,
        const PointF& control, const PointF& finish, int depth = 0) {
    const double dx = 2.0 * control.x - start.x - finish.x;
    const double dy = 2.0 * control.y - start.y - finish.y;
    const double limit = 4.0 * MaximumQuadraticError;
    if (dx * dx + dy * dy <= limit * limit) {
        // Unlike the legacy straight-line helper, do not discard a run of
        // short but nonzero curve chords after computing the error bound.
        if (start.x != finish.x || start.y != finish.y) {
            if (edges.size() >= MaximumGlyphEdges) return false;
            edges.push_back({start, finish, 0});
        }
        return true;
    }
    if (depth >= MaximumQuadraticDepth || edges.size() >= MaximumGlyphEdges)
        return false; // Reject pathological outlines instead of unbounded work.
    const PointF a{(start.x + control.x) * 0.5f, (start.y + control.y) * 0.5f};
    const PointF b{(control.x + finish.x) * 0.5f, (control.y + finish.y) * 0.5f};
    const PointF mid{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
    return appendQuadratic(edges, start, a, mid, depth + 1) &&
        appendQuadratic(edges, mid, b, finish, depth + 1);
}

// These objects must be released even if vector allocation or outline parsing
// throws. Select the original object back before deleting the selected HFONT.
struct SelectedSystemFont {
    HDC dc = nullptr;
    HFONT font = nullptr;
    HGDIOBJ previous = nullptr;
    std::wstring registeredFile;
    bool registered = false;

    SelectedSystemFont() = default;
    SelectedSystemFont(const SelectedSystemFont&) = delete;
    SelectedSystemFont& operator=(const SelectedSystemFont&) = delete;
    ~SelectedSystemFont() {
        if (dc && previous && previous != HGDI_ERROR) SelectObject(dc, previous);
        if (font) DeleteObject(font);
        if (dc) DeleteDC(dc);
        if (registered) RemoveFontResourceExW(registeredFile.c_str(), FR_PRIVATE, nullptr);
    }

    bool open(const Atlas& atlas) {
        if (!gameFilePath(atlas.relativeFile, registeredFile) ||
            !AddFontResourceExW(registeredFile.c_str(), FR_PRIVATE, nullptr)) return false;
        registered = true;
        dc = CreateCompatibleDC(nullptr);
        if (!dc) return false;
        font = CreateFontW(-AtlasEmPixels, 0, 0, 0, atlas.weight, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
            ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, atlas.family);
        if (!font) return false;
        previous = SelectObject(dc, font);
        return previous && previous != HGDI_ERROR;
    }
};

int requiredInkPadding(const GLYPHMETRICS& gm, int lineHeight, int ascent) {
    const int advance = std::max<LONG>(1, gm.gmCellIncX);
    const int64_t inkLeft = gm.gmptGlyphOrigin.x;
    const int64_t inkRight = inkLeft + static_cast<int64_t>(gm.gmBlackBoxX);
    const int64_t inkTop = gm.gmptGlyphOrigin.y;
    const int64_t inkBottom = inkTop - static_cast<int64_t>(gm.gmBlackBoxY);
    const int64_t logicalBottom = static_cast<int64_t>(ascent) - lineHeight;
    const int64_t needed = std::max<int64_t>(0, std::max(
        std::max(-inkLeft, inkRight - advance),
        std::max(inkTop - ascent, logicalBottom - inkBottom)));
    return needed <= MaximumAtlasSide && advance <= MaximumAtlasSide ?
        static_cast<int>(needed) : -1;
}

bool extractGlyph(HDC dc, WCHAR ch, Glyph& out, int lineHeight, int ascent,
                  int& inkPadding) {
    ch = existingGlyphOrFallback(dc, ch);
    MAT2 mat = {{0,1},{0,0},{0,0},{0,1}};
    GLYPHMETRICS gm = {};
    const DWORD nativeBytes = GetGlyphOutlineW(dc, ch, GGO_NATIVE, &gm, 0, nullptr, &mat);
    if (nativeBytes == GDI_ERROR || nativeBytes > MaximumOutlineBytes) return false;

    out.advance = std::max<LONG>(1, gm.gmCellIncX);
    inkPadding = requiredInkPadding(gm, lineHeight, ascent);
    if (inkPadding < 0) return false;

    if (nativeBytes == 0) return true; // Space and other non-ink glyphs.

    out.inkTop = gm.gmptGlyphOrigin.y;
    out.inkBottom = out.inkTop - static_cast<int>(gm.gmBlackBoxY);
    out.hasInk = true;

    std::vector<unsigned char> native(nativeBytes);
    // The first call only measured the buffer. A failed or short second call
    // must not be accepted as an empty glyph.
    if (GetGlyphOutlineW(dc, ch, GGO_NATIVE, &gm, nativeBytes, native.data(), &mat)
            != nativeBytes) return false;

    size_t pos = 0;
    while (pos < native.size()) {
        if (native.size() - pos < sizeof(TTPOLYGONHEADER)) return false;
        TTPOLYGONHEADER header;
        memcpy(&header, native.data() + pos, sizeof(header));
        if (header.dwType != TT_POLYGON_TYPE || header.cb <= sizeof(header) ||
            header.cb > native.size() - pos) return false;
        const size_t end = pos + header.cb;
        PointF current = pointFx(header.pfxStart), contourStart = current;
        size_t cp = pos + sizeof(header);
        while (cp < end) {
            if (end - cp < sizeof(WORD) * 2) return false;
            WORD type = 0, count = 0;
            memcpy(&type, native.data() + cp, sizeof(type));
            memcpy(&count, native.data() + cp + sizeof(type), sizeof(count));
            const size_t bytes = sizeof(WORD) * 2 +
                static_cast<size_t>(count) * sizeof(POINTFX);
            if (count == 0 || bytes > end - cp) return false;
            auto point = [&](WORD i) {
                POINTFX result;
                memcpy(&result, native.data() + cp + sizeof(WORD) * 2 +
                    static_cast<size_t>(i) * sizeof(POINTFX), sizeof(result));
                return pointFx(result);
            };
            if (type == TT_PRIM_LINE) {
                for (WORD i = 0; i < count; ++i) {
                    const PointF next = point(i);
                    addEdge(out.edges, current, next);
                    current = next;
                }
            } else if (type == TT_PRIM_QSPLINE && count >= 2) {
                for (WORD i = 0; i < count - 1; ++i) {
                    const PointF control = point(i), next = point(i + 1);
                    const PointF finish = i == count - 2 ? next :
                        PointF{(control.x + next.x) * 0.5f,
                               (control.y + next.y) * 0.5f};
                    if (!appendQuadratic(out.edges, current, control, finish))
                        return false;
                    current = finish;
                }
            } else {
                return false;
            }
            if (out.edges.size() > MaximumGlyphEdges) return false;
            cp += bytes;
        }
        addEdge(out.edges, current, contourStart);
        pos = end;
    }
    return !out.edges.empty() && out.edges.size() <= MaximumGlyphEdges;
}

bool finalizeGlyph(Glyph& glyph, int lineHeight, int ascent, int padding) {
    if (glyph.advance > MaximumAtlasSide - 2 * padding ||
        lineHeight > MaximumAtlasSide - 2 * padding) return false;
    glyph.w = glyph.advance + 2 * padding;
    glyph.h = lineHeight + 2 * padding;
    for (Edge& edge : glyph.edges) {
        edge.a.x += static_cast<float>(padding);
        edge.b.x += static_cast<float>(padding);
        edge.a.y = static_cast<float>(padding + ascent) - edge.a.y;
        edge.b.y = static_cast<float>(padding + ascent) - edge.b.y;
        edge.mask = edgeMask(edge.a, edge.b);
    }
    return true;
}

bool insideByWinding(float px,float py,const std::vector<Edge>& edges) {
    int winding = 0;
    for(const Edge& e : edges) {
        const float cross = (e.b.x-e.a.x)*(py-e.a.y) - (px-e.a.x)*(e.b.y-e.a.y);
        if(e.a.y <= py) {
            if(e.b.y > py && cross > 0.0f) ++winding;
        } else {
            if(e.b.y <= py && cross < 0.0f) --winding;
        }
    }
    return winding != 0;
}

float segmentDistanceSq(float px,float py,const Edge& e) {
    float vx=e.b.x-e.a.x,vy=e.b.y-e.a.y;
    float wx=px-e.a.x,wy=py-e.a.y;
    float vv=vx*vx+vy*vy;
    float t=vv>0.0f?(wx*vx+wy*vy)/vv:0.0f;
    t=std::max(0.0f,std::min(1.0f,t));
    float dx=px-(e.a.x+t*vx),dy=py-(e.a.y+t*vy);
    return dx*dx+dy*dy;
}

void makeDistanceField(const Glyph& g, unsigned char* dst, int stride) {
    if(g.edges.empty()) return;
    const size_t count=(size_t)g.w*g.h;
    std::vector<float> best(count*3,1e30f);
    const float radius=(float)PixelRange+1.5f;
    for(const Edge& e:g.edges) {
        int x0=std::max(0,(int)std::floor(std::min(e.a.x,e.b.x)-radius));
        int x1=std::min(g.w-1,(int)std::ceil(std::max(e.a.x,e.b.x)+radius));
        int y0=std::max(0,(int)std::floor(std::min(e.a.y,e.b.y)-radius));
        int y1=std::min(g.h-1,(int)std::ceil(std::max(e.a.y,e.b.y)+radius));
        for(int y=y0;y<=y1;++y) for(int x=x0;x<=x1;++x) {
            float d=segmentDistanceSq(x+0.5f,y+0.5f,e);
            if(d>radius*radius) continue;
            const size_t pixel=(size_t)y*g.w+x;
            size_t base=pixel*3;
            for(int c=0;c<3;++c) if(e.mask&(1<<c)) best[base+c]=std::min(best[base+c],d);
        }
    }
    for(int y=0;y<g.h;++y) for(int x=0;x<g.w;++x) {
        bool inside=insideByWinding(x+0.5f,y+0.5f,g.edges);
        float sign=inside?1.0f:-1.0f; unsigned char* p=dst+y*stride+x*3;
        const size_t pixel=(size_t)y*g.w+x;
        size_t base=pixel*3;
        float channel[3] = {};
        for(int c=0;c<3;++c) {
            float v;
            if(best[base+c]>=1e29f) v=inside?1.0f:0.0f;
            else v=0.5f+sign*std::sqrt(best[base+c])/(float)PixelRange;
            channel[c]=std::max(0.0f,std::min(1.0f,v));
        }
        // This compact generator uses a common whole-shape sign, not the
        // reference MSDF algorithm's per-edge signed/perpendicular distances.
        // Each edge occupies two channels, so their median already equals
        // the nearest-edge SDF at texel centres. A median-vs-true-SDF check
        // here cannot correct corner interpolation and was removed as dead
        // code. Contour-aware coloring/distance reconstruction remains work
        // for the generator, not something the fragment shader can invent.
        for(int c=0;c<3;++c) p[c]=(unsigned char)std::lround(channel[c]*255.0f);
    }
}

int nextPow2(int n){int p=1;while(p<n)p<<=1;return p;}

bool generateImpl(Atlas& atlas) {
    Glyph glyphs[256];
    int maxW = 0;
    size_t totalArea = 0;
    {
        SelectedSystemFont selected;
        if (!selected.open(atlas)) return false;
        TEXTMETRICW metrics = {};
        if (!GetTextMetricsW(selected.dc, &metrics) || metrics.tmHeight <= 0 ||
            metrics.tmHeight > MaximumAtlasSide || metrics.tmAscent < 0 ||
            metrics.tmAscent > metrics.tmHeight) return false;
        atlas.lineHeight = metrics.tmHeight;
        atlas.ascent = metrics.tmAscent;
        int bearingPadding = 0;
        for (int i = 0; i < 256; ++i) {
            int glyphPadding = 0;
            if (!extractGlyph(selected.dc, byteToWide(static_cast<unsigned char>(i)),
                    glyphs[i], atlas.lineHeight, atlas.ascent, glyphPadding)) return false;
            bearingPadding = std::max(bearingPadding, glyphPadding);
        }
        int inkTop = std::numeric_limits<int>::min();
        int inkBottom = std::numeric_limits<int>::max();
        for (const Glyph& glyph : glyphs) {
            if (!glyph.hasInk) continue;
            inkTop = std::max(inkTop, glyph.inkTop);
            inkBottom = std::min(inkBottom, glyph.inkBottom);
        }
        if (inkTop == std::numeric_limits<int>::min() ||
            inkBottom == std::numeric_limits<int>::max() ||
            inkTop <= inkBottom) return false;
        const int inkHeight = inkTop - inkBottom;
        const int croppedHeight = std::max(1,
            (inkHeight * (LogicalFontHeightPixels - ExtraVerticalCropPixels) +
             LogicalFontHeightPixels / 2) / LogicalFontHeightPixels);
        const int croppedPixels = inkHeight - croppedHeight;
        atlas.ascent = inkTop - croppedPixels / 2;
        atlas.lineHeight = croppedHeight;
        if (atlas.lineHeight > MaximumAtlasSide) return false;
        atlas.padding = std::max(PixelRange + 2, bearingPadding + PixelRange + 1);
        if (atlas.padding >= MaximumAtlasSide / 2) return false;
        for (Glyph& glyph : glyphs) {
            if (!finalizeGlyph(glyph, atlas.lineHeight, atlas.ascent, atlas.padding))
                return false;
            maxW = std::max(maxW, glyph.w);
            totalArea += static_cast<size_t>(glyph.w) * glyph.h;
            if (totalArea > static_cast<size_t>(MaximumAtlasSide) * MaximumAtlasSide)
                return false;
        }
    } // Release all GDI handles before the CPU distance-field work.
    const int estimate = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(totalArea)) * 1.18));
    int side = nextPow2(std::max(256, std::max(maxW, std::min(MaximumAtlasSide, estimate))));
    for (;;) {
        int x = 0, y = 0, row = 0;
        bool fit = true;
        for (Glyph& glyph : glyphs) {
            if (x + glyph.w > side) { x = 0; y += row; row = 0; }
            if (y + glyph.h > side) { fit = false; break; }
            glyph.x = x; glyph.y = y;
            x += glyph.w; row = std::max(row, glyph.h);
        }
        if (fit) break;
        if (side >= MaximumAtlasSide) return false;
        side *= 2;
    }
    atlas.side = side;
    atlas.rgb.assign(static_cast<size_t>(side) * side * 3, 0);
    const float horizontalCrop = &atlas == &g_atlases[0] ?
        UiHorizontalCropPixels * atlas.lineHeight / LogicalFontHeightPixels : 0.0f;
    for (int i = 0; i < 256; ++i) {
        Glyph& g = glyphs[i];
        if (g.advance <= horizontalCrop * 2.0f) return false;
        unsigned char* dst = atlas.rgb.data() + ((size_t)g.y * side + g.x) * 3;
        makeDistanceField(g, dst, side * 3);
        // KOTOR measures these rectangles. Publish ONLY the logical advance /
        // line cell, not its MSDF border. FontDraw expands UVs and the emitted
        // planes together, after layout, so bearings/overhang are still drawn.
        atlas.upper[i] = {(g.x + atlas.padding + horizontalCrop) / side,
                   1.0f - (float)(g.y + atlas.padding) / side, 0};
        atlas.lower[i] = {(g.x + atlas.padding + g.advance - horizontalCrop) / side,
                   1.0f - (float)(g.y + atlas.padding + atlas.lineHeight) / side, 0};
    }
    // The glyph packer writes top-down CPU rows, while the KOTOR UV tables
    // above use bottom-origin v coordinates (1 - y / height). glTexImage2D
    // does not flip incoming rows: byte row 0 becomes texture v == 0.
    // Reverse the completed image ONCE before upload so those existing UVs
    // address the glyphs, not the unused opposite half of the atlas. Keep
    // upper.v > lower.v: KOTOR also uses that difference for line geometry.
    const size_t rowBytes = static_cast<size_t>(atlas.side) * 3;
    for (int top = 0, bottom = atlas.side - 1; top < bottom; ++top, --bottom) {
        unsigned char* first = atlas.rgb.data() + static_cast<size_t>(top) * rowBytes;
        unsigned char* opposite = atlas.rgb.data() + static_cast<size_t>(bottom) * rowBytes;
        std::swap_ranges(first, first + rowBytes, opposite);
    }
    return true;
}

bool generate(Atlas& atlas) {
    if (atlas.generation == GenerationState::Ready) return true;
    if (atlas.generation == GenerationState::Failed) return false;
    atlas.generation = generateImpl(atlas) ? GenerationState::Ready : GenerationState::Failed;
    if (atlas.generation == GenerationState::Failed) atlas.rgb.clear();
    return atlas.generation == GenerationState::Ready;
}

FontState readFontState(void* fontInfo, Atlas& atlas) {
    char* font = static_cast<char*>(fontInfo);
    FontState state;
    state.atlas = &atlas;
    state.original = {
        *reinterpret_cast<float*>(font + g_fontHeight),
        *reinterpret_cast<float*>(font + g_spacingR),
        *reinterpret_cast<float*>(font + g_spacingB)
    };
    state.upper = *reinterpret_cast<Vec3**>(font + g_upper);
    state.lower = *reinterpret_cast<Vec3**>(font + g_lower);
    state.glyphCount = static_cast<unsigned int>(
        *reinterpret_cast<int*>(font + g_numChars));
    return state;
}

void writeMetrics(void* fontInfo, const FontMetrics::Values& metrics) {
    char* font = static_cast<char*>(fontInfo);
    *reinterpret_cast<float*>(font + g_fontHeight) = metrics.fontHeight;
    *reinterpret_cast<float*>(font + g_baseline) = metrics.baseline;
    *reinterpret_cast<float*>(font + g_textureWidth) = metrics.textureWidth;
    *reinterpret_cast<float*>(font + g_spacingR) = metrics.spacingR;
    *reinterpret_cast<float*>(font + g_spacingB) = metrics.spacingB;
}

FontMetrics::Values deriveMetrics(const FontState& state, float absoluteScale) {
    FontMetrics::Values metrics = FontMetrics::derive(state.original,
        {state.atlas->side, state.atlas->lineHeight, state.atlas->ascent},
        absoluteScale);
    metrics.spacingR = WrapSafetyPixels / 100.0f;
    return metrics;
}

void installFont(void* fontInfo, Atlas& atlas) {
    if (!generate(atlas)) return;
    FontState state = readFontState(fontInfo, atlas);
    if (!state.glyphCount || state.glyphCount > 256) return;
    if (state.original.fontHeight > 0.0f) {
        const float targetHeight = &atlas == &g_atlases[0] ?
            UiFontHeight : DescriptionFontHeight;
        const float adjustment = targetHeight / state.original.fontHeight;
        state.original.fontHeight = targetHeight;
        state.original.spacingR *= adjustment;
        state.original.spacingB *= adjustment;
    }
    const float scale = FontRefresh::contentScale();
    const FontMetrics::Values metrics = deriveMetrics(state, scale);
    const size_t coordinates = static_cast<size_t>(state.glyphCount) * sizeof(Vec3);
    std::memcpy(state.upper, atlas.upper, coordinates);
    std::memcpy(state.lower, atlas.lower, coordinates);
    writeMetrics(fontInfo, metrics);
    g_fonts[fontInfo] = state;
}

} // namespace

bool isApplied(void* fontInfo) {
    return fontState(fontInfo) != nullptr;
}

void refreshMetrics() {
    const float scale = FontRefresh::contentScale();
    for (auto& entry : g_fonts) {
        writeMetrics(entry.first, deriveMetrics(entry.second, scale));
    }
}

void registerTextureFont(void* texture) {
    if (!texture || !resolveOffsets()) return;
    char* object = static_cast<char*>(texture);
    void* fontInfo = *reinterpret_cast<void**>(object + g_textureFontInfo);
    if (!fontInfo) return;
    g_fonts.erase(fontInfo);

    char name[33] = {};
    std::memcpy(name, object + g_textureName, 32);
    Atlas* atlas = atlasForResource(name);
    if (atlas) installFont(fontInfo, *atlas);
}

void forgetTextureFont(void* texture) {
    if (!texture || !resolveOffsets()) return;
    void* fontInfo = *reinterpret_cast<void**>(
        static_cast<char*>(texture) + g_textureFontInfo);
    g_fonts.erase(fontInfo);
}

bool renderPadding(void* fontInfo, RenderPadding& out) {
    const FontState* state = fontState(fontInfo);
    if (!state) return false;
    const Atlas& atlas = *state->atlas;
    const float logicalHeight = *reinterpret_cast<float*>(
        static_cast<char*>(fontInfo) + g_fontHeight);
    out = {static_cast<float>(atlas.padding) / atlas.side,
        logicalHeight * static_cast<float>(atlas.padding) / atlas.lineHeight};
    return true;
}

bool expandGlyphUv(void* fontInfo, unsigned int glyph, const RenderPadding& padding,
                   GlyphUvRestore& saved) {
    FontState* state = fontState(fontInfo);
    if (!state || glyph >= state->glyphCount) return false;
    saved.upper = reinterpret_cast<float*>(state->upper + glyph);
    saved.lower = reinterpret_cast<float*>(state->lower + glyph);
    saved.upperX = saved.upper[0];
    saved.upperY = saved.upper[1];
    saved.lowerX = saved.lower[0];
    saved.lowerY = saved.lower[1];
    saved.upper[0] -= padding.uv;
    saved.upper[1] += padding.uv;
    saved.lower[0] += padding.uv;
    saved.lower[1] -= padding.uv;
    return true;
}

void restoreGlyphUv(const GlyphUvRestore& saved) {
    saved.upper[0] = saved.upperX;
    saved.upper[1] = saved.upperY;
    saved.lower[0] = saved.lowerX;
    saved.lower[1] = saved.lowerY;
}

bool bindTexture(void* fontInfo, int& side) {
    FontState* state = fontState(fontInfo);
    if (!state) return false;
    Atlas& atlas = *state->atlas;
    if (!atlas.glTexture) {
        glGenTextures(1, &atlas.glTexture);
        if (!atlas.glTexture) return false;
        glBindTexture(GL_TEXTURE_2D, atlas.glTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, atlas.side, atlas.side, 0,
            GL_RGB, GL_UNSIGNED_BYTE, atlas.rgb.data());
    } else {
        glBindTexture(GL_TEXTURE_2D, atlas.glTexture);
    }
    side = atlas.side;
    return true;
}

void invalidateGlContext() {
    for (Atlas& atlas : g_atlases) atlas.glTexture = 0;
}

} // namespace SystemFontAtlas
