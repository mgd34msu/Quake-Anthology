#include "native_q2_inventory_scanner.h"
#include "native_q2_inventory_source.h"
#include "guest_native_q2_private.h"
#include "guest_native_q2_attack.h"
#include "guest_native_q2_combat.h"
#include "native_q2_callbacks.h"
#include "control_frame.h"
#include "qa/native_observe.h"
#include "qa/text.h"
#include "qa/network.h"
#include <math.h>

typedef struct inventory_write_field { uint32_t offset, bytes; } inventory_write_field;
enum { INVENTORY_ENTRY_GIVE = 8, INVENTORY_ENTRY_DEATH_DROP, INVENTORY_ENTRY_DROP_ALLOCATE };
enum { INVENTORY_GIVE_WEAPONS = 6, INVENTORY_GIVE_AMMO,
    INVENTORY_GIVE_UNKNOWN, INVENTORY_GIVE_AMMO_GRANT, INVENTORY_DEATH_DROP,
    INVENTORY_DROP_DEBIT, INVENTORY_DROP_CONSUMER };
typedef enum inventory_drop_debit {
    INVENTORY_DROP_PENDING, INVENTORY_DROP_ACCEPTED, INVENTORY_DROP_REFUSED
} inventory_drop_debit;
typedef struct inventory_selection {
    struct inventory_selection *next;
    qa_actor_id actor;
    qa_item_id item;
    uint32_t index;
    size_t storage_slot;
} inventory_selection;
typedef struct inventory_restore {
    struct inventory_restore *next;
    application_native_q2_inventory_source source;
    qa_native_address address;
    qa_buffer bytes;
    qa_unified_frame_lease *storage;
} inventory_restore;
typedef struct inventory_frame {
    struct inventory_frame *previous;
    qa_actor_id actor;
    int32_t flags;
    bool named, dropping, giving, death_drop, has_named_row;
    bool drop_weapon;
    inventory_drop_debit drop_debit;
    qa_actor_id drop_pickup;
    application_native_q2_inventory_source drop_source;
    qa_native_address drop_counter;
    struct inventory_region *debit_region;
    int32_t debit_before;
    struct application_native_q2_resource_scope *consumer_scope;
    struct inventory_region *ammo_region;
    application_native_q2_inventory_source ammo_source;
    uint32_t ammo_index;
    int32_t ammo_before;
    application_native_q2_inventory_row named_row;
    qa_buffer requested;
    qa_arena requested_storage;
    qa_unified_frame_lease *storage;
    inventory_restore *restore;
} inventory_frame;
typedef struct inventory_entry {
    application_native_q2_inventory_scanner *owner;
    qa_native_entry_observer *binding;
    qa_native_address address;
    qa_native_type parameters[3];
    qa_native_signature signature;
    unsigned kind;
} inventory_entry;
typedef struct inventory_region {
    application_native_q2_inventory_scanner *owner;
    qa_native_region_binding *binding;
    uint32_t id;
    unsigned kind;
    int direction;
    qa_native_register descriptor;
    bool set;
} inventory_region;
struct application_native_q2_inventory_scanner {
    struct application_native_q2 *engine;
    const qa_native_declaration *declaration;
    qa_json_document *document;
    application_native_q2_inventory_scanner_options options;
    inventory_write_field *writes;
    size_t write_count;
    inventory_entry entries[11];
    inventory_region *regions;
    size_t entry_count, region_count;
    qa_arena readout_storage;
    qa_pool readout_pages;
    qa_arena selection_storage, selection_index_storage, name_storage;
    qa_pool selection_pool;
    uint8_t *zero;
    inventory_selection *selected;
    qa_actor_index selection_index;
    inventory_frame *frame;
    inventory_restore *pending;
    const qa_native_region_event *command_event;
    qa_actor_id command_actor;
    uint32_t inventory, count, cursor, client_pointer, item_table, item_stride, item_count;
    uint32_t drop_weapon, drop_pending, death_ammo_index;
    qa_native_address drop_lookup_return;
    uint32_t drop_free;
    int32_t empty;
    qa_native_abi abi;
    uint8_t pointer_bytes;
    unsigned calls, evaluating;
    bool active;
};
static qa_native_instance *instance(application_native_q2_inventory_scanner *o)
{ return qa_native_host_instance(o->engine->provider->state.native.host); }
static bool current(application_native_q2_inventory_scanner *o, qa_error *e)
{
    return o && o->engine && o->engine->declaration == o->declaration && o->engine->provider->state.native.host &&
        !o->engine->shutting_down && !qa_native_terminal(instance(o))
        ? true : application_fail(e, QA_ERROR_ARGUMENT, "Native inventory scanner lost its acquired source owner");
}
static bool word(const qa_json_document *d, qa_json_id object, const char *key, uint32_t *out, qa_error *e)
{
    uint64_t value;
    if (!qa_json_u64(d, qa_json_get(d, object, key), &value, e)) return false;
    if (value > UINT32_MAX) return application_fail(e, QA_ERROR_FORMAT, "Native inventory profile word overflows");
    *out = (uint32_t)value; return true;
}
static bool address(application_native_q2_inventory_scanner *o, uint32_t rva, qa_native_address *out, qa_error *e)
{ return qa_native_rva(instance(o), rva, 1, out, e); }
static bool read_word(application_native_q2_inventory_scanner *o, qa_native_address at, int32_t *out, qa_error *e)
{
    uint8_t bytes[4];
    if (!qa_native_read(instance(o), at, bytes, sizeof(bytes), e)) return false;
    *out = qa_load_i32le(bytes); return true;
}
static bool write_word(application_native_q2_inventory_scanner *o, qa_native_address at, int32_t value, qa_error *e)
{
    uint8_t bytes[4]; qa_store_u32le(bytes, (uint32_t)value);
    return qa_native_write(instance(o), at, (qa_bytes){bytes, sizeof(bytes)}, e);
}
static inventory_selection *selection(application_native_q2_inventory_scanner *o, qa_actor_id actor)
{
    size_t value;
    return qa_actor_index_find_retained(&o->selection_index,actor,&value)?(inventory_selection *)(uintptr_t)value:NULL;
}
static inventory_selection *selection_take(application_native_q2_inventory_scanner *o, qa_error *e)
{
    if (o->selection_pool.head == SIZE_MAX &&
        !qa_pool_extend(&o->selection_pool, &o->selection_storage,
            o->selection_pool.capacity ? o->selection_pool.capacity : 1, e)) return NULL;
    size_t slot;
    inventory_selection *s = qa_pool_take(&o->selection_pool, &slot);
    if (s) *s = (inventory_selection){.storage_slot = slot};
    return s;
}
static void selection_drop(application_native_q2_inventory_scanner *o, inventory_selection *s)
{ if (s) qa_pool_release(&o->selection_pool, s->storage_slot); }
void application_native_q2_inventory_scanner_release(application_native_q2_inventory_scanner *o, qa_actor_id actor)
{
    if (!o) return;
    inventory_selection *s=selection(o,actor);
    if(!s) return;
    inventory_selection **p=&o->selected;
    while(*p!=s) p=&(*p)->next;
    *p=s->next;
    qa_actor_index_forget_retained(&o->selection_index,actor);
    selection_drop(o, s);
}
static bool selection_install(application_native_q2_inventory_scanner *o,inventory_selection *s,qa_error *e)
{
    inventory_selection *previous=selection(o,s->actor);
    if(!qa_actor_index_remember(&o->selection_index,s->actor,(size_t)(uintptr_t)s,e)) return false;
    if(previous) {
        inventory_selection **p=&o->selected;
        while(*p!=previous) p=&(*p)->next;
        *p=previous->next;selection_drop(o, previous);
    }
    s->next=o->selected;o->selected=s;return true;
}
void application_native_q2_inventory_readout_free(application_native_q2_inventory_readout *r)
{
    if (!r) return;
    if (r->storage) qa_pool_release_run(r->storage,r->storage_slot,r->storage_pages);
    *r=(application_native_q2_inventory_readout){0};
}
static bool rows(application_native_q2_inventory_scanner *o,qa_actor_id actor, bool retain,
    application_native_q2_inventory_readout *out,qa_error *e)
{
    const application_native_q2_inventory_row *borrowed=NULL;size_t count=0;bool present=false;
    if(!o->options.rows(o->options.context,actor,&borrowed,&count,&present,e)) return false;
    if((!present&&(count||borrowed)) || (count&&!borrowed) || count>SIZE_MAX/sizeof(*borrowed))
        return application_fail(e,QA_ERROR_ARGUMENT,"Native inventory row producer returned an invalid acquisition");
    application_native_q2_inventory_readout r={.present=present};
    if(!count) {*out=r;return true;}
    if (!retain) { r.rows = (application_native_q2_inventory_row *)borrowed; r.count = count; *out = r; return true; }
    size_t bytes=count*sizeof(*borrowed)+128;
    for(size_t i=0;i<count;++i) {
        if(!borrowed[i].item || !borrowed[i].label || borrowed[i].source_index>=o->count)
            return application_fail(e,QA_ERROR_ARGUMENT,"Mixed inventory row lacks its actual source prototype");
        bytes+=strlen(borrowed[i].label)+1;
        if(borrowed[i].presentation.icon) bytes+=strlen(borrowed[i].presentation.icon)+1;
        if(borrowed[i].presentation.lump) bytes+=strlen(borrowed[i].presentation.lump)+1;
    }
    r.storage_pages=(bytes+o->readout_pages.stride-1)/o->readout_pages.stride;
    if(!qa_pool_run_available(&o->readout_pages,r.storage_pages)&&
        !qa_pool_extend(&o->readout_pages,&o->readout_storage,r.storage_pages,e))return false;
    void *memory=qa_pool_take_run(&o->readout_pages,r.storage_pages,&r.storage_slot);
    if(!memory) return application_fail(e,QA_ERROR_CAPACITY,"Native inventory exhausted load-sized readout pages");
    r.storage=&o->readout_pages;
    qa_arena arena={0};
    bool okay=qa_arena_init_buffer(&arena,memory,r.storage_pages*o->readout_pages.stride,e);
    r.rows=okay?qa_arena_alloc(&arena,count*sizeof(*r.rows),_Alignof(application_native_q2_inventory_row),e):NULL;
    okay=r.rows!=NULL;
    for(size_t i=0;okay && i<count;++i) {
        okay = application_native_q2_inventory_row_copy(&arena, borrowed + i, r.rows + i, e);
        ++r.count;
        for(size_t j=0;okay && j<i;++j) if(r.rows[j].item==r.rows[i].item)
            okay=application_fail(e,QA_ERROR_ARGUMENT,"Mixed inventory producer repeats a canonical item");
    }
    if(!okay) {application_native_q2_inventory_readout_free(&r);return false;}
    *out=r;return true;
}
static bool source(application_native_q2_inventory_scanner *o, qa_actor_id actor,
    application_native_q2_inventory_source *out, qa_error *e)
{
    if (!current(o, e) || !application_native_q2_inventory_source_read(o->engine, actor, out, e)) return false;
    if (out->inventory_offset != o->inventory || out->count != o->count || out->cursor_offset != o->cursor || out->empty != o->empty)
        return application_fail(e, QA_ERROR_ARGUMENT, "Native scanner profile differs from its actual inventory owner");
    return true;
}
static bool cursor(application_native_q2_inventory_scanner *o, qa_actor_id actor,
    const application_native_q2_inventory_readout *r, size_t *out, qa_error *e)
{
    application_native_q2_inventory_source s; int32_t index;
    if (!source(o, actor, &s, e) || !read_word(o, s.client + o->cursor, &index, e)) return false;
    *out = SIZE_MAX; inventory_selection *tracked = selection(o, actor);
    if (tracked && index >= 0 && tracked->index == (uint32_t)index) {
        for (size_t i = 0; i < r->count; ++i) if (r->rows[i].item == tracked->item) { *out = i; return true; }
        application_native_q2_inventory_scanner_release(o, actor);
        return write_word(o, s.client + o->cursor, o->empty, e);
    }
    application_native_q2_inventory_scanner_release(o, actor);
    for (size_t i = 0; i < r->count; ++i)
        if (!r->rows[i].selected && index >= 0 && r->rows[i].source_index == (uint32_t)index) { *out = i; break; }
    return true;
}
static qa_unified_frame_lease *frame_storage(application_native_q2_inventory_scanner *o,
    inventory_frame *frame, qa_error *e)
{
    if (!frame->storage)
        frame->storage = application_control_storage_acquire(o->engine->provider->application, e);
    return frame->storage;
}
static bool save_bytes(application_native_q2_inventory_scanner *o, qa_unified_frame_lease *storage,
    const application_native_q2_inventory_source *s, uint32_t offset, size_t count,
    inventory_restore **chain, qa_error *e)
{
    if (!storage) return false;
    inventory_restore *r = qa_unified_frame_lease_alloc(storage, 1, sizeof(*r), _Alignof(inventory_restore), e);
    if (!r) return false;
    r->source = *s; r->address = s->client + offset;
    r->bytes.data = qa_unified_frame_lease_alloc(storage, count, 1, 1, e); r->bytes.size = count;
    if (!r->bytes.data || !qa_native_read(instance(o), r->address, r->bytes.data, count, e)) {
        return false;
    }
    if (!qa_unified_frame_lease_retain(storage, e)) return false;
    r->storage = storage;
    r->next = *chain; *chain = r; return true;
}
static void discard(inventory_restore **chain)
{
    while (*chain) { inventory_restore *r = *chain; *chain = r->next; qa_unified_frame_lease_release(r->storage); }
}
static bool restore(application_native_q2_inventory_scanner *o, inventory_restore **chain, qa_error *e)
{
    while (*chain) {
        inventory_restore *r = *chain;
        if (qa_actors_get(qa_session_actors(o->engine->provider->application->session), r->source.actor)) {
            if (!application_native_q2_inventory_source_current(o->engine, &r->source, e) ||
                !qa_native_write(instance(o), r->address, (qa_bytes){r->bytes.data, r->bytes.size}, e)) return false;
        }
        *chain = r->next; qa_unified_frame_lease_release(r->storage);
    }
    return true;
}
static void pending(application_native_q2_inventory_scanner *o, inventory_restore **chain)
{
    if (!*chain) return;
    inventory_restore *tail = *chain;
    while (tail->next) tail = tail->next;
    tail->next = o->pending; o->pending = *chain; *chain = NULL;
}
static double source_count(const application_native_q2_inventory_row *r)
{ return r->presence_only ? r->count != 0 ? 1 : 0 : r->count; }
static bool count_write(application_native_q2_inventory_scanner *o, qa_native_address address,
    const application_native_q2_inventory_row *r, qa_error *e)
{
    double n = source_count(r);
    if (!isfinite(n) || n <= -2147483649.0 || n >= 2147483648.0)
        return application_fail(e, QA_ERROR_ARGUMENT, "Projected inventory count exceeds original signed storage");
    return write_word(o, address, (int32_t)n, e);
}
static bool evaluate(application_native_q2_inventory_scanner *o, qa_actor_id actor,
    const application_native_q2_inventory_row *r, int direction, int32_t flags,
    const qa_native_region_event *event, bool *accepted, qa_error *e)
{
    application_native_q2_inventory_source s; inventory_restore *inventory = NULL, *writes = NULL;
    *accepted = false;
    if (!source(o, actor, &s, e) || !r->source_index || r->source_index >= o->count) return false;
    qa_unified_frame_lease *storage = application_control_storage_acquire(o->engine->provider->application, e);
    if (!storage) return false;
    if (!save_bytes(o, storage, &s, o->inventory, (size_t)o->count * 4, &inventory, e)) {
        qa_unified_frame_lease_release(storage); return false;
    }
    for (size_t i = 0; i < o->write_count; ++i)
        if (!save_bytes(o, storage, &s, o->writes[i].offset, o->writes[i].bytes, &writes, e)) {
            discard(&inventory); discard(&writes); qa_unified_frame_lease_release(storage); return false;
        }
    ++o->evaluating;
    bool ok = qa_native_write(instance(o), s.client + o->inventory, (qa_bytes){o->zero, (size_t)o->count * 4}, e) &&
        count_write(o, s.client + o->inventory + (uint64_t)r->source_index * 4, r, e) &&
        write_word(o, s.client + o->cursor, 0, e);
    inventory_entry *entry = &o->entries[direction > 0 ? 0 : 1];
    qa_native_value arguments[3] = {{.type = QA_NATIVE_ADDRESS, .as.address = s.entity},
        {.type = QA_NATIVE_I32, .as.i32 = flags}, {.type = QA_NATIVE_U8, .as.u8 = 0}};
    qa_native_value result = {.type = QA_NATIVE_VOID}; int32_t selected;
    if (ok) ok = qa_native_region_invoke(instance(o), event, entry->address, &entry->signature,
        arguments, entry->signature.parameter_count, &result, e);
    if (ok) ok = application_native_q2_inventory_source_current(o->engine, &s, e) && read_word(o, s.client + o->cursor, &selected, e);
    if (ok) *accepted = selected >= 0 && (uint32_t)selected == r->source_index;
    --o->evaluating;
    qa_error cleanup = {0}; bool restored = restore(o, &inventory, &cleanup);
    if (*accepted) discard(&writes);
    else if (!restore(o, &writes, &cleanup)) restored = false;
    pending(o, &inventory); pending(o, &writes);
    qa_unified_frame_lease_release(storage);
    if (!restored && ok) { ok = false; if (e) *e = cleanup; }
    return ok;
}
static bool navigate(application_native_q2_inventory_scanner *o, inventory_frame *frame,
    const application_native_q2_inventory_readout *r, int direction,
    const qa_native_region_event *event, qa_error *e)
{
    size_t chosen;
    if (!cursor(o, frame->actor, r, &chosen, e)) return false;
    if(!selection(o,frame->actor)&&!qa_id_index_reserve(&o->selection_index.retained,1,e)) return false;
    inventory_selection *retained=selection_take(o,e);
    if(!retained) return false;
    for (size_t step = 1; step <= r->count; ++step) {
        size_t index;
        if (direction > 0) index = chosen == SIZE_MAX ? step - 1 : (chosen + step) % r->count;
        else index = chosen == SIZE_MAX ? (r->count - step) % r->count : (chosen + r->count - step) % r->count;
        const application_native_q2_inventory_row *row = r->rows + index;
        if (row->count == 0) continue;
        bool duplicate = false;
        for (size_t prior = 1; prior < step; ++prior) {
            size_t p = direction > 0 ? chosen == SIZE_MAX ? prior - 1 : (chosen + prior) % r->count :
                chosen == SIZE_MAX ? (r->count - prior) % r->count : (chosen + r->count - prior) % r->count;
            if (r->rows[p].count != 0 && r->rows[p].source_index == row->source_index && source_count(r->rows + p) == source_count(row)) { duplicate = true; break; }
        }
        if (duplicate) continue;
        bool accepted;
        if (!evaluate(o, frame->actor, row, direction, frame->flags, event, &accepted, e)) { selection_drop(o,retained); return false; }
        if (accepted) {
            *retained=(inventory_selection){.actor=frame->actor,.item=row->item,.index=row->source_index,
                .storage_slot=retained->storage_slot};
            if(!selection_install(o,retained,e)) {selection_drop(o,retained);return false;}
            return true;
        }
    }
    selection_drop(o,retained);
    application_native_q2_inventory_source s;
    if (!source(o, frame->actor, &s, e) || !write_word(o, s.client + o->cursor, o->empty, e)) return false;
    application_native_q2_inventory_scanner_release(o, frame->actor); return true;
}
static void frame_row(inventory_frame *frame, const application_native_q2_inventory_row *row)
{
    frame->drop_weapon = row->presentation.kind == APPLICATION_NATIVE_INVENTORY_PRESENTATION_WEAPON;
    frame->named_row = *row;
    frame->named_row.label = NULL;
    frame->named_row.presentation = (application_native_q2_inventory_presentation){0};
    frame->has_named_row = true;
}
static bool project_drop(application_native_q2_inventory_scanner *o, inventory_frame *frame,
    const application_native_q2_inventory_row *row, bool set_cursor,
    qa_native_address *descriptor, qa_error *e)
{
    application_native_q2_inventory_source s;
    uint32_t index = row->drop_index;
    if (index >= o->item_count || index >= o->count)
        return application_fail(e, QA_ERROR_FORMAT, "Native drop prototype leaves its item table");
    qa_native_address current_weapon, pending_weapon;
    qa_unified_frame_lease *storage = frame_storage(o, frame, e);
    if (!storage || !source(o, frame->actor, &s, e) ||
        !qa_native_rva(instance(o), (uint64_t)o->item_table + (uint64_t)index * o->item_stride,
            o->item_stride, descriptor, e) ||
        !application_native_q2_attack_drop_projection(o->engine, frame->actor, row->item, *descriptor,
            row->presentation.kind == APPLICATION_NATIVE_INVENTORY_PRESENTATION_WEAPON,
            storage, &current_weapon, &pending_weapon, e) ||
        !save_bytes(o, storage, &s, o->inventory + index * 4, 4, &frame->restore, e) ||
        !save_bytes(o, storage, &s, o->drop_weapon, o->pointer_bytes, &frame->restore, e) ||
        !save_bytes(o, storage, &s, o->drop_pending, o->pointer_bytes, &frame->restore, e) ||
        (set_cursor && !save_bytes(o, storage, &s, o->cursor, 4, &frame->restore, e))) return false;
    uint8_t weapon[8], next[8];
    if (o->pointer_bytes == 4) {
        qa_store_u32le(weapon, (uint32_t)current_weapon); qa_store_u32le(next, (uint32_t)pending_weapon);
    } else {
        qa_store_u64le(weapon, current_weapon); qa_store_u64le(next, pending_weapon);
    }
    if (!count_write(o, s.client + o->inventory + (uint64_t)index * 4, row, e) ||
        !qa_native_write(instance(o), s.client + o->drop_weapon, (qa_bytes){weapon, o->pointer_bytes}, e) ||
        !qa_native_write(instance(o), s.client + o->drop_pending, (qa_bytes){next, o->pointer_bytes}, e) ||
        (set_cursor && !write_word(o, s.client + o->cursor, (int32_t)index, e))) return false;
    frame->drop_source = s;
    frame->drop_counter = s.client + o->inventory + (uint64_t)index * 4;
    frame_row(frame, row);
    return true;
}
static bool drop_pickup_address(application_native_q2_inventory_scanner *o, inventory_frame *frame,
    qa_native_address *out, bool *present, qa_error *e)
{
    qa_native_host *host = o->engine->provider->state.native.host;
    uint32_t slot; qa_native_slot_binding binding; bool active;
    *present = false;
    if (!frame->drop_pickup.registry ||
        !qa_actors_get(qa_session_actors(o->engine->provider->application->session), frame->drop_pickup) ||
        !qa_native_host_slot_for_actor(host, frame->drop_pickup, &slot)) return true;
    if (!qa_native_slot(instance(o), slot, &binding, e)) return false;
    if (!qa_actor_id_equal(binding.actor, frame->drop_pickup)) return true;
    if (!qa_native_host_source_active(host, slot, &active, e)) return false;
    if (!active) return true;
    if (!qa_native_entity_address(instance(o), slot, out, e)) return false;
    *present = true; return true;
}
static bool drop_pickup_free(application_native_q2_inventory_scanner *o, inventory_frame *frame,
    const qa_native_region_event *event, qa_error *e)
{
    qa_native_address pickup, entry; bool present;
    if (!drop_pickup_address(o, frame, &pickup, &present, e)) return false;
    if (present) {
        if (!address(o, o->drop_free, &entry, e)) return false;
        qa_native_type parameter = {.kind = QA_NATIVE_ADDRESS, .count = 1};
        qa_native_signature signature = {.abi = o->abi, .parameters = &parameter,
            .parameter_count = 1, .result = {.kind = QA_NATIVE_VOID, .count = 1}};
        qa_native_value argument = {.type = QA_NATIVE_ADDRESS, .as.address = pickup};
        qa_native_value result = {.type = QA_NATIVE_VOID};
        bool okay = event ? qa_native_region_invoke(instance(o), event, entry, &signature,
            &argument, 1, &result, e) : qa_native_invoke(instance(o), entry, &signature,
            &argument, 1, &result, e);
        if (!okay) return false;
    }
    frame->drop_pickup = (qa_actor_id){0}; return true;
}
static bool drop_consumer_end(inventory_frame *frame, qa_error *e)
{
    struct application_native_q2_resource_scope *scope = frame->consumer_scope;
    frame->consumer_scope = NULL;
    return !scope || application_native_q2_combat_resources_end(scope, e);
}
static bool drop_committed(void *context)
{ return ((const inventory_frame *)context)->drop_debit == INVENTORY_DROP_ACCEPTED; }
static bool drop_debit_region(application_native_q2_inventory_scanner *o, inventory_region *region,
    inventory_frame *frame, const qa_native_region_event *event, qa_error *e)
{
    if (!frame->dropping) return true;
    if (event->phase == QA_NATIVE_REGION_ENTER) {
        if (!frame->drop_counter || !frame->has_named_row) return true;
        if (!application_native_q2_inventory_source_current(o->engine, &frame->drop_source, e) ||
            !read_word(o, frame->drop_counter, &frame->debit_before, e)) {
            qa_error cleanup = {0};
            frame->drop_debit = INVENTORY_DROP_REFUSED;
            frame->drop_counter = 0;
            (void)restore(o, &frame->restore, &cleanup);
            (void)drop_pickup_free(o, frame, event, &cleanup);
            return false;
        }
        frame->debit_region = region; return true;
    }
    if (frame->debit_region != region) return true;
    frame->debit_region = NULL;
    int32_t after = 0;
    bool okay = application_native_q2_inventory_source_current(o->engine, &frame->drop_source, e) &&
        read_word(o, frame->drop_counter, &after, e);
    int64_t count = okay ? (int64_t)frame->debit_before - after : 0;
    qa_error cleanup = {0};
    if (!restore(o, &frame->restore, &cleanup)) {
        if (okay && e) *e = cleanup;
        okay = false;
    }
    frame->drop_counter = 0;
    qa_native_address pickup; bool present = false;
    if (okay) okay = drop_pickup_address(o, frame, &pickup, &present, e);
    if (okay && (count <= 0 || !present)) okay = application_fail(e, QA_ERROR_ARGUMENT,
        "Accepted original drop lacks its source allocation or positive debit");
    bool accepted = false;
    qa_pickup_cargo cargo = {.item = frame->named_row.item, .count = (double)count, .weapon = frame->drop_weapon};
    if (okay) {
        const qa_native_region_event *previous = o->command_event;
        qa_actor_id previous_actor = o->command_actor;
        o->command_event = event; o->command_actor = frame->actor;
        okay = o->options.dropped(o->options.context, frame->actor, frame->drop_pickup, &cargo, &accepted, e);
        o->command_event = previous; o->command_actor = previous_actor;
    }
    frame->drop_debit = okay && accepted ? INVENTORY_DROP_ACCEPTED : INVENTORY_DROP_REFUSED;
    if (frame->drop_debit == INVENTORY_DROP_REFUSED) {
        cleanup = (qa_error){0};
        if (!drop_pickup_free(o, frame, event, &cleanup) && okay) { okay = false; if (e) *e = cleanup; }
    }
    return okay;
}
static bool drop_consumer_region(application_native_q2_inventory_scanner *o, inventory_frame *frame,
    const qa_native_region_event *event, qa_native_region_decision *decision, qa_error *e)
{
    if (event->phase == QA_NATIVE_REGION_JOIN) return drop_consumer_end(frame, e);
    if (!frame->dropping || frame->drop_debit == INVENTORY_DROP_PENDING) return true;
    if (frame->drop_debit == INVENTORY_DROP_REFUSED || qa_combat_protection_owner(
        o->engine->provider->application->combat, frame->actor, QA_PROTECTION_POWERED, NULL, NULL)) {
        decision->action = QA_NATIVE_REGION_SKIP_TO_JOIN; return true;
    }
    return application_native_q2_combat_resources_begin(o->engine, frame->actor, NULL,
        QA_PROTECTION_POWERED, drop_committed, frame, &frame->consumer_scope, e);
}
static bool death_drop_region(application_native_q2_inventory_scanner *o, inventory_frame *frame,
    const qa_native_region_event *event, qa_error *e)
{
    if (!frame->death_drop) return true;
    if (event->phase == QA_NATIVE_REGION_JOIN) return restore(o, &frame->restore, e);
    qa_unified_frame_lease *storage = frame_storage(o, frame, e);
    if (!storage) return false;
    qa_supply_weapon original = {0}; int32_t amount = 0; bool handled = false;
    if (!application_native_q2_attack_death_drop(o->engine, frame->actor, storage,
        &original, &amount, &handled, e)) return false;
    if (!handled || !qa_actors_get(qa_session_actors(o->engine->provider->application->session), frame->actor)) return true;
    application_native_q2_inventory_source s;
    qa_native_address descriptor = 0; uint32_t weapon_index = 0, ammo_index = 0;
    if (!source(o, frame->actor, &s, e)) return false;
    if (original.item) {
        if (!application_native_q2_inventory_index(o->engine, original.item, &weapon_index, e)) return false;
        if (weapon_index >= o->item_count)
            return application_fail(e, QA_ERROR_FORMAT, "Native death-drop weapon leaves its original descriptor table");
        if (!qa_native_rva(instance(o), (uint64_t)o->item_table + (uint64_t)weapon_index * o->item_stride,
            o->item_stride, &descriptor, e)) return false;
    }
    if (original.ammo) {
        if (!application_native_q2_inventory_index(o->engine, original.ammo, &ammo_index, e)) return false;
        if (ammo_index >= o->count)
            return application_fail(e, QA_ERROR_FORMAT, "Native death-drop ammunition leaves its original counter table");
    }
    if (!save_bytes(o, storage, &s, o->drop_weapon, o->pointer_bytes, &frame->restore, e) ||
        (o->death_ammo_index != UINT32_MAX && !save_bytes(o, storage, &s,
            o->death_ammo_index, 4, &frame->restore, e)) ||
        (original.ammo && !save_bytes(o, storage, &s, o->inventory + ammo_index * 4,
            4, &frame->restore, e))) return false;
    uint8_t pointer[8];
    if (o->pointer_bytes == 4) qa_store_u32le(pointer, (uint32_t)descriptor);
    else qa_store_u64le(pointer, descriptor);
    return qa_native_write(instance(o), s.client + o->drop_weapon, (qa_bytes){pointer, o->pointer_bytes}, e) &&
        (o->death_ammo_index == UINT32_MAX || write_word(o, s.client + o->death_ammo_index, (int32_t)ammo_index, e)) &&
        (!original.ammo || write_word(o, s.client + o->inventory + (uint64_t)ammo_index * 4, amount, e));
}
static bool drop_lookup(application_native_q2_inventory_scanner *, inventory_entry *,
    const qa_native_value *, size_t, qa_native_value *, qa_error *);
static bool drop_allocate(application_native_q2_inventory_scanner *o, inventory_entry *entry,
    const qa_native_value *arguments, size_t count, qa_native_value *result, qa_error *e)
{
    inventory_frame *frame = o->frame;
    if (!qa_native_invoke_original(entry->binding, arguments, count, result, e)) return false;
    if (!frame || !frame->dropping || !frame->drop_counter || !frame->has_named_row ||
        arguments[0].as.address != frame->drop_source.entity ||
        result->type != QA_NATIVE_ADDRESS || !result->as.address) return true;
    if (!application_native_q2_inventory_source_current(o->engine, &frame->drop_source, e)) return false;
    return qa_native_host_source_actor(o->engine->provider->state.native.host,
        result->as.address, true, &frame->drop_pickup, e);
}
static bool entry_call(void *opaque, qa_native_instance *actual, qa_native_entry_observer *binding,
    const qa_native_value *arguments, size_t count, qa_native_value *result, qa_error *e)
{
    inventory_entry *entry = opaque; application_native_q2_inventory_scanner *o = entry->owner;
    if (actual != instance(o) || binding != entry->binding || !current(o, e) || !count ||
        arguments[0].type != QA_NATIVE_ADDRESS || o->pending)
        return application_fail(e, QA_ERROR_ARGUMENT, "Native scanner entry lost its exact source invocation");
    if (entry->kind == 7) return drop_lookup(o, entry, arguments, count, result, e);
    if (entry->kind == INVENTORY_ENTRY_DROP_ALLOCATE) return drop_allocate(o, entry, arguments, count, result, e);
    inventory_frame frame = {.previous = o->frame, .flags = -1,
        .named = entry->kind == 4 || entry->kind == 5,
        .dropping = entry->kind == 5 || entry->kind == 6, .giving = entry->kind == INVENTORY_ENTRY_GIVE,
        .death_drop = entry->kind == INVENTORY_ENTRY_DEATH_DROP};
    if (count > 1 && arguments[1].type == QA_NATIVE_I32) frame.flags = arguments[1].as.i32;
    if (arguments[0].as.address && !qa_native_host_source_actor(o->engine->provider->state.native.host,
        arguments[0].as.address, true, &frame.actor, e)) return false;
    o->frame = &frame; ++o->calls;
    bool ok = true;
    if (entry->kind == 2 && frame.actor.registry && !o->evaluating) {
        application_native_q2_inventory_readout r = {0}; size_t chosen = SIZE_MAX;
        ok = rows(o, frame.actor, true, &r, e);
        if (ok && r.present) ok = cursor(o, frame.actor, &r, &chosen, e);
        if (ok && chosen != SIZE_MAX && r.rows[chosen].selected) {
            application_native_q2_inventory_source s;
            ok = source(o, frame.actor, &s, e) && save_bytes(o, frame_storage(o, &frame, e), &s,
                o->inventory + r.rows[chosen].source_index * 4, 4, &frame.restore, e) &&
                count_write(o, s.client + o->inventory + (uint64_t)r.rows[chosen].source_index * 4,
                    r.rows + chosen, e);
        }
        application_native_q2_inventory_readout_free(&r);
    }
    if (ok) ok = qa_native_invoke_original(binding, arguments, count, result, e);
    qa_error cleanup = {0}; bool cleaned = drop_consumer_end(&frame, &cleanup);
    if (!restore(o, &frame.restore, &cleanup)) cleaned = false;
    if (!ok && frame.drop_debit != INVENTORY_DROP_ACCEPTED &&
        !drop_pickup_free(o, &frame, NULL, &cleanup)) cleaned = false;
    pending(o, &frame.restore); qa_unified_frame_lease_release(frame.storage);
    qa_arena_destroy(&frame.requested_storage);
    if (!cleaned && ok) { ok = false; if (e) *e = cleanup; }
    --o->calls; o->frame = frame.previous; return ok;
}
static bool normalized(qa_bytes text, qa_arena *storage, qa_buffer *out, qa_error *e)
{
    qa_buffer lowered = {0};
    if (!qa_utf8_lower_arena(text, storage, &lowered, e)) return false;
    size_t written = 0;
    for (size_t i = 0; i < lowered.size; ++i) if (lowered.data[i] != ' ') lowered.data[written++] = lowered.data[i];
    lowered.data[written] = 0; lowered.size = written; *out = lowered; return true;
}
static bool same_name(application_native_q2_inventory_scanner *o, const char *text,
    const qa_buffer *request, bool *same, qa_error *e)
{
    qa_buffer value = {0};
    qa_arena_reset(&o->name_storage);
    if (!normalized((qa_bytes){(const uint8_t *)text, strlen(text)}, &o->name_storage, &value, e)) return false;
    *same = value.size == request->size && (!value.size || !memcmp(value.data, request->data, value.size));
    return true;
}
static bool named_choice(application_native_q2_inventory_scanner *o, inventory_frame *frame,
    const application_native_q2_inventory_readout *r, qa_native_address original,
    size_t *chosen, qa_error *e)
{
    *chosen = SIZE_MAX;
    qa_strings *strings = qa_session_strings(o->engine->provider->application->session);
    for (size_t i = 0; i < r->count; ++i) {
        const char *item = qa_strings_cstr(strings, r->rows[i].item); bool match;
        if (!item || !same_name(o, item, &frame->requested, &match, e)) return false;
        if (match) { *chosen = i; return true; }
    }
    if (original) {
        application_native_q2_inventory_source s; qa_item_id item;
        if (!source(o, frame->actor, &s, e) || !application_native_q2_attack_descriptor_item(o->engine,original,&item,e)) return false;
        for (size_t i = 0; i < r->count; ++i) if (r->rows[i].item == item) { *chosen = i; return true; }
    }
    size_t matches = 0;
    for (size_t i = 0; i < r->count; ++i) {
        bool match;
        if (!same_name(o, r->rows[i].label, &frame->requested, &match, e)) return false;
        if (match) { *chosen = i; ++matches; }
    }
    if (matches <= 1) return true;
    *chosen = SIZE_MAX;
    if (original) return true;
    size_t needed = frame->requested.size + 40;
    for (size_t i = 0; i < r->count; ++i) {
        bool match;
        if (!same_name(o, r->rows[i].label, &frame->requested, &match, e)) return false;
        if (match) {
            const char *item = qa_strings_cstr(strings, r->rows[i].item);
            if (!item || strlen(item) > SIZE_MAX - needed - 2)
                return application_fail(e, QA_ERROR_MEMORY, "Ambiguous inventory name exceeds its report extent");
            needed += strlen(item) + 2;
        }
    }
    qa_unified_frame_lease *storage = frame_storage(o, frame, e);
    char *text = storage ? qa_unified_frame_lease_alloc(storage, needed, 1, 1, e) : NULL;
    if (!text) return false;
    size_t at = (size_t)snprintf(text, needed, "Ambiguous item \"%s\"; use ", (char *)frame->requested.data); size_t emitted = 0;
    bool ok = true;
    for (size_t i = 0; ok && i < r->count; ++i) {
        bool match; ok = same_name(o, r->rows[i].label, &frame->requested, &match, e);
        if (ok && match) {
            const char *item = qa_strings_cstr(strings, r->rows[i].item); size_t bytes = strlen(item);
            if (emitted++) { memcpy(text + at, ", ", 2); at += 2; }
            memcpy(text + at, item, bytes); at += bytes;
        }
    }
    text[at++] = '\n'; text[at] = 0;
    if (ok) ok = o->options.print(o->options.context, frame->actor, text, e);
    return ok;
}
static bool lookup_text(application_native_q2_inventory_scanner *o, qa_native_address name,
    inventory_frame *frame, qa_error *e)
{
    if (!name) return application_fail(e, QA_ERROR_ARGUMENT, "Original native item lookup has no actual name argument");
    qa_arena_reset(&frame->requested_storage); frame->requested = (qa_buffer){0};
    qa_bytes text;
    if (!qa_native_string_span(instance(o), name, 1024 * 1024, &text, e)) return false;
    if (!text.data) {
        uint8_t *copy = qa_arena_alloc(&frame->requested_storage, text.size + 1, 1, e);
        if (!copy || !qa_native_read(instance(o), name, copy, text.size + 1, e)) return false;
        text.data = copy;
    }
    return normalized(text, &frame->requested_storage, &frame->requested, e);
}
static bool lookup_name(application_native_q2_inventory_scanner *o, const qa_native_region_event *event,
    inventory_frame *frame, qa_error *e)
{
    qa_native_address name;
    if (o->pointer_bytes == 4) {
        uint8_t bytes[4];
        if (!qa_native_read(instance(o), (uint32_t)event->state.registers[QA_NATIVE_RSP], bytes, 4, e)) return false;
        name = qa_load_u32le(bytes);
    } else name = event->state.registers[QA_NATIVE_RCX];
    return lookup_text(o, name, frame, e);
}
static bool drop_lookup(application_native_q2_inventory_scanner *o, inventory_entry *entry,
    const qa_native_value *arguments, size_t count, qa_native_value *result, qa_error *e)
{
    inventory_frame *frame = o->frame;
    qa_native_address caller = 0;
    bool selected = frame && frame->dropping && frame->named && frame->actor.registry && !o->evaluating;
    if (selected && !qa_native_entry_caller(entry->binding, &caller, e)) return false;
    if (!qa_native_invoke_original(entry->binding, arguments, count, result, e)) return false;
    if (!selected || caller != o->drop_lookup_return) return true;
    application_native_q2_inventory_readout r = {0}; size_t chosen = SIZE_MAX;
    bool ok = lookup_text(o, arguments[0].as.address, frame, e) && rows(o, frame->actor, true, &r, e);
    if (ok && r.present) ok = named_choice(o, frame, &r, result->as.address, &chosen, e);
    if (ok && chosen != SIZE_MAX && r.rows[chosen].selected)
        ok = project_drop(o, frame, r.rows + chosen, false, &result->as.address, e);
    application_native_q2_inventory_readout_free(&r);
    return ok;
}
static bool give_region(application_native_q2_inventory_scanner *o, inventory_region *region,
    inventory_frame *frame, const qa_native_region_event *event,
    qa_native_region_decision *decision, qa_error *e)
{
    if (!frame->giving) return true;
    if (region->kind == INVENTORY_GIVE_AMMO_GRANT && event->phase == QA_NATIVE_REGION_ENTER) {
        qa_native_address table, descriptor = event->state.registers[region->descriptor];
        if (o->pointer_bytes == 4) descriptor = (uint32_t)descriptor;
        if (!address(o, o->item_table, &table, e)) return false;
        if (descriptor < table || (descriptor - table) % o->item_stride ||
            (descriptor - table) / o->item_stride >= o->item_count ||
            (descriptor - table) / o->item_stride >= o->count)
            return application_fail(e, QA_ERROR_FORMAT, "Native ammunition grant has no actual source descriptor");
        frame->ammo_index = (uint32_t)((descriptor - table) / o->item_stride);
        if (!source(o, frame->actor, &frame->ammo_source, e) ||
            !read_word(o, frame->ammo_source.client + o->inventory + (uint64_t)frame->ammo_index * 4,
                &frame->ammo_before, e)) return false;
        frame->ammo_region = region;
        return true;
    }
    bool category = region->kind == INVENTORY_GIVE_WEAPONS || region->kind == INVENTORY_GIVE_AMMO;
    if ((category && event->phase != QA_NATIVE_REGION_JOIN) ||
        (region->kind == INVENTORY_GIVE_UNKNOWN && event->phase != QA_NATIVE_REGION_ENTER) ||
        (region->kind == INVENTORY_GIVE_AMMO_GRANT &&
            (event->phase != QA_NATIVE_REGION_JOIN || frame->ammo_region != region))) return true;
    if (!qa_actors_get(qa_session_actors(o->engine->provider->application->session), frame->actor)) return true;
    const qa_native_region_event *previous = o->command_event;
    qa_actor_id previous_actor = o->command_actor;
    o->command_event = event; o->command_actor = frame->actor;
    bool ok;
    if (category) ok = o->options.give(o->options.context, frame->actor,
        region->kind == INVENTORY_GIVE_WEAPONS, e);
    else if (region->kind == INVENTORY_GIVE_UNKNOWN) {
        const qa_command_tokens *arguments = &o->engine->arguments;
        bool handled = false;
        ok = o->options.give_item(o->options.context, frame->actor,
            arguments->count > 1 ? (const char *const *)(arguments->values + 1) : NULL,
            arguments->count ? arguments->count - 1 : 0, &handled, e);
        if (ok && handled) decision->action = QA_NATIVE_REGION_SKIP_TO_JOIN;
    } else {
        qa_item_id item; int32_t after;
        frame->ammo_region = NULL;
        ok = application_native_q2_inventory_source_current(o->engine, &frame->ammo_source, e) &&
            read_word(o, frame->ammo_source.client + o->inventory + (uint64_t)frame->ammo_index * 4, &after, e) &&
            application_native_q2_inventory_item(o->engine, frame->ammo_index, &item, e) &&
            o->options.give_ammo(o->options.context, frame->actor, item, region->set,
                region->set ? after : qa_i32_sub(after, frame->ammo_before), e);
    }
    o->command_event = previous; o->command_actor = previous_actor;
    return ok;
}
static bool region_call(void *opaque, qa_native_instance *actual, const qa_native_region_event *event,
    qa_native_region_decision *decision, qa_error *e)
{
    inventory_region *region = opaque; application_native_q2_inventory_scanner *o = region->owner;
    if (actual != instance(o) || event->region.id != region->id || !current(o, e)) return false;
    inventory_frame *frame = o->frame;
    if (!frame || !frame->actor.registry || o->evaluating) return true;
    if (region->kind == INVENTORY_DEATH_DROP) return death_drop_region(o, frame, event, e);
    if (region->kind == INVENTORY_DROP_DEBIT) return drop_debit_region(o, region, frame, event, e);
    if (region->kind == INVENTORY_DROP_CONSUMER) return drop_consumer_region(o, frame, event, decision, e);
    if (region->kind >= INVENTORY_GIVE_WEAPONS) return give_region(o, region, frame, event, decision, e);
    if (region->kind == 4) {
        if (event->phase != QA_NATIVE_REGION_JOIN || !frame->dropping || frame->named) return true;
        application_native_q2_inventory_readout r = {0}; size_t chosen = SIZE_MAX;
        bool ok = rows(o, frame->actor, true, &r, e);
        if (ok && r.present) ok = cursor(o, frame->actor, &r, &chosen, e);
        qa_native_address descriptor;
        if (ok && chosen != SIZE_MAX && r.rows[chosen].selected)
            ok = project_drop(o, frame, r.rows + chosen, true, &descriptor, e);
        application_native_q2_inventory_readout_free(&r);
        return ok;
    }
    if (region->kind == 3 && frame->named) {
        if (event->phase == QA_NATIVE_REGION_ENTER) return lookup_name(o, event, frame, e);
        application_native_q2_inventory_readout r = {0}; size_t chosen = SIZE_MAX;
        qa_native_address original = o->pointer_bytes == 4 ? (uint32_t)event->state.registers[QA_NATIVE_RAX] : event->state.registers[QA_NATIVE_RAX];
        bool ok = rows(o, frame->actor, true, &r, e);
        if (ok && r.present) ok = named_choice(o, frame, &r, original, &chosen, e);
        if (ok && chosen != SIZE_MAX && r.rows[chosen].selected) {
            application_native_q2_inventory_source s; qa_native_address descriptor;
            uint32_t index = r.rows[chosen].source_index;
            if (index >= o->item_count) ok = application_fail(e, QA_ERROR_FORMAT, "Mixed inventory prototype leaves the genuine descriptor table");
            if (ok) ok = source(o, frame->actor, &s, e) && qa_native_rva(instance(o),
                (uint64_t)o->item_table + (uint64_t)index * o->item_stride, o->item_stride, &descriptor, e) &&
                save_bytes(o, frame_storage(o, frame, e), &s, o->inventory + index * 4, 4, &frame->restore, e) &&
                count_write(o, s.client + o->inventory + (uint64_t)index * 4, r.rows + chosen, e);
            if (ok) {
                frame_row(frame, r.rows + chosen);
                decision->replace_state = true; decision->state = event->state; decision->state.registers[QA_NATIVE_RAX] = descriptor;
            }
        }
        application_native_q2_inventory_readout_free(&r); return ok;
    }
    if (event->phase != QA_NATIVE_REGION_ENTER) return true;
    application_native_q2_inventory_readout r = {0}; bool ok = rows(o, frame->actor, true, &r, e);
    if (!ok || !r.present) { application_native_q2_inventory_readout_free(&r); return ok; }
    if (region->kind == 1) {
        ok = navigate(o, frame, &r, region->direction, event, e);
        if (ok) decision->action = QA_NATIVE_REGION_SKIP_TO_JOIN;
    } else if (region->kind == 2 || region->kind == 5) {
        if ((region->kind == 5) != frame->dropping) {
            application_native_q2_inventory_readout_free(&r); return true;
        }
        size_t chosen = SIZE_MAX;
        if (!frame->named && !frame->dropping) ok = cursor(o, frame->actor, &r, &chosen, e);
        const application_native_q2_inventory_row *row = frame->named || frame->dropping ? frame->has_named_row ? &frame->named_row : NULL :
            chosen != SIZE_MAX ? r.rows + chosen : NULL;
        if (ok && row && row->selected) {
            bool dispatch = true;
            if (frame->dropping) {
                qa_item_definition definition; bool found;
                ok = qa_inventory_item_definition_find(o->engine->provider->application->inventory,
                    frame->actor, NULL, row->item, &definition, &found, e);
                dispatch = ok && found && (definition.actions & QA_ITEM_DROP);
            }
            if (!ok || !dispatch) { application_native_q2_inventory_readout_free(&r); return ok; }
            ok = restore(o, &frame->restore, e);
            if (ok) frame->drop_counter = 0;
            if (ok) {
                const qa_native_region_event *previous = o->command_event;
                qa_actor_id previous_actor = o->command_actor;
                o->command_event = event; o->command_actor = frame->actor;
                ok = o->options.action(o->options.context, frame->actor, row->item,
                    frame->dropping ? QA_ITEM_DROP : QA_ITEM_USE, e);
                o->command_event = previous; o->command_actor = previous_actor;
            }
            if (ok) decision->action = QA_NATIVE_REGION_SKIP_TO_JOIN;
        }
    }
    application_native_q2_inventory_readout_free(&r); return ok;
}
const qa_native_region_event *application_native_q2_inventory_scanner_command_event(
    const application_native_q2_inventory_scanner *o, qa_actor_id actor)
{
    return o && o->active && o->calls && o->frame && !o->evaluating &&
        qa_actor_id_equal(o->command_actor, actor) &&
        qa_actor_id_equal(o->frame->actor, actor) ? o->command_event : NULL;
}
static bool region_profile(application_native_q2_inventory_scanner *o, size_t at,
    const char *path, unsigned kind, int direction, qa_error *e)
{
    qa_native_declared_region declared;
    if (!qa_native_declaration_find_region(o->declaration, path, &declared, e)) return false;
    o->regions[at] = (inventory_region){.owner = o, .id = declared.id, .kind = kind, .direction = direction};
    return true;
}
bool application_native_q2_inventory_scanner_create(struct application_native_q2 *engine,
    const application_native_q2_inventory_scanner_options *options,
    application_native_q2_inventory_scanner **out, qa_error *e)
{
    if (!out || !engine || !engine->declaration || !engine->primary_inventory || !options ||
        !options->storage_bytes || !options->rows || !options->action || !options->print || !engine->provider->state.native.host)
        return application_fail(e, QA_ERROR_ARGUMENT, "Native inventory scanner requires its actual primary profile and row services");
    *out = NULL;
    application_native_q2_inventory_scanner *o = calloc(1, sizeof(*o));
    if (!o) return application_fail(e, QA_ERROR_MEMORY, "Constructing original native inventory scanner");
    o->engine = engine; o->declaration = engine->declaration; o->options = *options; *out = o;
    const qa_actor_registry *actors = qa_session_actors(engine->provider->application->session);
    size_t capacity = qa_actors_capacity(actors);
    if (!qa_pool_prepare(&o->selection_pool, &o->selection_storage, capacity,
            sizeof(inventory_selection), _Alignof(inventory_selection), e) ||
        !qa_actor_index_create(&o->selection_index, actors, &o->selection_index_storage, e) ||
        !qa_id_index_reserve(&o->selection_index.retained, capacity, e)) return false;
    qa_native_module_info info = qa_native_module_describe(engine->provider->state.native.module);
    o->abi = info.image.target.abi; o->pointer_bytes = info.image.target.pointer_bytes;
    if (!qa_json_parse(QA_JSON_STRICT, qa_native_declaration_primary(o->declaration), &o->document, e)) return false;
    const qa_json_document *d = o->document; qa_json_id root = qa_json_root(d), inventory = qa_json_get(d, root, "inventory");
    qa_json_id commands = qa_json_get(d, root, "commands"), items = qa_json_get(d, commands, "items"); int64_t empty;
    qa_json_id give = qa_json_get(d, commands, "give"), ammo_grants = qa_json_get(d, give, "ammoGrants");
    qa_json_id death_drop = qa_json_get(d, commands, "drop");
    bool death_drops = death_drop != QA_JSON_NONE && qa_json_type(d, death_drop) != QA_JSON_NULL;
    if (death_drops && qa_json_type(d, death_drop) != QA_JSON_OBJECT)
        return application_fail(e, QA_ERROR_FORMAT, "Native death drop requires its original command declaration");
    bool gives = qa_json_type(d, give) != QA_JSON_NULL;
    size_t ammo_grant_count = gives ? qa_json_size(d, ammo_grants) : 0;
    if (gives && (qa_json_type(d, give) != QA_JSON_OBJECT || qa_json_type(d, ammo_grants) != QA_JSON_ARRAY ||
        !options->give || !options->give_item || !options->give_ammo))
        return application_fail(e, QA_ERROR_FORMAT, "Native give commands require their declared grant regions and shared inventory services");
    qa_json_id drop = qa_json_get(d, root, "drop"), callbacks = qa_json_get(d, drop, "callbacks");
    qa_json_id debits = qa_json_get(d, drop, "debits"), consumer = qa_json_get(d, drop, "consumer");
    bool drops = qa_json_get(d, drop, "named") != QA_JSON_NONE;
    size_t callback_count = drops ? qa_json_size(d, callbacks) : 0;
    size_t debit_count = drops ? qa_json_size(d, debits) : 0;
    bool consumes = drops && consumer != QA_JSON_NONE && qa_json_type(d, consumer) != QA_JSON_NULL;
    if (drops && (qa_json_type(d, callbacks) != QA_JSON_ARRAY || qa_json_type(d, debits) != QA_JSON_ARRAY ||
        !options->dropped || (consumes && qa_json_type(d, consumer) != QA_JSON_OBJECT)))
        return application_fail(e, QA_ERROR_FORMAT, "Native drops require their declared callback, debit and shared cargo services");
    size_t region_capacity = 7u + (gives ? 3u : 0u) + (death_drops ? 1u : 0u) + (consumes ? 1u : 0u);
    if (callback_count > SIZE_MAX - region_capacity ||
        ammo_grant_count > SIZE_MAX - region_capacity - callback_count ||
        debit_count > SIZE_MAX - region_capacity - callback_count - ammo_grant_count)
        return application_fail(e, QA_ERROR_MEMORY, "Native command region count overflows");
    region_capacity += callback_count + ammo_grant_count + debit_count;
    if (region_capacity > SIZE_MAX / sizeof(*o->regions))
        return application_fail(e, QA_ERROR_MEMORY, "Native command region storage overflows");
    o->regions = qa_arena_alloc(&o->selection_storage,
        region_capacity * sizeof(*o->regions), _Alignof(inventory_region), e);
    if (!o->regions) return false;
    memset(o->regions, 0, region_capacity * sizeof(*o->regions));
    if (!word(d, inventory, "client", &o->client_pointer, e) || !word(d, inventory, "inventory", &o->inventory, e) ||
        !word(d, inventory, "count", &o->count, e) || !word(d, inventory, "cursor", &o->cursor, e) ||
        !qa_json_i64(d, qa_json_get(d, inventory, "empty"), &empty, e) || empty < INT32_MIN || empty > INT32_MAX ||
        !o->count || o->count > 65536 || !word(d, items, "table", &o->item_table, e) ||
        !word(d, items, "stride", &o->item_stride, e) || !word(d, items, "count", &o->item_count, e) || !o->item_stride)
        return application_fail(e, QA_ERROR_FORMAT, "Native scanner profile has invalid actual source extents");
    o->empty = (int32_t)empty;
    o->zero = qa_arena_alloc(&o->selection_storage, (size_t)o->count * 4, 1, e);
    if (!o->zero) return false;
    memset(o->zero, 0, (size_t)o->count * 4);
    qa_json_id writes = qa_json_get(d, inventory, "selectionWrites");
    if (qa_json_type(d, writes) != QA_JSON_ARRAY || qa_json_size(d, writes) > SIZE_MAX / sizeof(*o->writes))
        return application_fail(e, QA_ERROR_FORMAT, "Native scanner profile has no selection write inventory");
    o->write_count = qa_json_size(d, writes);
    o->writes = o->write_count ? qa_arena_alloc(&o->selection_storage,
        o->write_count * sizeof(*o->writes), _Alignof(inventory_write_field), e) : NULL;
    if (o->write_count && !o->writes) return false;
    uint32_t client_bytes = application_native_q2_inventory_client_bytes(engine);
    for (size_t i = 0; i < o->write_count; ++i) {
        if (!word(d, qa_json_at(d, writes, i), "offset", &o->writes[i].offset, e) ||
            !word(d, qa_json_at(d, writes, i), "bytes", &o->writes[i].bytes, e) || !o->writes[i].bytes)
            return application_fail(e, QA_ERROR_FORMAT, "Native scanner selection write has an empty extent");
        if (o->writes[i].offset > client_bytes || o->writes[i].bytes > client_bytes - o->writes[i].offset)
            return application_fail(e, QA_ERROR_FORMAT, "Native scanner selection write leaves its declared client");
    }
    const char *names[] = {"next", "previous", "validate", "use", "namedUse"};
    o->entry_count = drops ? 8 : 5;
    for (unsigned i = 0; i < 5; ++i) {
        inventory_entry *entry = o->entries + i; uint32_t rva;
        entry->owner = o; entry->kind = i;
        if (!word(d, qa_json_get(d, inventory, names[i]), "entry", &rva, e) || !address(o, rva, &entry->address, e)) return false;
        entry->parameters[0] = (qa_native_type){.kind = QA_NATIVE_ADDRESS, .count = 1};
        entry->parameters[1] = (qa_native_type){.kind = QA_NATIVE_I32, .count = 1};
        entry->parameters[2] = (qa_native_type){.kind = QA_NATIVE_U8, .count = 1};
        size_t count = i < 2 ? 2 : 1;
        if (!i) { bool menu;
            if (!qa_json_bool(d, qa_json_get(d, qa_json_get(d, inventory, "next"), "menuArgument"), &menu, e)) return false;
            if (menu) ++count;
        }
        entry->signature = (qa_native_signature){.abi = o->abi, .parameters = entry->parameters,
            .parameter_count = count, .result = {.kind = QA_NATIVE_VOID, .count = 1}};
    }
    if (drops) {
        qa_json_id client = qa_json_get(d, drop, "client");
        uint32_t pointer, inventory_offset, cursor_offset, lookup_return;
        if (!word(d, client, "pointer", &pointer, e) ||
            !word(d, client, "inventory", &inventory_offset, e) ||
            !word(d, client, "cursor", &cursor_offset, e) ||
            !word(d, client, "weapon", &o->drop_weapon, e) ||
            !word(d, client, "pending", &o->drop_pending, e) ||
            !word(d, drop, "free", &o->drop_free, e) ||
            !word(d, drop, "lookupReturn", &lookup_return, e) ||
            !address(o, lookup_return, &o->drop_lookup_return, e)) return false;
        if (pointer != o->client_pointer || inventory_offset != o->inventory || cursor_offset != o->cursor ||
            o->drop_weapon > client_bytes || o->pointer_bytes > client_bytes - o->drop_weapon ||
            o->drop_pending > client_bytes || o->pointer_bytes > client_bytes - o->drop_pending)
            return application_fail(e, QA_ERROR_FORMAT, "Native drop fields leave their declared client layout");
        for (unsigned i = 5; i < 8; ++i) {
            inventory_entry *entry = o->entries + i; uint32_t rva;
            qa_json_id object = i == 6 ? qa_json_get(d, drop, "inventory") : drop;
            const char *key = i == 5 ? "named" : i == 6 ? "entry" : "find";
            entry->owner = o; entry->kind = i;
            if (!word(d, object, key, &rva, e) || !address(o, rva, &entry->address, e)) return false;
            entry->parameters[0] = (qa_native_type){.kind = QA_NATIVE_ADDRESS, .count = 1};
            entry->signature = (qa_native_signature){.abi = o->abi, .parameters = entry->parameters,
                .parameter_count = 1, .result = {.kind = i == 7 ? QA_NATIVE_ADDRESS : QA_NATIVE_VOID, .count = 1}};
        }
    }
    if (gives) {
        inventory_entry *entry = o->entries + o->entry_count++; uint32_t rva;
        entry->owner = o; entry->kind = INVENTORY_ENTRY_GIVE;
        if (!word(d, give, "entry", &rva, e) || !address(o, rva, &entry->address, e)) return false;
        entry->parameters[0] = (qa_native_type){.kind = QA_NATIVE_ADDRESS, .count = 1};
        entry->signature = (qa_native_signature){.abi = o->abi, .parameters = entry->parameters,
            .parameter_count = 1, .result = {.kind = QA_NATIVE_VOID, .count = 1}};
    }
    if (death_drops) {
        inventory_entry *entry = o->entries + o->entry_count++; uint32_t rva, pointer, inventory_offset, weapon;
        qa_json_id client = qa_json_get(d, commands, "client"), ammo_index = qa_json_get(d, client, "ammoIndex");
        if (!word(d, client, "pointer", &pointer, e) || !word(d, client, "inventory", &inventory_offset, e) ||
            !word(d, client, "weapon", &weapon, e) || !word(d, death_drop, "entry", &rva, e) ||
            !address(o, rva, &entry->address, e)) return false;
        o->death_ammo_index = UINT32_MAX;
        if (qa_json_type(d, ammo_index) != QA_JSON_NULL && !word(d, client, "ammoIndex", &o->death_ammo_index, e)) return false;
        if (pointer != o->client_pointer || inventory_offset != o->inventory ||
            (drops && weapon != o->drop_weapon) || weapon > client_bytes || o->pointer_bytes > client_bytes - weapon ||
            (o->death_ammo_index != UINT32_MAX &&
                (o->death_ammo_index > client_bytes || client_bytes - o->death_ammo_index < 4)))
            return application_fail(e, QA_ERROR_FORMAT, "Native death drop fields leave their original client layout");
        o->drop_weapon = weapon;
        entry->owner = o; entry->kind = INVENTORY_ENTRY_DEATH_DROP;
        entry->parameters[0] = (qa_native_type){.kind = QA_NATIVE_ADDRESS, .count = 1};
        entry->signature = (qa_native_signature){.abi = o->abi, .parameters = entry->parameters,
            .parameter_count = 1, .result = {.kind = QA_NATIVE_VOID, .count = 1}};
    }
    if (drops) {
        inventory_entry *entry = o->entries + o->entry_count++; uint32_t rva;
        entry->owner = o; entry->kind = INVENTORY_ENTRY_DROP_ALLOCATE;
        if (!word(d, drop, "allocate", &rva, e) || !address(o, rva, &entry->address, e)) return false;
        entry->parameters[0] = (qa_native_type){.kind = QA_NATIVE_ADDRESS, .count = 1};
        entry->parameters[1] = (qa_native_type){.kind = QA_NATIVE_ADDRESS, .count = 1};
        entry->signature = (qa_native_signature){.abi = o->abi, .parameters = entry->parameters,
            .parameter_count = 2, .result = {.kind = QA_NATIVE_ADDRESS, .count = 1}};
    }
    if (!region_profile(o, 0, "/inventory/next/scan", 1, 1, e) ||
        !region_profile(o, 1, "/inventory/previous/scan", 1, -1, e) ||
        !region_profile(o, 2, "/inventory/use/call", 2, 0, e) ||
        !region_profile(o, 3, "/inventory/namedUse/call", 2, 0, e) ||
        !region_profile(o, 4, "/inventory/namedUse/lookup", 3, 0, e)) return false;
    o->region_count = 5;
    qa_json_id validate = qa_json_get(d, qa_json_get(d, inventory, "validate"), "scan");
    if (qa_json_type(d, validate) != QA_JSON_NULL) {
        if (!region_profile(o, 5, "/inventory/validate/scan", 1, 1, e)) return false;
        o->region_count = 6;
    }
    if (drops) {
        if (!region_profile(o, o->region_count++, "/drop/inventory/admission", 4, 0, e)) return false;
        for (size_t i = 0; i < callback_count; ++i) {
            char path[64]; snprintf(path, sizeof(path), "/drop/callbacks/%zu", i);
            if (!region_profile(o, o->region_count++, path, 5, 0, e)) return false;
        }
        for (size_t i = 0; i < debit_count; ++i) {
            char path[64]; snprintf(path, sizeof(path), "/drop/debits/%zu", i);
            if (!region_profile(o, o->region_count++, path, INVENTORY_DROP_DEBIT, 0, e)) return false;
        }
        if (consumes && !region_profile(o, o->region_count++, "/drop/consumer",
            INVENTORY_DROP_CONSUMER, 0, e)) return false;
    }
    if (gives) {
        if (!region_profile(o, o->region_count++, "/commands/give/weapons", INVENTORY_GIVE_WEAPONS, 0, e) ||
            !region_profile(o, o->region_count++, "/commands/give/ammo", INVENTORY_GIVE_AMMO, 0, e) ||
            !region_profile(o, o->region_count++, "/commands/give/unknown", INVENTORY_GIVE_UNKNOWN, 0, e)) return false;
        for (size_t i = 0; i < ammo_grant_count; ++i) {
            char path[80]; snprintf(path, sizeof(path), "/commands/give/ammoGrants/%zu", i);
            size_t index = o->region_count++;
            if (!region_profile(o, index, path, INVENTORY_GIVE_AMMO_GRANT, 0, e)) return false;
            inventory_region *region = o->regions + index;
            qa_json_id grant = qa_json_at(d, ammo_grants, i), kind = qa_json_get(d, grant, "kind");
            if (qa_json_string_equal(d, kind, "set")) region->set = true;
            else if (!qa_json_string_equal(d, kind, "add"))
                return application_fail(e, QA_ERROR_FORMAT, "Native ammunition grant has no declared set or add semantics");
            qa_buffer name = {0};
            bool parsed = qa_json_string(d, qa_json_get(d, grant, "descriptor"), &name, e);
            if (parsed) parsed = qa_native_register_find((qa_bytes){name.data, name.size}, &region->descriptor);
            qa_buffer_free(&name);
            if (!parsed || region->descriptor == QA_NATIVE_RSP ||
                (o->pointer_bytes == 4 && region->descriptor >= QA_NATIVE_R8))
                return application_fail(e, QA_ERROR_FORMAT, "Native ammunition descriptor has no valid source register");
        }
    }
    if (death_drops && !region_profile(o, o->region_count++, "/commands/drop/eligibility",
        INVENTORY_DEATH_DROP, 0, e)) return false;
    return true;
}
bool application_native_q2_inventory_scanner_activate(application_native_q2_inventory_scanner *o, qa_error *e)
{
    if (!current(o, e) || o->calls || o->pending || !o->engine->map_ready)
        return application_fail(e, QA_ERROR_ARGUMENT, "Native scanner activation requires its returned source map");
    if(!o->readout_pages.values) {
        size_t bytes=o->options.storage_bytes(o->options.context),stride=4096;
        size_t pages=bytes/stride+(bytes%stride!=0);
        if(!qa_pool_prepare(&o->readout_pages,&o->readout_storage,pages,stride,_Alignof(max_align_t),e)) return false;
    }
    for (size_t i = 0; i < o->entry_count; ++i) if (!o->entries[i].binding && !qa_native_observe_entry(instance(o),
        o->entries[i].address, &o->entries[i].signature, entry_call, o->entries + i, &o->entries[i].binding, e)) return false;
    for (size_t i = 0; i < o->region_count; ++i) if (!o->regions[i].binding &&
        !qa_native_bind_region(instance(o), o->regions[i].id, region_call, o->regions + i, &o->regions[i].binding, e)) return false;
    o->active = true; return true;
}
bool application_native_q2_inventory_scanner_idle(const application_native_q2_inventory_scanner *o)
{ return !o || (!o->calls && !o->frame && !o->evaluating && !o->pending); }
bool application_native_q2_inventory_scanner_returned(const application_native_q2_inventory_scanner *o)
{ return !o || (!o->calls && !o->frame && !o->evaluating); }
bool application_native_q2_inventory_scanner_suspend(application_native_q2_inventory_scanner *o, qa_error *e)
{
    if (!o) return true;
    if (o->calls || o->frame || o->evaluating) return application_fail(e, QA_ERROR_ARGUMENT, "Native inventory scanner remains entered");
    if (!restore(o, &o->pending, e)) return false;
    for (size_t i = o->region_count; i; --i) if (o->regions[i - 1].binding) {
        if (!qa_native_remove_region(o->regions[i - 1].binding, e)) return false;
        o->regions[i - 1].binding = NULL;
    }
    for (size_t i = o->entry_count; i; --i) if (o->entries[i - 1].binding) {
        if (!qa_native_unobserve_entry(o->entries[i - 1].binding, e)) return false;
        o->entries[i - 1].binding = NULL;
    }
    o->active = false; return true;
}
bool application_native_q2_inventory_scanner_destroy(application_native_q2_inventory_scanner *o, qa_error *e)
{
    if (!o) return true;
    if (!application_native_q2_inventory_scanner_suspend(o, e)) return false;
    while (o->selected) application_native_q2_inventory_scanner_release(o, o->selected->actor);
    qa_actor_index_destroy(&o->selection_index);
    qa_arena_destroy(&o->readout_storage);
    qa_arena_destroy(&o->name_storage);
    qa_arena_destroy(&o->selection_index_storage);
    qa_arena_destroy(&o->selection_storage);
    qa_json_destroy(o->document); free(o); return true;
}
bool application_native_q2_inventory_scanner_read(application_native_q2_inventory_scanner *o,
    qa_actor_id actor, application_native_q2_inventory_readout *out, qa_error *e)
{
    if (!out || !current(o, e) || !o->active || !application_native_q2_inventory_scanner_idle(o)) return false;
    application_native_q2_inventory_readout r = {0}; ++o->calls;
    bool ok = rows(o, actor, false, &r, e); size_t chosen = SIZE_MAX;
    if (ok && r.present) ok = cursor(o, actor, &r, &chosen, e);
    if (ok && !r.present) {
        inventory_selection *tracked = selection(o, actor);
        if (tracked) { application_native_q2_inventory_source s; int32_t index;
            ok = source(o, actor, &s, e) && read_word(o, s.client + o->cursor, &index, e);
            if (ok && index >= 0 && (uint32_t)index == tracked->index) ok = write_word(o, s.client + o->cursor, o->empty, e);
            if (ok) application_native_q2_inventory_scanner_release(o, actor);
        }
    }
    if (ok && chosen != SIZE_MAX) {
        r.selected = r.rows[chosen].item;
        r.selected_presentation=r.rows[chosen].presentation;
    }
    --o->calls;
    if (!ok) { application_native_q2_inventory_readout_free(&r); return false; }
    *out = r; return true;
}
bool application_native_q2_inventory_scanner_restore_selection(application_native_q2_inventory_scanner *o,
    qa_actor_id actor, qa_item_id item, qa_error *e)
{
    if (!current(o, e) || !application_native_q2_inventory_scanner_idle(o)) return false;
    application_native_q2_inventory_source s;
    if (!source(o, actor, &s, e)) return false;
    if (!item) {
        if (!write_word(o, s.client + o->cursor, o->empty, e)) return false;
        application_native_q2_inventory_scanner_release(o, actor); return true;
    }
    if(!selection(o,actor)&&!qa_id_index_reserve(&o->selection_index.retained,1,e)) return false;
    inventory_selection *retained = selection_take(o, e);
    if (!retained) return false;
    ++o->calls;
    application_native_q2_inventory_readout r = {0}; bool ok = rows(o, actor, true, &r, e); size_t found = SIZE_MAX;
    for (size_t i = 0; ok && i < r.count; ++i) if (r.rows[i].item == item) { found = i; break; }
    if (ok && found == SIZE_MAX) ok = application_fail(e, QA_ERROR_NOT_FOUND, "Saved mixed inventory selection is absent from its actual rows");
    if (ok) ok = application_native_q2_inventory_source_current(o->engine, &s, e) &&
        write_word(o, s.client + o->cursor, (int32_t)r.rows[found].source_index, e);
    if (ok) {
        *retained = (inventory_selection){.actor = actor, .item = item,
            .index = r.rows[found].source_index, .storage_slot = retained->storage_slot};
        ok=selection_install(o,retained,e);
        if(ok) retained=NULL;
    }
    --o->calls; selection_drop(o, retained);
    application_native_q2_inventory_readout_free(&r); return ok;
}
bool application_native_q2_inventory_scanner_capture(application_native_q2_inventory_scanner *o,
    qa_buffer *out, qa_error *e)
{
    if (!out || !current(o, e) || !application_native_q2_inventory_scanner_idle(o)) return false;
    qa_session *session = o->engine->provider->application->session;
    size_t size = 8, count = 0;
    for (inventory_selection *s = o->selected; s; s = s->next) {
        const char *item = qa_strings_cstr(qa_session_strings(session), s->item);
        if (!item || strlen(item) > UINT32_MAX || strlen(item) > SIZE_MAX - size - 20 || count == UINT32_MAX)
            return application_fail(e, QA_ERROR_MEMORY, "Native inventory selection continuation exceeds its source extent");
        size += 20 + strlen(item); ++count;
    }
    qa_buffer buffer = {.data = malloc(size), .size = size};
    if (!buffer.data) return application_fail(e, QA_ERROR_MEMORY, "Capturing actual native inventory selections");
    qa_net_writer w; qa_net_writer_init(&w, buffer.data, buffer.size, e);
    bool ok = qa_net_write_data(&w, "NQIS", 4) && qa_net_write_u32(&w, (uint32_t)count);
    for (inventory_selection *s = o->selected; ok && s; s = s->next) {
        qa_saved_actor_id actor; const char *item = qa_strings_cstr(qa_session_strings(session), s->item);
        ok = qa_actors_save_reference(qa_session_actors(session), s->actor, &actor, e) &&
            qa_net_write_u64(&w, actor.generation) && qa_net_write_u32(&w, actor.slot) &&
            qa_net_write_u32(&w, s->index) && qa_net_write_u32(&w, (uint32_t)strlen(item)) && qa_net_write_data(&w, item, strlen(item));
    }
    if (!ok) { qa_buffer_free(&buffer); return false; }
    *out = buffer; return true;
}
bool application_native_q2_inventory_scanner_restore(application_native_q2_inventory_scanner *o,
    qa_bytes bytes, qa_error *e)
{
    if (!current(o, e) || !application_native_q2_inventory_scanner_idle(o) || o->active)
        return application_fail(e, QA_ERROR_ARGUMENT, "Native inventory selection restore requires its detached real scanner");
    qa_net_reader r; qa_net_reader_init(&r, bytes, e); qa_bytes magic;
    if (!qa_net_read_bytes(&r, 4, &magic) || memcmp(magic.data, "NQIS", 4))
        return application_fail(e, QA_ERROR_FORMAT, "Invalid original inventory scanner continuation");
    uint32_t count = qa_net_read_u32(&r); inventory_selection *head = NULL;
    qa_session *session = o->engine->provider->application->session; bool ok = !r.failed && bytes.size >= 8 && count <= (bytes.size - 8) / 20;
    qa_actor_index decoded={0}; qa_arena decoded_storage={0};
    if(ok) ok=qa_actor_index_create(&decoded,qa_session_actors(session),&decoded_storage,e);
    if(ok) ok=qa_id_index_reserve(&decoded.retained,count,e);
    for (uint32_t i = 0; ok && i < count; ++i) {
        inventory_selection *s = selection_take(o, e);
        if (!s) { ok = false; break; }
        s->next = head; head = s;
        qa_saved_actor_id saved = {.generation = qa_net_read_u64(&r), .slot = qa_net_read_u32(&r)};
        s->index = qa_net_read_u32(&r); uint32_t length = qa_net_read_u32(&r); qa_bytes item;
        const qa_actor_record *actor = qa_actors_resolve_saved(qa_session_actors(session), saved);
        ok = !r.failed && actor && s->index < o->count && length > 0 &&
            qa_net_read_bytes(&r, length, &item) && !memchr(item.data, 0, item.size);
        if (ok) {
            s->actor = actor->id; s->item = qa_strings_find(qa_session_strings(session), item);
            ok = s->item != 0;
            size_t previous;
            if(ok) ok=!qa_actor_index_find_retained(&decoded,s->actor,&previous);
        }
        if (!ok) application_fail(e, QA_ERROR_FORMAT, "Saved inventory selection has no actual canonical actor/item or repeats its actor");
        else ok=qa_actor_index_remember(&decoded,s->actor,(size_t)(uintptr_t)s,e);
    }
    if (ok && (r.failed || bytes.size > SIZE_MAX / 8 || r.bit != bytes.size * 8)) ok = application_fail(e, QA_ERROR_FORMAT, "Native inventory selection continuation has trailing fields");
    if (!ok) {
        qa_actor_index_destroy(&decoded);
        qa_arena_destroy(&decoded_storage);
        while (head) { inventory_selection *s = head; head = s->next; selection_drop(o, s); } return false;
    }
    while (o->selected) application_native_q2_inventory_scanner_release(o, o->selected->actor);
    qa_actor_index_destroy(&o->selection_index);
    qa_arena_destroy(&o->selection_index_storage);
    o->selection_index_storage=decoded_storage;
    decoded.retained.storage=&o->selection_index_storage;
    o->selection_index=decoded;
    o->selected = head; return true;
}
bool application_native_q2_inventory_scanner_finish_restore(application_native_q2_inventory_scanner *o, qa_error *e)
{
    if (!current(o, e) || !application_native_q2_inventory_scanner_idle(o)) return false;
    for (inventory_selection *s = o->selected; s; s = s->next) {
        application_native_q2_inventory_source actual; application_native_q2_inventory_readout r = {0}; int32_t cursor_value;
        bool ok = source(o, s->actor, &actual, e) && read_word(o, actual.client + o->cursor, &cursor_value, e) &&
            rows(o, s->actor, true, &r, e); bool found = false;
        for (size_t i = 0; ok && i < r.count; ++i) if (r.rows[i].item == s->item && r.rows[i].source_index == s->index) found = true;
        application_native_q2_inventory_readout_free(&r);
        if (!ok) return false;
        if (!found || cursor_value < 0 || (uint32_t)cursor_value != s->index)
            return application_fail(e, QA_ERROR_ARGUMENT, "Restored inventory selection differs from genuine restored source RAM and canonical rows");
    }
    return true;
}
bool application_native_q2_inventory_mixed_read(application_provider *provider,qa_actor_id actor,
    application_native_q2_inventory_readout *out,qa_error *e)
{
    if(!out||!provider||provider->kind!=APPLICATION_PROVIDER_NATIVE)
        return application_fail(e,QA_ERROR_ARGUMENT,"Mixed inventory requires its actual native provider");
    struct application_native_q2 *engine=provider->state.native.q2_engine;
    if(!engine) return application_fail(e,QA_ERROR_ARGUMENT,"Mixed inventory lost its native engine");
    if(!engine->inventory_scanner) { *out=(application_native_q2_inventory_readout){0}; return true; }
    return application_native_q2_inventory_scanner_read(engine->inventory_scanner,actor,out,e);
}
