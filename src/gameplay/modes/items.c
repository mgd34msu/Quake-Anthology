#include "internal.h"
#include <inttypes.h>
#include <stdio.h>

bool mode_inventory_item(qa_modes *m, mode_instance *v, qa_item_id source,
                         qa_item_id *out, qa_error *e) {
    for (size_t i = 0; i < v->item_count; ++i)
        if (v->items[i].source == source) {
            *out = v->items[i].inventory;
            return true;
        }
    qa_strings *strings = qa_session_strings(m->options.services.session);
    qa_bytes name = qa_strings_text(strings, source);
    if (!name.size)
        return mode_fail(e, "mode item has no source identity");
    char prefix[128];
    int length = snprintf(prefix, sizeof(prefix), "mode:%" PRIu64 ":%u:%" PRIu64 ":",
                          (uint64_t)m->options.owner, v->id.slot, v->id.generation);
    if (length < 0 || (size_t)length >= sizeof(prefix) || name.size > SIZE_MAX - (size_t)length)
        return mode_fail(e, "mode item identity too long");
    if (v->item_count == v->item_capacity) {
        size_t capacity = v->item_capacity ? v->item_capacity * 2 : 8;
        if (capacity < v->item_capacity || capacity > SIZE_MAX / sizeof(*v->items))
            return mode_fail(e, "mode item capacity exhausted");
        void *items = realloc(v->items, capacity * sizeof(*v->items));
        if (!items) {
            qa_error_set(e, QA_ERROR_MEMORY, 0, "retaining mode item identities");
            return false;
        }
        v->items = items;
        v->item_capacity = capacity;
    }
    size_t size = (size_t)length + name.size;
    unsigned char *text = malloc(size);
    if (!text) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "scoping mode item identity");
        return false;
    }
    memcpy(text, prefix, (size_t)length);
    memcpy(text + length, name.data, name.size);
    bool ok = qa_strings_intern(strings, (qa_bytes){text, size}, out, e);
    free(text);
    if (ok)
        v->items[v->item_count++] = (qa_mode_item_binding){source, *out};
    return ok;
}
static bool item_use(qa_modes *m, qa_mode_id id, qa_actor_id actor, qa_item_id item, qa_item_action action,
                     bool *handled, qa_error *e) {
    mode_instance *v = mode_get(m, id);
    if (!v || !handled || (action != QA_ITEM_USE && action != QA_ITEM_DROP))
        return mode_fail(e, "invalid mode item action");
    *handled = false;
    mode_member *p = mode_member_get(m, v, actor);
    if (!p || !v->value.rules.enabled)
        return true;
    qa_item_id source = 0;
    for (size_t i = 0; i < v->item_count; ++i)
        if (v->items[i].inventory == item)
            source = v->items[i].source;
    if (!source)
        return true;
    mode_object *object = mode_object_get(m, p->relic);
    if (object && object->spec.item == source) {
        if (v->value.rules.source == QA_MODE_THREEWAVE)
            return true;
        bool use = v->value.rules.source == QA_MODE_LMCTF;
        if (action == QA_ITEM_USE && !use)
            return true;
        *handled = true;
        p->relic = (qa_actor_id){0};
        return mode_object_drop(m, v, object, actor, false, e);
    }
    object = mode_object_get(m, p->flag);
    if (object && object->spec.item == source && v->value.rules.source == QA_MODE_LMCTF) {
        *handled = true;
        p->flag = (qa_actor_id){0};
        return mode_object_drop(m, v, object, actor, false, e);
    }
    return true;
}
bool qa_modes_item_action(qa_modes *m, qa_mode_id id, qa_actor_id actor, qa_item_id item, qa_item_action action,
                          bool *handled, qa_error *e) {
    if (!m)
        return mode_fail(e, "invalid mode item service");
    return MODE_CALLBACK(m, item_use(m, id, actor, item, action, handled, e));
}
static bool item_action(void *context, qa_item_id item, qa_item_action action, qa_error *e) {
    mode_player *p = context;
    qa_modes *m = p->modes;
    for (uint32_t i = 0; i < m->mode_capacity; ++i) {
        mode_instance *v = &m->instances[i];
        if (!v->active)
            continue;
        for (size_t j = 0; j < v->item_count; ++j)
            if (v->items[j].inventory == item) {
                bool handled;
                if (!qa_modes_item_action(m, v->id, p->value.actor, item, action, &handled, e))
                    return false;
                return handled || mode_fail(e, "mode item is not held");
            }
    }
    return mode_fail(e, "unknown mode inventory handle");
}
bool qa_modes_publish_items(qa_modes *m, qa_actor_id actor, qa_error *e) {
    mode_player *player = mode_player_get(m, actor);
    if (!player)
        return mode_fail(e, "unknown mode item owner");
    qa_item_definition *added = calloc(m->actor_capacity, sizeof(*added));
    if (!added) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "publishing mode item catalog");
        return false;
    }
    size_t count = 0;
    static const char *runes[] = {"Resistance Rune", "Strength Rune", "Haste Rune",
                                  "Regeneration Rune", "Vampire Rune"};
    for (uint32_t i = 0; i < m->actor_capacity; ++i) {
        mode_object *o = &m->objects[i];
        mode_instance *v = o->active ? mode_get(m, o->mode) : NULL;
        if (!v || !o->spec.item || !mode_member_get(m, v, actor))
            continue;
        qa_item_id item;
        if (!mode_inventory_item(m, v, o->spec.item, &item, e)) {
            free(added);
            return false;
        }
        bool duplicate = false;
        for (size_t j = 0; j < count; ++j)
            if (added[j].item == item)
                duplicate = true;
        if (duplicate)
            continue;
        uint32_t actions =
            o->spec.kind == QA_MODE_OBJECT_RELIC && v->value.rules.source != QA_MODE_THREEWAVE
                ? QA_ITEM_DROP
                : 0;
        if (v->value.rules.source == QA_MODE_LMCTF &&
            (o->spec.kind == QA_MODE_OBJECT_RELIC || o->spec.kind == QA_MODE_OBJECT_FLAG))
            actions |= QA_ITEM_USE | QA_ITEM_DROP;
        added[count++] = (qa_item_definition){
            .item = item,
            .owner = m->options.owner,
            .actions = actions,
            .label = o->spec.kind == QA_MODE_OBJECT_RELIC  ? runes[o->spec.relic]
                     : o->spec.kind == QA_MODE_OBJECT_FLAG ? "Flag"
                                                           : "Tag Token"};
    }
    bool ok = qa_inventory_replace_definitions(m->options.services.inventory, actor,
        m->options.owner, added, count, item_action, player, player->items, &player->items, e);
    free(added);
    return ok;
}
