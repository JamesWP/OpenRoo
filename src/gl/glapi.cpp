#include "glapi_internal.h"
#include "windev.h"

GLApi gl;
GLFlavour gl_flavour = GLFlavour::ES32;

bool gl_load(const windev::Window &window, const char **missing)
{
#define X(name, upper) \
    gl.name = (PFNGL##upper##PROC)window.glProcAddress("gl" #name); \
    if (!gl.name) { *missing = "gl" #name; return false; }
    GL_FUNCTIONS(X)
#undef X
    return true;
}
