#include "native_q2_inventory_scanner.h"
#include "native_q2_inventory_source.h"
#include "guest_native_q2_private.h"
#include "guest_native_q2_attack.h"
#include "qa/native_observe.h"
#include "qa/text.h"
#include "qa/network.h"
#include <math.h>

typedef struct inventory_write_field { uint32_t offset, bytes; } inventory_write_field;
typedef struct inventory_selection {
    struct inventory_selection *next;
    qa_actor_id actor;
    qa_item_id item;
    uint32_t index;
} inventory_selection;
typedef struct inventory_restore {
    struct inventory_restore *next;
    application_native_q2_inventory_source source;
    qa_native_address address;
    qa_buffer bytes;
} inventory_restore;
typedef struct inventory_frame {
    struct inventory_frame *previous;
    qa_actor_id actor;
    int32_t flags;
    bool named, has_named_row;
    application_native_q2_inventory_row named_row;
    qa_buffer requested;
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
} inventory_region;
struct application_native_q2_inventory_scanner {
    struct application_native_q2 *engine;
    const qa_native_declaration *declaration;
    qa_json_document *document;
    application_native_q2_inventory_scanner_options options;
    inventory_write_field *writes;
    size_t write_count;
    inventory_entry entries[5];
    inventory_region regions[6];
    size_t region_count;
    inventory_selection *selected;
    inventory_frame *frame;
    inventory_restore *pending;
    const qa_native_region_event *command_event;
    qa_actor_id command_actor;
    uint32_t inventory, count, cursor, client_pointer, item_table, item_stride, item_count;
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
    for (inventory_selection *s = o->selected; s; s = s->next)
        if (qa_actor_id_equal(s->actor, actor)) return s;
    return NULL;
}
void application_native_q2_inventory_scanner_release(application_native_q2_inventory_scanner *o, qa_actor_id actor)
{
    if (!o) return;
    inventory_selection **p = &o->selected;
    while (*p) {
        inventory_selection *s = *p;
        if (qa_actor_id_equal(s->actor, actor)) { *p = s->next; free(s); return; }
        p = &s->next;
    }
}
void application_native_q2_inventory_readout_free(application_native_q2_inventory_readout *r)
{
    if (!r) return;
    for (size_t i = 0; i < r->count; ++i) {
        free((void *)r->rows[i].label);
        free((void *)r->rows[i].presentation.icon); free((void *)r->rows[i].presentation.lump);
    }
    free((void *)r->selected_presentation.icon); free((void *)r->selected_presentation.lump);
    free(r->rows); *r = (application_native_q2_inventory_readout){0};
}
static bool copy_text(const char *text, const char **out, qa_error *e)
{
    *out = NULL; if (!text) return true;
    size_t bytes = strlen(text) + 1; char *copy = malloc(bytes);
    if (!copy) return application_fail(e, QA_ERROR_MEMORY, "Copying actual inventory presentation text");
    memcpy(copy, text, bytes); *out = copy; return true;
}
static bool copy_presentation(const application_native_q2_inventory_presentation *from,
    application_native_q2_inventory_presentation *out, qa_error *e)
{
    *out = *from; out->icon = out->lump = NULL;
    return copy_text(from->icon, &out->icon, e) && copy_text(from->lump, &out->lump, e);
}
static bool rows(application_native_q2_inventory_scanner *o, qa_actor_id actor,
    application_native_q2_inventory_readout *out, qa_error *e)
{
    const application_native_q2_inventory_row *borrowed = NULL; size_t count = 0; bool present = false;
    if (!o->options.rows(o->options.context, actor, &borrowed, &count, &present, e)) return false;
    if ((!present && (count || borrowed)) || (count && !borrowed) || count > SIZE_MAX / sizeof(*borrowed))
        return application_fail(e, QA_ERROR_ARGUMENT, "Native inventory row producer returned an invalid acquisition");
    application_native_q2_inventory_readout r = {.present = present};
    r.rows = count ? calloc(count, sizeof(*r.rows)) : NULL;
    if (count && !r.rows) return application_fail(e, QA_ERROR_MEMORY, "Copying current mixed inventory rows");
    for (size_t i = 0; i < count; ++i) {
        if (!borrowed[i].item || !borrowed[i].label || borrowed[i].source_index >= o->count) {
            application_native_q2_inventory_readout_free(&r);
            return application_fail(e, QA_ERROR_ARGUMENT, "Mixed inventory row lacks its actual source prototype");
        }
        r.rows[i] = borrowed[i]; r.rows[i].label = NULL;
        r.rows[i].presentation.icon = r.rows[i].presentation.lump = NULL; ++r.count;
        size_t length = strlen(borrowed[i].label);
        char *label = malloc(length + 1);
        if (!label) { application_native_q2_inventory_readout_free(&r); return application_fail(e, QA_ERROR_MEMORY, "Copying mixed inventory labels"); }
        memcpy(label, borrowed[i].label, length + 1); r.rows[i].label = label;
        if (!copy_presentation(&borrowed[i].presentation, &r.rows[i].presentation, e)) {
            application_native_q2_inventory_readout_free(&r); return false;
        }
        for (size_t j = 0; j < i; ++j) if (r.rows[j].item == r.rows[i].item) {
            application_native_q2_inventory_readout_free(&r);
            return application_fail(e, QA_ERROR_ARGUMENT, "Mixed inventory producer repeats a canonical item");
        }
    }
    *out = r; return true;
}
static bool source(application_native_q2_inventory_scanner *o, qa_actor_id actor,
    application_native_q2_inventory_source *out, qa_error *e)
{
    if (!current(o, e) || !application_native_q2_inventory_source_read(o->engine, actor, out, e)) return false;
    if (out->inventory_offset != o->inventory || out->count != o->count || out->cursor_offset != o->cursor || out->empty != o->empty)
        return application_fail(e, QA_ERROR_ARGUMENT, "Native scanner profile differs from its actual inventory owner");
    for (size_t i = 0; i < o->write_count; ++i)
        if (o->writes[i].offset > out->client_bytes || o->writes[i].bytes > out->client_bytes - o->writes[i].offset)
            return application_fail(e, QA_ERROR_FORMAT, "Native scanner selection write leaves its declared client");
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
static bool save_bytes(application_native_q2_inventory_scanner *o,
    const application_native_q2_inventory_source *s, uint32_t offset, size_t count,
    inventory_restore **chain, qa_error *e)
{
    inventory_restore *r = calloc(1, sizeof(*r));
    if (!r) return application_fail(e, QA_ERROR_MEMORY, "Retaining original inventory temporary write");
    r->source = *s; r->address = s->client + offset; r->bytes.data = malloc(count); r->bytes.size = count;
    if (!r->bytes.data || !qa_native_read(instance(o), r->address, r->bytes.data, count, e)) {
        qa_buffer_free(&r->bytes); free(r); return false;
    }
    r->next = *chain; *chain = r; return true;
}
static void discard(inventory_restore **chain)
{
    while (*chain) { inventory_restore *r = *chain; *chain = r->next; qa_buffer_free(&r->bytes); free(r); }
}
static bool restore(application_native_q2_inventory_scanner *o, inventory_restore **chain, qa_error *e)
{
    while (*chain) {
        inventory_restore *r = *chain;
        if (qa_actors_get(qa_session_actors(o->engine->provider->application->session), r->source.actor)) {
            if (!application_native_q2_inventory_source_current(o->engine, &r->source, e) ||
                !qa_native_write(instance(o), r->address, (qa_bytes){r->bytes.data, r->bytes.size}, e)) return false;
        }
        *chain = r->next; qa_buffer_free(&r->bytes); free(r);
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
{ return r->presence_only ? r->count ? 1 : 0 : r->count; }
static int32_t count_word(const application_native_q2_inventory_row *r)
{
    double n = source_count(r);
    if (!isfinite(n) || n == 0) return 0;
    n = fmod(trunc(n), 4294967296.0);
    if (n < 0) n += 4294967296.0;
    return n >= 2147483648.0 ? (int32_t)(n - 4294967296.0) : (int32_t)n;
}
static bool evaluate(application_native_q2_inventory_scanner *o, qa_actor_id actor,
    const application_native_q2_inventory_row *r, int direction, int32_t flags,
    const qa_native_region_event *event, bool *accepted, qa_error *e)
{
    application_native_q2_inventory_source s; inventory_restore *inventory = NULL, *writes = NULL;
    *accepted = false;
    if (!source(o, actor, &s, e) || !r->source_index || r->source_index >= o->count) return false;
    if (!save_bytes(o, &s, o->inventory, (size_t)o->count * 4, &inventory, e)) return false;
    for (size_t i = 0; i < o->write_count; ++i)
        if (!save_bytes(o, &s, o->writes[i].offset, o->writes[i].bytes, &writes, e)) {
            discard(&inventory); discard(&writes); return false;
        }
    uint8_t *zero = calloc(o->count, 4);
    if (!zero) { discard(&inventory); discard(&writes); return application_fail(e, QA_ERROR_MEMORY, "Projecting original inventory scanner"); }
    ++o->evaluating;
    bool ok = qa_native_write(instance(o), s.client + o->inventory, (qa_bytes){zero, (size_t)o->count * 4}, e) &&
        write_word(o, s.client + o->inventory + (uint64_t)r->source_index * 4, count_word(r), e) &&
        write_word(o, s.client + o->cursor, 0, e);
    free(zero);
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
    if (!restored && ok) { ok = false; if (e) *e = cleanup; }
    return ok;
}
static bool navigate(application_native_q2_inventory_scanner *o, inventory_frame *frame,
    const application_native_q2_inventory_readout *r, int direction,
    const qa_native_region_event *event, qa_error *e)
{
    size_t chosen;
    if (!cursor(o, frame->actor, r, &chosen, e)) return false;
    inventory_selection *retained=calloc(1,sizeof(*retained));
    if(!retained) return application_fail(e,QA_ERROR_MEMORY,"Reserving reached native inventory selection");
    for (size_t step = 1; step <= r->count; ++step) {
        size_t index;
        if (direction > 0) index = chosen == SIZE_MAX ? step - 1 : (chosen + step) % r->count;
        else index = chosen == SIZE_MAX ? (r->count - step) % r->count : (chosen + r->count - step) % r->count;
        const application_native_q2_inventory_row *row = r->rows + index;
        if (!row->count) continue;
        bool duplicate = false;
        for (size_t prior = 1; prior < step; ++prior) {
            size_t p = direction > 0 ? chosen == SIZE_MAX ? prior - 1 : (chosen + prior) % r->count :
                chosen == SIZE_MAX ? (r->count - prior) % r->count : (chosen + r->count - prior) % r->count;
            if (r->rows[p].count && r->rows[p].source_index == row->source_index && source_count(r->rows + p) == source_count(row)) { duplicate = true; break; }
        }
        if (duplicate) continue;
        bool accepted;
        if (!evaluate(o, frame->actor, row, direction, frame->flags, event, &accepted, e)) { free(retained); return false; }
        if (accepted) {
            application_native_q2_inventory_scanner_release(o,frame->actor);
            *retained=(inventory_selection){.actor=frame->actor,.item=row->item,.index=row->source_index,.next=o->selected};
            o->selected=retained; return true;
        }
    }
    free(retained);
    application_native_q2_inventory_source s;
    if (!source(o, frame->actor, &s, e) || !write_word(o, s.client + o->cursor, o->empty, e)) return false;
    application_native_q2_inventory_scanner_release(o, frame->actor); return true;
}
static bool entry_call(void *opaque, qa_native_instance *actual, qa_native_entry_observer *binding,
    const qa_native_value *arguments, size_t count, qa_native_value *result, qa_error *e)
{
    inventory_entry *entry = opaque; application_native_q2_inventory_scanner *o = entry->owner;
    if (actual != instance(o) || binding != entry->binding || !current(o, e) || !count ||
        arguments[0].type != QA_NATIVE_ADDRESS || o->pending)
        return application_fail(e, QA_ERROR_ARGUMENT, "Native scanner entry lost its exact source invocation");
    inventory_frame frame = {.previous = o->frame, .flags = -1, .named = entry->kind == 4};
    if (count > 1 && arguments[1].type == QA_NATIVE_I32) frame.flags = arguments[1].as.i32;
    if (arguments[0].as.address && !qa_native_host_source_actor(o->engine->provider->state.native.host,
        arguments[0].as.address, true, &frame.actor, e)) return false;
    o->frame = &frame; ++o->calls;
    bool ok = true;
    if (entry->kind == 2 && frame.actor.registry && !o->evaluating) {
        application_native_q2_inventory_readout r = {0}; size_t chosen = SIZE_MAX;
        ok = rows(o, frame.actor, &r, e);
        if (ok && r.present) ok = cursor(o, frame.actor, &r, &chosen, e);
        if (ok && chosen != SIZE_MAX && r.rows[chosen].selected) {
            application_native_q2_inventory_source s;
            ok = source(o, frame.actor, &s, e) && save_bytes(o, &s,
                o->inventory + r.rows[chosen].source_index * 4, 4, &frame.restore, e) &&
                write_word(o, s.client + o->inventory + (uint64_t)r.rows[chosen].source_index * 4,
                    count_word(r.rows + chosen), e);
        }
        application_native_q2_inventory_readout_free(&r);
    }
    if (ok) ok = qa_native_invoke_original(binding, arguments, count, result, e);
    qa_error cleanup = {0}; bool cleaned = restore(o, &frame.restore, &cleanup);
    pending(o, &frame.restore); qa_buffer_free(&frame.requested);
    if (!cleaned && ok) { ok = false; if (e) *e = cleanup; }
    --o->calls; o->frame = frame.previous; return ok;
}
static bool normalized(const char *text, qa_buffer *out, qa_error *e)
{
    qa_buffer lowered = {0}; size_t length = strlen(text);
    if (!qa_utf8_lower((qa_bytes){(const uint8_t *)text, length}, &lowered, e)) return false;
    size_t written = 0;
    for (size_t i = 0; i < lowered.size; ++i) if (lowered.data[i] != ' ') lowered.data[written++] = lowered.data[i];
    lowered.data[written] = 0; lowered.size = written; *out = lowered; return true;
}
static bool same_name(const char *text, const qa_buffer *request, bool *same, qa_error *e)
{
    qa_buffer value = {0};
    if (!normalized(text, &value, e)) return false;
    *same = value.size == request->size && (!value.size || !memcmp(value.data, request->data, value.size));
    qa_buffer_free(&value); return true;
}
static bool named_choice(application_native_q2_inventory_scanner *o, inventory_frame *frame,
    const application_native_q2_inventory_readout *r, qa_native_address original,
    size_t *chosen, qa_error *e)
{
    *chosen = SIZE_MAX;
    qa_strings *strings = qa_session_strings(o->engine->provider->application->session);
    for (size_t i = 0; i < r->count; ++i) {
        const char *item = qa_strings_cstr(strings, r->rows[i].item); bool match;
        if (!item || !same_name(item, &frame->requested, &match, e)) return false;
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
        if (!same_name(r->rows[i].label, &frame->requested, &match, e)) return false;
        if (match) { *chosen = i; ++matches; }
    }
    if (matches <= 1) return true;
    *chosen = SIZE_MAX;
    if (original) return true;
    size_t needed = frame->requested.size + 40;
    for (size_t i = 0; i < r->count; ++i) {
        bool match;
        if (!same_name(r->rows[i].label, &frame->requested, &match, e)) return false;
        if (match) {
            const char *item = qa_strings_cstr(strings, r->rows[i].item);
            if (!item || strlen(item) > SIZE_MAX - needed - 2)
                return application_fail(e, QA_ERROR_MEMORY, "Ambiguous inventory name exceeds its report extent");
            needed += strlen(item) + 2;
        }
    }
    char *text = malloc(needed);
    if (!text) return application_fail(e, QA_ERROR_MEMORY, "Reporting ambiguous inventory name");
    size_t at = (size_t)snprintf(text, needed, "Ambiguous item \"%s\"; use ", (char *)frame->requested.data); size_t emitted = 0;
    bool ok = true;
    for (size_t i = 0; ok && i < r->count; ++i) {
        bool match; ok = same_name(r->rows[i].label, &frame->requested, &match, e);
        if (ok && match) {
            const char *item = qa_strings_cstr(strings, r->rows[i].item); size_t bytes = strlen(item);
            if (emitted++) { memcpy(text + at, ", ", 2); at += 2; }
            memcpy(text + at, item, bytes); at += bytes;
        }
    }
    text[at++] = '\n'; text[at] = 0;
    if (ok) ok = o->options.print(o->options.context, frame->actor, text, e);
    free(text); return ok;
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
    if (!name) return application_fail(e, QA_ERROR_ARGUMENT, "Original native item lookup has no actual name argument");
    qa_buffer text = {0}; bool ok = qa_native_read_string(instance(o), name, 1024 * 1024, &text, e);
    qa_buffer_free(&frame->requested);
    if (ok) ok = normalized((char *)text.data, &frame->requested, e);
    qa_buffer_free(&text); return ok;
}
static bool region_call(void *opaque, qa_native_instance *actual, const qa_native_region_event *event,
    qa_native_region_decision *decision, qa_error *e)
{
    inventory_region *region = opaque; application_native_q2_inventory_scanner *o = region->owner;
    if (actual != instance(o) || event->region.id != region->id || !current(o, e)) return false;
    inventory_frame *frame = o->frame;
    if (!frame || !frame->actor.registry || o->evaluating) return true;
    if (region->kind == 3 && frame->named) {
        if (event->phase == QA_NATIVE_REGION_ENTER) return lookup_name(o, event, frame, e);
        application_native_q2_inventory_readout r = {0}; size_t chosen = SIZE_MAX;
        qa_native_address original = o->pointer_bytes == 4 ? (uint32_t)event->state.registers[QA_NATIVE_RAX] : event->state.registers[QA_NATIVE_RAX];
        bool ok = rows(o, frame->actor, &r, e);
        if (ok && r.present) ok = named_choice(o, frame, &r, original, &chosen, e);
        if (ok && chosen != SIZE_MAX && r.rows[chosen].selected) {
            application_native_q2_inventory_source s; qa_native_address descriptor;
            uint32_t index = r.rows[chosen].source_index;
            if (index >= o->item_count) ok = application_fail(e, QA_ERROR_FORMAT, "Mixed inventory prototype leaves the genuine descriptor table");
            if (ok) ok = source(o, frame->actor, &s, e) && qa_native_rva(instance(o),
                (uint64_t)o->item_table + (uint64_t)index * o->item_stride, o->item_stride, &descriptor, e) &&
                save_bytes(o, &s, o->inventory + index * 4, 4, &frame->restore, e) &&
                write_word(o, s.client + o->inventory + (uint64_t)index * 4, count_word(r.rows + chosen), e);
            if (ok) {
                frame->named_row = r.rows[chosen]; frame->named_row.label = NULL;
                frame->named_row.presentation = (application_native_q2_inventory_presentation){0}; frame->has_named_row = true;
                decision->replace_state = true; decision->state = event->state; decision->state.registers[QA_NATIVE_RAX] = descriptor;
            }
        }
        application_native_q2_inventory_readout_free(&r); return ok;
    }
    if (event->phase != QA_NATIVE_REGION_ENTER) return true;
    application_native_q2_inventory_readout r = {0}; bool ok = rows(o, frame->actor, &r, e);
    if (!ok || !r.present) { application_native_q2_inventory_readout_free(&r); return ok; }
    if (region->kind == 1) {
        ok = navigate(o, frame, &r, region->direction, event, e);
        if (ok) decision->action = QA_NATIVE_REGION_SKIP_TO_JOIN;
    } else if (region->kind == 2) {
        size_t chosen = SIZE_MAX;
        if (!frame->named) ok = cursor(o, frame->actor, &r, &chosen, e);
        const application_native_q2_inventory_row *row = frame->named ? frame->has_named_row ? &frame->named_row : NULL :
            chosen != SIZE_MAX ? r.rows + chosen : NULL;
        if (ok && row && row->selected) {
            ok = restore(o, &frame->restore, e);
            if (ok) {
                const qa_native_region_event *previous = o->command_event;
                qa_actor_id previous_actor = o->command_actor;
                o->command_event = event; o->command_actor = frame->actor;
                ok = o->options.use(o->options.context, frame->actor, row->item, e);
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
        !options->rows || !options->use || !options->print || !engine->provider->state.native.host)
        return application_fail(e, QA_ERROR_ARGUMENT, "Native inventory scanner requires its actual primary profile and row services");
    *out = NULL;
    application_native_q2_inventory_scanner *o = calloc(1, sizeof(*o));
    if (!o) return application_fail(e, QA_ERROR_MEMORY, "Constructing original native inventory scanner");
    o->engine = engine; o->declaration = engine->declaration; o->options = *options; *out = o;
    qa_native_module_info info = qa_native_module_describe(engine->provider->state.native.module);
    o->abi = info.image.target.abi; o->pointer_bytes = info.image.target.pointer_bytes;
    if (!qa_json_parse(qa_native_declaration_primary(o->declaration), &o->document, e)) return false;
    const qa_json_document *d = o->document; qa_json_id root = qa_json_root(d), inventory = qa_json_get(d, root, "inventory");
    qa_json_id items = qa_json_get(d, qa_json_get(d, root, "commands"), "items"); int64_t empty;
    if (!word(d, inventory, "client", &o->client_pointer, e) || !word(d, inventory, "inventory", &o->inventory, e) ||
        !word(d, inventory, "count", &o->count, e) || !word(d, inventory, "cursor", &o->cursor, e) ||
        !qa_json_i64(d, qa_json_get(d, inventory, "empty"), &empty, e) || empty < INT32_MIN || empty > INT32_MAX ||
        !o->count || o->count > 65536 || !word(d, items, "table", &o->item_table, e) ||
        !word(d, items, "stride", &o->item_stride, e) || !word(d, items, "count", &o->item_count, e) || !o->item_stride)
        return application_fail(e, QA_ERROR_FORMAT, "Native scanner profile has invalid actual source extents");
    o->empty = (int32_t)empty;
    qa_json_id writes = qa_json_get(d, inventory, "selectionWrites");
    if (qa_json_type(d, writes) != QA_JSON_ARRAY || qa_json_size(d, writes) > SIZE_MAX / sizeof(*o->writes))
        return application_fail(e, QA_ERROR_FORMAT, "Native scanner profile has no selection write inventory");
    o->write_count = qa_json_size(d, writes); o->writes = o->write_count ? calloc(o->write_count, sizeof(*o->writes)) : NULL;
    if (o->write_count && !o->writes) return application_fail(e, QA_ERROR_MEMORY, "Retaining original selection writes");
    for (size_t i = 0; i < o->write_count; ++i)
        if (!word(d, qa_json_at(d, writes, i), "offset", &o->writes[i].offset, e) ||
            !word(d, qa_json_at(d, writes, i), "bytes", &o->writes[i].bytes, e) || !o->writes[i].bytes)
            return application_fail(e, QA_ERROR_FORMAT, "Native scanner selection write has an empty extent");
    const char *names[] = {"next", "previous", "validate", "use", "namedUse"};
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
    return true;
}
bool application_native_q2_inventory_scanner_activate(application_native_q2_inventory_scanner *o, qa_error *e)
{
    if (!current(o, e) || o->calls || o->pending || !o->engine->map_ready)
        return application_fail(e, QA_ERROR_ARGUMENT, "Native scanner activation requires its returned source map");
    for (size_t i = 0; i < 5; ++i) if (!o->entries[i].binding && !qa_native_observe_entry(instance(o),
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
    for (size_t i = 5; i; --i) if (o->entries[i - 1].binding) {
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
    free(o->writes); qa_json_destroy(o->document); free(o); return true;
}
bool application_native_q2_inventory_scanner_read(application_native_q2_inventory_scanner *o,
    qa_actor_id actor, application_native_q2_inventory_readout *out, qa_error *e)
{
    if (!out || !current(o, e) || !o->active || !application_native_q2_inventory_scanner_idle(o)) return false;
    application_native_q2_inventory_readout r = {0}; ++o->calls;
    bool ok = rows(o, actor, &r, e); size_t chosen = SIZE_MAX;
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
        ok = copy_presentation(&r.rows[chosen].presentation, &r.selected_presentation, e);
    }
    if (ok) { size_t retained = 0;
        for (size_t i = 0; i < r.count; ++i) if (r.rows[i].count) r.rows[retained++] = r.rows[i]; else {
            free((void *)r.rows[i].label); free((void *)r.rows[i].presentation.icon); free((void *)r.rows[i].presentation.lump);
        }
        r.count = retained;
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
    inventory_selection *retained = calloc(1, sizeof(*retained));
    if (!retained) return application_fail(e, QA_ERROR_MEMORY, "Reserving restored native inventory selection");
    ++o->calls;
    application_native_q2_inventory_readout r = {0}; bool ok = rows(o, actor, &r, e); size_t found = SIZE_MAX;
    for (size_t i = 0; ok && i < r.count; ++i) if (r.rows[i].item == item) { found = i; break; }
    if (ok && found == SIZE_MAX) ok = application_fail(e, QA_ERROR_NOT_FOUND, "Saved mixed inventory selection is absent from its actual rows");
    if (ok) ok = application_native_q2_inventory_source_current(o->engine, &s, e) &&
        write_word(o, s.client + o->cursor, (int32_t)r.rows[found].source_index, e);
    if (ok) {
        application_native_q2_inventory_scanner_release(o, actor);
        *retained = (inventory_selection){.actor = actor, .item = item,
            .index = r.rows[found].source_index, .next = o->selected};
        o->selected = retained; retained = NULL;
    }
    --o->calls; free(retained);
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
    for (uint32_t i = 0; ok && i < count; ++i) {
        inventory_selection *s = calloc(1, sizeof(*s));
        if (!s) { ok = application_fail(e, QA_ERROR_MEMORY, "Decoding actual inventory selection mapping"); break; }
        s->next = head; head = s;
        qa_saved_actor_id saved = {.generation = qa_net_read_u64(&r), .slot = qa_net_read_u32(&r)};
        s->index = qa_net_read_u32(&r); uint32_t length = qa_net_read_u32(&r); qa_bytes item;
        const qa_actor_record *actor = qa_actors_resolve_saved(qa_session_actors(session), saved);
        ok = !r.failed && actor && s->index < o->count && length > 0 &&
            qa_net_read_bytes(&r, length, &item) && !memchr(item.data, 0, item.size);
        if (ok) {
            s->actor = actor->id; s->item = qa_strings_find(qa_session_strings(session), item);
            ok = s->item != 0;
            for (inventory_selection *prior = s->next; ok && prior; prior = prior->next)
                ok = !qa_actor_id_equal(prior->actor, s->actor);
        }
        if (!ok) application_fail(e, QA_ERROR_FORMAT, "Saved inventory selection has no actual canonical actor/item or repeats its actor");
    }
    if (ok && (r.failed || bytes.size > SIZE_MAX / 8 || r.bit != bytes.size * 8)) ok = application_fail(e, QA_ERROR_FORMAT, "Native inventory selection continuation has trailing fields");
    if (!ok) { while (head) { inventory_selection *s = head; head = s->next; free(s); } return false; }
    while (o->selected) application_native_q2_inventory_scanner_release(o, o->selected->actor);
    o->selected = head; return true;
}
bool application_native_q2_inventory_scanner_finish_restore(application_native_q2_inventory_scanner *o, qa_error *e)
{
    if (!current(o, e) || !application_native_q2_inventory_scanner_idle(o)) return false;
    for (inventory_selection *s = o->selected; s; s = s->next) {
        application_native_q2_inventory_source actual; application_native_q2_inventory_readout r = {0}; int32_t cursor_value;
        bool ok = source(o, s->actor, &actual, e) && read_word(o, actual.client + o->cursor, &cursor_value, e) &&
            rows(o, s->actor, &r, e); bool found = false;
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
