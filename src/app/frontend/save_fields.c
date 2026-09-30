#include "save_private.h"
bool frontend_save_provider(qa_source_save_io *io, qa_application *application, qa_actor_owner *owner)
{
    const char *instance = io->direction == QA_SOURCE_SAVE_WRITE && *owner ?
        qa_application_provider_instance(application, *owner) : NULL;
    if (io->direction == QA_SOURCE_SAVE_WRITE && *owner && !instance)
        return frontend_fail(io->error, QA_ERROR_NOT_FOUND, "presentation provider has no selected instance identity");
    if (!qa_source_save_text(io, &instance)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (!instance) *owner = 0;
        else if (!qa_application_provider_owner(application, instance, owner))
            return frontend_fail(io->error, QA_ERROR_FORMAT, "saved presentation provider is absent from candidate");
    }
    return true;
}
bool frontend_save_text(qa_source_save_io *io, char **owned)
{
    const char *text = io->direction == QA_SOURCE_SAVE_WRITE ? *owned : NULL;
    if (!qa_source_save_text(io, &text)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        char *copy = NULL;
        if (text) {
            size_t size = strlen(text) + 1; copy = malloc(size);
            if (!copy) return frontend_fail(io->error, QA_ERROR_MEMORY, "retaining saved presentation text");
            memcpy(copy, text, size);
        }
        free(*owned); *owned = copy;
    }
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
