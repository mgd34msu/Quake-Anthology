#include "guest_native_q2_private.h"
#include "guest_native_q2_attack.h"
#include "qa/native_observe.h"
#include "qa/persistence_fields.h"
#include <math.h>

typedef struct attack_item {
    qa_native_address descriptor, think;
    qa_item_id item;
} attack_item;
typedef struct attack_factor {
    struct attack_factor *next;
    qa_actor_id actor;
    double factor;
    bool available;
} attack_factor;
typedef struct attack_projectile {
    struct attack_projectile *next;
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
    struct application_native_q2_attack *owner;
    qa_native_entry_observer *binding;
    qa_native_address address;
} attack_hook;
struct application_native_q2_attack {
    struct application_native_q2 *engine;
    qa_json_document *document;
    qa_json_id damage;
    uint32_t client_pointer, weapon, client_bytes, item_table, item_stride,
        item_count, item_classname, item_flags, weapon_flag, weapon_think;
    uint32_t spawn, free, factor_entry, factor_address;
    uint8_t pointer_bytes;
    qa_native_value_type factor_type;
    qa_native_abi abi;
    attack_item *items;
    size_t count;
    attack_hook *weapons;
    qa_native_entry_observer *spawn_hook, *free_hook, *factor_hook;
    attack_factor *factors;
    attack_projectile *projectiles;
    attack_frame *current;
    uint64_t sequence;
    bool active, items_ready;
};
static bool word(const qa_json_document *doc, qa_json_id object, const char *name,
    uint32_t *out, qa_error *error)
{
    uint64_t value;
    if (!qa_json_u64(doc, qa_json_get(doc, object, name), &value, error)) return false;
    if (value > UINT32_MAX) return application_fail(error, QA_ERROR_FORMAT, "Native attack source field exceeds uint32");
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
{ return (qa_native_signature){p->abi, parameters, count, scalar(result), false}; }
static bool source_actor(struct application_native_q2_attack *p, qa_native_address address,
    qa_actor_id *out, qa_error *error)
{
    if (!qa_native_host_source_actor(p->engine->provider->state.native.host, address, true, out, error)) return false;
    return out->registry != 0 || application_fail(error, QA_ERROR_NOT_FOUND, "Native attack actor is not in use");
}
static attack_factor *factor_for(struct application_native_q2_attack *p, qa_actor_id actor)
{
    for (attack_factor *factor = p->factors; factor; factor = factor->next)
        if (qa_actor_id_equal(factor->actor, actor)) return factor;
    return NULL;
}
bool application_native_q2_attack_prepare(struct application_native_q2 *engine, qa_error *error)
{
    if (!engine->declaration || engine->profile == QA_NATIVE_Q2_CGAME_API2023 ||
        qa_native_declaration_callbacks(engine->declaration).data) return true;
    struct application_native_q2_attack *p = calloc(1, sizeof(*p));
    if (!p) return application_fail(error, QA_ERROR_MEMORY, "Preparing native source attack producer");
    engine->source_attack = p; p->engine = engine;
    if (!qa_json_parse(qa_native_declaration_primary(engine->declaration), &p->document, error)) return false;
    const qa_json_document *doc = p->document;
    qa_json_id root = qa_json_root(doc), commands = qa_json_get(doc, root, "commands"),
        weapons = qa_json_get(doc, root, "weapons"), client = qa_json_get(doc, commands, "client"),
        items = qa_json_get(doc, commands, "items"), source_client = qa_json_get(doc, weapons, "client"),
        entries = qa_json_get(doc, qa_json_get(doc, root, "world"), "entries");
    qa_native_module_info info = qa_native_module_describe(engine->provider->state.native.module);
    p->pointer_bytes = info.image.target.pointer_bytes; p->abi = info.image.target.abi;
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
    qa_json_id provenance = qa_json_get(doc, weapons, "provenance");
    if (qa_json_type(doc, provenance) == QA_JSON_OBJECT) {
        if (!word(doc, provenance, "weaponThink", &p->weapon_think, error)) return false;
    } else {
        qa_sha256_digest classic, kex;
        if (!qa_sha256_parse("8187df3fd5b4d435d8227434d3351aad2b47e546236403e52adcd4d275810c45", &classic, error) ||
            !qa_sha256_parse("045d49c53722d9b922caf14f168dd28a97d4c514a6e443a3140560f8668baccd", &kex, error)) return false;
        /* Exact original gitem prefix, not an inferred private mod layout:
         * classic classname/pickup/use/drop/weaponthink; KEX id then those. */
        if (engine->profile == QA_NATIVE_Q2_GAME_API3 && p->pointer_bytes == 4 &&
            qa_sha256_equal(&info.image.digest, &classic) && p->item_classname == 0 && p->item_stride == 76)
            p->weapon_think = 16;
        else if (engine->profile == QA_NATIVE_Q2_GAME_API2023 && p->pointer_bytes == 8 &&
            qa_sha256_equal(&info.image.digest, &kex) && p->item_classname == 8 && p->item_stride == 192)
            p->weapon_think = 40;
        else return application_fail(error, QA_ERROR_UNSUPPORTED, "Native source weapon callback requires artifact-qualified member metadata");
    }
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
    ++p->engine->calls;
    bool ok = qa_native_invoke_original(binding, arguments, count, result, error);
    --p->engine->calls;
    if (!ok) return false;
    if (result->type != p->factor_type)
        return application_fail(error, QA_ERROR_FORMAT, "Original damage modifier changed its result ABI");
    double value = result->type == QA_NATIVE_U8 ? result->as.u8 : result->as.i32;
    if (!isfinite(value) || value < 1)
        return application_fail(error, QA_ERROR_FORMAT, "Original damage modifier returned an invalid factor");
    attack_factor *factor = factor_for(p, actor);
    if (!factor) { factor = calloc(1, sizeof(*factor));
        if (!factor) return application_fail(error, QA_ERROR_MEMORY, "Retaining original weapon modifier result");
        factor->actor = actor; factor->next = p->factors; p->factors = factor; }
    factor->factor = value; factor->available = true; (void)native; return true;
}
static void forget_address(struct application_native_q2_attack *p, qa_native_address address)
{
    attack_projectile **link = &p->projectiles;
    while (*link) {
        attack_projectile *lease = *link;
        if (lease->address != address) { link = &lease->next; continue; }
        *link = lease->next; free(lease);
    }
}
static bool spawn_entry(void *opaque, qa_native_instance *native, qa_native_entry_observer *binding,
    const qa_native_value *arguments, size_t count, qa_native_value *result, qa_error *error)
{
    struct application_native_q2_attack *p = opaque; attack_frame *frame = p->current;
    if (frame && !capture(p, frame, error)) return false;
    if (!qa_native_host_source_reconcile(p->engine->provider->state.native.host, error)) return false;
    ++p->engine->calls;
    bool ok = qa_native_invoke_original(binding, arguments, count, result, error);
    --p->engine->calls;
    if (!ok) return false;
    if (result->type != QA_NATIVE_ADDRESS || !result->as.address)
        return application_fail(error, QA_ERROR_FORMAT, "Original native G_Spawn returned no source entity");
    forget_address(p, result->as.address);
    if (!frame) return true;
    qa_actor_id actor;
    if (!source_actor(p, result->as.address, &actor, error)) return false;
    attack_projectile *lease = calloc(1, sizeof(*lease));
    if (!lease) return application_fail(error, QA_ERROR_MEMORY, "Retaining original projectile attack provenance");
    *lease = (attack_projectile){.next = p->projectiles, .address = result->as.address, .actor = actor,
        .attack = frame->attack};
    lease->attack.inflictor = actor; lease->attack.projectile = actor;
    p->projectiles = lease; (void)native; return true;
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
    forget_address(p, arguments[0].as.address);
    (void)native;
    return qa_native_host_source_reconcile(p->engine->provider->state.native.host, error);
}
bool application_native_q2_attack_activate(struct application_native_q2 *engine, qa_error *error)
{
    struct application_native_q2_attack *p = engine->source_attack;
    if (!p || p->active) return true;
    if (!engine->provider->state.native.host || !engine->initialized)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native attack activation requires initialized original item storage");
    if (!p->items_ready) {
        free(p->items); p->items = NULL; p->count = 0;
        p->items = calloc(p->item_count, sizeof(*p->items));
        if (!p->items) return application_fail(error, QA_ERROR_MEMORY, "Retaining qualified native weapon callbacks");
        for (uint32_t i = 0; i < p->item_count; ++i) {
            qa_native_address record, name, think; uint8_t flags[4]; qa_buffer text = {0};
            if (!qa_native_rva(instance(p), (uint64_t)p->item_table + (uint64_t)i * p->item_stride, p->item_stride, &record, error) ||
                !qa_native_read(instance(p), record + p->item_flags, flags, 4, error)) return false;
            if (!(qa_load_u32le(flags) & p->weapon_flag)) continue;
            if (!pointer_read(p, record + p->item_classname, &name, error) ||
                !pointer_read(p, record + p->weapon_think, &think, error)) return false;
            if (!name || !think) return application_fail(error, QA_ERROR_FORMAT, "Native weapon descriptor lacks its original name or callback");
            if (!qa_native_read_string(instance(p), name, 1024, &text, error)) return false;
            bool valid = text.size > 0;
            for (size_t j = 0; j < text.size; ++j)
                valid = valid && ((text.data[j] >= 'a' && text.data[j] <= 'z') ||
                    (j && ((text.data[j] >= '0' && text.data[j] <= '9') || text.data[j] == '_')));
            if (!valid) { qa_buffer_free(&text); return application_fail(error, QA_ERROR_FORMAT, "Native weapon classname is not qualified"); }
            char qualified[1030]; snprintf(qualified, sizeof(qualified), "q2:%s", (char *)text.data);
            qa_buffer_free(&text); qa_item_id item;
            if (!qa_strings_intern_cstr(qa_session_strings(engine->provider->application->session), qualified, &item, error)) return false;
            p->items[p->count++] = (attack_item){record, think, item};
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
    for (size_t i = 0; i < p->count; ++i) {
        attack_hook *hook = p->weapons;
        while (hook && hook->address != p->items[i].think) hook = hook->next;
        if (!hook) {
            hook = calloc(1, sizeof(*hook));
            if (!hook) return application_fail(error, QA_ERROR_MEMORY, "Retaining native weapon entry lease");
            hook->owner = p; hook->address = p->items[i].think; hook->next = p->weapons; p->weapons = hook;
        }
        if (!hook->binding && !qa_native_observe_entry(instance(p), hook->address, &weapon_sig,
                weapon_entry, hook, &hook->binding, error)) return false;
    }
    qa_native_address entry;
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
    for (attack_projectile *lease = p->projectiles; lease; lease = lease->next) if (qa_actor_id_equal(lease->actor, inflictor)) {
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
        !application_native_q2_idle(engine->provider))
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
void application_native_q2_attack_released(struct application_native_q2 *engine, qa_actor_id actor)
{
    struct application_native_q2_attack *p = engine->source_attack;
    if (!p) return;
    attack_projectile **leases = &p->projectiles;
    while (*leases) {
        attack_projectile *lease = *leases;
        if (!qa_actor_id_equal(lease->actor, actor)) { leases = &lease->next; continue; }
        *leases = lease->next; free(lease);
    }
    attack_factor **factors = &p->factors;
    while (*factors) {
        attack_factor *factor = *factors;
        if (!qa_actor_id_equal(factor->actor, actor)) { factors = &factor->next; continue; }
        *factors = factor->next; free(factor);
    }
}
bool application_native_q2_attack_suspend(struct application_native_q2 *engine, qa_error *error)
{
    struct application_native_q2_attack *p = engine->source_attack;
    if (!p) return true;
    if (p->current) return application_fail(error, QA_ERROR_ARGUMENT, "Native attack close requires drained original weapon calls");
    p->active = false;
    for (attack_hook *hook = p->weapons; hook; hook = hook->next) {
        if (hook->binding && !qa_native_unobserve_entry(hook->binding, error)) return false;
        hook->binding = NULL;
    }
    qa_native_entry_observer **bindings[] = {&p->factor_hook, &p->spawn_hook, &p->free_hook};
    for (size_t i = 0; i < 3; ++i) {
        if (*bindings[i] && !qa_native_unobserve_entry(*bindings[i], error)) return false;
        *bindings[i] = NULL;
    }
    while (p->weapons) { attack_hook *next = p->weapons->next; free(p->weapons); p->weapons = next; }
    return true;
}
bool application_native_q2_attack_close(struct application_native_q2 *engine, qa_error *error)
{
    struct application_native_q2_attack *p = engine->source_attack;
    if (!p) return true;
    if (!application_native_q2_attack_suspend(engine, error)) return false;
    while (p->factors) { attack_factor *next = p->factors->next; free(p->factors); p->factors = next; }
    while (p->projectiles) { attack_projectile *next = p->projectiles->next; free(p->projectiles); p->projectiles = next; }
    qa_json_destroy(p->document); free(p->items); free(p); engine->source_attack = NULL;
    return true;
}

struct application_native_q2_attack_restore {
    uint64_t sequence;
    attack_factor *factors;
    attack_projectile *projectiles;
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
    uint32_t version = 1; size_t factors = 0, projectiles = 0;
    for (attack_factor *row = p->factors; row; row = row->next) ++factors;
    for (attack_projectile *row = p->projectiles; row; row = row->next) ++projectiles;
    uint64_t sequence = p->sequence;
    bool ok = qa_source_save_u32(&io, &version) && qa_source_save_u64(&io, &sequence) &&
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
    while (prepared->factors) { attack_factor *next = prepared->factors->next; free(prepared->factors); prepared->factors = next; }
    while (prepared->projectiles) { attack_projectile *next = prepared->projectiles->next; free(prepared->projectiles); prepared->projectiles = next; }
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
    if (p->active || p->current || p->weapons || p->spawn_hook || p->free_hook || p->factor_hook || !engine->provider->state.native.host)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native attack restore requires retired observers and real restored source storage");
    struct application_native_q2_attack_restore *prepared = calloc(1, sizeof(*prepared));
    if (!prepared) return application_fail(error, QA_ERROR_MEMORY, "Preparing native attack continuation");
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, engine->provider->application->session, bytes, error);
    uint32_t version = 0; size_t count = 0;
    if (ok) ok = qa_source_save_u32(&io, &version) && version == 1 &&
        qa_source_save_u64(&io, &prepared->sequence) && qa_source_save_count(&io, &count, 65536);
    for (size_t i = 0; ok && i < count; ++i) {
        attack_factor *row = calloc(1, sizeof(*row));
        if (!row) { ok = application_fail(error, QA_ERROR_MEMORY, "Preparing native modifier continuation"); break; }
        row->next = prepared->factors; prepared->factors = row;
        ok = qa_source_save_actor(&io, &row->actor) && qa_source_save_f64(&io, &row->factor) &&
            qa_source_save_bool(&io, &row->available);
        if (ok && (!row->actor.registry || !qa_actors_get(qa_session_actors(engine->provider->application->session), row->actor) ||
            !isfinite(row->factor) || row->factor < 1))
            ok = application_fail(error, QA_ERROR_FORMAT, "Native modifier continuation has invalid source authority");
        for (attack_factor *prior = row->next; ok && prior; prior = prior->next)
            if (qa_actor_id_equal(prior->actor, row->actor)) ok = application_fail(error, QA_ERROR_FORMAT, "Native modifier continuation repeats an actor");
    }
    if (ok) ok = qa_source_save_count(&io, &count, 65536);
    for (size_t i = 0; ok && i < count; ++i) {
        attack_projectile *row = calloc(1, sizeof(*row)); uint32_t slot = 0;
        if (!row) { ok = application_fail(error, QA_ERROR_MEMORY, "Preparing original projectile continuation"); break; }
        row->next = prepared->projectiles; prepared->projectiles = row;
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
        for (attack_projectile *prior = row->next; ok && prior; prior = prior->next)
            if (qa_actor_id_equal(prior->actor, row->actor) || prior->address == row->address)
                ok = application_fail(error, QA_ERROR_FORMAT, "Original projectile continuation repeats a source slot");
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
    while (p->factors) { attack_factor *next = p->factors->next; free(p->factors); p->factors = next; }
    while (p->projectiles) { attack_projectile *next = p->projectiles->next; free(p->projectiles); p->projectiles = next; }
    p->sequence = prepared->sequence; p->factors = prepared->factors; p->projectiles = prepared->projectiles;
    prepared->factors = NULL; prepared->projectiles = NULL; free(prepared);
    /* Raw descriptor pointers belong to the candidate image and are resolved
     * again before observers are rebound; none are saved or transplanted. */
    free(p->items); p->items = NULL; p->count = 0; p->items_ready = false;
}
