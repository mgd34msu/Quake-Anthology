#include "selected_effects_private.h"
#include "../../presentation/q3_native/events_internal.h"
#include "../../presentation/q3/internal.h"
#include "qa/scene_marks.h"
#include "qa/scene_world_save.h"
#include "native_q3_client.h"
#include "capture.h"
#include "source_effects.h"
#include <math.h>

static bool primary_current(const qa_frontend *frontend, const frontend_effects_primary *primary)
{
    if (!frontend || !primary || (!!primary->native == !!primary->source)) return false;
    if (primary->source) {
        qa_q3_presentation *presentation; uint32_t seat; qa_ui_preferences preferences;
        return frontend_source_effects_primary_read(primary->source, frontend, &presentation, &seat, &preferences) &&
            presentation == primary->presentation && seat == primary->physical_seat;
    }
    const q3n_frame *frame = primary->native;
    return frame->application == frontend->application && !frame->effects_source &&
        frame->presentation == primary->presentation && frame->physical_presentation_seat == primary->physical_seat &&
        frame->physical_presentation_seat < frontend->options.seats &&
        qa_application_native_q3_presentation_current(frame->application, &frame->source);
}

static frontend_effects_primary native_primary(const q3n_frame *frame)
{ return (frontend_effects_primary){.native = frame, .presentation = frame ? frame->presentation : NULL,
    .physical_seat = frame ? frame->physical_presentation_seat : 0,
    .preferences = frame ? frame->preferences : (qa_ui_preferences){0}}; }

static bool source_primary(const qa_frontend *frontend, const frontend_source_effects *source,
    frontend_effects_primary *out)
{
    *out = (frontend_effects_primary){.source = source};
    return frontend_source_effects_primary_read(source, frontend, &out->presentation,
        &out->physical_seat, &out->preferences);
}

static bool primary_binding(const qa_frontend *frontend, const frontend_effects_primary *primary,
    qa_q3_presentation_binding *out, qa_error *error)
{
    return primary_current(frontend, primary) && (primary->source ?
        frontend_source_effects_binding_read(primary->source, frontend, out) :
        qa_q3_presentation_binding_read(primary->presentation, out, error));
}

static bool group_binding(const frontend_selected_effects_group *group,
    const qa_q3_presentation_binding *binding)
{
    return group && !group->restoring && binding && binding->world &&
        binding->world == group->world && binding->geometry == group->geometry &&
        binding->frame == &group->owner->frontend->frame;
}

static bool group_primary_current(const frontend_selected_effects_group *group,
    const frontend_effects_primary *primary, qa_error *error)
{
    qa_q3_presentation_binding actual, retained;
    return group && group->owner && primary_current(group->owner->frontend, primary) &&
        primary_binding(group->owner->frontend, primary, &actual, error) &&
        qa_q3_presentation_binding_read(group->primary, &retained, error) &&
        actual.world == group->world && actual.geometry == group->geometry &&
        actual.frame == &group->owner->frontend->frame &&
        actual.options.seat == primary->physical_seat &&
        retained.world == group->world && retained.geometry == group->geometry &&
        retained.frame == actual.frame && retained.options.owner == group->view.primary_identity &&
        retained.options.seat == group->view.physical_seat;
}

bool frontend_selected_effects_group_current(const frontend_selected_effects_group *group,
    const qa_application_selected_effects *source)
{
    return group && !group->restoring && group->launch && source && source->kind == QA_APPLICATION_EFFECTS_Q3 &&
        group->view.provider == source->provider && group->view.product == source->product &&
        group->view.q3_product == source->q3_product && qa_vfs_lookup_equal(group->view.content.mounts, source->content) &&
        qa_vfs_lookup_equal(group->launch->content, source->content) &&
        group->publication_generation == source->publication_generation && group->map_revision == source->map_revision &&
        group->launch->storage == source->launch->storage;
}

static bool active_current(frontend_selected_effects_group *group, const q3n_frame *frame, qa_error *error)
{
    return group_primary_current(group, group->active_primary, error) &&
        frame && frame->effect_output_context == group &&
        frame->effects_source == group->active_source && frame->effect_event == group->active_event &&
        frontend_selected_effects_group_current(group, frame->effects_source) && q3ne_current(frame, error);
}

static bool pose_current(void *context)
{
    const frontend_selected_effects_group *group = context;
    const frontend_selected_effects_pose *pose = group->active_pose;
    return group->active_event && group->active_event->event && pose && pose->current &&
        qa_actor_id_equal(pose->actor, group->active_event->event->actor) && pose->current(pose->context);
}

static bool trace(void *context, const q3n_frame *frame, qa_vec3 start, qa_vec3 end,
    qa_bounds bounds, int32_t skip, uint32_t mask, qa_trace_result *out, qa_error *error)
{
    (void)skip;
    frontend_selected_effects_group *group = context;
    qa_trace_query query = {.start = start, .end = end, .shape = {QA_SHAPE_BOX, bounds},
        .policy = qa_collision_default_policy(QA_COLLISION_Q3)};
    query.policy.contents_mask = mask;
    return active_current(group, frame, error) && qa_collision_trace(group->geometry,
        qa_world_trace_scratch(qa_application_world(group->owner->frontend->application),group->geometry),
        &query, out, error) &&
        active_current(group, frame, error);
}

static bool contents(void *context, const q3n_frame *frame, qa_vec3 point, int32_t pass,
    uint32_t *out, qa_error *error)
{
    (void)pass;
    frontend_selected_effects_group *group = context;
    qa_point_query query = {.point = point, .policy = qa_collision_default_policy(QA_COLLISION_Q3)};
    qa_point_contents value;
    if (!out || !active_current(group, frame, error) ||
        !qa_collision_point_contents(group->geometry,
            qa_world_trace_scratch(qa_application_world(group->owner->frontend->application),group->geometry),
            &query, &value, error) || !active_current(group, frame, error)) return false;
    *out = (uint32_t)value.contents; return true;
}

static bool marks(void *context, const q3n_frame *frame, const qa_vec3 *points, size_t count,
    qa_vec3 projection, qa_vec3 *out, size_t point_capacity, q3n_mark_fragment *fragments,
    size_t capacity, size_t *returned, qa_error *error)
{
    frontend_selected_effects_group *group = context;
    if (!returned || !active_current(group, frame, error) || capacity > SIZE_MAX / sizeof(qa_scene_mark_fragment)) return false;
    qa_scene_mark_fragment *native = capacity ? calloc(capacity, sizeof(*native)) : NULL;
    if (capacity && !native) return frontend_fail(error, QA_ERROR_MEMORY, "Allocating actual selected effect mark fragments");
    qa_scene_mark_result result;
    bool okay = qa_scene_world_mark_fragments(group->world, points, count, projection,
        out, point_capacity, native, capacity, &result, error) && active_current(group, frame, error);
    for (size_t i = 0; okay && i < result.fragment_count; ++i) {
        if (native[i].first_point > UINT32_MAX || native[i].point_count > UINT32_MAX)
            okay = frontend_fail(error, QA_ERROR_FORMAT, "Selected effect mark fragment exceeds its source word");
        else fragments[i] = (q3n_mark_fragment){(uint32_t)native[i].first_point, (uint32_t)native[i].point_count};
    }
    if (okay) *returned = result.fragment_count;
    free(native); return okay;
}

static double clock(void *context) { return (double)((frontend_selected_effects_group *)context)->time; }
static uint64_t bus(void *context) { return ((frontend_selected_effects_group *)context)->audio_bus; }
static bool sound_output(void *context, const q3n_frame *frame, qa_audio_asset *asset,
    const qa_vec3 *origin, int32_t channel, qa_error *error)
{
    frontend_selected_effects_group *group = context;
    if (!asset || !origin || !active_current(group, frame, error)) return false;
    qa_frontend *frontend = group->owner->frontend;
    if (!frontend->audio || group->view.presentation->options.audio != frontend->audio)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected effect sound lost its actual shared audio owner");
    qa_resource *resource = qa_audio_asset_resource(asset);
    qa_audio_play play = {.sample = qa_audio_asset_sample(asset), .asset = asset,
        .resource_id = resource ? qa_resource_id(resource) : 0, .name = qa_audio_asset_name(asset),
        .family = QA_AUDIO_Q3, .actor = QA_AUDIO_NO_ACTOR, .origin_actor = QA_AUDIO_NO_ACTOR,
        .owner = group->view.identity, .audience = QA_AUDIO_WORLD, .origin_kind = QA_AUDIO_FIXED,
        .origin = *origin, .channel = channel, .volume = 1, .attenuation = 1};
    return qa_audio_engine_play(frontend->audio, &play,
        q3ne_word((uint32_t)(frontend->time_ns / UINT64_C(1000000))), error) && active_current(group, frame, error);
}

static bool capture_ref(void *context, const qa_q3_ref_entity *ref, float radius, qa_error *error)
{
    frontend_selected_effects_group *group = context;
    if (group->ref_count == group->ref_capacity) {
        size_t capacity = group->ref_capacity ? group->ref_capacity * 2 : 32;
        if (capacity < group->ref_capacity || capacity > SIZE_MAX / sizeof(*group->refs))
            return frontend_fail(error, QA_ERROR_MEMORY, "Selected effect reference capacity is exhausted");
        frontend_selected_effects_ref *next = realloc(group->refs, capacity * sizeof(*next));
        if (!next) return frontend_fail(error, QA_ERROR_MEMORY, "Capturing actual selected effect reference");
        group->refs = next; group->ref_capacity = capacity;
    }
    group->refs[group->ref_count++] = (frontend_selected_effects_ref){*ref, radius}; return true;
}

bool frontend_selected_effects_group_dispose(frontend_selected_effects_group *group, qa_error *error)
{
    if (!group) return true;
    if (group->active_source || group->active_primary || group->active_pose || !q3n_events_idle(group->view.events) ||
        (group->view.presentation && !qa_q3_presentation_idle(group->view.presentation)))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected effect group still owns an active source frame");
    if (group->view.presentation && group->view.presentation->options.audio &&
        !qa_audio_engine_stop_owner(group->view.presentation->options.audio, group->view.identity, QA_AUDIO_WORLD, error)) return false;
    if (group->view.presentation && !qa_q3_presentation_destroy(group->view.presentation, error)) return false;
    group->view.presentation = NULL;
    q3n_events_destroy(group->view.events); q3n_media_destroy(group->view.media);
    qa_q3_presentation_assets_destroy(group->view.assets); qa_audio_bank_destroy(group->view.sounds);
    qa_launch_instance_lease_release(group->launch_lease); free(group->refs); free(group); return true;
}

bool frontend_selected_effects_empty(frontend_selected_effects_group *group,
    const qa_q3_presentation_binding *binding, qa_error *error)
{
    if (!binding->options.audio)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected effects need the actual shared audio owner");
    bool okay = qa_audio_bank_create(group->view.content.mounts, &group->view.sounds, error);
    qa_q3_presentation_asset_options assets = {.provider = {group->view.content.mounts,
        group->view.content.images, group->view.content.materials, QA_SCENE_Q3}, .sounds = group->view.sounds};
    if (okay) okay = qa_q3_presentation_assets_create(&assets, &group->view.assets, error);
    qa_q3_presentation_options backend = binding->options;
    backend.assets = group->view.assets; backend.owner = group->view.identity;
    backend.context = group; backend.clock = (qa_media_clock){group, clock};
    backend.audio_actor = NULL; backend.listener = NULL; backend.music = NULL; backend.frame_number = NULL;
    backend.milliseconds = NULL; backend.audio_bus = bus; backend.system_movie = NULL;
    backend.prepare_view = NULL; backend.submit_view = NULL; backend.prepare_picture = NULL;
    backend.video_frame = NULL; backend.video_context = NULL; backend.remap = NULL; backend.print = NULL; backend.scene_cleared = NULL;
    if (okay) okay = qa_q3_presentation_create(&backend, &group->view.presentation, error);
    q3n_media_options media = {.product=group->view.q3_product, .assets=group->view.assets};
    q3n_event_options events = {.product = group->view.q3_product, .assets = group->view.assets,
        .context = group, .trace = trace, .point_contents = contents, .mark_fragments = marks};
    if (okay) okay = q3n_media_create(&media, &group->view.media, error) &&
        q3n_events_create_effects(&events, &group->view.events, error);
    return okay;
}

bool frontend_selected_effects_constructor(qa_frontend *frontend, const frontend_effects_primary *primary,
    const qa_application_selected_effects *source, const qa_application_effect_event *event,
    frontend_selected_effects_group **out, qa_error *error)
{
    qa_q3_presentation_binding binding;
    if (!out || !primary_current(frontend, primary) || !source || source->kind != QA_APPLICATION_EFFECTS_Q3 ||
        !qa_q3_presentation_binding_read(primary->presentation, &binding, error) ||
        !binding.world || !binding.geometry || binding.frame != &frontend->frame ||
        binding.options.seat != primary->physical_seat ||
        binding.options.audio != frontend->audio || !frontend->audio)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected effects need their actual physical primary map and scene binding");
    frontend_selected_effects_group *group = calloc(1, sizeof(*group));
    if (!group) return frontend_fail(error, QA_ERROR_MEMORY, "Allocating selected effect content owner");
    group->view.provider = source->provider; group->view.product = source->product;
    group->view.q3_product = source->q3_product; group->view.physical_seat = binding.options.seat;
    group->view.source_files = source->content;
    group->view.primary_identity = binding.options.owner; group->primary = primary->presentation;
    group->publication_generation = source->publication_generation; group->map_revision = source->map_revision;
    group->world = binding.world; group->geometry = binding.geometry; group->time = source->sample_time_ms;
    group->audio_bus = binding.options.audio_bus ? binding.options.audio_bus(binding.options.context) : 0;
    bool okay = frontend_source_identity_allocate(frontend, &group->view.identity, error) &&
        qa_launch_instance_retain_metadata(source->launch, &group->launch_lease, error);
    if (okay) group->launch = qa_launch_instance_lease_view(group->launch_lease);
    if (okay) okay = frontend_visual_media_acquire(frontend, source->provider, QA_GAME_Q3, &group->view.content, error) &&
        qa_vfs_lookup_equal(group->view.content.mounts, source->content);
    if (okay) okay = frontend_selected_effects_empty(group, &binding, error) &&
        qa_q3_presentation_prepare_restored(group->view.presentation, binding.frame,
            binding.world, binding.geometry, binding.entities, error);
    if (okay) okay = q3n_media_load_effects(group->view.media, frontend->application, source, event, error) &&
        primary_current(frontend, primary);
    if (!okay) { frontend_selected_effects_group_dispose(group, NULL); return false; }
    *out = group; return true;
}

static q3n_frame frame(frontend_selected_effects_group *group, const qa_application_selected_effects *source,
    const qa_application_effect_event *event, const frontend_effects_primary *primary, const q3n_event_settings *settings)
{
    return (q3n_frame){.application = group->owner->frontend->application, .effects_source = source,
        .effect_event = event, .effect_output_context = group, .effect_entity_output = capture_ref,
        .effect_sound_output = sound_output, .effect_pose_current = event ? pose_current : NULL,
        .presentation = group->view.presentation, .assets = group->view.assets, .media = group->view.media,
        .events = group->view.events, .event_settings = settings, .preferences = primary->preferences,
        .time = group->time, .frame_milliseconds = group->sampled ? q3ne_sub(group->time, group->previous_time) : 0};
}

static bool renderer_name(const char *name, const char *match)
{
    for (; *name; ++name) {
        const char *text = name, *part = match;
        while (*text && *part) {
            unsigned char value = (unsigned char)*text++;
            if (value >= 'A' && value <= 'Z') value = (unsigned char)(value + 32);
            if (value != (unsigned char)*part) break;
            ++part;
        }
        if (!*part) return true;
    }
    return false;
}
static q3n_event_settings effect_settings(const qa_frontend *frontend)
{
    const qa_gl_capabilities *caps = qa_gl_capabilities_get(frontend->gl);
    bool ragepro = caps && !renderer_name(caps->renderer, "banshee") &&
        !renderer_name(caps->renderer, "voodoo_graphics") &&
        (renderer_name(caps->renderer, "rage pro") || renderer_name(caps->renderer, "ragepro"));
    return (q3n_event_settings){.blood = true, .gibs = true, .score_plum = true, .add_marks = true, .ragepro = ragepro};
}

static bool prepare_primary(qa_frontend *frontend, const frontend_effects_primary *primary, qa_error *error)
{
    qa_q3_presentation_binding consumer;
    if (!primary_binding(frontend, primary, &consumer, error) || !frontend_selected_effects_idle(frontend))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected effect sampling lost its real primary frame");
    frontend_selected_effects *owner = frontend->selected_effects;
    if (!owner) return true;
    owner->busy = true;
    bool okay = true;
    for (frontend_selected_effects_group *group = owner->groups; okay && group; group = group->next) {
        if (!group_binding(group, &consumer)) continue;
        qa_application_selected_effects source;
        qa_q3_presentation_binding binding;
        okay = qa_application_effects_producer_read(frontend->application, group->view.provider, &source, error) &&
            frontend_selected_effects_group_current(group, &source) &&
            qa_q3_presentation_binding_read(group->primary, &binding, error) &&
            binding.options.owner == group->view.primary_identity && binding.options.seat == group->view.physical_seat &&
            binding.world == group->world && binding.geometry == group->geometry && binding.frame == &frontend->frame &&
            group_primary_current(group, primary, error);
        if (!okay) break;
        if (group->prepared && group->prepared_application_frame == source.application_frame &&
            group->time == source.sample_time_ms) continue;
        if (group->sampled && q3ne_sub(source.sample_time_ms, group->previous_time) < 0) {
            okay = frontend_fail(error, QA_ERROR_ARGUMENT, "Selected effect clock rewound without resetting its actual source owner"); break;
        }
        group->time = source.sample_time_ms; group->active_source = &source; group->active_primary = primary;
        group->audio_bus = binding.options.audio_bus ? binding.options.audio_bus(binding.options.context) : 0;
        q3n_event_settings settings = effect_settings(frontend);
        q3n_frame current = frame(group, &source, NULL, primary, &settings);
        qa_bounds bounds = qa_scene_world_bounds(group->world);
        current.refdef.origin = qa_vec_add(bounds.maxs, qa_v3(65536,65536,65536));
        group->ref_count = 0;
        okay = q3ne_current(&current, error) && qa_q3_presentation_clear(group->view.presentation, error) &&
            q3n_local_submit(&current, error) && q3n_marks_submit(&current, error) &&
            q3ne_current(&current, error) && primary_current(frontend, primary);
        group->active_source = NULL; group->active_primary = NULL;
        if (okay) {
            group->previous_time = group->time; group->prepared_application_frame = source.application_frame;
            group->sampled = true; group->prepared = true;
        }
    }
    owner->busy = false;
    if (!okay && (!error || error->code == QA_OK))
        frontend_fail(error, QA_ERROR_ARGUMENT, "Selected effect sampling lost its admitted producer or primary map alias");
    return okay;
}

static bool lights_primary(qa_frontend *frontend, const frontend_effects_primary *primary, qa_error *error)
{
    qa_q3_presentation_binding consumer;
    if (!primary_binding(frontend, primary, &consumer, error) || !frontend_selected_effects_idle(frontend))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected effect lights require their actual open primary frame");
    frontend_selected_effects *owner = frontend->selected_effects;
    if (!owner) return true;
    owner->busy = true; bool okay = true;
    for (frontend_selected_effects_group *group = owner->groups; okay && group; group = group->next) {
        if (!group_binding(group, &consumer)) continue;
        qa_application_selected_effects source;
        okay = group->prepared && group_primary_current(group, primary, error) &&
            qa_application_effects_producer_read(frontend->application, group->view.provider, &source, error) &&
            frontend_selected_effects_group_current(group, &source) && group->prepared_application_frame == source.application_frame &&
            group->time == source.sample_time_ms;
        const qa_q3_presentation *packet = group->view.presentation;
        for (size_t i = 0; okay && i < packet->light_count; ++i) {
            const qa_scene_light *light = &packet->lights[i];
            okay = qa_q3_presentation_light(primary->presentation, light->origin,
                light->radius, light->color, light->additive, error) && primary_current(frontend, primary) &&
                qa_application_selected_effects_current(frontend->application, &source);
        }
    }
    owner->busy = false; return okay;
}

static bool event_primary(qa_frontend *frontend, const frontend_effects_primary *primary,
    const qa_application_effect_event *event, const frontend_selected_effects_pose *pose,
    bool *admitted, qa_error *error)
{
    if (!admitted || !event || !event->event || !primary_current(frontend, primary) ||
        !qa_application_effect_event_current(frontend->application, event))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected effect event lost its canonical source or primary recipient");
    *admitted = false;
    const qa_builtin_event *source_event = event->event;
    if (event->source.kind != QA_APPLICATION_EFFECTS_Q3 || source_event->kind != QA_BUILTIN_ANIMATION ||
        event->source.provider == event->source.primary) return true;
    qa_q3_player_state player;
    if (!qa_q3_player_read(event->source.native.q3, source_event->actor, &player) ||
        !(player.selections & QA_Q3_EFFECTS)) return true;
    int32_t code = source_event->code & ~0x300;
    if (code != Q3N_EV_JUMP_PAD && code != Q3N_EV_TELEPORT_IN && code != Q3N_EV_TELEPORT_OUT && code != Q3N_EV_GIB) return true;
    if (!pose) return true;
    if (!pose->current || !qa_actor_id_equal(pose->actor, source_event->actor) || !pose->current(pose->context))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected effect lost its actual captured full actor pose");
    if (!frontend->selected_effects) {
        frontend->selected_effects = calloc(1, sizeof(*frontend->selected_effects));
        if (!frontend->selected_effects) return frontend_fail(error, QA_ERROR_MEMORY, "Allocating selected effect producer owner");
        frontend->selected_effects->frontend = frontend;
    }
    frontend_selected_effects *owner = frontend->selected_effects;
    if (owner->busy) return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected effect owner is already emitting");
    qa_q3_presentation_binding consumer;
    if (!primary_binding(frontend, primary, &consumer, error)) return false;
    frontend_selected_effects_group *group = owner->groups;
    while (group && (!group_binding(group, &consumer) ||
        !frontend_selected_effects_group_current(group, &event->source))) group = group->next;
    if (!group) {
        if (!frontend_selected_effects_constructor(frontend, primary, &event->source, event, &group, error)) return false;
        group->owner = owner;
        if (owner->tail) owner->tail->next = group; else owner->groups = group;
        owner->tail = group;
    }
    if (!group_primary_current(group, primary, error))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected effect event changed its actual shared map binding");
    if (group->event_generation == event->queue_generation && event->event_id < group->event_cursor) return true;
    owner->busy = true; group->active_source = &event->source; group->active_event = event; group->active_primary = primary;
    group->active_pose = pose;
    group->time = q3ne_word((uint32_t)(source_event->time_ns / UINT64_C(1000000)));
    q3n_event_settings settings = effect_settings(frontend);
    q3n_frame current = frame(group, &event->source, event, primary, &settings);
    bool okay = q3ne_current(&current, error);
    if (okay) {
        if (code == Q3N_EV_GIB) q3n_effect_gib_player(&current, pose->origin);
        else if (code == Q3N_EV_JUMP_PAD) {
            q3n_smoke smoke = {.origin = pose->origin, .velocity = {0,0,1}, .radius = 32,
                .color = {1,1,1,.33f}, .duration = 1000, .start_time = current.time, .flags = Q3N_LE_DONT_SCALE,
                .shader = q3n_media_read(group->view.media)->graphics[Q3N_G_SMOKE_PUFF]};
            q3n_effect_smoke(&current, &smoke);
        } else q3n_effect_spawn(&current, pose->origin);
        okay = q3ne_current(&current, error) && primary_current(frontend, primary);
        if (okay) { group->event_generation = event->queue_generation; group->event_cursor = event->event_id + 1;
            group->prepared = false; *admitted = true; }
    }
    group->active_source = NULL; group->active_event = NULL; group->active_primary = NULL;
    group->active_pose = NULL; owner->busy = false; return okay;
}

size_t frontend_selected_effects_count(const qa_frontend *frontend)
{
    size_t count = 0;
    for (const frontend_selected_effects_group *group = frontend && frontend->selected_effects ? frontend->selected_effects->groups : NULL;
        group; group = group->next) ++count;
    return count;
}
const frontend_selected_effects_group *frontend_selected_effects_group_at(const qa_frontend *frontend, size_t ordinal)
{
    const frontend_selected_effects_group *group = frontend && frontend->selected_effects ? frontend->selected_effects->groups : NULL;
    while (group && ordinal) { group = group->next; --ordinal; } return group;
}
bool frontend_selected_effects_source_matches(const qa_frontend *frontend,
    const frontend_source_effects *source, size_t ordinal)
{
    const frontend_selected_effects_group *group = frontend_selected_effects_group_at(frontend, ordinal);
    qa_q3_presentation_binding binding;
    return group && group->owner && group->owner->frontend == frontend &&
        frontend_source_effects_binding_read(source, frontend, &binding) && group_binding(group, &binding);
}
bool frontend_selected_effects_native_matches(const qa_frontend *frontend,
    const q3n_frame *frame, size_t ordinal)
{
    const frontend_selected_effects_group *group = frontend_selected_effects_group_at(frontend, ordinal);
    frontend_effects_primary primary = native_primary(frame); qa_q3_presentation_binding binding;
    return group && group->owner && group->owner->frontend == frontend &&
        primary_binding(frontend, &primary, &binding, NULL) && group_binding(group, &binding);
}
bool frontend_selected_effects_at(const qa_frontend *frontend, size_t ordinal, frontend_selected_effects_view *out, qa_error *error)
{
    const frontend_selected_effects_group *group = frontend_selected_effects_group_at(frontend, ordinal);
    if (!out || !group || group->active_source || frontend->selected_effects->busy)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected effect inventory requires its actual idle physical group");
    *out = group->view; out->source_time_ms = group->time;
    out->sampled_application_frame = group->prepared_application_frame; out->prepared = group->prepared;
    return true;
}
bool frontend_selected_effects_parent_resolve(const qa_frontend *frontend,
    frontend_selected_effects_parent_kind kind, size_t ordinal, qa_q3_presentation **out, qa_error *error)
{
    if (!frontend || !out) return false;
    if (kind == FRONTEND_EFFECTS_PARENT_SOURCE) {
        frontend_source_group_view source;
        if (!frontend_source_group_read(frontend, ordinal, &source) || !source.presentation)
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected effects lost their actual source parent row");
        *out = source.presentation;
    } else if (kind == FRONTEND_EFFECTS_PARENT_NATIVE) {
        frontend_native_q3_view source;
        if (!frontend_native_q3_read(frontend, ordinal, &source, error) || !source.presentation) return false;
        *out = source.presentation;
    } else return frontend_fail(error, QA_ERROR_FORMAT, "Selected effect parent has no genuine physical owner kind");
    return true;
}
bool frontend_selected_effects_parent_read(const qa_frontend *frontend, size_t ordinal,
    frontend_selected_effects_parent *out, qa_error *error)
{
    const frontend_selected_effects_group *group = frontend_selected_effects_group_at(frontend, ordinal);
    if (!out || !group || group->active_source || frontend->selected_effects->busy)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected effect parent observation requires its returned source frame");
    bool found = false; frontend_selected_effects_parent parent = {0};
    for (unsigned kind = FRONTEND_EFFECTS_PARENT_SOURCE; kind <= FRONTEND_EFFECTS_PARENT_NATIVE; ++kind) {
        size_t count = kind == FRONTEND_EFFECTS_PARENT_SOURCE ? frontend_source_group_count(frontend) : frontend_native_q3_count(frontend);
        for (size_t i = 0; i < count; ++i) {
            qa_q3_presentation *actual;
            if (!frontend_selected_effects_parent_resolve(frontend, (frontend_selected_effects_parent_kind)kind, i, &actual, error)) return false;
            if (actual != group->primary) continue;
            if (found) return frontend_fail(error, QA_ERROR_FORMAT, "Selected effect parent repeats physical destructor authority");
            parent.kind = (frontend_selected_effects_parent_kind)kind; parent.ordinal = i; found = true;
        }
    }
    if (!found || !qa_q3_presentation_binding_read(group->primary, &parent.binding, error) ||
        parent.binding.options.owner != group->view.primary_identity || parent.binding.options.seat != group->view.physical_seat ||
        (parent.binding.frame != &frontend->frame && (!group->restoring || parent.binding.frame)))
        return frontend_fail(error, QA_ERROR_FORMAT, "Selected effect parent leaves its actual retained backend identity");
    *out = parent; return true;
}
bool frontend_selected_effects_q3_ready(const qa_frontend *frontend, size_t ordinal,
    const qa_q3_presentation_options *policy, const qa_q3_presentation_asset_options *assets, qa_error *error)
{
    const frontend_selected_effects_group *group = frontend_selected_effects_group_at(frontend, ordinal);
    frontend_selected_effects_parent parent; qa_q3_presentation_binding own;
    if (!group || !policy || !assets || !frontend_selected_effects_parent_read(frontend, ordinal, &parent, error) ||
        !qa_q3_presentation_binding_read(group->view.presentation, &own, error)) return false;
    const qa_q3_presentation_options *source = &parent.binding.options;
    return policy->assets == group->view.assets && policy->audio == source->audio &&
        policy->owner == group->view.identity && policy->seat == group->view.physical_seat &&
        policy->context == group && policy->clock.context == group && policy->clock.sample == clock &&
        !policy->audio_actor && !policy->milliseconds && policy->audio_bus == bus &&
        !policy->listener && !policy->music && !policy->frame_number && !policy->system_movie &&
        !policy->prepare_view && !policy->submit_view && !policy->prepare_picture &&
        !policy->video_frame && !policy->video_context && !policy->remap && !policy->print && !policy->scene_cleared &&
        policy->near_clip == source->near_clip && policy->far_clip == source->far_clip &&
        policy->identity_light == source->identity_light && policy->lod_scale == source->lod_scale &&
        policy->lod_bias == source->lod_bias && policy->shadow_mode == source->shadow_mode &&
        policy->rail_core_width == source->rail_core_width && policy->rail_ring_width == source->rail_ring_width &&
        policy->rail_segment_length == source->rail_segment_length &&
        own.world == parent.binding.world && own.geometry == parent.binding.geometry && own.frame == parent.binding.frame &&
        own.entities.data == parent.binding.entities.data && own.entities.size == parent.binding.entities.size &&
        assets->provider.mounts == group->view.content.mounts && assets->provider.images == group->view.content.images &&
        assets->provider.materials == group->view.content.materials && assets->provider.family == QA_SCENE_Q3 &&
        assets->sounds == group->view.sounds && !assets->movies && !assets->zero_sound &&
        !assets->context && !assets->select && !assets->print ? true :
        frontend_fail(error, QA_ERROR_FORMAT, "Selected effect dictionary differs from its genuine standalone constructor");
}
static bool output_primary(const qa_frontend *frontend, const frontend_effects_primary *primary, size_t ordinal,
    const qa_q3_scene_options *options, const qa_scene_frame *frame,
    const frontend_selected_effects_group **out_group, frontend_selected_effects_view *out_view, qa_error *error)
{
    const frontend_selected_effects_group *group = frontend_selected_effects_group_at(frontend, ordinal);
    qa_application_selected_effects source; qa_q3_presentation_binding actual, retained;
    if (!out_group || !out_view || !group || !primary ||
        group->active_source || group->active_primary || group->active_pose ||
        frontend->selected_effects->busy || !primary_current(frontend, primary) || !group->prepared ||
        !qa_application_effects_producer_read(frontend->application, group->view.provider, &source, error) ||
        !frontend_selected_effects_group_current(group, &source) || group->prepared_application_frame != source.application_frame ||
        group->time != source.sample_time_ms || !q3n_media_effects_ready(group->view.media) ||
        !qa_q3_presentation_selected_binding_read(primary->presentation, options, frame, &actual, error) ||
        !qa_q3_presentation_binding_read(group->view.presentation, &retained, error) ||
        actual.frame != &frontend->frame || actual.options.seat != primary->physical_seat ||
        actual.world != group->world || actual.geometry != group->geometry ||
        retained.world != actual.world || retained.geometry != actual.geometry || retained.frame != actual.frame)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected effect output lost its actual source packet or primary view lease");
    *out_group = group; *out_view = group->view; out_view->source_time_ms = group->time;
    out_view->sampled_application_frame = group->prepared_application_frame; out_view->prepared = true; return true;
}
bool frontend_selected_effects_prepare(qa_frontend *frontend, const q3n_frame *frame, qa_error *error)
{ frontend_effects_primary primary = native_primary(frame); return prepare_primary(frontend, &primary, error); }
bool frontend_selected_effects_lights(qa_frontend *frontend, const q3n_frame *frame, qa_error *error)
{ frontend_effects_primary primary = native_primary(frame); return lights_primary(frontend, &primary, error); }
bool frontend_selected_effects_event(qa_frontend *frontend, const q3n_frame *frame,
    const qa_application_effect_event *event, const frontend_selected_effects_pose *pose, bool *admitted, qa_error *error)
{ frontend_effects_primary primary = native_primary(frame); return event_primary(frontend, &primary, event, pose, admitted, error); }
bool frontend_selected_effects_output_read(const qa_frontend *frontend, const q3n_frame *frame, size_t ordinal,
    const qa_q3_scene_options *options, const qa_scene_frame *scene,
    const frontend_selected_effects_group **group, frontend_selected_effects_view *view, qa_error *error)
{ frontend_effects_primary primary = native_primary(frame); return output_primary(frontend, &primary, ordinal, options, scene, group, view, error); }
bool frontend_selected_effects_source_prepare(qa_frontend *frontend, const frontend_source_effects *source, qa_error *error)
{ frontend_effects_primary primary; return source_primary(frontend, source, &primary) && prepare_primary(frontend, &primary, error); }
bool frontend_selected_effects_source_light_read(const qa_frontend *frontend, const frontend_source_effects *scope,
    size_t ordinal, const qa_scene_light **out, size_t *count, qa_error *error)
{
    const frontend_selected_effects_group *group = frontend_selected_effects_group_at(frontend, ordinal);
    qa_application_selected_effects source; qa_q3_presentation_binding binding;
    if (!out || !count || !frontend_selected_effects_source_matches(frontend, scope, ordinal) ||
        !frontend_selected_effects_idle(frontend) || !group->prepared ||
        !qa_application_effects_producer_read(frontend->application, group->view.provider, &source, error) ||
        !frontend_selected_effects_group_current(group, &source) || group->prepared_application_frame != source.application_frame ||
        group->time != source.sample_time_ms || !q3n_media_effects_ready(group->view.media) ||
        !qa_q3_presentation_binding_read(group->view.presentation, &binding, error) ||
        binding.world != group->world || binding.geometry != group->geometry || binding.frame != &frontend->frame)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Source effect lights lost their actual completed packet");
    *out = group->view.presentation->lights; *count = group->view.presentation->light_count; return true;
}
bool frontend_selected_effects_source_event(qa_frontend *frontend, const frontend_source_effects *source,
    const qa_application_effect_event *event, const frontend_selected_effects_pose *pose, bool *admitted, qa_error *error)
{ frontend_effects_primary primary; return source_primary(frontend, source, &primary) && event_primary(frontend, &primary, event, pose, admitted, error); }
bool frontend_selected_effects_source_output_read(const qa_frontend *frontend, const frontend_source_effects *source, size_t ordinal,
    const qa_q3_scene_options *options, const qa_scene_frame *scene,
    const frontend_selected_effects_group **group, frontend_selected_effects_view *view, qa_error *error)
{ frontend_effects_primary primary; return source_primary(frontend, source, &primary) && output_primary(frontend, &primary, ordinal, options, scene, group, view, error); }
size_t frontend_selected_effects_ref_count(const frontend_selected_effects_group *group)
{ return group && group->prepared && !group->active_source ? group->ref_count : 0; }
const frontend_selected_effects_ref *frontend_selected_effects_ref_at(const frontend_selected_effects_group *group, size_t ordinal)
{ return group && group->prepared && !group->active_source && ordinal < group->ref_count ? &group->refs[ordinal] : NULL; }
size_t frontend_selected_effects_poly_count(const frontend_selected_effects_group *group)
{ return group && group->prepared && !group->active_source ? group->view.presentation->polygon_count : 0; }
bool frontend_selected_effects_poly_at(const frontend_selected_effects_group *group, size_t ordinal,
    frontend_selected_effects_poly *out, qa_error *error)
{
    if (!group || !out || !group->prepared || group->active_source || ordinal >= group->view.presentation->polygon_count)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected effect polygon lacks its actual retained packet row");
    const qa_q3_presentation *packet = group->view.presentation;
    const q3p_polygon *poly = &packet->polygons[ordinal];
    if (poly->first > packet->vertex_count || poly->count > packet->vertex_count - poly->first)
        return frontend_fail(error, QA_ERROR_FORMAT, "Selected effect polygon exceeds its actual retained vertices");
    *out = (frontend_selected_effects_poly){poly->shader, packet->vertices + poly->first, poly->count, poly->fog}; return true;
}

bool frontend_selected_effects_idle(const qa_frontend *frontend)
{
    if (!frontend) return false;
    const frontend_selected_effects *owner = frontend->selected_effects;
    if (!owner) return true;
    if (owner->busy) return false;
    for (const frontend_selected_effects_group *group = owner->groups; group; group = group->next)
        if (group->active_source || group->active_primary || group->active_pose || !q3n_events_idle(group->view.events) || !q3n_media_idle(group->view.media) ||
            !qa_q3_presentation_idle(group->view.presentation)) return false;
    return true;
}
bool frontend_selected_effects_retire(qa_frontend *frontend, qa_error *error)
{
    if (!frontend_selected_effects_idle(frontend))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected effects retirement requires the actual idle producer and frame owners");
    frontend_selected_effects *owner = frontend->selected_effects;
    if (!owner) return true;
    while (owner->groups) {
        frontend_selected_effects_group *group = owner->groups, *next = group->next;
        if (!frontend_selected_effects_group_dispose(group, error)) return false;
        owner->groups = next;
    }
    free(owner); frontend->selected_effects = NULL; return true;
}
bool frontend_selected_effects_retire_source(qa_frontend *frontend, qa_actor_owner provider,
    const qa_launch_instance *launch, qa_error *error)
{
    if (!provider || !launch || !launch->storage || !frontend_selected_effects_idle(frontend))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected effect source retirement requires its returned actual owner");
    frontend_selected_effects *owner = frontend->selected_effects;
    if (!owner) return true;
    frontend_selected_effects_group **link = &owner->groups;
    while (*link) {
        frontend_selected_effects_group *group = *link;
        bool selected = group->view.provider == provider && group->launch && group->launch->storage == launch->storage;
        if (!selected) { link = &group->next; continue; }
        frontend_selected_effects_group *next = group->next;
        if (!frontend_selected_effects_group_dispose(group, error)) return false;
        *link = next;
        owner->tail = NULL;
        for (frontend_selected_effects_group *last = owner->groups; last; last = last->next) owner->tail = last;
    }
    return true;
}
bool frontend_selected_effects_retire_parent(qa_frontend *frontend, const qa_q3_presentation *primary, qa_error *error)
{
    if (!primary || !frontend_selected_effects_idle(frontend))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected effect parent retirement requires its returned actual backend");
    frontend_selected_effects *owner = frontend->selected_effects;
    if (!owner) return true;
    frontend_selected_effects_group **link = &owner->groups;
    while (*link) {
        frontend_selected_effects_group *group = *link;
        if (group->primary != primary) { link = &group->next; continue; }
        frontend_selected_effects_group *next = group->next;
        if (!frontend_selected_effects_group_dispose(group, error)) return false;
        *link = next;
        owner->tail = NULL;
        for (frontend_selected_effects_group *last = owner->groups; last; last = last->next) owner->tail = last;
    }
    return true;
}
bool frontend_selected_effects_round(qa_frontend *frontend, qa_error *error)
{
    if (!frontend_selected_effects_idle(frontend))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected effect round reset requires its actual idle owners");
    if (!frontend->selected_effects) return true;
    for (frontend_selected_effects_group *group = frontend->selected_effects->groups; group; group = group->next) {
        if (!qa_q3_presentation_clear(group->view.presentation, error)) return false;
        q3n_events_round(group->view.events); group->ref_count = 0; group->sampled = false;
        group->prepared = false;
        group->prepared_application_frame = 0;
        group->event_cursor = qa_application_events_next(frontend->application);
        group->event_generation = qa_application_protocol_events_generation(frontend->application);
    }
    return true;
}
bool frontend_selected_effects_content_visit(const qa_frontend *frontend, const qa_application_content_visitor *visitor, qa_error *error)
{
    if (!frontend || !visitor || !visitor->catalog || !visitor->view ||
        (frontend->selected_effects && frontend->selected_effects->busy))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected effects content visitor requires its actual retained graph");
    for (const frontend_selected_effects_group *group = frontend->selected_effects ? frontend->selected_effects->groups : NULL; group; group = group->next) {
        const qa_q3_presentation_assets *assets = group->view.assets;
        if (group->restoring || !group->launch || group->active_source || group->active_event ||
            group->active_primary || group->active_pose || !q3n_events_idle(group->view.events) ||
            !q3n_media_idle(group->view.media) || !group->view.presentation || group->view.presentation->busy ||
            group->view.presentation->options.assets != assets)
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected effect content still holds an actual source callback");
        bool captured = false;
        if (frontend->capture && assets && assets->users && assets->capturing && assets->busy == 1 && !assets->codec_busy) {
            for (size_t i = 0;; ++i) {
                const qa_q3_presentation_assets *held = frontend_capture_assets_at(frontend->capture, i);
                if (!held) break;
                if (held == assets) { captured = true; break; }
            }
        }
        qa_q3_presentation_binding binding;
        if (captured ? !qa_q3_presentation_binding_read(group->view.presentation, &binding, error) :
            !qa_q3_presentation_idle(group->view.presentation))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected effect content lacks its actual idle or captured registry lease");
        if (!visitor->catalog(visitor->context, qa_launch_instance_catalog(group->launch), error) ||
            !visitor->view(visitor->context, group->launch->content, error) ||
            !visitor->view(visitor->context, group->view.source_files, error) ||
            !visitor->view(visitor->context, group->view.content.mounts, error)) return false;
    }
    return true;
}
