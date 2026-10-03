#include "msdf_renderer.h"
#include "system_font_atlas.h"
#include "opengl_context.h"

#include <windows.h>
#include <GL/gl.h>

#pragma comment(lib, "opengl32.lib")

namespace MsdfRenderer {
namespace {

constexpr GLenum GlFragmentShader = 0x8B30;
constexpr GLenum GlCompileStatus = 0x8B81;
constexpr GLenum GlLinkStatus = 0x8B82;
constexpr GLenum GlCurrentProgram = 0x8B8D;
constexpr GLenum GlFragmentProgramArb = 0x8804;
constexpr GLenum GlTextureBinding2D = 0x8069;
constexpr GLenum GlActiveTexture = 0x84E0;
constexpr GLenum GlTexture0 = 0x84C0;
constexpr GLenum GlAlphaTest = 0x0BC0;

typedef GLuint (APIENTRY *GlCreateShaderFn)(GLenum);
typedef void (APIENTRY *GlShaderSourceFn)(GLuint, GLsizei, const char* const*, const GLint*);
typedef void (APIENTRY *GlCompileShaderFn)(GLuint);
typedef void (APIENTRY *GlGetShaderivFn)(GLuint, GLenum, GLint*);
typedef void (APIENTRY *GlDeleteShaderFn)(GLuint);
typedef GLuint (APIENTRY *GlCreateProgramFn)();
typedef void (APIENTRY *GlAttachShaderFn)(GLuint, GLuint);
typedef void (APIENTRY *GlLinkProgramFn)(GLuint);
typedef void (APIENTRY *GlGetProgramivFn)(GLuint, GLenum, GLint*);
typedef void (APIENTRY *GlDeleteProgramFn)(GLuint);
typedef void (APIENTRY *GlUseProgramFn)(GLuint);
typedef GLint (APIENTRY *GlGetUniformLocationFn)(GLuint, const char*);
typedef void (APIENTRY *GlUniform2fFn)(GLint, GLfloat, GLfloat);
typedef void (APIENTRY *GlActiveTextureFn)(GLenum);

struct ShaderApi {
    GlCreateShaderFn createShader = nullptr;
    GlShaderSourceFn shaderSource = nullptr;
    GlCompileShaderFn compileShader = nullptr;
    GlGetShaderivFn getShaderiv = nullptr;
    GlDeleteShaderFn deleteShader = nullptr;
    GlCreateProgramFn createProgram = nullptr;
    GlAttachShaderFn attachShader = nullptr;
    GlLinkProgramFn linkProgram = nullptr;
    GlGetProgramivFn getProgramiv = nullptr;
    GlDeleteProgramFn deleteProgram = nullptr;
    GlUseProgramFn useProgram = nullptr;
    GlGetUniformLocationFn getUniformLocation = nullptr;
    GlUniform2fFn uniform2f = nullptr;
    GlActiveTextureFn activeTexture = nullptr;
};

struct ContextState {
    HGLRC context = nullptr;
    ShaderApi api{};
    GLuint program = 0;
    GLint atlasSizeUniform = -1;
    bool arbFragmentSupported = false;
};

struct SavedState {
    GLint program = 0;
    GLboolean arbFragmentEnabled = GL_FALSE;
    GLboolean alphaTestEnabled = GL_FALSE;
    GLint texture0 = 0;
    GLint activeTexture = GlTexture0;
};

struct RenderState {
    SavedState saved{};
    bool active = false;
};

ContextState g_context;
RenderState g_render;

const char* FragmentShaderSource =
    "#version 120\n"
    "uniform sampler2D uFont;\n"
    "uniform vec2 uAtlasSize;\n"
    "float median3(float r, float g, float b) {\n"
    "  return max(min(r, g), min(max(r, g), b));\n"
    "}\n"
    "void main() {\n"
    "  vec3 msd = texture2D(uFont, gl_TexCoord[0].st).rgb;\n"
    "  float encodedDistance = median3(msd.r, msd.g, msd.b);\n"
    "  vec2 unitRange = vec2(8.0) / uAtlasSize;\n"
    "  vec2 screenTexSize = vec2(1.0) / fwidth(gl_TexCoord[0].st);\n"
    "  float screenPxRange = max(0.5 * dot(unitRange, screenTexSize), 1.0);\n"
    "  float alpha = clamp(screenPxRange * (encodedDistance - 0.5) + 0.5, 0.0, 1.0);\n"
    "  float opacity = gl_Color.a * alpha;\n"
    "  if (opacity <= 0.0) discard;\n"
    "  gl_FragColor = vec4(gl_Color.rgb, opacity);\n"
    "}\n";

bool compileShader(ContextState& state) {
    ShaderApi& api = state.api;
    bool ok = true;
#define RESOLVE(name, symbol) ok = FontOpenGl::resolve(symbol, api.name) && ok
    RESOLVE(createShader, "glCreateShader");
    RESOLVE(shaderSource, "glShaderSource");
    RESOLVE(compileShader, "glCompileShader");
    RESOLVE(getShaderiv, "glGetShaderiv");
    RESOLVE(deleteShader, "glDeleteShader");
    RESOLVE(createProgram, "glCreateProgram");
    RESOLVE(attachShader, "glAttachShader");
    RESOLVE(linkProgram, "glLinkProgram");
    RESOLVE(getProgramiv, "glGetProgramiv");
    RESOLVE(deleteProgram, "glDeleteProgram");
    RESOLVE(useProgram, "glUseProgram");
    RESOLVE(getUniformLocation, "glGetUniformLocation");
    RESOLVE(uniform2f, "glUniform2f");
    RESOLVE(activeTexture, "glActiveTexture");
#undef RESOLVE
    state.arbFragmentSupported = FontOpenGl::hasExtension("GL_ARB_fragment_program");
    if (!ok) return false;

    GLuint shader = api.createShader(GlFragmentShader);
    if (!shader) return false;
    const char* source = FragmentShaderSource;
    api.shaderSource(shader, 1, &source, nullptr);
    api.compileShader(shader);
    GLint compiled = 0;
    api.getShaderiv(shader, GlCompileStatus, &compiled);
    if (!compiled) {
        api.deleteShader(shader);
        return false;
    }

    GLuint program = api.createProgram();
    if (!program) {
        api.deleteShader(shader);
        return false;
    }
    api.attachShader(program, shader);
    api.linkProgram(program);
    api.deleteShader(shader);
    GLint linked = 0;
    api.getProgramiv(program, GlLinkStatus, &linked);
    if (!linked) {
        api.deleteProgram(program);
        return false;
    }

    state.program = program;
    state.atlasSizeUniform = api.getUniformLocation(program, "uAtlasSize");
    return true;
}

bool ensureContext() {
    HGLRC current = wglGetCurrentContext();
    if (!current) return false;
    if (g_context.context == current) return g_context.program != 0;
    g_context = ContextState{};
    g_context.context = current;
    SystemFontAtlas::invalidateGlContext();
    return compileShader(g_context);
}

SavedState captureState() {
    SavedState state;
    glGetIntegerv(GlCurrentProgram, &state.program);
    if (g_context.arbFragmentSupported)
        state.arbFragmentEnabled = glIsEnabled(GlFragmentProgramArb);
    state.alphaTestEnabled = glIsEnabled(GlAlphaTest);
    glGetIntegerv(GlActiveTexture, &state.activeTexture);
    g_context.api.activeTexture(GlTexture0);
    glGetIntegerv(GlTextureBinding2D, &state.texture0);
    return state;
}

void restoreState(const SavedState& state) {
    g_context.api.useProgram(static_cast<GLuint>(state.program));
    if (g_context.arbFragmentSupported) {
        if (state.arbFragmentEnabled) glEnable(GlFragmentProgramArb);
        else glDisable(GlFragmentProgramArb);
    }
    if (state.alphaTestEnabled) glEnable(GlAlphaTest);
    else glDisable(GlAlphaTest);
    g_context.api.activeTexture(GlTexture0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(state.texture0));
    g_context.api.activeTexture(static_cast<GLenum>(state.activeTexture));
}

void activate(int side) {
    if (g_context.arbFragmentSupported) glDisable(GlFragmentProgramArb);
    glDisable(GlAlphaTest);
    g_context.api.useProgram(g_context.program);
    if (g_context.atlasSizeUniform >= 0)
        g_context.api.uniform2f(g_context.atlasSizeUniform,
            static_cast<GLfloat>(side), static_cast<GLfloat>(side));
}

} // namespace

void begin(void* fontInfo) {
    if (!SystemFontAtlas::isApplied(fontInfo) || !ensureContext()) return;

    g_render.saved = captureState();
    int side = 0;
    if (!SystemFontAtlas::bindTexture(fontInfo, side)) {
        restoreState(g_render.saved);
        return;
    }

    activate(side);
    g_render.active = true;
}

void end() {
    if (!g_render.active) return;
    restoreState(g_render.saved);
    g_render.active = false;
}

} // namespace MsdfRenderer
