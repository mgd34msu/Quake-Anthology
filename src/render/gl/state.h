#ifndef QA_RENDER_GL_STATE_H
#define QA_RENDER_GL_STATE_H

/* Every native setter below shares this shadow. Source pipeline state remains
 * the desired state; resource and composite operations update actual state here. */
static inline void gl_state_invalidate(qa_gl_renderer *renderer)
{
    memset(&renderer->native_state, 0, sizeof(renderer->native_state));
    memset(renderer->stage_values, 0, sizeof(renderer->stage_values));
    renderer->vertex_array_known = false;
}
static inline void gl_state_skeletal_buffer(qa_gl_renderer *renderer, unsigned binding,
                                            GLuint buffer, size_t offset, size_t bytes)
{
    gl_native_state *state = &renderer->native_state;
    if (state->skin_known[binding] && state->skin_buffer[binding] == buffer &&
        state->skin_offset[binding] == offset && state->skin_size[binding] == bytes) return;
    if (bytes) renderer->gl.BindBufferRange(GL_SHADER_STORAGE_BUFFER, binding, buffer,
        (GLintptr)offset, (GLsizeiptr)bytes);
    else renderer->gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, buffer);
    state->skin_known[binding] = true;
    state->skin_buffer[binding] = buffer;
    state->skin_offset[binding] = offset;
    state->skin_size[binding] = bytes;
}
static inline bool gl_state_changed(gl_cached_value *cached, const void *value, size_t size)
{
    if (cached->size == size && memcmp(cached->value, value, size) == 0) return false;
    memcpy(cached->value, value, size);
    cached->size = (uint8_t)size;
    return true;
}
static inline void gl_state_enable(qa_gl_renderer *renderer, GLenum capability, bool enabled)
{
    unsigned bit;
    switch (capability) {
    case GL_DEPTH_TEST: bit = 1u << 0; break;
    case GL_BLEND: bit = 1u << 1; break;
    case GL_CULL_FACE: bit = 1u << 2; break;
    case GL_POLYGON_OFFSET_FILL: bit = 1u << 3; break;
    case GL_STENCIL_TEST: bit = 1u << 4; break;
    case GL_SCISSOR_TEST: bit = 1u << 5; break;
    case GL_ALPHA_TEST: bit = 1u << 6; break;
    default: bit = 0; break;
    }
    gl_native_state *state = &renderer->native_state;
    if (bit && (state->enable_known & bit) && !!(state->enabled & bit) == enabled) return;
    if (enabled) renderer->gl.Enable(capability); else renderer->gl.Disable(capability);
    state->enable_known |= bit;
    if (enabled) state->enabled |= bit; else state->enabled &= ~bit;
}
static inline void gl_state_depth_func(qa_gl_renderer *renderer, GLenum function)
{
    const GLenum value[] = {function};
    if (gl_state_changed(&renderer->native_state.values[0], value, sizeof(value)))
        renderer->gl.DepthFunc(function);
}
static inline void gl_state_depth_mask(qa_gl_renderer *renderer, GLboolean enabled)
{
    const GLboolean value[] = {enabled};
    if (gl_state_changed(&renderer->native_state.values[1], value, sizeof(value)))
        renderer->gl.DepthMask(enabled);
}
static inline void gl_state_color_mask(qa_gl_renderer *renderer, GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{
    const GLboolean value[] = {red, green, blue, alpha};
    if (gl_state_changed(&renderer->native_state.values[2], value, sizeof(value)))
        renderer->gl.ColorMask(red, green, blue, alpha);
}
static inline void gl_state_stencil_func(qa_gl_renderer *renderer, GLenum function, GLint reference, GLuint mask)
{
    const GLuint value[] = {(GLuint)function, (GLuint)reference, mask};
    if (gl_state_changed(&renderer->native_state.values[3], value, sizeof(value)))
        renderer->gl.StencilFunc(function, reference, mask);
}
static inline void gl_state_stencil_op(qa_gl_renderer *renderer, GLenum fail, GLenum depth_fail, GLenum depth_pass)
{
    const GLenum value[] = {fail, depth_fail, depth_pass};
    if (gl_state_changed(&renderer->native_state.values[4], value, sizeof(value)))
        renderer->gl.StencilOp(fail, depth_fail, depth_pass);
}
static inline void gl_state_stencil_mask(qa_gl_renderer *renderer, GLuint mask)
{
    const GLuint value[] = {mask};
    if (gl_state_changed(&renderer->native_state.values[5], value, sizeof(value)))
        renderer->gl.StencilMask(mask);
}
static inline void gl_state_depth_range(qa_gl_renderer *renderer, GLdouble near_depth, GLdouble far_depth)
{
    const GLdouble value[] = {near_depth, far_depth};
    if (gl_state_changed(&renderer->native_state.values[6], value, sizeof(value)))
        renderer->gl.DepthRange(near_depth, far_depth);
}
static inline void gl_state_polygon_offset(qa_gl_renderer *renderer, GLfloat factor, GLfloat units)
{
    const GLfloat value[] = {factor, units};
    if (gl_state_changed(&renderer->native_state.values[7], value, sizeof(value)))
        renderer->gl.PolygonOffset(factor, units);
}
static inline void gl_state_line_width(qa_gl_renderer *renderer, GLfloat width)
{
    const GLfloat value[] = {width};
    if (gl_state_changed(&renderer->native_state.values[8], value, sizeof(value)))
        renderer->gl.LineWidth(width);
}
static inline void gl_state_blend_func(qa_gl_renderer *renderer, GLenum source, GLenum destination)
{
    const GLenum value[] = {source, destination};
    if (gl_state_changed(&renderer->native_state.values[9], value, sizeof(value)))
        renderer->gl.BlendFunc(source, destination);
}
static inline void gl_state_cull_face(qa_gl_renderer *renderer, GLenum face)
{
    const GLenum value[] = {face};
    if (gl_state_changed(&renderer->native_state.values[10], value, sizeof(value)))
        renderer->gl.CullFace(face);
}
static inline void gl_state_front_face(qa_gl_renderer *renderer, GLenum face)
{
    const GLenum value[] = {face};
    if (gl_state_changed(&renderer->native_state.values[11], value, sizeof(value)))
        renderer->gl.FrontFace(face);
}
static inline void gl_state_program(qa_gl_renderer *renderer, GLuint program)
{
    const GLuint value[] = {program};
    if (gl_state_changed(&renderer->native_state.values[12], value, sizeof(value)))
        renderer->gl.UseProgram(program);
}
static inline void gl_state_polygon_mode(qa_gl_renderer *renderer, GLenum face, GLenum mode)
{
    gl_native_state *state = &renderer->native_state;
    unsigned mask = face == GL_FRONT ? 1u : face == GL_BACK ? 2u : 3u;
    bool changed = false;
    for (unsigned i = 0; i < 2; ++i)
        if ((mask & (1u << i)) && (!(state->polygon_known & (1u << i)) || state->polygon[i] != mode)) changed = true;
    if (!changed) return;
    renderer->gl.PolygonMode(face, mode);
    for (unsigned i = 0; i < 2; ++i) if (mask & (1u << i)) state->polygon[i] = mode;
    state->polygon_known |= mask;
}
static inline void gl_state_active_texture(qa_gl_renderer *renderer, GLenum unit)
{
    gl_native_state *state = &renderer->native_state;
    if (!state->active_known || state->active != unit) renderer->gl.ActiveTexture(unit);
    state->active = unit;
    state->active_known = true;
}
static inline void gl_state_bind_texture(qa_gl_renderer *renderer, GLenum target, GLuint texture)
{
    gl_native_state *state = &renderer->native_state;
    unsigned unit = state->active - GL_TEXTURE0;
    if (target == GL_TEXTURE_2D && state->active_known && unit < 3) {
        unsigned bit = 1u << unit;
        if ((state->texture_known & bit) && state->texture[unit] == texture) return;
        state->texture[unit] = texture;
        state->texture_known |= bit;
    }
    renderer->gl.BindTexture(target, texture);
}
static inline void gl_state_texture_2d(qa_gl_renderer *renderer, GLenum unit, GLuint texture)
{
    unsigned index = unit - GL_TEXTURE0;
    if (index < 3 && (renderer->native_state.texture_known & (1u << index)) &&
        renderer->native_state.texture[index] == texture) return;
    gl_state_active_texture(renderer, unit);
    gl_state_bind_texture(renderer, GL_TEXTURE_2D, texture);
}
static inline void gl_state_delete_textures(qa_gl_renderer *renderer, GLsizei count, const GLuint *textures)
{
    renderer->gl.DeleteTextures(count, textures);
    for (GLsizei i = 0; i < count; ++i)
        for (unsigned unit = 0; unit < 3; ++unit)
            if (textures[i] && renderer->native_state.texture[unit] == textures[i])
                renderer->native_state.texture_known &= ~(1u << unit);
    renderer->native_state.destination_valid = false;
}
static inline void gl_state_framebuffer(qa_gl_renderer *renderer, GLenum target, GLuint framebuffer)
{
    renderer->native_state.destination_valid = false;
    renderer->gl.BindFramebuffer(target, framebuffer);
}
static inline void gl_state_draw_buffer(qa_gl_renderer *renderer, GLenum buffer)
{
    renderer->native_state.destination_valid = false;
    renderer->gl.DrawBuffer(buffer);
}
static inline void gl_state_read_buffer(qa_gl_renderer *renderer, GLenum buffer)
{
    renderer->native_state.destination_valid = false;
    renderer->gl.ReadBuffer(buffer);
}
static inline void gl_state_vertex_array(qa_gl_renderer *renderer, GLuint array)
{
    if (!renderer->vertex_array_known || renderer->bound_vertex_array != array)
        renderer->gl.BindVertexArray(array);
    renderer->bound_vertex_array = array;
    renderer->vertex_array_known = true;
}
#endif
