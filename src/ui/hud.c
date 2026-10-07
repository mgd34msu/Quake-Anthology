#include "internal.h"
#include "qa/hud.h"
#include "qa/application_q1_composition.h"
#include <stdio.h>

typedef struct hud_message { char *text; uint64_t starts, until, character_ns; bool chat, instant; uint32_t lines; } hud_message;
struct qa_hud {
    qa_hud_options options;
    hud_message *notices, *centers;
    size_t notice_count, notice_capacity, center_count, center_capacity;
    char *pickup;
    const qa_scene_image *pickup_icon;
    uint64_t pickup_until, hit_until;
    float hit_damage;
    qa_builtin_ctf_status ctf_status;
    qa_actor_id ctf_actor;
    qa_actor_owner ctf_source;
    uint64_t ctf_time_ns;
    bool ctf_present;
    qa_builtin_ctf_capture ctf_capture;
    uint64_t ctf_capture_time_ns, ctf_capture_until_ns;
    bool ctf_capture_present;
    bool drawing;
};
bool qa_hud_idle(const qa_hud *hud) { return hud && !hud->drawing; }
static uint64_t after(uint64_t now, uint64_t duration);
static uint64_t after(uint64_t now, uint64_t duration) {
    return UINT64_MAX - now < duration ? UINT64_MAX : now + duration;
}
static char *copy_text(const char *text, qa_error *error) {
    if (!text) { ui_fail(error, "missing HUD text"); return NULL; }
    size_t length = strlen(text);
    char *copy = malloc(length + 1);
    if (!copy) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating HUD text"); return NULL; }
    memcpy(copy, text, length + 1);
    return copy;
}
bool qa_hud_create(const qa_hud_options *options, qa_hud **out, qa_error *error) {
    if (!options || !out || !options->ui || !options->application ||
        options->ui->options.seat != options->seat)
        return ui_fail(error, "HUD requires matching seat UI and application");
    qa_hud *hud = calloc(1, sizeof(*hud));
    if (!hud) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating seat HUD"); return false; }
    hud->options = *options;
    *out = hud;
    return true;
}
bool qa_hud_clear_notify(qa_hud *hud, qa_error *error) {
    if (!hud) return true;
    if (hud->drawing) return ui_fail(error, "HUD callback is active");
    for (size_t i = 0; i < hud->notice_count; ++i) free(hud->notices[i].text);
    hud->notice_count = 0;
    return true;
}
bool qa_hud_clear_center(qa_hud *hud, qa_error *error) {
    if (!hud) return true;
    if (hud->drawing) return ui_fail(error, "HUD callback is active");
    for (size_t i = 0; i < hud->center_count; ++i) free(hud->centers[i].text);
    hud->center_count = 0;
    return true;
}
bool qa_hud_destroy(qa_hud *hud, qa_error *error) {
    if (!hud) return true;
    if (hud->drawing) return ui_fail(error, "HUD callback is active");
    qa_hud_clear_notify(hud, NULL); qa_hud_clear_center(hud, NULL);
    free(hud->notices); free(hud->centers); free(hud->pickup); qa_scene_image_release(hud->pickup_icon); free(hud);
    return true;
}
bool qa_hud_notify(qa_hud *hud, const char *text, bool chat, uint64_t starts,
                    uint64_t duration, qa_error *error) {
    if (!hud || hud->drawing) return ui_fail(error, "HUD message callback is active");
    char *copy = copy_text(text, error);
    if (!copy) return false;
    if (!ui_reserve((void **)&hud->notices, &hud->notice_capacity, hud->notice_count + 1,
                     sizeof(*hud->notices), error)) { free(copy); return false; }
    hud->notices[hud->notice_count++] = (hud_message){.text = copy, .starts = starts,
        .until = after(starts, duration), .chat = chat, .instant = true};
    return true;
}
bool qa_hud_center_print(qa_hud *hud, const char *text, uint64_t starts, uint64_t duration,
                          bool instant, uint64_t character_ns, qa_error *error) {
    if (!hud || hud->drawing) return ui_fail(error, "HUD center callback is active");
    char *copy = copy_text(text, error);
    if (!copy) return false;
    if (!ui_reserve((void **)&hud->centers, &hud->center_capacity, hud->center_count + 1,
                     sizeof(*hud->centers), error)) { free(copy); return false; }
    if (instant) qa_hud_clear_center(hud, NULL);
    else if (hud->center_count && starts < hud->centers[hud->center_count - 1].until)
        starts = hud->centers[hud->center_count - 1].until;
    uint32_t lines = 1;
    for (const char *p = text; *p && lines <= 4; ++p) if (*p == '\n') ++lines;
    if (!instant) {
        qa_bytes bytes = {(const uint8_t *)text, strlen(text)};
        size_t cursor = 0; uint32_t scalar;
        while (qa_utf8_next(bytes, &cursor, &scalar)) duration = after(duration, character_ns);
    }
    hud->centers[hud->center_count++] = (hud_message){.text = copy, .starts = starts,
        .until = after(starts, duration), .instant = instant, .character_ns = character_ns, .lines = lines};
    return true;
}
bool qa_hud_pickup(qa_hud *hud, const char *text, const qa_scene_image *icon, uint64_t until,
                    qa_error *error) {
    if (!hud || hud->drawing) return ui_fail(error, "HUD pickup callback is active");
    char *copy = copy_text(text, error); if (!copy) return false;
    qa_scene_image_retain(icon); qa_scene_image_release(hud->pickup_icon);
    free(hud->pickup); hud->pickup = copy; hud->pickup_icon = icon; hud->pickup_until = until;
    return true;
}
void qa_hud_hit_marker(qa_hud *hud, float damage, uint64_t until) {
    if (hud && !hud->drawing && isfinite(damage)) { hud->hit_damage = damage; hud->hit_until = until; }
}
bool qa_hud_ctf_status(qa_hud *hud, const qa_builtin_event *event, qa_error *error) {
    if (!hud || hud->drawing || !event ||
        event->kind != QA_BUILTIN_CTF_STATUS || event->family != QA_GAME_Q1 || event->argument_count ||
        !event->provider || !qa_application_provider_instance(hud->options.application, event->provider) ||
        !qa_actors_get(qa_session_actors(qa_application_session(hud->options.application)), event->actor) ||
        !isfinite(event->ctf_status.red) || !isfinite(event->ctf_status.blue) ||
        !isfinite(event->ctf_status.flags) || !isfinite(event->ctf_status.rune_items))
        return ui_fail(error, "CTF HUD status requires its actual source and full recipient");
    if (!hud->ctf_present || hud->ctf_source != event->provider ||
        !qa_actor_id_equal(hud->ctf_actor, event->actor)) hud->ctf_capture_present = false;
    hud->ctf_status = event->ctf_status;
    hud->ctf_actor = event->actor; hud->ctf_source = event->provider;
    hud->ctf_time_ns = event->time_ns; hud->ctf_present = true;
    return true;
}
bool qa_hud_ctf_capture(qa_hud *hud, const qa_builtin_event *event,
    qa_actor_id recipient, qa_error *error) {
    if (!hud || hud->drawing || !event ||
        event->kind != QA_BUILTIN_CTF_CAPTURE || event->family != QA_GAME_Q1 ||
        !event->provider || event->argument_count || event->text || event->resource ||
        !qa_actor_id_equal(event->actor, (qa_actor_id){0}) ||
        !qa_actor_id_equal(event->other, (qa_actor_id){0}) || !isfinite(event->ctf_capture.total))
        return ui_fail(error, "CTF capture requires its typed source-wide total");
    uint64_t time_ns;
    bool found;
    if (!qa_application_q1_ctf_recipient_read(hud->options.application, event->provider,
            recipient, &time_ns, &found, error)) return false;
    if (!found || event->time_ns > time_ns)
        return ui_fail(error, "CTF capture lost its actual source recipient or clock");
    if (!hud->ctf_present || hud->ctf_source != event->provider ||
        !qa_actor_id_equal(hud->ctf_actor, recipient)) hud->ctf_status = (qa_builtin_ctf_status){0};
    if (event->ctf_capture.blue) hud->ctf_status.blue = event->ctf_capture.total;
    else hud->ctf_status.red = event->ctf_capture.total;
    hud->ctf_actor = recipient; hud->ctf_source = event->provider;
    hud->ctf_time_ns = event->time_ns; hud->ctf_present = true;
    hud->ctf_capture = event->ctf_capture;
    hud->ctf_capture_time_ns = event->time_ns;
    hud->ctf_capture_until_ns = after(event->time_ns, UINT64_C(3000000000));
    hud->ctf_capture_present = true;
    return true;
}
static void expire(hud_message *messages, size_t *count, uint64_t now) {
    size_t retained = 0;
    for (size_t i = 0; i < *count; ++i) {
        if (messages[i].until <= now) free(messages[i].text);
        else messages[retained++] = messages[i];
    }
    *count = retained;
}
static bool text(qa_hud *hud, qa_scene_frame *scene, qa_scene_rect target, float x, float y,
                  const char *value, qa_scene_vec4 color, float scale, qa_font_alignment align,
                  qa_error *error) {
    return ui_draw_text(hud->options.ui, scene, target, x, y, value, color, scale, align, error);
}
static bool icon(qa_hud *hud, qa_scene_frame *scene, qa_scene_rect target,
                  const qa_scene_image *image, qa_scene_rect_f rect, qa_scene_vec4 color,
                  qa_error *error) {
    if (!image) return true;
    qa_ui *ui = hud->options.ui;
    qa_scene_rect_f pixels = {ui->bias_x + rect.x * ui->scale, ui->bias_y + rect.y * ui->scale,
                             rect.width * ui->scale, rect.height * ui->scale};
    return qa_scene_frame_picture_f(scene, image, target, pixels,
        (qa_scene_vec4){0, 0, 1, 1}, color, error);
}
static bool number(qa_hud *hud, qa_scene_frame *scene, qa_scene_rect target, float x, float y,
                    const char *label, double value, bool warning, qa_error *error) {
    char numeric[32];
    if (!qa_format_number(value, numeric, error)) return false;
    qa_ui *ui = hud->options.ui;
    qa_font_info font;
    if (!qa_font_describe(ui->options.fonts.primary ? ui->options.fonts.primary : ui->options.fonts.classic, &font))
        return ui_fail(error, "HUD value lost its retained font metrics");
    float cap = font.has_cap_ink ? font.cap_height : 8, top = font.has_cap_ink ? font.cap_top : 0;
    if (!isfinite(cap) || cap <= 0 || !isfinite(top))
        return ui_fail(error, "HUD value has invalid retained cap metrics");
    float text_scale = ui->text_scale * 1.5f;
    bool compact = ui->scale * text_scale * cap < 8;
    float label_scale = compact ? 8 / cap / ui->scale : text_scale * .8f;
    float number_scale = compact ? label_scale : text_scale * 1.5f;
    float row = compact ? 14 / ui->scale : fmaxf(13, cap * label_scale + 4);
    float height = row + cap * number_scale;
    y = fminf(y, 476 - height);
    qa_scene_vec4 color = warning ? (qa_scene_vec4){1, .3f, .2f, 1} : (qa_scene_vec4){1, 1, 1, 1};
    return ui_draw_text(ui, scene, target, x, y - top * label_scale, label, color,
               label_scale / ui->text_scale, QA_FONT_ALIGN_CENTER, error) &&
           ui_draw_text(ui, scene, target, x, y + row - top * number_scale, numeric, color,
               number_scale / ui->text_scale, QA_FONT_ALIGN_CENTER, error);
}
static const char *flag_status(uint32_t bits) {
    return bits & 4 ? "dropped" : bits & 2 ? "carried" : "home";
}
static bool weapon_layout(qa_hud *hud, qa_scene_frame *scene, const char *value,
    float scale, float width, qa_scene_vec4 color, qa_font_layout *out, qa_error *error)
{
    qa_font_layout_options options = {.text = {(const uint8_t *)value, strlen(value)},
        .scale = scale, .max_width = width, .color = color, .color_codes = QA_FONT_COLOR_Q3,
        .force_color = true, .alignment = QA_FONT_ALIGN_LEFT};
    return qa_font_layout_build(&hud->options.ui->options.fonts, &options, &scene->storage, out, error);
}
static bool weapon_text(qa_hud *hud, qa_scene_frame *scene, qa_scene_rect target,
    const char *value, float x, float y, float scale, float width, float height,
    float cap_height, qa_scene_vec4 color, bool ellipsis, qa_error *error)
{
    qa_font_layout layout;
    if (!weapon_layout(hud, scene, value, scale, width, color, &layout, error)) return false;
    size_t visible = 0;
    while (visible < layout.line_count && layout.lines[visible].y + cap_height * scale <= height)
        ++visible;
    if (!visible) return true;
    bool truncated = ellipsis && visible < layout.line_count;
    qa_font_layout suffix = {0}; float suffix_x = 0, suffix_y = 0;
    if (truncated) {
        if (!weapon_layout(hud, scene, "…", scale, 0, color, &suffix, error)) return false;
        qa_font_line *lines = qa_arena_alloc(&scene->storage, visible * sizeof(*lines), _Alignof(qa_font_line), error);
        if (!lines) return false;
        memcpy(lines, layout.lines, visible * sizeof(*lines)); layout.lines = lines;
        qa_font_line *last = lines + visible - 1;
        while (last->glyph_count) {
            const qa_scene_rect_f *glyph = &layout.glyphs[last->first_glyph + last->glyph_count - 1].rect;
            suffix_x = glyph->x + glyph->width;
            if (suffix_x + suffix.width <= width) break;
            --last->glyph_count; suffix_x = 0;
        }
        suffix_y = last->y;
    }
    const qa_font_line *last = layout.lines + visible - 1;
    layout.line_count = visible; layout.glyph_count = last->first_glyph + last->glyph_count;
    qa_font_draw_options draw = {.seat = hud->options.seat, .target = target,
        .space = QA_FONT_PIXELS, .origin = {x - (float)target.x, y - (float)target.y},
        .shadow_offset = hud->options.ui->scale};
    if (!qa_font_draw_layout(scene, &layout, &draw, error)) return false;
    if (!truncated) return true;
    draw.origin.x += suffix_x; draw.origin.y += suffix_y;
    return qa_font_draw_layout(scene, &suffix, &draw, error);
}
static bool weapon_picture(qa_hud *hud, qa_scene_frame *scene, qa_scene_rect target,
    const qa_material *material, qa_scene_rect_f rect, uint64_t time_ns, qa_error *error)
{
    qa_scene_mesh mesh;
    qa_scene_vec4 white = {1, 1, 1, 1};
    if (!qa_scene_picture_geometry(scene, target, rect, (qa_scene_vec4){0, 0, 1, 1}, white, &mesh, error)) return false;
    if (!mesh.vertex_count) return true;
    qa_material_context context = {.entity_color = white, .identity_light = 1,
        .seconds = (double)time_ns / 1e9, .source_primitives = true,
        .source_writer = QA_SOURCE_WRITE_PICTURE, .video_frame = hud->options.video_frame,
        .video_context = hud->options.video_context};
    context.view.viewport = target; context.view.seat = hud->options.seat;
    context.view.axis[0] = qa_v3(1, 0, 0); context.view.axis[1] = qa_v3(0, 1, 0);
    context.view.axis[2] = qa_v3(0, 0, 1); qa_scene_matrix_identity(&context.model);
    size_t first = scene->command_count;
    qa_scene_command view = {.kind = QA_SCENE_COMMAND_VIEW, .data.view = context.view};
    if (!qa_scene_frame_emit(scene, &view, error) || !qa_material_submit(material, &mesh, &context, scene, error)) {
        scene->command_count = first; return false;
    }
    qa_scene_matrix projection = {.m = {2.0f / (float)target.width, 0, 0, 0,
        0, -2.0f / (float)target.height, 0, 0, 0, 0, 0, 0,
        -1 - 2.0f * (float)target.x / (float)target.width,
        1 + 2.0f * (float)target.y / (float)target.height, 0, 1}};
    for (size_t i = first; i < scene->command_count; ++i) {
        qa_scene_command *command = scene->commands + i;
        if (command->kind != QA_SCENE_COMMAND_DRAW) continue;
        command->data.draw.mvp = projection;
        command->data.draw.state.depth_test = QA_DEPTH_ALWAYS;
        command->data.draw.state.depth_write = false;
        command->data.draw.state.cull = QA_CULL_NONE;
    }
    return true;
}
typedef struct hud_status_layout {
    qa_scene_rect_f rect;
    float cap, top, text_scale, scale, label_top;
    bool compact;
} hud_status_layout;
static bool status_layout(qa_hud *hud, const qa_hud_frame *frame, size_t count,
    size_t index, bool native, hud_status_layout *out, qa_error *error)
{
    qa_ui *ui = hud->options.ui; qa_font_info font;
    if (!qa_font_describe(ui->options.fonts.primary ? ui->options.fonts.primary : ui->options.fonts.classic, &font))
        return ui_fail(error, "Status HUD lost its retained font metrics");
    float cap = font.has_cap_ink ? font.cap_height : 8, top = font.has_cap_ink ? font.cap_top : 0;
    if (!isfinite(cap) || cap <= 0 || !isfinite(top))
        return ui_fail(error, "Status HUD has invalid retained cap metrics");
    float text_scale = ui->text_scale * 1.5f, group = frame->scale;
    float fit = ui->scale / group, label_top = fmaxf(25, 8 + cap * text_scale * 1.5f);
    float height = fmaxf(42, ceilf(label_top + cap * text_scale * .9f + 4));
    float width = fminf(600 / group / (float)(count ? count : 1), 160);
    qa_scene_rect_f rect = {320 - width * (float)count * .5f + (float)index * width,
        476 - height, width - 4, height};
    float scale = ui->scale;
    float bias_x = (float)frame->safe_area.x + ((float)frame->safe_area.width - 640 * fit) * .5f + 320 * fit * (1 - group);
    float bias_y = (float)frame->safe_area.y + ((float)frame->safe_area.height - 480 * fit) * .5f + 480 * fit * (1 - group);
    bool compact = !native && scale * text_scale * cap < 8;
    if (native) {
        rect = (qa_scene_rect_f){8, 476 - height, 152, height};
        bias_x = (float)frame->safe_area.x + ((float)frame->safe_area.width - 640 * fit) * .5f;
    }
    if (compact) {
        width = fminf(180, ((float)frame->safe_area.width - 8) / (float)(count ? count : 1));
        rect = (qa_scene_rect_f){(float)frame->safe_area.x + ((float)frame->safe_area.width - width * (float)count) * .5f + (float)index * width,
            (float)frame->safe_area.y + (float)frame->safe_area.height - 36, width - 4, 32};
        scale = 1; bias_x = bias_y = 0; text_scale = 8 / cap;
    }
    *out = (hud_status_layout){.rect = {bias_x + rect.x * scale, bias_y + rect.y * scale,
            rect.width * scale, rect.height * scale}, .cap = cap, .top = top,
        .text_scale = text_scale * scale, .scale = scale, .label_top = label_top * scale, .compact = compact};
    return true;
}
static bool team_face_draw(qa_hud *hud,const qa_hud_frame *frame,const qa_hud_team_face *face,
    qa_scene_rect_f rect,qa_scene_frame *scene,qa_error *error)
{
    qa_ui *ui=hud->options.ui;
    float sx=rect.width/24,sy=rect.height/24;
    if (!qa_scene_frame_picture_f(scene,face->border,frame->safe_area,rect,
        (qa_scene_vec4){0,0,1,1},(qa_scene_vec4){1,1,1,1},error) ||
        !qa_scene_frame_picture_f(scene,ui->options.white,frame->safe_area,
            (qa_scene_rect_f){rect.x+sx,rect.y+3*sy,22*sx,9*sy},(qa_scene_vec4){0,0,1,1},face->top,error) ||
        !qa_scene_frame_picture_f(scene,ui->options.white,frame->safe_area,
            (qa_scene_rect_f){rect.x+sx,rect.y+12*sy,22*sx,9*sy},(qa_scene_vec4){0,0,1,1},face->bottom,error)) return false;
    char number[32]; snprintf(number,sizeof(number),"%3ld",(long)face->score);
    for (unsigned i=0;i<3;++i) {
        unsigned character=(uint8_t)number[i];
        if (character==' ') continue;
        if (face->alternate_digits) character=18+character-'0';
        qa_font_glyph glyph;
        if (!qa_font_find_glyph(ui->options.fonts.classic,character,&glyph))
            return ui_fail(error,"Rogue team score lost its original conchars glyph");
        if (glyph.visible && !qa_scene_frame_picture_f(scene,glyph.image,frame->safe_area,
            (qa_scene_rect_f){rect.x+(1+7*(float)i)*sx,rect.y+3*sy,8*sx,8*sy},
            glyph.uv,(qa_scene_vec4){1,1,1,1},error)) return false;
    }
    return true;
}
static bool status_vital(qa_hud *hud, const qa_hud_frame *frame, const qa_hud_value *vital,
    const qa_hud_team_face *team_face,
    size_t count, size_t index, qa_scene_frame *scene, qa_error *error)
{
    hud_status_layout layout;
    if (!status_layout(hud, frame, count, index, false, &layout, error)) return false;
    qa_scene_rect_f rect = layout.rect;
    if (rect.width <= 8 || rect.height <= 8) return true;
    if (!qa_scene_frame_picture_f(scene, hud->options.ui->options.white, frame->safe_area, rect,
        (qa_scene_vec4){0, 0, 1, 1}, (qa_scene_vec4){.055f, .06f, .065f, .94f}, error)) return false;
    qa_scene_rect_f face_rect={rect.x+6*layout.scale,rect.y+8*layout.scale,24*layout.scale,24*layout.scale};
    bool team=team_face && team_face->border;
    if (team ? !team_face_draw(hud,frame,team_face,face_rect,scene,error) :
        vital->icon && !qa_scene_frame_picture_f(scene,vital->icon,frame->safe_area,face_rect,
            (qa_scene_vec4){0,0,1,1},(qa_scene_vec4){1,1,1,1},error)) return false;
    char value[32];
    if (!qa_format_number(vital->value, value, error)) return false;
    const char *label = vital->label ? vital->label : "";
    float left = (vital->icon || team ? 34 : 4) * layout.scale;
    float available = rect.width - left - 4 * layout.scale;
    qa_scene_vec4 color = vital->warning ? (qa_scene_vec4){1, .65f, .22f, 1} : (qa_scene_vec4){.92f, .88f, .78f, 1};
    if (layout.compact) {
        size_t size = strlen(label) + strlen(value) + 2;
        char *combined = qa_arena_alloc(&scene->storage, size, 1, error);
        if (!combined) return false;
        snprintf(combined, size, "%s %s", label, value);
        qa_font_layout measured;
        if (!weapon_layout(hud, scene, combined, layout.text_scale, 0, color, &measured, error)) return false;
        if (measured.width <= available)
            return weapon_text(hud, scene, frame->safe_area, combined, rect.x + left,
                rect.y + 4 - layout.top * layout.text_scale, layout.text_scale, available,
                24, layout.cap, color, true, error);
        return weapon_text(hud, scene, frame->safe_area, label, rect.x + left,
                rect.y + 4 - layout.top * layout.text_scale, layout.text_scale, available,
                14, layout.cap, color, true, error) &&
            weapon_text(hud, scene, frame->safe_area, value, rect.x + left,
                rect.y + 18 - layout.top * layout.text_scale, layout.text_scale, available,
                14, layout.cap, color, true, error);
    }
    qa_font_layout measured;
    if (!weapon_layout(hud, scene, value, 1, 0, color, &measured, error)) return false;
    float numeric_scale = fminf(layout.text_scale * 1.5f, available / fmaxf(1, measured.width));
    float label_scale = layout.text_scale * .8f;
    return weapon_text(hud, scene, frame->safe_area, value, rect.x + left,
            rect.y + 4 * layout.scale - layout.top * numeric_scale, numeric_scale, available,
            rect.height, layout.cap, color, true, error) &&
        weapon_text(hud, scene, frame->safe_area, label, rect.x + left,
            rect.y + layout.label_top - layout.top * label_scale, label_scale, available,
            rect.height - layout.label_top, layout.cap, color, true, error);
}
static bool weapon_draw(qa_hud *hud, const qa_hud_frame *frame, const qa_hud_data *data,
    qa_scene_frame *scene, qa_error *error)
{
    const qa_hud_weapon *weapon = &data->weapon;
    if (!weapon->present) return true;
    if (!weapon->label || (weapon->finite_ammo && !isfinite(weapon->ammo_count)))
        return ui_fail(error, "Weapon HUD lost its actual source status");
    qa_ui *ui = hud->options.ui;
    size_t vitals = data->source_vitals ? data->vital_count : 2;
    size_t count = vitals + (weapon->native_status ? 0 : 1);
    hud_status_layout layout;
    if (!status_layout(hud, frame, count, vitals, weapon->native_status, &layout, error)) return false;
    float cap = layout.cap, top = layout.top, scale = layout.scale;
    float text_scale = layout.text_scale / scale, label_top = layout.label_top / scale;
    bool compact = layout.compact;
    qa_scene_rect_f pixels = layout.rect;
    qa_scene_rect_f rect = {0, 0, pixels.width / scale, pixels.height / scale};
    if (rect.width <= 8 || rect.height <= 8) return true;
    if (!qa_scene_frame_picture_f(scene, ui->options.white, frame->safe_area, pixels,
        (qa_scene_vec4){0, 0, 1, 1}, (qa_scene_vec4){.055f, .06f, .065f, .94f}, error)) return false;
    bool unavailable = weapon->finite_ammo && !weapon->has_ammo_to_start;
    const char *warning = weapon->aggregate_empty ? "OUT OF AMMO" : weapon->aggregate_low ? "LOW AMMO WARNING" :
        weapon->suppress_active_warning ? NULL : unavailable ? "NO AMMO" : weapon->finite_ammo && weapon->low_ammo ? "LOW AMMO" : NULL;
    qa_scene_vec4 color = warning || unavailable ? (qa_scene_vec4){1, .65f, .22f, 1} : (qa_scene_vec4){.92f, .88f, .78f, 1};
    char numeric[32] = "";
    if (weapon->finite_ammo && !qa_format_number(weapon->ammo_count, numeric, error)) return false;
    qa_font_layout measured;
    if (!weapon_layout(hud, scene, numeric, 1, 0, color, &measured, error)) return false;
    bool stacked = weapon->icon && measured.width * text_scale * 1.5f > rect.width - 66;
    float aspect = 1;
    if (weapon->icon) {
        for (size_t i = 0; i < weapon->icon->stage_count; ++i) {
            const qa_material_stage *stage = weapon->icon->stages + i;
            if (!stage->image_count || !stage->images[0] || !stage->images[0]->level_count) continue;
            const qa_scene_image *image = stage->images[0];
            uint32_t w = image->logical_width ? image->logical_width : image->levels[0].width;
            uint32_t h = image->logical_height ? image->logical_height : image->levels[0].height;
            if (w && h) aspect = (float)w / (float)h;
            break;
        }
        float max_width = compact || stacked ? 24 : warning ? 30 : 48;
        float max_height = compact ? 24 : stacked ? fminf(24, rect.height - label_top - 4) : warning ? 20 : 32;
        float icon_width = fminf(max_width, max_height * aspect);
        float column = compact || stacked ? 24 : 48;
        qa_scene_rect_f icon_rect = {pixels.x + (4 + (column - icon_width) * .5f) * scale,
            pixels.y + (compact ? 4 : stacked ? label_top : 5) * scale, icon_width * scale, icon_width / aspect * scale};
        if (!weapon_picture(hud, scene, frame->safe_area, weapon->icon, icon_rect, frame->time_ns, error)) return false;
    }
    float left = compact ? weapon->icon ? 32 : 4 : !weapon->icon || stacked ? 8 : 58;
    float available = rect.width - left - (compact ? 4 : 8);
    if (weapon->finite_ammo) {
        float number_scale = compact ? text_scale : fminf(text_scale * 1.5f, available / fmaxf(1, measured.width));
        if (!weapon_text(hud, scene, frame->safe_area, numeric, pixels.x + left * scale,
            pixels.y + (4 - top * number_scale) * scale, number_scale * scale, available * scale,
            rect.height * scale, cap, color, false, error)) return false;
    }
    if (compact) {
        if (!weapon->finite_ammo && !weapon->icon &&
            !weapon_text(hud, scene, frame->safe_area, weapon->label, pixels.x + left,
                pixels.y + 4 - top * text_scale, text_scale, available, 10, cap, color, false, error)) return false;
        const char *compact_warning = weapon->aggregate_low ? "LOW AMMO" : warning;
        return !compact_warning || weapon_text(hud, scene, frame->safe_area, compact_warning,
            pixels.x + left, pixels.y + 18 - top * text_scale, text_scale, available, 10, cap, color, false, error);
    }
    if (warning || !weapon->icon) {
        const char *label = warning ? warning : weapon->label;
        float label_left = compact ? left : stacked ? 32 : warning ? 4 : left;
        float label_available = rect.width - label_left - 4;
        if (!weapon_layout(hud, scene, label, 1, 0, color, &measured, error)) return false;
        float minimum = weapon->native_status ? 0 : 8 / cap / scale;
        if (weapon->aggregate_low && measured.width * minimum > label_available) {
            label = "LOW AMMO";
            if (!weapon_layout(hud, scene, label, 1, 0, color, &measured, error)) return false;
        }
        float label_scale = compact ? text_scale : fmaxf(minimum, fminf(text_scale * .9f, label_available / fmaxf(1, measured.width)));
        float label_y = compact ? 18 : label_top;
        if (!weapon_text(hud, scene, frame->safe_area, label, pixels.x + label_left * scale,
            pixels.y + (label_y - top * label_scale) * scale, label_scale * scale,
            label_available * scale, (rect.height - label_y - 4) * scale, cap, color, true, error)) return false;
    }
    return true;
}
static bool ctf_draw(qa_hud *hud, const qa_hud_frame *frame, qa_scene_frame *scene,
    qa_error *error) {
    if (!hud->ctf_present || !qa_actor_id_equal(frame->actor, hud->ctf_actor) ||
        !qa_actors_get(qa_session_actors(qa_application_session(hud->options.application)), frame->actor) ||
        !qa_application_provider_instance(hud->options.application, hud->ctf_source)) return true;
    uint64_t source_time_ns;
    bool found;
    if (!qa_application_q1_ctf_recipient_read(hud->options.application, hud->ctf_source,
            frame->actor, &source_time_ns, &found, error)) return false;
    if (!found) return true;
    char red[32], blue[32], row[128];
    if (!qa_format_number(hud->ctf_status.red, red, error) ||
        !qa_format_number(hud->ctf_status.blue, blue, error)) return false;
    snprintf(row, sizeof(row), "Red %s - Blue %s", red, blue);
    qa_scene_vec4 color = {1, 1, 1, 1};
    if (!text(hud, scene, frame->safe_area, 320, 56, row, color, 1, QA_FONT_ALIGN_CENTER, error)) return false;
    uint32_t flags = (uint32_t)qa_source_float_to_i32((float)hud->ctf_status.flags);
    uint32_t runes = (uint32_t)qa_source_float_to_i32((float)hud->ctf_status.rune_items);
    snprintf(row, sizeof(row), "Red flag %s - Blue flag %s",
        flag_status(flags & 7), flag_status((flags >> 3) & 7));
    if (!text(hud, scene, frame->safe_area, 320, 72, row, color, 1, QA_FONT_ALIGN_CENTER, error)) return false;
    static const char *const labels[] = {"Resistance", "Strength", "Haste", "Regeneration"};
    float y = 88;
    for (size_t i = 0; i < sizeof(labels) / sizeof(*labels); ++i) {
        if (!(runes & (UINT32_C(32) << i))) continue;
        if (!text(hud, scene, frame->safe_area, 320, y, labels[i], color, 1, QA_FONT_ALIGN_CENTER, error)) return false;
        y += 16;
    }
    if (hud->ctf_capture_present && hud->ctf_capture_until_ns > source_time_ns &&
        !text(hud, scene, frame->safe_area, 320, y,
            hud->ctf_capture.blue ? "Blue captured the flag" : "Red captured the flag",
            color, 1, QA_FONT_ALIGN_CENTER, error)) return false;
    return true;
}
static bool caption_draw(qa_ui *ui,const qa_active_caption *values,size_t count,qa_scene_rect target,
    qa_scene_rect_f area,float fit,qa_scene_frame *scene,qa_error *error)
{
    float width=area.width,height=area.height,scale=fit*ui->text_scale;
    if (!count || width<16 || height<12) return true;
    qa_font_info font;
    if (!qa_font_describe(ui->options.fonts.primary?ui->options.fonts.primary:ui->options.fonts.classic,&font))
        return ui_fail(error,"Caption presentation lost its actual font metrics");
    float cap_height=font.has_cap_ink?font.cap_height:8,cap_top=font.has_cap_ink?font.cap_top:0;
    float line_height=ceilf(cap_height*scale)+4;
    if (line_height+8>height) return true;
    if (count>SIZE_MAX/sizeof(qa_font_layout)) return ui_fail(error,"Caption layout count overflows");
    qa_font_layout *layouts=qa_arena_alloc(&scene->storage,count*sizeof(*layouts),_Alignof(qa_font_layout),error);
    if (!layouts) return false;
    size_t lines=0;
    for (size_t i=0;i<count;++i) {
        const qa_active_caption *caption=values+i;
        if (!caption->text) return ui_fail(error,"Caption presentation lost its localized text");
        const char *speaker=caption->speaker?caption->speaker:"",*value=caption->text;
        size_t speaker_size=strlen(speaker),text_size=strlen(value);
        if (speaker_size>SIZE_MAX-3 || text_size>SIZE_MAX-speaker_size-3)
            return ui_fail(error,"Caption text extent overflows");
        char *joined=qa_arena_alloc(&scene->storage,speaker_size+text_size+3,1,error);
        if (!joined) return false;
        size_t offset=0;
        if (speaker_size) { memcpy(joined,speaker,speaker_size); offset=speaker_size; joined[offset++]=':'; joined[offset++]=' '; }
        memcpy(joined+offset,value,text_size+1);
        qa_font_layout_options options={.text={(const uint8_t *)joined,offset+text_size},.scale=scale,
            .color={1,1,1,1},.color_codes=QA_FONT_COLOR_LITERAL,.force_color=true,
            .alignment=QA_FONT_ALIGN_CENTER,.max_width=width-8};
        if (!qa_font_layout_build(&ui->options.fonts,&options,&scene->storage,layouts+i,error)) return false;
        if (layouts[i].line_count>SIZE_MAX-lines) return ui_fail(error,"Caption line count overflows");
        lines+=layouts[i].line_count;
    }
    long double maximum=floorl(((long double)height-8)/(long double)line_height);
    size_t visible=maximum>=(long double)lines?lines:(size_t)maximum;
    float panel_height=(float)visible*line_height+8;
    float x=area.x,y=area.y+area.height-panel_height;
    if (!qa_scene_frame_picture_f(scene,ui->options.white,target,(qa_scene_rect_f){x,y,width,panel_height},
        (qa_scene_vec4){0,0,1,1},(qa_scene_vec4){0,0,0,.92f},error)) return false;
    size_t skip=lines-visible,shown=0;
    for (size_t i=0;i<count;++i) {
        qa_font_layout *layout=layouts+i;
        for (size_t row=0;row<layout->line_count;++row) {
            if (skip) { --skip; continue; }
            const qa_font_line *line=layout->lines+row;
            qa_font_positioned_glyph *glyphs=(qa_font_positioned_glyph *)layout->glyphs+line->first_glyph;
            for (size_t j=0;j<line->glyph_count;++j) glyphs[j].rect.y-=line->y;
            qa_font_line single={line->width,0,0,line->glyph_count};
            qa_font_layout draw=*layout; draw.glyphs=glyphs; draw.glyph_count=line->glyph_count;
            draw.lines=&single; draw.line_count=1; draw.height=draw.line_height;
            qa_font_draw_options options={.seat=ui->options.seat,.target=target,.space=QA_FONT_PIXELS,
                .origin={x+4-(float)target.x,y+4+(float)shown*line_height-cap_top*scale-(float)target.y},.shadow_offset=fit};
            if (!qa_font_draw_layout(scene,&draw,&options,error)) return false;
            ++shown;
        }
    }
    return true;
}
bool qa_ui_captions_draw(qa_ui *ui,qa_scene_frame *scene,qa_scene_rect target,qa_scene_rect_f area,
    float fit,const qa_active_caption *values,size_t count,qa_error *error)
{
    if (!qa_ui_idle(ui) || !scene || (count && !values) || !isfinite(fit) || fit<=0 ||
        !isfinite(area.x) || !isfinite(area.y) || !isfinite(area.width) || !isfinite(area.height) ||
        area.width<0 || area.height<0 || !isfinite(area.x+area.width) || !isfinite(area.y+area.height))
        return ui_fail(error,"Caption draw requires its idle actual seat UI and finite viewport region");
    if (!target.width || !target.height) return true;
    ui->handling=ui->drawing=true;
    bool ok=caption_draw(ui,values,count,target,area,fit,scene,error);
    ui->handling=ui->drawing=false; return ok;
}
static float hud_scale(qa_scene_rect seat, float scale)
{ return fminf((float)seat.width / 640, (float)seat.height / 480) * scale; }
qa_hud_q1_placement qa_hud_q1_place(qa_scene_rect seat, float scale, double view_size,
    bool overlay_status, bool intermission, bool deathmatch)
{
    double size = intermission ? 120 : view_size > 0 ? view_size : 100;
    scale = hud_scale(seat, scale);
    uint32_t lines = size >= 120 ? 0u : size >= 110 ? 24u : 48u;
    uint32_t reserved = overlay_status && size >= 100 ? 0u : (uint32_t)ceilf((float)lines * scale);
    if (reserved >= seat.height) reserved = seat.height ? seat.height - 1 : 0;
    return (qa_hud_q1_placement){.x = (float)seat.x + (deathmatch ? 0 : ((float)seat.width - 320 * scale) * .5f),
        .y = (float)seat.y + (float)seat.height - 24 * scale, .scale = scale, .lines = lines, .reserved = reserved};
}
static bool q1_picture(const qa_hud_q1_status *status, const qa_hud_frame *frame,
    qa_hud_q1_placement place, const char *name, float x, float y, qa_scene_frame *scene, qa_error *error)
{
    const qa_scene_image *image = status->picture(status->picture_context, name, error);
    if (!image) return false;
    float width = (float)image->logical_width, height = (float)image->logical_height;
    return qa_scene_frame_picture_f(scene, image, frame->safe_area,
        (qa_scene_rect_f){place.x + x * place.scale, place.y + y * place.scale, width * place.scale, height * place.scale},
        (qa_scene_vec4){0, 0, 1, 1}, (qa_scene_vec4){1, 1, 1, 1}, error);
}
static bool q1_character(qa_hud *hud, const qa_hud_frame *frame, qa_hud_q1_placement place,
    unsigned character, float x, float y, qa_scene_frame *scene, qa_error *error)
{
    qa_font_glyph glyph;
    if (!qa_font_find_glyph(hud->options.ui->options.fonts.classic, character, &glyph))
        return ui_fail(error, "Q1 inventory lost its source conchars glyph");
    return !glyph.visible || qa_scene_frame_picture_f(scene, glyph.image, frame->safe_area,
        (qa_scene_rect_f){place.x + (x + 4) * place.scale, place.y + y * place.scale, 8 * place.scale, 8 * place.scale},
        glyph.uv, (qa_scene_vec4){1, 1, 1, 1}, error);
}
static bool q1_number(qa_hud *hud, const qa_hud_q1_status *status, const qa_hud_frame *frame,
    qa_hud_q1_placement place, float x, int32_t value, bool alternate, qa_scene_frame *scene, qa_error *error)
{
    char number[16]; snprintf(number, sizeof(number), "%ld", (long)value);
    size_t length = strlen(number); const char *digits = number + (length > 3 ? length - 3 : 0);
    length = strlen(digits); x += (float)(3 - length) * 24;
    qa_ui *ui = hud->options.ui;
    if (ui->options.fonts.primary || ui->text_scale != 1 || ui->high_contrast || ui->color_mode != QA_UI_COLOR_STANDARD) {
        qa_scene_vec4 color = alternate ? ui->color_mode == QA_UI_COLOR_BLUE_YELLOW ?
            (qa_scene_vec4){1, .9f, .2f, 1} : (qa_scene_vec4){1, .3f, .2f, 1} : (qa_scene_vec4){1, 1, 1, 1};
        if (ui->color_mode == QA_UI_COLOR_MONOCHROME) color = (qa_scene_vec4){1, 1, 1, 1};
        float left = place.x + x * place.scale, width = (float)length * 24 * place.scale;
        if (ui->high_contrast && !qa_scene_frame_picture_f(scene, ui->options.white, frame->safe_area,
            (qa_scene_rect_f){left, place.y, width, 24 * place.scale}, (qa_scene_vec4){0, 0, 1, 1},
            (qa_scene_vec4){0, 0, 0, 1}, error)) return false;
        qa_font_layout layout;
        if (!weapon_layout(hud, scene, digits, 3 * place.scale * ui->text_scale, width, color, &layout, error)) return false;
        return qa_font_draw_layout(scene, &layout, &(qa_font_draw_options){.seat = frame->seat,
            .target = frame->safe_area, .space = QA_FONT_PIXELS,
            .origin = {left - (float)frame->safe_area.x, place.y - (float)frame->safe_area.y}, .shadow_offset = place.scale}, error);
    }
    for (const char *digit = digits; *digit; ++digit, x += 24) {
        char name[32];
        if (*digit == '-') snprintf(name, sizeof(name), "%s_minus", alternate ? "anum" : "num");
        else snprintf(name, sizeof(name), "%s_%c", alternate ? "anum" : "num", *digit);
        if (!q1_picture(status, frame, place, name, x, 0, scene, error)) return false;
    }
    return true;
}
static unsigned q1_weapon_frame(const qa_hud_q1_status *status, unsigned bit)
{
    if (status->item_gettime && !status->reduced_flashes) {
        double elapsed = (status->seconds - status->item_gettime[bit]) * 10;
        if (isfinite(elapsed) && elapsed < 10) return (unsigned)fmax(0, elapsed) % 5 + 2;
    }
    return status->active_weapon == (UINT32_C(1) << bit) ? 1u : 0u;
}
static bool q1_weapon_picture(const qa_hud_q1_status *status, const qa_hud_frame *frame,
    qa_hud_q1_placement place, const char *weapon, unsigned animation, float x, float y,
    qa_scene_frame *scene, qa_error *error)
{
    char name[48];
    if (animation < 2) snprintf(name, sizeof(name), "%s_%s", animation ? "inv2" : "inv", weapon);
    else snprintf(name, sizeof(name), "inva%u_%s", animation - 1, weapon);
    return q1_picture(status, frame, place, name, x, y, scene, error);
}
static bool q1_inventory(qa_hud *hud, const qa_hud_q1_status *status, const qa_hud_frame *frame,
    qa_hud_q1_placement place, qa_scene_frame *scene, qa_error *error)
{
    const char *bar = status->variant == QA_HUD_Q1_ROGUE ?
        status->active_weapon >= 4096 ? "r_invbar1" : "r_invbar2" : "ibar";
    bool heads_up = status->overlay_status && status->view_size >= 100;
    qa_hud_q1_placement side = place; side.x = (float)frame->safe_area.x;
    float logical_width = (float)frame->safe_area.width / place.scale;
    if (!heads_up && !q1_picture(status, frame, place, bar, 0, -24, scene, error)) return false;
    static const char *const weapons[] = {"shotgun", "sshotgun", "nailgun", "snailgun", "rlaunch", "srlaunch", "lightng"};
    for (unsigned i = 0; i < 7; ++i) {
        if (!(status->items & (UINT32_C(1) << i)) ||
            (heads_up && !i && (float)frame->safe_area.height / place.scale <= 200)) continue;
        float x = heads_up ? status->hud_swap ? 0 : logical_width - 24 : (float)i * 24;
        float y = heads_up ? -68 - (float)(7 - i) * 16 : -16;
        if (!q1_weapon_picture(status, frame, heads_up ? side : place, weapons[i],
            q1_weapon_frame(status, i), x, y, scene, error)) return false;
    }
    if (status->variant == QA_HUD_Q1_HIPNOTIC) {
        static const unsigned bits[] = {23, 7, 4, 16};
        bool grenade = false;
        for (unsigned i = 0; i < 4; ++i) {
            if (!(status->items & (UINT32_C(1) << bits[i]))) continue;
            unsigned animation = q1_weapon_frame(status, bits[i]);
            if (i < 2) {
                if (!q1_weapon_picture(status, frame, place, i ? "mjolnir" : "laser", animation,
                    176 + (float)i * 24, -16, scene, error)) return false;
            } else if (i == 2 && (status->items & 65536u) && animation) {
                grenade = true;
                if (!q1_weapon_picture(status, frame, place, "gren_prox", animation, 96, -16, scene, error)) return false;
            } else if (i == 3 && !grenade) {
                if (!q1_weapon_picture(status, frame, place, status->items & 16u ? "prox_gren" : "prox",
                    animation, 96, -16, scene, error)) return false;
            }
        }
    } else if (status->variant == QA_HUD_Q1_ROGUE) {
        static const char *const powered[] = {"r_lava", "r_superlava", "r_gren", "r_multirock", "r_plasma"};
        for (unsigned i = 0; i < 5; ++i)
            if (status->active_weapon == (4096u << i) && !q1_picture(status, frame, place,
                powered[i], (float)(i + 2) * 24, -16, scene, error)) return false;
    }
    for (unsigned i = 0; i < 4; ++i) {
        char number[16]; snprintf(number, sizeof(number), "%3lu", (unsigned long)status->ammunition[i]);
        float y = heads_up ? -24 - (float)(4 - i) * 11 : -24;
        if (heads_up) {
            const qa_scene_image *image = status->picture(status->picture_context, "ibar", error);
            if (!image) return false;
            float x = status->hud_swap ? 0 : logical_width - 42;
            if (!qa_scene_frame_picture_f(scene, image, frame->safe_area,
                (qa_scene_rect_f){side.x + x * place.scale, place.y + y * place.scale, 42 * place.scale, 11 * place.scale},
                (qa_scene_vec4){(float)(3 + i * 48) / (float)image->logical_width, 0,
                    (float)(45 + i * 48) / (float)image->logical_width, 11.0f / (float)image->logical_height},
                (qa_scene_vec4){1, 1, 1, 1}, error)) return false;
        }
        for (unsigned j = 0; j < 3; ++j) {
            float x = heads_up ? (status->hud_swap ? 3 : logical_width - 39) + (float)j * 8 :
                (float)((6 * i + j + 1) * 8) - 2;
            if (number[j] >= '0' && number[j] <= '9' && !q1_character(hud, frame, heads_up ? side : place,
                18u + (unsigned)(number[j] - '0'), x, y, scene, error)) return false;
        }
    }
    static const char *const items[] = {"sb_key1", "sb_key2", "sb_invis", "sb_invuln", "sb_suit", "sb_quad"};
    for (unsigned i = 0; i < 6; ++i)
        if ((status->items & (UINT32_C(1) << (17 + i))) &&
            (status->variant != QA_HUD_Q1_HIPNOTIC || i > 1) &&
            !q1_picture(status, frame, place, items[i], 192 + (float)i * 16, -16, scene, error)) return false;
    if (status->variant != QA_HUD_Q1_BASE) {
        const char *const extra[2] = {status->variant == QA_HUD_Q1_ROGUE ? "r_shield1" : "sb_wsuit",
            status->variant == QA_HUD_Q1_ROGUE ? "r_agrav1" : "sb_eshld"};
        unsigned bit = status->variant == QA_HUD_Q1_ROGUE ? 29u : 24u;
        for (unsigned i = 0; i < 2; ++i)
            if ((status->items & (UINT32_C(1) << (bit + i))) &&
                !q1_picture(status, frame, place, extra[i], 288 + (float)i * 16, -16, scene, error)) return false;
    }
    if (status->variant != QA_HUD_Q1_ROGUE) {
        for (unsigned i = 0; i < 4; ++i) {
            char name[16]; snprintf(name, sizeof(name), "sb_sigil%u", i + 1);
            if ((status->items & (UINT32_C(1) << (28 + i))) &&
                !q1_picture(status, frame, place, name, 288 + (float)i * 8, -16, scene, error)) return false;
        }
    }
    return true;
}
static bool q1_string(qa_hud *hud, const qa_hud_frame *frame, qa_hud_q1_placement place,
    const char *value, float x, float y, qa_scene_frame *scene, qa_error *error)
{
    qa_ui *ui = hud->options.ui;
    if (ui->options.fonts.primary || ui->text_scale != 1) {
        qa_font_layout layout;
        if (!weapon_layout(hud, scene, value, place.scale * ui->text_scale, 320 * place.scale,
            (qa_scene_vec4){1, 1, 1, 1}, &layout, error)) return false;
        return qa_font_draw_layout(scene, &layout, &(qa_font_draw_options){.seat = frame->seat,
            .target = frame->safe_area, .space = QA_FONT_PIXELS,
            .origin = {place.x + x * place.scale - (float)frame->safe_area.x,
                place.y + y * place.scale - (float)frame->safe_area.y}, .shadow_offset = place.scale}, error);
    }
    for (const unsigned char *c = (const unsigned char *)value; *c; ++c, x += 8)
        if (!q1_character(hud, frame, place, *c, x - 4, y, scene, error)) return false;
    return true;
}
static bool q1_scorebar(qa_hud *hud, const qa_hud_q1_status *status, const qa_hud_frame *frame,
    qa_hud_q1_placement place, qa_scene_frame *scene, qa_error *error)
{
    if (!q1_picture(status, frame, place, "scorebar", 0, 0, scene, error)) return false;
    char text[80];
    if (!status->quakeworld) {
        snprintf(text, sizeof(text), "Monsters:%3ld /%3ld", (long)status->killed_monsters, (long)status->total_monsters);
        if (!q1_string(hud, frame, place, text, 8, 4, scene, error)) return false;
        snprintf(text, sizeof(text), "Secrets :%3ld /%3ld", (long)status->found_secrets, (long)status->total_secrets);
        if (!q1_string(hud, frame, place, text, 8, 12, scene, error)) return false;
        const char *level = status->level ? status->level : "";
        if (!q1_string(hud, frame, place, level, 232 - (float)strlen(level) * 4, 12, scene, error)) return false;
    }
    int32_t seconds = (int32_t)fmax(0, fmin(INT32_MAX, status->seconds));
    snprintf(text, sizeof(text), "Time :%3ld:%02ld", (long)(seconds / 60), (long)(seconds % 60));
    return q1_string(hud, frame, place, text, 184, 4, scene, error);
}
static bool q1_backtile(const qa_hud_q1_status *status, const qa_hud_frame *frame,
    qa_hud_q1_placement place, qa_scene_frame *scene, qa_error *error)
{
    if (!place.reserved || (float)frame->safe_area.width <= 320 * place.scale) return true;
    const qa_scene_image *image = status->picture(status->picture_context, "backtile", error);
    if (!image) return false;
    float tile_width = (float)image->logical_width, tile_height = (float)image->logical_height;
    float left = (float)frame->safe_area.x, top = (float)frame->safe_area.y;
    float right = left + (float)frame->safe_area.width, bottom = top + (float)frame->safe_area.height;
    const float edges[] = {left, place.x, place.x + 320 * place.scale, right};
    for (unsigned side = 0; side < 2; ++side) {
        float x = edges[side * 2], end = edges[side * 2 + 1];
        float y = bottom - (float)place.reserved;
        if (end <= x) continue;
        if (!qa_scene_frame_picture_f(scene, image, frame->safe_area,
            (qa_scene_rect_f){x, y, end - x, (float)place.reserved},
            (qa_scene_vec4){(x - left) / tile_width, (y - top) / tile_height,
                (end - left) / tile_width, (bottom - top) / tile_height},
            (qa_scene_vec4){1, 1, 1, 1}, error)) return false;
    }
    return true;
}
static bool q1_status_draw(qa_hud *hud, const qa_hud_frame *frame, const qa_hud_data *data,
    qa_scene_frame *scene, qa_error *error)
{
    const qa_hud_q1_status *status = &data->q1;
    if (!status->picture || !isfinite(status->view_size) || !isfinite(status->seconds) ||
        (unsigned)status->variant > QA_HUD_Q1_ROGUE) return ui_fail(error, "Q1 HUD lost its actual Source status and prepared pictures");
    qa_hud_q1_placement place = qa_hud_q1_place(frame->safe_area, frame->scale, status->view_size,
        status->overlay_status, status->intermission, status->deathmatch || status->quakeworld);
    if (!place.lines) return true;
    if (!q1_backtile(status, frame, place, scene, error)) return false;
    if (place.lines > 24 && !q1_inventory(hud, status, frame, place, scene, error)) return false;
    if (frame->show_scores || status->health <= 0)
        return q1_scorebar(hud, status, frame, place, scene, error);
    if (!(status->overlay_status && status->view_size >= 100) &&
        !q1_picture(status, frame, place, "sbar", 0, 0, scene, error)) return false;
    if (status->variant == QA_HUD_Q1_HIPNOTIC) {
        if ((status->items & 131072u) && !q1_picture(status, frame, place, "sb_key1", 209, 3, scene, error)) return false;
        if ((status->items & 262144u) && !q1_picture(status, frame, place, "sb_key2", 209, 12, scene, error)) return false;
    }
    bool invulnerable = (status->items & 1048576u) != 0;
    if (!q1_number(hud, status, frame, place, 24, invulnerable ? 666 : (int32_t)status->armor,
        invulnerable || status->armor <= 25, scene, error)) return false;
    if (invulnerable) {
        if (!q1_picture(status, frame, place, "disc", 0, 0, scene, error)) return false;
    } else {
        unsigned bit = status->variant == QA_HUD_Q1_ROGUE ? 23u : 13u;
        for (unsigned i = 3; i > 0; --i) if (status->items & (UINT32_C(1) << (bit + i - 1))) {
            char name[16]; snprintf(name, sizeof(name), "sb_armor%u", i);
            if (!q1_picture(status, frame, place, name, 0, 0, scene, error)) return false;
            break;
        }
    }
    qa_scene_rect_f face = {place.x + 112 * place.scale, place.y, 24 * place.scale, 24 * place.scale};
    if (data->health_team_face.border) {
        if (!team_face_draw(hud, frame, &data->health_team_face, face, scene, error)) return false;
    } else if (status->face && !qa_scene_frame_picture_f(scene, status->face, frame->safe_area, face,
        (qa_scene_vec4){0, 0, 1, 1}, (qa_scene_vec4){1, 1, 1, 1}, error)) return false;
    if (!q1_number(hud, status, frame, place, 136, status->health, status->health <= 25, scene, error)) return false;
    static const char *const ammunition[] = {"sb_shells", "sb_nails", "sb_rocket", "sb_cells",
        "r_ammolava", "r_ammoplasma", "r_ammomulti"};
    unsigned first = status->variant == QA_HUD_Q1_ROGUE ? 7u : 8u;
    for (unsigned i = 0; i < (status->variant == QA_HUD_Q1_ROGUE ? 7u : 4u); ++i) {
        unsigned bit = i < 4 ? first + i : 22u + i;
        if (!(status->items & (UINT32_C(1) << bit))) continue;
        if (!q1_picture(status, frame, place, ammunition[i], 224, 0, scene, error)) return false;
        break;
    }
    return q1_number(hud, status, frame, place, 248, (int32_t)status->ammo_count,
        status->ammo_count <= 10, scene, error);
}
static bool draw(qa_hud *hud, const qa_hud_frame *frame, qa_scene_frame *scene, qa_error *error) {
    qa_ui *ui = hud->options.ui;
    ui->scale = hud_scale(frame->safe_area, frame->scale);
    if (!isfinite(ui->scale) || ui->scale <= 0) return ui_fail(error, "HUD scale overflow");
    ui->bias_x = (float)frame->safe_area.x + ((float)frame->safe_area.width - 640 * ui->scale) * .5f;
    ui->bias_y = (float)frame->safe_area.y + ((float)frame->safe_area.height - 480 * ui->scale) * .5f;
    qa_hud_data data = {.crosshair_visible = true, .crosshair_color = {1, 1, 1, 1}};
    if (hud->options.read && !hud->options.read(hud->options.context, frame, &data, error)) return false;
    if ((data.vital_count && !data.vitals) || (data.bar_count && !data.bars) ||
        (data.timer_count && !data.timers) || (data.score_count && !data.scores) ||
        (data.help_count && !data.help_lines) || (data.caption_count && !data.captions))
        return ui_fail(error, "HUD source returned invalid spans");
    qa_scene_rect target = frame->safe_area;
    if (frame->weapon_only) return weapon_draw(hud, frame, &data, scene, error);
    if (hud->options.source_draw && !hud->options.source_draw(hud->options.context,
        frame, scene, error)) return false;
    qa_combat_state combat;
    if (frame->actor.registry && !data.source_vitals && !data.q1.present && !qa_combat_read(qa_application_combat(hud->options.application),
        frame->actor, &combat, error)) return false;
    qa_hud_value canonical[2];
    if (frame->actor.registry && !data.source_vitals && !data.q1.present) {
        canonical[0] = (qa_hud_value){.label = "Health", .value = combat.health, .warning = combat.health <= 25, .icon = data.health_icon};
        canonical[1] = (qa_hud_value){.label = "Armor", .value = combat.armor.regular.points};
        data.vitals = canonical; data.vital_count = 2;
    }
    qa_inventory *inventory = qa_application_inventory(hud->options.application);
    qa_item_definition *definitions = NULL;
    size_t definition_count = 0;
    if (frame->actor.registry && (frame->show_inventory ||
        (data.selected_weapon && !data.source_vitals && !data.weapon.present))) {
        if (!qa_inventory_item_definitions(inventory, frame->actor, NULL, 0, &definition_count, error)) return false;
        if (definition_count > SIZE_MAX / sizeof(*definitions)) return ui_fail(error, "HUD item definition overflow");
        if (definition_count) {
            definitions = qa_arena_alloc(&scene->storage, definition_count * sizeof(*definitions),
                                          _Alignof(qa_item_definition), error);
            if (!definitions || !qa_inventory_item_definitions(inventory, frame->actor,
                definitions, definition_count, &definition_count, error)) return false;
            for (size_t i = 0; i < definition_count; ++i) {
                const char *label = definitions[i].label ? definitions[i].label : "";
                size_t length = strlen(label);
                char *copy = qa_arena_alloc(&scene->storage, length + 1, 1, error);
                if (!copy) return false;
                memcpy(copy, label, length + 1); definitions[i].label = copy;
            }
        }
    }
    if (frame->actor.registry && data.selected_weapon && !data.source_vitals && !data.weapon.present) {
        for (size_t i = 0; i < definition_count; ++i) {
            const qa_item_definition *weapon = definitions + i;
            if (weapon->item != data.selected_weapon || !weapon->ammo) continue;
            qa_inventory_entry ammo;
            if (!qa_inventory_entry_read(inventory, frame->actor, weapon->ammo, &ammo, error)) return false;
            data.weapon = (qa_hud_weapon){.present = true, .label = weapon->label,
                .ammo_count = ammo.count, .finite_ammo = true, .has_ammo_to_start = ammo.count > 0};
            break;
        }
    }
    if (data.q1.present && !frame->source_status_native) {
        if (!q1_status_draw(hud, frame, &data, scene, error)) return false;
    } else if (!weapon_draw(hud, frame, &data, scene, error)) return false;
    size_t status_count = data.vital_count + (data.weapon.present && !data.weapon.native_status ? 1 : 0);
    for (size_t i = 0; !data.q1.present && i < data.vital_count; ++i)
        if (!status_vital(hud, frame, data.vitals + i,i==0?&data.health_team_face:NULL,
            status_count, i, scene, error)) return false;
    for (size_t i = 0; i < data.bar_count; ++i) {
        const qa_hud_value *bar = &data.bars[i];
        float width = bar->maximum > 0 ? (float)fmax(0, fmin(1, bar->value / bar->maximum)) * 240 : 0;
        float y = 90 + (float)i * 24;
        if (!ui_fill(ui, scene, target, (qa_scene_rect_f){200, y, 240, 16},
                       (qa_scene_vec4){.1f, .1f, .1f, .8f}, error) ||
            !ui_fill(ui, scene, target, (qa_scene_rect_f){200, y, width, 16},
                       (qa_scene_vec4){.7f, .2f, .1f, .9f}, error) ||
            !text(hud, scene, target, 320, y + 2, bar->label, (qa_scene_vec4){1, 1, 1, 1},
                    .9f, QA_FONT_ALIGN_CENTER, error)) return false;
    }
    for (size_t i = 0; i < data.timer_count; ++i) {
        const qa_hud_timer *timer = &data.timers[i];
        if (timer->until_ns <= frame->time_ns) continue;
        double seconds = ceil((double)(timer->until_ns - frame->time_ns) / 1e9);
        float y = 120 + (float)i * 40;
        if (!icon(hud, scene, target, timer->icon, (qa_scene_rect_f){544, y, 28, 28},
            (qa_scene_vec4){1, 1, 1, 1}, error) ||
            !number(hud, scene, target, 590, y, timer->label, seconds, seconds <= 5, error)) return false;
    }
    if (data.crosshair_visible && !frame->show_scores && !frame->show_inventory) {
        float size=data.crosshair_size>0?data.crosshair_size:8;
        if (!isfinite(size) || size<2 || size>32) return ui_fail(error,"Invalid HUD crosshair size");
        qa_scene_rect view = frame->viewport.width && frame->viewport.height ? frame->viewport : target;
        float center_x = ((float)view.x + (float)view.width * .5f - ui->bias_x) / ui->scale;
        float center_y = ((float)view.y + (float)view.height * .5f - ui->bias_y) / ui->scale;
        if (data.crosshair) {
            float picture_size=data.crosshair_size>0?size:16;
            qa_scene_rect_f pixels = {ui->bias_x + (center_x-picture_size*.5f) * ui->scale,
                ui->bias_y + (center_y-picture_size*.5f) * ui->scale,
                picture_size * ui->scale, picture_size * ui->scale};
            if (!qa_scene_frame_picture_f(scene, data.crosshair, target, pixels,
                (qa_scene_vec4){0, 0, 1, 1}, data.crosshair_color, error)) return false;
        } else if (!ui_fill(ui, scene, target, (qa_scene_rect_f){center_x-size*.125f, center_y-size*.5f, size*.25f, size}, data.crosshair_color, error) ||
                   !ui_fill(ui, scene, target, (qa_scene_rect_f){center_x-size*.5f, center_y-size*.125f, size, size*.25f}, data.crosshair_color, error)) return false;
    }
    if (frame->show_inventory && frame->actor.registry) {
        size_t count = 0;
        if (!qa_inventory_entries(inventory, frame->actor, NULL, 0, &count, error)) return false;
        if (count > SIZE_MAX / sizeof(qa_inventory_entry)) return ui_fail(error, "HUD inventory overflow");
        qa_inventory_entry *entries = count ? qa_arena_alloc(&scene->storage, count * sizeof(*entries),
            _Alignof(qa_inventory_entry), error) : NULL;
        if (count && (!entries || !qa_inventory_entries(inventory, frame->actor, entries, count, &count, error))) return false;
        float y = 110;
        for (size_t i = 0; i < count; ++i) {
            if (entries[i].count <= 0) continue;
            for (size_t j = 0; j < definition_count; ++j) {
                if (definitions[j].item != entries[i].item) continue;
                if (!number(hud, scene, target, 320, y, definitions[j].label, entries[i].count, false, error)) return false;
                y += 30; break;
            }
        }
    }
    if (frame->show_scores) {
        for (size_t i = 0; i < data.score_count; ++i) {
            char row[256];
            const qa_hud_score *score = &data.scores[i];
            snprintf(row, sizeof(row), "%s  %s  %d  %d%s", score->name ? score->name : "",
                score->team ? score->team : "", score->score, score->ping, score->spectator ? "  spectator" : "");
            if (!text(hud, scene, target, 64, 110 + (float)i * 20, row,
                score->local ? (qa_scene_vec4){1, .8f, .3f, 1} : (qa_scene_vec4){1, 1, 1, 1},
                1, QA_FONT_ALIGN_LEFT, error)) return false;
        }
    }
    for (size_t i = 0; i < hud->notice_count; ++i) {
        const hud_message *notice = &hud->notices[i];
        if (notice->starts > frame->time_ns) continue;
        if (!ui_draw_source_text(ui, scene, target, 16, 20 + (float)i * 16, notice->text,
            notice->chat ? (qa_scene_vec4){.6f, 1, .6f, 1} : (qa_scene_vec4){1, 1, 1, 1},
            1, QA_FONT_ALIGN_LEFT, error)) return false;
    }
    if (hud->center_count && hud->centers[0].starts <= frame->time_ns) {
        const hud_message *center = &hud->centers[0];
        const char *value = center->text;
        if (!center->instant && center->character_ns) {
            uint64_t characters = (frame->time_ns - center->starts) / center->character_ns;
            qa_bytes bytes = {(const uint8_t *)value, strlen(value)};
            size_t offset = 0; uint32_t scalar;
            while (characters && qa_utf8_next(bytes, &offset, &scalar)) --characters;
            char *shown = qa_arena_alloc(&scene->storage, offset + 1, 1, error);
            if (!shown) return false;
            memcpy(shown, value, offset); shown[offset] = 0; value = shown;
        }
        float center_x = ((float)target.x + (float)target.width * .5f - ui->bias_x) / ui->scale;
        float center_y = ((float)target.y + (center->lines <= 4 ? (float)target.height * .35f : 48) - ui->bias_y) / ui->scale;
        if (!ui_draw_source_text(ui, scene, target, center_x, center_y, value, (qa_scene_vec4){1, 1, 1, 1}, 1,
            QA_FONT_ALIGN_CENTER, error)) return false;
    }
    if (hud->pickup && hud->pickup_until > frame->time_ns &&
        (!icon(hud, scene, target, hud->pickup_icon, (qa_scene_rect_f){304, 328, 32, 32},
            (qa_scene_vec4){1, 1, 1, 1}, error) || !text(hud, scene, target, 320, 360, hud->pickup, (qa_scene_vec4){1, 1, .5f, 1},
               1, QA_FONT_ALIGN_CENTER, error))) return false;
    if (hud->hit_damage > 0 && hud->hit_until > frame->time_ns &&
        !text(hud, scene, target, 320, 256, "X", (qa_scene_vec4){1, 1, 1, 1}, 1,
               QA_FONT_ALIGN_CENTER, error)) return false;
    if (data.help_title && !text(hud, scene, target, 24, 90, data.help_title,
        (qa_scene_vec4){1, 1, 1, 1}, 1, QA_FONT_ALIGN_LEFT, error)) return false;
    for (size_t i = 0; i < data.help_count; ++i)
        if (!text(hud, scene, target, 24, 108 + (float)i * 16, data.help_lines[i],
            (qa_scene_vec4){1, 1, 1, 1}, 1, QA_FONT_ALIGN_LEFT, error)) return false;
    if (!ctf_draw(hud, frame, scene, error)) return false;
    float fit=fminf((float)target.width/640,(float)target.height/480);
    return caption_draw(ui,data.captions,data.caption_count,target,
        (qa_scene_rect_f){(float)target.x+8,(float)target.y+(float)target.height*.60f,
            (float)target.width-16,(float)target.height*.22f},fit,scene,error);
}
bool qa_hud_draw(qa_hud *hud, const qa_hud_frame *frame, qa_scene_frame *scene, qa_error *error) {
    if (!hud || !frame || !scene || frame->seat != hud->options.seat || hud->drawing ||
        hud->options.ui->handling ||
        !frame->safe_area.width || !frame->safe_area.height || !isfinite(frame->scale) || frame->scale <= 0 ||
        (double)frame->safe_area.x + frame->safe_area.width > INT32_MAX ||
        (double)frame->safe_area.y + frame->safe_area.height > INT32_MAX)
        return ui_fail(error, "invalid or reentrant seat HUD draw");
    expire(hud->notices, &hud->notice_count, frame->time_ns);
    expire(hud->centers, &hud->center_count, frame->time_ns);
    if (!frame->visible) return true;
    qa_ui *ui = hud->options.ui;
    float scale = ui->scale, x = ui->bias_x, y = ui->bias_y;
    qa_ui_presentation prior = {.fonts = ui->options.fonts, .text_scale = ui->text_scale, .color_mode = ui->color_mode};
    hud->drawing = true;
    ui->handling = true;
    ui->drawing = true;
    bool ok = true;
    if (hud->options.presentation) {
        qa_ui_presentation presentation;
        ok = hud->options.presentation(hud->options.context, &presentation, error);
        if (ok && (presentation.fonts.seat != frame->seat || !presentation.fonts.classic ||
            !isfinite(presentation.text_scale) || presentation.text_scale <= 0))
            ok = ui_fail(error, "HUD presentation lost its actual seat fonts or text scale");
        if (ok) {
            ui->options.fonts = presentation.fonts; ui->text_scale = presentation.text_scale;
            ui->color_mode = presentation.color_mode;
        }
    }
    if (ok) ok = draw(hud, frame, scene, error);
    hud->drawing = false;
    ui->handling = false;
    ui->drawing = false;
    ui->scale = scale; ui->bias_x = x; ui->bias_y = y;
    ui->options.fonts = prior.fonts; ui->text_scale = prior.text_scale; ui->color_mode = prior.color_mode;
    return ok;
}
