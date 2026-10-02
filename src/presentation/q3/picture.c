#include "internal.h"
#include "qa/q3_cinematic_handles.h"
#include <stdio.h>

const qa_material *q3p_default_material(const qa_q3_presentation *p)
{
    return qa_material_find(p->options.assets->options.provider.materials, "*default");
}
static bool source_picture_geometry(qa_scene_frame *frame, qa_scene_rect target,
    qa_scene_rect_f rect, qa_scene_vec4 uv, qa_scene_vec4 color, qa_scene_mesh *mesh, qa_error *error)
{
    if (!target.width || !target.height || !isfinite(rect.x) || !isfinite(rect.y) ||
        !isfinite(rect.width) || !isfinite(rect.height) || !isfinite(rect.x + rect.width) ||
        !isfinite(rect.y + rect.height))
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Source picture requires its actual finite rectangle and viewport");
    qa_scene_vertex *vertices = qa_arena_alloc(&frame->storage, 4 * sizeof(*vertices), _Alignof(qa_scene_vertex), error);
    uint32_t *indices = qa_arena_alloc(&frame->storage, 6 * sizeof(*indices), _Alignof(uint32_t), error);
    if (!vertices || !indices) return false;
    const uint32_t pattern[6] = {3, 0, 2, 2, 0, 1};
    memcpy(indices, pattern, sizeof(pattern));
    vertices[0] = (qa_scene_vertex){.position = {rect.x, rect.y, 0}, .texcoord = {uv.x, uv.y}, .color = color};
    vertices[1] = (qa_scene_vertex){.position = {rect.x + rect.width, rect.y, 0}, .texcoord = {uv.z, uv.y}, .color = color};
    vertices[2] = (qa_scene_vertex){.position = {rect.x + rect.width, rect.y + rect.height, 0}, .texcoord = {uv.z, uv.w}, .color = color};
    vertices[3] = (qa_scene_vertex){.position = {rect.x, rect.y + rect.height, 0}, .texcoord = {uv.x, uv.w}, .color = color};
    *mesh = (qa_scene_mesh){.vertices = vertices, .indices = indices, .vertex_count = 4,
        .index_count = 6, .primitive = QA_SCENE_TRIANGLES,
        .bounds = {{fminf(rect.x, rect.x + rect.width), fminf(rect.y, rect.y + rect.height), 0},
            {fmaxf(rect.x, rect.x + rect.width), fmaxf(rect.y, rect.y + rect.height), 0}}};
    return true;
}

static bool picture_emit(qa_q3_presentation *p, const qa_material *material,
    qa_scene_rect_f rect, qa_scene_vec4 uv, const qa_q3_picture_receipt *receipt, qa_error *error)
{
    if (!p->frame) return q3p_fail(error, QA_ERROR_ARGUMENT, "Q3 picture has no frame owner");
    if (!material) material = q3p_default_material(p);
    if (!material) return q3p_fail(error, QA_ERROR_ARGUMENT, "Q3 renderer has no default material");
    qa_scene_rect target = receipt ? receipt->viewport : p->options.viewport;
    qa_scene_vec4 color = receipt ? receipt->color : p->color;
    uint32_t seat = receipt ? receipt->seat : p->options.seat;
    rect.x += (float)target.x; rect.y += (float)target.y;
    qa_scene_mesh mesh;
    qa_material_context context = {.entity_color = color,
        .identity_light = receipt ? receipt->identity_light : p->options.identity_light,
        .source_scratch = p->options.source_scratch,
        .source_white = qa_material_library_has_source_profile(p->options.assets->options.provider.materials) ?
            qa_scene_source_q3_white(p->options.assets->options.provider.images) :
            qa_scene_white(p->options.assets->options.provider.images),
        .source_diagnostics = {.polygon_offset_factor = -1, .polygon_offset_units = -2},
        .milliseconds = receipt ? receipt->milliseconds :
            p->options.milliseconds ? p->options.milliseconds(p->options.context) : p->render_milliseconds,
        .video_frame = p->options.video_frame, .video_context = p->options.video_context};
    context.seconds = (float)context.milliseconds * .001f;
    if (p->material_view_valid) context.view = p->material_view;
    else {
        context.view.axis[0] = qa_v3(1, 0, 0);
        context.view.axis[1] = qa_v3(0, 1, 0);
        context.view.axis[2] = qa_v3(0, 0, 1);
    }
    context.view.viewport = target; context.view.seat = seat;
    context.local_view_origin = context.view.origin;
    qa_scene_matrix_identity(&context.model);
    if (p->options.prepare_picture && !p->options.prepare_picture(p->options.context, &context, error)) return false;
    if (!(context.source_scratch ? source_picture_geometry(p->frame, target, rect, uv, color, &mesh, error) :
        qa_scene_picture_geometry(p->frame, target, rect, uv, color, &mesh, error))) return false;
    if (!mesh.vertex_count) return true;
    context.source_primitives = true;
    context.source_writer = QA_SOURCE_WRITE_PICTURE;
    context.fog = (qa_scene_fog){0}; context.fog_tc_scale = 0; context.fog_index = 0;
    context.view.clip_enabled = false;
    qa_scene_matrix projection = {.m = {
        2.0f / (float)target.width, 0, 0, 0,
        0, -2.0f / (float)target.height, 0, 0,
        0, 0, 0, 0,
        -1.0f - 2.0f * (float)target.x / (float)target.width,
        1.0f + 2.0f * (float)target.y / (float)target.height, 0, 1}};
    bool first_picture = true;
    if (context.source_scratch) {
        projection.m[10] = -2; projection.m[14] = -1;
        context.source_picture_clock = receipt ? NULL : p->options.milliseconds;
        context.source_picture_clock_context = receipt ? NULL : p->options.context;
        context.source_picture = true; context.source_picture_projection = projection;
        if (!qa_material_source_picture_begin(context.source_scratch, p->frame, &context.view, &first_picture, error)) return false;
    }
    size_t first = p->frame->command_count;
    qa_scene_command view = {.kind = QA_SCENE_COMMAND_VIEW,
        .data.view = {.viewport = target, .seat = seat}};
    if ((first_picture && !qa_scene_frame_emit(p->frame, &view, error)) ||
        !qa_material_submit(material, &mesh, &context, p->frame, error)) {
        p->frame->command_count = first; return false;
    }
    for (size_t i = first; i < p->frame->command_count; ++i) {
        qa_scene_command *command = &p->frame->commands[i];
        if (command->kind != QA_SCENE_COMMAND_DRAW) continue;
        command->data.draw.mvp = projection;
        if (!context.source_scratch) {
            command->data.draw.state.depth_test = QA_DEPTH_ALWAYS;
            command->data.draw.state.depth_write = false;
            command->data.draw.state.cull = QA_CULL_NONE;
        }
    }
    return true;
}
bool q3p_picture(qa_q3_presentation *p, const qa_material *material,
    qa_scene_rect_f rect, qa_scene_vec4 uv, qa_error *error)
{ return picture_emit(p, material, rect, uv, NULL, error); }

static bool raw_picture_emit(qa_q3_presentation *p,const qa_scene_image *image,qa_scene_rect_f rect,
    qa_scene_vec4 uv,qa_scene_vec4 color,const qa_q3_picture_receipt *receipt,qa_error *error)
{
    const qa_q3_presentation_assets *assets=p?p->options.assets:NULL;
    if (!p || !p->frame || !p->busy || !assets || assets->busy!=1 || !image)
        return q3p_fail(error,QA_ERROR_ARGUMENT,"Raw cinematic requires its actual entered renderer and completed image");
    qa_scene_rect target=receipt?receipt->viewport:p->options.viewport;
    qa_material_context context={.identity_light=receipt?receipt->identity_light:p->options.identity_light,
        .source_scratch=p->options.source_scratch,
        .milliseconds=receipt?receipt->milliseconds:p->render_milliseconds,
        .source_picture_clock=receipt?NULL:p->options.milliseconds,
        .source_picture_clock_context=receipt?NULL:p->options.context};
    context.view.viewport=target; context.view.seat=receipt?receipt->seat:p->options.seat;
    if (p->options.prepare_picture && !p->options.prepare_picture(p->options.context,&context,error)) return false;
    qa_scene_draw draw={.textures={image,NULL},.texture_count=1,.source_direct=QA_SOURCE_DIRECT_RAW,
        .retain_texture={context.source_scratch!=NULL,false}};
    qa_scene_state_default(&draw.state);
    if (!source_picture_geometry(p->frame,target,rect,uv,color,&draw.mesh,error)) return false;
    uint32_t *indices=(uint32_t *)draw.mesh.indices;
    const uint32_t quad[6]={0,1,2,0,2,3};
    memcpy(indices,quad,sizeof(quad));
    qa_scene_matrix_identity(&draw.model);
    draw.mvp=(qa_scene_matrix){.m={2.0f/(float)target.width,0,0,0,
        0,-2.0f/(float)target.height,0,0,0,0,-2,0,
        -1.0f-2.0f*(float)target.x/(float)target.width,
        1.0f+2.0f*(float)target.y/(float)target.height,-1,1}};
    context.source_picture_projection=draw.mvp;
    if (context.source_scratch)
        return qa_material_source_raw_submit(context.source_scratch,p->frame,&context,&draw,error);
    qa_scene_command view={.kind=QA_SCENE_COMMAND_VIEW,
        .data.view={.viewport=target,.seat=context.view.seat,.projection=draw.mvp}};
    return qa_scene_frame_emit(p->frame,&view,error) && qa_scene_frame_draw(p->frame,&draw,error);
}
bool q3p_source_raw_picture(qa_q3_presentation *p,const qa_scene_image *image,qa_scene_rect_f rect,
    qa_scene_vec4 uv,qa_scene_vec4 color,qa_error *error)
{
    if (!p || !p->options.assets || !image || (qa_scene_image_resource_owner(image)!=p->options.assets->options.provider.images &&
        (!p->options.cinematics || !qa_q3_cinematic_handles_image_is(
            qa_q3_cinematic_source_handles(p->options.cinematics),image))))
        return q3p_fail(error,QA_ERROR_ARGUMENT,"Raw cinematic changed its actual registered image namespace");
    return raw_picture_emit(p,image,rect,uv,color,NULL,error);
}

bool qa_q3_presentation_picture(qa_q3_presentation *p, int32_t shader,
                                qa_scene_rect_f rect, qa_scene_vec4 uv, qa_error *error)
{
    if (!q3p_begin(p, error)) return false;
    const qa_material *material;
    bool ok = q3p_shader_get(p->options.assets, shader, &material, error);
    if (ok && !material) material = q3p_default_material(p);
    if (ok && p->options.picture_capture) {
        qa_q3_picture_receipt receipt = {.assets = p->options.assets, .material = material,
            .shader = shader, .rect = rect, .uv = uv, .color = p->color, .viewport = p->options.viewport,
            .seat = p->options.seat, .identity_light = p->options.identity_light,
            .milliseconds = p->options.milliseconds ? p->options.milliseconds(p->options.context) : p->render_milliseconds};
        ok = p->frame && material && p->options.picture_capture(p->options.context, &receipt, error);
        if (!ok && (!error || error->code == QA_OK)) q3p_fail(error, QA_ERROR_ARGUMENT, "Q3 picture capture lost its actual frame or material");
    } else if (ok) ok = q3p_picture(p, material, rect, uv, error);
    return q3p_end(p, ok);
}

bool qa_q3_presentation_selected_picture(qa_q3_presentation *p, const qa_q3_picture_receipt *receipt,
    const qa_q3_scene_options *options, qa_scene_frame *frame, qa_error *error)
{
    const qa_q3_presentation_assets *primary = p ? p->options.assets : NULL;
    if (!p || !receipt || !receipt->assets || !receipt->material || !options || !frame ||
        !p->busy || p->submission != options || p->frame != frame || !primary || primary->busy != 1 ||
        primary->capturing || primary->codec_busy || !q3p_assets_children_idle(primary) ||
        (receipt->assets != primary && !qa_q3_assets_idle(receipt->assets)))
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Selected picture requires its actual primary view and retained command namespace");
    const qa_material *material;
    if (!q3p_shader_get(receipt->assets, receipt->shader, &material, error)) return false;
    if (!material) material = qa_material_find(receipt->assets->options.provider.materials, "*default");
    if (material != receipt->material)
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Selected picture changed its actual registered material");
    return picture_emit(p, material, receipt->rect, receipt->uv, receipt, error);
}
bool qa_q3_presentation_completed_picture(qa_q3_presentation *p, const qa_q3_picture_receipt *receipt,
    qa_scene_frame *frame, qa_error *error)
{
    if (!p || !receipt || !receipt->assets ||
        ((receipt->material != NULL) == (receipt->image != NULL)) || !frame || p->frame != frame ||
        !qa_q3_presentation_idle(p) || !qa_q3_assets_idle(receipt->assets))
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Completed picture requires its actual idle primary frame and retained command namespace");
    if (!q3p_begin(p, error)) return false;
    if (receipt->image) {
        bool ok = qa_scene_image_resource_owner(receipt->image) == receipt->assets->options.provider.images ||
            (p->options.cinematics && qa_q3_cinematic_handles_image_is(
                qa_q3_cinematic_source_handles(p->options.cinematics),receipt->image));
        if (!ok) q3p_fail(error, QA_ERROR_ARGUMENT, "Completed cinematic changed its actual image namespace");
        if (ok && receipt->source_raw)
            return q3p_end(p,raw_picture_emit(p,receipt->image,receipt->rect,receipt->uv,
                receipt->color,receipt,error));
        qa_material_source_scratch *source = NULL;
        if (ok && p->options.source_state) {
            source = p->options.source_state(p->options.context, error);
            ok = source != NULL;
        } else if (ok) source = p->options.source_scratch;
        if (ok && source) ok = qa_material_source_swap_end(source, frame, error);
        if (ok) ok = qa_scene_frame_picture_f(frame, receipt->image, receipt->viewport,
            receipt->rect, receipt->uv, receipt->color, error);
        return q3p_end(p, ok);
    }
    const qa_material *material;
    bool ok = q3p_shader_get(receipt->assets, receipt->shader, &material, error);
    if (ok && !material) material = qa_material_find(receipt->assets->options.provider.materials, "*default");
    if (ok && material != receipt->material)
        ok = q3p_fail(error, QA_ERROR_ARGUMENT, "Completed picture changed its actual registered material");
    if (ok) ok = picture_emit(p, material, receipt->rect, receipt->uv, receipt, error);
    return q3p_end(p, ok);
}

bool qa_q3_presentation_remap(qa_q3_presentation *p, const char *original,
                              const char *replacement, float offset, qa_error *error)
{
    if (!original || !replacement || !isfinite(offset) || !q3p_begin(p, error)) return false;
    bool ok;
    if (p->options.remap) ok = p->options.remap(p->options.context, original, replacement, offset, error);
    else {
        qa_q3_presentation_provider provider;
        qa_material_source_remap_status status = QA_MATERIAL_SOURCE_REMAP_APPLIED;
        ok = q3p_select(p->options.assets, original, QA_Q3_ASSET_SHADER, &provider, error) &&
            qa_material_remap_source(provider.materials, original, replacement, offset, &status, error);
        if (ok && status == QA_MATERIAL_SOURCE_REMAP_APPLIED && p->world)
            ok = qa_scene_world_remap_source(p->world, original, replacement, offset, &status, error);
        if (ok && status != QA_MATERIAL_SOURCE_REMAP_APPLIED && p->options.print) {
            char warning[512];
            snprintf(warning, sizeof(warning), "WARNING: R_RemapShader: %sshader %s not found\n",
                status == QA_MATERIAL_SOURCE_REMAP_TARGET_DEFAULT ? "new " : "",
                status == QA_MATERIAL_SOURCE_REMAP_TARGET_DEFAULT ? replacement : original);
            p->options.print(p->options.context, warning);
        }
    }
    return q3p_end(p, ok);
}
