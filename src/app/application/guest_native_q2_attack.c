#include "guest_native_q2_private.h"
#include "guest_native_q2_attack.h"
#include "guest_native_q2_input.h"
#include "control_frame.h"
#include "native_q2_inventory_scanner.h"
#include "native_q2_callbacks.h"
#include "supplies.h"
#include "map_players_private.h"
#include "qa/native_observe.h"
#include "qa/persistence_fields.h"
#include "qa/pool.h"
#include "qa/text.h"
#include <math.h>

typedef struct attack_item {
    qa_native_address descriptor, think;
    qa_item_id item, ammo;
} attack_item;
typedef struct attack_factor {
    struct attack_factor *next, *previous;
    size_t storage_slot;
    unsigned calls;
    qa_actor_id actor;
    double factor, delay_flag;
    uint64_t clock_ns;
    bool available, retired, delay_available, clock_present;
} attack_factor;
typedef struct attack_projectile {
    struct attack_projectile *next, *previous;
    size_t storage_slot;
    qa_native_address address;
    qa_actor_id actor;
    qa_attack attack;
} attack_projectile;
typedef struct attack_frame {
    struct attack_frame *previous;
    qa_actor_id actor;
    qa_item_id item;
    qa_attack attack;
    double factor;
    bool captured;
} attack_frame;
typedef struct attack_hook {
    struct attack_hook *next;
    size_t storage_slot;
    struct application_native_q2_attack *owner;
    qa_native_entry_observer *binding;
    qa_native_address address;
} attack_hook;
typedef enum decision_record {
    DECISION_ENTITY, DECISION_CLIENT, DECISION_IMAGE
} decision_record;
typedef struct decision_field {
    decision_record record;
    qa_native_value_type encoding;
    uint32_t offset;
    uint8_t bytes;
} decision_field;
typedef struct decision_input {
    decision_field field;
    uint32_t mask;
} decision_input;
typedef struct decision_test_descriptor {
    decision_field field;
    bool pointer, has_pointer_value, masked, at_most;
    uint32_t rva, mask;
    uint32_t *indirections;
    size_t indirection_count;
    double expected;
} decision_test_descriptor;
typedef struct decision_group {
    decision_test_descriptor *tests;
    size_t count;
} decision_group;
typedef struct decision_region {
    struct application_native_q2_attack *owner;
    decision_input *fields;
    size_t field_count;
    uint32_t id;
    qa_native_region_binding *binding;
} decision_region;
typedef struct decision_saved {
    qa_native_address address;
    uint32_t bits, mask;
    size_t bytes;
    bool written;
} decision_saved;
typedef struct decision_projection {
    struct decision_projection *next;
    qa_unified_frame_lease *storage;
    decision_region *region;
    decision_saved *fields;
    size_t count;
    qa_actor_id actor;
    qa_native_address entity;
} decision_projection;
typedef struct decision_frame {
    struct decision_frame *previous;
    qa_actor_id actor;
    qa_native_address entity;
    decision_projection *base;
    bool committed, reached;
} decision_frame;
typedef struct attack_delay_projection {
    decision_field field;
    double value;
} attack_delay_projection;
typedef struct attack_equipment_context {
    qa_string_id implementation;
    qa_item_id item;
} attack_equipment_context;
typedef enum attack_delay_kind { ATTACK_DELAY_NONE, ATTACK_DELAY_FLAG, ATTACK_DELAY_ANIMATION } attack_delay_kind;
struct application_native_q2_attack {
    struct application_native_q2 *engine;
    qa_json_document *document;
    qa_json_id damage;
    uint32_t client_pointer, weapon, client_bytes, item_table, item_stride,
        item_count, item_classname, item_flags, weapon_flag, weapon_think;
    uint32_t spawn, free, factor_entry, factor_address;
    uint32_t client_spawn;
    decision_group spawn_accepted;
    decision_field max_health;
    bool client_spawn_declared;
    uint8_t pointer_bytes;
    qa_native_value_type factor_type;
    qa_native_abi abi;
    attack_item *items;
    size_t count;
    attack_hook *weapons;
    qa_native_entry_observer *spawn_hook, *free_hook, *factor_hook, *client_spawn_hook;
    attack_factor *factors;
    attack_projectile *projectiles;
    qa_actor_index factor_index, projectile_index;
    qa_pool factor_pool, projectile_pool, hook_pool;
    qa_arena factor_storage, projectile_storage, hook_storage;
    attack_frame *current;
    uint64_t sequence;
    uint32_t dispatcher_entry, dispatcher_argument, dispatcher_count;
    qa_native_type dispatcher_parameters[16];
    qa_native_entry_observer *dispatcher_hook;
    decision_region *decisions;
    size_t decision_count;
    decision_frame *dispatch;
    decision_projection *projections;
    decision_group *committed;
    size_t committed_count;
    decision_group available;
    decision_group *continuations;
    size_t continuation_count;
    decision_field clock;
    double clock_milliseconds;
    bool clock_declared, continuations_declared;
    decision_field water_level;
    uint32_t command_angles;
    attack_delay_kind delay_kind;
    decision_field delay_flag;
    uint32_t delay_region, delay_entry;
    qa_native_region_binding *delay_hook;
    qa_native_address delay_result;
    double *delay_factors, delay_baseline;
    size_t delay_factor_count, delay_projection_count, delay_write_count;
    attack_delay_projection *delay_projection;
    decision_field *delay_writes;
    attack_equipment_context *equipment_contexts;
    size_t equipment_context_count;
    qa_string_id hand_grenades;
    qa_supply_weapon *source_weapons;
    bool ammo_by_name;
    uint32_t ammo_offset, ammo_label;
    qa_arena decision_storage;
    bool active, items_ready;
};
static bool word(const qa_json_document *doc, qa_json_id object, const char *name,
    uint32_t *out, qa_error *error)
{
    uint64_t value;
    if (!qa_json_u64(doc, qa_json_get(doc, object, name), &value, error)) return false;
    if (value > UINT32_MAX) { application_fail(error, QA_ERROR_FORMAT, "Native attack source field exceeds uint32"); return false; }
    *out = (uint32_t)value; return true;
}
static qa_native_instance *instance(struct application_native_q2_attack *p)
{ return qa_native_host_instance(p->engine->provider->state.native.host); }
static bool pointer_read(struct application_native_q2_attack *p, qa_native_address address,
    qa_native_address *out, qa_error *error)
{
    uint8_t bytes[8];
    if (!qa_native_read(instance(p), address, bytes, p->pointer_bytes, error)) return false;
    *out = p->pointer_bytes == 4 ? qa_load_u32le(bytes) : qa_load_u64le(bytes); return true;
}
static qa_native_type scalar(qa_native_value_type kind)
{ return (qa_native_type){.kind = kind, .count = 1}; }
static qa_native_signature signature(struct application_native_q2_attack *p,
    const qa_native_type *parameters, size_t count, qa_native_value_type result)
{ return (qa_native_signature){.abi = p->abi, .parameters = parameters,
    .parameter_count = count, .result = scalar(result)}; }
static bool source_actor(struct application_native_q2_attack *p, qa_native_address address,
    qa_actor_id *out, qa_error *error)
{
    if (!qa_native_host_source_actor(p->engine->provider->state.native.host, address, true, out, error)) return false;
    return out->registry != 0 || application_fail(error, QA_ERROR_NOT_FOUND, "Native attack actor is not in use");
}
#include "guest_native_q2_attack_decisions.h"
bool application_native_q2_attack_input_fields(struct application_native_q2 *engine,uint32_t slot,
    qa_actor_id actor,qa_movement_result *out,qa_vec3 *command_angles,qa_error *error)
{
    struct application_native_q2_attack *p=engine?engine->source_attack:NULL;
    if(!p||!out) return application_fail(error,QA_ERROR_NOT_FOUND,"Native Source input has no qualified primary field producer");
    qa_native_address entity,client;
    qa_actor_id current;
    if(!qa_native_entity_address(instance(p),slot,&entity,error)||
        !qa_native_host_source_actor(engine->provider->state.native.host,entity,false,&current,error)||
        !pointer_read(p,entity+p->client_pointer,&client,error)) return false;
    if(!qa_actor_id_equal(current,actor)||!client)
        return application_fail(error,QA_ERROR_NOT_FOUND,"Native input fields lost their physical Source client");
    double water;
    if(!decision_scalar(p,entity,&p->water_level,&water,error)) return false;
    if(water<0||water>3||trunc(water)!=water)
        return application_fail(error,QA_ERROR_FORMAT,"Native Source water level exceeds its actual SDK domain");
    out->water_level=(int32_t)water;
    uint8_t bytes[12];
    if(!qa_native_read(instance(p),client+p->command_angles,bytes,sizeof(bytes),error)) return false;
    *command_angles=qa_v3(qa_load_f32le(bytes),qa_load_f32le(bytes+4),qa_load_f32le(bytes+8));
    return qa_vec_finite(*command_angles)||application_fail(error,QA_ERROR_FORMAT,"Native Source command angles are not finite");
}
static attack_factor *factor_for(struct application_native_q2_attack *p, qa_actor_id actor)
{
    size_t value;
    return qa_actor_index_find(&p->factor_index, actor, &value) ? (attack_factor *)(uintptr_t)value : NULL;
}
static attack_factor *factor_create(struct application_native_q2_attack *p, qa_actor_id actor,
    qa_error *error)
{
    if (p->factor_pool.head == SIZE_MAX && !qa_pool_extend(&p->factor_pool,
            &p->factor_storage, p->factor_pool.first_capacity ? p->factor_pool.first_capacity : 1, error)) return NULL;
    size_t slot; attack_factor *factor = qa_pool_take(&p->factor_pool, &slot);
    if (!factor) { application_fail(error, QA_ERROR_MEMORY, "Retaining original weapon modifier result"); return NULL; }
    *factor = (attack_factor){.storage_slot = slot, .actor = actor, .factor = 1, .next = p->factors};
    if (factor->next) factor->next->previous = factor;
    p->factors = factor;
    qa_actor_index_put(&p->factor_index, actor, (size_t)(uintptr_t)factor);
    return factor;
}
static bool policy_entity(struct application_native_q2 *engine, qa_actor_id actor,
    struct application_native_q2_attack **owner, qa_native_address *entity, qa_error *error)
{
    struct application_native_q2_attack *p = engine ? engine->source_attack : NULL;
    uint32_t slot;
    if (!p || !qa_native_host_slot_for_actor(engine->provider->state.native.host, actor, &slot))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Native weapon policy has no actual Source player");
    if (!qa_native_entity_address(instance(p), slot, entity, error)) return false;
    *owner = p; return true;
}
bool application_native_q2_attack_available(struct application_native_q2 *engine,
    qa_actor_id actor, bool *out, qa_error *error)
{
    if (!engine->source_attack) { *out = true; return true; }
    struct application_native_q2_attack *p;
    qa_native_address entity;
    return policy_entity(engine, actor, &p, &entity, error) &&
        decision_matches(p, entity, &p->available, out, error);
}
bool application_native_q2_attack_continuing(struct application_native_q2 *engine,
    qa_actor_id actor, bool *out, qa_error *error)
{
    if (!engine->source_attack || !engine->source_attack->continuations_declared) {
        *out = true; return true;
    }
    struct application_native_q2_attack *p;
    qa_native_address entity;
    return policy_entity(engine, actor, &p, &entity, error) &&
        decision_any(p, entity, p->continuations, p->continuation_count, out, error);
}
static bool clock_read(struct application_native_q2_attack *p, uint64_t *out, qa_error *error)
{
    double value;
    if (!decision_scalar(p, 0, &p->clock, &value, error)) return false;
    long double ns = (long double)value * p->clock_milliseconds * UINT64_C(1000000);
    if (ns < 0 || ns >= 18446744073709551616.0L)
        return application_fail(error, QA_ERROR_FORMAT, "Native level time exceeds its shared clock");
    *out = (uint64_t)ns; return true;
}
bool application_native_q2_attack_clock_reset(struct application_native_q2 *engine,
    qa_actor_id actor, qa_error *error)
{
    struct application_native_q2_attack *p = engine ? engine->source_attack : NULL;
    if (!p || !p->clock_declared) return true;
    attack_factor *state = factor_for(p, actor);
    if (!state) state = factor_create(p, actor, error);
    if (!state || !clock_read(p, &state->clock_ns, error)) return false;
    state->clock_present = true; return true;
}
bool application_native_q2_attack_clock_step(struct application_native_q2 *engine,
    qa_actor_id actor, uint64_t *time_ns, uint64_t *elapsed_ns, qa_error *error)
{
    struct application_native_q2_attack *p = engine->source_attack;
    if (!p || !p->clock_declared) return true;
    if (!clock_read(p, time_ns, error)) return false;
    attack_factor *state = factor_for(p, actor);
    if (!state) state = factor_create(p, actor, error);
    if (!state) return false;
    *elapsed_ns = state->clock_present && *time_ns >= state->clock_ns ? *time_ns - state->clock_ns : 0;
    state->clock_ns = *time_ns; state->clock_present = true;
    return true;
}
static void factor_reclaim(struct application_native_q2_attack *p, attack_factor *factor)
{
    if (!factor->retired || factor->calls) return;
    if (factor->previous) factor->previous->next = factor->next;
    else p->factors = factor->next;
    if (factor->next) factor->next->previous = factor->previous;
    qa_pool_release(&p->factor_pool, factor->storage_slot);
}
static attack_projectile *projectile_for(struct application_native_q2_attack *p, qa_actor_id actor)
{
    size_t slot;
    return qa_actor_index_find(&p->projectile_index, actor, &slot) ? qa_pool_at(&p->projectile_pool, slot) : NULL;
}
static void projectile_release(struct application_native_q2_attack *p, attack_projectile *lease)
{
    qa_actor_index_remove(&p->projectile_index, lease->actor);
    if (lease->previous) lease->previous->next = lease->next;
    else p->projectiles = lease->next;
    if (lease->next) lease->next->previous = lease->previous;
    qa_pool_release(&p->projectile_pool, lease->storage_slot);
}
static bool delay_prepare(struct application_native_q2_attack *p, qa_json_id weapons,
    qa_json_id items, qa_error *error)
{
    const qa_json_document *doc = p->document;
    qa_json_id delay = qa_json_get(doc, weapons, "delay");
    if (qa_json_type(doc, delay) == QA_JSON_NULL) return true;
    qa_json_id evaluate = qa_json_get(doc, delay, "evaluate");
    qa_native_declared_region region;
    if (!decision_scalar_prepare(p, qa_json_get(doc, delay, "flag"), &p->delay_flag, error) ||
        !qa_native_declaration_find_region(p->engine->declaration, "/weapons/delay/region", &region, error)) return false;
    p->delay_region = region.id;
    if (qa_json_string_equal(doc, qa_json_get(doc, evaluate, "kind"), "source-flag")) {
        p->delay_kind = ATTACK_DELAY_FLAG;
        qa_json_id factors = qa_json_get(doc, evaluate, "factors");
        p->delay_factor_count = qa_json_size(doc, factors);
        if (qa_json_type(doc, factors) != QA_JSON_ARRAY || !p->delay_factor_count)
            return application_fail(error, QA_ERROR_FORMAT, "Native firing modifier has no declared factors");
        p->delay_factors = decision_array(p, p->delay_factor_count, sizeof(double), _Alignof(double), error);
        if (!p->delay_factors) return false;
        for (size_t i = 0; i < p->delay_factor_count; ++i)
            if (!qa_json_number(doc, qa_json_at(doc, factors, i), p->delay_factors + i, error) ||
                !isfinite(p->delay_factors[i]) || p->delay_factors[i] < 0)
                return application_fail(error, QA_ERROR_FORMAT, "Native firing factor is not finite and nonnegative");
    } else if (qa_json_string_equal(doc, qa_json_get(doc, evaluate, "kind"), "source-animation")) {
        p->delay_kind = ATTACK_DELAY_ANIMATION;
        qa_json_id projection = qa_json_get(doc, evaluate, "projection"), writes = qa_json_get(doc, evaluate, "writes");
        if (!word(doc, evaluate, "entry", &p->delay_entry, error) ||
            !qa_json_number(doc, qa_json_get(doc, evaluate, "baselineMilliseconds"), &p->delay_baseline, error)) return false;
        if (!isfinite(p->delay_baseline) || p->delay_baseline <= 0 ||
            qa_json_type(doc, projection) != QA_JSON_ARRAY || qa_json_type(doc, writes) != QA_JSON_ARRAY)
            return application_fail(error, QA_ERROR_FORMAT, "Native animation delay has no valid baseline or field rosters");
        p->delay_projection_count = qa_json_size(doc, projection);
        p->delay_write_count = qa_json_size(doc, writes);
        if (p->delay_projection_count > SIZE_MAX - p->delay_write_count - 1)
            return application_fail(error, QA_ERROR_FORMAT, "Native animation delay field count overflows");
        if (p->delay_projection_count) {
            p->delay_projection = decision_array(p, p->delay_projection_count, sizeof(*p->delay_projection),
                _Alignof(attack_delay_projection), error);
            if (!p->delay_projection) return false;
        }
        if (p->delay_write_count) {
            p->delay_writes = decision_array(p, p->delay_write_count, sizeof(*p->delay_writes), _Alignof(decision_field), error);
            if (!p->delay_writes) return false;
        }
        for (size_t i = 0; i < p->delay_projection_count; ++i) {
            qa_json_id row = qa_json_at(doc, projection, i);
            if (!decision_scalar_prepare(p, qa_json_get(doc, row, "field"), &p->delay_projection[i].field, error) ||
                !qa_json_number(doc, qa_json_get(doc, row, "value"), &p->delay_projection[i].value, error)) return false;
        }
        for (size_t i = 0; i < p->delay_write_count; ++i)
            if (!decision_scalar_prepare(p, qa_json_at(doc, writes, i), p->delay_writes + i, error)) return false;
    } else return application_fail(error, QA_ERROR_FORMAT, "Native firing delay has no supported original evaluator");
    qa_json_id ammo = qa_json_get(doc, items, "ammo");
    p->ammo_by_name = qa_json_string_equal(doc, qa_json_get(doc, ammo, "kind"), "name");
    if (!p->ammo_by_name && !qa_json_string_equal(doc, qa_json_get(doc, ammo, "kind"), "index"))
        return application_fail(error, QA_ERROR_FORMAT, "Native firing ammunition has no declared name or index rule");
    if (!word(doc, ammo, "offset", &p->ammo_offset, error) ||
        (p->ammo_by_name && !word(doc, ammo, "label", &p->ammo_label, error))) return false;
    if (p->ammo_offset > p->item_stride || (p->ammo_by_name ? p->pointer_bytes : 4u) > p->item_stride - p->ammo_offset ||
        (p->ammo_by_name && (p->ammo_label > p->item_stride || p->pointer_bytes > p->item_stride - p->ammo_label)))
        return application_fail(error, QA_ERROR_FORMAT, "Native firing ammunition leaves its declared item descriptor");
    p->source_weapons = decision_array(p, p->item_count, sizeof(*p->source_weapons), _Alignof(qa_supply_weapon), error);
    if (!p->source_weapons) return false;
    qa_strings *strings = qa_session_strings(p->engine->provider->application->session);
    if (!qa_strings_intern_cstr(strings, "q2:equipment/hand-grenades", &p->hand_grenades, error)) return false;
    qa_json_id contexts = qa_json_get(doc, weapons, "equipmentContexts");
    if (qa_json_type(doc, contexts) != QA_JSON_ARRAY)
        return application_fail(error, QA_ERROR_FORMAT, "Native firing equipment contexts are not a declared array");
    p->equipment_context_count = qa_json_size(doc, contexts);
    if (p->equipment_context_count) {
        p->equipment_contexts = decision_array(p, p->equipment_context_count, sizeof(*p->equipment_contexts),
            _Alignof(attack_equipment_context), error);
        if (!p->equipment_contexts) return false;
    }
    for (size_t i = 0; i < p->equipment_context_count; ++i) {
        qa_json_id row = qa_json_at(doc, contexts, i); qa_buffer provider = {0}, item = {0};
        bool ok = qa_json_string(doc, qa_json_get(doc, row, "provider"), &provider, error) &&
            qa_json_string(doc, qa_json_get(doc, row, "item"), &item, error) &&
            qa_strings_intern(strings, (qa_bytes){provider.data, provider.size}, &p->equipment_contexts[i].implementation, error) &&
            qa_strings_intern(strings, (qa_bytes){item.data, item.size}, &p->equipment_contexts[i].item, error);
        qa_buffer_free(&provider); qa_buffer_free(&item);
        if (!ok) return false;
    }
    return true;
}
static bool delay_capture(void *opaque, qa_native_instance *native, const qa_native_region_event *event,
    qa_native_region_decision *out, qa_error *error)
{
    struct application_native_q2_attack *p = opaque;
    *out = (qa_native_region_decision){.action = QA_NATIVE_REGION_CONTINUE};
    if (event->phase != QA_NATIVE_REGION_JOIN || !p->dispatch) return true;
    attack_factor *factor = factor_for(p, p->dispatch->actor);
    if (!factor) factor = factor_create(p, p->dispatch->actor, error);
    if (!factor || !decision_scalar(p, p->dispatch->entity, &p->delay_flag, &factor->delay_flag, error)) return false;
    factor->delay_available = true;
    (void)native; return true;
}
bool application_native_q2_attack_prepare(struct application_native_q2 *engine, qa_error *error)
{
    if (!engine->declaration || engine->profile == QA_NATIVE_Q2_CGAME_API2023 ||
        qa_native_declaration_callbacks(engine->declaration).data) return true;
    struct application_native_q2_attack *p = calloc(1, sizeof(*p));
    if (!p) return application_fail(error, QA_ERROR_MEMORY, "Preparing native source attack producer");
    engine->source_attack = p; p->engine = engine;
    const qa_actor_registry *registry = qa_session_actors(engine->provider->application->session);
    if (!qa_actor_index_create(&p->factor_index, registry, NULL, error) ||
        !qa_actor_index_create(&p->projectile_index, registry, NULL, error) ||
        !qa_pool_prepare(&p->factor_pool, &p->factor_storage,
            qa_actors_capacity(registry), sizeof(attack_factor), _Alignof(attack_factor), error) ||
        !qa_pool_reserve_pages(&p->projectile_pool, &p->projectile_storage,
            qa_actors_capacity(registry), sizeof(attack_projectile), _Alignof(attack_projectile), error)) return false;
    if (!qa_json_parse(QA_JSON_STRICT, qa_native_declaration_primary(engine->declaration), &p->document, error)) return false;
    const qa_json_document *doc = p->document;
    qa_json_id root = qa_json_root(doc), commands = qa_json_get(doc, root, "commands"),
        weapons = qa_json_get(doc, root, "weapons"), client = qa_json_get(doc, commands, "client"),
        items = qa_json_get(doc, commands, "items"), source_client = qa_json_get(doc, weapons, "client"),
        entries = qa_json_get(doc, qa_json_get(doc, root, "world"), "entries");
    qa_native_module_info info = qa_native_module_describe(engine->provider->state.native.module);
    p->pointer_bytes = info.image.target.pointer_bytes; p->abi = info.image.target.abi;
    qa_json_id dispatcher=qa_json_get(doc,weapons,"dispatcher");
    if(!qa_json_string_equal(doc,qa_json_get(doc,dispatcher,"record"),"entity") ||
        !qa_json_string_equal(doc,qa_json_get(doc,qa_json_get(doc,dispatcher,"entry"),"kind"),"rva") ||
        !word(doc,qa_json_get(doc,dispatcher,"entry"),"rva",&p->dispatcher_entry,error) ||
        !word(doc,dispatcher,"arguments",&p->dispatcher_count,error) ||
        !word(doc,dispatcher,"argument",&p->dispatcher_argument,error)) return false;
    if(!p->dispatcher_count||p->dispatcher_count>16||p->dispatcher_argument>=p->dispatcher_count)
        return application_fail(error,QA_ERROR_FORMAT,"Native weapon dispatcher actor exceeds its real argument roster");
    for(size_t i=0;i<p->dispatcher_count;++i) p->dispatcher_parameters[i]=scalar(QA_NATIVE_ADDRESS);
    qa_json_id committed=qa_json_get(doc,weapons,"committedInput");
    qa_json_id decisions=qa_json_get(doc,weapons,"decisions");
    p->decision_count=qa_json_size(doc,decisions);
    if(qa_json_type(doc,committed)!=QA_JSON_ARRAY || qa_json_type(doc,decisions)!=QA_JSON_ARRAY || !p->decision_count)
        return application_fail(error,QA_ERROR_FORMAT,"Native weapon dispatcher lacks its actual decision and commitment rosters");
    p->decisions=calloc(p->decision_count,sizeof(*p->decisions));
    if(!p->decisions) return application_fail(error,QA_ERROR_MEMORY,"Retaining qualified original weapon decisions");
    for(size_t i=0;i<p->decision_count;++i) {
        char path[64]; snprintf(path,sizeof(path),"/weapons/decisions/%zu",i);
        qa_native_declared_region region;
        if(!qa_native_declaration_find_region(engine->declaration,path,&region,error)) return false;
        qa_json_id fields=qa_json_get(doc,qa_json_at(doc,decisions,i),"fields");
        if(qa_json_type(doc,fields)!=QA_JSON_ARRAY||!qa_json_size(doc,fields))
            return application_fail(error,QA_ERROR_FORMAT,"Native weapon decision lacks its actual input mask roster");
        p->decisions[i]=(decision_region){.owner=p,
            .field_count=qa_json_size(doc,fields),.id=region.id};
    }
    if (!word(doc, client, "pointer", &p->client_pointer, error) ||
        !word(doc, client, "weapon", &p->weapon, error) ||
        !word(doc, source_client, "byteLength", &p->client_bytes, error) ||
        !word(doc, items, "table", &p->item_table, error) ||
        !word(doc, items, "stride", &p->item_stride, error) ||
        !word(doc, items, "count", &p->item_count, error) ||
        !word(doc, items, "classname", &p->item_classname, error) ||
        !word(doc, items, "flags", &p->item_flags, error) ||
        !word(doc, items, "weaponFlag", &p->weapon_flag, error) ||
        !word(doc, entries, "spawn", &p->spawn, error) || !word(doc, entries, "free", &p->free, error)) return false;
    uint32_t expected = engine->profile == QA_NATIVE_Q2_GAME_API2023 ? 120u : p->pointer_bytes == 4 ? 84u : 88u;
    if (p->client_pointer != expected || !p->item_count || p->item_count > 65536 ||
        !p->item_stride || p->item_stride > 65536 || !p->weapon_flag ||
        p->weapon > p->client_bytes || p->pointer_bytes > p->client_bytes - p->weapon ||
        p->item_classname > p->item_stride || p->pointer_bytes > p->item_stride - p->item_classname ||
        p->item_flags > p->item_stride || p->item_stride - p->item_flags < 4)
        return application_fail(error, QA_ERROR_FORMAT, "Native attack producer differs from its source descriptors");
    p->items = qa_arena_alloc(&p->hook_storage, p->item_count * sizeof(*p->items), _Alignof(attack_item), error);
    if (!p->items || !qa_pool_prepare(&p->hook_pool, &p->hook_storage,
            p->item_count, sizeof(attack_hook), _Alignof(attack_hook), error)) return false;
    memset(p->items, 0, p->item_count * sizeof(*p->items));
    qa_arena_seal(&p->hook_storage);
    if(!delay_prepare(p,weapons,items,error) ||
        !decision_descriptors_prepare(p,weapons,committed,decisions,error)) return false;
    qa_json_id provenance = qa_json_get(doc, weapons, "provenance");
    if (qa_json_type(doc, provenance) != QA_JSON_OBJECT)
        return application_fail(error, QA_ERROR_UNSUPPORTED,
            "Native source weapon callback requires artifact-qualified member metadata");
    if (!word(doc, provenance, "weaponThink", &p->weapon_think, error)) return false;
    if (p->weapon_think > p->item_stride || p->pointer_bytes > p->item_stride - p->weapon_think ||
        p->weapon_think == p->item_classname)
        return application_fail(error, QA_ERROR_FORMAT, "Native weapon callback exceeds its qualified descriptor");
    p->damage = qa_json_get(doc, weapons, "damage");
    if (qa_json_string_equal(doc, qa_json_get(doc, p->damage, "kind"), "source-result")) {
        if (!word(doc, p->damage, "entry", &p->factor_entry, error)) return false;
        qa_json_id type = qa_json_get(doc, p->damage, "result");
        if (qa_json_string_equal(doc, type, "uint8")) p->factor_type = QA_NATIVE_U8;
        else if (qa_json_string_equal(doc, type, "int32")) p->factor_type = QA_NATIVE_I32;
        else return application_fail(error, QA_ERROR_FORMAT, "Native source damage factor has an unsupported original result");
    } else if (qa_json_string_equal(doc, qa_json_get(doc, p->damage, "kind"), "source-flag")) {
        if (!word(doc, p->damage, "address", &p->factor_address, error)) return false;
        if (!qa_json_string_equal(doc, qa_json_get(doc, p->damage, "encoding"), "int32"))
            return application_fail(error, QA_ERROR_UNSUPPORTED, "Native source weapon flag requires its original int32 producer");
        qa_json_id factors = qa_json_get(doc, p->damage, "factors");
        if (qa_json_type(doc, factors) != QA_JSON_ARRAY || !qa_json_size(doc, factors))
            return application_fail(error, QA_ERROR_FORMAT, "Native source weapon factors are absent");
    } else return application_fail(error, QA_ERROR_FORMAT, "Native attack has no original damage-factor producer");
    return true;
}
static bool capture(struct application_native_q2_attack *p, attack_frame *frame, qa_error *error)
{
    if (frame->captured) return true;
    qa_application *app = p->engine->provider->application;
    if (!qa_actors_get(qa_session_actors(app->session), frame->actor))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Native weapon actor retired before firing");
    application_provider *arsenal = application_provider_for(app, frame->actor, QA_ROLE_ARSENAL, NULL);
    if(arsenal!=p->engine->provider && p->dispatch && p->dispatch->committed &&
        qa_actor_id_equal(p->dispatch->actor,frame->actor)) arsenal=p->engine->provider;
    if (arsenal != p->engine->provider)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Native weapon firing requires its selected original arsenal producer");
    application_provider *combat = application_provider_for(app, frame->actor, QA_ROLE_COMBAT, NULL),
        *inventory = application_provider_for(app, frame->actor, QA_ROLE_INVENTORY, NULL),
        *movement = application_provider_for(app, frame->actor, QA_ROLE_MOVEMENT, NULL);
    if (!combat || !inventory || !movement)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Native source attack has an unavailable selected authority");
    double factor;
    if (p->factor_entry) {
        factor = frame->factor;
    } else {
        qa_native_address address; uint8_t bytes[4];
        if (!qa_native_rva(instance(p), p->factor_address, 4, &address, error) ||
            !qa_native_read(instance(p), address, bytes, 4, error)) return false;
        int32_t index = qa_load_i32le(bytes);
        qa_json_id factors = qa_json_get(p->document, p->damage, "factors");
        if (index < 0 || !qa_json_number(p->document, qa_json_at(p->document, factors, (size_t)index), &factor, error)) return false;
    }
    if (!isfinite(factor) || factor < 1)
        return application_fail(error, QA_ERROR_FORMAT, "Original native damage modifier is invalid");
    frame->attack = (qa_attack){.time_ns = p->engine->frame.time_ns, .attacker = frame->actor,
        .inflictor = frame->actor, .weapon = frame->item, .weapon_provider = arsenal->owner,
        .combat_provider = combat->owner, .inventory_provider = inventory->owner,
        .movement_provider = movement->owner, .powerup_applied = factor != 1,
        .powerup_owner = factor != 1 ? p->engine->provider->owner : 0,
        .cause.kind = QA_CAUSE_Q2};
    if (!qa_attack_next(&p->sequence, &frame->attack, error)) return false;
    frame->captured = true; return true;
}
static bool weapon_entry(void *opaque, qa_native_instance *native, qa_native_entry_observer *binding,
    const qa_native_value *arguments, size_t count, qa_native_value *result, qa_error *error)
{
    attack_hook *hook = opaque; struct application_native_q2_attack *p = hook->owner;
    if (count != 1 || arguments[0].type != QA_NATIVE_ADDRESS || !arguments[0].as.address)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original weapon callback lacks its source entity");
    qa_actor_id actor; qa_native_address client, descriptor;
    if (!source_actor(p, arguments[0].as.address, &actor, error) ||
        !pointer_read(p, arguments[0].as.address + p->client_pointer, &client, error)) return false;
    if (!client) return application_fail(error, QA_ERROR_NOT_FOUND, "Original weapon callback has no source client");
    if (!pointer_read(p, client + p->weapon, &descriptor, error)) return false;
    const attack_item *item = NULL;
    for (size_t i = 0; i < p->count; ++i)
        if (p->items[i].descriptor == descriptor && p->items[i].think == hook->address) { item = &p->items[i]; break; }
    if (!item) return application_fail(error, QA_ERROR_ARGUMENT, "Original weapon callback differs from its qualified equipped descriptor");
    attack_frame frame = {.previous = p->current, .actor = actor, .item = item->item};
    if (p->factor_entry) {
        attack_factor *observed = factor_for(p, actor);
        if (!observed || !observed->available)
            return application_fail(error, QA_ERROR_NOT_FOUND, "Original damage modifier did not precede this weapon callback");
        frame.factor = observed->factor; observed->available = false;
    }
    p->current = &frame; ++p->engine->calls;
    bool ok = qa_native_invoke_original(binding, arguments, count, result, error);
    --p->engine->calls; p->current = frame.previous;
    (void)native; return ok;
}
static bool factor_entry(void *opaque, qa_native_instance *native, qa_native_entry_observer *binding,
    const qa_native_value *arguments, size_t count, qa_native_value *result, qa_error *error)
{
    struct application_native_q2_attack *p = opaque;
    if (count != 1 || arguments[0].type != QA_NATIVE_ADDRESS)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original damage modifier lacks its source actor");
    qa_actor_id actor;
    if (!source_actor(p, arguments[0].as.address, &actor, error)) return false;
    attack_factor *factor = factor_for(p, actor);
    bool created = factor == NULL;
    if (!factor) factor = factor_create(p, actor, error);
    if (!factor) return false;
    ++factor->calls; ++p->engine->calls;
    bool ok = qa_native_invoke_original(binding, arguments, count, result, error);
    --p->engine->calls;
    if (ok && result->type != p->factor_type)
        ok = application_fail(error, QA_ERROR_FORMAT, "Original damage modifier changed its result ABI");
    if (ok) {
        double value = result->type == QA_NATIVE_U8 ? result->as.u8 : result->as.i32;
        if (!isfinite(value) || value < 1)
            ok = application_fail(error, QA_ERROR_FORMAT, "Original damage modifier returned an invalid factor");
        else { factor->factor = value; factor->available = true; }
    }
    if (!ok && created) {
        qa_actor_index_remove(&p->factor_index, actor); factor->retired = true;
    }
    --factor->calls; factor_reclaim(p, factor); (void)native; return ok;
}
static bool client_spawn_entry(void *opaque, qa_native_instance *native, qa_native_entry_observer *binding,
    const qa_native_value *arguments, size_t count, qa_native_value *result, qa_error *error)
{
    struct application_native_q2_attack *p = opaque;
    if (count != 1 || arguments[0].type != QA_NATIVE_ADDRESS || !arguments[0].as.address)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original client spawn lacks its Source entity");
    qa_native_address entity = arguments[0].as.address;
    uint32_t slot;
    if (!qa_native_entity_slot(native, entity, &slot, error)) return false;
    qa_actor_id actor = slot < sizeof(p->engine->clients) / sizeof(*p->engine->clients)
        ? p->engine->clients[slot].actor : (qa_actor_id){0};
    ++p->engine->calls;
    bool ok = qa_native_invoke_original(binding, arguments, count, result, error);
    --p->engine->calls;
    qa_application *app = p->engine->provider->application;
    if (!ok || !actor.registry || !qa_actors_get(qa_session_actors(app->session), actor)) return ok;
    qa_actor_id current;
    if (!qa_native_host_source_actor(p->engine->provider->state.native.host, entity, false, &current, error)) return false;
    if (!qa_actor_id_equal(current, actor)) return true;
    bool accepted;
    if (!decision_matches(p, entity, &p->spawn_accepted, &accepted, error)) return false;
    if (!accepted) return true;
    double maximum;
    if (!decision_scalar(p, entity, &p->max_health, &maximum, error) ||
        !application_players_source_arsenal_spawned(p->engine->provider, actor, maximum, error)) return false;
    return !qa_actors_get(qa_session_actors(app->session), actor) ||
        application_native_q2_attack_clock_reset(p->engine, actor, error);
}
static bool spawn_entry(void *opaque, qa_native_instance *native, qa_native_entry_observer *binding,
    const qa_native_value *arguments, size_t count, qa_native_value *result, qa_error *error)
{
    struct application_native_q2_attack *p = opaque; attack_frame *frame = p->current;
    if (frame && !capture(p, frame, error)) return false;
    if (!qa_native_host_source_reconcile(p->engine->provider->state.native.host, error)) return false;
    ++p->engine->calls;
    bool ok = qa_native_invoke_original(binding, arguments, count, result, error);
    if(ok&&(result->type!=QA_NATIVE_ADDRESS||!result->as.address))
        ok=application_fail(error,QA_ERROR_FORMAT,"Original native G_Spawn returned no source entity");
    qa_actor_id actor={0};
    if(ok) {
        ok=qa_native_host_source_birth(p->engine->provider->state.native.host,result->as.address,&actor,error);
    }
    if(ok&&!actor.registry)
        ok=application_fail(error,QA_ERROR_NOT_FOUND,"Original allocation did not publish its actual Source actor");
    if(ok&&frame) {
        size_t slot; attack_projectile *lease=qa_pool_take(&p->projectile_pool,&slot);
        if(ok&&!lease)
            ok=application_fail(error,QA_ERROR_MEMORY,"Original projectile attack pool exhausted");
        if(ok) {
            *lease=(attack_projectile){.storage_slot=slot,.next=p->projectiles,.address=result->as.address,.actor=actor,
                .attack=frame->attack};
            lease->attack.inflictor=actor; lease->attack.projectile=actor;
            if(lease->next)lease->next->previous=lease;
            p->projectiles=lease;
            qa_actor_index_put(&p->projectile_index,actor,slot);
        }
    }
    --p->engine->calls;
    (void)native; return ok;
}
static bool free_entry(void *opaque, qa_native_instance *native, qa_native_entry_observer *binding,
    const qa_native_value *arguments, size_t count, qa_native_value *result, qa_error *error)
{
    struct application_native_q2_attack *p = opaque;
    if (count != 1 || arguments[0].type != QA_NATIVE_ADDRESS)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original G_FreeEdict lacks its source entity");
    ++p->engine->calls;
    bool ok = qa_native_invoke_original(binding, arguments, count, result, error);
    --p->engine->calls;
    if (!ok) return false;
    (void)native;
    return qa_native_host_source_reconcile(p->engine->provider->state.native.host, error);
}
static bool item_identity(struct application_native_q2_attack *p, qa_native_address record,
    qa_item_id *out, qa_error *error)
{
    qa_native_address name; qa_buffer text = {0}; *out = 0;
    if (!pointer_read(p, record + p->item_classname, &name, error)) return false;
    if (!name) return true;
    if (!qa_native_read_string(instance(p), name, 1024, &text, error)) return false;
    bool valid = text.size > 0;
    for (size_t j = 0; j < text.size; ++j)
        valid = valid && ((text.data[j] >= 'a' && text.data[j] <= 'z') ||
            (j && ((text.data[j] >= '0' && text.data[j] <= '9') || text.data[j] == '_')));
    bool ok = true;
    if (valid) {
        char qualified[1030]; snprintf(qualified, sizeof(qualified), "q2:%s", (char *)text.data);
        ok = qa_strings_intern_cstr(qa_session_strings(p->engine->provider->application->session), qualified, out, error);
    }
    qa_buffer_free(&text); return ok;
}
static bool item_ammo(struct application_native_q2_attack *p, attack_item *item, qa_error *error)
{
    qa_native_address descriptor = 0;
    if (!p->ammo_by_name) {
        uint8_t bytes[4];
        if (!qa_native_read(instance(p), item->descriptor + p->ammo_offset, bytes, sizeof(bytes), error)) return false;
        int32_t index = qa_load_i32le(bytes);
        if (!index) return true;
        if (index < 0 || (uint32_t)index >= p->item_count)
            return application_fail(error, QA_ERROR_FORMAT, "Native weapon ammunition index leaves its source table");
        if (!qa_native_rva(instance(p), (uint64_t)p->item_table + (uint64_t)(uint32_t)index * p->item_stride,
            p->item_stride, &descriptor, error)) return false;
    } else {
        qa_native_address name; qa_buffer text = {0};
        if (!pointer_read(p, item->descriptor + p->ammo_offset, &name, error)) return false;
        if (!name) return true;
        if (!qa_native_read_string(instance(p), name, 1024, &text, error)) return false;
        if (!text.size) { qa_buffer_free(&text); return true; }
        bool ok = true;
        for (uint32_t i = 0; ok && i < p->item_count; ++i) {
            qa_native_address record, label; qa_buffer value = {0};
            ok = qa_native_rva(instance(p), (uint64_t)p->item_table + (uint64_t)i * p->item_stride,
                p->item_stride, &record, error) && pointer_read(p, record + p->ammo_label, &label, error);
            if (ok && label) ok = qa_native_read_string(instance(p), label, 1024, &value, error);
            bool matches = ok && label && qa_text_bytes_equal((qa_bytes){text.data, text.size},
                (qa_bytes){value.data, value.size}, true);
            qa_buffer_free(&value);
            if (matches) { descriptor = record; break; }
        }
        qa_buffer_free(&text); if (!ok) return false;
    }
    if (!descriptor) return application_fail(error, QA_ERROR_FORMAT, "Native weapon ammunition name has no source descriptor");
    if (!item_identity(p, descriptor, &item->ammo, error)) return false;
    return item->ammo || application_fail(error, QA_ERROR_FORMAT, "Native weapon ammunition has no source item identity");
}
bool application_native_q2_attack_activate(struct application_native_q2 *engine, qa_error *error)
{
    struct application_native_q2_attack *p = engine->source_attack;
    if (!p || p->active) return true;
    if (!engine->provider->state.native.host || !engine->initialized)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native attack activation requires initialized original item storage");
    if (!p->items_ready) {
        p->count = 0;
        for (uint32_t i = 0; i < p->item_count; ++i) {
            qa_native_address record, think; uint8_t flags[4];
            if (!qa_native_rva(instance(p), (uint64_t)p->item_table + (uint64_t)i * p->item_stride, p->item_stride, &record, error) ||
                !qa_native_read(instance(p), record + p->item_flags, flags, 4, error)) return false;
            if (!(qa_load_u32le(flags) & p->weapon_flag)) continue;
            qa_item_id item;
            if (!pointer_read(p, record + p->weapon_think, &think, error) || !item_identity(p, record, &item, error)) return false;
            if (!item || !think) return application_fail(error, QA_ERROR_FORMAT, "Native weapon descriptor lacks its original name or callback");
            p->items[p->count++] = (attack_item){.descriptor = record, .think = think, .item = item};
        }
        if (p->delay_kind != ATTACK_DELAY_NONE) for (size_t i = 0; i < p->count; ++i) {
            if (!item_ammo(p, p->items + i, error)) return false;
            p->source_weapons[i] = (qa_supply_weapon){p->items[i].item, p->items[i].ammo, true};
        }
        p->items_ready = true;
    }
    for (attack_projectile *lease = p->projectiles; lease; lease = lease->next) {
        bool found = false;
        for (size_t i = 0; i < p->count; ++i) found = found || p->items[i].item == lease->attack.weapon;
        if (!found) return application_fail(error, QA_ERROR_FORMAT, "Restored projectile weapon is outside its actual source descriptors");
    }
    qa_native_type pointer = scalar(QA_NATIVE_ADDRESS);
    qa_native_signature weapon_sig = signature(p, &pointer, 1, QA_NATIVE_VOID);
    if (p->client_spawn_declared && !p->client_spawn_hook) {
        qa_native_address entry;
        if (!qa_native_rva(instance(p), p->client_spawn, 1, &entry, error) ||
            !qa_native_observe_entry(instance(p), entry, &weapon_sig, client_spawn_entry,
                p, &p->client_spawn_hook, error)) return false;
    }
    for (size_t i = 0; i < p->count; ++i) {
        attack_hook *hook = p->weapons;
        while (hook && hook->address != p->items[i].think) hook = hook->next;
        if (!hook) {
            size_t slot; hook = qa_pool_take(&p->hook_pool, &slot);
            if (!hook) return application_fail(error, QA_ERROR_MEMORY, "Retaining native weapon entry lease");
            *hook = (attack_hook){.storage_slot = slot, .owner = p,
                .address = p->items[i].think, .next = p->weapons}; p->weapons = hook;
        }
        if (!hook->binding && !qa_native_observe_entry(instance(p), hook->address, &weapon_sig,
                weapon_entry, hook, &hook->binding, error)) return false;
    }
    qa_native_address entry;
    if(!p->dispatcher_hook) {
        qa_native_signature dispatcher_sig=signature(p,p->dispatcher_parameters,p->dispatcher_count,QA_NATIVE_VOID);
        if(!qa_native_rva(instance(p),p->dispatcher_entry,1,&entry,error)||
            !qa_native_observe_entry(instance(p),entry,&dispatcher_sig,dispatcher_call,p,&p->dispatcher_hook,error)) return false;
    }
    for(size_t i=0;i<p->decision_count;++i) {
        decision_region *region=p->decisions+i;
        if(!region->binding&&!qa_native_bind_region(instance(p),region->id,decision_call,region,&region->binding,error)) return false;
    }
    if (p->delay_kind != ATTACK_DELAY_NONE && !p->delay_hook &&
        !qa_native_bind_region(instance(p), p->delay_region, delay_capture, p, &p->delay_hook, error)) return false;
    if (p->delay_kind == ATTACK_DELAY_ANIMATION && !p->delay_result &&
        !qa_native_allocate(instance(p), 8, INT32_MIN + 14, &p->delay_result, error)) return false;
    if (!p->spawn_hook) {
        qa_native_signature sig = signature(p, NULL, 0, QA_NATIVE_ADDRESS);
        if (!qa_native_rva(instance(p), p->spawn, 1, &entry, error) ||
            !qa_native_observe_entry(instance(p), entry, &sig, spawn_entry, p, &p->spawn_hook, error)) return false;
    }
    if (!p->free_hook && (!qa_native_rva(instance(p), p->free, 1, &entry, error) ||
        !qa_native_observe_entry(instance(p), entry, &weapon_sig, free_entry, p, &p->free_hook, error))) return false;
    if (p->factor_entry && !p->factor_hook) {
        qa_native_signature sig = signature(p, &pointer, 1, p->factor_type);
        if (!qa_native_rva(instance(p), p->factor_entry, 1, &entry, error) ||
            !qa_native_observe_entry(instance(p), entry, &sig, factor_entry, p, &p->factor_hook, error)) return false;
    }
    p->active = true; return true;
}
bool application_native_q2_attack_read(struct application_native_q2 *engine, qa_actor_id attacker,
    qa_actor_id inflictor, qa_actor_id target, bool weapon_damage, qa_attack *out, qa_error *error)
{
    struct application_native_q2_attack *p = engine->source_attack;
    qa_application *app = engine->provider->application;
    if (!p || !p->active || !out || !qa_actors_get(qa_session_actors(app->session), target))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native damage requires its live original attack producer");
    attack_projectile *lease = projectile_for(p, inflictor);
    if (lease) {
        if (!qa_actors_get(qa_session_actors(app->session), lease->actor) ||
            !qa_actor_id_equal(lease->attack.attacker, attacker))
            return application_fail(error, QA_ERROR_NOT_FOUND, "Original projectile provenance changed its source actor");
        *out = lease->attack; return true;
    }
    if (p->current && qa_actor_id_equal(p->current->actor, attacker)) {
        if (!capture(p, p->current, error)) return false;
        *out = p->current->attack; out->inflictor = inflictor; return true;
    }
    if (weapon_damage)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Native weapon damage lacks its original fire or projectile provenance");
    application_provider *combat = application_provider_for(app, target, QA_ROLE_COMBAT, NULL),
        *inventory = application_provider_for(app, target, QA_ROLE_INVENTORY, NULL),
        *movement = application_provider_for(app, target, QA_ROLE_MOVEMENT, NULL);
    if (!combat || !inventory || !movement)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Native environmental attack has unavailable selected authority");
    qa_attack attack = {.time_ns = engine->frame.time_ns, .attacker = attacker, .inflictor = inflictor,
        .weapon_provider = engine->provider->owner, .combat_provider = combat->owner,
        .inventory_provider = inventory->owner, .movement_provider = movement->owner,
        .cause.kind = QA_CAUSE_Q2};
    if (!qa_attack_next(&p->sequence, &attack, error)) return false;
    *out = attack; return true;
}
bool application_native_q2_attack_weapon_read(struct application_native_q2 *engine, uint32_t slot,
    qa_actor_id actor, qa_item_id *out, qa_error *error)
{
    struct application_native_q2_attack *p = engine ? engine->source_attack : NULL;
    if (!p || !p->items_ready || !engine->provider->state.native.host || !out ||
        !slot || slot >= 257 || !engine->clients[slot].connected || !engine->clients[slot].begun ||
        engine->clients[slot].disconnect_started || !qa_actor_id_equal(engine->clients[slot].actor, actor) ||
        !qa_actors_get(qa_session_actors(engine->provider->application->session), actor) ||
        (!application_native_q2_idle(engine->provider) &&
            !application_native_q2_inventory_scanner_command_event(engine->inventory_scanner, actor)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native selected weapon requires its prepared physical client and item roster");
    qa_native_slot_binding binding;
    qa_native_address entity, client, descriptor;
    if (!qa_native_slot(instance(p), slot, &binding, error)) return false;
    if (binding.kind == QA_NATIVE_SLOT_FREE || !qa_actor_id_equal(binding.actor, actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native selected weapon source binding changed its actor");
    if (!qa_native_entity_address(instance(p), slot, &entity, error)) return false;
    if (entity > UINT64_MAX - p->client_pointer)
        return application_fail(error, QA_ERROR_FORMAT, "Native selected weapon client pointer address overflows");
    if (!pointer_read(p, entity + p->client_pointer, &client, error)) return false;
    if (!client || client > UINT64_MAX - p->weapon)
        return application_fail(error, QA_ERROR_FORMAT, "Native selected weapon has no admitted client address");
    if (!pointer_read(p, client + p->weapon, &descriptor, error)) return false;
    if (!descriptor) { *out = 0; return true; }
    const attack_item *selected = NULL;
    for (size_t i = 0; i < p->count; ++i) if (p->items[i].descriptor == descriptor) {
        if (selected) return application_fail(error, QA_ERROR_FORMAT, "Native selected weapon repeats its original descriptor");
        selected = p->items + i;
    }
    if (!selected || !selected->item)
        return application_fail(error, QA_ERROR_FORMAT, "Native selected weapon is outside its qualified canonical roster");
    *out = selected->item;
    return true;
}
bool application_native_q2_attack_weapon_contains(struct application_native_q2 *engine,
    qa_item_id item, bool *out, qa_error *error)
{
    const struct application_native_q2_attack *p = engine ? engine->source_attack : NULL;
    if (!p || !p->items_ready || !out || !item || !engine->provider->state.native.host ||
        qa_native_get_module(instance((struct application_native_q2_attack *)p)) != engine->provider->state.native.module ||
        qa_native_terminal(instance((struct application_native_q2_attack *)p)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native weapon membership requires its actual prepared source roster");
    *out = false;
    for (size_t i = 0; i < p->count; ++i) if (p->items[i].item == item) {
        if (*out) return application_fail(error, QA_ERROR_FORMAT, "Native weapon roster repeats its actual item identity");
        *out = true;
    }
    return true;
}
bool application_native_q2_attack_item_read(struct application_native_q2 *engine, uint32_t slot,
    qa_actor_id actor, qa_native_address descriptor, qa_item_id *out, qa_error *error)
{
    qa_item_id equipped;
    if (!out || !application_native_q2_attack_weapon_read(engine, slot, actor, &equipped, error)) return false;
    if (!descriptor) { *out = 0; return true; }
    const struct application_native_q2_attack *p = engine->source_attack;
    const attack_item *selected = NULL;
    for (size_t i = 0; i < p->count; ++i) if (p->items[i].descriptor == descriptor) {
        if (selected) return application_fail(error, QA_ERROR_FORMAT,
            "Native pending weapon repeats its original descriptor");
        selected = p->items + i;
    }
    if (!selected || !selected->item) return application_fail(error, QA_ERROR_FORMAT,
        "Native pending weapon is outside its actual prepared item roster");
    *out = selected->item;
    return true;
}
const qa_json_document *application_native_q2_attack_declaration_read(const struct application_native_q2 *engine)
{
    const struct application_native_q2_attack *p = engine ? engine->source_attack : NULL;
    return p && p->engine == engine && p->items_ready ? p->document : NULL;
}
bool application_native_q2_attack_descriptor_item(struct application_native_q2 *engine,
    qa_native_address descriptor,qa_item_id *out,qa_error *error)
{
    const struct application_native_q2_attack *p=engine?engine->source_attack:NULL;
    if(!p||p->engine!=engine||!p->items_ready||!out||!engine->provider->state.native.host||
        qa_native_get_module(instance((struct application_native_q2_attack *)p))!=engine->provider->state.native.module)
        return application_fail(error,QA_ERROR_ARGUMENT,"Native descriptor lookup lost its actual prepared roster");
    *out=0;
    for(size_t i=0;i<p->count;++i) if(p->items[i].descriptor==descriptor) {
        if(*out) return application_fail(error,QA_ERROR_FORMAT,"Native descriptor repeats its actual admitted identity");
        *out=p->items[i].item;
    }
    return true;
}
static bool delay_item(struct application_native_q2_attack *p, qa_actor_id actor,
    qa_item_id requested, qa_unified_frame_lease *storage, const attack_item **out, qa_error *error)
{
    *out = NULL;
    if (!requested) return true;
    for (size_t i = 0; i < p->count; ++i)
        if (p->items[i].item == requested) { *out = p->items + i; return true; }
    qa_application *app = p->engine->provider->application;
    application_provider *arsenal = application_provider_for(app, actor, QA_ROLE_ARSENAL, NULL);
    if (!arsenal) return application_fail(error, QA_ERROR_NOT_FOUND, "Native firing delay has no selected arsenal");
    qa_item_definition definition;
    if (!qa_inventory_source_definition_read(app->inventory, actor, arsenal->owner, requested, &definition, error)) return false;
    qa_supply_weapon selected = {requested, definition.ammo,
        application_supplies_weapon_droppable(arsenal, requested)};
    qa_item_id source;
    if (!application_supplies_weapon_sources(app->supplies, p->engine->provider, actor,
        &selected, 1, p->source_weapons, p->count, qa_unified_frame_lease_arena(storage), &source, error)) return false;
    if (!source) return true;
    for (size_t i = 0; i < p->count; ++i)
        if (p->items[i].item == source) { *out = p->items + i; return true; }
    return application_fail(error, QA_ERROR_NOT_FOUND, "Native firing delay lost its mapped original item");
}
typedef struct attack_delay_saved {
    qa_native_address address;
    uint8_t bytes[8];
    size_t count;
} attack_delay_saved;
static bool delay_animation(struct application_native_q2_attack *p, qa_native_address entity,
    const attack_factor *factor, qa_unified_frame_lease *storage, double *out, qa_error *error)
{
    size_t count = p->delay_projection_count + p->delay_write_count + 1, saved_count = 0;
    attack_delay_saved *saved = qa_unified_frame_lease_alloc(storage, count, sizeof(*saved),
        _Alignof(attack_delay_saved), error);
    if (!saved) return false;
    bool ok = true;
    for (size_t i = 0; ok && i < count; ++i) {
        const decision_field *field = i < p->delay_projection_count ? &p->delay_projection[i].field :
            i - p->delay_projection_count < p->delay_write_count ?
                p->delay_writes + i - p->delay_projection_count : &p->delay_flag;
        saved[i].count = field->bytes;
        ok = decision_field_address(p, entity, field, &saved[i].address, error) &&
            qa_native_read(instance(p), saved[i].address, saved[i].bytes, saved[i].count, error);
        if (ok) ++saved_count;
    }
    for (size_t i = 0; ok && i < p->delay_projection_count; ++i)
        ok = application_native_q2_scalar_write(instance(p), saved[i].address,
            p->delay_projection[i].field.encoding, p->delay_projection[i].value, error);
    if (ok) ok = application_native_q2_scalar_write(instance(p), saved[count - 1].address,
        p->delay_flag.encoding, factor->delay_flag, error);
    if (ok) {
        qa_native_address entry;
        qa_native_type parameters[] = {scalar(QA_NATIVE_ADDRESS), scalar(QA_NATIVE_ADDRESS)};
        qa_native_signature sig = signature(p, parameters, 2, QA_NATIVE_ADDRESS);
        qa_native_value arguments[] = {{.type = QA_NATIVE_ADDRESS, .as.address = p->delay_result},
            {.type = QA_NATIVE_ADDRESS, .as.address = entity}}, ignored;
        uint8_t bytes[8];
        ok = qa_native_rva(instance(p), p->delay_entry, 1, &entry, error) &&
            qa_native_invoke(instance(p), entry, &sig, arguments, 2, &ignored, error) &&
            qa_native_read(instance(p), p->delay_result, bytes, sizeof(bytes), error);
        if (ok) {
            uint64_t bits = qa_load_u64le(bytes); int64_t milliseconds;
            memcpy(&milliseconds, &bits, sizeof(milliseconds));
            if (milliseconds < 0 || milliseconds > INT64_C(9007199254740991))
                ok = application_fail(error, QA_ERROR_FORMAT, "Original animation delay exceeds its exact nonnegative interval");
            else *out = (double)milliseconds / p->delay_baseline;
        }
    }
    for (size_t i = saved_count; i > 0; --i) {
        qa_error cleanup = {0};
        if (!qa_native_write(instance(p), saved[i - 1].address,
            (qa_bytes){saved[i - 1].bytes, saved[i - 1].count}, &cleanup)) {
            if (ok && error) *error = cleanup;
            ok = false;
        }
    }
    return ok;
}
bool application_native_q2_attack_delay(struct application_native_q2 *engine, qa_actor_id actor,
    qa_item_id item, uint64_t native_ns, uint64_t *out, bool *handled, qa_error *error)
{
    struct application_native_q2_attack *p = engine ? engine->source_attack : NULL;
    *handled = false;
    if (!p || !p->active || p->delay_kind == ATTACK_DELAY_NONE) return true;
    attack_factor *factor = factor_for(p, actor);
    if (!factor || !factor->delay_available)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Original firing modifier has not run for this actor");
    qa_native_host *host = engine->provider->state.native.host;
    uint32_t slot; qa_native_address entity;
    if (!qa_native_host_slot_for_actor(host, actor, &slot))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Native firing modifier lost its original actor");
    if (!qa_native_entity_address(instance(p), slot, &entity, error)) return false;
    qa_unified_frame_lease *storage = application_control_storage_acquire(engine->provider->application, error);
    if (!storage) return false;
    const attack_item *original = NULL;
    bool ok = delay_item(p, actor, item, storage, &original, error), projected = false;
    qa_native_address weapon = 0; uint8_t previous[8];
    if (ok && original) {
        qa_native_address client;
        ok = pointer_read(p, entity + p->client_pointer, &client, error);
        if (ok && !client) ok = application_fail(error, QA_ERROR_NOT_FOUND, "Native firing delay has no original client");
        if (ok) {
            weapon = client + p->weapon;
            ok = qa_native_read(instance(p), weapon, previous, p->pointer_bytes, error);
        }
        if (ok) {
            uint8_t bytes[8]; qa_store_u64le(bytes, original->descriptor);
            projected = true;
            ok = qa_native_write(instance(p), weapon, (qa_bytes){bytes, p->pointer_bytes}, error);
        }
    }
    double multiplier = 0;
    ++factor->calls; ++engine->calls;
    if (ok && p->delay_kind == ATTACK_DELAY_FLAG) {
        double index = factor->delay_flag;
        if (index < 0 || index >= (double)p->delay_factor_count || trunc(index) != index)
            ok = application_fail(error, QA_ERROR_FORMAT, "Original firing flag has no declared source factor");
        else multiplier = p->delay_factors[(size_t)index];
    } else if (ok) ok = delay_animation(p, entity, factor, storage, &multiplier, error);
    if (projected) {
        qa_error cleanup = {0};
        if (!qa_native_write(instance(p), weapon, (qa_bytes){previous, p->pointer_bytes}, &cleanup)) {
            if (ok && error) *error = cleanup;
            ok = false;
        }
    }
    if (ok) {
        long double interval = (long double)native_ns * (long double)multiplier;
        if (!isfinite(multiplier) || multiplier < 0 || interval >= 18446744073709551616.0L)
            ok = application_fail(error, QA_ERROR_FORMAT, "Original firing delay exceeds the shared interval");
        else { *out = (uint64_t)interval; *handled = true; }
    }
    --engine->calls; --factor->calls; factor_reclaim(p, factor);
    qa_unified_frame_lease_release(storage); return ok;
}
bool application_native_q2_attack_hand_grenade_item(struct application_native_q2 *engine,
    qa_item_id *out, qa_error *error)
{
    struct application_native_q2_attack *p = engine ? engine->source_attack : NULL;
    *out = 0;
    if (!p || p->delay_kind == ATTACK_DELAY_NONE) return true;
    for (size_t i = 0; i < p->equipment_context_count; ++i)
        if (p->equipment_contexts[i].implementation == p->hand_grenades) {
            *out = p->equipment_contexts[i].item; return true;
        }
    return application_fail(error, QA_ERROR_NOT_FOUND, "Original source has no declared hand-grenade item context");
}
void application_native_q2_attack_released(struct application_native_q2 *engine, qa_actor_id actor)
{
    struct application_native_q2_attack *p = engine->source_attack;
    if (!p) return;
    attack_projectile *lease = projectile_for(p, actor);
    if (lease) projectile_release(p, lease);
    attack_factor *factor = factor_for(p, actor);
    if (factor) {
        qa_actor_index_remove(&p->factor_index, actor); factor->retired = true;
        factor_reclaim(p, factor);
    }
}
bool application_native_q2_attack_suspend(struct application_native_q2 *engine, qa_error *error)
{
    struct application_native_q2_attack *p = engine->source_attack;
    if (!p) return true;
    if (p->current||p->dispatch||p->projections) return application_fail(error, QA_ERROR_ARGUMENT, "Native attack close requires drained original weapon calls");
    p->active = false;
    for (attack_hook *hook = p->weapons; hook; hook = hook->next) {
        if (hook->binding && !qa_native_unobserve_entry(hook->binding, error)) return false;
        hook->binding = NULL;
    }
    for(size_t i=0;i<p->decision_count;++i) {
        decision_region *region=p->decisions+i;
        if(region->binding&&!qa_native_remove_region(region->binding,error)) return false;
        region->binding=NULL;
    }
    if (p->delay_hook && !qa_native_remove_region(p->delay_hook, error)) return false;
    p->delay_hook = NULL;
    if (p->delay_result && !qa_native_free(instance(p), p->delay_result, error)) return false;
    p->delay_result = 0;
    for (attack_factor *factor = p->factors; factor; factor = factor->next) {
        factor->delay_available = false;
        factor->clock_present = false;
    }
    qa_native_entry_observer **bindings[] = {&p->factor_hook, &p->spawn_hook, &p->free_hook,
        &p->dispatcher_hook, &p->client_spawn_hook};
    for (size_t i = 0; i < sizeof(bindings) / sizeof(*bindings); ++i) {
        if (*bindings[i] && !qa_native_unobserve_entry(*bindings[i], error)) return false;
        *bindings[i] = NULL;
    }
    while (p->weapons) { attack_hook *next = p->weapons->next;
        qa_pool_release(&p->hook_pool, p->weapons->storage_slot); p->weapons = next; }
    return true;
}
bool application_native_q2_attack_close(struct application_native_q2 *engine, qa_error *error)
{
    struct application_native_q2_attack *p = engine->source_attack;
    if (!p) return true;
    if (!application_native_q2_attack_suspend(engine, error)) return false;
    qa_actor_index_destroy(&p->factor_index); qa_actor_index_destroy(&p->projectile_index);
    qa_arena_destroy(&p->factor_storage);
    qa_arena_destroy(&p->projectile_storage);
    qa_arena_destroy(&p->hook_storage);
    qa_arena_destroy(&p->decision_storage);
    qa_json_destroy(p->document); free(p->decisions); free(p); engine->source_attack = NULL;
    return true;
}

struct application_native_q2_attack_restore {
    uint64_t sequence;
    attack_factor *factors;
    attack_projectile *projectiles;
    qa_actor_index factor_index, projectile_index;
    qa_pool factor_pool, projectile_pool;
    qa_arena factor_storage, projectile_storage;
};
static bool attack_fields(qa_source_save_io *io, qa_attack *attack)
{
    /* The common codec preserves original actor history. Source identities
     * additionally use text so numeric string IDs are never portable state. */
    return qa_persistence_attack(io, attack) && qa_source_save_string(io, &attack->weapon) &&
        qa_source_save_string(io, &attack->weapon_provider) && qa_source_save_string(io, &attack->combat_provider) &&
        qa_source_save_string(io, &attack->inventory_provider) && qa_source_save_string(io, &attack->movement_provider) &&
        qa_source_save_string(io, &attack->powerup_owner);
}
bool application_native_q2_attack_capture(struct application_native_q2 *engine, qa_buffer *out, qa_error *error)
{
    struct application_native_q2_attack *p = engine->source_attack;
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Native attack continuation output is absent");
    if (!p) { *out = (qa_buffer){0}; return true; }
    if (p->current)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native attack continuation requires drained firing calls");
    qa_source_save_io io = {0};
    if (!qa_source_save_writer(&io, engine->provider->application->session, error)) return false;
    size_t factors = 0, projectiles = 0;
    for (attack_factor *row = p->factors; row; row = row->next) ++factors;
    for (attack_projectile *row = p->projectiles; row; row = row->next) ++projectiles;
    uint64_t sequence = p->sequence;
    bool ok = qa_source_save_u64(&io, &sequence) &&
        qa_source_save_count(&io, &factors, 65536);
    for (attack_factor *row = p->factors; ok && row; row = row->next) {
        qa_actor_id actor = row->actor; double factor = row->factor; bool available = row->available;
        ok = qa_source_save_actor(&io, &actor) && qa_source_save_f64(&io, &factor) &&
            qa_source_save_bool(&io, &available);
    }
    if (ok) ok = qa_source_save_count(&io, &projectiles, 65536);
    for (attack_projectile *row = p->projectiles; ok && row; row = row->next) {
        uint32_t slot; qa_native_slot_binding binding; qa_actor_id actor = row->actor;
        qa_attack attack = row->attack;
        ok = qa_native_entity_slot(instance(p), row->address, &slot, error) &&
            qa_native_slot(instance(p), slot, &binding, error);
        if (ok && !qa_actor_id_equal(binding.actor, actor))
            ok = application_fail(error, QA_ERROR_NOT_FOUND, "Native projectile source generation changed before capture");
        if (ok) ok = qa_source_save_u32(&io, &slot) && qa_source_save_actor(&io, &actor) && attack_fields(&io, &attack);
    }
    if (ok) ok = qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return ok;
}
void application_native_q2_attack_restore_abort(struct application_native_q2_attack_restore *prepared)
{
    if (!prepared) return;
    qa_actor_index_destroy(&prepared->factor_index); qa_actor_index_destroy(&prepared->projectile_index);
    qa_arena_destroy(&prepared->factor_storage);
    qa_arena_destroy(&prepared->projectile_storage);
    free(prepared);
}
bool application_native_q2_attack_restore_prepare(struct application_native_q2 *engine, qa_bytes bytes,
    struct application_native_q2_attack_restore **out, qa_error *error)
{
    struct application_native_q2_attack *p = engine->source_attack;
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Native attack restore output is absent");
    if (!p) {
        if (bytes.size) return application_fail(error, QA_ERROR_FORMAT, "Native attack continuation has no qualified owner");
        *out = NULL; return true;
    }
    if (p->active || p->current || p->weapons || p->spawn_hook || p->free_hook || p->factor_hook ||
        p->client_spawn_hook || !engine->provider->state.native.host)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native attack restore requires retired observers and real restored source storage");
    struct application_native_q2_attack_restore *prepared = calloc(1, sizeof(*prepared));
    if (!prepared) return application_fail(error, QA_ERROR_MEMORY, "Preparing native attack continuation");
    qa_source_save_io io = {0};
    const qa_actor_registry *registry = qa_session_actors(engine->provider->application->session);
    bool ok = qa_actor_index_create(&prepared->factor_index, registry, NULL, error) &&
        qa_actor_index_create(&prepared->projectile_index, registry, NULL, error) &&
        qa_pool_prepare(&prepared->factor_pool, &prepared->factor_storage,
            qa_actors_capacity(registry), sizeof(attack_factor), _Alignof(attack_factor), error) &&
        qa_pool_reserve_pages(&prepared->projectile_pool, &prepared->projectile_storage,
            qa_actors_capacity(registry), sizeof(attack_projectile), _Alignof(attack_projectile), error) &&
        qa_source_save_reader(&io, engine->provider->application->session, bytes, error);
    size_t count = 0;
    if (ok) ok = qa_source_save_u64(&io, &prepared->sequence) && qa_source_save_count(&io, &count, 65536);
    for (size_t i = 0; ok && i < count; ++i) {
        if (prepared->factor_pool.head == SIZE_MAX && !qa_pool_extend(&prepared->factor_pool,
                &prepared->factor_storage, prepared->factor_pool.first_capacity ? prepared->factor_pool.first_capacity : 1, error)) {
            ok = false; break;
        }
        size_t storage_slot; attack_factor *row = qa_pool_take(&prepared->factor_pool, &storage_slot);
        if (!row) { ok = application_fail(error, QA_ERROR_MEMORY, "Preparing native modifier continuation"); break; }
        *row = (attack_factor){.storage_slot = storage_slot, .next = prepared->factors};
        if (row->next) row->next->previous = row;
        prepared->factors = row;
        ok = qa_source_save_actor(&io, &row->actor) && qa_source_save_f64(&io, &row->factor) &&
            qa_source_save_bool(&io, &row->available);
        if (ok && (!row->actor.registry || !qa_actors_get(qa_session_actors(engine->provider->application->session), row->actor) ||
            !isfinite(row->factor) || row->factor < 1))
            ok = application_fail(error, QA_ERROR_FORMAT, "Native modifier continuation has invalid source authority");
        size_t prior;
        if (ok && qa_actor_index_find(&prepared->factor_index, row->actor, &prior))
            ok = application_fail(error, QA_ERROR_FORMAT, "Native modifier continuation repeats an actor");
        if (ok) qa_actor_index_put(&prepared->factor_index, row->actor, (size_t)(uintptr_t)row);
    }
    if (ok) ok = qa_source_save_count(&io, &count, 65536);
    for (size_t i = 0; ok && i < count; ++i) {
        size_t storage_slot; attack_projectile *row = qa_pool_take(&prepared->projectile_pool, &storage_slot);
        uint32_t slot = 0;
        if (!row) { ok = application_fail(error, QA_ERROR_MEMORY, "Preparing original projectile continuation"); break; }
        *row = (attack_projectile){.storage_slot = storage_slot, .next = prepared->projectiles};
        if (row->next) row->next->previous = row;
        prepared->projectiles = row;
        ok = qa_source_save_u32(&io, &slot) && qa_source_save_actor(&io, &row->actor) && attack_fields(&io, &row->attack);
        qa_native_slot_binding binding;
        if (ok) ok = qa_native_slot(instance(p), slot, &binding, error) &&
            qa_native_entity_address(instance(p), slot, &row->address, error);
        if (ok && (!row->actor.registry || !qa_actor_id_equal(binding.actor, row->actor) ||
            !qa_actors_get(qa_session_actors(engine->provider->application->session), row->actor) ||
            !qa_actor_id_equal(row->attack.inflictor, row->actor) || !qa_actor_id_equal(row->attack.projectile, row->actor) ||
            !row->attack.sequence || row->attack.sequence > prepared->sequence || !row->attack.weapon ||
            row->attack.weapon_provider != engine->provider->owner ||
            (row->attack.powerup_applied ? row->attack.powerup_owner != engine->provider->owner : row->attack.powerup_owner != 0) ||
            row->attack.cause.kind != QA_CAUSE_Q2))
            ok = application_fail(error, QA_ERROR_FORMAT, "Original projectile continuation differs from its source lifetime");
        size_t prior;
        if (ok && qa_actor_index_find(&prepared->projectile_index, row->actor, &prior))
            ok = application_fail(error, QA_ERROR_FORMAT, "Original projectile continuation repeats a source slot");
        if (ok) qa_actor_index_put(&prepared->projectile_index, row->actor, storage_slot);
    }
    if (ok) ok = qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!ok) { if (error && error->code == QA_OK) application_fail(error, QA_ERROR_FORMAT, "Invalid native attack continuation");
        application_native_q2_attack_restore_abort(prepared); return false; }
    *out = prepared; return true;
}
void application_native_q2_attack_restore_commit(struct application_native_q2 *engine,
    struct application_native_q2_attack_restore *prepared)
{
    if (!prepared) return;
    struct application_native_q2_attack *p = engine->source_attack;
    qa_actor_index_destroy(&p->factor_index); qa_actor_index_destroy(&p->projectile_index);
    qa_arena_destroy(&p->factor_storage);
    qa_arena_destroy(&p->projectile_storage);
    p->sequence = prepared->sequence; p->factors = prepared->factors; p->projectiles = prepared->projectiles;
    p->factor_index = prepared->factor_index; p->projectile_index = prepared->projectile_index;
    p->factor_pool = prepared->factor_pool;
    p->factor_storage = prepared->factor_storage;
    p->projectile_pool = prepared->projectile_pool;
    p->projectile_storage = prepared->projectile_storage;
    free(prepared);
    /* Raw descriptor pointers belong to the candidate image and are resolved
     * again before observers are rebound; none are saved or transplanted. */
    p->count = 0; p->items_ready = false;
}
