#pragma once
#include <windows.h>
#include <GL/gl.h>
#include <cstring>

namespace FontOpenGl {
inline bool validProc(PROC p) {
    return p && p != reinterpret_cast<PROC>(1) &&
        p != reinterpret_cast<PROC>(2) && p != reinterpret_cast<PROC>(3) &&
        p != reinterpret_cast<PROC>(-1);
}

template<class T> inline bool resolve(const char* name, T& out) {
    PROC p = wglGetProcAddress(name);
    out = validProc(p) ? reinterpret_cast<T>(p) : nullptr;
    return out != nullptr;
}

inline bool hasExtension(const char* name) {
    const char* all = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if (!all || !name || !*name || std::strchr(name, ' ')) return false;
    const size_t n = std::strlen(name);
    for (const char* p = all; (p = std::strstr(p, name)); p += n) {
        if ((p == all || p[-1] == ' ') && (p[n] == ' ' || p[n] == 0)) return true;
    }
    return false;
}
} // namespace FontOpenGl
