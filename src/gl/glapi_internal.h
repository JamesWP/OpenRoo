/* The OpenGL ES entry points the backend uses: an OpenGL ES 3.2 subset, loaded
 * by name through the window (windev::Window::glProcAddress) once the context
 * exists, so nothing links against the system's OpenGL library.  Only the
 * gl group includes this.
 *
 *   gl.Enable(GL_BLEND);
 *
 * Nothing here is from the fixed-function pipeline: there are no matrix
 * stacks, no lights, no texture environments and no immediate mode. */

#pragma once
#ifndef OPENROO_GL_BACKEND
#error "Only the gl group's own sources may include this header; use glrenderdevice.h"
#endif
/* The Khronos header, vendored under khronos/ so every platform builds
 * against the same one; no prototypes, only the PFNGL...PROC types. */
#define GL_GLES_PROTOTYPES 0
#include <GLES3/gl32.h>

namespace windev { class Window; }

#define GL_FUNCTIONS(X) \
    X(Enable, ENABLE) X(Disable, DISABLE) X(GetString, GETSTRING) \
    X(GetIntegerv, GETINTEGERV) X(GetError, GETERROR) \
    X(Viewport, VIEWPORT) X(Clear, CLEAR) X(ClearColor, CLEARCOLOR) \
    X(ClearDepthf, CLEARDEPTHF) X(ClearStencil, CLEARSTENCIL) \
    X(ColorMask, COLORMASK) X(DepthMask, DEPTHMASK) X(DepthFunc, DEPTHFUNC) \
    X(StencilFunc, STENCILFUNC) X(StencilOp, STENCILOP) X(StencilMask, STENCILMASK) \
    X(BlendFunc, BLENDFUNC) X(CullFace, CULLFACE) X(FrontFace, FRONTFACE) \
    X(PixelStorei, PIXELSTOREI) X(ReadPixels, READPIXELS) \
    X(GenTextures, GENTEXTURES) X(DeleteTextures, DELETETEXTURES) \
    X(BindTexture, BINDTEXTURE) X(ActiveTexture, ACTIVETEXTURE) \
    X(TexImage2D, TEXIMAGE2D) X(TexSubImage2D, TEXSUBIMAGE2D) \
    X(TexParameteri, TEXPARAMETERI) \
    X(GenSamplers, GENSAMPLERS) X(DeleteSamplers, DELETESAMPLERS) \
    X(BindSampler, BINDSAMPLER) X(SamplerParameteri, SAMPLERPARAMETERI) \
    X(SamplerParameterfv, SAMPLERPARAMETERFV) \
    X(GenBuffers, GENBUFFERS) X(DeleteBuffers, DELETEBUFFERS) \
    X(BindBuffer, BINDBUFFER) X(BindBufferBase, BINDBUFFERBASE) \
    X(BufferData, BUFFERDATA) X(BufferSubData, BUFFERSUBDATA) \
    X(MapBufferRange, MAPBUFFERRANGE) X(UnmapBuffer, UNMAPBUFFER) \
    X(GenVertexArrays, GENVERTEXARRAYS) X(DeleteVertexArrays, DELETEVERTEXARRAYS) \
    X(BindVertexArray, BINDVERTEXARRAY) \
    X(EnableVertexAttribArray, ENABLEVERTEXATTRIBARRAY) \
    X(DisableVertexAttribArray, DISABLEVERTEXATTRIBARRAY) \
    X(VertexAttribPointer, VERTEXATTRIBPOINTER) X(VertexAttrib4f, VERTEXATTRIB4F) \
    X(CreateShader, CREATESHADER) X(DeleteShader, DELETESHADER) \
    X(ShaderSource, SHADERSOURCE) X(CompileShader, COMPILESHADER) \
    X(GetShaderiv, GETSHADERIV) X(GetShaderInfoLog, GETSHADERINFOLOG) \
    X(CreateProgram, CREATEPROGRAM) X(DeleteProgram, DELETEPROGRAM) \
    X(AttachShader, ATTACHSHADER) X(LinkProgram, LINKPROGRAM) \
    X(GetProgramiv, GETPROGRAMIV) X(GetProgramInfoLog, GETPROGRAMINFOLOG) \
    X(UseProgram, USEPROGRAM) X(GetUniformLocation, GETUNIFORMLOCATION) \
    X(Uniform1i, UNIFORM1I) \
    X(GetUniformBlockIndex, GETUNIFORMBLOCKINDEX) \
    X(UniformBlockBinding, UNIFORMBLOCKBINDING) \
    X(Uniform1f, UNIFORM1F) X(Uniform1fv, UNIFORM1FV) X(Uniform2fv, UNIFORM2FV) \
    X(Uniform3fv, UNIFORM3FV) X(Uniform4fv, UNIFORM4FV) \
    X(Uniform4iv, UNIFORM4IV) X(UniformMatrix4fv, UNIFORMMATRIX4FV) \
    X(TransformFeedbackVaryings, TRANSFORMFEEDBACKVARYINGS) \
    X(BeginTransformFeedback, BEGINTRANSFORMFEEDBACK) \
    X(EndTransformFeedback, ENDTRANSFORMFEEDBACK) \
    X(VertexAttribDivisor, VERTEXATTRIBDIVISOR) \
    X(DrawArraysInstanced, DRAWARRAYSINSTANCED) \
    X(Scissor, SCISSOR) X(DrawArrays, DRAWARRAYS) X(DrawElements, DRAWELEMENTS)

struct GLApi {
#define X(name, upper) PFNGL##upper##PROC name;
    GL_FUNCTIONS(X)
#undef X
};

/* The functions, valid after gl_load. */
extern GLApi gl;

/* Loads every function through `window`'s context.  False, with the first
 * missing name in `missing`, if the driver lacks one. */
bool gl_load(const windev::Window &window, const char **missing);
