#include "internal.h"

static bool load_proc(qa_gl_renderer *renderer, void *destination, size_t bytes,
                      const char *name, const char *fallback, qa_error *error)
{
    if (bytes != sizeof(void *)) {
        qa_error_set(error, QA_ERROR_UNSUPPORTED, 0,
                     "OpenGL function-pointer ABI is unsupported");
        return false;
    }
    qa_error ignored = {0};
    void *address = qa_display_gl_proc(renderer->options.display, name,
                                       fallback == NULL ? error : &ignored);
    if (address == NULL && fallback != NULL)
        address = qa_display_gl_proc(renderer->options.display, fallback, error);
    if (address == NULL) return false;
    memcpy(destination, &address, bytes);
    return true;
}

#define LOAD(member)                                                            \
    do {                                                                        \
        if (!load_proc(renderer, &renderer->gl.member,                           \
                       sizeof(renderer->gl.member), "gl" #member, NULL, error)) \
            return false;                                                       \
    } while (0)
#define LOAD_ALIAS(member, fallback)                                             \
    do {                                                                         \
        if (!load_proc(renderer, &renderer->gl.member,                            \
                       sizeof(renderer->gl.member), "gl" #member, fallback,      \
                       error)) return false;                                      \
    } while (0)

bool gl_api_load(qa_gl_renderer *renderer, qa_error *error)
{
    if (!qa_display_make_current(renderer->options.display, error)) return false;
    LOAD(GetString);
    LOAD(GetIntegerv);
    LOAD(GetFloatv);
    LOAD(GetError);
    LOAD(IsEnabled);
    LOAD(Enable);
    LOAD(Disable);
    LOAD(Viewport);
    LOAD(Scissor);
    LOAD(ClearColor);
    LOAD(ClearDepth);
    LOAD(ClearStencil);
    LOAD(Clear);
    LOAD(DepthFunc);
    LOAD(DepthMask);
    LOAD(ColorMask);
    LOAD(StencilFunc);
    LOAD(StencilOp);
    LOAD(StencilMask);
    LOAD(DepthRange);
    LOAD(PolygonMode);
    LOAD(PolygonOffset);
    LOAD(LineWidth);
    LOAD(BlendFunc);
    LOAD(CullFace);
    LOAD(FrontFace);
    LOAD(DrawBuffer);
    LOAD(ReadBuffer);
    LOAD_ALIAS(ActiveTexture, "glActiveTextureARB");
    LOAD(GenTextures);
    LOAD(DeleteTextures);
    LOAD(BindTexture);
    LOAD(TexParameteri);
    LOAD(TexParameterfv);
    LOAD(TexImage2D);
    LOAD(TexSubImage2D);
    LOAD(CopyTexImage2D);
    LOAD(GetTexImage);
    LOAD(GetTexLevelParameteriv);
    LOAD(GetTexParameteriv);
    LOAD(PixelStorei);
    LOAD(ReadPixels);
    LOAD_ALIAS(GenBuffers, "glGenBuffersARB");
    LOAD_ALIAS(DeleteBuffers, "glDeleteBuffersARB");
    LOAD_ALIAS(BindBuffer, "glBindBufferARB");
    LOAD_ALIAS(BufferData, "glBufferDataARB");
    LOAD_ALIAS(BufferSubData, "glBufferSubDataARB");
    LOAD_ALIAS(GetBufferSubData, "glGetBufferSubDataARB");
    LOAD(EnableVertexAttribArray);
    LOAD(DisableVertexAttribArray);
    LOAD(VertexAttribPointer);
    LOAD(DrawElements);
    LOAD(ArrayElement);
    LOAD(VertexAttrib4f);
    qa_error optional = {0};
    (void)load_proc(renderer, &renderer->gl.LockArraysEXT,
        sizeof(renderer->gl.LockArraysEXT), "glLockArraysEXT", NULL, &optional);
    (void)load_proc(renderer, &renderer->gl.UnlockArraysEXT,
        sizeof(renderer->gl.UnlockArraysEXT), "glUnlockArraysEXT", NULL, &optional);
    LOAD(CreateShader);
    LOAD(ShaderSource);
    LOAD(CompileShader);
    LOAD(GetShaderiv);
    LOAD(GetShaderInfoLog);
    LOAD(DeleteShader);
    LOAD(CreateProgram);
    LOAD(AttachShader);
    LOAD(BindAttribLocation);
    LOAD(LinkProgram);
    LOAD(GetProgramiv);
    LOAD(GetProgramInfoLog);
    LOAD(DeleteProgram);
    LOAD(UseProgram);
    LOAD(GetUniformLocation);
    LOAD(Uniform1i);
    LOAD(Uniform1f);
    LOAD(Uniform3f);
    LOAD(Uniform4f);
    LOAD(UniformMatrix3fv);
    LOAD(UniformMatrix4fv);
    LOAD_ALIAS(GenFramebuffers, "glGenFramebuffersEXT");
    LOAD_ALIAS(DeleteFramebuffers, "glDeleteFramebuffersEXT");
    LOAD_ALIAS(BindFramebuffer, "glBindFramebufferEXT");
    LOAD_ALIAS(FramebufferTexture2D, "glFramebufferTexture2DEXT");
    LOAD_ALIAS(CheckFramebufferStatus, "glCheckFramebufferStatusEXT");
    LOAD_ALIAS(GetFramebufferAttachmentParameteriv,
               "glGetFramebufferAttachmentParameterivEXT");
    LOAD_ALIAS(BlitFramebuffer, "glBlitFramebufferEXT");
    LOAD_ALIAS(GenRenderbuffers, "glGenRenderbuffersEXT");
    LOAD_ALIAS(DeleteRenderbuffers, "glDeleteRenderbuffersEXT");
    LOAD_ALIAS(BindRenderbuffer, "glBindRenderbufferEXT");
    LOAD_ALIAS(RenderbufferStorage, "glRenderbufferStorageEXT");
    LOAD_ALIAS(FramebufferRenderbuffer, "glFramebufferRenderbufferEXT");
    LOAD(Begin);
    LOAD(End);
    LOAD(TexCoord2f);
    LOAD(Vertex2f);
    LOAD(Vertex4f);
    LOAD(Color4f);
    LOAD(Finish);
    return true;
}

#undef LOAD
#undef LOAD_ALIAS
