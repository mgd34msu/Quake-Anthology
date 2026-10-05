#include "internal.h"
#include "native_q2_save.h"
#include "capture.h"
#include "resource_bindings.h"
#include "shared_resource_policy.h"
#include "save_private.h"
#include "source_restore.h"
#include "qa/persistence_content.h"
#include "qa/font_save.h"
#include "qa/font_world_save.h"
#include "qa/scene_resource_save.h"
#include "qa/localization.h"
#include "qa/binary.h"
#include <SDL.h>
#include <math.h>
#include <stdio.h>

typedef struct native_q2_picture {
    struct native_q2_picture *next;
    char *name;
    const qa_scene_image *image;
} native_q2_picture;
typedef struct native_q2_string {
    qa_native_instance *instance;
    qa_native_address address;
    size_t capacity;
} native_q2_string;
struct frontend_native_q2 {
    struct frontend_native_q2 *next;
    qa_frontend *frontend;
    qa_application *application;
    qa_actor_owner owner;
    void *owner_context;
    bool (*owner_idle)(void *);
    unsigned active_imports;
    uint64_t identity;
    qa_native_profile profile;
    qa_vfs *mounts;
    const qa_vfs *provider_files; /* Borrowed from the genuine provider launch. */
    qa_cvars *cvars; /* Borrowed through guest shutdown and frontend lease release. */
    qa_scene_resources *images;
    qa_audio_bank *sounds;
    qa_font_library *fonts;
    const qa_font *classic;
    qa_localization_pool *catalogs;
    qa_font_world_store *world_text;
    native_q2_picture *pictures;
    frontend_native_q2_image_policy *image_policy;
    native_q2_string strings[8];
    size_t next_string;
    uint64_t frame_time_ns, previous_frame_time_ns;
    float frame_seconds;
    uint32_t seat;
    qa_scene_rect viewport;
    bool seat_bound, alternate;
    bool prepared, restored;
};

static bool source_files(frontend_native_q2 *source, qa_error *error)
{
    if (source->images && source->sounds) return true;
    if (!source->mounts) return frontend_fail(error, QA_ERROR_ARGUMENT, "native Q2 frontend lease lacks its source content view");
    if (!source->images) {
        qa_scene_resources *images = qa_scene_resources_create(source->mounts, error);
        if (!images) return false;
        if (!frontend_image_policy_initialize(source->frontend, images, error)) {
            qa_scene_resources_destroy(images); return false;
        }
        source->images = images;
    }
    return source->images && (source->sounds || qa_audio_bank_create(source->mounts, &source->sounds, error));
}
static bool text_argument(const qa_native_host_q2_application_call *call, size_t index,
    qa_buffer *out, qa_error *error)
{
    if (index >= call->import->argument_count || call->import->arguments[index].type != QA_NATIVE_ADDRESS)
        return frontend_fail(error, QA_ERROR_FORMAT, "native Q2 string argument has wrong type");
    return qa_native_read_string(call->instance, call->import->arguments[index].as.address,
        1048576, out, error);
}
static bool return_string(frontend_native_q2 *source, qa_native_instance *instance,
    const char *text, qa_native_value *result, qa_error *error)
{
    size_t length = strlen(text);
    native_q2_string *item = &source->strings[source->next_string];
    if (item->instance && item->instance != instance)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "native Q2 frontend lease cannot change guest instances");
    if (length + 1 > item->capacity) {
        qa_native_address replacement = 0;
        if (!qa_native_allocate(instance, length + 1, INT32_MIN, &replacement, error)) return false;
        bool ok = qa_native_write(instance, replacement, (qa_bytes){(const uint8_t *)text, length + 1}, error);
        if (ok && item->address) ok = qa_native_free(instance, item->address, error);
        if (!ok) { qa_error cleanup = {0}; (void)qa_native_free(instance, replacement, &cleanup); return false; }
        item->address = replacement; item->capacity = length + 1; item->instance = instance;
    } else if (!qa_native_write(instance, item->address, (qa_bytes){(const uint8_t *)text, length + 1}, error)) {
        return false;
    }
    result->as.address = item->address; source->next_string = (source->next_string + 1) % 8; return true;
}
static bool vector_argument(const qa_native_host_q2_application_call *call, size_t index,
    qa_vec3 *out, qa_error *error)
{
    uint8_t bytes[12];
    if (!qa_native_read(call->instance, call->import->arguments[index].as.address, bytes, sizeof(bytes), error)) return false;
    *out = qa_v3(qa_load_f32le(bytes), qa_load_f32le(bytes + 4), qa_load_f32le(bytes + 8));
    return true;
}
static bool color_argument(const qa_native_host_q2_application_call *call, size_t index,
    qa_scene_vec4 *out, qa_error *error)
{
    uint8_t bytes[4];
    if (!qa_native_read(call->instance, call->import->arguments[index].as.address, bytes, sizeof(bytes), error)) return false;
    *out = (qa_scene_vec4){bytes[0] / 255.f, bytes[1] / 255.f, bytes[2] / 255.f, bytes[3] / 255.f};
    return true;
}
static bool catalog_for(frontend_native_q2 *source, qa_localization **out, qa_error *error)
{
    if (!source_files(source, error)) return false;
    qa_cvars *cvars = qa_application_cvars(source->application);
    const qa_cvar_view *language = NULL;
    if (source->seat_bound) {
        char name[64]; snprintf(name, sizeof(name), "ui_seat%u_language", source->seat + 1);
        language = qa_cvars_find(cvars, name);
    }
    if (!language) language = qa_cvars_find(source->cvars, "language");
    qa_localization_options options = {.profile = QA_LOCALIZATION_Q2_RERELEASE};
    return qa_localization_acquire(source->catalogs, source->mounts,
        language ? language->value : "english", &options, out, error);
}
static bool localize(frontend_native_q2 *source, const qa_native_host_q2_application_call *call,
    qa_native_value *result, qa_error *error)
{
    if (!source_files(source, error)) return false;
    uint64_t count = call->import->arguments[2].as.u64;
    if (count > 8) return frontend_fail(error, QA_ERROR_FORMAT, "native Q2 localization exceeds eight arguments");
    qa_buffer base = {0}, arguments[8] = {{0}};
    const char *texts[8] = {0}; bool ok = text_argument(call, 0, &base, error);
    qa_native_address pointers = call->import->arguments[1].as.address;
    uint8_t pointer_bytes = call->import->signature->abi == QA_NATIVE_ABI_CDECL_I386 ||
        call->import->signature->abi == QA_NATIVE_ABI_SYSTEM_V_I386 ? 4 : 8;
    for (size_t i = 0; ok && i < count; ++i) {
        uint8_t bytes[8];
        if (pointers > UINT64_MAX - i * pointer_bytes) { ok = frontend_fail(error, QA_ERROR_FORMAT, "native Q2 localization pointer overflow"); break; }
        ok = qa_native_read(call->instance, pointers + i * pointer_bytes, bytes, pointer_bytes, error);
        if (ok) {
            qa_native_address address = pointer_bytes == 4 ? qa_load_u32le(bytes) : qa_load_u64le(bytes);
            ok = qa_native_read_string(call->instance, address, 1048576, &arguments[i], error);
        }
        texts[i] = (const char *)arguments[i].data;
    }
    qa_localization *catalog = NULL;
    if (ok) ok = catalog_for(source, &catalog, error);
    char translated[1024];
    if (ok) {
        qa_localize(catalog, (const char *)base.data, texts, (size_t)count, true, true,
            translated, sizeof(translated));
        ok = return_string(source, call->instance, translated, result, error);
    }
    qa_localization_release(catalog); qa_buffer_free(&base);
    for (size_t i = 0; i < 8; ++i) qa_buffer_free(&arguments[i]);
    return ok;
}
static bool picture(frontend_native_q2 *source, const char *name,
    const qa_scene_image **out, qa_error *error)
{
    for (native_q2_picture *item = source->pictures; item; item = item->next)
        if (!strcmp(item->name, name)) { *out = item->image; return true; }
    if (!source_files(source, error)) return false;
    char path[1024];
    int length = name[0] == '/' || name[0] == '\\' ? snprintf(path, sizeof(path), "%s", name + 1) :
        snprintf(path, sizeof(path), "pics/%s", name);
    if (length < 0 || (size_t)length >= sizeof(path)) return frontend_fail(error, QA_ERROR_ARGUMENT, "native Q2 picture path exceeds source limit");
    qa_scene_image_options options = {.family = QA_SCENE_Q2, .wrap = QA_SCENE_CLAMP,
        .filter = QA_SCENE_LINEAR, .usage = QA_IMAGE_USAGE_PICTURE, .transparent = true, .transparent_index = 255};
    qa_scene_image *image = NULL;
    qa_error load_error = {0};
    if (!qa_scene_image_load(source->images, path, &options, &image, &load_error)) {
        if (load_error.code == QA_ERROR_NOT_FOUND) { *out = NULL; return true; }
        if (error) *error = load_error;
        return false;
    }
    if (!image) { *out = NULL; return true; }
    native_q2_picture *item = calloc(1, sizeof(*item));
    if (item) item->name = malloc(strlen(name) + 1);
    if (!item || !item->name) { if (item) free(item); qa_scene_image_release(image); return frontend_fail(error, QA_ERROR_MEMORY, "retaining native Q2 picture"); }
    strcpy(item->name, name); item->image = image; item->next = source->pictures; source->pictures = item;
    *out = image; return true;
}
static bool seat_ready(frontend_native_q2 *source, qa_error *error)
{
    return source->seat_bound && source->seat < source->frontend->options.seats &&
        source->frontend->display ? true : frontend_fail(error, QA_ERROR_UNSUPPORTED, "native Q2 draw requires a bound local seat");
}
static bool source_font(frontend_native_q2 *source, qa_error *error)
{
    if (source->classic) return true;
    if (!source_files(source, error)) return false;
    if (!source->fonts) source->fonts = qa_font_library_create(source->mounts, source->images, error);
    if (!source->fonts) return false;
    qa_scene_image_options options = {.family = QA_SCENE_Q2, .wrap = QA_SCENE_CLAMP,
        .filter = QA_SCENE_NEAREST, .usage = QA_IMAGE_USAGE_PICTURE, .transparent = true, .transparent_index = 255};
    qa_scene_image *image = NULL;
    if (!qa_scene_image_load(source->images, "pics/conchars.pcx", &options, &image, error)) return false;
    bool ok = qa_font_classic_create(source->fonts, "native-q2:conchars", image,
        QA_FONT_BAKED_COLOR, &source->classic, error);
    qa_scene_image_release(image); return ok;
}
static const char *binding_name(frontend_native_q2 *source, const char *command, char out[96])
{
    qa_input_seat *seat = source->frontend->seats[source->seat].input;
    for (size_t i = 0; i < qa_input_seat_binding_count(seat); ++i) {
        const qa_input_binding *binding = qa_input_seat_binding_at(seat, i);
        const char *text = binding->kind == QA_BIND_COMMAND ? binding->command : qa_input_action_command(binding->action);
        if (text && !SDL_strcasecmp(text, command) && qa_input_physical_name(binding->input, out, 96)) return out;
    }
    out[0] = 0; return out;
}
static bool font_layout(frontend_native_q2 *source, const char *text, int32_t scale,
    qa_scene_vec4 color, qa_font_alignment alignment, qa_arena *scratch, qa_font_layout *out,
    qa_error *error)
{
    if (!seat_ready(source, error) || scale <= 0) return frontend_fail(error, QA_ERROR_ARGUMENT, "native Q2 font scale or seat is invalid");
    if (!source_font(source, error)) return false;
    qa_font_selection selection = source->frontend->seats[source->seat].fonts;
    selection.classic = source->classic;
    const qa_cvar_view *use_font = qa_cvars_find(source->cvars, "scr_usekfont");
    bool selected = !use_font || use_font->integer != 0;
    if (!selected) { selection.primary = NULL; selection.fallbacks = NULL; selection.fallback_count = 0; }
    qa_font_layout_options options = {.text = {(const uint8_t *)text, strlen(text)},
        .scale = (float)scale, .color = color, .color_codes = QA_FONT_COLOR_LITERAL,
        .alignment = alignment, .alternate = source->alternate, .line_height = (selected ? 10.f : 8.f) * (float)scale};
    return qa_font_layout_build(&selection, &options, scratch, out, error);
}
static bool draw_text(frontend_native_q2 *source, const char *text, int32_t x, int32_t y,
    int32_t scale, qa_scene_vec4 color, bool shadow, qa_font_alignment alignment, qa_error *error)
{
    qa_arena scratch; qa_arena_init(&scratch, 16384); qa_font_layout layout;
    bool ok = font_layout(source, text, scale, color, alignment, &scratch, &layout, error);
    if (ok) {
        float origin_x = (float)x;
        if (alignment == QA_FONT_ALIGN_CENTER) origin_x -= layout.width * .5f;
        else if (alignment == QA_FONT_ALIGN_RIGHT) origin_x -= layout.width;
        qa_font_draw_options draw = {.seat = source->seat, .target = source->viewport,
            .origin = {origin_x - (float)source->viewport.x, (float)y - (float)source->viewport.y},
            .space = QA_FONT_PIXELS, .shadow_offset = shadow ? (float)scale : 0};
        ok = qa_font_draw_layout(&source->frontend->frame, &layout, &draw, error);
    }
    qa_arena_destroy(&scratch); return ok;
}
static bool world_text(frontend_native_q2 *source, const qa_native_host_q2_application_call *call,
    qa_error *error)
{
    bool fixed = call->import->slot == 60; size_t offset = fixed ? 1 : 0;
    qa_font_world_text text = {.orientation = fixed ? QA_FONT_WORLD_FIXED : QA_FONT_WORLD_BILLBOARD,
        .font = QA_FONT_WORLD_CLASSIC, .distance_cull_factor = .004f, .has_distance_cull = true,
        .content = source->owner};
    uint8_t bytes[255]; size_t used = 0;
    qa_native_address address = call->import->arguments[1 + offset].as.address;
    for (size_t i = 0; i < 127; ++i) {
        uint8_t byte;
        if (address > UINT64_MAX - i || !qa_native_read(call->instance, address + i, &byte, 1, error)) return false;
        if (!byte) break;
        if (byte < 128) bytes[used++] = byte;
        else { bytes[used++] = (uint8_t)(0xc0u | byte >> 6); bytes[used++] = (uint8_t)(0x80u | (byte & 63u)); }
    }
    text.text = (qa_bytes){bytes, used};
    if (!vector_argument(call, 0, &text.origin, error) ||
        (fixed && !vector_argument(call, 1, &text.angles, error)) || !color_argument(call, 2 + offset, &text.color, error)) return false;
    text.cell_size = call->import->arguments[3 + offset].as.f32 * 8;
    text.depth_test = call->import->arguments[5 + offset].as.u8 != 0;
    return qa_font_world_store_submit(source->world_text, &text,
        (double)source->frontend->time_ns / 1000000000, call->import->arguments[4 + offset].as.f32, error);
}
static bool application_import_body(void *context, const qa_native_host_q2_application_call *call,
    qa_native_value *result, qa_error *error)
{
    frontend_native_q2 *source = context; const qa_native_import_call *import = call->import;
    const qa_native_value *args = import->arguments;
    if (import->profile != source->profile) return frontend_fail(error, QA_ERROR_FORMAT, "native Q2 platform profile mismatch");
    if (source->profile == QA_NATIVE_Q2_CGAME_API2023) {
        source->seat_bound = call->seat_bound;
        if (source->seat_bound && !frontend_seat_ordinal_read(source->frontend,call->seat,&source->seat))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "native Q2 entry seat is unavailable");
        if (source->seat_bound) source->viewport = frontend_viewport(source->frontend, source->seat);
        if (!strcmp(import->name, "CL_ClientRealTime")) {
            result->as.u64 = source->frontend->time_ns / UINT64_C(1000000); return true;
        }
        if (!strcmp(import->name, "CL_FrameTime")) { result->as.f32 = source->frame_seconds; return true; }
        if (!strcmp(import->name, "CL_InAutoDemoLoop")) {
            /* This lease presents the local authoritative session; demo peers
             * require a separate admitted client presentation producer. */
            result->as.u8 = 0; return true;
        }
    }
    if (source->profile == QA_NATIVE_Q2_GAME_API2023) {
        if (import->slot >= 54 && import->slot <= 63 && import->slot != 59 && import->slot != 60) {
            qa_arena scratch; qa_arena_init(&scratch, 16384);
            const qa_debug_line *lines = NULL; size_t count = 0; uint32_t lifetime = 0; bool handled = false;
            bool ok = qa_debug_native_q2(call->instance, import, &scratch, &lines, &count, &lifetime, &handled, error);
            qa_tools *tools = frontend_tools_owner(source->frontend);
            if (ok && handled) {
                if (!tools) ok = frontend_fail(error, QA_ERROR_UNSUPPORTED, "native Q2 debug owner is absent");
                else ok = qa_debug_store_submit(qa_tools_debug(tools), lines, count,
                    (double)source->frontend->time_ns / 1000000, lifetime, error);
            }
            qa_arena_destroy(&scratch); return ok && handled;
        }
        if (import->slot == 59 || import->slot == 60) return world_text(source, call, error);
        if (!strcmp(import->name, "SendToClipBoard")) {
            qa_buffer text = {0}; bool ok = text_argument(call, 0, &text, error);
            if (ok) ok = frontend_clipboard_write(source->frontend, (const char *)text.data, error);
            qa_buffer_free(&text); return ok;
        }
    }
    if (source->profile != QA_NATIVE_Q2_CGAME_API2023)
        return frontend_fail(error, QA_ERROR_UNSUPPORTED, "native Q2 import requires canonical application service");
    if (!strcmp(import->name, "Localize")) return localize(source, call, result, error);
    if (import->slot == 20) {
        if (!seat_ready(source, error)) return false;
        qa_buffer command = {0}; char name[96];
        bool ok = text_argument(call, 0, &command, error) && return_string(source, call->instance,
            binding_name(source, (const char *)command.data, name), result, error);
        qa_buffer_free(&command); return ok;
    }
    if (import->slot == 30) {
        if (!seat_ready(source, error)) return false;
        frontend_seat *seat = &source->frontend->seats[source->seat];
        bool active = qa_input_seat_focus(seat->input) == QA_INPUT_CHAT;
        result->as.u8 = active;
        if (!active) return true;
        qa_native_value text = {.type = QA_NATIVE_ADDRESS};
        qa_field_view field = qa_text_field_read(qa_seat_console_field(seat->console, true));
        if (!return_string(source, call->instance, field.text, &text, error)) return false;
        uint8_t bytes[8], team = seat->chat_team ? 1 : 0;
        bool narrow = import->signature->abi == QA_NATIVE_ABI_CDECL_I386 || import->signature->abi == QA_NATIVE_ABI_SYSTEM_V_I386;
        if (narrow && text.as.address > UINT32_MAX) return frontend_fail(error, QA_ERROR_FORMAT, "native Q2 text address exceeds source pointer width");
        if (narrow) qa_store_u32le(bytes, (uint32_t)text.as.address); else qa_store_u64le(bytes, text.as.address);
        return qa_native_write(call->instance, args[0].as.address, (qa_bytes){bytes, narrow ? 4 : 8}, error) &&
            qa_native_write(call->instance, args[1].as.address, (qa_bytes){&team, 1}, error);
    }
    if (import->slot == 33) {
        if (!seat_ready(source, error)) return false;
        if (args[0].as.i32 < 0 || (uint32_t)args[0].as.i32 != call->seat)
            return frontend_fail(error, QA_ERROR_ARGUMENT, "native Q2 binding draw seat differs from active entry");
        qa_buffer command = {0}, purpose = {0}; qa_localization *catalog = NULL;
        bool ok = text_argument(call, 1, &command, error) && text_argument(call, 2, &purpose, error) && catalog_for(source, &catalog, error);
        char label[1024], key[96], line[1200];
        if (ok) {
            qa_localize(catalog, (const char *)purpose.data, NULL, 0, true, false, label, sizeof(label));
            binding_name(source, (const char *)command.data, key);
            snprintf(line, sizeof(line), "%s%s%s %s", *key ? "[" : "<", *key ? key : "unbound", *key ? "]" : ">", label);
            ok = draw_text(source, line, args[3].as.i32, args[4].as.i32, args[5].as.i32,
                (qa_scene_vec4){1, 1, 1, 1}, false, QA_FONT_ALIGN_CENTER, error);
            if (ok) result->as.i32 = 8;
        }
        qa_localization_release(catalog); qa_buffer_free(&command); qa_buffer_free(&purpose); return ok;
    }
    if (!strcmp(import->name, "SCR_SetAltTypeface")) { source->alternate = args[0].as.u8 != 0; return true; }
    if (import->slot == 23) {
        if (!seat_ready(source, error)) return false;
        int32_t scale = args[2].as.i32;
        if (scale <= 0) return frontend_fail(error, QA_ERROR_ARGUMENT, "native Q2 character scale is invalid");
        uint32_t character = (uint32_t)args[3].as.i32 & 255u;
        if (args[4].as.u8) character ^= 128u;
        qa_font_glyph glyph;
        if (!source_font(source, error)) return false;
        if (!qa_font_find_glyph(source->classic, character, &glyph) ||
            !glyph.visible || !glyph.image) return true;
        return qa_scene_frame_picture_f(&source->frontend->frame, glyph.image, source->viewport,
            (qa_scene_rect_f){(float)args[0].as.i32, (float)args[1].as.i32, 8.f * (float)scale, 8.f * (float)scale},
            glyph.uv, (qa_scene_vec4){1, 1, 1, 1}, error);
    }
    if (!strcmp(import->name, "Draw_RegisterPic") || !strcmp(import->name, "Draw_GetPicSize") ||
        !strcmp(import->name, "SCR_DrawPic") || !strcmp(import->name, "SCR_DrawColorPic")) {
        size_t index = !strcmp(import->name, "Draw_RegisterPic") ? 0 : !strcmp(import->name, "Draw_GetPicSize") ? 2 : 4;
        qa_buffer name = {0}; const qa_scene_image *image = NULL;
        bool ok = text_argument(call, index, &name, error) && picture(source, (const char *)name.data, &image, error);
        if (ok && import->slot == 21) result->as.u8 = image != NULL;
        else if (ok && import->slot == 22) {
            uint8_t width[4], height[4]; qa_store_u32le(width, image ? image->logical_width : UINT32_MAX); qa_store_u32le(height, image ? image->logical_height : UINT32_MAX);
            ok = qa_native_write(call->instance, args[0].as.address, (qa_bytes){width, 4}, error) &&
                qa_native_write(call->instance, args[1].as.address, (qa_bytes){height, 4}, error);
        } else if (ok) {
            ok = seat_ready(source, error); qa_scene_vec4 color = {1, 1, 1, 1};
            if (ok && import->slot == 25) ok = color_argument(call, 5, &color, error);
            if (ok && image) ok = qa_scene_frame_picture_f(&source->frontend->frame, image, source->viewport,
                (qa_scene_rect_f){(float)args[0].as.i32, (float)args[1].as.i32,
                    (float)args[2].as.i32, (float)args[3].as.i32}, (qa_scene_vec4){0, 0, 1, 1}, color, error);
        }
        qa_buffer_free(&name); return ok;
    }
    if (import->slot == 27 || import->slot == 28) {
        qa_buffer text = {0}; bool ok = text_argument(call, 0, &text, error);
        if (ok && import->slot == 27) {
            qa_scene_vec4 color; int32_t align = args[6].as.i32;
            if (align < 0 || align > 2) ok = frontend_fail(error, QA_ERROR_FORMAT, "native Q2 text alignment is invalid");
            else if (color_argument(call, 4, &color, error)) ok = draw_text(source, (const char *)text.data,
                args[1].as.i32, args[2].as.i32, args[3].as.i32, color, args[5].as.u8 != 0,
                (qa_font_alignment)align, error);
            else ok = false;
        } else if (ok) {
            qa_arena scratch; qa_arena_init(&scratch, 16384); qa_font_layout layout;
            ok = font_layout(source, (const char *)text.data, args[1].as.i32, (qa_scene_vec4){1, 1, 1, 1}, QA_FONT_ALIGN_LEFT, &scratch, &layout, error);
            if (ok && (result->type != QA_NATIVE_BYTES || result->as.bytes.size < 8 || !result->as.bytes.data))
                ok = frontend_fail(error, QA_ERROR_FORMAT, "native Q2 text measurement result storage is invalid");
            if (ok) {
                uint32_t width, height; memcpy(&width, &layout.width, 4); memcpy(&height, &layout.height, 4);
                qa_store_u32le(result->as.bytes.data, width); qa_store_u32le((uint8_t *)result->as.bytes.data + 4, height);
            }
            qa_arena_destroy(&scratch);
        }
        qa_buffer_free(&text); return ok;
    }
    if (import->slot == 29) {
        qa_arena scratch; qa_arena_init(&scratch, 4096); qa_font_layout layout;
        bool ok = font_layout(source, "", args[0].as.i32, (qa_scene_vec4){1, 1, 1, 1}, QA_FONT_ALIGN_LEFT, &scratch, &layout, error);
        if (ok) result->as.f32 = layout.line_height;
        qa_arena_destroy(&scratch); return ok;
    }
    return frontend_fail(error, QA_ERROR_UNSUPPORTED, "native Q2 platform import is not bound");
}
static void platform_print_body(void *context, const qa_native_host_print *print)
{
    frontend_native_q2 *source = context; qa_frontend *frontend = source->frontend;
    if (frontend->native_print) {
        frontend->native_print(frontend->native_output_context, print); return;
    }
    if (!print || !print->text || print->kind == QA_NATIVE_HOST_PRINT_DEBUG) return;
    fputs(print->text, stdout);
    if (frontend->options.dedicated) return;
    for (uint32_t seat = 0; seat < frontend->options.seats; ++seat) {
        qa_actor_id actor;
        if (print->kind != QA_NATIVE_HOST_PRINT_BROADCAST &&
            (!frontend_seat_actor_read(frontend,seat,&actor) ||
                !qa_actor_id_equal(actor, print->client))) continue;
        qa_error error = {0};
        bool ok;
        if (print->kind == QA_NATIVE_HOST_PRINT_CENTER) {
            const qa_cvar_view *time = qa_cvars_find(source->cvars, "scr_centertime");
            double seconds = time && isfinite(time->number) ? fmax(0, fmin(time->number, 86400)) : 2.5;
            ok = qa_hud_center_print(frontend->seats[seat].hud, print->text, frontend->time_ns,
                (uint64_t)(seconds * 1000000000), true, 0, &error);
        } else ok = qa_seat_console_print(frontend->seats[seat].console, print->text, &error);
        if (!ok) fprintf(stderr, "native Q2 print: %s\n", error.message);
    }
}
static bool platform_sound_body(void *context, const qa_native_host_sound *event, qa_error *error)
{
    frontend_native_q2 *source = context; qa_frontend *frontend = source->frontend;
    if (!frontend->audio) return true;
    if (!event || !event->name || !*event->name)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "native Q2 sound lacks its canonical source resource name");
    if (!event->audience_captured || !event->recipient_count) return true;
    if (!event->recipients)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Native Q2 sound lost its captured full recipient identities");
    if (!source_files(source, error)) return false;
    qa_audio_asset *asset = NULL;
    if (!qa_audio_bank_register(source->sounds, event->name, QA_AUDIO_Q2, &asset, error)) return false;
    if (!asset) return true;
    uint64_t actor = frontend_audio_actor(frontend, event->actor, error);
    if (event->actor.registry && actor == QA_AUDIO_NO_ACTOR) { qa_audio_asset_release(asset); return false; }
    qa_audio_play sound = {.sample = qa_audio_asset_sample(asset), .asset = asset,
        .resource_id = qa_resource_id(qa_audio_asset_resource(asset)), .name = event->name, .family = QA_AUDIO_Q2,
        .actor = actor, .owner = source->identity,
        .origin_kind = event->positioned ? QA_AUDIO_FIXED : QA_AUDIO_ACTOR,
        .origin_actor = actor, .origin = event->origin, .channel = event->channel,
        .volume = event->volume, .attenuation = event->attenuation, .delay_seconds = event->time_offset};
    bool ok = true;
    for (uint32_t seat = 0; ok && seat < frontend->options.seats; ++seat) {
        qa_actor_id recipient;
        if (!frontend_seat_actor_read(frontend, seat, &recipient)) continue;
        bool admitted = false;
        for (size_t i = 0; i < event->recipient_count; ++i)
            if (qa_actor_id_equal(recipient, event->recipients[i])) { admitted = true; break; }
        if (!admitted) continue;
        sound.audience = seat;
        ok = qa_audio_engine_play(frontend->audio, &sound,
            (int32_t)((frontend->time_ns / 1000000) & INT32_MAX), error);
    }
    qa_audio_asset_release(asset); return ok;
}
static bool platform_hud_view_body(void *context, uint32_t launch_seat,
    qa_native_host_q2_hud_view *out, qa_error *error)
{
    frontend_native_q2 *source = context;
    uint32_t seat;
    if (!out || source->frontend->options.dedicated || !frontend_seat_ordinal_read(source->frontend,launch_seat,&seat))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "native Q2 HUD requires an available local seat");
    qa_scene_rect rect = frontend_viewport(source->frontend, seat);
    if (rect.width > INT32_MAX || rect.height > INT32_MAX)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "native Q2 HUD viewport exceeds source integer bounds");
    *out = (qa_native_host_q2_hud_view){.x = rect.x, .y = rect.y,
        .width = (int32_t)rect.width, .height = (int32_t)rect.height,
        .safe_x = rect.x, .safe_y = rect.y,
        .safe_width = (int32_t)rect.width, .safe_height = (int32_t)rect.height, .scale = 1};
    return true;
}
static bool application_import(void *context, const qa_native_host_q2_application_call *call,
    qa_native_value *result, qa_error *error)
{
    frontend_native_q2 *source = context;
    if (source->image_policy)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Native Q2 import overlaps its actual prepared image bindings");
    ++source->active_imports;
    bool ok = application_import_body(context, call, result, error);
    --source->active_imports;
    return ok;
}
static void platform_print(void *context, const qa_native_host_print *print)
{
    frontend_native_q2 *source = context;
    ++source->active_imports;
    platform_print_body(context, print);
    --source->active_imports;
}
static bool platform_sound(void *context, const qa_native_host_sound *event, qa_error *error)
{
    frontend_native_q2 *source = context;
    ++source->active_imports;
    bool ok = platform_sound_body(context, event, error);
    --source->active_imports;
    return ok;
}
static bool platform_hud_view(void *context, uint32_t seat,
    qa_native_host_q2_hud_view *out, qa_error *error)
{
    frontend_native_q2 *source = context;
    ++source->active_imports;
    bool ok = platform_hud_view_body(context, seat, out, error);
    --source->active_imports;
    return ok;
}
static bool platform_resource_precache(void *context, qa_native_host_resource_kind kind,
    const char *logical, const qa_vfs **files, qa_resource **resource,
    qa_vfs_acquisition *opening, bool *found, qa_error *error)
{
    frontend_native_q2 *source = context;
    if (!source || kind != QA_NATIVE_HOST_IMAGE || !logical || !*logical || !files || *files ||
        !resource || *resource || !opening || !found || source->image_policy)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Native Q2 image precache lacks its actual Source outputs");
    ++source->active_imports;
    *found = false;
    bool ok = source_files(source, error);
    char path[1024];
    int length = logical[0] == '/' || logical[0] == '\\' ? snprintf(path, sizeof(path), "%s", logical + 1) :
        snprintf(path, sizeof(path), "pics/%s", logical);
    if (ok && (length < 0 || (size_t)length >= sizeof(path)))
        ok = frontend_fail(error, QA_ERROR_ARGUMENT, "Native Q2 image precache path exceeds Source limit");
    qa_scene_image *image = NULL; qa_scene_image_load_receipt receipt = {0}; qa_error local = {0};
    if (ok) {
        qa_scene_image_options options = {.family = QA_SCENE_Q2, .wrap = QA_SCENE_CLAMP,
            .filter = QA_SCENE_LINEAR, .usage = QA_IMAGE_USAGE_PICTURE, .transparent = true, .transparent_index = 255};
        bool decoded = qa_scene_image_load_observed(source->images, path, &options, &image, &receipt, &local);
        if (!decoded && (local.code == QA_ERROR_MEMORY || (!receipt.source && local.code != QA_ERROR_NOT_FOUND))) {
            if (error) *error = local;
            ok = false;
        } else if (receipt.source) {
            ok = receipt.source_opening.opening_present &&
                qa_vfs_acquisition_copy(&receipt.source_opening, opening, error);
            if (ok) {
                *resource = receipt.source; receipt.source = NULL;
                *files = source->mounts; *found = true;
            } else if (!error || error->code == QA_OK)
                frontend_fail(error, QA_ERROR_FORMAT, "Native Q2 image winner lacks its actual opening receipt");
        }
    }
    qa_scene_image_release(image); qa_scene_image_load_receipt_dispose(&receipt);
    --source->active_imports;
    return ok;
}
static bool source_free(frontend_native_q2 *source, qa_error *error)
{
    if (!frontend_owners_idle(source->frontend))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Native Q2 frontend source retains an active parent or child owner");
    if (source->frontend->audio && source->identity) {
        if (!qa_audio_engine_stop_owner(source->frontend->audio, source->identity, QA_AUDIO_WORLD, error))
            return false;
        for (uint32_t seat = 0; seat < source->frontend->options.seats; ++seat)
            if (!qa_audio_engine_stop_owner(source->frontend->audio, source->identity, seat, error))
                return false;
    }
    while (source->pictures) { native_q2_picture *picture = source->pictures; source->pictures = picture->next; qa_scene_image_release(picture->image); free(picture->name); free(picture); }
    qa_font_world_store_destroy(source->world_text); qa_localization_pool_destroy(source->catalogs);
    qa_audio_bank_destroy(source->sounds); qa_font_library_destroy(source->fonts);
    qa_scene_resources_destroy(source->images); qa_vfs_destroy(source->mounts);
    free(source);
    return true;
}
static void release_source(void *context)
{
    frontend_native_q2 *source = context;
    /* The application has consumed its real lease. Retain only the frontend's
     * owned row when a parent or independent child capture still holds it. */
    source->owner_context = NULL; source->owner_idle = NULL;
    source->application = NULL; source->provider_files = NULL; source->cvars = NULL;
    source->prepared = true; source->seat_bound = false;
    frontend_native_q2 **item = &source->frontend->native_q2;
    while (*item && *item != source) item = &(*item)->next;
    frontend_native_q2 *next = source->next;
    if (source_free(source, NULL) && *item) *item = next;
}
bool frontend_native_q2_services(void *context, qa_application *application, qa_actor_owner owner,
    qa_native_profile profile, qa_native_host_engine_services *engine,
    qa_native_host_q2_application_fn *out, void **application_context, qa_error *error)
{
    qa_frontend *frontend = context;
    if (!frontend || !application || !engine || !engine->content_files || !engine->cvars || !out || !application_context || !owner ||
        (profile != QA_NATIVE_Q2_GAME_API3 && profile != QA_NATIVE_Q2_GAME_API2023 && profile != QA_NATIVE_Q2_CGAME_API2023))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "invalid native Q2 platform service binding");
    if (!frontend_owners_idle(frontend))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Native Q2 source construction requires idle frontend parent and child owners");
    if (profile == QA_NATIVE_Q2_CGAME_API2023 && (frontend->options.dedicated || !frontend->options.seats))
        return frontend_fail(error, QA_ERROR_UNSUPPORTED, "native Q2 cgame requires a local presentation seat");
    frontend_native_q2 *source = NULL;
    if (frontend->source_restoring) {
        if (frontend->application != application || !engine->owner_idle || !engine->owner_context)
            return frontend_fail(error, QA_ERROR_ARGUMENT, "restored native Q2 factory lacks its actual application owner");
        for (frontend_native_q2 *item = frontend->native_q2; item; item = item->next)
            if (item->owner == owner && item->profile == profile) {
                if (source) return frontend_fail(error, QA_ERROR_FORMAT, "restored native Q2 factory has duplicate saved rows");
                source = item;
            }
        if (!source || !source->prepared || source->frontend != frontend || source->application != application ||
            source->provider_files != engine->content_files || !source->mounts || source->active_imports)
            return frontend_fail(error, QA_ERROR_FORMAT, "native Q2 factory differs from its saved constructor topology");
    } else {
        if (frontend->next_source_id >= UINT64_MAX - QA_FRONTEND_COMMAND_OWNER - 1)
            return frontend_fail(error, QA_ERROR_MEMORY, "native Q2 frontend identity exhausted");
        source = calloc(1, sizeof(*source));
        if (!source) return frontend_fail(error, QA_ERROR_MEMORY, "allocating native Q2 frontend lease");
        source->frontend = frontend; source->application = application; source->owner = owner; source->profile = profile;
        source->identity = QA_FRONTEND_COMMAND_OWNER + ++frontend->next_source_id;
        source->frame_time_ns = frontend->time_ns;
        source->provider_files = engine->content_files;
        source->next = frontend->native_q2; frontend->native_q2 = source;
        source->mounts = qa_vfs_clone(engine->content_files, error);
        source->catalogs = qa_localization_pool_create(error); source->world_text = qa_font_world_store_create(error);
        if (!source->mounts || !source->catalogs || !source->world_text) { release_source(source); return false; }
    }
    source->owner_context = engine->owner_context; source->owner_idle = engine->owner_idle;
    source->cvars = engine->cvars; source->prepared = false;
    engine->context = source; engine->print = platform_print; engine->sound = platform_sound;
    engine->hud_view = platform_hud_view;
    engine->resource_precache = platform_resource_precache;
    engine->frontend_lifetime = source; engine->release_frontend = release_source;
    *out = application_import; *application_context = source; return true;
}
bool frontend_native_q2_frame(qa_frontend *frontend, uint32_t seat, qa_scene_rect rect, qa_error *error)
{
    if (seat >= frontend->options.seats) return frontend_fail(error, QA_ERROR_ARGUMENT, "native Q2 frame seat is invalid");
    for (frontend_native_q2 *source = frontend->native_q2; source; source = source->next) {
        if (source->profile == QA_NATIVE_Q2_CGAME_API2023 && source->seat_bound && source->seat == seat)
            source->viewport = rect;
        if (source->profile == QA_NATIVE_Q2_CGAME_API2023 && source->frame_time_ns != frontend->time_ns) {
            source->previous_frame_time_ns = source->frame_time_ns;
            source->frame_time_ns = frontend->time_ns;
            source->frame_seconds = (float)((double)(source->frame_time_ns - source->previous_frame_time_ns) / 1000000000.0);
        }
    }
    return true;
}
bool frontend_native_q2_world_text(qa_frontend *frontend, uint32_t seat, const qa_scene_view *view, qa_error *error)
{
    if (seat >= frontend->options.seats) return frontend_fail(error, QA_ERROR_ARGUMENT, "native Q2 world text seat is invalid");
    qa_arena scratch; qa_arena_init(&scratch, 16384); bool ok = true;
    for (frontend_native_q2 *source = frontend->native_q2; source && ok; source = source->next) {
        qa_font_world_snapshot snapshot;
        ok = qa_font_world_store_snapshot(source->world_text, (double)frontend->time_ns / 1000000000,
            frontend->frame_number, &scratch, &snapshot, error);
        if (ok && snapshot.count) {
            ok = source_font(source, error);
            qa_font_selection fonts = frontend->seats[seat].fonts; fonts.classic = source->classic;
            if (ok) ok = qa_font_world_draw(&frontend->frame, view, &snapshot, &fonts, 0, false, error);
        }
    }
    qa_arena_destroy(&scratch); return ok;
}
void frontend_native_q2_retire_world(qa_frontend *frontend)
{
    for (frontend_native_q2 *source = frontend->native_q2; source; source = source->next) {
        qa_font_world_store_clear(source->world_text); source->seat_bound = false;
    }
}
const qa_scene_resources *frontend_native_q2_images_at(qa_frontend *frontend, size_t index)
{
    for (frontend_native_q2 *source = frontend->native_q2; source; source = source->next)
        if (source->images && index-- == 0) return source->images;
    return NULL;
}
bool frontend_native_q2_callbacks_idle(const qa_frontend *frontend)
{
    if (!frontend || frontend->stepping) return false;
    for (const frontend_native_q2 *source=frontend->native_q2;source;source=source->next)
        if (source->frontend!=frontend || source->application!=frontend->application || source->active_imports) return false;
    return true;
}

bool frontend_native_q2_children_idle(const qa_frontend *frontend)
{
    if (!frontend) return false;
    for (const frontend_native_q2 *source = frontend->native_q2; source; source = source->next)
        if (source->frontend != frontend || source->active_imports || source->image_policy ||
            (source->images && !qa_scene_resources_idle(source->images)) ||
            (source->fonts && !qa_font_library_idle(source->fonts))) return false;
    return true;
}

bool frontend_native_q2_q3_round_ready(const qa_frontend *frontend, qa_error *error)
{
    if (!frontend || !frontend->application || frontend->stepping)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "native Q2 round admission requires an idle frontend owner");
    for (const frontend_native_q2 *source = frontend->native_q2; source; source = source->next) {
        if (source->frontend != frontend || source->application != frontend->application ||
            source->prepared || source->restored || source->active_imports ||
            !source->owner_idle || !source->owner_idle(source->owner_context))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "native Q2 round admission has an unfinished source lease");
        if (source->profile == QA_NATIVE_Q2_CGAME_API2023)
            return frontend_fail(error, QA_ERROR_UNSUPPORTED,
                "native Q2 cgame requires a native KEX character source unavailable in a Q3 round");
    }
    return true;
}

bool frontend_native_q2_rebind_ready(const qa_frontend *candidate, const qa_frontend *published,
    qa_error *error)
{
    if (!candidate || !published || candidate == published || candidate->stepping || published->stepping ||
        !candidate->application || candidate->options.seats != published->options.seats ||
        candidate->options.dedicated != published->options.dedicated)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "native Q2 frontend adoption requires idle matching frontends");
    for (frontend_native_q2 *source = candidate->native_q2; source; source = source->next) {
        if (source->prepared || source->restored || (source->frontend != candidate && source->frontend != published) ||
            source->application != candidate->application || source->active_imports ||
            !source->owner_idle || !source->owner_idle(source->owner_context) ||
            (source->seat_bound && source->seat >= candidate->options.seats))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "native Q2 frontend lease has active or mismatched source references");
    }
    for (frontend_native_q2 *source = published->native_q2; source; source = source->next)
        if (source->frontend != published || source->application != published->application ||
            source->active_imports || !source->owner_idle || !source->owner_idle(source->owner_context))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "published native Q2 frontend owner is active");
    return true;
}

void frontend_native_q2_rebind(qa_frontend *owned, qa_frontend *destination)
{
    for (frontend_native_q2 *source = owned->native_q2; source; source = source->next)
        source->frontend = destination;
}

typedef struct native_q2_plan {
    qa_actor_owner owner;
    uint32_t profile;
    uint64_t identity, mounts_view, provider_view;
    bool images, sounds, fonts;
} native_q2_plan;
static bool native_q2_plan_fields(qa_source_save_io *io, qa_strings *strings, native_q2_plan *plan)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    char *owner = !reading ? (char *)qa_strings_cstr(strings, plan->owner) : NULL;
    if ((!reading && (!plan->owner || !owner || !*owner)) || !qa_source_save_owned_text(io, &owner)) return false;
    if (reading) {
        plan->owner = owner ? qa_strings_find(strings, (qa_bytes){(const uint8_t *)owner, strlen(owner)}) : 0;
        free(owner);
    }
    return plan->owner && qa_source_save_u32(io, &plan->profile) &&
        (plan->profile == QA_NATIVE_Q2_GAME_API3 || plan->profile == QA_NATIVE_Q2_GAME_API2023 ||
         plan->profile == QA_NATIVE_Q2_CGAME_API2023) &&
        qa_source_save_u64(io, &plan->identity) && plan->identity > QA_FRONTEND_COMMAND_OWNER &&
        qa_source_save_u64(io, &plan->mounts_view) && qa_source_save_u64(io, &plan->provider_view) &&
        plan->mounts_view && plan->provider_view && plan->mounts_view != plan->provider_view &&
        qa_source_save_bool(io, &plan->images) && qa_source_save_bool(io, &plan->sounds) &&
        qa_source_save_bool(io, &plan->fonts) && (!plan->fonts || plan->images);
}
static bool native_q2_plans(qa_source_save_io *io, qa_frontend *frontend,
    native_q2_plan **plans, size_t *count)
{
    uint8_t magic[4] = {'Q','N','F','T'}; bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t maximum = SIZE_MAX / sizeof(**plans);
    if (reading && io->input.size / 37 < maximum) maximum = io->input.size / 37;
    if (!qa_source_save_bytes(io, magic, 4) || memcmp(magic, "QNFT", 4) ||
        !qa_source_save_count(io, count, maximum)) return false;
    if (reading) {
        *plans = calloc(*count ? *count : 1, sizeof(**plans));
        if (!*plans) return frontend_fail(io->error, QA_ERROR_MEMORY, "retaining native Q2 constructor topology");
    }
    qa_strings *strings = qa_session_strings(qa_application_session(frontend->application));
    for (size_t i = 0; i < *count; ++i) {
        native_q2_plan *plan = *plans + i;
        if (!native_q2_plan_fields(io, strings, plan) ||
            plan->identity - QA_FRONTEND_COMMAND_OWNER > frontend->next_source_id ||
            frontend_source_identity_used(frontend, plan->identity) ||
            (plan->profile == QA_NATIVE_Q2_CGAME_API2023 && (frontend->options.dedicated || !frontend->options.seats))) return false;
        for (size_t j = 0; j < i; ++j)
            if (plan->identity == (*plans)[j].identity || plan->mounts_view == (*plans)[j].mounts_view ||
                (plan->owner == (*plans)[j].owner && plan->profile == (*plans)[j].profile)) return false;
    }
    return true;
}
size_t frontend_native_q2_owner_count(const qa_frontend *frontend)
{
    size_t count = 0;
    if (frontend) for (const frontend_native_q2 *source = frontend->native_q2; source; source = source->next) ++count;
    return count;
}
bool frontend_native_q2_owner_read(const qa_frontend *frontend, size_t index,
    frontend_native_q2_owner_view *out)
{
    if (!frontend || !out || frontend->stepping || !frontend->application) return false;
    const frontend_native_q2 *source = frontend->native_q2;
    while (source && index--) source = source->next;
    if (!source || source->frontend != frontend || source->application != frontend->application || source->active_imports) return false;
    *out = (frontend_native_q2_owner_view){.owner = source->owner, .profile = source->profile,
        .identity = source->identity, .mounts = source->mounts, .provider_files = source->provider_files,
        .images = source->images, .sounds = source->sounds, .fonts = source->fonts, .prepared = source->prepared};
    return true;
}
bool frontend_native_q2_audio_view(const qa_frontend *frontend,const qa_audio_asset *asset,qa_vfs **out)
{
    if (!frontend || !asset || !out) return false;
    qa_resource *resource=qa_audio_asset_resource(asset);
    if (!resource) return false;
    for (const frontend_native_q2 *source=frontend->native_q2;source;source=source->next)
        if (source->sounds && qa_audio_bank_get(source->sounds,qa_resource_id(resource),qa_audio_asset_family(asset))==asset) {
            *out=source->mounts; return *out!=NULL;
        }
    return false;
}
bool frontend_native_q2_topology_checkpoint(const qa_frontend *frontend, qa_buffer *out, qa_error *error)
{
    if (!frontend || !frontend->application || frontend->stepping || frontend->source_restoring || frontend->resource_inventory ||
        !out || out->data || out->size)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "native Q2 topology requires idle owners and empty output");
    qa_application_content_graph *graph = qa_application_content_graph_read(frontend->application);
    if (!graph) return frontend_fail(error, QA_ERROR_ARGUMENT, "native Q2 topology requires its captured content graph");
    size_t count = frontend_native_q2_owner_count(frontend);
    native_q2_plan *plans = calloc(count ? count : 1, sizeof(*plans));
    if (!plans) return frontend_fail(error, QA_ERROR_MEMORY, "retaining native Q2 topology capture");
    bool ok = true; size_t index = 0;
    for (const frontend_native_q2 *source = frontend->native_q2; source && ok; source = source->next, ++index) {
        ok = !source->prepared && !source->restored && source->frontend == frontend &&
            source->application == frontend->application && !source->active_imports &&
            source->owner_idle && source->owner_idle(source->owner_context);
        plans[index] = (native_q2_plan){.owner = source->owner, .profile = source->profile,
            .identity = source->identity, .mounts_view = qa_application_content_view_id(graph, source->mounts),
            .provider_view = qa_application_content_view_id(graph, source->provider_files),
            .images = source->images != NULL, .sounds = source->sounds != NULL, .fonts = source->fonts != NULL};
    }
    qa_source_save_io io = {0};
    ok = ok && qa_source_save_writer(&io, qa_application_session(frontend->application), error) &&
        native_q2_plans(&io, (qa_frontend *)frontend, &plans, &count) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); free(plans);
    if (!ok && (!error || error->code == QA_OK)) frontend_fail(error, QA_ERROR_FORMAT, "native Q2 topology is not completely qualified");
    return ok;
}
bool frontend_native_q2_prepare_restored(qa_frontend *frontend, qa_bytes bytes, qa_error *error)
{
    if (!frontend || !frontend->application || frontend->stepping || !frontend->source_restoring || frontend->native_q2)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "native Q2 topology admission requires empty isolated native owners");
    qa_application_content_graph *graph = qa_application_content_graph_read(frontend->application);
    if (!graph) return frontend_fail(error, QA_ERROR_ARGUMENT, "native Q2 topology lacks its restored content graph");
    native_q2_plan *plans = NULL; size_t count = 0; qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, qa_application_session(frontend->application), bytes, error) &&
        native_q2_plans(&io, frontend, &plans, &count) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    for (size_t i = 0; ok && i < count; ++i) {
        const qa_vfs *mounts = qa_application_content_view(graph, plans[i].mounts_view);
        const qa_vfs *provider = qa_application_content_view(graph, plans[i].provider_view);
        ok = mounts && provider && qa_vfs_resources(mounts) == qa_vfs_resources(provider);
    }
    if (!ok) {
        free(plans);
        if (!error || error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "native Q2 saved constructor topology is invalid");
        return false;
    }
    frontend_native_q2 **tail = &frontend->native_q2;
    for (size_t i = 0; ok && i < count; ++i) {
        frontend_native_q2 *source = calloc(1, sizeof(*source));
        if (!source) { ok = frontend_fail(error, QA_ERROR_MEMORY, "allocating prepared native Q2 lease"); break; }
        *tail = source; tail = &source->next;
        source->frontend = frontend; source->application = frontend->application;
        source->owner = plans[i].owner; source->profile = (qa_native_profile)plans[i].profile;
        source->identity = plans[i].identity; source->prepared = source->restored = true;
        source->frame_time_ns = frontend->time_ns;
        source->provider_files = qa_application_content_view(graph, plans[i].provider_view);
        ok = qa_application_content_claim_view(graph, plans[i].mounts_view, &source->mounts, error);
        if (ok) source->catalogs = qa_localization_pool_create(error);
        if (ok) source->world_text = qa_font_world_store_create(error);
        ok = ok && source->catalogs && source->world_text;
        if (ok && plans[i].images) ok = (source->images = qa_scene_resources_create_detached(source->mounts, error)) != NULL;
        if (ok && plans[i].sounds) ok = qa_audio_bank_create(source->mounts, &source->sounds, error);
        if (ok && plans[i].fonts) ok = (source->fonts = qa_font_library_create(source->mounts, source->images, error)) != NULL;
    }
    free(plans); return ok;
}
bool frontend_native_q2_topology_ready(const qa_frontend *frontend, qa_error *error)
{
    if (!frontend || !frontend->application || frontend->stepping || !frontend->source_restoring)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "native Q2 restored topology requires idle constructor policy");
    for (const frontend_native_q2 *source = frontend->native_q2; source; source = source->next)
        if (source->prepared || !source->restored || source->frontend != frontend ||
            source->application != frontend->application || source->active_imports || !source->owner_idle ||
            !source->owner_idle(source->owner_context) || !source->mounts || !source->provider_files || !source->cvars)
            return frontend_fail(error, QA_ERROR_FORMAT, "native Q2 saved lease was not bound to its actual provider");
    return true;
}
void frontend_native_q2_topology_finish(qa_frontend *frontend)
{
    for (frontend_native_q2 *source = frontend->native_q2; source; source = source->next) source->restored = false;
}
bool frontend_native_q2_discard_unbound(qa_frontend *frontend, qa_error *error)
{
    if (!frontend || frontend->stepping || !frontend_owners_idle(frontend))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "native Q2 pending lease cleanup requires idle frontend");
    for (const frontend_native_q2 *source = frontend->native_q2; source; source = source->next)
        if (!source->prepared || source->active_imports || source->owner_context || source->owner_idle)
            return frontend_fail(error, QA_ERROR_ARGUMENT, "actual native Q2 application leases must retire before pending cleanup");
    while (frontend->native_q2) {
        frontend_native_q2 *source = frontend->native_q2, *next = source->next;
        if (!source_free(source, error)) return false;
        frontend->native_q2 = next;
    }
    return true;
}

typedef struct native_q2_private_record {
    native_q2_plan plan;
    uint64_t classic, frame_time_ns, previous_frame_time_ns;
    float frame_seconds;
    uint32_t seat;
    qa_scene_rect viewport;
    bool seat_bound, alternate;
    qa_buffer world_text;
} native_q2_private_record;
static bool native_q2_game_private(const frontend_native_q2 *source, qa_error *error)
{
    if (source->profile != QA_NATIVE_Q2_GAME_API3 && source->profile != QA_NATIVE_Q2_GAME_API2023)
        return frontend_fail(error, QA_ERROR_UNSUPPORTED, "native Q2 cgame frontend continuation requires qualified original guest allocations and catalogs");
    if (!source->catalogs || qa_localization_pool_count(source->catalogs) || source->pictures || source->next_string)
        return frontend_fail(error, QA_ERROR_FORMAT, "native Q2 GAME lease contains state outside its actual import profile");
    for (size_t i = 0; i < 8; ++i)
        if (source->strings[i].instance || source->strings[i].address || source->strings[i].capacity)
            return frontend_fail(error, QA_ERROR_UNSUPPORTED, "native Q2 GAME lease has an unqualified guest return allocation");
    return true;
}
static bool native_q2_world_encode(void *context, uint64_t content, uint64_t *out, qa_error *error)
{
    const frontend_native_q2 *source = context;
    if (content != source->owner)
        return frontend_fail(error, QA_ERROR_FORMAT, "native Q2 retained world text has a different source owner");
    *out = 1; return true;
}
static bool native_q2_world_decode(void *context, uint64_t saved, uint64_t *out, qa_error *error)
{
    const frontend_native_q2 *source = context;
    if (saved != 1 || !source->owner)
        return frontend_fail(error, QA_ERROR_FORMAT, "native Q2 saved world text lacks its exact source owner");
    *out = source->owner; return true;
}
static bool native_q2_private_fields(qa_source_save_io *io, qa_frontend *frontend,
    native_q2_private_record *record)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    qa_strings *strings = qa_session_strings(qa_application_session(frontend->application));
    if (!native_q2_plan_fields(io, strings, &record->plan) ||
        record->plan.profile == QA_NATIVE_Q2_CGAME_API2023 ||
        !qa_source_save_u64(io, &record->classic) || !qa_source_save_u64(io, &record->frame_time_ns) ||
        !qa_source_save_u64(io, &record->previous_frame_time_ns) || !qa_source_save_f32(io, &record->frame_seconds) ||
        !isfinite(record->frame_seconds) || record->frame_seconds < 0 ||
        !qa_source_save_u32(io, &record->seat) || !qa_source_save_i32(io, &record->viewport.x) ||
        !qa_source_save_i32(io, &record->viewport.y) || !qa_source_save_u32(io, &record->viewport.width) ||
        !qa_source_save_u32(io, &record->viewport.height) || !qa_source_save_bool(io, &record->seat_bound) ||
        !qa_source_save_bool(io, &record->alternate) ||
        (record->seat_bound && (frontend->options.dedicated || record->seat >= frontend->options.seats)) ||
        (record->classic && !record->plan.fonts)) return false;
    size_t length = reading ? 0 : record->world_text.size;
    if (!qa_source_save_count(io, &length, reading ? io->input.size - io->offset : SIZE_MAX) || !length) return false;
    if (reading) {
        record->world_text.data = malloc(length);
        if (!record->world_text.data) return frontend_fail(io->error, QA_ERROR_MEMORY, "retaining native Q2 world text continuation");
        record->world_text.size = length;
    }
    return qa_source_save_bytes(io, record->world_text.data, length);
}
static bool native_q2_private_header(qa_source_save_io *io, size_t *count)
{
    uint8_t magic[4] = {'Q','N','F','P'}; size_t maximum = SIZE_MAX / sizeof(native_q2_private_record);
    if (io->direction == QA_SOURCE_SAVE_READ && io->input.size / 100 < maximum) maximum = io->input.size / 100;
    return qa_source_save_bytes(io, magic, 4) && !memcmp(magic, "QNFP", 4) &&
        qa_source_save_count(io, count, maximum);
}
bool frontend_native_q2_private_checkpoint(const qa_frontend *frontend, qa_buffer *out, qa_error *error)
{
    if (!frontend || !frontend->application || frontend->stepping || frontend->source_restoring || frontend->resource_inventory ||
        !out || out->data || out->size)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "native Q2 private capture requires idle real owners and empty output");
    qa_application_content_graph *graph = qa_application_content_graph_read(frontend->application);
    if (!graph) return frontend_fail(error, QA_ERROR_ARGUMENT, "native Q2 private capture requires the actual content graph");
    qa_source_save_io io = {0}; size_t count = frontend_native_q2_owner_count(frontend);
    bool ok = qa_source_save_writer(&io, qa_application_session(frontend->application), error) && native_q2_private_header(&io, &count);
    for (const frontend_native_q2 *source = frontend->native_q2; source && ok; source = source->next) {
        ok = !source->prepared && !source->restored && source->frontend == frontend &&
            source->application == frontend->application && !source->active_imports &&
            source->owner_idle && source->owner_idle(source->owner_context) && native_q2_game_private(source, error);
        native_q2_private_record record = {.plan = {.owner = source->owner, .profile = source->profile,
            .identity = source->identity, .mounts_view = qa_application_content_view_id(graph, source->mounts),
            .provider_view = qa_application_content_view_id(graph, source->provider_files),
            .images = source->images != NULL, .sounds = source->sounds != NULL, .fonts = source->fonts != NULL},
            .frame_time_ns = source->frame_time_ns, .previous_frame_time_ns = source->previous_frame_time_ns,
            .frame_seconds = source->frame_seconds, .seat = source->seat, .viewport = source->viewport,
            .seat_bound = source->seat_bound, .alternate = source->alternate};
        if (source->classic) {
            for (size_t i = 0; i < qa_font_library_record_count(source->fonts); ++i)
                if (qa_font_library_record_at(source->fonts, i) == source->classic) record.classic = i + 1;
            if (!record.classic) ok = frontend_fail(error, QA_ERROR_FORMAT, "native Q2 classic font is outside its actual library");
        }
        qa_font_world_checkpoint_refs refs = {.context = (void *)source, .content_encode = native_q2_world_encode};
        ok = ok && qa_font_world_store_checkpoint(source->world_text, &refs, &record.world_text, error) &&
            native_q2_private_fields(&io, (qa_frontend *)frontend, &record);
        qa_buffer_free(&record.world_text);
    }
    ok = ok && qa_source_save_finish(&io, out); qa_source_save_dispose(&io);
    if (!ok && (!error || error->code == QA_OK)) frontend_fail(error, QA_ERROR_FORMAT, "native Q2 private frontend state is invalid");
    return ok;
}
bool frontend_native_q2_private_restore(qa_frontend *frontend, qa_bytes bytes, qa_error *error)
{
    if (!frontend_native_q2_topology_ready(frontend, error)) return false;
    qa_application_content_graph *graph = qa_application_content_graph_read(frontend->application);
    if (!graph) return frontend_fail(error, QA_ERROR_ARGUMENT, "native Q2 private import requires its actual restored content graph");
    size_t count = 0; native_q2_private_record *records = NULL; qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, qa_application_session(frontend->application), bytes, error) &&
        native_q2_private_header(&io, &count) && count == frontend_native_q2_owner_count(frontend);
    if (ok && count) {
        records = calloc(count, sizeof(*records));
        if (!records) ok = frontend_fail(error, QA_ERROR_MEMORY, "preparing native Q2 private lease rows");
    }
    for (size_t i = 0; ok && i < count; ++i) ok = native_q2_private_fields(&io, frontend, records + i);
    ok = ok && qa_source_save_finish(&io, NULL); qa_source_save_dispose(&io);
    const frontend_native_q2 *actual = frontend->native_q2;
    for (size_t i = 0; ok && i < count; ++i, actual = actual->next) {
        const native_q2_private_record *record = records + i; const native_q2_plan *plan = &record->plan;
        ok = native_q2_game_private(actual, error) && plan->owner == actual->owner && plan->profile == (uint32_t)actual->profile &&
            plan->identity == actual->identity && plan->mounts_view == qa_application_content_view_id(graph, actual->mounts) &&
            plan->provider_view == qa_application_content_view_id(graph, actual->provider_files) &&
            plan->images == (actual->images != NULL) && plan->sounds == (actual->sounds != NULL) &&
            plan->fonts == (actual->fonts != NULL);
        if (ok && record->classic) {
            const qa_font *font = record->classic - 1 <= SIZE_MAX ?
                qa_font_library_record_at(actual->fonts, (size_t)(record->classic - 1)) : NULL;
            qa_font_info info;
            ok = font && qa_font_describe(font, &info) && info.kind == QA_FONT_CLASSIC && info.name &&
                !strcmp(info.name, "native-q2:conchars");
        }
    }
    frontend_native_q2 *source = frontend->native_q2;
    for (size_t i = 0; ok && i < count; ++i, source = source->next) {
        const native_q2_private_record *record = records + i;
        qa_font_world_checkpoint_refs refs = {.context = source, .content_decode = native_q2_world_decode};
        ok = qa_font_world_store_restore(source->world_text, (qa_bytes){record->world_text.data, record->world_text.size}, &refs, error);
        if (!ok) break;
        source->classic = record->classic ? qa_font_library_record_at(source->fonts, (size_t)(record->classic - 1)) : NULL;
        source->frame_time_ns = record->frame_time_ns; source->previous_frame_time_ns = record->previous_frame_time_ns;
        source->frame_seconds = record->frame_seconds; source->seat = record->seat; source->viewport = record->viewport;
        source->seat_bound = record->seat_bound; source->alternate = record->alternate;
    }
    if (records) for (size_t i = 0; i < count; ++i) qa_buffer_free(&records[i].world_text);
    free(records);
    if (!ok && (!error || error->code == QA_OK)) frontend_fail(error, QA_ERROR_FORMAT, "saved native Q2 private frontend state differs from actual owners");
    return ok;
}

typedef struct native_q2_policy_source {
    frontend_native_q2 *source;
    native_q2_picture *pictures;
    qa_scene_resources *images;
    void *owner_context;
    bool (*owner_idle)(void *);
    qa_scene_resource_policy *bank;
} native_q2_policy_source;
typedef struct native_q2_policy_picture {
    native_q2_picture *picture;
    const qa_scene_image *original, *prepared;
} native_q2_policy_picture;
struct frontend_native_q2_image_policy {
    qa_frontend *frontend;
    qa_application *application;
    frontend_native_q2 *head;
    native_q2_policy_source *sources;
    native_q2_policy_picture *pictures;
    size_t source_count, picture_count;
    bool sealed, published;
};
static bool q2_policy_current(const frontend_native_q2_image_policy *ticket)
{
    if (!ticket || ticket->frontend->application != ticket->application ||
        ticket->frontend->native_q2 != ticket->head || ticket->frontend->stepping) return false;
    size_t row = 0, picture = 0;
    for (frontend_native_q2 *source = ticket->head; source; source = source->next, ++row) {
        if (row == ticket->source_count) return false;
        const native_q2_policy_source *saved = ticket->sources + row;
        if (source != saved->source || source->frontend != ticket->frontend ||
            source->application != ticket->application || source->active_imports ||
            source->image_policy != ticket || source->images != saved->images ||
            source->pictures != saved->pictures || source->owner_context != saved->owner_context ||
            source->owner_idle != saved->owner_idle || source->prepared || source->restored) return false;
        for (native_q2_picture *item = source->pictures; item; item = item->next, ++picture)
            if (picture == ticket->picture_count || ticket->pictures[picture].picture != item ||
                item->image != (ticket->published ? ticket->pictures[picture].prepared :
                    ticket->pictures[picture].original)) return false;
    }
    return row == ticket->source_count && picture == ticket->picture_count;
}
static void q2_policy_dispose(frontend_native_q2_image_policy *ticket)
{
    for (size_t i = 0; i < ticket->picture_count; ++i)
        qa_scene_image_release(ticket->published ? ticket->pictures[i].original : ticket->pictures[i].prepared);
    for (size_t i = 0; i < ticket->source_count; ++i)
        if (ticket->sources[i].source->image_policy == ticket) ticket->sources[i].source->image_policy = NULL;
    free(ticket->pictures); free(ticket->sources); free(ticket);
}
bool frontend_native_q2_image_policy_prepare(qa_frontend *f, qa_scene_resource_policy *const *banks,
    size_t count, frontend_native_q2_image_policy **out, qa_error *error)
{
    if (!f || !f->application || !f->resource_inventory || !out || *out || (count && !banks) ||
        !frontend_native_q2_callbacks_idle(f))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 image bindings require their actual retained frontend roster");
    frontend_native_q2_image_policy *ticket = calloc(1, sizeof(*ticket));
    if (!ticket) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining Q2 image bindings");
    ticket->frontend = f; ticket->application = f->application; ticket->head = f->native_q2;
    for (frontend_native_q2 *source = ticket->head; source; source = source->next) {
        if (source->prepared || source->restored || source->image_policy || !source->owner_idle ||
            !source->owner_idle(source->owner_context) || ticket->source_count == SIZE_MAX / sizeof(*ticket->sources)) goto invalid;
        ++ticket->source_count;
        for (native_q2_picture *picture = source->pictures; picture; picture = picture->next) {
            if (ticket->picture_count == SIZE_MAX / sizeof(*ticket->pictures)) goto invalid;
            ++ticket->picture_count;
        }
    }
    ticket->sources = ticket->source_count ? calloc(ticket->source_count, sizeof(*ticket->sources)) : NULL;
    ticket->pictures = ticket->picture_count ? calloc(ticket->picture_count, sizeof(*ticket->pictures)) : NULL;
    if ((ticket->source_count && !ticket->sources) || (ticket->picture_count && !ticket->pictures)) {
        free(ticket->pictures); free(ticket->sources); free(ticket);
        return frontend_fail(error, QA_ERROR_MEMORY, "Retaining actual Q2 picture binding rows");
    }
    size_t row = 0, picture = 0;
    for (frontend_native_q2 *source = ticket->head; source; source = source->next, ++row) {
        native_q2_policy_source *saved = ticket->sources + row;
        *saved = (native_q2_policy_source){source, source->pictures, source->images,
            source->owner_context, source->owner_idle, NULL};
        for (size_t i = 0; i < count; ++i)
            if (qa_scene_resource_policy_source(banks[i]) == source->images) saved->bank = banks[i];
        if (source->images && !saved->bank) goto failed;
        source->image_policy = ticket;
        for (native_q2_picture *item = source->pictures; item; item = item->next, ++picture) {
            native_q2_policy_picture *saved_picture = ticket->pictures + picture;
            *saved_picture = (native_q2_policy_picture){.picture = item, .original = item->image};
            qa_scene_image *image = NULL;
            if (!saved->bank || !qa_scene_resource_policy_image(saved->bank, item->image, &image, error)) goto failed;
            saved_picture->prepared = image;
        }
    }
    if (!q2_policy_current(ticket)) goto failed;
    *out = ticket; return true;
invalid:
    free(ticket->pictures); free(ticket->sources); free(ticket);
    return frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 image preparation lacks a returned real owner or addressable roster");
failed:
    /* Unvisited rows have no owner hold or image reference. */
    for (size_t i = 0; i < ticket->picture_count; ++i) qa_scene_image_release(ticket->pictures[i].prepared);
    for (size_t i = 0; i < ticket->source_count; ++i)
        if (ticket->sources[i].source && ticket->sources[i].source->image_policy == ticket)
            ticket->sources[i].source->image_policy = NULL;
    free(ticket->pictures); free(ticket->sources); free(ticket);
    if (error && error->code == QA_OK) frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 image bindings lost their actual prepared bank");
    return false;
}
bool frontend_native_q2_image_policy_ready(frontend_native_q2_image_policy *ticket, qa_error *error)
{
    if (!q2_policy_current(ticket) || ticket->published)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 image bindings lost their actual source owners");
    ticket->sealed = true; return true;
}
bool frontend_native_q2_image_policy_ready_is(const frontend_native_q2_image_policy *ticket)
{
    if (!q2_policy_current(ticket) || !ticket->sealed || ticket->published) return false;
    for (size_t i = 0; i < ticket->source_count; ++i)
        if (ticket->sources[i].bank && !qa_scene_resource_policy_ready_is(ticket->sources[i].bank)) return false;
    return true;
}
void frontend_native_q2_image_policy_publish(frontend_native_q2_image_policy *ticket)
{
    if (!q2_policy_current(ticket) || !ticket->sealed || ticket->published) return;
    for (size_t i = 0; i < ticket->picture_count; ++i)
        ticket->pictures[i].picture->image = ticket->pictures[i].prepared;
    ticket->published = true;
}
static bool q2_policy_end(frontend_native_q2_image_policy **owner, bool published, qa_error *error)
{
    if (!owner || !*owner) return true;
    if (!q2_policy_current(*owner) || (*owner)->published != published)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 image cleanup retains its nonterminal actual owner");
    q2_policy_dispose(*owner); *owner = NULL; return true;
}
bool frontend_native_q2_image_policy_finish(frontend_native_q2_image_policy **owner, qa_error *error)
{ return q2_policy_end(owner, true, error); }
bool frontend_native_q2_image_policy_abort(frontend_native_q2_image_policy **owner, qa_error *error)
{ return q2_policy_end(owner, false, error); }
