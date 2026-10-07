#include "glapi.h"
#include "windev.h"

GLApi gl;

bool gl_load(const windev::Window &window, const char **missing)
{
#define X(name, upper) \
    gl.name = (PFNGL##upper##PROC)window.glProcAddress("gl" #name); \
    if (!gl.name) { *missing = "gl" #name; return false; }
    GL_FUNCTIONS(X)
#undef X
#define X(name, upper) gl.name = (PFNGL##upper##PROC)window.glProcAddress("gl" #name);
    GL_OPTIONAL_FUNCTIONS(X)
#undef X
    return true;
}
