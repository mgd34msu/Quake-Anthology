#include "equipment_runtime.h"
#include "guest_q3_private.h"
#include "guest_q3_gear_private.h"
#include "native_q3_wire_state.h"
#include "control_frame.h"
#include "qa/game_q3_clients.h"
#include <time.h>

typedef struct equipment_source {
    struct application_equipment_runtime *runtime;
    application_provider *provider;
    application_equipment_runtime_source view;
    qa_component component;
    qa_component_admission *admission;
    qa_equipment_source source;
    qa_launch_instance_lease *descriptor;
    qa_vfs_acquisition acquisition;
    qa_resource *artifact;
    uint64_t attack_sequence;
    int32_t milliseconds, frame;
    bool attached, prepared;
} equipment_source;
struct application_equipment_runtime {
    application_equipment_runtime_options options;
    equipment_source *sources;
    size_t count;
    application_equipment_events *events;
    bool restoring, retired, closing, destroying;
};

static bool provider_current(const application_equipment_runtime *runtime,
    const application_provider *wanted)
{
    if (!runtime || runtime->retired || !wanted) return false;
    for (application_provider *provider = runtime->options.application->live_providers;
         provider; provider = provider->next_live)
        if (provider == wanted)
            return provider->constructed && !provider->close_pending &&
                provider->application == runtime->options.application;
    return false;
}

static bool source_current(void *context)
{
    equipment_source *source = context;
    return source && source->descriptor && (!source->runtime->closing || source->runtime->destroying) &&
        provider_current(source->runtime, source->provider) &&
        provider_current(source->runtime, source->runtime->options.world_source) &&
        source->provider->owner == source->view.selected_owner &&
        source->provider->launch &&
        qa_sha256_equal(&source->provider->launch->identity, &source->view.descriptor->identity);
}
bool application_equipment_runtime_owner_current(const application_equipment_runtime *runtime,
    qa_actor_owner owner)
{
    if (!runtime || !owner || runtime->closing || runtime->restoring) return false;
    for (size_t i = 0; i < runtime->count; ++i)
        if (runtime->sources[i].view.selected_owner == owner)
            return source_current(&runtime->sources[i]);
    return false;
}

static bool target_record(equipment_source *source, const qa_actor_record *record)
{
    const qa_builtin_services *services = &source->runtime->options.services;
    return record && record->owner != source->view.gear_owner &&
        (!services->physics || !qa_actor_id_equal(record->id, services->physics->world_actor)) &&
        qa_world_body_storage_serial(services->world, record->id) != 0;
}

static size_t target_count(void *context)
{
    equipment_source *source = context;
    const qa_actor_registry *actors = qa_session_actors(source->runtime->options.services.session);
    size_t count = 0; uint32_t cursor = 0; const qa_actor_record *record;
    while (qa_actors_next(actors, &cursor, &record)) count += target_record(source, record);
    return count;
}

static bool target(void *context, size_t index, application_q3_gear_target *out, qa_error *error)
{
    equipment_source *source = context;
    application_equipment_runtime *runtime = source->runtime;
    qa_application *app = runtime->options.application;
    const qa_builtin_services *services = &runtime->options.services;
    uint32_t cursor = 0; const qa_actor_record *record; qa_actor_id actor = {0};
    while (qa_actors_next(qa_session_actors(services->session), &cursor, &record)) {
        if (!target_record(source, record)) continue;
        if (!index--) { actor = record->id; break; }
    }
    if (!actor.registry || !source_current(source))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Gear target lost its actual shared actor");
    application_q3_gear_target result = {.actor = actor,
        .userinfo = "\\name\\Player\\ip\\localhost\\model\\sarge/default\\handicap\\100"};
    if (!qa_world_body_read(services->world, actor, &result.body, error)) return false;
    qa_combat_state combat = {0}; qa_error optional = {0};
    if (!qa_combat_read_traits(services->combat, actor, &combat, &optional) &&
        optional.code != QA_ERROR_NOT_FOUND) { if (error) *error = optional; return false; }
    result.health = combat.health;
    qa_application_control_view control;
    qa_builtin_player_info player = {0};
    result.player = qa_application_control_read(app, actor, &control);
    if (result.player) {
        result.body.angles = control.view_angles; result.view_height = control.view_height;
        const char *team = qa_strings_cstr(qa_session_strings(services->session), combat.team);
        result.team = team && (!strcmp(team, "red") || !strcmp(team, "team:red")) ? 1 :
            team && (!strcmp(team, "blue") || !strcmp(team, "team:blue")) ? 2 : 0;
        if (services->player_info && services->player_info(services->context, actor, &player) && player.spectator)
            result.team = 3;
        application_provider *primary = runtime->options.world_source;
        if (primary->kind == APPLICATION_PROVIDER_Q3) {
            uint32_t slot;
            if (!qa_q3_native_client_slot(primary->state.q3, actor, &slot, error) ||
                !application_native_q3_wire_source_userinfo_read(primary, slot, &result.userinfo, error)) return false;
        } else {
            struct application_q3_guest *guest = q3g_engine(primary);
            if (guest && guest->game) {
                uint32_t slot;
                if (!qa_q3_host_actor_slot(guest->game->host, actor, &slot, error) || slot >= 64 ||
                    !qa_actor_id_equal(guest->clients[slot].actor, actor) || !guest->clients[slot].userinfo)
                    return application_fail(error, QA_ERROR_ARGUMENT, "Gear target lost its genuine GAME userinfo");
                result.userinfo = guest->clients[slot].userinfo;
            }
        }
    } else {
        qa_actor_collision collision; optional = (qa_error){0};
        if (qa_world_get_collision(services->world, actor, &collision, &optional))
            result.mover = collision.inline_model;
        else if (optional.code != QA_OK) { if (error) *error = optional; return false; }
    }
    if (!qa_actors_get(qa_session_actors(services->session), actor) || !source_current(source))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Gear target changed during its source reads");
    *out = result; return true;
}

static bool damage(void *context, const application_q3_gear_damage *hit, qa_error *error)
{
    equipment_source *source = context; application_equipment_runtime *runtime = source->runtime;
    qa_application *app = runtime->options.application;
    if (!source_current(source)) return application_fail(error, QA_ERROR_ARGUMENT, "Gear damage source retired");
    qa_item_id weapon;
    if (!qa_builtin_resource(&runtime->options.services, "q3:weapon/grapple", &weapon, error)) return false;
    qa_attack attack = {.time_ns = (uint64_t)source->milliseconds*1000000,
        .attacker = hit->attacker, .inflictor = hit->inflictor, .weapon = weapon,
        .weapon_provider = source->view.selected_owner,
        .cause = {.kind = QA_CAUSE_Q3, .source.q3 = {.means_of_death = hit->method, .flags = (uint32_t)hit->flags}}};
    application_provider *provider = application_provider_for(app, hit->target, QA_ROLE_COMBAT, "");
    if (provider) attack.combat_provider = provider->owner;
    provider = application_provider_for(app, hit->target, QA_ROLE_INVENTORY, "");
    if (provider) attack.inventory_provider = provider->owner;
    provider = application_provider_for(app, hit->target, QA_ROLE_MOVEMENT, "");
    if (provider) attack.movement_provider = provider->owner;
    if (!qa_attack_next(&source->attack_sequence, &attack, error)) return false;
    qa_damage_request request = {.attack = attack, .target = hit->target,
        .amount = (float)hit->amount, .knockback = (hit->flags & 4) ? 0 : (float)hit->amount,
        .direction = hit->direction, .point = hit->point};
    qa_damage_outcome outcome;
    if (!qa_combat_apply(runtime->options.services.combat, &request, &outcome, error)) return false;
    return source_current(source) || application_fail(error, QA_ERROR_ARGUMENT, "Gear damage source retired during combat");
}

static bool velocity(void *context, qa_actor_id actor, qa_vec3 value, qa_error *error)
{
    equipment_source *source = context; application_equipment_runtime *runtime = source->runtime;
    if (!qa_actors_get(qa_session_actors(runtime->options.services.session), actor)) return true;
    qa_body_state body;
    if (!source_current(source) || !qa_world_body_read(runtime->options.services.world, actor, &body, error)) return false;
    if (body.velocity.x == value.x && body.velocity.y == value.y && body.velocity.z == value.z) return true;
    body.velocity = value; body.ground = (qa_actor_id){0};
    if (!qa_world_body_write(runtime->options.services.world, actor, &body, error)) return false;
    if (!qa_actors_get(qa_session_actors(runtime->options.services.session), actor)) return true;
    if (!application_control_velocity(runtime->options.application, actor, value, error)) return false;
    qa_movement_state *active = application_control_frames_state_current(runtime->options.application, actor);
    if (active && !qa_movement_set_velocity(active, value, error)) return false;
    if (actor.slot < runtime->options.application->control_capacity) {
        application_control_record *control = &runtime->options.application->controls[actor.slot];
        if (control->active && qa_actor_id_equal(control->actor, actor)) control->ground = (qa_movement_ground){0};
    }
    return source_current(source) || application_fail(error, QA_ERROR_ARGUMENT, "Gear velocity source retired during its write");
}

static bool event(equipment_source *source, application_equipment_source_event_kind kind,
    int32_t index, const char *text, qa_error *error)
{
    application_equipment_runtime *runtime = source->runtime;
    application_equipment_source_event value = {.kind = kind, .provider = source->view.gear_owner,
        .selected_provider = source->view.selected_owner, .service_owner = source->view.service_owner,
        .time_ns = (uint64_t)source->milliseconds*1000000, .index = index, .text = text};
    if (!source_current(source)) return application_fail(error, QA_ERROR_ARGUMENT, "Gear event source retired");
    if (kind == APPLICATION_EQUIPMENT_SERVER_COMMAND && index >= 0) {
        application_q3_gear *gear = source->view.gear;
        if (!gear || index >= 64)
            return application_fail(error, QA_ERROR_ARGUMENT, "Gear command has no actual physical client");
        for (uint32_t i = 0; i < gear->capacity; ++i) {
            q3gear_binding *binding = &gear->bindings[i]; uint32_t slot;
            if (!binding->actor.registry || !binding->player || binding->retired) continue;
            if (!q3gear_slot(gear, binding->pointer, &slot, error)) return false;
            if (slot == (uint32_t)index) {
                if (!qa_actors_get(qa_session_actors(runtime->options.services.session), binding->actor))
                    return application_fail(error, QA_ERROR_ARGUMENT, "Gear command recipient retired during emission");
                value.recipient = binding->actor; break;
            }
        }
    }
    if (!application_equipment_events_publish(runtime->events, &value, error)) return false;
    return source_current(source) || application_fail(error, QA_ERROR_ARGUMENT, "Gear event source retired during publication");
}
static bool configstring(void *context, uint32_t index, const char *text, qa_error *error)
{ return event(context, APPLICATION_EQUIPMENT_CONFIGSTRING, (int32_t)index, text, error); }
static bool server_command(void *context, int32_t index, const char *text, qa_error *error)
{ return event(context, APPLICATION_EQUIPMENT_SERVER_COMMAND, index, text, error); }
static void print(void *context, const char *text)
{
    equipment_source *source = context;
    qa_command_context command = {.owner = source->view.gear_owner, .dialect = QA_CONSOLE_Q3, .origin = QA_COMMAND_SERVER};
    application_console_print(source->runtime->options.application, &command, text);
}

static bool admit(void *context, qa_actor_id actor, qa_error *error)
{
    equipment_source *source = context;
    if (!source->prepared) {
        application_equipment_runtime *runtime = source->runtime;
        qa_source_frame world; qa_clock_state clock;
        qa_session *session = runtime->options.services.session;
        if (!source->attached || !source_current(source))
            return application_fail(error, QA_ERROR_ARGUMENT, "Gear admission precedes its actual registered source");
        if (!qa_session_active_frame(session, runtime->options.world_source->owner, &world)) {
            if (!qa_session_clock(session, runtime->options.world_source->owner, &clock))
                return application_fail(error, QA_ERROR_ARGUMENT, "Gear admission lost its actual completed WORLD clock");
            world = clock.frame;
        }
        if (world.start_ns > UINT64_MAX-world.elapsed_ns ||
            (world.start_ns+world.elapsed_ns)/1000000 > INT32_MAX || world.number > INT32_MAX)
            return application_fail(error, QA_ERROR_ARGUMENT, "Gear admission WORLD clock exceeds its real Q3 ABI");
        source->milliseconds = (int32_t)((world.start_ns+world.elapsed_ns)/1000000);
        source->frame = (int32_t)world.number;
        if (!application_q3_gear_begin_frame(source->view.gear, source->milliseconds, source->frame, error)) return false;
        source->prepared = true;
    }
    return application_q3_gear_admit(source->view.gear, actor, error);
}
static bool fire(void *context, qa_actor_id actor, const qa_equipment_controls *controls, qa_error *error)
{ (void)controls; equipment_source *source = context; return application_q3_gear_fire(source->view.gear, actor, error); }
static bool release(void *context, qa_actor_id actor, bool force, qa_error *error)
{ equipment_source *source = context; return application_q3_gear_release(source->view.gear, actor, force, error); }
static bool pull(void *context, qa_actor_id actor, qa_vec3 forward, qa_vec3 *out, bool *apply, qa_error *error)
{ equipment_source *source = context; return application_q3_gear_pull(source->view.gear, actor, forward, out, apply, error); }
static bool saved_actor(void *context, qa_actor_id actor, qa_error *error)
{ equipment_source *source = context; application_q3_gear_view view; return application_q3_gear_read(source->view.gear, actor, &view, error); }
static bool command_frame(void *context, uint64_t now, uint64_t elapsed, qa_error *error)
{
    (void)now; (void)elapsed; equipment_source *source = context;
    return (source_current(source) && source->prepared) ||
        application_fail(error, QA_ERROR_ARGUMENT, "Gear command precedes its genuine WORLD frame preparation");
}

static bool prepare_frame(void *context, qa_session *session, const qa_source_frame *frame, qa_error *error)
{
    equipment_source *source = context; application_equipment_runtime *runtime = source->runtime;
    qa_source_frame world;
    if (session != runtime->options.services.session || !source_current(source) ||
        frame->provider != source->view.gear_owner ||
        !qa_session_active_frame(session, runtime->options.world_source->owner, &world) ||
        world.start_ns != frame->start_ns || world.elapsed_ns != frame->elapsed_ns || world.number != frame->number ||
        world.start_ns > UINT64_MAX-world.elapsed_ns ||
        (world.start_ns+world.elapsed_ns)/1000000 > INT32_MAX || world.number > INT32_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "Gear participant lost its actual admitted WORLD interval");
    source->milliseconds = (int32_t)((world.start_ns+world.elapsed_ns)/1000000);
    source->frame = (int32_t)world.number; source->prepared = false;
    if (!application_q3_gear_begin_frame(source->view.gear, source->milliseconds, source->frame, error)) return false;
    source->prepared = true; return true;
}

bool application_equipment_runtime_idle(const application_equipment_runtime *runtime)
{
    if (!runtime) return true;
    for (size_t i = 0; i < runtime->count; ++i)
        if (runtime->sources[i].view.gear && !application_q3_gear_idle(runtime->sources[i].view.gear)) return false;
    return true;
}

void application_equipment_runtime_abort_components(application_equipment_runtime *runtime)
{
    if (!runtime) return;
    for (size_t i = 0; i < runtime->count; ++i) {
        qa_component_admission_abort(runtime->sources[i].admission);
        runtime->sources[i].admission = NULL;
    }
}
bool application_equipment_runtime_prepare_components(application_equipment_runtime *runtime, qa_error *error)
{
    if (!runtime || runtime->retired || runtime->closing || !application_equipment_runtime_idle(runtime))
        return application_fail(error, QA_ERROR_ARGUMENT, "Gear participant admission requires its retained idle roster");
    for (size_t i = 0; i < runtime->count; ++i) {
        equipment_source *source = &runtime->sources[i];
        if (!source->view.gear) continue;
        if (source->attached || source->admission || !qa_session_prepare_component(
            runtime->options.services.session, &source->component, 0, &source->admission, error)) return false;
    }
    return true;
}
bool application_equipment_runtime_validate_components(application_equipment_runtime *runtime, qa_error *error)
{
    if (!runtime || runtime->retired || runtime->closing) return application_fail(error, QA_ERROR_ARGUMENT, "Gear roster has retired");
    for (size_t i = 0; i < runtime->count; ++i)
        if (runtime->sources[i].view.gear && (!runtime->sources[i].admission ||
            !qa_component_admission_validate(runtime->sources[i].admission, error))) return false;
    return true;
}
bool application_equipment_runtime_commit_components(application_equipment_runtime *runtime, qa_error *error)
{
    if (!application_equipment_runtime_validate_components(runtime, error)) return false;
    for (size_t i = 0; i < runtime->count; ++i) {
        equipment_source *source = &runtime->sources[i];
        if (!source->view.gear) continue;
        if (!qa_component_admission_commit(source->admission, error)) return false;
        source->admission = NULL; source->attached = true;
        if (!runtime->restoring) {
            qa_clock_state clock;
            if (!qa_session_clock(runtime->options.services.session, runtime->options.world_source->owner, &clock))
                return application_fail(error, QA_ERROR_ARGUMENT, "Gear registration precedes its real WORLD component");
            clock.frame.provider = source->view.gear_owner;
            if (!qa_session_restore_clock(runtime->options.services.session, source->view.gear_owner, &clock, error)) return false;
        }
    }
    return true;
}

size_t application_equipment_runtime_component_count(const application_equipment_runtime *runtime)
{
    size_t count = 0;
    if (runtime) for (size_t i = 0; i < runtime->count; ++i) count += runtime->sources[i].view.gear != NULL;
    return count;
}
bool application_equipment_runtime_component_at(const application_equipment_runtime *runtime,
    size_t index, const qa_component **out, qa_error *error)
{
    if (runtime && !runtime->closing && out) for (size_t i = 0; i < runtime->count; ++i)
        if (runtime->sources[i].view.gear && !index--) { *out = &runtime->sources[i].component; return true; }
    return application_fail(error, QA_ERROR_ARGUMENT, "Gear participant index is outside its actual roster");
}
size_t application_equipment_runtime_source_count(const application_equipment_runtime *runtime)
{ return runtime ? runtime->count : 0; }
bool application_equipment_runtime_source_at(const application_equipment_runtime *runtime,
    size_t index, application_equipment_runtime_source *out, qa_error *error)
{
    if (!runtime || runtime->closing || !out || index >= runtime->count || !runtime->sources[index].descriptor)
        return application_fail(error, QA_ERROR_ARGUMENT, "Equipment source index is outside its actual roster");
    *out = runtime->sources[index].view;
    out->weapon_item = runtime->sources[index].source.weapon_item;
    return true;
}
bool application_equipment_runtime_actor_released(application_equipment_runtime *runtime,
    qa_actor_record record, qa_error *error)
{
    if (!runtime) return true;
    bool okay = true; qa_error first = {0};
    for (size_t i = 0; i < runtime->count; ++i) {
        qa_error current = {0};
        if (runtime->sources[i].view.gear && !application_q3_gear_actor_released(
            runtime->sources[i].view.gear, record, &current)) {
            if (okay) first = current; okay = false;
        }
    }
    if (!okay && error) *error = first; return okay;
}

application_equipment_events *application_equipment_runtime_events(const application_equipment_runtime *runtime)
{ return runtime ? runtime->events : NULL; }
bool application_equipment_runtime_event_current(const application_equipment_runtime *runtime,
    const qa_application_equipment_event *event)
{
    if (!runtime || runtime->closing || !event) return false;
    for (size_t i = 0; i < runtime->count; ++i) {
        equipment_source *source = &runtime->sources[i];
        if (source->view.gear && source_current(source) &&
            event->provider == source->view.gear_owner && event->selected_provider == source->view.selected_owner &&
            event->service_owner == source->view.service_owner && source->milliseconds >= 0 &&
            event->time_ns <= (uint64_t)source->milliseconds*1000000) return true;
    }
    return false;
}

bool application_equipment_runtime_destroy(application_equipment_runtime *runtime, qa_error *error)
{
    if (!runtime) return true;
    if (!application_equipment_runtime_idle(runtime))
        return application_fail(error, QA_ERROR_ARGUMENT, "Equipment source destruction retains an operation");
    runtime->closing = true; runtime->destroying = true;
    application_equipment_runtime_abort_components(runtime);
    for (size_t i = 0; i < runtime->count; ++i) {
        equipment_source *source = &runtime->sources[i];
        if (source->view.gear) {
            if (!application_q3_gear_destroy(source->view.gear, error)) { runtime->destroying = false; return false; }
            source->view.gear = NULL;
        }
        if (source->attached) {
            bool removed = qa_session_remove(runtime->options.services.session, source->view.gear_owner, error);
            qa_clock_state retained;
            if (removed || !qa_session_clock(runtime->options.services.session, source->view.gear_owner, &retained))
                source->attached = false;
            if (!removed) {
                runtime->destroying = false; return false;
            }
        }
        qa_resource_release(source->artifact); source->artifact = NULL; source->view.artifact = NULL;
        qa_vfs_acquisition_dispose(&source->acquisition);
        qa_launch_instance_lease_release(source->descriptor); source->descriptor = NULL;
    }
    runtime->retired = true;
    application_equipment_events_destroy(runtime->events);
    qa_launch_snapshot_release(runtime->options.snapshot); free(runtime->sources); free(runtime); return true;
}

static bool runtime_header(qa_source_save_io *io)
{
    uint8_t magic[4] = {'Q','E','R','T'}; uint32_t version = 3;
    return qa_source_save_bytes(io, magic, sizeof(magic)) && !memcmp(magic, "QERT", sizeof(magic)) &&
        qa_source_save_u32(io, &version) && version == 3;
}
static bool saved_blob(qa_source_save_io *io, qa_bytes *bytes)
{
    size_t size = io->direction == QA_SOURCE_SAVE_READ ? 0 : bytes->size;
    if (!qa_source_save_count(io, &size, io->direction == QA_SOURCE_SAVE_READ ? io->input.size : SIZE_MAX)) return false;
    if (io->direction == QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io, (void *)bytes->data, size);
    if (io->offset > io->input.size || size > io->input.size-io->offset) return false;
    *bytes = (qa_bytes){io->input.data+io->offset, size}; io->offset += size; return true;
}
typedef struct saved_source {
    qa_actor_owner selected, owner;
    qa_item_id weapon_item;
    qa_string_id service_owner;
    qa_sha256_digest descriptor, image;
    uint64_t attack_sequence;
    int32_t milliseconds, frame;
    bool gear, prepared;
    qa_bytes entities, executor;
} saved_source;
static bool source_fields(qa_source_save_io *io, saved_source *saved)
{
    if (!qa_source_save_string(io, &saved->selected) || !saved->selected ||
        !qa_source_save_bytes(io, saved->descriptor.bytes, sizeof(saved->descriptor.bytes)) ||
        !qa_source_save_bool(io, &saved->gear)) return false;
    if (!saved->gear) return true;
    return qa_source_save_string(io, &saved->owner) && saved->owner &&
        qa_source_save_string(io, &saved->weapon_item) && saved->weapon_item &&
        qa_source_save_string(io, &saved->service_owner) && saved->service_owner &&
        qa_source_save_bytes(io, saved->image.bytes, sizeof(saved->image.bytes)) &&
        qa_source_save_u64(io, &saved->attack_sequence) &&
        qa_source_save_i32(io, &saved->milliseconds) && saved->milliseconds >= 0 &&
        qa_source_save_i32(io, &saved->frame) && saved->frame >= 0 &&
        qa_source_save_bool(io, &saved->prepared) &&
        saved_blob(io, &saved->entities) && saved_blob(io, &saved->executor) && saved->executor.size;
}
static bool decode_roster(application_equipment_runtime *runtime, qa_bytes bytes,
    saved_source **out, qa_bytes *events, qa_error *error)
{
    saved_source *rows = calloc(runtime->count ? runtime->count : 1, sizeof(*rows));
    if (!rows) return application_fail(error, QA_ERROR_MEMORY, "Decoding retained equipment source roster");
    qa_source_save_io io = {0}; size_t count = 0;
    bool okay = qa_source_save_reader(&io, runtime->options.services.session, bytes, error) && runtime_header(&io) &&
        qa_source_save_count(&io, &count, runtime->count) && count == runtime->count;
    for (size_t i = 0; okay && i < count; ++i) {
        equipment_source *source = &runtime->sources[i];
        okay = source_fields(&io, &rows[i]) && rows[i].selected == source->view.selected_owner &&
            qa_sha256_equal(&rows[i].descriptor, &source->view.descriptor->identity);
        if (okay && rows[i].gear) {
            const char *item = qa_strings_cstr(qa_session_strings(runtime->options.services.session), rows[i].weapon_item);
            okay = item && !strcmp(item, "q3:weapon/grapple");
        }
        for (size_t j = 0; okay && j < i; ++j)
            if (rows[i].selected == rows[j].selected || (rows[i].gear && rows[j].gear &&
                (rows[i].owner == rows[j].owner || rows[i].service_owner == rows[j].service_owner))) okay = false;
    }
    if (okay) okay = saved_blob(&io, events) && events->size && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!okay) { free(rows); return application_fail(error, QA_ERROR_FORMAT, "Equipment source roster differs from its selected descriptors"); }
    *out = rows; return true;
}
static bool saved_events_match(application_equipment_runtime *runtime,
    const saved_source *saved, qa_error *error)
{
    for (size_t i = 0; i < application_equipment_events_count(runtime->events); ++i) {
        qa_application_equipment_event event; bool found = false;
        if (!application_equipment_events_at(runtime->events, i, &event)) return false;
        for (size_t j = 0; j < runtime->count; ++j)
            if (saved[j].gear && event.provider == saved[j].owner &&
                event.selected_provider == saved[j].selected && event.service_owner == saved[j].service_owner &&
                event.time_ns <= (uint64_t)saved[j].milliseconds*1000000) { found = true; break; }
        if (!found) return application_fail(error, QA_ERROR_FORMAT, "Saved gear event leaves its actual source namespace or clock");
    }
    return true;
}

bool application_equipment_runtime_saved(qa_session *session, const qa_save_image *image,
    qa_bytes *out, qa_error *error)
{
    const qa_save_record *record = image ? qa_save_image_find(image, QA_SAVE_EQUIPMENT, "") : NULL;
    if (!session || !record || !out || record->owner.schema_version != 3 ||
        !record->owner.schema || strcmp(record->owner.schema, "qa.equipment"))
        return application_fail(error, QA_ERROR_FORMAT, "Equipment topology requires its actual save record");
    uint8_t magic[8] = {0}; uint32_t version = 0; bool present = false; qa_bytes source = {0};
    qa_source_save_io io = {0};
    bool okay = qa_source_save_reader(&io, session, record->payload, error) &&
        qa_source_save_bytes(&io, magic, sizeof(magic)) && !memcmp(magic, "QAEQUIP", sizeof(magic)) &&
        qa_source_save_u32(&io, &version) && version == 3 &&
        qa_source_save_bool(&io, &present) && present && saved_blob(&io, &source) && source.size;
    qa_source_save_dispose(&io);
    if (!okay) return application_fail(error, QA_ERROR_FORMAT, "Equipment save lacks its genuine runtime topology");
    *out = source; return true;
}

static int32_t calendar(void *context, qa_q3_host_calendar *out)
{
    (void)context; time_t now = time(NULL); const struct tm *value = localtime(&now);
    if (!value) { if (out) *out = (qa_q3_host_calendar){0}; return -1; }
    if (out) *out = (qa_q3_host_calendar){value->tm_sec, value->tm_min, value->tm_hour,
        value->tm_mday, value->tm_mon, value->tm_year, value->tm_wday, value->tm_yday, value->tm_isdst};
    uint32_t bits = (uint32_t)now; int32_t result; memcpy(&result, &bits, sizeof(result)); return result;
}
static bool names(equipment_source *source, const saved_source *saved, qa_error *error)
{
    qa_strings *strings = qa_session_strings(source->runtime->options.services.session);
    char digest[65], name[160], service[192]; qa_sha256_hex(&source->view.descriptor->identity, digest);
    if (saved) {
        source->view.gear_owner = saved->owner; source->view.service_owner = saved->service_owner;
        const char *text = qa_strings_cstr(strings, saved->owner);
        if (!text) return application_fail(error, QA_ERROR_FORMAT, "Saved gear namespace is absent");
        snprintf(service, sizeof(service), "%s:services", text);
        if (qa_strings_find(strings, (qa_bytes){(const uint8_t *)service, strlen(service)}) != saved->service_owner)
            return application_fail(error, QA_ERROR_FORMAT, "Saved gear services differ from its true namespace");
        const char *suffix = strrchr(text, ':'); uint64_t generation = 0;
        bool valid = suffix && suffix[1] >= '1' && suffix[1] <= '9';
        for (const char *digit = suffix ? suffix+1 : ""; valid && *digit; ++digit) {
            if (*digit < '0' || *digit > '9' || generation > (UINT64_MAX-(uint64_t)(*digit-'0'))/10) valid = false;
            else generation = generation*10+(uint64_t)(*digit-'0');
        }
        if (valid) {
            snprintf(name, sizeof(name), "q3-gear:%u:%s:%llu", source->view.selected_owner,
                digest, (unsigned long long)generation);
            if (!strcmp(text, name)) return true;
        }
        return application_fail(error, QA_ERROR_FORMAT, "Saved gear namespace differs from its immutable selected source");
    }
    for (uint64_t generation = 1; generation; ++generation) {
        snprintf(name, sizeof(name), "q3-gear:%u:%s:%llu", source->view.selected_owner,
            digest, (unsigned long long)generation);
        if (qa_strings_find(strings, (qa_bytes){(const uint8_t *)name, strlen(name)})) continue;
        snprintf(service, sizeof(service), "%s:services", name);
        return qa_strings_intern_cstr(strings, name, &source->view.gear_owner, error) &&
            qa_strings_intern_cstr(strings, service, &source->view.service_owner, error);
    }
    return application_fail(error, QA_ERROR_MEMORY, "Gear namespace generation exhausted");
}

static bool create_gear(equipment_source *source, const saved_source *saved, qa_error *error)
{
    application_equipment_runtime *runtime = source->runtime;
    struct application_q3_guest *engine = q3g_engine(source->provider);
    q3g_artifact *artifact = engine && engine->game ? engine->game->artifact : NULL;
    if (!artifact || !artifact->qvm || artifact->kind != QA_QVM_GAME || !artifact->image ||
        !artifact->resource || !artifact->view || !artifact->path || artifact->abi != QA_QVM_Q3_MODERN ||
        !qa_vfs_acquisition_retained(artifact->view, &artifact->acquisition, error))
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Selected gear lacks its genuine retained original GAME QVM");
    application_q3_grapple_profile *profile = NULL;
    if (!application_q3_grapple_profile_create(artifact->image, artifact->kind, artifact->abi,
        artifact->path, &profile, error)) return false;
    if (!profile) return application_fail(error, QA_ERROR_UNSUPPORTED, "Selected GAME artifact declares no supported grapple profile");
    bool okay = (!saved || (saved->gear && qa_sha256_equal(&saved->image, qa_qvm_image_digest(artifact->image)))) &&
        names(source, saved, error) && q3g_acquisition_copy(&artifact->acquisition, &source->acquisition, error);
    if (okay) {
        source->artifact = artifact->resource; source->view.artifact = artifact->resource; qa_resource_retain(artifact->resource);
        source->view.acquisition = &source->acquisition; source->view.content = artifact->view;
        source->view.definition = application_q3_grapple_profile_definition(profile);
        qa_item_id weapon_item = saved ? saved->weapon_item : 0;
        if (!saved) okay = qa_builtin_resource(&runtime->options.services,
            "q3:weapon/grapple", &weapon_item, error);
        source->component = (qa_component){.owner = source->view.gear_owner,
            .clock = runtime->options.world_source->component.clock, .state = source, .prepare_frame = prepare_frame};
        application_q3_gear_options options = {.profile = profile,
            .host = {.role = QA_QVM_GAME, .abi = QA_QVM_Q3_MODERN,
                .session = runtime->options.services.session, .world = runtime->options.services.world,
                .owner = source->view.gear_owner, .service_owner = source->view.service_owner,
                .engine_cvars = runtime->options.application->cvars, .mounts = artifact->view,
                .command_context = {.owner = source->view.gear_owner, .dialect = QA_CONSOLE_Q3, .origin = QA_COMMAND_SERVER},
                .common = {.context = source, .print = print, .calendar = calendar},
                .server = {.context = source, .send_command = server_command},
                .entity_text = saved ? saved->entities : runtime->options.entity_text},
            .services = runtime->options.services, .context = source, .current = source_current,
            .target_count = target_count, .target = target, .damage = damage, .velocity = velocity,
            .configstring = configstring};
        if (okay) okay = application_q3_gear_create(&options, saved != NULL, &source->view.gear, error);
        if (okay) source->source = (qa_equipment_source){.owner = source->view.selected_owner,
            .weapon_item = weapon_item,
            .context = source, .current = source_current, .admit = admit, .frame = command_frame,
            .fire = fire, .release = release, .pull = pull, .saved_actor = saved_actor};
        if (okay && saved) {
            source->attack_sequence = saved->attack_sequence; source->milliseconds = saved->milliseconds;
            source->frame = saved->frame; source->prepared = saved->prepared;
        }
    }
    application_q3_grapple_profile_destroy(profile);
    if (!okay && (!error || error->code == QA_OK))
        application_fail(error, QA_ERROR_FORMAT, "Selected gear saved artifact differs from its genuine source");
    return okay;
}

static bool source_resolve(void *context, qa_actor_owner owner, qa_equipment_source *out, qa_error *error)
{
    application_equipment_runtime *runtime = context;
    for (size_t i = 0; runtime && out && i < runtime->count; ++i)
        if (runtime->sources[i].view.selected_owner == owner) {
            if (!source_current(&runtime->sources[i]))
                return application_fail(error, QA_ERROR_ARGUMENT, "Selected equipment source has retired");
            *out = runtime->sources[i].source; return true;
        }
    return application_fail(error, QA_ERROR_NOT_FOUND, "Selected equipment source is outside its retained roster");
}
static bool source_idle(const void *context) { return application_equipment_runtime_idle(context); }
static bool source_destroy(void *context, qa_error *error) { return application_equipment_runtime_destroy(context, error); }

static bool source_capture(void *context, qa_buffer *out, qa_error *error)
{
    application_equipment_runtime *runtime = context; qa_source_save_io io = {0};
    if (!runtime || runtime->restoring || runtime->closing || !out || !application_equipment_runtime_idle(runtime))
        return application_fail(error, QA_ERROR_ARGUMENT, "Equipment source capture requires its completed roster");
    size_t count = runtime->count;
    bool okay = qa_source_save_writer(&io, runtime->options.services.session, error) &&
        runtime_header(&io) && qa_source_save_count(&io, &count, SIZE_MAX);
    for (size_t i = 0; i < count && okay; ++i) {
        equipment_source *source = &runtime->sources[i]; qa_buffer executor = {0};
        saved_source saved = {.selected = source->view.selected_owner, .descriptor = source->view.descriptor->identity,
            .gear = source->view.gear != NULL};
        okay = source_current(source);
        if (okay && saved.gear) {
            okay = source->attached && application_q3_gear_checkpoint(source->view.gear, &executor, error);
            saved.owner = source->view.gear_owner; saved.service_owner = source->view.service_owner;
            saved.weapon_item = source->source.weapon_item;
            saved.image = *qa_qvm_image_digest(source->view.gear->image); saved.attack_sequence = source->attack_sequence;
            saved.milliseconds = source->milliseconds; saved.frame = source->frame; saved.prepared = source->prepared;
            saved.entities = (qa_bytes){source->view.gear->entities.data, source->view.gear->entities.size};
            saved.executor = (qa_bytes){executor.data, executor.size};
            okay = okay && saved.milliseconds == source->view.gear->milliseconds && saved.frame == source->view.gear->frame;
        }
        if (okay) okay = source_fields(&io, &saved);
        qa_buffer_free(&executor);
    }
    qa_buffer events = {0};
    for (size_t i = 0; okay && i < application_equipment_events_count(runtime->events); ++i) {
        qa_application_equipment_event event;
        okay = application_equipment_events_at(runtime->events, i, &event) &&
            application_equipment_runtime_event_current(runtime, &event);
    }
    if (okay) okay = application_equipment_events_capture(runtime->events, &events, error);
    qa_bytes event_bytes = {events.data, events.size};
    if (okay) okay = saved_blob(&io, &event_bytes) && qa_source_save_finish(&io, out);
    qa_buffer_free(&events);
    qa_source_save_dispose(&io);
    if (!okay && (!error || error->code == QA_OK))
        application_fail(error, QA_ERROR_FORMAT, "Equipment source capture lost its actual participant");
    return okay;
}
static bool console_identity(void *context, qa_console_save_identity kind, uint64_t saved,
    uint64_t *restored, qa_error *error)
{
    equipment_source *source = context;
    if (!saved || (kind == QA_CONSOLE_SAVE_OWNER &&
        (saved == source->view.gear_owner || saved == source->view.service_owner))) {
        *restored = saved; return true;
    }
    return application_fail(error, QA_ERROR_FORMAT, "Gear console identity is outside its actual private owner");
}
static bool console_context(void *context, uint64_t captured_registry,
    const qa_command_context *saved, qa_command_context *restored, qa_error *error)
{
    (void)captured_registry; equipment_source *source = context;
    if (saved->owner != source->view.gear_owner || saved->dialect != QA_CONSOLE_Q3 ||
        saved->origin != QA_COMMAND_SERVER || saved->session || saved->client || saved->seat ||
        saved->registry || saved->generation || saved->actor.registry || saved->actor.slot ||
        saved->actor.generation || !source_current(source))
        return application_fail(error, QA_ERROR_FORMAT, "Gear console command belongs to another source lifetime");
    *restored = *saved; return true;
}
static bool source_restore(void *context, qa_bytes bytes, qa_error *error)
{
    application_equipment_runtime *runtime = context; saved_source *saved = NULL; qa_bytes events = {0};
    if (!runtime || !runtime->restoring || runtime->closing || !application_equipment_runtime_idle(runtime) ||
        !decode_roster(runtime, bytes, &saved, &events, error)) return false;
    qa_buffer current_events = {0};
    bool okay = application_equipment_events_capture(runtime->events, &current_events, error) &&
        current_events.size == events.size && (!events.size || !memcmp(current_events.data, events.data, events.size)) &&
        saved_events_match(runtime, saved, error);
    qa_buffer_free(&current_events);
    for (size_t i = 0; i < runtime->count && okay; ++i) {
        equipment_source *source = &runtime->sources[i]; application_q3_gear *gear = source->view.gear;
        okay = saved[i].gear == (gear != NULL);
        if (!okay || !gear) continue;
        okay = source->attached && source->view.gear_owner == saved[i].owner &&
            source->source.weapon_item == saved[i].weapon_item &&
            source->view.service_owner == saved[i].service_owner &&
            qa_sha256_equal(&saved[i].image, qa_qvm_image_digest(gear->image)) &&
            source->attack_sequence == saved[i].attack_sequence &&
            source->milliseconds == saved[i].milliseconds && source->frame == saved[i].frame &&
            source->prepared == saved[i].prepared && gear->entities.size == saved[i].entities.size &&
            (!gear->entities.size || !memcmp(gear->entities.data, saved[i].entities.data, gear->entities.size));
    }
    for (size_t i = 0; i < runtime->count && okay; ++i) {
        application_q3_gear *gear = runtime->sources[i].view.gear;
        if (!gear) continue;
        qa_console_save_resolvers resolvers = {.context = &runtime->sources[i],
            .identity = console_identity, .command_context = console_context};
        okay = application_q3_gear_restore(gear, saved[i].executor, &resolvers, error) &&
            gear->milliseconds == saved[i].milliseconds && gear->frame == saved[i].frame &&
            application_q3_gear_finish_restore(gear, error);
    }
    free(saved); if (okay) runtime->restoring = false;
    if (!okay && (!error || error->code == QA_OK))
        application_fail(error, QA_ERROR_FORMAT, "Equipment source import differs from its actual prepared topology");
    return okay;
}
void application_equipment_runtime_bind(application_equipment_runtime *runtime, qa_equipment_options *out)
{
    if (!runtime || !out) return;
    out->source = source_resolve; out->source_context = runtime; out->source_idle = source_idle;
    out->source_destroy = source_destroy; out->source_capture = source_capture; out->source_restore = source_restore;
}

static bool selected(const char *name, const char *wanted)
{ return name && *name && wanted && !strcmp(name, wanted); }
static bool wanted_source(const qa_launch_choices *choices, const char *name, bool *grapple)
{
    bool wanted = false; *grapple = false;
    for (size_t i = 0; i < choices->equipment_count; ++i) {
        const qa_launch_equipment *row = &choices->equipment[i];
        bool is_grapple = row->selection.grapple != QA_GRAPPLE_DISABLED &&
            selected(row->grapple_source && *row->grapple_source ? row->grapple_source : row->instance, name);
        wanted |= selected(row->instance, name) || is_grapple ||
            (row->selection.grenades.enabled && selected(row->grenade_source && *row->grenade_source ? row->grenade_source : row->instance, name));
        *grapple |= is_grapple && row->selection.grapple == QA_GRAPPLE_Q3;
    }
    for (size_t i = 0; i < choices->binding_count; ++i)
        wanted |= choices->bindings[i].role == QA_ROLE_EQUIPMENT && selected(choices->bindings[i].instance, name);
    return wanted;
}
static bool selected_provider(const application_equipment_runtime *runtime,
    const application_equipment_runtime_options *options, const char *name, qa_error *error)
{
    application_provider *found = NULL;
    for (size_t i = 0; name && *name && i < options->provider_count; ++i) {
        application_provider *provider = options->providers[i];
        if (!provider || !provider_current(runtime, provider) || !provider->launch ||
            strcmp(provider->launch->selection.instance, name)) continue;
        if (found) return application_fail(error, QA_ERROR_ARGUMENT, "Equipment selection has competing actual provider owners");
        found = provider;
    }
    return found != NULL || application_fail(error, QA_ERROR_NOT_FOUND, "Equipment selection has no actual constructed provider");
}
static bool qualify_roster(const application_equipment_runtime *runtime,
    const application_equipment_runtime_options *options, const qa_launch_choices *choices, qa_error *error)
{
    if (!choices || !provider_current(runtime, options->world_source))
        return application_fail(error, QA_ERROR_ARGUMENT, "Equipment publication lost its actual WORLD owner");
    for (size_t i = 0; i < choices->equipment_count; ++i) {
        const qa_launch_equipment *row = &choices->equipment[i];
        if (!selected_provider(runtime, options, row->instance, error) ||
            (row->selection.grapple != QA_GRAPPLE_DISABLED && !selected_provider(runtime, options,
                row->grapple_source && *row->grapple_source ? row->grapple_source : row->instance, error)) ||
            (row->selection.grenades.enabled && !selected_provider(runtime, options,
                row->grenade_source && *row->grenade_source ? row->grenade_source : row->instance, error))) return false;
    }
    for (size_t i = 0; i < choices->binding_count; ++i)
        if (choices->bindings[i].role == QA_ROLE_EQUIPMENT &&
            !selected_provider(runtime, options, choices->bindings[i].instance, error)) return false;
    return true;
}
bool application_equipment_runtime_create(const application_equipment_runtime_options *options,
    qa_bytes saved_roster, application_equipment_runtime **out, qa_error *error)
{
    if (!options || !out || !options->application || !options->snapshot || !options->world_source ||
        !options->services.session || options->services.session != options->application->session ||
        !options->services.world || (options->provider_count && !options->providers) ||
        (saved_roster.size && !saved_roster.data) || !qa_builtin_services_validate(&options->services, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Equipment runtime lacks its actual publication owners");
    application_equipment_runtime *runtime = calloc(1, sizeof(*runtime));
    if (!runtime) return application_fail(error, QA_ERROR_MEMORY, "Allocating actual equipment source roster");
    runtime->options = *options; runtime->options.providers = NULL; runtime->options.provider_count = 0;
    runtime->restoring = saved_roster.size != 0; qa_launch_snapshot_retain(options->snapshot);
    const qa_launch_choices *choices = qa_launch_snapshot_choices(options->snapshot);
    bool okay = application_equipment_events_create(options->services.session, &runtime->events, error) &&
        qualify_roster(runtime, options, choices, error);
    for (size_t i = 0; okay && i < options->provider_count; ++i) {
        application_provider *provider = options->providers[i]; bool grapple;
        if (provider && provider->launch && wanted_source(choices, provider->launch->selection.instance, &grapple)) ++runtime->count;
    }
    if (okay) runtime->sources = calloc(runtime->count ? runtime->count : 1, sizeof(*runtime->sources));
    if (okay && !runtime->sources) {
        runtime->count = 0; okay = application_fail(error, QA_ERROR_MEMORY, "Allocating retained equipment source entries");
    }
    size_t used = 0;
    for (size_t i = 0; okay && i < options->provider_count; ++i) {
        application_provider *provider = options->providers[i]; bool grapple;
        if (!provider || !provider->launch || !wanted_source(choices, provider->launch->selection.instance, &grapple)) continue;
        equipment_source *source = &runtime->sources[used++]; source->runtime = runtime; source->provider = provider;
        okay = provider_current(runtime, provider) && qa_launch_instance_retain_metadata(provider->launch, &source->descriptor, error);
        if (okay) {
            source->view.selected_owner = provider->owner;
            source->view.descriptor = qa_launch_instance_lease_view(source->descriptor);
            source->source = (qa_equipment_source){.owner = provider->owner, .context = source, .current = source_current};
            switch (provider->kind) {
            case APPLICATION_PROVIDER_Q1: source->source.q1 = provider->state.q1; break;
            case APPLICATION_PROVIDER_Q2: source->source.q2 = provider->state.q2; break;
            case APPLICATION_PROVIDER_Q3:
                source->source.q3 = provider->state.q3;
                source->source.q3_product = provider->product && !strcmp(provider->product->campaign, "missionpack") ? QA_Q3_TEAM_ARENA : QA_Q3_ARENA;
                break;
            case APPLICATION_PROVIDER_QC: case APPLICATION_PROVIDER_QVM: case APPLICATION_PROVIDER_NATIVE: break;
            }
        }
    }
    saved_source *saved = NULL; qa_bytes events = {0};
    if (okay && runtime->restoring) okay = decode_roster(runtime, saved_roster, &saved, &events, error) &&
        application_equipment_events_restore(runtime->events, events, error) && saved_events_match(runtime, saved, error);
    for (size_t i = 0; okay && i < runtime->count; ++i) {
        equipment_source *source = &runtime->sources[i]; bool grapple;
        wanted_source(choices, source->view.descriptor->selection.instance, &grapple);
        bool needs_gear = grapple && !source->source.q3;
        if (runtime->restoring && saved[i].gear != needs_gear) { okay = false; break; }
        if (!needs_gear) continue;
        okay = create_gear(source, runtime->restoring ? &saved[i] : NULL, error);
    }
    free(saved);
    /* All immutable artifacts and constructor owners are qualified before the
     * first genuine private GAME Init; no target actor is admitted here. */
    for (size_t i = 0; okay && !runtime->restoring && i < runtime->count; ++i)
        if (runtime->sources[i].view.gear) {
            struct application_q3_guest *engine = q3g_engine(runtime->sources[i].provider);
            okay = application_q3_gear_initialize(runtime->sources[i].view.gear, engine->random_seed, error);
        }
    if (!okay) {
        qa_error cleanup = {0};
        if (!application_equipment_runtime_destroy(runtime, &cleanup)) {
            /* A genuine refusal retains the owner for the caller's checked
             * publication cleanup; never free callbacks still held by a VM. */
            *out = runtime;
        }
        if (error && error->code != QA_OK) return false;
        return application_fail(error, QA_ERROR_FORMAT, "Cannot prepare the actual selected equipment source roster");
    }
    *out = runtime; return true;
}
