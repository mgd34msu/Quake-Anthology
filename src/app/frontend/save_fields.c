#include "save_private.h"
#include "qa/application_equipment_content.h"
#include "qa/console_save.h"
bool frontend_save_command_context(qa_source_save_io *io, qa_command_context *context, char **script,
    uint64_t captured_registry)
{
    qa_command_context saved_context = *context;
    if (io->direction == QA_SOURCE_SAVE_WRITE) {
        context = &saved_context;
        context->registry = qa_console_save_context_registry(io->session, context->registry, captured_registry);
    }
    uint32_t dialect = context->dialect, origin = context->origin;
    bool ok = qa_source_save_u64(io, &context->session) && qa_source_save_u64(io, &context->owner)
        && qa_source_save_u64(io, &context->client) && qa_source_save_u32(io, &context->seat)
        && qa_source_save_u32(io, &dialect) && dialect <= QA_RULESET_Q3
        && qa_source_save_u32(io, &origin) && origin <= QA_COMMAND_REMOTE
        && qa_source_save_bool(io, &context->direct) && qa_source_save_bool(io, &context->console_text)
        && qa_source_save_u64(io, &context->registry) && qa_source_save_u64(io, &context->generation)
        && qa_source_save_actor(io, &context->actor) && qa_source_save_owned_text(io, script);
    if (ok && io->direction == QA_SOURCE_SAVE_READ) {
        context->dialect = (qa_ruleset_id)dialect; context->origin = (qa_command_origin)origin;
        context->script = *script;
        if (context->registry == captured_registry)
            context->registry = qa_actors_identity(qa_session_actors(io->session));
        else { context->registry = 0; if (!context->generation) context->generation = UINT64_MAX; }
    }
    return ok;
}
bool frontend_save_provider(qa_source_save_io *io, qa_application *application, qa_actor_owner *owner)
{
    const char *instance = io->direction == QA_SOURCE_SAVE_WRITE && *owner ?
        qa_application_provider_instance(application, *owner) : NULL;
    if (io->direction == QA_SOURCE_SAVE_WRITE && *owner && !instance)
        return frontend_fail(io->error, QA_ERROR_NOT_FOUND, "presentation provider has no selected instance identity");
    char *text = (char *)instance;
    if (!qa_source_save_owned_text(io, &text)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        bool resolved = !text || qa_application_provider_owner(application, text, owner);
        if (!text) *owner = 0;
        free(text);
        if (!resolved)
            return frontend_fail(io->error, QA_ERROR_FORMAT, "saved presentation provider is absent from candidate");
    }
    return true;
}
bool frontend_save_sound_owner(qa_source_save_io *io, qa_application *application,
    qa_actor_owner *owner, qa_game_family *family)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint32_t tag = !reading && *owner ? 1 : 0, kind = *family;
    qa_application_equipment_content gear;
    if (!reading && *owner && !qa_application_provider_instance(application, *owner)) {
        if (!qa_application_equipment_content_read(application, *owner, &gear, io->error)) return false;
        tag = 2;
    }
    if (!qa_source_save_u32(io, &tag) || tag > 2 || !qa_source_save_u32(io, &kind) ||
        kind > QA_GAME_Q3 || (tag == 2 && kind != QA_GAME_Q3)) return false;
    if (tag == 1) {
        if (!frontend_save_provider(io, application, owner) || !*owner) return false;
    } else if (tag == 2) {
        qa_strings *strings = qa_session_strings(qa_application_session(application));
        char *name = !reading ? (char *)qa_strings_cstr(strings, *owner) : NULL;
        if (!reading && !name) return false;
        bool ok = qa_source_save_owned_text(io, &name);
        if (reading) {
            *owner = name ? qa_strings_find(strings, (qa_bytes){(const uint8_t *)name, strlen(name)}) : 0;
            free(name);
        }
        if (!ok || !*owner || !qa_application_equipment_content_read(application, *owner, &gear, io->error))
            return false;
    } else *owner = 0;
    *family = (qa_game_family)kind;
    return true;
}
bool frontend_save_random(qa_source_save_io *io, qa_builtin_random *random)
{
    for (size_t i = 0; i < 31; ++i) if (!qa_source_save_u32(io, &random->words[i])) return false;
    return qa_source_save_u8(io, &random->front) && qa_source_save_u8(io, &random->rear) &&
        qa_source_save_u64(io, &random->draws) && random->front < 31 && random->rear < 31 &&
        (random->front + 31u - random->rear) % 31u == 3;
}
static bool fog_fields(qa_source_save_io *io, qa_q2_fog *fog)
{
    return qa_source_save_f32(io, &fog->density) && qa_source_save_f32(io, &fog->sky_factor) &&
        qa_source_save_vec3(io, &fog->color) && qa_source_save_vec3(io, &fog->start_color) &&
        qa_source_save_vec3(io, &fog->end_color) && qa_source_save_f32(io, &fog->start_distance) &&
        qa_source_save_f32(io, &fog->end_distance) && qa_source_save_f32(io, &fog->falloff) &&
        qa_source_save_f32(io, &fog->height_density) && isfinite(fog->density) && isfinite(fog->sky_factor) &&
        qa_vec_finite(fog->color) && qa_vec_finite(fog->start_color) && qa_vec_finite(fog->end_color) &&
        isfinite(fog->start_distance) && isfinite(fog->end_distance) && isfinite(fog->falloff) && isfinite(fog->height_density);
}
bool frontend_save_q2_event(qa_source_save_io *io, qa_q2_map_event *event)
{
    uint32_t kind = event->kind;
    bool ok = !event->argument_count && qa_source_save_u32(io, &kind) && kind <= QA_Q2_MAP_HELP_COMPUTER &&
        qa_source_save_actor(io, &event->actor) && qa_source_save_actor(io, &event->recipient) && qa_source_save_actor(io, &event->target) &&
        qa_source_save_string(io, &event->text) && qa_source_save_string(io, &event->resource) &&
        qa_source_save_vec3(io, &event->origin) && qa_source_save_vec3(io, &event->direction) && qa_source_save_vec3(io, &event->color) &&
        fog_fields(io, &event->fog) && qa_source_save_f32(io, &event->value) && qa_source_save_f32(io, &event->duration) &&
        qa_source_save_f32(io, &event->radius) && qa_source_save_f32(io, &event->alpha) && qa_source_save_f32(io, &event->intensity) &&
        qa_source_save_f32(io, &event->fade_start) && qa_source_save_f32(io, &event->fade_end) && qa_source_save_f32(io, &event->cone_cosine) &&
        qa_source_save_i32(io, &event->count) && qa_source_save_i32(io, &event->style) && qa_source_save_i32(io, &event->slot) &&
        qa_source_save_u32(io, &event->flags) && qa_source_save_u32(io, &event->resolution) && qa_source_save_bool(io, &event->visible) &&
        qa_vec_finite(event->origin) && qa_vec_finite(event->direction) && qa_vec_finite(event->color) &&
        isfinite(event->value) && isfinite(event->duration) && isfinite(event->radius) && isfinite(event->alpha) &&
        isfinite(event->intensity) && isfinite(event->fade_start) && isfinite(event->fade_end) && isfinite(event->cone_cosine);
    if (ok) event->kind = (qa_q2_map_event_kind)kind;
    return ok;
}
