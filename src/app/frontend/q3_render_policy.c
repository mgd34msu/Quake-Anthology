#include "q3_render_policy.h"
#include "shared_resource_policy.h"
#include "q3_color_policy.h"

static bool source_cluster_clear(void *context, qa_error *error)
{
    qa_frontend *f = context;
    qa_cvars *registry = f && f->application ? qa_application_cvars(f->application) : NULL;
    const qa_cvar_view *row = frontend_render_control_record(registry, "r_showcluster");
    if (!row || !row->value)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Source visibility lost its actual ENGINE cluster row");
    qa_cvars_clear_modified(registry, "r_showcluster");
    row = frontend_render_control_record(registry, "r_showcluster");
    return (row && !row->modified) ||
        frontend_fail(error, QA_ERROR_ARGUMENT, "Source visibility could not acknowledge its actual cluster row");
}
static bool source_cluster_modified(void *context, bool *out, qa_error *error)
{
    qa_frontend *f = context;
    const qa_cvar_view *row = f && f->application ?
        frontend_render_control_record(qa_application_cvars(f->application), "r_showcluster") : NULL;
    if (!out || !row || !row->value)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Source visibility lost its actual cluster modification cell");
    *out = row->modified;
    return true;
}

static bool permedia2(const qa_gl_capabilities *caps)
{
    if (!caps) return false;
    char renderer[sizeof(caps->renderer)];
    for (size_t i = 0; i < sizeof(renderer); ++i) {
        unsigned char c = (unsigned char)caps->renderer[i];
        renderer[i] = (char)(c >= 'A' && c <= 'Z' ? c + 'a' - 'A' : c);
    }
    renderer[sizeof(renderer) - 1] = 0;
    return strstr(renderer, "permedia2") != NULL;
}

static bool records(qa_frontend *f, const qa_cvars_edit *edit, const char *const *names,
    size_t count, const qa_cvar_view **out, qa_error *error)
{
    if (!f || !f->application || (edit &&
        (qa_cvars_edit_registry(edit) != qa_application_cvars(f->application) ||
         !qa_cvars_edit_returned_is(edit, qa_application_cvars(f->application)))))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Source renderer policy requires its actual ENGINE registry");
    for (size_t i = 0; i < count; ++i) {
        out[i] = edit ? qa_cvars_edit_canonical_record(edit, names[i]) :
            frontend_render_control_record(qa_application_cvars(f->application), names[i]);
        if (!out[i] || !out[i]->value)
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Source renderer policy lacks a physical canonical row");
    }
    return true;
}
bool frontend_q3_material_profile_initialize(qa_frontend *f, qa_material_library *library,
    qa_error *error)
{
    const qa_cvars_edit *edit = NULL;
    qa_material_profile profile;
    return frontend_q3_source_color_ensure(f, error) &&
        frontend_resource_policy_admission_edit(f, &edit, error) &&
        frontend_q3_material_profile_read(f, edit, &profile, error) &&
        qa_material_library_set_source_profile(library, &profile, error) &&
        frontend_q3_material_source_bind(f, library, error);
}
bool frontend_q3_material_source_bind(qa_frontend *f, qa_material_library *library, qa_error *error)
{
    if (!f || !f->application || !qa_material_library_has_source_profile(library))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Source material producers require their actual tagged owner");
    return qa_material_library_set_source_ui_fullscreen(library, frontend_q3_material_ui_fullscreen_read, f, error) &&
        qa_material_library_set_source_upload(library, frontend_q3_source_upload_read, f, error);
}
bool frontend_q3_material_ui_fullscreen_read(void *context, bool *out, qa_error *error)
{
    qa_frontend *f = context;
    const qa_cvars_edit *edit = NULL;
    static const char *const names[] = {"r_uifullscreen"};
    const qa_cvar_view *row;
    if (!out || !frontend_resource_policy_admission_edit(f, &edit, error) ||
        !records(f, edit, names, 1, &row, error)) return false;
    *out = row->integer != 0;
    return true;
}
bool frontend_q3_material_profile_read(qa_frontend *f, const qa_cvars_edit *edit,
    qa_material_profile *out, qa_error *error)
{
    static const char *const names[] = {"r_detailtextures", "r_vertexLight", "r_uifullscreen",
        "r_ignoreFastPath", "r_allowExtensions", "r_ext_multitexture", "r_ext_texture_env_add"};
    const qa_cvar_view *rows[sizeof(names)/sizeof(*names)];
    if (!out || !records(f, edit, names, sizeof(names)/sizeof(*names), rows, error)) return false;
    const qa_gl_capabilities *caps = f->gl ? qa_gl_capabilities_get(f->gl) : NULL;
    *out = (qa_material_profile){.detail_textures = rows[0]->integer != 0,
        .vertex_lighting = rows[1]->integer != 0, .ui_fullscreen = rows[2]->integer != 0,
        .ignore_fast_path = rows[3]->integer != 0,
        .permedia2 = permedia2(caps),
        .multitexture = rows[4]->integer != 0 && rows[5]->number != 0 &&
            (!f->gl || (caps && caps->texture_units >= 2)),
        .texture_env_add = rows[4]->integer != 0 && rows[6]->integer != 0};
    return true;
}
bool frontend_q3_renderer_options_read(qa_frontend *f, qa_q3_presentation_options *out,
    qa_error *error)
{
    static const char *const names[] = {"r_znear"};
    const qa_cvars_edit *edit = NULL;
    const qa_cvar_view *row;
    if (!out || !frontend_resource_policy_admission_edit(f, &edit, error) ||
        !records(f, edit, names, 1, &row, error)) return false;
    qa_q3_color_lighting lighting;
    if (!frontend_q3_source_color_lighting_read(f, edit, &lighting, error)) return false;
    out->near_clip = (float)row->number;
    out->identity_light = lighting.identity_light;
    return true;
}
bool frontend_q3_scene_policy_read(qa_frontend *f, qa_q3_scene_options *out, qa_error *error)
{
    static const char *const names[] = {"r_lodscale", "r_lodbias", "r_lodCurveError",
        "r_railCoreWidth", "r_railWidth", "r_railSegmentLength", "r_drawworld", "r_drawentities",
        "r_nocull", "r_novis", "r_nocurves", "r_facePlaneCull", "r_lockpvs", "r_noportals",
        "r_portalOnly", "r_fastsky", "r_dynamiclight", "r_vertexLight", "r_ambientScale",
        "r_directedScale", "r_znear", "r_norefresh", "r_showcluster"};
    const qa_cvar_view *rows[sizeof(names)/sizeof(*names)];
    if (!out || !records(f, NULL, names, sizeof(names)/sizeof(*names), rows, error)) return false;
    out->lod_scale = (float)rows[0]->number; out->lod_bias = (float)rows[1]->integer;
    out->world.curve_error = (float)rows[2]->number;
    out->rail = (qa_scene_rail_options){.core_width = rows[3]->integer,
        .ring_width = rows[4]->integer, .segment_length = (float)rows[5]->number};
    out->world.skip_world = rows[6]->integer == 0; out->no_entities = rows[7]->integer == 0;
    out->world.no_cull = rows[8]->integer != 0; out->world.no_vis = rows[9]->integer != 0;
    out->world.no_curves = rows[10]->integer != 0;
    out->world.disable_face_plane_cull = rows[11]->integer == 0; out->world.lock_pvs = rows[12]->integer != 0;
    out->no_portals = rows[13]->integer != 0; out->portal_only = rows[14]->integer != 0;
    out->world.fast_sky = rows[15]->integer;
    const qa_gl_capabilities *caps = f->gl ? qa_gl_capabilities_get(f->gl) : NULL;
    if (rows[16]->integer == 0 || rows[17]->integer == 1 || permedia2(caps)) {
        out->world.light_count = 0; out->world.projected_light_count = 0;
    }
    out->ambient_scale = (float)rows[18]->number; out->directed_scale = (float)rows[19]->number;
    out->near_clip = (float)rows[20]->number; out->no_refresh = rows[21]->integer != 0;
    out->world.source_show_cluster = rows[22]->integer != 0;
    out->world.source_show_cluster_modified = rows[22]->modified;
    out->world.source_cluster_clear = source_cluster_clear; out->world.source_cluster_context = f;
    out->world.source_cluster_modified = source_cluster_modified;
    qa_q3_color_lighting lighting;
    if (!frontend_q3_source_color_lighting_read(f, NULL, &lighting, error)) return false;
    out->world.identity_light = lighting.identity_light;
    return frontend_q3_material_diagnostics_read(f, &out->world.source_diagnostics, error);
}
bool frontend_q3_material_diagnostics_read(qa_frontend *f, qa_scene_source_diagnostics *out,
    qa_error *error)
{
    static const char *const names[] = {"r_debugSort", "r_showtris", "r_shownormals", "r_showsky",
        "r_nobind", "r_offsetfactor", "r_offsetunits", "r_fastsky"};
    const qa_cvar_view *rows[sizeof(names)/sizeof(*names)];
    if (!out || !records(f, NULL, names, sizeof(names)/sizeof(*names), rows, error)) return false;
    *out = (qa_scene_source_diagnostics){.debug_sort = rows[0]->integer,
        .show_triangles = rows[1]->integer != 0, .show_normals = rows[2]->integer != 0,
        .show_sky = rows[3]->integer != 0, .no_bind = rows[4]->integer != 0,
        .polygon_offset_factor = (float)rows[5]->number, .polygon_offset_units = (float)rows[6]->number,
        .fast_sky = rows[7]->integer};
    if (f->cpu && f->gl)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Source stencil capability requires one actual renderer");
    if (f->cpu) {
        qa_cpu_capabilities caps;
        if (!qa_cpu_capabilities_read(f->cpu, &caps, error)) return false;
        out->stencil_bits = (int32_t)caps.stencil_bits;
    } else if (f->gl) {
        const qa_gl_capabilities *caps = qa_gl_capabilities_get(f->gl);
        if (!caps || caps->stencil_bits > INT32_MAX)
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Source stencil capability lost its actual OpenGL visual");
        out->stencil_bits = (int32_t)caps->stencil_bits;
    }
    return frontend_q3_frame_policy_read(f, error);
}
bool frontend_q3_frame_policy_read(qa_frontend *f, qa_error *error)
{
    static const char *const names[] = {"r_skipBackEnd", "r_clear"};
    const qa_cvar_view *rows[2];
    if (!records(f, NULL, names, 2, rows, error)) return false;
    f->frame.source_backend = true;
    f->frame.source_skip_backend = rows[0]->integer != 0;
    f->frame.source_clear_draw_buffer = rows[1]->integer != 0;
    return true;
}
bool frontend_q3_world_policy_read(qa_frontend *f, const qa_cvars_edit *edit,
    qa_scene_world_options *out, qa_error *error)
{
    static const char *const names[] = {"r_subdivisions", "r_mapOverBrightBits"};
    const qa_cvar_view *rows[2];
    if (!out || !records(f, edit, names, 2, rows, error)) return false;
    qa_q3_color_lighting lighting;
    if (!frontend_q3_source_color_lighting_read(f, edit, &lighting, error) ||
        !qa_q3_color_map_shift(rows[1]->integer, &lighting, &out->q3_overbright, error)) return false;
    out->subdivisions = (float)rows[0]->number;
    return true;
}
bool frontend_q3_world_policy_initialize(qa_frontend *f, qa_scene_world_options *options,
    qa_error *error)
{
    const qa_cvars_edit *edit = NULL;
    return frontend_resource_policy_admission_edit(f, &edit, error) &&
        frontend_q3_world_policy_read(f, edit, options, error);
}
