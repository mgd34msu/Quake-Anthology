#include "source_trajectory.h"
#include "source_trajectory_native_builtin.h"
#include "internal.h"
#include "map_players_private.h"
#include "qa/binary.h"
#include "qa/console_cvars_prepare.h"
#include "qa/json.h"
#include "qa/native_host.h"
#include "qa/native_observe.h"
#include "qa/source_save.h"
#include "qa/text.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum { NATIVE_TRAJECTORY_CLIENT = 1, NATIVE_TRAJECTORY_TARGET = 2,
    NATIVE_TRAJECTORY_PROJECTILE = 4, NATIVE_TRAJECTORY_RETIRED = 8,
    NATIVE_TRAJECTORY_ACTIVATED = 16, NATIVE_TRAJECTORY_CALLS = 64 };

typedef struct native_trajectory_registration {
    uint32_t bytes, name, tag, callback;
} native_trajectory_registration;

typedef struct native_trajectory_entry {
    uint32_t rva, registration_rva, tag;
    const char *name;
    native_trajectory_registration registration;
    qa_native_address address;
} native_trajectory_entry;

typedef struct native_trajectory_command {
    const char **arguments;
    size_t count;
    const char *tail;
} native_trajectory_command;

typedef struct native_trajectory_slot { uint8_t flags; int32_t generation; } native_trajectory_slot;

typedef struct trajectory_native {
    qa_application *application;
    const qa_launch_instance *launch;
    qa_native_module *module;
    qa_native_host *host;
    qa_native_instance *instance;
    qa_native_entry_observer *free_observer;
    qa_builtin_services services;
    qa_actor_owner owner;
    qa_cvars *cvars;
    qa_cvars_value_scope *provisioning_scope;
    qa_arena definitions;
    qa_buffer entities;
    const char **initialization_classes;
    size_t initialization_class_count;
    qa_q2_config_layout config_layout;
    qa_string_id *configstrings;
    qa_id_index resources;
    native_trajectory_entry allocate, free_entity, touch, equipped;
    native_trajectory_entry *equip, *fire;
    size_t equip_count, fire_count;
    native_trajectory_command *equipment, ammunition;
    size_t equipment_count;
    qa_cvar_assignment *initial_cvars, *provisioning_cvars;
    size_t initial_cvar_count, provisioning_cvar_count;
    native_trajectory_registration think_registration;
    uint32_t think_tag, time_rva;
    qa_native_address time_address;
    uint32_t entity_bytes, origin, angles, velocity, client, owner_pointer, view_height;
    uint32_t generation, nextthink, think_callback, think_record, touch_callback;
    uint32_t client_bytes, weapon, view_angles, forward;
    uint32_t equipped_bytes, equipped_callback;
    native_trajectory_slot *slots;
    uint32_t capacity, maximum_clients;
    uint64_t time_ns, refreshed_time, interval_ns;
    bool refreshed_once, capturing, releasing, restoring;
    uint32_t captured_slot;
    unsigned captured_count;
    qa_native_address captured_shooter;
    const qa_builtin_weapon_launch *launching;
    const native_trajectory_command *command;
} trajectory_native;

static bool native_word(trajectory_native *source, qa_native_address address,
    uint64_t *value, unsigned width, qa_error *error)
{
    uint8_t bytes[8];
    if (!qa_native_read(source->instance, address, bytes, width, error)) return false;
    *value = width == 8 ? qa_load_u64le(bytes) : qa_load_u32le(bytes);
    return true;
}

static bool native_write_word(trajectory_native *source, qa_native_address address,
    uint64_t value, unsigned width, qa_error *error)
{
    uint8_t bytes[8];
    if (width == 8) qa_store_u64le(bytes, value); else qa_store_u32le(bytes, (uint32_t)value);
    return qa_native_write(source->instance, address, (qa_bytes){bytes, width}, error);
}

static bool native_vector(trajectory_native *source, qa_native_address address,
    qa_vec3 *value, qa_error *error)
{
    uint8_t bytes[12];
    if (!qa_native_read(source->instance, address, bytes, sizeof(bytes), error)) return false;
    *value = qa_v3(qa_load_f32le(bytes), qa_load_f32le(bytes + 4), qa_load_f32le(bytes + 8));
    return true;
}

static bool native_write_vector(trajectory_native *source, qa_native_address address,
    qa_vec3 value, qa_error *error)
{
    uint8_t bytes[12]; qa_store_f32le(bytes, value.x); qa_store_f32le(bytes + 4, value.y);
    qa_store_f32le(bytes + 8, value.z);
    return qa_native_write(source->instance, address, (qa_bytes){bytes, sizeof(bytes)}, error);
}

static qa_native_signature native_signature(qa_native_type *parameters, size_t count,
    qa_native_value_type result)
{
    return (qa_native_signature){.abi = QA_NATIVE_ABI_MICROSOFT_X64,
        .parameters = parameters, .parameter_count = count, .result = {.kind = result, .count = 1}};
}

static bool native_invoke_entity(trajectory_native *source, qa_native_address entry,
    qa_native_address entity, qa_error *error)
{
    qa_native_type parameter = {.kind = QA_NATIVE_ADDRESS, .count = 1};
    qa_native_signature signature = native_signature(&parameter, 1, QA_NATIVE_VOID);
    qa_native_value argument = {.type = QA_NATIVE_ADDRESS, .as.address = entity};
    return qa_native_invoke(source->instance, entry, &signature, &argument, 1, NULL, error);
}

static bool native_registration_check(trajectory_native *source, qa_native_address record,
    const native_trajectory_registration *layout, uint32_t tag, qa_native_address callback,
    const char *name, qa_error *error)
{
    uint64_t actual_tag, actual_callback, text;
    if (!qa_native_range_check(source->instance, record, layout->bytes, QA_NATIVE_MEMORY_READ, error) ||
        !native_word(source, record + layout->tag, &actual_tag, 4, error) ||
        !native_word(source, record + layout->callback, &actual_callback, 8, error)) return false;
    if (actual_tag != tag || actual_callback != callback)
        return application_fail(error, QA_ERROR_FORMAT, "Native trajectory typed callback registration differs");
    if (name) {
        const char *actual;
        if (!native_word(source, record + layout->name, &text, 8, error) || !text ||
            !qa_native_host_string_span(source->host, text, &actual, error)) return false;
        if (strcmp(actual, name))
            return application_fail(error, QA_ERROR_FORMAT, "Native trajectory callback registration name differs");
    }
    return true;
}

static bool native_entry_bind(trajectory_native *source, native_trajectory_entry *entry, qa_error *error)
{
    if (!qa_native_rva(source->instance, entry->rva, 1, &entry->address, error) ||
        !qa_native_range_check(source->instance, entry->address, 1, QA_NATIVE_MEMORY_EXECUTE, error)) return false;
    if (entry->registration_rva) {
        qa_native_address registration;
        if (!qa_native_rva(source->instance, entry->registration_rva, entry->registration.bytes, &registration, error) ||
            !native_registration_check(source, registration, &entry->registration,
                entry->tag, entry->address, entry->name, error)) return false;
    }
    return true;
}

static bool native_addresses(trajectory_native *source, qa_error *error)
{
    if (!native_entry_bind(source, &source->allocate, error) ||
        !native_entry_bind(source, &source->free_entity, error) ||
        !native_entry_bind(source, &source->touch, error) ||
        !native_entry_bind(source, &source->equipped, error) ||
        !qa_native_rva(source->instance, source->time_rva, 8, &source->time_address, error) ||
        !qa_native_range_check(source->instance, source->time_address, 8, QA_NATIVE_MEMORY_WRITE, error)) return false;
    for (size_t i = 0; i < source->equip_count; ++i)
        if (!native_entry_bind(source, source->equip + i, error)) return false;
    for (size_t i = 0; i < source->fire_count; ++i)
        if (!native_entry_bind(source, source->fire + i, error)) return false;
    return true;
}

static bool native_capacity(trajectory_native *source, qa_error *error)
{
    qa_native_entity_table table;
    if (!qa_native_entity_table_refresh(source->instance, &table, error)) return false;
    if (table.stride < source->entity_bytes)
        return application_fail(error, QA_ERROR_FORMAT, "Native trajectory entity layout exceeds its source rows");
    if (table.capacity > source->capacity) {
        if (source->capacity && !source->restoring)
            return application_fail(error, QA_ERROR_CAPACITY, "Native trajectory changed its admitted source capacity");
        native_trajectory_slot *slots = calloc(table.capacity, sizeof(*slots));
        if (!slots) return application_fail(error, QA_ERROR_MEMORY, "Retaining native trajectory slot state");
        if (source->capacity) memcpy(slots, source->slots, source->capacity * sizeof(*slots));
        free(source->slots); source->slots = slots; source->capacity = table.capacity;
    }
    return true;
}

static bool native_generation(trajectory_native *source, uint32_t slot,
    int32_t *generation, qa_error *error)
{
    qa_native_address address; uint64_t value;
    if (!qa_native_entity_address(source->instance, slot, &address, error) ||
        !native_word(source, address + source->generation, &value, 4, error)) return false;
    *generation = qa_i32_from_u32((uint32_t)value); return true;
}

static bool native_remember(trajectory_native *source, uint32_t slot, qa_actor_id actor,
    uint8_t flags, qa_error *error)
{
    if (!native_generation(source, slot, &source->slots[slot].generation, error)) return false;
    qa_native_slot_binding binding = {.kind = QA_NATIVE_SLOT_BORROWED, .slot = slot,
        .source_slot = slot, .owner = source->owner, .actor = actor};
    if (!qa_native_bind_slot(source->instance, &binding, error)) return false;
    source->slots[slot].flags = flags; return true;
}

static bool native_time(trajectory_native *source, uint64_t time_ns, qa_error *error)
{
    if (time_ns < source->time_ns || time_ns / UINT64_C(1000000) > INT64_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native trajectory time exceeds its source clock");
    source->time_ns = time_ns;
    return native_write_word(source, source->time_address, time_ns / UINT64_C(1000000), 8, error);
}

static bool native_body(trajectory_native *source, uint32_t slot, const qa_body_state *body, qa_error *error)
{
    return qa_native_host_source_body_write(source->host, slot, source->velocity, 0, body, error);
}

static bool native_trajectory(trajectory_native *source, uint32_t slot,
    qa_builtin_trajectory_update *out, qa_error *error)
{
    qa_native_address address;
    return qa_native_entity_address(source->instance, slot, &address, error) &&
        native_vector(source, address + source->origin, &out->origin, error) &&
        native_vector(source, address + source->angles, &out->angles, error) &&
        native_vector(source, address + source->velocity, &out->velocity, error);
}

static bool native_config_get(void *context, int32_t index, qa_string_id *out, qa_error *error)
{
    trajectory_native *source = context;
    if (index < 0 || (uint32_t)index >= source->config_layout.max_configs)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native trajectory configstring is outside its ABI");
    *out = source->configstrings[index]; return true;
}

static bool native_config_set(void *context, int32_t index, const char *text, qa_error *error)
{
    trajectory_native *source = context;
    if (index < 0 || (uint32_t)index >= source->config_layout.max_configs)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native trajectory configstring is outside its ABI");
    qa_string_id id = 0;
    if (*text && !qa_strings_intern_cstr(qa_session_strings(source->application->session), text, &id, error)) return false;
    qa_string_id previous = source->configstrings[index];
    if (previous && previous != id) {
        uint32_t bases[] = {source->config_layout.models, source->config_layout.sounds, source->config_layout.images};
        uint32_t counts[] = {source->config_layout.max_models, source->config_layout.max_sounds, source->config_layout.max_images};
        for (uint32_t kind = 0; kind < 3; ++kind)
            if ((uint32_t)index >= bases[kind] && (uint32_t)index - bases[kind] < counts[kind])
                qa_id_index_remove_value(&source->resources, previous, kind, (uint32_t)index - bases[kind]);
    }
    source->configstrings[index] = id; return true;
}

static bool native_resource(void *context, qa_native_host_resource_kind kind, const char *text,
    int32_t *out, qa_error *error)
{
    trajectory_native *source = context; *out = 0;
    if (!*text) return true;
    qa_string_id id;
    if (!qa_strings_intern_cstr(qa_session_strings(source->application->session), text, &id, error)) return false;
    qa_id_index_entry *existing = qa_id_index_find(&source->resources, id, (uint32_t)kind);
    if (existing) { *out = (int32_t)existing->value; return true; }
    uint32_t base = kind == QA_NATIVE_HOST_MODEL ? source->config_layout.models :
        kind == QA_NATIVE_HOST_SOUND ? source->config_layout.sounds : source->config_layout.images;
    uint32_t maximum = kind == QA_NATIVE_HOST_MODEL ? source->config_layout.max_models :
        kind == QA_NATIVE_HOST_SOUND ? source->config_layout.max_sounds : source->config_layout.max_images;
    for (uint32_t index = 1; index < maximum; ++index) {
        if (kind == QA_NATIVE_HOST_MODEL && index == 255) continue;
        qa_string_id value = source->configstrings[base + index];
        if (value && value != id) continue;
        if (source->resources.count == source->resources.capacity)
            return application_fail(error, QA_ERROR_CAPACITY, "Native trajectory resource namespace is full");
        source->configstrings[base + index] = id;
        qa_id_index_insert(&source->resources, id, (uint32_t)kind, index);
        *out = (int32_t)index; return true;
    }
    return application_fail(error, QA_ERROR_CAPACITY, "Native trajectory resource namespace is full");
}

static bool native_command_read(void *context, qa_native_host_command_view *out, qa_error *error)
{
    trajectory_native *source = context; (void)error;
    *out = source->command ? (qa_native_host_command_view){.arguments = source->command->arguments,
        .count = source->command->count, .tail = source->command->tail} : (qa_native_host_command_view){.tail = ""};
    return true;
}

static void native_print(void *context, const qa_native_host_print *print) { (void)context; (void)print; }
static bool native_message(void *context, const qa_native_host_message *event, qa_error *error)
{ (void)context; (void)event; (void)error; return true; }
static bool native_sound(void *context, const qa_native_host_sound *event, qa_error *error)
{ (void)context; (void)event; (void)error; return true; }
static uint32_t native_server_frame(void *context)
{
    trajectory_native *source = context;
    return source->interval_ns ? (uint32_t)(source->time_ns / source->interval_ns) : 0;
}
static uint64_t native_source_frame(void *context)
{
    trajectory_native *source = context;
    return source->interval_ns ? source->time_ns / source->interval_ns : 0;
}

static bool native_mirror(trajectory_native *, qa_actor_id, const qa_body_state *,
    uint32_t *, qa_error *);

static bool native_address_for_actor(void *context, qa_native_host *host, qa_actor_id actor,
    qa_native_address *out, bool *present, qa_error *error)
{
    trajectory_native *source = context; uint32_t slot;
    (void)host; *present = false;
    if (!native_mirror(source, actor, NULL, &slot, error) ||
        !qa_native_entity_address(source->instance, slot, out, error)) return false;
    *present = true; return true;
}

static bool native_import(void *context, const qa_native_import_call *call, qa_native_value *result,
    bool *handled, qa_error *error)
{
    trajectory_native *source = context;
    qa_native_q2_import_kind kind = qa_native_q2_import_dispatch(call->profile, call->slot);
    *handled = false;
    switch (kind) {
    case QA_NATIVE_Q2_IMPORT_SetAreaPortalState:
    case QA_NATIVE_Q2_IMPORT_SendToClipBoard:
    case QA_NATIVE_Q2_IMPORT_AddCommandString:
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Native trajectory cannot change the primary world or execute console commands");
    case QA_NATIVE_Q2_IMPORT_linkentity:
        if (source->capturing) {
            qa_native_address address = call->arguments[0].as.address; uint64_t owner, touch;
            if (!native_word(source, address + source->owner_pointer, &owner, 8, error) ||
                !native_word(source, address + source->touch_callback, &touch, 8, error)) return false;
            if (owner == source->captured_shooter && touch == source->touch.address) {
                uint32_t slot;
                if (!qa_native_entity_table_refresh(source->instance, &(qa_native_entity_table){0}, error) ||
                    !qa_native_entity_slot(source->instance, address, &slot, error)) return false;
                if (slot >= source->capacity)
                    return application_fail(error, QA_ERROR_CAPACITY, "Native trajectory launch exceeds its admitted source slots");
                if (!source->captured_count) {
                    if (!native_remember(source, slot, source->launching->projectile,
                        NATIVE_TRAJECTORY_PROJECTILE, error)) return false;
                    source->captured_slot = slot; source->captured_count = 1;
                } else if (slot != source->captured_slot) {
                    source->captured_count = 2;
                    return application_fail(error, QA_ERROR_UNSUPPORTED, "Native trajectory launch requires one-to-many projectile composition");
                }
            }
        }
        break;
    case QA_NATIVE_Q2_IMPORT_WriteChar: case QA_NATIVE_Q2_IMPORT_WriteByte:
    case QA_NATIVE_Q2_IMPORT_WriteShort: case QA_NATIVE_Q2_IMPORT_WriteLong:
    case QA_NATIVE_Q2_IMPORT_WriteFloat: case QA_NATIVE_Q2_IMPORT_WriteAngle:
    case QA_NATIVE_Q2_IMPORT_WritePosition: case QA_NATIVE_Q2_IMPORT_WriteDir:
    case QA_NATIVE_Q2_IMPORT_WriteString: case QA_NATIVE_Q2_IMPORT_WriteEntity:
    case QA_NATIVE_Q2_IMPORT_unicast: case QA_NATIVE_Q2_IMPORT_multicast:
    case QA_NATIVE_Q2_IMPORT_sound: case QA_NATIVE_Q2_IMPORT_positioned_sound:
    case QA_NATIVE_Q2_IMPORT_local_sound: case QA_NATIVE_Q2_IMPORT_bprintf:
    case QA_NATIVE_Q2_IMPORT_dprintf: case QA_NATIVE_Q2_IMPORT_cprintf:
    case QA_NATIVE_Q2_IMPORT_centerprintf: case QA_NATIVE_Q2_IMPORT_Broadcast_Print:
    case QA_NATIVE_Q2_IMPORT_Com_Print: case QA_NATIVE_Q2_IMPORT_Client_Print:
    case QA_NATIVE_Q2_IMPORT_Center_Print: case QA_NATIVE_Q2_IMPORT_Loc_Print:
    case QA_NATIVE_Q2_IMPORT_DebugGraph: case QA_NATIVE_Q2_IMPORT_Draw_Line:
    case QA_NATIVE_Q2_IMPORT_Draw_Point: case QA_NATIVE_Q2_IMPORT_Draw_Circle:
    case QA_NATIVE_Q2_IMPORT_Draw_Bounds: case QA_NATIVE_Q2_IMPORT_Draw_Sphere:
    case QA_NATIVE_Q2_IMPORT_Draw_OrientedWorldText: case QA_NATIVE_Q2_IMPORT_Draw_StaticWorldText:
    case QA_NATIVE_Q2_IMPORT_Draw_Cylinder: case QA_NATIVE_Q2_IMPORT_Draw_Ray:
    case QA_NATIVE_Q2_IMPORT_Draw_Arrow: case QA_NATIVE_Q2_IMPORT_ReportMatchDetails_Multicast:
        (void)result; *handled = true; return true;
    default: break;
    }
    return true;
}

static bool native_free_hook(void *context, qa_native_instance *instance,
    qa_native_entry_observer *observer, const qa_native_value *arguments, size_t count,
    qa_native_value *result, qa_error *error)
{
    trajectory_native *source = context; (void)instance;
    if (source->releasing) return qa_native_invoke_original(observer, arguments, count, result, error);
    uint32_t slot;
    if (count != 1 || !qa_native_entity_slot(source->instance, arguments[0].as.address, &slot, error)) return false;
    if (slot < source->capacity && (source->slots[slot].flags & NATIVE_TRAJECTORY_PROJECTILE)) {
        source->slots[slot].flags |= NATIVE_TRAJECTORY_RETIRED; return true;
    }
    if (!qa_native_invoke_original(observer, arguments, count, result, error)) return false;
    if (slot < source->capacity && source->slots[slot].flags) {
        bool active;
        if (!qa_native_host_source_active(source->host, slot, &active, error)) return false;
        if (!active) {
            qa_native_slot_binding clear = {.kind = QA_NATIVE_SLOT_FREE, .slot = slot};
            if (!qa_native_bind_slot(source->instance, &clear, error)) return false;
            source->slots[slot] = (native_trajectory_slot){0};
        }
    }
    return true;
}

static bool native_activate(trajectory_native *source, qa_error *error)
{
    if (!native_addresses(source, error)) return false;
    qa_native_type parameter = {.kind = QA_NATIVE_ADDRESS, .count = 1};
    qa_native_signature signature = native_signature(&parameter, 1, QA_NATIVE_VOID);
    return qa_native_observe_entry(source->instance, source->free_entity.address, &signature,
        native_free_hook, source, &source->free_observer, error);
}

static bool native_suspend(trajectory_native *source, qa_error *error)
{
    if (!source->free_observer) return true;
    if (!qa_native_unobserve_entry(source->free_observer, error)) return false;
    source->free_observer = NULL; return true;
}

typedef struct native_provision_call {
    trajectory_native *source;
    uint32_t slot;
    const native_trajectory_command *command;
} native_provision_call;

static bool native_provision_command(void *context, qa_error *error)
{
    native_provision_call *call = context;
    trajectory_native *source = call->source;
    if (!qa_native_host_refresh_cvars(source->host, error)) return false;
    const native_trajectory_command *previous = source->command;
    source->command = call->command;
    bool okay = qa_native_host_client_command(source->host, call->slot, error);
    source->command = previous;
    return okay;
}

static bool native_provision(trajectory_native *source, uint32_t slot,
    const native_trajectory_command *command, qa_error *error)
{
    native_provision_call call = {.source = source, .slot = slot, .command = command};
    bool okay = qa_cvars_value_scope_run(source->provisioning_scope,
        native_provision_command, &call, error);
    qa_error restored = {0};
    bool refreshed = qa_native_host_refresh_cvars(source->host, &restored);
    if (okay && !refreshed && error) *error = restored;
    return okay && refreshed;
}

static bool native_shooter(trajectory_native *source, qa_actor_id actor,
    const qa_body_state *body, uint32_t *out, qa_error *error)
{
    uint32_t slot; bool created = !qa_native_host_slot_for_actor(source->host, actor, &slot);
    if (!created && !(source->slots[slot].flags & NATIVE_TRAJECTORY_CLIENT)) {
        qa_native_address previous;
        if (!qa_native_entity_address(source->instance, slot, &previous, error) ||
            !qa_native_host_detach_actor(source->host, slot, actor, error) ||
            !native_invoke_entity(source, source->free_entity.address, previous, error)) return false;
        source->slots[slot] = (native_trajectory_slot){0}; created = true;
    }
    if (created) {
        for (slot = 1; slot <= source->maximum_clients && source->slots[slot].flags; ++slot) {}
        if (slot > source->maximum_clients)
            return application_fail(error, QA_ERROR_CAPACITY, "Native trajectory source client capacity is full");
        const application_player_record *record = application_player_record_find(source->application->players, actor);
        const char *userinfo = record ? record->userinfo : NULL;
        char fallback[2048] = {0};
        if (!userinfo) {
            qa_actor_player *player = qa_actors_player(qa_session_actors(source->application->session), actor);
            const char *name = player && player->name ? qa_strings_cstr(qa_session_strings(source->application->session), player->name) : "player";
            size_t size = 0;
            if (!qa_info_append(fallback, sizeof(fallback), &size, (qa_bytes){(const uint8_t *)"name", 4},
                (qa_bytes){(const uint8_t *)name, strlen(name)}, 0, error) ||
                !qa_info_append(fallback, sizeof(fallback), &size, (qa_bytes){(const uint8_t *)"ip", 2},
                (qa_bytes){(const uint8_t *)"loopback", 8}, 0, error)) return false;
            userinfo = fallback;
        }
        if (!native_remember(source, slot, actor, NATIVE_TRAJECTORY_CLIENT, error)) return false;
        qa_native_host_client_request request = {.slot = slot, .userinfo = userinfo, .social_id = ""};
        bool accepted = false;
        bool connected = qa_native_host_client_connect(source->host, &request, &accepted, error);
        if (!connected || !accepted) {
            qa_error cleanup = {0};
            bool detached = qa_native_host_detach_actor(source->host, slot, actor, &cleanup);
            if (detached) source->slots[slot] = (native_trajectory_slot){0};
            if (!connected) return false;
            if (!detached) { if (error) *error = cleanup; return false; }
            return application_fail(error, QA_ERROR_UNSUPPORTED, "Native trajectory source rejected its mirrored shooter");
        }
        if (!qa_native_host_client_begin(source->host, slot, error) ||
            !native_generation(source, slot, &source->slots[slot].generation, error)) return false;
    }
    if (!native_body(source, slot, body, error)) return false;
    qa_builtin_player_control control = {0}; qa_error local = {0};
    qa_vec3 view = body->angles; float height = body->bounds.maxs.z;
    if (source->services.player_control && source->services.player_control(source->services.context,
        source->owner, actor, &control, &local)) {
        if (control.view_angles) view = *control.view_angles;
        if (control.view_height) height = *control.view_height;
    } else if (local.code != QA_OK && local.code != QA_ERROR_NOT_FOUND) {
        if (error) *error = local;
        return false;
    }
    qa_native_address entity; uint64_t client;
    qa_vec3 forward; qa_angle_vectors(view, &forward, NULL, NULL, QA_ANGLE_NATIVE_MATH);
    if (!qa_native_entity_address(source->instance, slot, &entity, error) ||
        !native_word(source, entity + source->client, &client, 8, error) || !client ||
        !qa_native_range_check(source->instance, client, source->client_bytes, QA_NATIVE_MEMORY_READ | QA_NATIVE_MEMORY_WRITE, error) ||
        !native_write_vector(source, client + source->view_angles, view, error) ||
        !native_write_vector(source, client + source->forward, forward, error) ||
        !native_write_word(source, entity + source->view_height, (uint32_t)qa_source_to_i32(height), 4, error)) return false;
    if (!(source->slots[slot].flags & NATIVE_TRAJECTORY_ACTIVATED)) {
        for (size_t i = 0; i < source->equipment_count; ++i)
            if (!native_provision(source, slot, source->equipment + i, error)) return false;
        if (!qa_native_host_refresh_cvars(source->host, error)) return false;
        for (size_t i = 0; i < source->equip_count; ++i)
            if (!native_invoke_entity(source, source->equip[i].address, entity, error)) return false;
        uint64_t weapon, callback;
        if (!native_word(source, client + source->weapon, &weapon, 8, error) || !weapon ||
            !qa_native_range_check(source->instance, weapon, source->equipped_bytes, QA_NATIVE_MEMORY_READ, error) ||
            !native_word(source, weapon + source->equipped_callback, &callback, 8, error)) return false;
        if (callback != source->equipped.address)
            return application_fail(error, QA_ERROR_FORMAT, "Native trajectory source did not equip its declared weapon");
        source->slots[slot].flags |= NATIVE_TRAJECTORY_ACTIVATED;
    }
    *out = slot; return true;
}

static bool native_mirror(trajectory_native *source, qa_actor_id actor,
    const qa_body_state *observed, uint32_t *out, qa_error *error)
{
    uint32_t slot; bool present = qa_native_host_slot_for_actor(source->host, actor, &slot);
    if (present && (source->slots[slot].flags & NATIVE_TRAJECTORY_PROJECTILE)) { *out = slot; return true; }
    qa_body_state body;
    if (observed) body = *observed;
    else if (!qa_world_body_read(source->application->world, actor, &body, error)) return false;
    if (!present) {
        qa_native_signature signature = native_signature(NULL, 0, QA_NATIVE_ADDRESS);
        qa_native_value result = {.type = QA_NATIVE_ADDRESS};
        qa_native_entity_table table;
        if (!qa_native_invoke(source->instance, source->allocate.address, &signature, NULL, 0, &result, error) ||
            !qa_native_entity_table_refresh(source->instance, &table, error) ||
            !qa_native_entity_slot(source->instance, result.as.address, &slot, error)) return false;
        if (slot >= source->capacity || source->slots[slot].flags)
            return application_fail(error, QA_ERROR_FORMAT, "Native trajectory allocator returned an occupied source slot");
        if (!native_remember(source, slot, actor, NATIVE_TRAJECTORY_TARGET, error)) return false;
    }
    if (source->slots[slot].flags & NATIVE_TRAJECTORY_CLIENT)
        return native_shooter(source, actor, &body, out, error);
    if (!native_body(source, slot, &body, error)) return false;
    *out = slot; return true;
}

static bool native_refresh(trajectory_native *source, qa_actor_id excluded, qa_error *error)
{
    if (source->refreshed_once && source->refreshed_time == source->time_ns) return true;
    uint32_t cursor = 0; const qa_actor_record *actor;
    while (qa_actors_next(qa_session_actors(source->application->session), &cursor, &actor)) {
        if (qa_actor_id_equal(actor->id, excluded)) continue;
        uint32_t slot;
        if (!qa_native_host_slot_for_actor(source->host, actor->id, &slot) ||
            !(source->slots[slot].flags & (NATIVE_TRAJECTORY_TARGET | NATIVE_TRAJECTORY_CLIENT))) continue;
        qa_body_state body;
        if (!qa_world_body_read(source->application->world, actor->id, &body, error) ||
            !native_mirror(source, actor->id, &body, &slot, error)) return false;
    }
    source->refreshed_time = source->time_ns; source->refreshed_once = true; return true;
}

static bool native_attachment_slot(trajectory_native *source,
    const application_trajectory_attachment *attachment, uint32_t *slot, bool *present,
    qa_error *error)
{
    *present = qa_native_host_slot_for_actor(source->host, attachment->projectile, slot);
    if (!*present) return true;
    if (*slot >= source->capacity || attachment->handle != *slot ||
        (source->slots[*slot].flags & (NATIVE_TRAJECTORY_CLIENT | NATIVE_TRAJECTORY_TARGET |
            NATIVE_TRAJECTORY_PROJECTILE)) != NATIVE_TRAJECTORY_PROJECTILE)
        return application_fail(error, QA_ERROR_FORMAT, "Native trajectory attachment differs from its retained projectile slot");
    qa_native_slot_binding binding;
    if (!qa_native_slot(source->instance, *slot, &binding, error)) return false;
    if (binding.kind != QA_NATIVE_SLOT_BORROWED || binding.owner != source->owner ||
        binding.source_slot != *slot || !qa_actor_id_equal(binding.actor, attachment->projectile))
        return application_fail(error, QA_ERROR_FORMAT, "Native trajectory attachment differs from its full source actor binding");
    return true;
}

static bool native_launch(void *context, const qa_builtin_weapon_launch *launch,
    application_trajectory_attachment *attachment, qa_builtin_trajectory_update *out,
    bool *changed, qa_error *error)
{
    trajectory_native *source = context; *changed = false;
    qa_body_state shooter_body;
    if (!native_time(source, launch->time_ns, error) || !native_refresh(source, launch->projectile, error) ||
        !qa_world_body_read(source->application->world, launch->shooter, &shooter_body, error)) return false;
    uint32_t shooter;
    if (!native_shooter(source, launch->shooter, &shooter_body, &shooter, error) ||
        !native_provision(source, shooter, &source->ammunition, error) ||
        !qa_native_host_refresh_cvars(source->host, error) ||
        !qa_native_entity_address(source->instance, shooter, &source->captured_shooter, error)) return false;
    source->capturing = true; source->launching = launch; source->captured_count = 0;
    bool okay = true;
    for (size_t i = 0; okay && i < source->fire_count; ++i)
        okay = native_invoke_entity(source, source->fire[i].address, source->captured_shooter, error);
    source->capturing = false; source->launching = NULL;
    if (source->captured_count)
        *attachment = (application_trajectory_attachment){.projectile = launch->projectile, .handle = source->captured_slot};
    if (!okay) return false;
    if (!source->captured_count) return true;
    uint32_t slot; bool present;
    if (!native_attachment_slot(source, attachment, &slot, &present, error)) return false;
    if (!present) return application_fail(error, QA_ERROR_FORMAT, "Native trajectory launch lost its retained projectile actor");
    if (!native_trajectory(source, slot, out, error)) return false;
    *changed = true; return true;
}

static bool native_step(void *context, application_trajectory_attachment *attachment,
    const qa_body_state *body, uint64_t time_ns, qa_builtin_trajectory_update *out,
    bool *changed, qa_error *error)
{
    trajectory_native *source = context; uint32_t slot; bool present; *changed = false;
    if (!native_attachment_slot(source, attachment, &slot, &present, error)) return false;
    if (!present) return application_fail(error, QA_ERROR_FORMAT, "Native trajectory attachment has no retained projectile actor");
    if (source->slots[slot].flags & NATIVE_TRAJECTORY_RETIRED) { attachment->retired = true; return true; }
    bool active; int32_t generation;
    if (!native_time(source, time_ns, error) || !native_refresh(source, attachment->projectile, error) ||
        !qa_native_host_source_active(source->host, slot, &active, error) ||
        !native_generation(source, slot, &generation, error)) return false;
    if (!active || generation != source->slots[slot].generation) {
        source->slots[slot].flags |= NATIVE_TRAJECTORY_RETIRED; attachment->retired = true; return true;
    }
    qa_native_address address; uint64_t next, callback, registration;
    if (!native_body(source, slot, body, error) || !qa_native_entity_address(source->instance, slot, &address, error) ||
        !native_word(source, address + source->nextthink, &next, 8, error)) return false;
    if (!next || next > INT64_MAX || next > time_ns / UINT64_C(1000000)) return true;
    if (!native_word(source, address + source->think_callback, &callback, 8, error) ||
        !native_word(source, address + source->think_record, &registration, 8, error) ||
        !qa_native_range_check(source->instance, callback, 1, QA_NATIVE_MEMORY_EXECUTE, error) ||
        !native_registration_check(source, registration, &source->think_registration, source->think_tag, callback, NULL, error) ||
        !native_write_word(source, address + source->nextthink, 0, 8, error) ||
        !qa_native_host_refresh_cvars(source->host, error) ||
        !native_invoke_entity(source, callback, address, error) ||
        !qa_native_host_source_active(source->host, slot, &active, error) ||
        !native_generation(source, slot, &generation, error)) return false;
    if (!active || generation != source->slots[slot].generation ||
        (source->slots[slot].flags & NATIVE_TRAJECTORY_RETIRED)) {
        source->slots[slot].flags |= NATIVE_TRAJECTORY_RETIRED; attachment->retired = true; return true;
    }
    if (!native_trajectory(source, slot, out, error)) return false;
    *changed = true; return true;
}

static bool native_private_release(trajectory_native *source, uint32_t slot, qa_error *error)
{
    qa_native_address address; bool active; int32_t generation;
    if (!qa_native_entity_address(source->instance, slot, &address, error) ||
        !qa_native_host_source_active(source->host, slot, &active, error) ||
        !native_generation(source, slot, &generation, error)) return false;
    bool prior = source->releasing; source->releasing = true; bool okay = true;
    if (source->slots[slot].flags & NATIVE_TRAJECTORY_CLIENT) {
        okay = qa_native_host_client_disconnect(source->host, slot, error) &&
            qa_native_host_client_retained_set(source->host, slot, false, error);
    } else if (active && generation == source->slots[slot].generation)
        okay = native_invoke_entity(source, source->free_entity.address, address, error);
    source->releasing = prior;
    if (okay) source->slots[slot] = (native_trajectory_slot){0};
    return okay;
}

static bool native_release(void *context, application_trajectory_attachment *attachment, qa_error *error)
{
    trajectory_native *source = context; uint32_t slot; bool present;
    if (!native_attachment_slot(source, attachment, &slot, &present, error)) return false;
    if (!present) return true;
    if (!qa_actors_get(qa_session_actors(source->application->session), attachment->projectile)) return true;
    return qa_native_host_detach_actor(source->host, slot, attachment->projectile, error) &&
        native_private_release(source, slot, error);
}

static bool native_actor_released(void *context, qa_actor_record actor, qa_error *error)
{
    trajectory_native *source = context; uint32_t slot;
    bool present = qa_native_host_slot_for_actor(source->host, actor.id, &slot);
    if (!qa_native_host_actor_released(source->host, actor, error)) return false;
    if (!present) return true;
    if (qa_native_terminal(source->instance)) { source->slots[slot] = (native_trajectory_slot){0}; return true; }
    return native_private_release(source, slot, error);
}

static bool native_host_capture(void *context, qa_buffer *out, qa_error *error)
{
    trajectory_native *source = context; qa_source_save_io io = {0};
    bool okay = qa_source_save_writer(&io, source->application->session, error) &&
        qa_source_save_u64(&io, &source->time_ns);
    size_t count = 0;
    for (uint32_t slot = 1; slot < source->capacity; ++slot) if (source->slots[slot].flags) ++count;
    okay = okay && qa_source_save_count(&io, &count, source->capacity);
    for (uint32_t slot = 1; okay && slot < source->capacity; ++slot) if (source->slots[slot].flags) {
        native_trajectory_slot row = source->slots[slot];
        okay = qa_source_save_u32(&io, &slot) && qa_source_save_u8(&io, &row.flags) &&
            qa_source_save_i32(&io, &row.generation);
    }
    count = 0;
    for (uint32_t index = 0; index < source->config_layout.max_configs; ++index) if (source->configstrings[index]) ++count;
    okay = okay && qa_source_save_count(&io, &count, source->config_layout.max_configs);
    for (uint32_t index = 0; okay && index < source->config_layout.max_configs; ++index) if (source->configstrings[index])
        okay = qa_source_save_u32(&io, &index) && qa_source_save_string(&io, source->configstrings + index);
    if (okay) okay = qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return okay;
}

static bool native_resources_rebind(trajectory_native *source, qa_error *error)
{
    qa_id_index_clear(&source->resources);
    uint32_t bases[] = {source->config_layout.models, source->config_layout.sounds, source->config_layout.images};
    uint32_t counts[] = {source->config_layout.max_models, source->config_layout.max_sounds, source->config_layout.max_images};
    for (uint32_t kind = 0; kind < 3; ++kind)
        for (uint32_t index = 1; index < counts[kind]; ++index) {
            qa_string_id name = source->configstrings[bases[kind] + index];
            if (!name) continue;
            if (source->resources.count == source->resources.capacity)
                return application_fail(error, QA_ERROR_CAPACITY, "Restored native trajectory resource namespace is full");
            if (!qa_id_index_find(&source->resources, name, kind)) qa_id_index_insert(&source->resources, name, kind, index);
        }
    return true;
}

static bool native_host_restore(void *context, qa_bytes bytes,
    qa_native_host_restore_phase phase, qa_error *error)
{
    trajectory_native *source = context; qa_source_save_io io = {0}; size_t count;
    bool okay = qa_source_save_reader(&io, source->application->session, bytes, error) &&
        qa_source_save_u64(&io, &source->time_ns) && qa_source_save_count(&io, &count, source->capacity);
    memset(source->slots, 0, source->capacity * sizeof(*source->slots));
    for (size_t i = 0; okay && i < count; ++i) {
        uint32_t slot; native_trajectory_slot row;
        okay = qa_source_save_u32(&io, &slot) && qa_source_save_u8(&io, &row.flags) && qa_source_save_i32(&io, &row.generation);
        if (okay && (!slot || slot >= source->capacity || source->slots[slot].flags || !row.flags ||
            (row.flags & ~(NATIVE_TRAJECTORY_CLIENT | NATIVE_TRAJECTORY_TARGET | NATIVE_TRAJECTORY_PROJECTILE | NATIVE_TRAJECTORY_RETIRED | NATIVE_TRAJECTORY_ACTIVATED))))
            okay = application_fail(error, QA_ERROR_FORMAT, "Invalid saved native trajectory slot state");
        if (okay) source->slots[slot] = row;
    }
    memset(source->configstrings, 0, source->config_layout.max_configs * sizeof(*source->configstrings));
    okay = okay && qa_source_save_count(&io, &count, source->config_layout.max_configs);
    for (size_t i = 0; okay && i < count; ++i) {
        uint32_t index; qa_string_id name;
        okay = qa_source_save_u32(&io, &index) && qa_source_save_string(&io, &name);
        if (okay && (index >= source->config_layout.max_configs || source->configstrings[index] || !name))
            okay = application_fail(error, QA_ERROR_FORMAT, "Invalid saved native trajectory configstring");
        if (okay) source->configstrings[index] = name;
    }
    if (okay) okay = qa_source_save_finish(&io, NULL) && native_resources_rebind(source, error);
    qa_source_save_dispose(&io); (void)phase; return okay;
}

static bool native_capture(void *context, qa_buffer *out, qa_error *error)
{
    trajectory_native *source = context; qa_native_checkpoint checkpoint = {0};
    if (!native_suspend(source, error)) return false;
    bool okay = qa_native_checkpoint_capture(source->instance,
        (qa_native_checkpoint_request){.game = true, .level = true, .host = true}, &checkpoint, error) &&
        qa_native_checkpoint_encode(&checkpoint, out, error);
    qa_native_checkpoint_free(&checkpoint);
    qa_error restored = {0}; bool active = native_activate(source, &restored);
    if (okay && !active && error) *error = restored;
    return okay && active;
}

static bool native_open_host(trajectory_native *, qa_error *);

static bool native_close_host(trajectory_native *source, qa_error *error)
{
    if (!source->host) return true;
    if (!qa_native_host_startup_cancel(source->host, error)) return false;
    qa_native_instance *instance = qa_native_host_instance(source->host);
    if (!qa_native_host_destroy_ready(source->host))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native trajectory source still has an active callback");
    bool terminal = qa_native_terminal(instance);
    if (!terminal && !native_suspend(source, error)) return false;
    qa_native_entity_table table = {0};
    if (instance && !(terminal ? qa_native_terminal_entity_table(instance, &table, error) :
        qa_native_entity_table_get(instance, &table, error))) return false;
    for (uint32_t slot = 0; slot < table.capacity; ++slot) {
        qa_native_slot_binding binding;
        if (!qa_native_slot(instance, slot, &binding, error)) return false;
        if (binding.kind != QA_NATIVE_SLOT_FREE) {
            qa_native_slot_binding clear = {.kind = QA_NATIVE_SLOT_FREE, .slot = slot};
            if (!qa_native_bind_slot(instance, &clear, error)) return false;
        }
    }
    bool destroyed = qa_native_host_destroy_owned(&source->host, error);
    if (!source->host) { source->instance = NULL; source->free_observer = NULL; }
    return destroyed;
}

static bool native_restore(void *context, qa_bytes bytes, qa_error *error)
{
    trajectory_native *source = context; qa_native_checkpoint checkpoint = {0};
    if (!qa_native_checkpoint_decode(bytes, &checkpoint, error) || !native_close_host(source, error) ||
        !native_open_host(source, error)) {
        qa_native_checkpoint_free(&checkpoint); return false;
    }
    source->restoring = true;
    const char *map = qa_strings_cstr(qa_session_strings(source->application->session), source->application->current_map);
    bool okay = qa_native_host_restore(source->host, (qa_bytes){checkpoint.host.data, checkpoint.host.size},
            QA_NATIVE_HOST_RESTORE_CVARS, true, error) &&
        qa_native_host_initialize(source->host, 0, (int32_t)source->owner, false, error) &&
        qa_native_checkpoint_restore(source->instance, &checkpoint, QA_NATIVE_RESTORE_GAME, error) &&
        qa_native_host_restore(source->host, (qa_bytes){checkpoint.host.data, checkpoint.host.size},
            QA_NATIVE_HOST_RESTORE_PRECACHES, false, error) &&
        qa_native_host_spawn_entities(source->host, map, (const char *)source->entities.data, "", error) &&
        native_capacity(source, error) &&
        qa_native_host_restore(source->host, (qa_bytes){checkpoint.host.data, checkpoint.host.size},
            QA_NATIVE_HOST_RESTORE_PRECACHES, false, error) &&
        qa_native_checkpoint_restore(source->instance, &checkpoint, QA_NATIVE_RESTORE_LEVEL, error) &&
        qa_native_checkpoint_restore(source->instance, &checkpoint, QA_NATIVE_RESTORE_HOST, error) &&
        native_addresses(source, error) && native_time(source, source->time_ns, error);
    for (uint32_t slot = 1; okay && slot < source->capacity; ++slot) if (source->slots[slot].flags) {
        int32_t generation; bool active;
        okay = native_generation(source, slot, &generation, error) &&
            qa_native_host_source_active(source->host, slot, &active, error);
        if (okay && (!active || generation != source->slots[slot].generation))
            okay = application_fail(error, QA_ERROR_FORMAT, "Native trajectory source save changed a retained entity generation");
    }
    source->restoring = false; source->refreshed_once = false;
    qa_native_checkpoint_free(&checkpoint);
    return okay && native_activate(source, error);
}

static bool native_destroy(void *context, qa_error *error)
{
    trajectory_native *source = context;
    if (!native_close_host(source, error)) return false;
    qa_cvars_value_scope_destroy(source->provisioning_scope);
    qa_cvars_destroy(source->cvars); qa_native_module_release(source->module);
    qa_id_index_destroy(&source->resources); qa_arena_destroy(&source->definitions);
    qa_buffer_free(&source->entities); free(source->configstrings); free(source->slots);
    free(source); return true;
}

static bool native_json_u32(const qa_json_document *document, qa_json_id object,
    const char *name, uint32_t *out, qa_error *error)
{
    uint64_t value;
    if (!qa_json_u64(document, qa_json_get(document, object, name), &value, error)) return false;
    if (value > UINT32_MAX) return application_fail(error, QA_ERROR_FORMAT, "Native trajectory declaration exceeds uint32 storage");
    *out = (uint32_t)value; return true;
}

static const char *native_json_text(trajectory_native *source, const qa_json_document *document,
    qa_json_id id, bool empty, qa_error *error)
{
    qa_buffer text = {0};
    if (!qa_json_string(document, id, &text, error)) return NULL;
    if ((!empty && !text.size) || memchr(text.data, 0, text.size)) {
        qa_buffer_free(&text);
        application_fail(error, QA_ERROR_FORMAT, "Native trajectory declaration requires non-NUL text"); return NULL;
    }
    const char *retained = qa_arena_text(&source->definitions, (const char *)text.data, error);
    qa_buffer_free(&text); return retained;
}

static bool native_layout(const qa_json_document *document, qa_json_id object,
    const char *const *names, const unsigned *widths, uint32_t *const *offsets,
    size_t count, uint32_t *bytes, qa_error *error)
{
    if (!native_json_u32(document, object, "byteLength", bytes, error)) return false;
    for (size_t i = 0; i < count; ++i) {
        if (!native_json_u32(document, object, names[i], offsets[i], error)) return false;
        uint32_t offset = *offsets[i]; unsigned alignment = widths[i] == 12 ? 4 : widths[i];
        if (offset % alignment || offset > *bytes || widths[i] > *bytes - offset)
            return application_fail(error, QA_ERROR_FORMAT, "Native trajectory field exceeds its declared record or alignment");
        for (size_t j = 0; j < i; ++j)
            if (offset < *offsets[j] + widths[j] && *offsets[j] < offset + widths[i])
                return application_fail(error, QA_ERROR_FORMAT, "Native trajectory declaration fields overlap");
    }
    return true;
}

static bool native_registration_parse(const qa_json_document *document, qa_json_id object,
    native_trajectory_registration *out, qa_error *error)
{
    static const char *const names[] = {"name", "tag", "callback"};
    static const unsigned widths[] = {8, 4, 8};
    uint32_t *offsets[] = {&out->name, &out->tag, &out->callback};
    return native_layout(document, object, names, widths, offsets, 3, &out->bytes, error);
}

static bool native_entry_parse(trajectory_native *source, const qa_json_document *document,
    qa_json_id object, native_trajectory_entry *out, qa_error *error)
{
    if (!native_json_u32(document, object, "rva", &out->rva, error)) return false;
    if (!out->rva) return application_fail(error, QA_ERROR_FORMAT, "Native trajectory requires a non-null declared source entry");
    qa_json_id registration = qa_json_get(document, object, "registration");
    if (qa_json_type(document, registration) == QA_JSON_NULL) return true;
    if (!native_json_u32(document, registration, "rva", &out->registration_rva, error) ||
        !native_json_u32(document, registration, "tag", &out->tag, error) ||
        !native_registration_parse(document, qa_json_get(document, registration, "layout"), &out->registration, error)) return false;
    out->name = native_json_text(source, document, qa_json_get(document, registration, "name"), false, error);
    if (!out->registration_rva)
        return application_fail(error, QA_ERROR_FORMAT, "Native trajectory registration requires a non-null source RVA");
    return out->name != NULL;
}

static bool native_calls_parse(trajectory_native *source, const qa_json_document *document,
    qa_json_id object, native_trajectory_entry **out, size_t *count, qa_error *error)
{
    if (!qa_json_string_equal(document, qa_json_get(document, object, "signature"), "entity-void"))
        return application_fail(error, QA_ERROR_FORMAT, "Native trajectory source call has an unsupported ABI");
    qa_json_id calls = qa_json_get(document, object, "calls");
    *count = qa_json_size(document, calls);
    if (qa_json_type(document, calls) != QA_JSON_ARRAY || !*count || *count > NATIVE_TRAJECTORY_CALLS)
        return application_fail(error, QA_ERROR_FORMAT, "Native trajectory requires one to 64 declared source calls");
    *out = qa_arena_alloc(&source->definitions, *count * sizeof(**out), _Alignof(native_trajectory_entry), error);
    if (!*out) return false;
    memset(*out, 0, *count * sizeof(**out));
    for (size_t i = 0; i < *count; ++i)
        if (!native_entry_parse(source, document, qa_json_at(document, calls, i), *out + i, error)) return false;
    return true;
}

static bool native_command_parse(trajectory_native *source, const qa_json_document *document,
    qa_json_id object, native_trajectory_command *out, qa_error *error)
{
    qa_json_id arguments = qa_json_get(document, object, "arguments");
    out->count = qa_json_size(document, arguments);
    if (qa_json_type(document, arguments) != QA_JSON_ARRAY || !out->count || out->count > SIZE_MAX / sizeof(*out->arguments))
        return application_fail(error, QA_ERROR_FORMAT, "Native trajectory command requires its argument vector");
    out->arguments = qa_arena_alloc(&source->definitions, out->count * sizeof(*out->arguments), _Alignof(const char *), error);
    if (!out->arguments) return false;
    for (size_t i = 0; i < out->count; ++i) {
        out->arguments[i] = native_json_text(source, document, qa_json_at(document, arguments, i), i != 0, error);
        if (!out->arguments[i]) return false;
    }
    out->tail = native_json_text(source, document, qa_json_get(document, object, "tail"), true, error);
    return out->tail != NULL;
}

static bool native_cvars_parse(trajectory_native *source, const qa_json_document *document,
    qa_json_id array, qa_cvar_assignment **out, size_t *count, qa_error *error)
{
    *count = qa_json_size(document, array);
    if (qa_json_type(document, array) != QA_JSON_ARRAY || *count > SIZE_MAX / sizeof(**out))
        return application_fail(error, QA_ERROR_FORMAT, "Native trajectory cvars require a declared list");
    if (!*count) return true;
    *out = qa_arena_alloc(&source->definitions, *count * sizeof(**out), _Alignof(qa_cvar_assignment), error);
    if (!*out) return false;
    for (size_t i = 0; i < *count; ++i) {
        qa_json_id row = qa_json_at(document, array, i);
        qa_cvar_assignment *value = *out + i;
        value->name = native_json_text(source, document, qa_json_get(document, row, "name"), false, error);
        value->value = native_json_text(source, document, qa_json_get(document, row, "value"), true, error);
        if (!value->name || !value->value) return false;
        for (const char *c = value->name; *c; ++c)
            if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || *c == '_' ||
                (c != value->name && *c >= '0' && *c <= '9')))
                return application_fail(error, QA_ERROR_FORMAT, "Native trajectory cvar name is invalid");
        for (size_t j = 0; j < i; ++j)
            if (qa_text_ascii_equal(value->name, (*out)[j].name))
                return application_fail(error, QA_ERROR_FORMAT, "Native trajectory declares a cvar twice");
    }
    return true;
}

typedef struct native_declaration_range {
    uint32_t rva, bytes;
    bool executable, present;
} native_declaration_range;

typedef struct native_declaration_ranges {
    uint64_t preferred_base;
    native_declaration_range rows[2 * (4 + 2 * NATIVE_TRAJECTORY_CALLS)];
    size_t count;
} native_declaration_ranges;

static void native_declaration_entry_ranges(native_declaration_ranges *ranges,
    const native_trajectory_entry *entry)
{
    ranges->rows[ranges->count++] = (native_declaration_range){.rva = entry->rva,
        .bytes = 1, .executable = true};
    if (entry->registration_rva)
        ranges->rows[ranges->count++] = (native_declaration_range){.rva = entry->registration_rva,
            .bytes = entry->registration.bytes};
}

static bool native_declaration_range_visit(void *context, const qa_native_image_range *range,
    qa_error *error)
{
    native_declaration_ranges *ranges = context; (void)error;
    uint64_t start = range->address - ranges->preferred_base;
    for (size_t i = 0; i < ranges->count; ++i) {
        native_declaration_range *row = ranges->rows + i;
        if ((row->executable ? range->executable : range->readable) &&
            row->rva >= start && row->rva - start <= range->bytes &&
            row->bytes <= range->bytes - (row->rva - start)) row->present = true;
    }
    return true;
}

static bool native_declaration_image(trajectory_native *source, qa_error *error)
{
    if (!qa_native_module_mutable_range(source->module, source->time_rva, 8, error)) return false;
    native_declaration_ranges ranges = {.preferred_base = qa_native_module_describe(source->module).image.preferred_base};
    native_declaration_entry_ranges(&ranges, &source->allocate);
    native_declaration_entry_ranges(&ranges, &source->free_entity);
    native_declaration_entry_ranges(&ranges, &source->touch);
    native_declaration_entry_ranges(&ranges, &source->equipped);
    for (size_t i = 0; i < source->equip_count; ++i) native_declaration_entry_ranges(&ranges, source->equip + i);
    for (size_t i = 0; i < source->fire_count; ++i) native_declaration_entry_ranges(&ranges, source->fire + i);
    qa_native_image_layout layout;
    if (!qa_native_image_inspect_layout(qa_native_module_bytes(source->module), &layout,
        native_declaration_range_visit, &ranges, error)) return false;
    for (size_t i = 0; i < ranges.count; ++i)
        if (!ranges.rows[i].present)
            return application_fail(error, QA_ERROR_FORMAT, "Native trajectory callback or registration exceeds its source image permissions");
    return true;
}

static bool native_declaration(trajectory_native *source, const qa_catalog_weapon_behavior *behavior,
    qa_error *error)
{
    qa_native_module_info module = qa_native_module_describe(source->module);
    if (module.profile != QA_NATIVE_Q2_GAME_API2023 || module.image.target.abi != QA_NATIVE_ABI_MICROSOFT_X64 ||
        module.image.target.pointer_bytes != 8)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Declared native trajectory requires the API2023 Windows x64 source ABI");
    qa_json_document *document = NULL;
    qa_bytes declaration = behavior->entry.size ? behavior->entry :
        (qa_bytes){(const uint8_t *)application_native_q2eaks_trajectory, sizeof(application_native_q2eaks_trajectory) - 1};
    if (!qa_json_parse(QA_JSON_STRICT, declaration, &document, error)) return false;
    qa_json_id root = qa_json_root(document);
    bool okay = qa_json_string_equal(document, qa_json_get(document, root, "kind"), "q2-api2023-trajectory") &&
        qa_json_string_equal(document, qa_json_get(document, root, "abi"), "windows-x86-64");
    if (!okay) application_fail(error, QA_ERROR_FORMAT, "Unsupported native trajectory declaration ABI");
    static const char *const entity_names[] = {"origin", "angles", "velocity", "client", "owner", "viewHeight",
        "generation", "nextThink", "thinkCallback", "thinkRegistration", "touchCallback"};
    static const unsigned entity_widths[] = {12, 12, 12, 8, 8, 4, 4, 8, 8, 8, 8};
    uint32_t *entity_offsets[] = {&source->origin, &source->angles, &source->velocity, &source->client,
        &source->owner_pointer, &source->view_height, &source->generation, &source->nextthink,
        &source->think_callback, &source->think_record, &source->touch_callback};
    static const char *const client_names[] = {"weapon", "viewAngles", "forward"};
    static const unsigned client_widths[] = {8, 12, 12};
    uint32_t *client_offsets[] = {&source->weapon, &source->view_angles, &source->forward};
    static const char *const weapon_names[] = {"callback"}; static const unsigned weapon_widths[] = {8};
    uint32_t *weapon_offsets[] = {&source->equipped_callback};
    qa_json_id equipped = qa_json_get(document, root, "equippedWeapon");
    qa_json_id time = qa_json_get(document, root, "time"), think = qa_json_get(document, root, "think");
    qa_json_id allocate = qa_json_get(document, root, "allocate"), release = qa_json_get(document, root, "free");
    if (okay) okay = native_layout(document, qa_json_get(document, root, "entity"), entity_names,
        entity_widths, entity_offsets, sizeof(entity_names) / sizeof(*entity_names), &source->entity_bytes, error) &&
        native_layout(document, qa_json_get(document, root, "client"), client_names,
            client_widths, client_offsets, 3, &source->client_bytes, error) &&
        native_layout(document, equipped, weapon_names, weapon_widths, weapon_offsets, 1, &source->equipped_bytes, error) &&
        native_entry_parse(source, document, qa_json_get(document, equipped, "expected"), &source->equipped, error) &&
        native_entry_parse(source, document, qa_json_get(document, root, "projectileTouch"), &source->touch, error) &&
        native_entry_parse(source, document, qa_json_get(document, allocate, "entry"), &source->allocate, error) &&
        native_entry_parse(source, document, qa_json_get(document, release, "entry"), &source->free_entity, error) &&
        native_registration_parse(document, qa_json_get(document, think, "registration"), &source->think_registration, error) &&
        native_json_u32(document, think, "tag", &source->think_tag, error) &&
        native_json_u32(document, time, "rva", &source->time_rva, error) &&
        native_calls_parse(source, document, qa_json_get(document, root, "equip"), &source->equip, &source->equip_count, error) &&
        native_calls_parse(source, document, qa_json_get(document, root, "launch"), &source->fire, &source->fire_count, error);
    if (okay && (!source->time_rva ||
        !qa_json_string_equal(document, qa_json_get(document, time, "storage"), "int64-milliseconds") ||
        !qa_json_string_equal(document, qa_json_get(document, think, "signature"), "entity-void") ||
        !qa_json_string_equal(document, qa_json_get(document, allocate, "signature"), "void-pointer") ||
        !qa_json_string_equal(document, qa_json_get(document, release, "signature"), "entity-void")))
        okay = application_fail(error, QA_ERROR_FORMAT, "Unsupported native trajectory source signature or clock storage");
    uint32_t fire = 0, activation = 0;
    if (okay) okay = native_json_u32(document, root, "fireRva", &fire, error);
    qa_json_id activation_id = qa_json_get(document, root, "activateRva");
    if (okay && qa_json_type(document, activation_id) != QA_JSON_NULL)
        okay = native_json_u32(document, root, "activateRva", &activation, error);
    bool fire_found = false, activation_found = activation == 0;
    for (size_t i = 0; i < source->fire_count; ++i) if (source->fire[i].rva == fire) fire_found = true;
    for (size_t i = 0; i < source->equip_count; ++i) if (source->equip[i].rva == activation) activation_found = true;
    if (okay && (!fire_found || !activation_found))
        okay = application_fail(error, QA_ERROR_FORMAT, "Native trajectory callbacks differ from their declared call chains");
    if (okay) okay = native_cvars_parse(source, document, qa_json_get(document, root, "initialCvars"),
        &source->initial_cvars, &source->initial_cvar_count, error) &&
        native_cvars_parse(source, document, qa_json_get(document, root, "provisioningCvars"),
            &source->provisioning_cvars, &source->provisioning_cvar_count, error) &&
        native_command_parse(source, document, qa_json_get(document, root, "ammunition"), &source->ammunition, error);
    qa_json_id equipment = qa_json_get(document, root, "equipment");
    source->equipment_count = qa_json_size(document, equipment);
    if (okay && (qa_json_type(document, equipment) != QA_JSON_ARRAY || source->equipment_count > SIZE_MAX / sizeof(*source->equipment)))
        okay = application_fail(error, QA_ERROR_FORMAT, "Native trajectory equipment requires a command list");
    if (okay && source->equipment_count) {
        source->equipment = qa_arena_alloc(&source->definitions, source->equipment_count * sizeof(*source->equipment), _Alignof(native_trajectory_command), error);
        okay = source->equipment != NULL;
        for (size_t i = 0; okay && i < source->equipment_count; ++i)
            okay = native_command_parse(source, document, qa_json_at(document, equipment, i), source->equipment + i, error);
    }
    qa_json_id classes = qa_json_get(document, root, "initializationClasses");
    size_t count = qa_json_size(document, classes), worlds = 0;
    const char **names = NULL;
    if (okay && (qa_json_type(document, classes) != QA_JSON_ARRAY || !count || count > SIZE_MAX / sizeof(*names)))
        okay = application_fail(error, QA_ERROR_FORMAT, "Native trajectory requires declared initialization classes");
    if (okay) { names = qa_arena_alloc(&source->definitions, count * sizeof(*names), _Alignof(const char *), error); okay = names != NULL; }
    for (size_t i = 0; okay && i < count; ++i) {
        names[i] = native_json_text(source, document, qa_json_at(document, classes, i), false, error);
        okay = names[i] != NULL;
        if (okay && !strcmp(names[i], "worldspawn")) ++worlds;
        for (size_t j = 0; okay && j < i; ++j) if (!strcmp(names[i], names[j]))
            okay = application_fail(error, QA_ERROR_FORMAT, "Native trajectory repeats an initialization class");
    }
    if (okay && worlds != 1) okay = application_fail(error, QA_ERROR_FORMAT, "Native trajectory requires one worldspawn initialization class");
    if (okay) {
        source->initialization_classes = names;
        source->initialization_class_count = count;
        okay = native_declaration_image(source, error);
    }
    qa_json_destroy(document); return okay;
}

bool application_trajectory_native_check(qa_native_module *module,
    const qa_catalog_weapon_behavior *behavior, qa_error *error)
{
    if (!module || !behavior)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native trajectory checking requires its source module and declaration");
    trajectory_native source = {.module = module};
    qa_arena_init(&source.definitions, 16384);
    bool okay = native_declaration(&source, behavior, error);
    qa_arena_destroy(&source.definitions);
    return okay;
}

static bool native_open_host(trajectory_native *source, qa_error *error)
{
    qa_application *application = source->application;
    qa_native_host_q2_game_options options = {
        .instance = {.observe = true, .runtime = application->native_runtime_config,
            .tick_rate = source->interval_ns ? (uint32_t)(UINT64_C(1000000000) / source->interval_ns) : 0,
            .frame_seconds = (float)source->interval_ns / 1000000000.f,
            .frame_milliseconds = (uint32_t)(source->interval_ns / UINT64_C(1000000))},
        .world = {.session = application->session, .world = application->world,
            .owner = source->owner, .definition = source->owner, .world_actor = application->physics->world_actor,
            .projected_entities = true, .binding_context = source, .address_for_actor = native_address_for_actor},
        .services = {.engine = {.context = source, .strings = qa_session_strings(application->session),
            .cvars = source->cvars, .content_files = source->launch->content,
            .print = native_print, .configstring_get = native_config_get, .configstring_set = native_config_set,
            .resource_index = native_resource, .command = native_command_read,
            .message = native_message, .sound = native_sound, .source_import = native_import,
            .server_frame = native_server_frame, .source_frame = native_source_frame,
            .checkpoint = native_host_capture, .restore = native_host_restore}},
        .cvars = source->cvars, .console = application->console,
        .command_context = {.owner = source->owner, .dialect = QA_RULESET_Q2_RERELEASE, .origin = QA_COMMAND_SERVER}};
    bool okay = qa_native_host_create_q2_game(source->module, &options, &source->host, error);
    source->instance = qa_native_host_instance(source->host);
    if (okay) {
        const qa_native_host_entity_layout *layout = qa_native_host_source_layout(source->host);
        if (!layout || source->entity_bytes < layout->bytes || source->origin != 4 ||
            source->angles != 16 || source->client != layout->client || source->owner_pointer != layout->owner)
            okay = application_fail(error, QA_ERROR_FORMAT, "Native trajectory declaration differs from its public API2023 entity prefix");
    }
    return okay && native_addresses(source, error);
}

bool application_trajectory_native_create(qa_application *application, const qa_launch_instance *launch,
    qa_native_module *module, const qa_catalog_weapon_behavior *behavior,
    application_trajectory_source *out, qa_error *error)
{
    trajectory_native *source = calloc(1, sizeof(*source));
    if (!source) return application_fail(error, QA_ERROR_MEMORY, "Creating native trajectory source");
    source->application = application; source->launch = launch; source->module = module;
    source->interval_ns = launch->selection.clock.interval_ns;
    qa_native_module_retain(module); qa_arena_init(&source->definitions, 16384);
    source->services = application_builtin_services(application, application->world, application->physics);
    qa_q2_codec codec;
    bool okay = qa_strings_intern_cstr(qa_session_strings(application->session), behavior->id, &source->owner, error) &&
        qa_q2_codec_init(&codec, qa_q2_default_protocol(true), error) &&
        qa_q2_config_layout_read(&codec, &source->config_layout, error) && native_declaration(source, behavior, error);
    if (okay) okay = application_trajectory_entities_prepare(application, source->initialization_classes,
        source->initialization_class_count, &source->entities, error);
    if (okay) {
        source->configstrings = calloc(source->config_layout.max_configs, sizeof(*source->configstrings));
        okay = source->configstrings && qa_id_index_reserve(&source->resources,
            (size_t)source->config_layout.max_models + source->config_layout.max_sounds + source->config_layout.max_images, error);
    }
    if (okay) {
        qa_cvar_options options = {.dialect = QA_RULESET_Q2_RERELEASE, .side = QA_CVAR_SIDE_SERVER,
            .role = QA_CVAR_ROLE_GAME, .default_save_policy = QA_CVAR_SAVE_GAMEPLAY};
        source->cvars = qa_cvars_create_view(application->cvars, &options, error); okay = source->cvars != NULL;
    }
    if (okay) okay = native_open_host(source, error);
    for (size_t i = 0; okay && i < source->initial_cvar_count; ++i)
        okay = qa_cvars_apply(source->cvars, &(qa_cvars_edit_command){.kind = QA_CVARS_EDIT_SET,
            .name = source->initial_cvars[i].name, .value = source->initial_cvars[i].value, .force = true}, error);
    if (okay) okay = qa_native_host_initialize(source->host, 0, (int32_t)source->owner, false, error);
    if (okay) {
        const char *map = qa_strings_cstr(qa_session_strings(application->session), application->current_map);
        okay = qa_native_host_spawn_entities(source->host, map, (const char *)source->entities.data, "", error) && native_capacity(source, error);
        const qa_cvar_view *maximum = qa_cvars_find(source->cvars, "maxclients");
        source->maximum_clients = maximum && maximum->integer > 0 ? (uint32_t)maximum->integer : 1;
        if (okay && source->maximum_clients >= source->capacity)
            okay = application_fail(error, QA_ERROR_FORMAT, "Native trajectory source clients exceed its actual entity capacity");
    }
    if (okay) okay = qa_cvars_value_scope_create(source->cvars, source->provisioning_cvars,
        source->provisioning_cvar_count, &source->provisioning_scope, error) && native_activate(source, error);
    if (!okay) {
        if (error && error->code == QA_OK) application_fail(error, QA_ERROR_MEMORY, "Preparing native trajectory source");
        if (!native_destroy(source, NULL))
            *out = (application_trajectory_source){.context = source, .destroy = native_destroy};
        return false;
    }
    qa_arena_seal(&source->definitions);
    *out = (application_trajectory_source){.context = source, .launch = native_launch, .step = native_step,
        .release = native_release, .actor_released = native_actor_released,
        .capture = native_capture, .restore = native_restore, .destroy = native_destroy};
    return true;
}
