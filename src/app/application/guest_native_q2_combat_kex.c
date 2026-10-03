#include "guest_native_q2_private.h"
#include "guest_native_q2_combat_kex.h"
#include "guest_native_q2_combat.h"
#include "qa/native_observe.h"
#include "qa/persistence_fields.h"
#include <math.h>

typedef enum kex_storage { KEX_POINTER, KEX_I32, KEX_U32 } kex_storage;
typedef struct kex_location {
    bool stack;
    kex_storage storage;
    uint32_t offset;
    qa_native_register reg;
} kex_location;
typedef struct kex_repair { kex_location source, target; } kex_repair;
typedef struct kex_pending {
    struct kex_pending *next;
    struct application_q2_kex_damage *owner;
    application_q2_combat_actor source;
    qa_native_write_observer *mod_watch, *blood_watch;
    qa_damage_request request;
    bool active, pending;
} kex_pending;
struct application_q2_kex_damage {
    application_q2_combat_profile *profile;
    qa_native_declared_region region;
    qa_native_region_binding *armor;
    qa_native_entry_observer *process;
    kex_location target, amount, point, normal, flags, result;
    kex_repair *repairs;
    size_t repair_count;
    kex_pending *records;
    bool active;
};
static qa_native_instance *native(struct application_q2_kex_damage *p)
{ return qa_native_host_instance(p->profile->engine->provider->state.native.host); }
static bool location_read(const qa_json_document *doc, qa_json_id object,
    kex_location *out, qa_error *error)
{
    qa_json_id storage = qa_json_get(doc, object, "storage"), kind = qa_json_get(doc, object, "kind");
    if (qa_json_string_equal(doc, storage, "pointer")) out->storage = KEX_POINTER;
    else if (qa_json_string_equal(doc, storage, "int32")) out->storage = KEX_I32;
    else if (qa_json_string_equal(doc, storage, "uint32")) out->storage = KEX_U32;
    else return application_fail(error, QA_ERROR_FORMAT, "Native KEX armor location changes its original scalar representation");
    out->stack = qa_json_string_equal(doc, kind, "stack");
    if (out->stack) {
        uint64_t offset;
        if (!qa_json_u64(doc, qa_json_get(doc, object, "offset"), &offset, error) ||
            offset > 1048576u - (out->storage == KEX_POINTER ? 8u : 4u))
            return application_fail(error, QA_ERROR_FORMAT, "Native KEX armor stack location exceeds its declared source bound");
        out->offset = (uint32_t)offset; return true;
    }
    if (!qa_json_string_equal(doc, kind, "register"))
        return application_fail(error, QA_ERROR_FORMAT, "Native KEX armor location is neither source register nor source stack");
    static const char *const names[] = {"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
        "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"};
    for (size_t i = 0; i < sizeof(names) / sizeof(*names); ++i)
        if (i != QA_NATIVE_RSP && qa_json_string_equal(doc, qa_json_get(doc, object, "register"), names[i])) {
            out->reg = (qa_native_register)i; return true;
        }
    return application_fail(error, QA_ERROR_FORMAT, "Native KEX armor location has an unqualified processor register");
}
static bool value_read(struct application_q2_kex_damage *p, const qa_native_processor_state *state,
    kex_location location, uint64_t *out, qa_error *error)
{
    if (!location.stack) { *out = state->registers[location.reg]; }
    else {
        uint64_t stack = state->registers[QA_NATIVE_RSP]; uint8_t bytes[8];
        size_t extent = location.storage == KEX_POINTER ? 8 : 4;
        if (!stack || stack > UINT64_MAX - location.offset ||
            !qa_native_read(native(p), stack + location.offset, bytes, extent, error)) return false;
        *out = extent == 8 ? qa_load_u64le(bytes) : qa_load_u32le(bytes);
    }
    if (location.storage != KEX_POINTER) *out = (uint32_t)*out;
    return true;
}
static bool value_write(struct application_q2_kex_damage *p, qa_native_processor_state *state,
    kex_location location, uint64_t value, qa_error *error)
{
    if (location.storage != KEX_POINTER) value = (uint32_t)value;
    if (!location.stack) { state->registers[location.reg] = value; return true; }
    uint64_t stack = state->registers[QA_NATIVE_RSP]; uint8_t bytes[8];
    if (!stack || stack > UINT64_MAX - location.offset)
        return application_fail(error, QA_ERROR_FORMAT, "Native KEX armor replacement lost its source stack");
    size_t extent = location.storage == KEX_POINTER ? 8 : 4;
    if (extent == 8) qa_store_u64le(bytes, value); else qa_store_u32le(bytes, (uint32_t)value);
    return qa_native_write(native(p), stack + location.offset, (qa_bytes){bytes, extent}, error);
}
static bool armor_region(void *opaque, qa_native_instance *instance,
    const qa_native_region_event *event, qa_native_region_decision *decision, qa_error *error)
{
    (void)instance; struct application_q2_kex_damage *p = opaque;
    if (!p->active || event->phase != QA_NATIVE_REGION_ENTER) return true;
    uint64_t target, amount, point, normal, flags;
    if (!value_read(p, &event->state, p->target, &target, error) ||
        !value_read(p, &event->state, p->amount, &amount, error) ||
        !value_read(p, &event->state, p->point, &point, error) ||
        !value_read(p, &event->state, p->normal, &normal, error) ||
        !value_read(p, &event->state, p->flags, &flags, error)) return false;
    uint64_t repair[128];
    for (size_t i = 0; i < p->repair_count; ++i)
        if (!value_read(p, &event->state, p->repairs[i].source, &repair[i], error)) return false;
    bool handled = false; int32_t saved = 0;
    ++p->profile->engine->calls;
    bool ok = application_native_q2_combat_inline_armor(p->profile->engine, target,
        (int32_t)(uint32_t)amount, (uint32_t)flags, point, normal, &handled, &saved, error);
    --p->profile->engine->calls;
    if (!ok || !handled) return ok;
    decision->action = QA_NATIVE_REGION_SKIP_TO_JOIN;
    decision->replace_state = true; decision->state = event->state;
    for (size_t i = 0; i < p->repair_count; ++i)
        if (!value_write(p, &decision->state, p->repairs[i].target, repair[i], error)) return false;
    return value_write(p, &decision->state, p->result, (uint32_t)saved, error);
}
static kex_pending *record_for(struct application_q2_kex_damage *p, qa_actor_id actor)
{
    for (kex_pending *record = p->records; record; record = record->next)
        if (qa_actor_id_equal(record->source.actor, actor)) return record;
    return NULL;
}
static bool mod_store(void *opaque, qa_native_instance *instance,
    const qa_native_write_event *event, qa_error *error)
{
    (void)instance; (void)event; kex_pending *record = opaque;
    if (!record->active || !record->owner->active) return true;
    if (!application_q2_combat_actor_valid(&record->source, error)) return false;
    const qa_damage_request *request = application_native_q2_combat_request(
        record->owner->profile->engine, record->source.actor);
    if (!request) return application_fail(error, QA_ERROR_UNSUPPORTED,
        "Original KEX monster accumulation lacks an active captured damage request");
    record->request = *request; record->pending = true; return true;
}
static bool blood_store(void *opaque, qa_native_instance *instance,
    const qa_native_write_event *event, qa_error *error)
{
    (void)instance; kex_pending *record = opaque;
    if (!record->active || !record->owner->active) return true;
    if (!application_q2_combat_actor_valid(&record->source, error)) return false;
    if (event->after.size != 4) return application_fail(error, QA_ERROR_FORMAT,
        "Original KEX blood accumulation changes its exact int32 extent");
    if (qa_load_i32le(event->after.data) == 0) record->pending = false;
    return true;
}
static bool vector_read(struct application_q2_kex_damage *p, qa_native_address at,
    qa_vec3 *out, qa_error *error)
{
    uint8_t bytes[12];
    if (!qa_native_read(native(p), at, bytes, sizeof(bytes), error)) return false;
    *out = qa_v3(qa_load_f32le(bytes), qa_load_f32le(bytes + 4), qa_load_f32le(bytes + 8));
    return qa_vec_finite(*out) || application_fail(error, QA_ERROR_FORMAT, "Original KEX accumulated point is not finite");
}
typedef struct process_call {
    qa_native_entry_observer *binding;
    const qa_native_value *arguments;
    size_t count;
    qa_native_value *result;
} process_call;
static bool process_original(void *opaque, qa_error *error)
{
    process_call *call = opaque;
    return qa_native_invoke_original(call->binding, call->arguments, call->count, call->result, error);
}
static bool process_entry(void *opaque, qa_native_instance *instance, qa_native_entry_observer *binding,
    const qa_native_value *arguments, size_t count, qa_native_value *result, qa_error *error)
{
    (void)instance; struct application_q2_kex_damage *p = opaque;
    if (!p->active) return qa_native_invoke_original(binding, arguments, count, result, error);
    qa_native_value fields[APPLICATION_Q2_FIELD_COUNT]; qa_actor_id actor;
    if (!application_q2_call_project(&p->profile->calls[APPLICATION_Q2_PROCESS_PAIN],
        arguments, count, fields, error)) return false;
    uint8_t blood[4], knockback[4];
    qa_native_address target = fields[APPLICATION_Q2_TARGET].as.address;
    if (!target || target > UINT64_MAX - p->profile->monster_blood ||
        !qa_native_read(native(p), target + p->profile->monster_blood, blood, 4, error)) return false;
    int32_t amount = qa_load_i32le(blood);
    if (!amount) return qa_native_invoke_original(binding, arguments, count, result, error);
    if (!qa_native_host_source_actor(p->profile->engine->provider->state.native.host,
            fields[APPLICATION_Q2_TARGET].as.address, false, &actor, error)) return false;
    kex_pending *record = record_for(p, actor);
    if (!record || !record->active || !record->pending)
        return application_fail(error, QA_ERROR_UNSUPPORTED,
            "Original KEX ProcessPain lacks the captured accumulator request for its live target");
    if (!application_q2_combat_actor_valid(&record->source, error)) return false;
    qa_damage_request request = record->request; qa_combat_state state;
    if (amount < 0 || (double)(float)amount != amount ||
        !qa_native_read(native(p), record->source.address + p->profile->monster_knockback, knockback, 4, error) ||
        !vector_read(p, record->source.address + p->profile->monster_point, &request.point, error) ||
        !application_q2_combat_state_read(&record->source, &state, error))
        return application_fail(error, QA_ERROR_FORMAT, "Original KEX reaction accumulation exceeds its exact shared representation");
    int32_t kick = qa_load_i32le(knockback); request.knockback = (float)kick;
    if ((double)request.knockback != kick)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Original KEX accumulated kick exceeds exact shared representation");
    qa_damage_result reaction = {.reaction = state.health <= 0 ? QA_REACTION_DEATH : QA_REACTION_PAIN,
        .applied_damage = (float)amount};
    record->pending = false;
    process_call original = {binding, arguments, count, result};
    ++p->profile->engine->calls;
    bool ok = application_native_q2_combat_deferred(p->profile->engine,
        &request, &reaction, process_original, &original, error);
    --p->profile->engine->calls; return ok;
}
bool application_q2_kex_damage_prepare(application_q2_combat_profile *profile,
    struct application_q2_kex_damage **out, qa_error *error)
{
    if (!out || !profile || !profile->kex) return application_fail(error, QA_ERROR_ARGUMENT,
        "KEX damage observation requires the actual qualified KEX source profile");
    struct application_q2_kex_damage *p = calloc(1, sizeof(*p));
    if (!p) return application_fail(error, QA_ERROR_MEMORY, "Preparing original KEX damage observer");
    *out = p; p->profile = profile;
    uint32_t offsets[] = {profile->monster_attacker, profile->monster_inflictor, profile->monster_blood,
        profile->monster_knockback, profile->monster_point, profile->monster_mod, profile->monster_invincible};
    uint32_t sizes[] = {8, 8, 4, 4, 12, 3, 8};
    uint32_t authority[] = {profile->health, profile->damageable, profile->flags, profile->mass,
        profile->velocity, profile->pain, profile->die, profile->generation};
    uint32_t authority_sizes[] = {4, 1, 8, 4, 12, 8, 8, 4};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(*sizes); ++i) {
        if (offsets[i] < 1472 || offsets[i] > profile->entity_bytes || sizes[i] > profile->entity_bytes - offsets[i])
            return application_fail(error, QA_ERROR_FORMAT, "Native KEX monster accumulation aliases its public prefix or exceeds its edict");
        for (size_t j = 0; j < i; ++j)
            if (offsets[i] < (uint64_t)offsets[j] + sizes[j] && offsets[j] < (uint64_t)offsets[i] + sizes[i])
                return application_fail(error, QA_ERROR_FORMAT, "Native KEX monster accumulation repeats source authority");
        for (size_t j = 0; j < sizeof(authority_sizes) / sizeof(*authority_sizes); ++j)
            if (offsets[i] < (uint64_t)authority[j] + authority_sizes[j] && authority[j] < (uint64_t)offsets[i] + sizes[i])
                return application_fail(error, QA_ERROR_FORMAT, "Native KEX monster accumulation aliases another observed source field");
    }
    const qa_json_document *doc = profile->document;
    qa_json_id metadata = qa_json_get(doc, profile->world, "regularArmor");
    kex_location *locations[] = {&p->target, &p->amount, &p->point, &p->normal, &p->flags, &p->result};
    static const char *const names[] = {"target", "amount", "point", "normal", "flags", "result"};
    for (size_t i = 0; i < sizeof(locations) / sizeof(*locations); ++i)
        if (!location_read(doc, qa_json_get(doc, metadata, names[i]), locations[i], error) ||
            locations[i]->storage != ((i == 0 || i == 2 || i == 3) ? KEX_POINTER : KEX_I32))
            return application_fail(error, QA_ERROR_FORMAT, "Native KEX armor semantic location changes its original type");
    qa_json_id repairs = qa_json_get(doc, metadata, "repair"); p->repair_count = qa_json_size(doc, repairs);
    if (qa_json_type(doc, repairs) != QA_JSON_ARRAY || p->repair_count > 128)
        return application_fail(error, QA_ERROR_FORMAT, "Native KEX armor repair roster exceeds its declared bound");
    if (p->repair_count) {
        p->repairs = calloc(p->repair_count, sizeof(*p->repairs));
        if (!p->repairs) return application_fail(error, QA_ERROR_MEMORY, "Retaining original KEX armor repair locations");
    }
    for (size_t i = 0; i < p->repair_count; ++i) {
        qa_json_id row = qa_json_at(doc, repairs, i);
        if (!location_read(doc, qa_json_get(doc, row, "source"), &p->repairs[i].source, error) ||
            !location_read(doc, qa_json_get(doc, row, "target"), &p->repairs[i].target, error) ||
            p->repairs[i].source.storage != p->repairs[i].target.storage)
            return application_fail(error, QA_ERROR_FORMAT, "Native KEX armor repair changes its original scalar representation");
    }
    return qa_native_declaration_find_region(profile->engine->declaration,
        "/world/entries/regularArmor", &p->region, error);
}
bool application_q2_kex_damage_bind(struct application_q2_kex_damage *p, qa_error *error)
{
    return !p || p->armor || qa_native_bind_region(native(p), p->region.id,
        armor_region, p, &p->armor, error);
}
bool application_q2_kex_damage_activate(struct application_q2_kex_damage *p, qa_error *error)
{
    if (!p || p->active) return true;
    if (!application_q2_kex_damage_bind(p, error)) return false;
    qa_native_address entry;
    if (!p->process && (!qa_native_rva(native(p), p->profile->process_entry, 1, &entry, error) ||
        !qa_native_observe_entry(native(p), entry, &p->profile->calls[APPLICATION_Q2_PROCESS_PAIN].signature,
            process_entry, p, &p->process, error))) return false;
    p->active = true;
    for (kex_pending *record = p->records; record; record = record->next)
        if ((record->active || record->pending) && !application_q2_kex_damage_track(p, &record->source, error)) {
            p->active = false; return false;
        }
    return true;
}
bool application_q2_kex_damage_track(struct application_q2_kex_damage *p,
    const application_q2_combat_actor *source, qa_error *error)
{
    if (!p || !p->active) return true;
    uint8_t flags[4];
    if (!application_q2_combat_actor_valid((application_q2_combat_actor *)source, error) ||
        !qa_native_read(native(p), source->address + p->profile->svflags, flags, 4, error)) return false;
    if (!(qa_load_u32le(flags) & 4)) return true;
    kex_pending *record = record_for(p, source->actor);
    if (!record) {
        record = calloc(1, sizeof(*record));
        if (!record) return application_fail(error, QA_ERROR_MEMORY, "Retaining original KEX monster damage lifetime");
        record->source = *source; record->source.admitting = false;
        record->owner = p; record->next = p->records; p->records = record;
    }
    if (record->source.address != source->address || record->source.generation != source->generation)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Original KEX monster source lifetime changed");
    record->active = true;
    if (!record->mod_watch && !qa_native_observe_writes(native(p), source->address + p->profile->monster_mod + 2,
        1, mod_store, record, &record->mod_watch, error)) return false;
    if (!record->blood_watch && !qa_native_observe_writes(native(p), source->address + p->profile->monster_blood,
        4, blood_store, record, &record->blood_watch, error)) return false;
    return true;
}
void application_q2_kex_damage_released(struct application_q2_kex_damage *p, qa_actor_id actor)
{
    kex_pending *record = p ? record_for(p, actor) : NULL;
    if (record) record->active = record->pending = false;
}
bool application_q2_kex_damage_suspend(struct application_q2_kex_damage *p, qa_error *error)
{
    if (!p) return true;
    p->active = false;
    for (kex_pending *record = p->records; record; record = record->next) {
        if (!qa_native_unobserve_writes(record->mod_watch, error)) return false;
        record->mod_watch = NULL;
        if (!qa_native_unobserve_writes(record->blood_watch, error)) return false;
        record->blood_watch = NULL;
    }
    if (!qa_native_unobserve_entry(p->process, error)) return false;
    p->process = NULL;
    if (p->armor && !qa_native_remove_region(p->armor, error)) return false;
    p->armor = NULL; return true;
}
bool application_q2_kex_damage_close(struct application_q2_kex_damage *p, qa_error *error)
{
    if (!p) return true;
    if (!application_q2_kex_damage_suspend(p, error)) return false;
    while (p->records) { kex_pending *record = p->records; p->records = record->next; free(record); }
    free(p->repairs); free(p); return true;
}

typedef struct kex_saved {
    kex_pending record;
    struct kex_saved *next;
    int32_t blood, knockback;
    qa_vec3 point;
    uint8_t mod[3];
    uint32_t attacker_slot, inflictor_slot;
} kex_saved;
struct application_q2_kex_restore { kex_saved *records; };
static bool request_fields(qa_source_save_io *io, qa_damage_request *request)
{
    qa_attack *attack = &request->attack;
    return qa_source_save_actor(io, &request->target) && qa_persistence_attack(io, attack) &&
        (attack->cause.kind != QA_CAUSE_Q1 || qa_source_save_string(io, &attack->cause.source.q1.death_type)) &&
        qa_source_save_string(io, &attack->weapon) && qa_source_save_string(io, &attack->weapon_provider) &&
        qa_source_save_string(io, &attack->combat_provider) && qa_source_save_string(io, &attack->inventory_provider) &&
        qa_source_save_string(io, &attack->movement_provider) && qa_source_save_string(io, &attack->powerup_owner) &&
        qa_source_save_f32(io, &request->amount) && qa_source_save_f32(io, &request->knockback) &&
        qa_source_save_vec3(io, &request->direction) && qa_source_save_vec3(io, &request->point) &&
        qa_source_save_vec3(io, &request->normal) && qa_source_save_bool(io, &request->radius);
}
static bool saved_fields(qa_source_save_io *io, kex_saved *saved)
{
    return qa_source_save_u32(io, &saved->record.source.slot) &&
        qa_source_save_i32(io, &saved->record.source.generation) && request_fields(io, &saved->record.request) &&
        qa_source_save_i32(io, &saved->blood) && qa_source_save_i32(io, &saved->knockback) &&
        qa_source_save_vec3(io, &saved->point) && qa_source_save_bytes(io, saved->mod, 3) &&
        qa_source_save_u32(io, &saved->attacker_slot) && qa_source_save_u32(io, &saved->inflictor_slot);
}
static bool pointer_slot(struct application_q2_kex_damage *p, qa_native_address at,
    uint32_t *out, qa_error *error)
{
    uint8_t bytes[8];
    if (!qa_native_read(native(p), at, bytes, 8, error)) return false;
    qa_native_address address = qa_load_u64le(bytes);
    if (!address) return application_fail(error, QA_ERROR_FORMAT, "Original KEX pending accumulation has a null source actor pointer");
    return qa_native_entity_slot(native(p), address, out, error);
}
bool application_q2_kex_damage_capture(struct application_q2_kex_damage *p, qa_buffer *out, qa_error *error)
{
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "KEX deferred continuation output is absent");
    if (!p) { *out = (qa_buffer){0}; return true; }
    qa_source_save_io io = {0};
    if (!qa_source_save_writer(&io, p->profile->engine->provider->application->session, error)) return false;
    size_t count = 0;
    for (kex_pending *record = p->records; record; record = record->next)
        if (record->active && record->pending) ++count;
    bool ok = qa_source_save_count(&io, &count, 65536);
    for (kex_pending *record = p->records; ok && record; record = record->next) {
        if (!record->active || !record->pending) continue;
        kex_saved saved = {.record = *record}; uint8_t bytes[4];
        ok = application_q2_combat_actor_valid(&record->source, error) &&
            qa_native_read(native(p), record->source.address + p->profile->monster_blood, bytes, 4, error);
        if (ok) saved.blood = qa_load_i32le(bytes);
        if (ok) ok = qa_native_read(native(p), record->source.address + p->profile->monster_knockback, bytes, 4, error);
        if (ok) saved.knockback = qa_load_i32le(bytes);
        if (ok) ok = vector_read(p, record->source.address + p->profile->monster_point, &saved.point, error) &&
            qa_native_read(native(p), record->source.address + p->profile->monster_mod, saved.mod, 3, error) &&
            pointer_slot(p, record->source.address + p->profile->monster_attacker, &saved.attacker_slot, error) &&
            pointer_slot(p, record->source.address + p->profile->monster_inflictor, &saved.inflictor_slot, error);
        if (ok && (saved.blood < 0 || (double)(float)saved.blood != saved.blood ||
            (double)(float)saved.knockback != saved.knockback ||
            !qa_damage_request_validate(&saved.record.request, error)))
            ok = application_fail(error, QA_ERROR_FORMAT, "Original KEX pending damage exceeds its exact saved representation");
        qa_damage_cause cause;
        qa_native_value mod = {.type = QA_NATIVE_BYTES, .as.bytes = {saved.mod, 3}};
        if (ok) ok = application_q2_combat_cause_read(p->profile, &mod, 0, &cause, error) && saved_fields(&io, &saved);
    }
    if (ok) ok = qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return ok;
}
void application_q2_kex_damage_restore_abort(struct application_q2_kex_restore *prepared)
{
    if (!prepared) return;
    while (prepared->records) { kex_saved *saved = prepared->records; prepared->records = saved->next; free(saved); }
    free(prepared);
}
bool application_q2_kex_damage_restore_prepare(struct application_q2_kex_damage *p, qa_bytes bytes,
    struct application_q2_kex_restore **out, qa_error *error)
{
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "KEX deferred restore output is absent");
    *out = NULL;
    if (!p) return !bytes.size || application_fail(error, QA_ERROR_FORMAT, "KEX deferred continuation has no actual source owner");
    if (p->active || p->process || p->armor)
        return application_fail(error, QA_ERROR_ARGUMENT, "KEX deferred restore requires its suspended original source observers");
    for (kex_pending *record = p->records; record; record = record->next)
        if (record->mod_watch || record->blood_watch)
            return application_fail(error, QA_ERROR_ARGUMENT, "KEX deferred restore retains a failed source observer removal");
    struct application_q2_kex_restore *prepared = calloc(1, sizeof(*prepared));
    if (!prepared) return application_fail(error, QA_ERROR_MEMORY, "Preparing KEX deferred continuation");
    qa_source_save_io io = {0}; size_t count = 0;
    bool ok = qa_source_save_reader(&io, p->profile->engine->provider->application->session, bytes, error) &&
        qa_source_save_count(&io, &count, 65536);
    qa_native_entity_table table;
    if (ok) ok = qa_native_entity_table_get(native(p), &table, error);
    for (size_t i = 0; ok && i < count; ++i) {
        kex_saved *saved = calloc(1, sizeof(*saved));
        if (!saved) { ok = application_fail(error, QA_ERROR_MEMORY, "Preparing original KEX pending damage"); break; }
        saved->next = prepared->records; prepared->records = saved;
        ok = saved_fields(&io, saved);
        uint32_t slot = saved->record.source.slot; int32_t generation = saved->record.source.generation;
        if (ok) ok = application_q2_combat_actor_init(p->profile, slot,
            saved->record.request.target, false, &saved->record.source, error);
        uint8_t flags[4];
        if (ok) ok = qa_native_read(native(p), saved->record.source.address + p->profile->svflags, flags, 4, error);
        if (ok && (saved->record.source.generation != generation || !(qa_load_u32le(flags) & 4) ||
            saved->attacker_slot >= table.capacity || saved->inflictor_slot >= table.capacity ||
            saved->blood < 0 || (double)(float)saved->blood != saved->blood ||
            (double)(float)saved->knockback != saved->knockback || !qa_vec_finite(saved->point) ||
            !qa_damage_request_validate(&saved->record.request, error)))
            ok = application_fail(error, QA_ERROR_FORMAT, "KEX deferred continuation differs from its original source authority");
        qa_damage_cause cause;
        qa_native_value mod = {.type = QA_NATIVE_BYTES, .as.bytes = {saved->mod, 3}};
        if (ok) ok = application_q2_combat_cause_read(p->profile, &mod, 0, &cause, error);
        for (kex_saved *prior = saved->next; ok && prior; prior = prior->next)
            if (prior->record.source.slot == slot || qa_actor_id_equal(prior->record.request.target, saved->record.request.target))
                ok = application_fail(error, QA_ERROR_FORMAT, "KEX deferred continuation repeats an original source target");
        saved->record.owner = p; saved->record.active = saved->record.pending = true;
    }
    if (ok) ok = qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    /* All owned rows and references are prepared before touching the isolated
     * candidate's private accumulators. Failure leaves that candidate discardable. */
    for (kex_saved *saved = prepared->records; ok && saved; saved = saved->next) {
        qa_native_address attacker = 0, inflictor = 0; uint8_t words[8], point[12];
        ok = qa_native_entity_address(native(p), saved->attacker_slot, &attacker, error) &&
            qa_native_entity_address(native(p), saved->inflictor_slot, &inflictor, error);
        qa_native_address address = saved->record.source.address;
        qa_store_u64le(words, attacker);
        if (ok) ok = qa_native_write(native(p), address + p->profile->monster_attacker, (qa_bytes){words, 8}, error);
        qa_store_u64le(words, inflictor);
        if (ok) ok = qa_native_write(native(p), address + p->profile->monster_inflictor, (qa_bytes){words, 8}, error);
        qa_store_u32le(words, (uint32_t)saved->blood);
        if (ok) ok = qa_native_write(native(p), address + p->profile->monster_blood, (qa_bytes){words, 4}, error);
        qa_store_u32le(words, (uint32_t)saved->knockback);
        if (ok) ok = qa_native_write(native(p), address + p->profile->monster_knockback, (qa_bytes){words, 4}, error);
        float axes[] = {saved->point.x, saved->point.y, saved->point.z};
        for (size_t i = 0; i < 3; ++i) { uint32_t bits; memcpy(&bits, &axes[i], 4); qa_store_u32le(point + i * 4, bits); }
        if (ok) ok = qa_native_write(native(p), address + p->profile->monster_point, (qa_bytes){point, 12}, error) &&
            qa_native_write(native(p), address + p->profile->monster_mod, (qa_bytes){saved->mod, 3}, error);
    }
    if (!ok) {
        if (error && error->code == QA_OK) application_fail(error, QA_ERROR_FORMAT, "Invalid original KEX deferred continuation");
        application_q2_kex_damage_restore_abort(prepared); return false;
    }
    *out = prepared; return true;
}
void application_q2_kex_damage_restore_commit(struct application_q2_kex_damage *p,
    struct application_q2_kex_restore *prepared)
{
    if (!prepared) return;
    while (p->records) { kex_pending *record = p->records; p->records = record->next; free(record); }
    while (prepared->records) {
        kex_saved *saved = prepared->records; prepared->records = saved->next;
        /* The prepared allocation owns its first retained record without a
         * second allocation at publication. Keep that allocation's base. */
        kex_pending *record = &saved->record;
        record->next = p->records; p->records = record;
    }
    free(prepared);
}
