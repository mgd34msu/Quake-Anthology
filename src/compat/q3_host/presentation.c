#include "internal.h"
#include "qa/q3_presentation.h"
#include "qa/font.h"
#include "qa/text.h"
#include "qa/q3_ui_client_state.h"

#include <math.h>

static qa_vec3 record_vector(const uint8_t *bytes)
{
    return (qa_vec3){qa_load_f32le(bytes), qa_load_f32le(bytes + 4),
                     qa_load_f32le(bytes + 8)};
}

bool qa_q3_host_ref_entity_decode(qa_bytes source, qa_q3_ref_entity *out, qa_error *error)
{
    if (!out || !source.data || source.size != 140)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 refEntity requires its complete original record");
    const uint8_t *bytes = source.data;
    int32_t kind = qa_load_i32le(bytes);
    if (kind < QA_Q3_REF_MODEL || kind > QA_Q3_REF_PORTAL)
        return q3_fail(error, QA_ERROR_FORMAT, 0, "invalid Q3 render entity kind");
    qa_q3_ref_entity entity = {
        .kind = (qa_q3_ref_kind)kind,
        .flags = qa_load_i32le(bytes + 4), .model = qa_load_i32le(bytes + 8),
        .lighting_origin = record_vector(bytes + 12),
        .shadow_plane = qa_load_f32le(bytes + 24),
        .non_normalized_axes = qa_load_i32le(bytes + 64) != 0,
        .origin = record_vector(bytes + 68), .frame = qa_load_i32le(bytes + 80),
        .old_origin = record_vector(bytes + 84), .old_frame = qa_load_i32le(bytes + 96),
        .back_lerp = qa_load_f32le(bytes + 100), .skin = qa_load_i32le(bytes + 104),
        .custom_skin = qa_load_i32le(bytes + 108),
        .custom_shader = qa_load_i32le(bytes + 112),
        .shader_texcoord = {qa_load_f32le(bytes + 120), qa_load_f32le(bytes + 124)},
        .shader_time = qa_load_f32le(bytes + 128), .radius = qa_load_f32le(bytes + 132),
        .rotation = qa_load_f32le(bytes + 136),
    };
    for (size_t i = 0; i < 3; ++i)
        entity.axis[i] = record_vector(bytes + 28 + i * 12);
    memcpy(entity.color, bytes + 116, sizeof(entity.color));
    *out = entity;
    return true;
}

static bool ref_entity(q3_call *call, qa_q3_ref_entity *out, qa_error *error)
{
    uint8_t bytes[140];
    return q3_read(call, call->arguments[0], bytes, sizeof(bytes), error) &&
        qa_q3_host_ref_entity_decode((qa_bytes){bytes, sizeof(bytes)}, out, error);
}

static bool refdef(q3_call *call, qa_q3_refdef *out, qa_error *error)
{
    uint8_t bytes[368];
    if (!q3_read(call, call->arguments[0], bytes, sizeof(bytes), error))
        return false;
    qa_q3_refdef view = {
        .x = qa_load_i32le(bytes), .y = qa_load_i32le(bytes + 4),
        .width = qa_load_i32le(bytes + 8), .height = qa_load_i32le(bytes + 12),
        .fov_x = qa_load_f32le(bytes + 16), .fov_y = qa_load_f32le(bytes + 20),
        .origin = record_vector(bytes + 24), .time = qa_load_i32le(bytes + 72),
        .flags = qa_load_i32le(bytes + 76),
    };
    for (size_t i = 0; i < 3; ++i)
        view.axis[i] = record_vector(bytes + 36 + i * 12);
    memcpy(view.area_mask, bytes + 80, sizeof(view.area_mask));
    memcpy(view.text, bytes + 112, sizeof(view.text));
    for (size_t i = 0; i < 8; ++i)
        if (!memchr(view.text[i], 0, sizeof(view.text[i])))
            return q3_fail(error, QA_ERROR_FORMAT, 112 + i * 32,
                            "Q3 render text row is unterminated");
    *out = view;
    return true;
}

bool qa_q3_host_render_scope_current(const qa_q3_host *host, const qa_qvm_call *call,
    const void *lifetime, uint64_t service_owner, qa_qvm_role role,
    const qa_q3_presentation *presentation)
{
    const q3_call *entered = host ? host->render_call : NULL;
    return host && entered && entered->host == host && host->calls &&
        !host->retired && !host->restore_pending &&
        entered->source_call == call && (call ? entered->vm == host->vm : entered->native != NULL) &&
        role != QA_QVM_GAME && host->options.role == role &&
        lifetime && host->options.frontend_lifetime == lifetime &&
        service_owner && host->options.service_owner == service_owner &&
        presentation && host->options.presentation.seat == presentation;
}

bool qa_q3_host_system_movie_scope_current(const qa_q3_host *host, const qa_qvm_call *call,
    const void *lifetime, uint64_t service_owner, qa_qvm_role role,
    const qa_q3_presentation *presentation)
{
    const q3_call *entered = host ? host->system_movie_call : NULL;
    return host && entered && entered->host == host && host->calls &&
        !host->retired && !host->restore_pending &&
        entered->source_call == call && (call ? entered->vm && entered->vm == host->vm :
            entered->native && entered->native == host->native && entered->native_host == host->native_host) &&
        role != QA_QVM_GAME && host->options.role == role &&
        entered->service == (role == QA_QVM_UI ? 75 : 74) && (entered->arguments[5] & 1u) &&
        lifetime && host->options.frontend_lifetime == lifetime &&
        service_owner && host->options.service_owner == service_owner &&
        presentation && host->options.presentation.seat == presentation;
}

static bool system_movie_open(void *context, const qa_q3_movie_request *request,
    qa_q3_system_movie *out, qa_error *error)
{
    const q3_call *call = context;
    qa_q3_host *host = call->host;
    const qa_q3_host_presentation_services *services = &host->options.presentation;
    if (!services->system_movie_context || !services->system_movie || host->system_movie_call)
        return q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 system cinematic owner is unbound or entered");
    host->system_movie_call = call;
    bool ok = services->system_movie(services->system_movie_context, host, call->source_call,
        request, out, error);
    host->system_movie_call = NULL;
    return ok;
}

static bool render(q3_call *call, qa_q3_presentation *presentation,
    const qa_q3_refdef *definition, qa_error *error)
{
    qa_q3_host *host = call->host;
    const qa_q3_host_render_services *services = &host->options.render;
    if (!!services->enter != !!services->leave || host->render_call)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 RenderScene requires paired callbacks and a returned host scope");
    host->render_call = call;
    void *token = NULL;
    bool okay = !services->enter || services->enter(services->context, host,
        call->source_call, definition, &token, error);
    if (okay) okay = qa_q3_presentation_render(presentation, definition, error);
    if (services->enter) services->leave(services->context, token, okay);
    host->render_call = NULL;
    return okay;
}

static bool polygons(q3_call *call, qa_q3_presentation *seat, bool multiple,
                      qa_error *error)
{
    int32_t shader = q3_integer(call, 0), count = q3_integer(call, 1);
    int32_t groups = multiple ? q3_integer(call, 3) : 1;
    if (shader == 0 || count <= 0 || groups <= 0)
        return true;
    size_t vertices = (size_t)count;
    if (vertices > SIZE_MAX / 24 || (size_t)groups > SIZE_MAX / (vertices * 24) ||
        vertices > SIZE_MAX / sizeof(qa_q3_poly_vertex))
        return q3_fail(error, QA_ERROR_MEMORY, 0, "Q3 polygon extent overflows");
    q3_record source;
    if (!q3_record_open(call, call->arguments[2], vertices * 24 * (size_t)groups,
                         &source, error))
        return false;
    qa_q3_poly_vertex *polygon = qa_arena_alloc(&call->host->scratch,
        vertices * sizeof(*polygon), _Alignof(qa_q3_poly_vertex), error);
    if (!polygon)
        return false;
    for (int32_t group = 0; group < groups; ++group) {
        const uint8_t *bytes = source.abi.bytes.data + (size_t)group * vertices * 24;
        for (size_t i = 0; i < vertices; ++i) {
            const uint8_t *vertex = bytes + i * 24;
            polygon[i].position = record_vector(vertex);
            polygon[i].texcoord = (qa_scene_vec2){qa_load_f32le(vertex + 12),
                                                 qa_load_f32le(vertex + 16)};
            memcpy(polygon[i].color, vertex + 20, 4);
        }
        if (!qa_q3_presentation_poly(seat, shader, polygon, vertices, error))
            return false;
    }
    return true;
}

static bool tag(q3_call *call, qa_q3_presentation_assets *assets, bool ui,
                 int32_t *result, qa_error *error)
{
    q3_record destination;
    if (!q3_record_open(call, call->arguments[0], 48, &destination, error))
        return false;
    bool has_tags, found = false;
    int32_t model = q3_integer(call, 1);
    if (!qa_q3_presentation_model_has_tags(assets, model, &has_tags, error))
        return false;
    qa_model_tag value = {.axes = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    if (has_tags) {
        qa_buffer name = {0};
        if (!q3_string(call, call->arguments[5], &name, error))
            return false;
        bool ok = qa_q3_presentation_tag(assets, model, (const char *)name.data,
            q3_integer(call, 2), q3_integer(call, 3), q3_float(call, 4),
            &value, &found, error);
        qa_buffer_free(&name);
        if (!ok)
            return false;
    }
    qa_vec3 origin = {value.origin[0], value.origin[1], value.origin[2]};
    if (!q3_write_vector(call, call->arguments[0], origin, error))
        return false;
    for (size_t i = 0; i < 3; ++i) {
        qa_vec3 axis = {value.axes[i][0], value.axes[i][1], value.axes[i][2]};
        if (!q3_write_vector(call, call->arguments[0] + 12 + i * 12,
                              axis, error))
            return false;
    }
    *result = ui ? 0 : found;
    return true;
}

typedef struct font_images {
    qa_q3_presentation_assets *assets;
    qa_error error;
    bool failed;
} font_images;

static int32_t font_image(void *opaque, const qa_scene_image *image)
{
    font_images *images = opaque;
    int32_t handle = 0;
    if (!images->failed &&
        !qa_q3_register_picture_image(images->assets, image, &handle, &images->error))
        images->failed = true;
    return handle;
}

static bool font(q3_call *call, qa_error *error)
{
    qa_q3_host_presentation_services *services = &call->host->options.presentation;
    if (!services->fonts)
        return q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 font owner is unbound");
    q3_record destination;
    if (!q3_record_open(call, call->arguments[2], QA_Q3_FONT_RECORD_BYTES,
                         &destination, error))
        return false;
    qa_buffer path = {0};
    if (!q3_string(call, call->arguments[0], &path, error))
        return false;
    qa_font_q3_options options = {.point_size = q3_integer(call, 1),
        .truetype_path = (const char *)path.data, .generate_if_missing = true};
    const qa_font *registered;
    bool ok = qa_font_q3_register(services->fonts, &options, &registered, error);
    qa_buffer_free(&path);
    if (!ok)
        return false;
    uint8_t *record = qa_arena_alloc(&call->host->scratch, QA_Q3_FONT_RECORD_BYTES, 1, error);
    font_images images = {.assets = qa_q3_presentation_resources(services->seat)};
    if (!record || !qa_font_q3_export(registered, font_image, &images, record, error))
        return false;
    if (images.failed) {
        if (error) *error = images.error;
        return false;
    }
    return q3_write(call, call->arguments[2], (qa_bytes){record, QA_Q3_FONT_RECORD_BYTES}, error);
}

static bool remap(q3_call *call, qa_q3_presentation *seat, qa_error *error)
{
    qa_buffer original = {0}, replacement = {0}, time = {0};
    bool ok = q3_string(call, call->arguments[0], &original, error) &&
              q3_string(call, call->arguments[1], &replacement, error);
    if (ok && call->arguments[2])
        ok = q3_string(call, call->arguments[2], &time, error);
    if (ok) {
        float offset;
        ok = qa_parse_atof_float(time.data ? (const char *)time.data : "", &offset, error) &&
            qa_q3_presentation_remap(seat, (const char *)original.data,
                                      (const char *)replacement.data, offset, error);
    }
    qa_buffer_free(&original);
    qa_buffer_free(&replacement);
    qa_buffer_free(&time);
    return ok;
}

static qa_scene_rect_f movie_rect(const q3_call *call)
{
    return (qa_scene_rect_f){(float)q3_integer(call, 1), (float)q3_integer(call, 2),
                              (float)q3_integer(call, 3), (float)q3_integer(call, 4)};
}

static bool ui_client_state(q3_call *call, qa_error *error)
{
    const qa_q3_host_client_services *services = &call->host->options.client;
    if (!services->ui_state_context || !services->ui_state)
        return q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 UI client-state owner is unbound");
    q3_record destination;
    if (!q3_record_open(call, call->arguments[0], QA_Q3_UI_CLIENT_STATE_BYTES,
                         &destination, error)) return false;
    qa_q3_ui_client_state state = {0};
    if (!services->ui_state(services->ui_state_context, call->host, &state, error)) return false;
    const char *server_end = memchr(state.server_name, 0, sizeof(state.server_name));
    const char *update_end = memchr(state.update_info, 0, sizeof(state.update_info));
    const char *message_end = memchr(state.message, 0, sizeof(state.message));
    if (state.phase < 0 || state.phase > 9 || !server_end || !update_end || !message_end)
        return q3_fail(error, QA_ERROR_FORMAT, 0, "Q3 UI client-state receipt is invalid");
    uint8_t record[QA_Q3_UI_CLIENT_STATE_BYTES] = {0};
    qa_store_u32le(record, (uint32_t)state.phase);
    qa_store_u32le(record + 4, (uint32_t)state.connect_packet_count);
    qa_store_u32le(record + 8, (uint32_t)state.client_number);
    memcpy(record + 12, state.server_name, (size_t)(server_end - state.server_name));
    memcpy(record + 1036, state.update_info, (size_t)(update_end - state.update_info));
    memcpy(record + 2060, state.message, (size_t)(message_end - state.message));
    return q3_write(call, call->arguments[0], (qa_bytes){record, sizeof(record)}, error);
}

q3_service_result q3_presentation(q3_call *call, int32_t *result, qa_error *error)
{
    qa_qvm_role role = call->host->options.role;
    if (role == QA_QVM_GAME)
        return Q3_UNHANDLED;
    bool ui = role == QA_QVM_UI;
    int32_t service = call->service;
    int32_t first_movie = ui ? 75 : 74;
    bool owned = ui ? ((service >= 18 && service <= 29) || service == 31 || service == 32 ||
                       service == 43 || service == 44 || service == 55 || service == 56 || service == 62 ||
                       service == 63 || (service >= 75 && service <= 80))
                   : ((service >= 28 && service <= 49) || service == 57 || service == 58 ||
                       service == 69 || service == 73 || (service >= 74 && service <= 81) ||
                       service == 85 || service == 87 || service == 88 || service == 17);
    if (!owned)
        return Q3_UNHANDLED;
    qa_q3_host_presentation_services *services = &call->host->options.presentation;
    qa_q3_presentation *seat = services->seat;
    if (service == (ui ? 28 : 17)) {
        if (!services->update_screen)
            return q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 screen owner is unbound"), Q3_FAILED;
        return services->update_screen(services->context, error) ? Q3_COMPLETED : Q3_FAILED;
    }
    if (ui && service == 44)
        return ui_client_state(call, error) ? Q3_COMPLETED : Q3_FAILED;
    if (service == (ui ? 43 : 49)) {
        if (!services->configuration)
            return q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 display configuration is unbound"), Q3_FAILED;
        q3_record destination;
        bool legacy = call->host->options.abi == QA_QVM_Q3_116N;
        size_t size = legacy ? 4164 : 11332;
        uint8_t *configuration = qa_arena_alloc(&call->host->scratch, 11332, 1, error);
        if (!configuration || !q3_record_open(call, call->arguments[0], size,
                                               &destination, error))
            return Q3_FAILED;
        if (!services->configuration(services->context, configuration, error)) return Q3_FAILED;
        if (legacy) {
            configuration[4095] = 0;
            memmove(configuration + 4096, configuration + 11264, 68);
        }
        return q3_write(call, call->arguments[0], (qa_bytes){configuration, size}, error)
            ? Q3_COMPLETED : Q3_FAILED;
    }
    if (!seat)
        return q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 presentation seat is unbound"), Q3_FAILED;
    qa_q3_presentation_assets *assets = qa_q3_presentation_resources(seat);
    bool ok = false;
    if (service == (ui ? 18 : 37) || service == (ui ? 19 : 38) ||
        service == (ui ? 20 : 57) || (!ui && service == 39) ||
        service == (ui ? 31 : 34)) {
        qa_buffer name = {0};
        if (call->arguments[0] && !q3_string(call, call->arguments[0], &name, error))
            return Q3_FAILED;
        const char *text = name.data ? (const char *)name.data : "";
        if (service == (ui ? 18 : 37)) ok = qa_q3_register_model(assets, text, result, error);
        else if (service == (ui ? 19 : 38)) ok = qa_q3_register_skin(assets, text, result, error);
        else if (service == (ui ? 31 : 34)) ok = qa_q3_register_sound(assets, text,
            call->host->options.abi == QA_QVM_Q3_MODERN && q3_integer(call, 1) != 0, result, error);
        else ok = qa_q3_register_shader(assets, text, !ui && service == 39, result, error);
        qa_buffer_free(&name);
    } else if (service == (ui ? 21 : 40)) {
        ok = qa_q3_presentation_clear(seat, error);
    } else if (service == (ui ? 22 : 41)) {
        qa_q3_ref_entity entity;
        bool suppress = false;
        ok = ref_entity(call, &entity, error);
        if (ok && !ui && call->source_call && call->host->options.source_entity) {
            int32_t pointer;
            ok = qa_qvm_call_argument(call->source_call, 0, &pointer, error) &&
                call->host->options.source_entity(call->host->options.source_entity_context,
                    call->source_call, pointer, &entity, &suppress, error);
        }
        if (ok && !suppress) ok = qa_q3_presentation_entity(seat, &entity, error);
    } else if (service == (ui ? 23 : 42) || (!ui && service == 87)) {
        ok = polygons(call, seat, service == 87, error);
    } else if (service == (ui ? 24 : 43) || (!ui && service == 85)) {
        float radius = q3_float(call, 1);
        if (radius <= 0.0f) ok = true;
        else {
            qa_vec3 position;
            ok = q3_vector(call, call->arguments[0], &position, error) &&
                qa_q3_presentation_light(seat, position, radius,
                    (qa_vec3){q3_float(call, 2), q3_float(call, 3), q3_float(call, 4)},
                    service == 85, error);
        }
    } else if (service == (ui ? 25 : 44)) {
        qa_q3_refdef view;
        ok = refdef(call, &view, error) && render(call, seat, &view, error);
    } else if (service == (ui ? 26 : 45)) {
        qa_scene_vec4 color;
        uint8_t bytes[16];
        if (call->arguments[0]) {
            if (!q3_read(call, call->arguments[0], bytes, sizeof(bytes), error))
                return Q3_FAILED;
            color = (qa_scene_vec4){qa_load_f32le(bytes), qa_load_f32le(bytes + 4),
                                    qa_load_f32le(bytes + 8), qa_load_f32le(bytes + 12)};
        }
        qa_q3_presentation_color(seat, call->arguments[0] ? &color : NULL);
        ok = true;
    } else if (service == (ui ? 27 : 46)) {
        ok = qa_q3_presentation_picture(seat, q3_integer(call, 8),
            (qa_scene_rect_f){q3_float(call, 0), q3_float(call, 1), q3_float(call, 2), q3_float(call, 3)},
            (qa_scene_vec4){q3_float(call, 4), q3_float(call, 5), q3_float(call, 6), q3_float(call, 7)}, error);
    } else if (service == (ui ? 56 : 47)) {
        q3_record minimum, maximum;
        qa_bounds bounds;
        ok = q3_record_open(call, call->arguments[1], 12, &minimum, error) &&
            q3_record_open(call, call->arguments[2], 12, &maximum, error) &&
            qa_q3_presentation_model_bounds(assets, q3_integer(call, 0), &bounds, error) &&
            q3_write_vector(call, call->arguments[1], bounds.mins, error) &&
            q3_write_vector(call, call->arguments[2], bounds.maxs, error);
    } else if (service == (ui ? 29 : 48)) {
        ok = tag(call, assets, ui, result, error);
    } else if (service == (ui ? 55 : 58)) {
        ok = font(call, error);
    } else if (service == (ui ? 80 : 79)) {
        ok = remap(call, seat, error);
    } else if (service == (ui ? 32 : 29)) {
        int32_t sound = q3_integer(call, 0);
        ok = !qa_q3_presentation_sound_valid(seat, sound) ||
            qa_q3_presentation_sound(seat, sound, NULL, 0, q3_integer(call, 1), true, error);
    } else if (service == (ui ? 63 : 35)) {
        qa_buffer intro = {0}, loop = {0};
        ok = (!call->arguments[0] || q3_string(call, call->arguments[0], &intro, error)) &&
             (!call->arguments[1] || q3_string(call, call->arguments[1], &loop, error));
        if (ok) ok = qa_q3_presentation_music(seat, intro.data ? (const char *)intro.data : "",
                                               loop.data ? (const char *)loop.data : "", error);
        qa_buffer_free(&intro); qa_buffer_free(&loop);
    } else if (service == (ui ? 62 : 69)) {
        ok = qa_q3_presentation_music(seat, "", "", error);
    } else if (service >= first_movie && service <= first_movie + 4) {
        if (service == first_movie) {
            qa_buffer path = {0};
            if (!q3_string(call, call->arguments[0], &path, error)) return Q3_FAILED;
            uint32_t flags = (uint32_t)q3_integer(call, 5);
            ok = flags & 1u ? qa_q3_presentation_movie_play_system(seat, (const char *)path.data,
                flags, system_movie_open, call, result, error) :
                qa_q3_presentation_movie_play(seat, (const char *)path.data,
                    movie_rect(call), flags, result, error);
            qa_buffer_free(&path);
        } else if (service == first_movie + 1) {
            ok = qa_q3_presentation_movie_stop(seat, q3_integer(call, 0), true, error);
            if (ok) *result = 2;
        } else if (service == first_movie + 2) {
            ok = qa_q3_presentation_movie_run(seat, q3_integer(call, 0), result, error);
        } else if (service == first_movie + 3) {
            ok = qa_q3_presentation_movie_draw(seat, q3_integer(call, 0), error);
        } else {
            qa_q3_presentation_movie_extents(seat, q3_integer(call, 0), movie_rect(call));
            ok = true;
        }
    } else if (!ui && service == 36) {
        qa_buffer path = {0};
        if (!q3_string(call, call->arguments[0], &path, error)) return Q3_FAILED;
        ok = qa_q3_presentation_load_world(seat, (const char *)path.data, error);
        qa_buffer_free(&path);
    } else if (!ui && service == 88) {
        qa_vec3 first, second;
        bool visible;
        ok = q3_vector(call, call->arguments[0], &first, error) &&
            q3_vector(call, call->arguments[1], &second, error) &&
            qa_q3_presentation_in_pvs(seat, first, second, &visible, error);
        if (ok) *result = visible;
    } else if (!ui && service == 28) {
        int32_t entity = q3_integer(call, 1), sound = q3_integer(call, 3);
        if (!call->arguments[0] && (entity < 0 || entity > 1024))
            return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 sound entity is out of range"), Q3_FAILED;
        if (!qa_q3_presentation_sound_valid(seat, sound)) ok = true;
        else {
            qa_vec3 origin;
            ok = !call->arguments[0] || q3_vector(call, call->arguments[0], &origin, error);
            if (ok) ok = qa_q3_presentation_sound(seat, sound,
                call->arguments[0] ? &origin : NULL, entity, q3_integer(call, 2), false, error);
        }
    } else if (!ui && service == 30) {
        ok = qa_q3_presentation_clear_loops(seat,
            call->host->options.abi == QA_QVM_Q3_116N || q3_integer(call, 0) != 0, error);
    } else if (!ui && (service == 31 || service == 80)) {
        int32_t sound = q3_integer(call, 3);
        if (!qa_q3_presentation_sound_valid(seat, sound)) ok = true;
        else {
            qa_vec3 origin, velocity;
            ok = q3_vector(call, call->arguments[1], &origin, error) &&
                q3_vector(call, call->arguments[2], &velocity, error) &&
                qa_q3_presentation_loop(seat, sound, q3_integer(call, 0), origin,
                                          velocity, service == 80, error);
        }
    } else if (!ui && service == 32) {
        qa_vec3 origin;
        ok = q3_vector(call, call->arguments[1], &origin, error) &&
            qa_q3_presentation_sound_position(seat, q3_integer(call, 0), origin, error);
    } else if (!ui && service == 33) {
        qa_vec3 origin, axis[3];
        ok = q3_vector(call, call->arguments[1], &origin, error);
        for (size_t i = 0; ok && i < 3; ++i)
            ok = q3_vector(call, call->arguments[2] + i * 12, &axis[i], error);
        if (ok) ok = qa_q3_presentation_listener(seat, q3_integer(call, 0), origin, axis, error);
    } else if (!ui && service == 81) {
        ok = qa_q3_presentation_stop_loop(seat, q3_integer(call, 0), error);
    } else if (!ui && service == 73) {
        qa_vec3 point, ambient = {0}, directed = {0}, direction = {0};
        q3_record outputs[3];
        ok = q3_vector(call, call->arguments[0], &point, error);
        for (size_t i = 0; ok && i < 3; ++i)
            ok = q3_record_open(call, call->arguments[i + 1], 12, &outputs[i], error);
        if (ok) {
            *result = call->host->options.scene_world &&
                qa_scene_world_sample_light(call->host->options.scene_world, point,
                                              &ambient, &directed, &direction);
            ambient = qa_vec_scale(ambient, 255.0f);
            directed = qa_vec_scale(directed, 255.0f);
            ok = q3_write_vector(call, call->arguments[1], ambient, error) &&
                 q3_write_vector(call, call->arguments[2], directed, error) &&
                 q3_write_vector(call, call->arguments[3], direction, error);
        }
    }
    return ok ? Q3_COMPLETED : Q3_FAILED;
}
