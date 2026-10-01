#include "native_composition.h"
#include "native_character.h"
#include "equipment_native.h"
#include "selected_effects.h"
#include "../../presentation/q3_native/weapon.h"
#include "save_private.h"
#include <stdlib.h>
#include <string.h>

typedef struct primary_pose {
    struct primary_pose *next;
    qa_actor_id actor;
    uint32_t physical;
    qa_vec3 origin;
} primary_pose;
typedef struct native_composition {
    qa_frontend *frontend;
    frontend_native_q3 *row;
    frontend_native_q3_composition equipment;
    frontend_native_character *character;
    const q3n_frame *frame;
    primary_pose *primary_poses;
    uint32_t effects_first, effects_count;
    bool entered;
} native_composition;
static bool idle(const void *context)
{
    const native_composition *owner = context;
    return owner && !owner->entered && !owner->frame && !owner->primary_poses &&
        (!owner->character || frontend_native_character_idle(owner->character)) &&
        (!owner->equipment.context || owner->equipment.idle(owner->equipment.context));
}
static bool destroy(void *context, qa_error *error)
{
    native_composition *owner = context;
    if (!idle(owner)) return frontend_fail(error, QA_ERROR_ARGUMENT, "Native composition retains an actual frame child");
    if (owner->character && !frontend_native_character_destroy(owner->character, error)) return false;
    owner->character = NULL;
    if (owner->equipment.context && !owner->equipment.destroy(owner->equipment.context, error)) return false;
    owner->equipment = (frontend_native_q3_composition){0}; free(owner); return true;
}
static bool rebind_ready(const void *context, const qa_frontend *frontend, qa_error *error)
{
    const native_composition *owner = context;
    return idle(owner) && owner->frontend == frontend &&
        frontend_native_character_rebind_ready(owner->character, frontend, error) &&
        owner->equipment.rebind_ready(owner->equipment.context, frontend, error);
}
static void rebind(void *context, qa_frontend *frontend)
{
    native_composition *owner = context;
    frontend_native_character_rebind(owner->character, frontend);
    owner->equipment.rebind(owner->equipment.context, frontend); owner->frontend = frontend;
}
static bool begin(void *context, const q3n_frame *frame, qa_error *error)
{
    native_composition *owner = context;
    if (!idle(owner)) return frontend_fail(error, QA_ERROR_ARGUMENT, "Native composition entry requires its returned children");
    owner->entered = true; owner->frame = frame;
    return frontend_native_character_begin(owner->character, frame, error) &&
        owner->equipment.begin_frame(owner->equipment.context, frame, error);
}
static void end(void *context)
{
    native_composition *owner = context;
    if (!owner || !owner->entered) return;
    frontend_native_character_end(owner->character);
    owner->equipment.end_frame(owner->equipment.context);
    while (owner->primary_poses) {
        primary_pose *next = owner->primary_poses->next;
        free(owner->primary_poses); owner->primary_poses = next;
    }
    owner->frame = NULL; owner->entered = false;
    owner->effects_first = owner->effects_count = 0;
}
static bool captured_origin(native_composition *owner, qa_actor_id actor,
    qa_vec3 *origin, bool *found, qa_error *error)
{
    *found = false;
    if (!owner->entered || !owner->frame ||
        !qa_application_native_q3_presentation_current(owner->frame->application, &owner->frame->source) ||
        !frontend_native_character_origin(owner->character, owner->frame, actor, origin, found, error)) return false;
    if (*found) return true;
    for (const primary_pose *pose = owner->primary_poses; pose; pose = pose->next) {
        if (!qa_actor_id_equal(pose->actor, actor)) continue;
        qa_application_native_q3_entity actual;
        if (!qa_application_native_q3_presentation_entity(owner->frame->application, &owner->frame->source,
            pose->physical, &actual, error) || !actual.present || !qa_actor_id_equal(actual.binding.actor, actor))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Captured primary pose lost its actual physical source actor");
        *origin = pose->origin; *found = true; return true;
    }
    qa_world *world = qa_application_world(owner->frame->application);
    if (!world || !qa_actors_get(qa_world_actors(world), actor) || !qa_world_body_storage_serial(world, actor)) return true;
    qa_body_state body;
    if (!qa_world_body_read(world, actor, &body, error) ||
        !qa_application_native_q3_presentation_current(owner->frame->application, &owner->frame->source)) return false;
    *origin = body.origin; *found = true; return true;
}
typedef struct effect_pose_scope {
    native_composition *owner;
    qa_actor_id actor;
    qa_vec3 origin;
} effect_pose_scope;
static bool effect_pose_current(void *context)
{
    effect_pose_scope *scope = context; qa_vec3 actual; bool found;
    return captured_origin(scope->owner, scope->actor, &actual, &found, NULL) && found &&
        actual.x == scope->origin.x && actual.y == scope->origin.y && actual.z == scope->origin.z;
}
static bool before_render(void *context, const q3n_frame *frame, qa_error *error)
{
    native_composition *owner = context;
    if (!owner || !owner->entered || owner->frame != frame || !frame ||
        frame->application != owner->frontend->application ||
        !qa_application_native_q3_presentation_current(frame->application, &frame->source))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected effects require their actual entered native frame");
    size_t count = qa_application_event_count(frame->application);
    for (size_t i = 0; i < count; ++i) {
        qa_builtin_event event;
        if (!qa_application_event_at(frame->application, i, &event))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected effect canonical queue changed during its native frame");
        if (event.family != QA_GAME_Q3) continue;
        effect_pose_scope scope = {.owner = owner, .actor = event.actor}; bool found;
        if (!captured_origin(owner, event.actor, &scope.origin, &found, error)) return false;
        if (!found) continue;
        frontend_selected_effects_pose pose = {event.actor, scope.origin, &scope, effect_pose_current};
        qa_application_effect_event actual; bool admitted;
        if (!qa_application_effect_event_read(frame->application, i, &actual, error) ||
            !frontend_selected_effects_event(owner->frontend, frame, &actual, &pose, &admitted, error)) return false;
    }
    return frontend_selected_effects_prepare(owner->frontend, frame, error) &&
        frontend_selected_effects_lights(owner->frontend, frame, error) &&
        qa_application_native_q3_presentation_current(frame->application, &frame->source);
}
static bool body_hidden(void *context, const q3n_frame *frame,
    const qa_application_native_q3_entity *actual, bool *hidden, qa_error *error)
{ native_composition *owner = context; return frontend_native_character_body_hidden(owner->character, frame, actual, hidden, error); }
static bool capture_primary_pose(native_composition *owner, const q3n_frame *frame,
    const qa_application_native_q3_entity *actual, uint32_t part, const qa_q3_ref_entity *ref, qa_error *error)
{
    if (part != 0 && part != 3) return true;
    if (!owner->entered || owner->frame != frame || !actual || !actual->present || !ref ||
        !qa_application_native_q3_presentation_current(frame->application, &frame->source)) return false;
    if (ref->kind != QA_Q3_REF_MODEL || (ref->flags & 4)) return true;
    for (const primary_pose *pose = owner->primary_poses; pose; pose = pose->next)
        if (qa_actor_id_equal(pose->actor, actual->binding.actor)) return true;
    if (actual->binding.number < 0 || (uint32_t)actual->binding.number >= frame->source.entity_count)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Primary presentation pose lacks its actual physical source row");
    primary_pose *pose = malloc(sizeof(*pose));
    if (!pose) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining actual primary presentation pose for selected effects");
    *pose = (primary_pose){owner->primary_poses, actual->binding.actor, (uint32_t)actual->binding.number, ref->origin};
    owner->primary_poses = pose; return true;
}
static bool body(void *context, const q3n_frame *frame, const qa_application_native_q3_entity *actual,
    uint32_t part, const qa_q3_ref_entity *material, bool base, bool *consumed, qa_error *error)
{
    native_composition *owner = context;
    return frontend_native_character_body(owner->character, frame, actual, part, material, base, consumed, error) &&
        (*consumed || capture_primary_pose(owner, frame, actual, part, material, error));
}
static bool packet(void *context, const q3n_frame *frame, const qa_application_native_q3_entity *actual,
    q3n_entity *cent, const qa_q3_ref_entity *material, bool *consumed, qa_error *error)
{ (void)cent; return body(context, frame, actual, 3, material, true, consumed, error); }
static bool view(void *context, const q3n_frame *frame, const qa_q3_player *player, bool *consumed, qa_error *error)
{ native_composition *owner = context; return owner->equipment.view_weapon(owner->equipment.context, frame, player, consumed, error); }
static bool held(void *context, const q3n_frame *frame, const qa_q3_entity *state,
    const qa_q3_ref_entity *torso, bool *consumed, qa_error *error)
{
    native_composition *owner = context;
    qa_application_native_q3_entity actual;
    const qa_q3_presentation_assets *parent_assets;
    const qa_q3_ref_entity *selected_torso; bool found;
    if (consumed) *consumed = false;
    if (!owner || !owner->entered || owner->frame != frame || !frame || !state || !torso ||
        !consumed || state->number < 0 || state->number >= 1024 ||
        !qa_application_native_q3_presentation_current(frame->application, &frame->source) ||
        !qa_application_native_q3_presentation_entity(frame->application, &frame->source,
            (uint32_t)state->number, &actual, error) ||
        !frontend_native_character_torso(owner->character, actual.binding.actor,
            &parent_assets, &selected_torso, &found, error)) return false;
    if (found) {
        if (!frontend_equipment_native_character_held(owner->equipment.context, frame,
            actual.binding.actor, parent_assets, selected_torso, state->powerups, consumed, error)) return false;
        if (*consumed) return true;
        qa_q3_ref_entity parent = *selected_torso;
        parent.flags = torso->flags; parent.lighting_origin = torso->lighting_origin;
        parent.shadow_plane = torso->shadow_plane;
        if (!q3n_weapons_player_parent(frame, parent_assets, &parent,
            &frame->entities[state->number], state, consumed, error)) return false;
        return frontend_native_character_admitted(owner->character, actual.binding.actor) &&
            qa_application_native_q3_presentation_current(frame->application, &frame->source);
    }
    return owner->equipment.held_weapon(owner->equipment.context, frame, state, torso, consumed, error);
}
static bool warning(void *context, const q3n_frame *frame, q3n_weapon_hud *out, qa_error *error)
{ native_composition *owner = context; return owner->equipment.weapon_warning(owner->equipment.context, frame, out, error); }
static bool actor_admitted(const void *context, qa_actor_id actor)
{ const native_composition *owner = context; return owner && frontend_native_character_admitted(owner->character, actor); }
static bool weapon_snapshot(const void *context, qa_application_equipment_view *out, bool *requested, qa_error *error)
{ const native_composition *owner = context; return owner && frontend_equipment_native_weapon(owner->equipment.context, out, requested, error); }
static void cleared(void *context)
{
    native_composition *owner = context;
    if (owner->equipment.scene_cleared) owner->equipment.scene_cleared(owner->equipment.context);
}
static bool prepare(void *context, const qa_q3_refdef *definition,
    qa_q3_scene_options *options, qa_error *error)
{
    native_composition *owner = context;
    if (!owner || !owner->entered || !owner->frame || !options ||
        !qa_application_native_q3_presentation_current(owner->frame->application, &owner->frame->source))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Native scene order requires its actual entered frame");
    owner->effects_first = options->first_entity; owner->effects_count = 0;
    if (!options->world.no_world) {
        size_t groups = frontend_selected_effects_count(owner->frontend);
        for (size_t i = 0; i < groups; ++i) {
            frontend_selected_effects_view view;
            qa_application_selected_effects source;
            const frontend_selected_effects_group *group = frontend_selected_effects_group_at(owner->frontend, i);
            if (!frontend_selected_effects_at(owner->frontend, i, &view, error) || !view.prepared ||
                !qa_application_effects_producer_read(owner->frame->application, view.provider, &source, error) ||
                view.sampled_application_frame != source.application_frame || view.source_time_ms != source.sample_time_ms)
                return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected effects have no completed source packet for this scene");
            size_t count = frontend_selected_effects_ref_count(group);
            if (options->first_entity >= 1022 || count > 1021u - options->first_entity)
                return frontend_fail(error, QA_ERROR_FORMAT, "Selected effects exceed the actual source scene order");
            options->first_entity += (uint32_t)count; owner->effects_count += (uint32_t)count;
        }
    }
    return frontend_native_character_prepare_view(owner->character, options, error) &&
        owner->equipment.prepare_view(owner->equipment.context, definition, options, error);
}
static bool effects_current(native_composition *owner, size_t ordinal,
    const qa_q3_scene_options *options, qa_scene_frame *frame,
    const frontend_selected_effects_group *expected, const frontend_selected_effects_view *saved,
    size_t refs, size_t polygons, qa_error *error)
{
    const frontend_selected_effects_group *actual; frontend_selected_effects_view view;
    return frontend_selected_effects_output_read(owner->frontend, owner->frame, ordinal,
        options, frame, &actual, &view, error) && actual == expected &&
        view.assets == saved->assets && view.source_time_ms == saved->source_time_ms &&
        frontend_selected_effects_ref_count(actual) == refs &&
        frontend_selected_effects_poly_count(actual) == polygons;
}
static bool effects_submit(native_composition *owner, const qa_q3_scene_options *options,
    qa_scene_frame *frame, qa_error *error)
{
    if (options->world.no_world) return true;
    uint32_t order = owner->effects_first;
    size_t groups = frontend_selected_effects_count(owner->frontend);
    for (size_t i = 0; i < groups; ++i) {
        const frontend_selected_effects_group *group; frontend_selected_effects_view view;
        if (!frontend_selected_effects_output_read(owner->frontend, owner->frame, i,
            options, frame, &group, &view, error)) return false;
        size_t refs = frontend_selected_effects_ref_count(group);
        size_t polygons = frontend_selected_effects_poly_count(group);
        if (order < owner->effects_first || order > owner->effects_first + owner->effects_count ||
            refs > owner->effects_first + owner->effects_count - order)
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected effect packet changed its reserved source extent");
        for (size_t p = 0; p < polygons; ++p) {
            frontend_selected_effects_poly poly;
            if (!frontend_selected_effects_poly_at(group, p, &poly, error) ||
                !qa_q3_presentation_selected_poly(owner->frame->presentation, view.assets, poly.shader,
                    poly.vertices, poly.count, view.source_time_ms, options, frame, error) ||
                !effects_current(owner, i, options, frame, group, &view, refs, polygons, error)) return false;
        }
        for (size_t r = 0; r < refs; ++r) {
            const frontend_selected_effects_ref *captured = frontend_selected_effects_ref_at(group, r);
            if (!captured) return frontend_fail(error, QA_ERROR_ARGUMENT, "Selected effect refEntity lost its actual packet row");
            qa_q3_ref_entity ref = captured->ref;
            if (ref.kind == QA_Q3_REF_PORTAL || (ref.kind != QA_Q3_REF_MODEL &&
                qa_vec_length(qa_vec_sub(ref.origin, options->world.view.origin)) < captured->cull_radius)) continue;
            if (!qa_q3_presentation_selected_effect(owner->frame->presentation, view.assets, &ref,
                view.source_time_ms, options, order + (uint32_t)r, frame, error) ||
                !effects_current(owner, i, options, frame, group, &view, refs, polygons, error)) return false;
        }
        order += (uint32_t)refs;
    }
    return order == owner->effects_first + owner->effects_count &&
        frontend_selected_effects_count(owner->frontend) == groups &&
        qa_application_native_q3_presentation_current(owner->frame->application, &owner->frame->source) ? true :
        frontend_fail(error, QA_ERROR_ARGUMENT, "Selected effects changed their actual prepared scene roster");
}
static bool submit(void *context, const qa_q3_scene_options *options, qa_scene_frame *frame, qa_error *error)
{
    native_composition *owner = context;
    return frontend_native_character_submit(owner->character, options, frame, error) &&
        owner->equipment.submit_view(owner->equipment.context, options, frame, error) &&
        effects_submit(owner, options, frame, error);
}
static bool blob(qa_source_save_io *io, qa_bytes *bytes)
{
    size_t count = io->direction == QA_SOURCE_SAVE_WRITE ? bytes->size : 0;
    if (!qa_source_save_count(io, &count, 64u * 1024u * 1024u)) return false;
    if (io->direction == QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io, (void *)bytes->data, count);
    if (io->offset > io->input.size || count > io->input.size - io->offset) return false;
    *bytes = (qa_bytes){io->input.data + io->offset, count}; io->offset += count; return true;
}
static bool checkpoint(const void *context, qa_buffer *out, qa_error *error)
{
    const native_composition *owner = context;
    if (!idle(owner) || !out || out->data || out->size)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Native composition capture requires its returned genuine children");
    qa_buffer children[2] = {0};
    bool okay = owner->equipment.checkpoint(owner->equipment.context, children, error) &&
        frontend_native_character_checkpoint(owner->character, children + 1, error);
    qa_source_save_io io = {0}; uint8_t magic[4] = {'Q','F','N','P'}; uint32_t version = 1;
    okay = okay && qa_source_save_writer(&io, qa_application_session(owner->frontend->application), error) &&
        qa_source_save_bytes(&io, magic, 4) && qa_source_save_u32(&io, &version);
    for (unsigned i = 0; okay && i < 2; ++i) { qa_bytes bytes = {children[i].data, children[i].size}; okay = blob(&io, &bytes); }
    okay = okay && qa_source_save_finish(&io, out); qa_source_save_dispose(&io);
    for (unsigned i = 0; i < 2; ++i) qa_buffer_free(children + i); return okay;
}
static bool restore(void *context, const qa_application_q3_client_context *client, qa_bytes bytes, qa_error *error)
{
    native_composition *owner = context;
    if (!idle(owner)) return frontend_fail(error, QA_ERROR_ARGUMENT, "Native composition import requires its empty genuine children");
    qa_source_save_io io = {0}; uint8_t magic[4]; uint32_t version; qa_bytes children[2] = {0};
    bool okay = qa_source_save_reader(&io, qa_application_session(owner->frontend->application), bytes, error) &&
        qa_source_save_bytes(&io, magic, 4) && !memcmp(magic, "QFNP", 4) && qa_source_save_u32(&io, &version) && version == 1;
    for (unsigned i = 0; okay && i < 2; ++i) okay = blob(&io, children + i);
    okay = okay && qa_source_save_finish(&io, NULL);
    if (okay) okay = owner->equipment.restore(owner->equipment.context, client, children[0], error) &&
        frontend_native_character_restore(owner->character, children[1], error);
    qa_source_save_dispose(&io);
    if (!okay && error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "Invalid native composed child continuation");
    return okay;
}
bool frontend_native_composition_create(void *context, frontend_native_q3 *row,
    frontend_native_q3_composition *out, qa_error *error)
{
    qa_frontend *frontend = context;
    if (!frontend || !out) return frontend_fail(error, QA_ERROR_ARGUMENT, "Native composition requires its actual frontend factory");
    native_composition *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining actual native composition");
    owner->frontend = frontend; owner->row = row;
    *out = (frontend_native_q3_composition){.context = owner, .idle = idle, .destroy = destroy,
        .rebind_ready = rebind_ready, .rebind = rebind, .checkpoint = checkpoint, .restore = restore,
        .begin_frame = begin, .end_frame = end, .before_render = before_render,
        .body_hidden = body_hidden, .body = body, .packet = packet,
        .actor_admitted = actor_admitted, .weapon_snapshot = weapon_snapshot,
        .view_weapon = view, .held_weapon = held, .weapon_warning = warning,
        .scene_cleared = cleared, .prepare_view = prepare, .submit_view = submit};
    return frontend_native_character_create(frontend, row, &owner->character, error) &&
        frontend_equipment_native_compose(frontend, row, &owner->equipment, error);
}
