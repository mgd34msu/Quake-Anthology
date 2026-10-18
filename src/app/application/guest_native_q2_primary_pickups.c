#include "guest_native_q2_private.h"
#include "guest_native_q2_primary_pickups.h"
#include "guest_native_q2_combat.h"
#include "native_q2_inventory_source.h"
#include "control_frame.h"
#include "supplies.h"
#include "qa/native_observe.h"
#include <math.h>

typedef enum primary_supply_kind { PRIMARY_NO_SUPPLY, PRIMARY_AMMO, PRIMARY_WEAPON } primary_supply_kind;
typedef enum primary_region_kind {
    PRIMARY_RECIPIENT, PRIMARY_AMMO_GRANT, PRIMARY_SETTLE, PRIMARY_AUTOSWITCH,
    PRIMARY_TARGETS, PRIMARY_AMMO_STOP
} primary_region_kind;
typedef enum primary_cancel { PRIMARY_STALE = 1, PRIMARY_REFUSED = 2, PRIMARY_QUANTITY_STOP = 4 } primary_cancel;
typedef struct primary_consumer {
    qa_native_address address;
    uint32_t rva;
    qa_native_signature signature;
    qa_protection_channel channel;
} primary_consumer;
typedef struct primary_grant {
    struct application_native_q2_primary_pickups *owner;
    qa_native_address address, ammo_return;
    uint32_t rva, ammo_return_rva;
    qa_native_entry_observer *binding;
    primary_consumer *consumers;
    size_t consumer_count;
    primary_supply_kind supply;
    qa_pickup_resource_kind resource;
    qa_native_register amount;
} primary_grant;
typedef struct primary_region {
    struct application_native_q2_primary_pickups *owner;
    primary_grant *grant;
    primary_region_kind kind;
    uint32_t id;
    qa_native_region_binding *binding;
} primary_region;
typedef struct primary_item {
    qa_native_address descriptor;
    qa_item_id item;
} primary_item;
typedef struct primary_call {
    struct primary_call *previous;
    struct primary_frame *frame;
    qa_native_call_scope *scope;
    unsigned accepts;
} primary_call;
typedef struct primary_frame {
    struct primary_frame *previous, *pending_next;
    struct application_native_q2_primary_pickups *owner;
    qa_unified_frame_lease *storage;
    primary_grant *grant;
    qa_pickup_offer offer;
    qa_pickup_selection selection;
    qa_pickup_execution *execution;
    qa_actor_record pickup, recipient;
    uint32_t pickup_slot, recipient_slot;
    qa_native_address pickup_address, recipient_address, descriptor;
    application_native_q2_inventory_source inventory;
    const qa_native_value *arguments;
    size_t argument_count;
    qa_native_value result;
    qa_native_write_observer *watches[4];
    int32_t generations[2];
    primary_call *calls;
    const qa_native_region_event *event;
    qa_error cancellation;
    unsigned cancel_kind, projections;
    qa_supply_offer supply;
    qa_pickup_grant ammo;
    qa_native_address ammo_descriptor, projected_counter;
    int32_t ammo_amount, projected_before;
    bool invalid, called, skipped, settled, granted, supply_active, accepted_ammo;
    bool quantity_active, projected;
} primary_frame;
struct application_native_q2_primary_pickups {
    struct application_native_q2 *engine;
    const qa_native_declaration *declaration;
    qa_json_document *document;
    qa_arena signatures;
    qa_native_signature touch_signature, grant_signature, ammo_signature;
    qa_native_entry_observer *touch, *ammo;
    qa_native_address touch_address, ammo_address, grant_return, table, time;
    uint32_t touch_rva, ammo_rva, grant_return_rva, time_rva;
    primary_grant *grants;
    primary_region *regions;
    primary_item *items;
    size_t grant_count, region_count;
    uint32_t table_rva, item_stride, item_count, item_classname, item_pickup;
    uint32_t entity_item, entity_count, entity_flags, inuse, generation;
    uint32_t client_pointer, inventory_offset, item_flags, weapon_flag, ammo_tag;
    uint32_t *capacities;
    size_t capacity_count;
    uint8_t pointer_bytes, inuse_bytes, capacity_bytes;
    bool has_generation, milliseconds, active;
    primary_frame *frames, *pending;
    unsigned calls;
};
static qa_application *application(const struct application_native_q2_primary_pickups *o)
{ return o->engine->provider->application; }
static qa_native_instance *instance(const struct application_native_q2_primary_pickups *o)
{ return qa_native_host_instance(o->engine->provider->state.native.host); }
static bool word(const qa_json_document *d, qa_json_id object, const char *name, uint32_t *out, qa_error *e)
{
    uint64_t n;
    if (!qa_json_u64(d, qa_json_get(d, object, name), &n, e)) return false;
    if (n > UINT32_MAX) return application_fail(e, QA_ERROR_FORMAT, "Native pickup word exceeds its source representation");
    *out = (uint32_t)n; return true;
}
static bool pointer(struct application_native_q2_primary_pickups *o, qa_native_address address,
    qa_native_address *out, qa_error *e)
{
    uint8_t bytes[8];
    if (!qa_native_read(instance(o), address, bytes, o->pointer_bytes, e)) return false;
    *out = o->pointer_bytes == 4 ? qa_load_u32le(bytes) : qa_load_u64le(bytes); return true;
}
static bool integer(struct application_native_q2_primary_pickups *o, qa_native_address address,
    size_t bytes, int32_t *out, qa_error *e)
{
    uint8_t value[4];
    if (!qa_native_read(instance(o), address, value, bytes, e)) return false;
    *out = bytes == 1 ? value[0] : bytes == 2 ? (int16_t)qa_load_u16le(value) : qa_load_i32le(value); return true;
}
static bool store(struct application_native_q2_primary_pickups *o, qa_native_address address,
    size_t bytes, int32_t value, qa_error *e)
{
    uint8_t encoded[4]; qa_store_u32le(encoded, (uint32_t)value);
    return qa_native_write(instance(o), address, (qa_bytes){encoded, bytes}, e);
}
static bool rva(struct application_native_q2_primary_pickups *o, uint32_t value,
    qa_native_address *out, qa_error *e)
{ return qa_native_rva(instance(o), value, 1, out, e); }
static qa_native_value boolean(const qa_native_signature *signature, bool value)
{
    switch (signature->result.kind) {
    case QA_NATIVE_U8: return (qa_native_value){.type = QA_NATIVE_U8, .as.u8 = value};
    case QA_NATIVE_U32: return (qa_native_value){.type = QA_NATIVE_U32, .as.u32 = value};
    default: return (qa_native_value){.type = QA_NATIVE_I32, .as.i32 = value};
    }
}
static bool truth(const qa_native_value *result)
{
    return result->type == QA_NATIVE_U8 ? result->as.u8 != 0 :
        result->type == QA_NATIVE_U32 ? result->as.u32 != 0 : result->as.i32 != 0;
}
static primary_item *item(struct application_native_q2_primary_pickups *o, qa_native_address descriptor)
{
    if (descriptor < o->table || (descriptor - o->table) % o->item_stride) return NULL;
    uint64_t index = (descriptor - o->table) / o->item_stride;
    return index < o->item_count ? o->items + index : NULL;
}
static bool actor_current(primary_frame *f, bool recipient)
{
    struct application_native_q2_primary_pickups *o = f->owner;
    const qa_actor_record *saved = recipient ? &f->recipient : &f->pickup;
    const qa_actor_record *actual = qa_actors_get(qa_session_actors(application(o)->session), saved->id);
    uint32_t slot = recipient ? f->recipient_slot : f->pickup_slot;
    qa_native_address address = 0; qa_native_slot_binding binding; qa_error ignored = {0}; int32_t used;
    return actual && actual->owner == saved->owner && actual->definition == saved->definition &&
        qa_native_slot(instance(o), slot, &binding, &ignored) && qa_actor_id_equal(binding.actor, saved->id) &&
        qa_native_entity_address(instance(o), slot, &address, &ignored) &&
        address == (recipient ? f->recipient_address : f->pickup_address) &&
        integer(o, address + o->inuse, o->inuse_bytes, &used, &ignored) && used;
}
static bool live(primary_frame *f)
{
    struct application_native_q2_primary_pickups *o = f->owner;
    qa_native_address descriptor = 0; qa_error ignored = {0};
    return o->active && !f->invalid && !o->engine->shutting_down &&
        actor_current(f, false) && actor_current(f, true) &&
        pointer(o, f->pickup_address + o->entity_item, &descriptor, &ignored) && descriptor == f->descriptor &&
        (f->selection != QA_PICKUP_SELECT_REPLACEMENT || qa_pickup_current(f->execution));
}
static bool cancel(primary_frame *f, primary_cancel kind, qa_error *e)
{
    if (kind == PRIMARY_STALE) f->invalid = true;
    f->cancel_kind = kind;
    qa_error_set(&f->cancellation, QA_ERROR_NOT_FOUND, (size_t)(uintptr_t)f,
        "Native pickup source continuation retired (%u)", (unsigned)kind);
    if (e) *e = f->cancellation;
    return false;
}
static bool current(primary_frame *f, qa_error *e)
{ return live(f) || cancel(f, PRIMARY_STALE, e); }
static bool accepts(void *context, const qa_error *e)
{
    primary_call *call = context;
    return (call->accepts & call->frame->cancel_kind) && qa_error_same(e, &call->frame->cancellation);
}
typedef bool (*primary_invoke_fn)(void *, qa_error *);
static bool invoke_scope(primary_frame *f, unsigned admitted, primary_invoke_fn invoke, void *context,
    bool *cancelled, qa_error *e)
{
    primary_call *call = qa_unified_frame_lease_alloc(f->storage, 1, sizeof(*call), _Alignof(primary_call), e);
    if (!call) return false;
    *call = (primary_call){.frame = f, .accepts = admitted, .previous = f->calls};
    if (!qa_native_call_scope_open(instance(f->owner), accepts, call, &call->scope, e)) return false;
    f->calls = call;
    bool ok = invoke(context, e);
    if (!qa_native_call_scope_resolve(call->scope, ok, cancelled, e)) ok = false;
    else if (*cancelled) ok = true;
    qa_error cleanup = {0};
    if (!qa_native_call_scope_close(&call->scope, &cleanup)) {
        if (ok && e) *e = cleanup;
        return false;
    }
    f->calls = call->previous;
    return ok;
}
typedef struct primary_original {
    primary_frame *frame;
    qa_native_entry_observer *binding;
    const qa_native_value *arguments;
    size_t count;
    qa_native_value *result;
} primary_original;
static bool original(void *context, qa_error *e)
{
    primary_original *call = context;
    return qa_native_invoke_original(call->binding, call->arguments, call->count, call->result, e);
}
static bool projected_restore(primary_frame *f, qa_error *e)
{
    if (!f->projected) return true;
    if (actor_current(f, true) && !store(f->owner, f->projected_counter, 4, f->projected_before, e)) return false;
    f->projected = false; return true;
}
static bool changed(void *context, qa_native_instance *native, const qa_native_write_event *event, qa_error *e)
{
    primary_frame *f = context; struct application_native_q2_primary_pickups *o = f->owner;
    qa_native_address records[] = {f->pickup_address, f->recipient_address};
    for (size_t i = 0; i < 2; ++i) {
        int32_t value;
        if (event->address == records[i] + o->inuse) {
            if (!integer(o, event->address, o->inuse_bytes, &value, e)) return false;
            if (!value) f->invalid = true;
        } else if (o->has_generation && event->address == records[i] + o->generation) {
            if (!integer(o, event->address, 4, &value, e)) return false;
            if (value != f->generations[i]) f->invalid = true;
        }
    }
    (void)native; return true;
}
static bool frame_cleanup(primary_frame *f, qa_error *e)
{
    if (!projected_restore(f, e)) return false;
    for (size_t i = 0; i < sizeof(f->watches) / sizeof(*f->watches); ++i) {
        if (!qa_native_unobserve_writes(f->watches[i], e)) return false;
        f->watches[i] = NULL;
    }
    while (f->calls) {
        primary_call *call = f->calls;
        if (!qa_native_call_scope_abandon(call->scope, e) || !qa_native_call_scope_close(&call->scope, e)) return false;
        f->calls = call->previous;
    }
    return true;
}
static void frame_retire(primary_frame *f)
{
    f->arguments = NULL; f->argument_count = 0; f->execution = NULL; f->event = NULL; f->supply_active = false;
    bool held = f->projected || f->calls;
    for (size_t i = 0; i < sizeof(f->watches) / sizeof(*f->watches); ++i) held |= f->watches[i] != NULL;
    if (held) { f->pending_next = f->owner->pending; f->owner->pending = f; }
    else qa_unified_frame_lease_release(f->storage);
}
static bool run_source(void *context, const qa_pickup_offer *offer, qa_pickup_selection selection,
    qa_pickup_execution *execution, qa_error *e)
{
    primary_frame *f = context; struct application_native_q2_primary_pickups *o = f->owner;
    if (selection == QA_PICKUP_SELECT_STALE) return true;
    f->offer = *offer; f->selection = selection; f->execution = execution;
    f->previous = o->frames; o->frames = f;
    bool ok = true;
    if (selection != QA_PICKUP_SELECT_ORIGINAL) {
        qa_native_address records[] = {f->pickup_address, f->recipient_address};
        for (size_t i = 0; ok && i < 2; ++i) {
            ok = qa_native_observe_writes(instance(o), &(qa_native_write_watch){
                .address = records[i] + o->inuse, .bytes = o->inuse_bytes},
                changed, f, &f->watches[i * 2], e);
            if (ok && o->has_generation) ok = integer(o, records[i] + o->generation, 4, &f->generations[i], e) &&
                qa_native_observe_writes(instance(o), &(qa_native_write_watch){
                    .address = records[i] + o->generation, .bytes = 4},
                    changed, f, &f->watches[i * 2 + 1], e);
        }
    }
    if (ok) {
        primary_original call = {f, o->touch, f->arguments, f->argument_count, &f->result}; bool cancelled = false;
        ok = selection == QA_PICKUP_SELECT_ORIGINAL ? original(&call, e) :
            invoke_scope(f, PRIMARY_STALE, original, &call, &cancelled, e);
        if (ok && cancelled) f->result = (qa_native_value){.type = QA_NATIVE_VOID};
    }
    qa_error cleanup = {0};
    if (!frame_cleanup(f, &cleanup)) { if (ok && e) *e = cleanup; ok = false; }
    o->frames = f->previous;
    return ok;
}
static bool touch(void *context, qa_native_instance *native, qa_native_entry_observer *binding,
    const qa_native_value *arguments, size_t count, qa_native_value *result, qa_error *e)
{
    struct application_native_q2_primary_pickups *o = context;
    if (!o->active) return qa_native_invoke_original(binding, arguments, count, result, e);
    qa_native_address descriptor, callback, client;
    if (!pointer(o, arguments[0].as.address + o->entity_item, &descriptor, e) ||
        !pointer(o, arguments[1].as.address + o->client_pointer, &client, e)) return false;
    primary_item *row = item(o, descriptor); primary_grant *grant = NULL;
    if (row && row->item && client) {
        if (!pointer(o, descriptor + o->item_pickup, &callback, e)) return false;
        for (size_t i = 0; i < o->grant_count; ++i) if (o->grants[i].address == callback) { grant = o->grants + i; break; }
    }
    if (!grant) {
        primary_frame *previous = o->frames; o->frames = NULL;
        bool ok = qa_native_invoke_original(binding, arguments, count, result, e); o->frames = previous; return ok;
    }
    primary_frame f = {.owner = o, .grant = grant, .descriptor = descriptor,
        .pickup_address = arguments[0].as.address, .recipient_address = arguments[1].as.address,
        .arguments = arguments, .argument_count = count, .result = {.type = QA_NATIVE_VOID}};
    qa_actor_id a, b; qa_native_host *host = o->engine->provider->state.native.host;
    if (!qa_native_host_source_actor(host, f.pickup_address, true, &a, e) ||
        !qa_native_host_source_actor(host, f.recipient_address, true, &b, e)) return false;
    const qa_actor_record *pickup = qa_actors_get(qa_session_actors(application(o)->session), a),
        *recipient = qa_actors_get(qa_session_actors(application(o)->session), b);
    if (!pickup || !recipient) { *result = f.result; return true; }
    f.pickup = *pickup; f.recipient = *recipient;
    if (!qa_native_entity_slot(native, f.pickup_address, &f.pickup_slot, e) ||
        !qa_native_entity_slot(native, f.recipient_address, &f.recipient_slot, e)) return false;
    int32_t amount, flags; uint8_t clock[8];
    if (!integer(o, f.pickup_address + o->entity_count, 4, &amount, e) ||
        !integer(o, f.pickup_address + o->entity_flags, 4, &flags, e) ||
        !qa_native_read(native, o->time, clock, o->milliseconds ? 8 : 4, e)) return false;
    double ns = o->milliseconds ? (double)(int64_t)qa_load_u64le(clock) * 1000000.0 : (double)qa_load_f32le(clock) * 1000000000.0;
    if (!isfinite(ns) || ns >= (double)UINT64_MAX) return application_fail(e, QA_ERROR_FORMAT, "Native pickup clock exceeds its source timeline");
    f.offer = (qa_pickup_offer){.recipient = b, .pickup = a, .source = o->engine->provider->owner,
        .item = row->item, .default_resource = {.kind = grant->resource, .item = grant->resource == QA_PICKUP_INVENTORY ? row->item : 0,
            .channel = QA_PROTECTION_REGULAR}, .override_count = amount != 0, .count = amount,
        .dropped = ((uint32_t)flags & 0x30000u) != 0, .spawnflags = (uint32_t)flags,
        .time_ns = ns > 0 ? (uint64_t)ns : 0};
    f.storage = application_control_storage_acquire(application(o), e);
    if (!f.storage) return false;
    primary_frame *held = qa_unified_frame_lease_alloc(f.storage, 1, sizeof(*held), _Alignof(primary_frame), e);
    if (!held) { qa_unified_frame_lease_release(f.storage); return false; }
    *held = f; ++o->calls;
    bool ok = qa_pickups_run_source(application(o)->pickups, &held->offer, run_source, held, e);
    --o->calls;
    if (ok) *result = held->result;
    frame_retire(held); return ok;
}
typedef struct primary_source_call {
    primary_frame *frame;
    qa_native_address address;
    const qa_native_signature *signature;
    const qa_native_value *arguments;
    size_t count;
    qa_native_value *result;
} primary_source_call;
static bool source_call(void *context, qa_error *e)
{
    primary_source_call *call = context; primary_frame *f = call->frame;
    return f->event ? qa_native_region_invoke(instance(f->owner), f->event, call->address,
        call->signature, call->arguments, call->count, call->result, e) :
        qa_native_invoke(instance(f->owner), call->address, call->signature,
            call->arguments, call->count, call->result, e);
}
static bool committed(void *context)
{ return ((primary_frame *)context)->projections == 0; }
typedef struct primary_resources {
    primary_frame *frame;
    size_t next;
    qa_pickup_outcome outcome;
} primary_resources;
static bool resources(void *context, qa_error *e)
{
    primary_resources *call = context; primary_frame *f = call->frame;
    primary_grant *g = f->grant; size_t index = call->next;
    if (index < g->consumer_count) {
        call->next = index + 1;
        bool ok = application_native_q2_combat_resources(f->owner->engine, f->recipient.id,
            f->execution, g->consumers[index].channel, resources, call, committed, f, e);
        call->next = index; return ok;
    }
    if (!current(f, e) || !qa_pickup_execute_grant(f->execution, &call->outcome, e) || !current(f, e)) return false;
    if (call->outcome == QA_PICKUP_STALE) return cancel(f, PRIMARY_STALE, e);
    if (call->outcome != QA_PICKUP_ACCEPTED || (g->supply != PRIMARY_NO_SUPPLY && !f->accepted_ammo)) return true;
    for (size_t i = 0; i < g->consumer_count; ++i) {
        primary_consumer *consumer = g->consumers + i;
        if (!current(f, e)) return false;
        if (qa_combat_protection_owner(application(f->owner)->combat, f->recipient.id, consumer->channel, NULL, NULL)) continue;
        qa_native_value argument = {.type = QA_NATIVE_ADDRESS, .as.address = f->recipient_address}, result;
        primary_source_call invoke = {f, consumer->address, &consumer->signature, &argument, 1, &result};
        if (!source_call(&invoke, e) || !current(f, e)) return false;
    }
    return true;
}
static bool grant_resources(primary_frame *f, qa_pickup_outcome *out, qa_error *e)
{
    if (f->granted) return application_fail(e, QA_ERROR_ARGUMENT, "Native pickup repeated its selected resource grant");
    f->granted = true;
    primary_resources call = {.frame = f};
    if (!resources(&call, e)) return false;
    *out = call.outcome; return true;
}
static bool settle(primary_frame *f, qa_error *e)
{
    if (f->settled) return true;
    if (!projected_restore(f, e) || !current(f, e)) return false;
    f->settled = true;
    struct application_native_q2_primary_pickups *o = f->owner;
    primary_item *ammo = item(o, f->ammo_descriptor);
    if (f->grant->supply == PRIMARY_AMMO && (!ammo || !ammo->item))
        return application_fail(e, QA_ERROR_FORMAT, "Native ammunition grant lacks its original item descriptor");
    f->ammo = (qa_pickup_grant){ammo ? ammo->item : 0, f->ammo_amount};
    int32_t flags;
    if (!integer(o, f->descriptor + o->item_flags, 4, &flags, e)) return false;
    f->supply = (qa_supply_offer){.kind = f->grant->supply == PRIMARY_WEAPON ? QA_SUPPLY_WEAPON :
        ((uint32_t)flags & o->weapon_flag) ? QA_SUPPLY_AMMO_WEAPON : QA_SUPPLY_AMMO,
        .item = f->offer.item, .weapon = f->offer.item,
        .ammo = f->ammo.item ? &f->ammo : NULL, .ammo_count = f->ammo.item ? 1 : 0};
    f->supply_active = true; qa_pickup_outcome outcome = QA_PICKUP_REFUSED;
    bool ok = grant_resources(f, &outcome, e);
    f->supply_active = false;
    if (ok && outcome == QA_PICKUP_REFUSED) return cancel(f, PRIMARY_REFUSED, e);
    return ok;
}
static bool quantity(void *context, const qa_inventory_entry *entry, qa_supply_quantity *out, qa_error *e)
{
    primary_frame *f = context; struct application_native_q2_primary_pickups *o = f->owner;
    primary_item *ammo = item(o, f->ammo_descriptor); int32_t tag;
    if (!f->supply_active || !ammo || !ammo->item || !current(f, e)) return false;
    double count = entry->count, capacity = entry->capacity;
    if (!isfinite(count) || count != trunc(count) || count < INT32_MIN || count > INT32_MAX ||
        !isfinite(capacity) || capacity != trunc(capacity) || capacity < 0 ||
        capacity > (o->capacity_bytes == 2 ? INT16_MAX : INT32_MAX))
        return application_fail(e, QA_ERROR_ARGUMENT, "Selected ammunition quantity exceeds the original native counter ABI");
    if (!integer(o, f->ammo_descriptor + o->ammo_tag, 4, &tag, e)) return false;
    if (tag < 0 || (size_t)tag >= o->capacity_count)
        return application_fail(e, QA_ERROR_FORMAT, "Native ammunition has no declared source capacity");
    if (!application_native_q2_inventory_source_current(o->engine, &f->inventory, e)) return false;
    uint32_t index = (uint32_t)(ammo - o->items);
    qa_native_address counter = f->inventory.client + o->inventory_offset + (uint64_t)index * 4,
        cap = f->inventory.client + o->capacities[(size_t)tag];
    int32_t previous_count, previous_capacity, after = 0;
    if (!integer(o, counter, 4, &previous_count, e) || !integer(o, cap, o->capacity_bytes, &previous_capacity, e)) return false;
    ++f->projections;
    bool ok = store(o, counter, 4, (int32_t)count, e) && store(o, cap, o->capacity_bytes, (int32_t)capacity, e);
    qa_native_value arguments[] = {{.type = QA_NATIVE_ADDRESS, .as.address = f->recipient_address},
        {.type = QA_NATIVE_ADDRESS, .as.address = f->ammo_descriptor}, {.type = QA_NATIVE_I32, .as.i32 = f->ammo_amount}}, result = {0};
    bool cancelled = false;
    if (ok) {
        f->quantity_active = true;
        primary_source_call call = {f, o->ammo_address, &o->ammo_signature, arguments, 3, &result};
        ok = invoke_scope(f, PRIMARY_QUANTITY_STOP, source_call, &call, &cancelled, e);
        f->quantity_active = false;
    }
    if (ok) ok = current(f, e) && integer(o, counter, 4, &after, e);
    qa_error cleanup = {0}; bool restored = true;
    if (actor_current(f, true)) {
        restored = store(o, counter, 4, previous_count, &cleanup);
        if (!store(o, cap, o->capacity_bytes, previous_capacity, &cleanup)) restored = false;
    }
    --f->projections;
    if (!restored && ok) { ok = false; if (e) *e = cleanup; }
    if (ok) {
        bool accepted = cancelled || truth(&result);
        f->accepted_ammo |= accepted;
        *out = (qa_supply_quantity){.amount = (double)after - count, .exact = true, .accepted = accepted};
    }
    return ok;
}
static bool region(void *context, qa_native_instance *native, const qa_native_region_event *event,
    qa_native_region_decision *decision, qa_error *e)
{
    primary_region *r = context; struct application_native_q2_primary_pickups *o = r->owner;
    primary_frame *f = o->frames;
    *decision = (qa_native_region_decision){.action = QA_NATIVE_REGION_CONTINUE};
    if (!o->active || !f || f->selection == QA_PICKUP_SELECT_ORIGINAL ||
        (r->grant && f->grant != r->grant) || event->phase != QA_NATIVE_REGION_ENTER) return true;
    if (r->kind == PRIMARY_AMMO_STOP)
        return !f->quantity_active || cancel(f, PRIMARY_QUANTITY_STOP, e);
    if (!current(f, e)) return false;
    if (r->kind == PRIMARY_TARGETS) return true;
    if (f->selection != QA_PICKUP_SELECT_REPLACEMENT || !f->called) return true;
    const qa_native_region_event *previous = f->event; f->event = event; bool ok = true;
    switch (r->kind) {
    case PRIMARY_RECIPIENT:
        if (f->grant->supply == PRIMARY_AMMO) break;
        ok = projected_restore(f, e);
        if (ok) { f->skipped = true; decision->action = QA_NATIVE_REGION_SKIP_TO_JOIN; }
        break;
    case PRIMARY_AMMO_GRANT:
        f->ammo_descriptor = f->descriptor;
        f->ammo_amount = (int32_t)(uint32_t)event->state.registers[f->grant->amount];
        ok = settle(f, e);
        if (ok) { f->skipped = true; decision->action = QA_NATIVE_REGION_SKIP_TO_JOIN; }
        break;
    case PRIMARY_SETTLE: ok = settle(f, e); break;
    case PRIMARY_AUTOSWITCH:
        ok = settle(f, e);
        if (ok) decision->action = QA_NATIVE_REGION_SKIP_TO_JOIN;
        break;
    case PRIMARY_TARGETS: case PRIMARY_AMMO_STOP: break;
    }
    f->event = previous; (void)native; return ok;
}
static bool ammo_grant(void *context, qa_native_instance *native, qa_native_entry_observer *binding,
    const qa_native_value *arguments, size_t count, qa_native_value *result, qa_error *e)
{
    struct application_native_q2_primary_pickups *o = context; primary_frame *f = o->frames;
    if (!o->active || !f || f->quantity_active || f->selection != QA_PICKUP_SELECT_REPLACEMENT ||
        !f->called || f->grant->supply != PRIMARY_WEAPON)
        return qa_native_invoke_original(binding, arguments, count, result, e);
    qa_native_address caller;
    if (!qa_native_entry_caller(binding, &caller, e)) return false;
    if (caller != f->grant->ammo_return) return qa_native_invoke_original(binding, arguments, count, result, e);
    if (!current(f, e)) return false;
    if (arguments[0].as.address != f->recipient_address || f->ammo_descriptor)
        return application_fail(e, QA_ERROR_ARGUMENT, "Native weapon ammunition changed its original grant recipient");
    f->ammo_descriptor = arguments[1].as.address; f->ammo_amount = arguments[2].as.i32;
    *result = boolean(&o->ammo_signature, true); (void)native; return true;
}
static bool weapon_projection(primary_frame *f, qa_error *e)
{
    struct application_native_q2_primary_pickups *o = f->owner; qa_supply *supply;
    if (!application_supplies_for(application(o)->supplies, o->engine->provider, f->recipient.id, &supply, e)) return false;
    if (!supply) return true;
    bool owned;
    if (f->offer.cargo_count == 1) {
        double count;
        if (!qa_inventory_count_read(application(o)->inventory, f->recipient.id,
            f->offer.cargo[0].item, &count, e)) return false;
        owned = count > 0;
    } else if (!qa_supply_owns(supply, f->recipient.id, f->offer.item, &owned, e)) return false;
    if (!current(f, e)) return false;
    primary_item *row = item(o, f->descriptor);
    f->projected_counter = f->inventory.client + o->inventory_offset + (uint64_t)(row - o->items) * 4;
    if (!integer(o, f->projected_counter, 4, &f->projected_before, e)) return false;
    f->projected = true;
    return store(o, f->projected_counter, 4, owned, e);
}
static bool grant(void *context, qa_native_instance *native, qa_native_entry_observer *binding,
    const qa_native_value *arguments, size_t count, qa_native_value *result, qa_error *e)
{
    primary_grant *g = context; struct application_native_q2_primary_pickups *o = g->owner;
    primary_frame *f = o->frames;
    if (!o->active || !f || f->grant != g || f->selection == QA_PICKUP_SELECT_ORIGINAL)
        return qa_native_invoke_original(binding, arguments, count, result, e);
    qa_native_address caller;
    if (!qa_native_entry_caller(binding, &caller, e)) return false;
    if (caller != o->grant_return) return qa_native_invoke_original(binding, arguments, count, result, e);
    if (!current(f, e)) return false;
    if (f->called || arguments[0].as.address != f->pickup_address || arguments[1].as.address != f->recipient_address)
        return application_fail(e, QA_ERROR_ARGUMENT, "Native item grant changed its original touch invocation");
    f->called = true;
    if (f->selection == QA_PICKUP_SELECT_BLOCKED) { *result = boolean(&o->grant_signature, false); return true; }
    bool ok = true;
    if (g->supply == PRIMARY_NO_SUPPLY) {
        qa_pickup_outcome outcome = QA_PICKUP_REFUSED;
        if (!grant_resources(f, &outcome, e)) return false;
        if (outcome == QA_PICKUP_REFUSED) { *result = boolean(&o->grant_signature, false); return true; }
    } else {
        if (!application_native_q2_inventory_source_read(o->engine, f->recipient.id, &f->inventory, e)) return false;
        if (g->supply == PRIMARY_WEAPON) ok = weapon_projection(f, e);
    }
    bool cancelled = false;
    if (ok) {
        primary_original call = {f, binding, arguments, count, result};
        ok = invoke_scope(f, PRIMARY_REFUSED, original, &call, &cancelled, e);
    }
    qa_error cleanup = {0};
    if (!projected_restore(f, &cleanup)) { if (ok && e) *e = cleanup; ok = false; }
    if (ok && cancelled) { *result = boolean(&o->grant_signature, false); return true; }
    if (ok) ok = current(f, e);
    if (ok && truth(result) && (!f->skipped || (g->supply != PRIMARY_NO_SUPPLY && !f->settled)))
        ok = application_fail(e, QA_ERROR_FORMAT, "Native pickup accepted before its declared resource continuation");
    if (ok && g->supply == PRIMARY_NO_SUPPLY && !truth(result))
        ok = application_fail(e, QA_ERROR_FORMAT, "Native pickup rejected its already committed selected grant");
    (void)native; return ok;
}
bool application_native_q2_primary_pickups_supply(struct application_native_q2_primary_pickups *o,
    const qa_pickup_offer *offer, qa_supply_offer *out, qa_supply_options *options, qa_error *e)
{
    primary_frame *f = o ? o->frames : NULL;
    if (!f || !offer || !out || !options || !f->granted || !f->supply_active ||
        offer->source != f->offer.source || offer->item != f->offer.item ||
        !qa_actor_id_equal(offer->pickup, f->pickup.id) || !qa_actor_id_equal(offer->recipient, f->recipient.id) || !current(f, e))
        return application_fail(e, QA_ERROR_ARGUMENT, "Native selected supply requires its held original item grant");
    *out = f->supply;
    *options = (qa_supply_options){.selection = f->supply.kind == QA_SUPPLY_AMMO_WEAPON ?
        QA_PICKUP_SWITCH_ALWAYS : QA_PICKUP_SWITCH_IF_BETTER,
        .auto_switch = true, .only_empty = f->supply.kind == QA_SUPPLY_AMMO_WEAPON,
        .quantity = f->ammo_descriptor ? quantity : NULL, .quantity_context = f};
    return true;
}
static bool region_add(struct application_native_q2_primary_pickups *o, const char *path,
    primary_region_kind kind, primary_grant *grant, qa_error *e)
{
    qa_native_declared_region declared;
    if (!qa_native_declaration_find_region(o->declaration, path, &declared, e)) return false;
    o->regions[o->region_count++] = (primary_region){.owner = o, .grant = grant, .kind = kind, .id = declared.id};
    return true;
}
static bool signature_shape(const qa_native_signature *signature, size_t count,
    qa_native_value_type result, qa_error *e)
{
    if (signature->parameter_count != count || signature->result.kind != result || signature->variadic)
        return application_fail(e, QA_ERROR_FORMAT, "Native pickup call differs from its declared source ABI");
    return true;
}
bool application_native_q2_primary_pickups_prepare(struct application_native_q2 *engine, qa_error *e)
{
    if (engine->primary_pickups || !engine->declaration || !qa_native_declaration_primary(engine->declaration).size ||
        qa_native_declaration_kind(engine->declaration, "/pickups") == QA_JSON_INVALID ||
        qa_native_declaration_kind(engine->declaration, "/pickups") == QA_JSON_NULL) return true;
    struct application_native_q2_primary_pickups *o = calloc(1, sizeof(*o));
    if (!o) return application_fail(e, QA_ERROR_MEMORY, "Preparing native primary pickup continuations");
    o->engine = engine; o->declaration = engine->declaration; engine->primary_pickups = o;
    qa_native_module_info info = qa_native_module_describe(engine->provider->state.native.module);
    o->pointer_bytes = info.image.target.pointer_bytes;
    if (!qa_json_parse(QA_JSON_STRICT, qa_native_declaration_primary(o->declaration), &o->document, e)) return false;
    const qa_json_document *d = o->document;
    qa_json_id root = qa_json_get(d, qa_json_root(d), "pickups"), items = qa_json_get(d, root, "items"),
        entity = qa_json_get(d, root, "entity"), time = qa_json_get(d, root, "time"),
        supply = qa_json_get(d, root, "supply"), ammo = qa_json_get(d, supply, "ammo"),
        rows = qa_json_get(d, root, "grants");
    uint32_t inuse_bytes, capacity_bytes;
    if (qa_json_type(d, root) != QA_JSON_OBJECT || qa_json_type(d, rows) != QA_JSON_ARRAY ||
        !word(d, root, "touch", &o->touch_rva, e) || !word(d, root, "grantReturn", &o->grant_return_rva, e) ||
        !word(d, items, "table", &o->table_rva, e) || !word(d, items, "stride", &o->item_stride, e) ||
        !word(d, items, "count", &o->item_count, e) || !word(d, items, "classname", &o->item_classname, e) ||
        !word(d, items, "pickup", &o->item_pickup, e) || !word(d, entity, "item", &o->entity_item, e) ||
        !word(d, entity, "count", &o->entity_count, e) || !word(d, entity, "spawnflags", &o->entity_flags, e) ||
        !word(d, entity, "inuse", &o->inuse, e) || !word(d, entity, "inuseBytes", &inuse_bytes, e) ||
        !word(d, time, "address", &o->time_rva, e) || !word(d, supply, "client", &o->client_pointer, e) ||
        !word(d, supply, "inventory", &o->inventory_offset, e) || !word(d, supply, "flags", &o->item_flags, e) ||
        !word(d, supply, "weaponFlag", &o->weapon_flag, e) || !word(d, ammo, "entry", &o->ammo_rva, e) ||
        !word(d, ammo, "tag", &o->ammo_tag, e) || !word(d, ammo, "capacityBytes", &capacity_bytes, e)) return false;
    if (!o->item_count || !o->item_stride || o->item_classname > o->item_stride ||
        o->pointer_bytes > o->item_stride - o->item_classname || o->item_pickup > o->item_stride ||
        o->pointer_bytes > o->item_stride - o->item_pickup || o->item_flags > o->item_stride ||
        o->item_stride - o->item_flags < 4 || o->ammo_tag > o->item_stride ||
        o->item_stride - o->ammo_tag < 4 || (inuse_bytes != 1 && inuse_bytes != 4) ||
        (capacity_bytes != 2 && capacity_bytes != 4))
        return application_fail(e, QA_ERROR_FORMAT, "Native pickup fields exceed their declared source records");
    o->inuse_bytes = (uint8_t)inuse_bytes; o->capacity_bytes = (uint8_t)capacity_bytes;
    qa_json_id generation = qa_json_get(d, entity, "generation");
    o->has_generation = generation != QA_JSON_NONE && qa_json_type(d, generation) != QA_JSON_NULL;
    if (o->has_generation && !word(d, entity, "generation", &o->generation, e)) return false;
    if (qa_json_string_equal(d, qa_json_get(d, time, "storage"), "int64-milliseconds")) o->milliseconds = true;
    else if (!qa_json_string_equal(d, qa_json_get(d, time, "storage"), "float32-seconds"))
        return application_fail(e, QA_ERROR_FORMAT, "Native pickup clock has no declared source encoding");
    if (!qa_native_declaration_signature(o->declaration, "/pickups/touchSignature", &o->signatures, &o->touch_signature, e) ||
        !qa_native_declaration_signature(o->declaration, "/pickups/grantSignature", &o->signatures, &o->grant_signature, e) ||
        !qa_native_declaration_signature(o->declaration, "/pickups/supply/ammo/signature", &o->signatures, &o->ammo_signature, e)) return false;
    qa_native_value_type grant_result = o->grant_signature.result.kind, ammo_result = o->ammo_signature.result.kind;
    if ((grant_result != QA_NATIVE_I32 && grant_result != QA_NATIVE_U8 && grant_result != QA_NATIVE_U32) ||
        (ammo_result != QA_NATIVE_I32 && ammo_result != QA_NATIVE_U8 && ammo_result != QA_NATIVE_U32) ||
        o->touch_signature.parameter_count < 2 ||
        !signature_shape(&o->touch_signature, o->touch_signature.parameter_count, QA_NATIVE_VOID, e) ||
        !signature_shape(&o->grant_signature, 2, grant_result, e) ||
        !signature_shape(&o->ammo_signature, 3, ammo_result, e) ||
        o->touch_signature.parameters[0].kind != QA_NATIVE_ADDRESS ||
        o->touch_signature.parameters[1].kind != QA_NATIVE_ADDRESS ||
        o->grant_signature.parameters[0].kind != QA_NATIVE_ADDRESS ||
        o->grant_signature.parameters[1].kind != QA_NATIVE_ADDRESS ||
        o->ammo_signature.parameters[0].kind != QA_NATIVE_ADDRESS ||
        o->ammo_signature.parameters[1].kind != QA_NATIVE_ADDRESS || o->ammo_signature.parameters[2].kind != QA_NATIVE_I32)
        return application_fail(e, QA_ERROR_FORMAT, "Native pickup requires its actual original item call signatures");
    qa_json_id capacities = qa_json_get(d, ammo, "capacities");
    o->capacity_count = qa_json_size(d, capacities); o->grant_count = qa_json_size(d, rows);
    if (qa_json_type(d, capacities) != QA_JSON_ARRAY || !o->capacity_count ||
        o->capacity_count > SIZE_MAX / sizeof(*o->capacities) || o->grant_count > SIZE_MAX / sizeof(*o->grants) ||
        o->grant_count > (SIZE_MAX - 2) / 3 || o->grant_count * 3 + 2 > SIZE_MAX / sizeof(*o->regions))
        return application_fail(e, QA_ERROR_FORMAT, "Native pickup declaration exceeds its source row extents");
    o->capacities = calloc(o->capacity_count, sizeof(*o->capacities));
    o->grants = o->grant_count ? calloc(o->grant_count, sizeof(*o->grants)) : NULL;
    o->regions = calloc(o->grant_count * 3 + 2, sizeof(*o->regions));
    o->items = calloc(o->item_count, sizeof(*o->items));
    if (!o->capacities || (o->grant_count && !o->grants) || !o->regions || !o->items)
        return application_fail(e, QA_ERROR_MEMORY, "Retaining native source pickup descriptors");
    uint32_t client_bytes = application_native_q2_inventory_client_bytes(engine);
    for (size_t i = 0; i < o->capacity_count; ++i) {
        uint64_t n;
        if (!qa_json_u64(d, qa_json_at(d, capacities, i), &n, e)) return false;
        if (n > client_bytes || o->capacity_bytes > client_bytes - n)
            return application_fail(e, QA_ERROR_FORMAT, "Native pickup capacity exceeds its actual client record");
        o->capacities[i] = (uint32_t)n;
    }
    for (size_t i = 0; i < o->grant_count; ++i) {
        primary_grant *g = o->grants + i; g->owner = o;
        qa_json_id row = qa_json_at(d, rows, i), operation = qa_json_get(d, row, "supply"),
            consumers = qa_json_get(d, row, "consumers");
        char path[128];
        if (!word(d, row, "entry", &g->rva, e)) return false;
        if (qa_json_string_equal(d, qa_json_get(d, row, "resource"), "regular")) g->resource = QA_PICKUP_PROTECTION;
        else if (qa_json_string_equal(d, qa_json_get(d, row, "resource"), "inventory")) g->resource = QA_PICKUP_INVENTORY;
        else return application_fail(e, QA_ERROR_FORMAT, "Native pickup resource has no canonical destination");
        snprintf(path, sizeof(path), "/pickups/grants/%zu/recipient", i);
        if (!region_add(o, path, PRIMARY_RECIPIENT, g, e)) return false;
        if (operation != QA_JSON_NONE && qa_json_type(d, operation) != QA_JSON_NULL) {
            if (qa_json_string_equal(d, qa_json_get(d, operation, "kind"), "ammo")) {
                g->supply = PRIMARY_AMMO; qa_buffer name = {0};
                bool ok = qa_json_string(d, qa_json_get(d, operation, "amount"), &name, e) &&
                    qa_native_register_find((qa_bytes){name.data, name.size}, &g->amount);
                qa_buffer_free(&name);
                if (!ok || g->amount == QA_NATIVE_RSP || (o->pointer_bytes == 4 && g->amount >= QA_NATIVE_R8))
                    return application_fail(e, QA_ERROR_FORMAT, "Native ammo grant has no original count register");
                snprintf(path, sizeof(path), "/pickups/grants/%zu/supply", i);
                if (!region_add(o, path, PRIMARY_AMMO_GRANT, g, e)) return false;
            } else if (qa_json_string_equal(d, qa_json_get(d, operation, "kind"), "weapon")) {
                g->supply = PRIMARY_WEAPON;
                if (!word(d, operation, "ammoReturn", &g->ammo_return_rva, e)) return false;
                snprintf(path, sizeof(path), "/pickups/grants/%zu/settle", i);
                if (!region_add(o, path, PRIMARY_SETTLE, g, e)) return false;
                snprintf(path, sizeof(path), "/pickups/grants/%zu/supply/autoswitch", i);
                if (!region_add(o, path, PRIMARY_AUTOSWITCH, g, e)) return false;
            } else return application_fail(e, QA_ERROR_FORMAT, "Native pickup has an unknown source supply operation");
        }
        g->consumer_count = qa_json_size(d, consumers);
        if ((consumers != QA_JSON_NONE && qa_json_type(d, consumers) != QA_JSON_ARRAY) ||
            g->consumer_count > SIZE_MAX / sizeof(*g->consumers))
            return application_fail(e, QA_ERROR_FORMAT, "Native pickup consumers are not source call rows");
        g->consumers = g->consumer_count ? calloc(g->consumer_count, sizeof(*g->consumers)) : NULL;
        if (g->consumer_count && !g->consumers) return application_fail(e, QA_ERROR_MEMORY, "Retaining native pickup protection consumers");
        for (size_t j = 0; j < g->consumer_count; ++j) {
            primary_consumer *c = g->consumers + j; qa_json_id consumer = qa_json_at(d, consumers, j);
            if (!word(d, consumer, "entry", &c->rva, e)) return false;
            snprintf(path, sizeof(path), "/pickups/grants/%zu/consumers/%zu/signature", i, j);
            if (!qa_native_declaration_signature(o->declaration, path, &o->signatures, &c->signature, e) ||
                !signature_shape(&c->signature, 1, QA_NATIVE_VOID, e) || c->signature.parameters[0].kind != QA_NATIVE_ADDRESS) return false;
            if (qa_json_string_equal(d, qa_json_get(d, consumer, "protection"), "regular")) c->channel = QA_PROTECTION_REGULAR;
            else if (qa_json_string_equal(d, qa_json_get(d, consumer, "protection"), "powered")) c->channel = QA_PROTECTION_POWERED;
            else return application_fail(e, QA_ERROR_FORMAT, "Native pickup consumer has no original protection channel");
        }
    }
    if (!region_add(o, "/pickups/targets", PRIMARY_TARGETS, NULL, e)) return false;
    qa_json_id stop = qa_json_get(d, ammo, "stop");
    if (stop != QA_JSON_NONE && qa_json_type(d, stop) != QA_JSON_NULL &&
        !region_add(o, "/pickups/supply/ammo/stop", PRIMARY_AMMO_STOP, NULL, e)) return false;
    return true;
}
bool application_native_q2_primary_pickups_activate(struct application_native_q2 *engine, qa_error *e)
{
    struct application_native_q2_primary_pickups *o = engine->primary_pickups;
    if (!o || o->active) return true;
    if (!rva(o, o->touch_rva, &o->touch_address, e) || !rva(o, o->ammo_rva, &o->ammo_address, e) ||
        !rva(o, o->grant_return_rva, &o->grant_return, e) || !rva(o, o->time_rva, &o->time, e)) return false;
    for (size_t i = 0; i < o->grant_count; ++i) {
        primary_grant *g = o->grants + i;
        if (!rva(o, g->rva, &g->address, e) ||
            (g->supply == PRIMARY_WEAPON && !rva(o, g->ammo_return_rva, &g->ammo_return, e))) return false;
        for (size_t j = 0; j < g->consumer_count; ++j)
            if (!rva(o, g->consumers[j].rva, &g->consumers[j].address, e)) return false;
    }
    if ((uint64_t)o->item_stride * o->item_count > SIZE_MAX ||
        !qa_native_rva(instance(o), o->table_rva, (size_t)o->item_stride * o->item_count, &o->table, e)) return false;
    for (uint32_t i = 0; i < o->item_count; ++i) {
        primary_item *row = o->items + i; row->descriptor = o->table + (uint64_t)i * o->item_stride; row->item = 0;
        qa_native_address classname;
        if (!pointer(o, row->descriptor + o->item_classname, &classname, e)) return false;
        if (classname) {
            qa_error lookup = {0};
            if (!application_native_q2_inventory_item(engine, i, &row->item, &lookup) && lookup.code != QA_ERROR_NOT_FOUND) {
                if (e) *e = lookup;
                return false;
            }
        }
    }
    if (!o->touch && !qa_native_observe_entry(instance(o), o->touch_address, &o->touch_signature, touch, o, &o->touch, e)) return false;
    if (!o->ammo && !qa_native_observe_entry(instance(o), o->ammo_address, &o->ammo_signature, ammo_grant, o, &o->ammo, e)) return false;
    for (size_t i = 0; i < o->grant_count; ++i) {
        primary_grant *g = o->grants + i;
        if (!g->binding && !qa_native_observe_entry(instance(o), g->address, &o->grant_signature, grant, g, &g->binding, e)) return false;
    }
    for (size_t i = 0; i < o->region_count; ++i) {
        primary_region *r = o->regions + i;
        if (!r->binding && !qa_native_bind_region(instance(o), r->id, region, r, &r->binding, e)) return false;
    }
    o->active = true; return true;
}
bool application_native_q2_primary_pickups_returned(const struct application_native_q2_primary_pickups *o)
{ return !o || (!o->frames && !o->calls); }
bool application_native_q2_primary_pickups_idle(const struct application_native_q2_primary_pickups *o)
{ return application_native_q2_primary_pickups_returned(o) && (!o || !o->pending); }
bool application_native_q2_primary_pickups_suspend(struct application_native_q2 *engine, qa_error *e)
{
    struct application_native_q2_primary_pickups *o = engine->primary_pickups;
    if (!o) return true;
    if (!application_native_q2_primary_pickups_returned(o))
        return application_fail(e, QA_ERROR_ARGUMENT, "Native pickup suspension retains an original item call");
    while (o->pending) {
        primary_frame *f = o->pending;
        if (!frame_cleanup(f, e)) return false;
        o->pending = f->pending_next; qa_unified_frame_lease_release(f->storage);
    }
    o->active = false;
    for (size_t i = 0; i < o->region_count; ++i) {
        primary_region *r = o->regions + i;
        if (!qa_native_remove_region(r->binding, e)) return false;
        r->binding = NULL;
    }
    for (size_t i = 0; o->grants && i < o->grant_count; ++i) {
        if (!qa_native_unobserve_entry(o->grants[i].binding, e)) return false;
        o->grants[i].binding = NULL;
    }
    if (!qa_native_unobserve_entry(o->ammo, e)) return false;
    o->ammo = NULL;
    if (!qa_native_unobserve_entry(o->touch, e)) return false;
    o->touch = NULL; return true;
}
bool application_native_q2_primary_pickups_close(struct application_native_q2 *engine, qa_error *e)
{
    struct application_native_q2_primary_pickups *o = engine->primary_pickups;
    if (!o) return true;
    if (!application_native_q2_primary_pickups_suspend(engine, e)) return false;
    for (size_t i = 0; o->grants && i < o->grant_count; ++i) free(o->grants[i].consumers);
    free(o->grants); free(o->regions); free(o->items); free(o->capacities);
    qa_arena_destroy(&o->signatures); qa_json_destroy(o->document); free(o);
    engine->primary_pickups = NULL; return true;
}
