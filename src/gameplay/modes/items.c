#include "internal.h"

static bool item_use(qa_modes *m, qa_actor_id actor, qa_item_id item, qa_item_action action,
                     bool *handled, qa_error *e) {
    if (!m || !handled || (action != QA_ITEM_USE && action != QA_ITEM_DROP))
        return mode_fail(e, "invalid mode item action");
    *handled = false;
    for (uint32_t i = 0; i < m->mode_capacity; ++i) {
        mode_instance *v = &m->instances[i];
        mode_member *p = mode_member_get(m, v->active ? v : NULL, actor);
        if (!p || !v->value.rules.enabled)
            continue;
        mode_object *object = mode_object_get(m, p->relic);
        if (object && object->spec.item == item) {
            if (v->value.rules.source == QA_MODE_THREEWAVE)
                continue;
            bool use = v->value.rules.source == QA_MODE_LMCTF;
            if (action == QA_ITEM_USE && !use)
                continue;
            *handled = true;
            p->relic = (qa_actor_id){0};
            return mode_object_drop(m, v, object, actor, false, e);
        }
        object = mode_object_get(m, p->flag);
        if (object && object->spec.item == item && v->value.rules.source == QA_MODE_LMCTF) {
            *handled = true;
            p->flag = (qa_actor_id){0};
            return mode_object_drop(m, v, object, actor, false, e);
        }
    }
    return true;
}
bool qa_modes_item_action(qa_modes *m, qa_actor_id actor, qa_item_id item, qa_item_action action,
                          bool *handled, qa_error *e) {
    if (!m)
        return mode_fail(e, "invalid mode item service");
    return MODE_CALLBACK(m, item_use(m, actor, item, action, handled, e));
}
static bool item_action(void *context, qa_item_id item, qa_item_action action, qa_error *e) {
    mode_player *p = context;
    bool handled;
    if (!qa_modes_item_action(p->modes, p->value.actor, item, action, &handled, e))
        return false;
    return handled || mode_fail(e, "mode item is not held");
}
bool qa_modes_publish_items(qa_modes *m, qa_actor_id actor, qa_error *e) {
    mode_player *player = mode_player_get(m, actor);
    if (!player)
        return mode_fail(e, "unknown mode item owner");
    if (player->items.serial &&
        !qa_inventory_close_items(m->options.services.inventory, player->items, e))
        return false;
    player->items = (qa_inventory_lease){0};
    size_t old_count = 0;
    qa_error local = {0};
    bool ok = qa_inventory_item_definitions(m->options.services.inventory, actor, NULL, 0,
                                            &old_count, &local);
    if (!ok && !old_count) {
        if (e)
            *e = local;
        return false;
    }
    if (old_count > SIZE_MAX / sizeof(qa_item_definition))
        return mode_fail(e, "item catalog too large");
    qa_item_definition *old = old_count ? malloc(old_count * sizeof(*old)) : NULL;
    qa_item_definition *added = calloc(m->actor_capacity, sizeof(*added));
    if ((old_count && !old) || !added) {
        free(old);
        free(added);
        qa_error_set(e, QA_ERROR_MEMORY, 0, "publishing mode item catalog");
        return false;
    }
    if (!qa_inventory_item_definitions(m->options.services.inventory, actor, old, old_count,
                                       &old_count, e)) {
        free(old);
        free(added);
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
        bool duplicate = false;
        for (size_t j = 0; j < old_count; ++j)
            if (old[j].item == o->spec.item)
                duplicate = true;
        for (size_t j = 0; j < count; ++j)
            if (added[j].item == o->spec.item)
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
            .item = o->spec.item,
            .owner = m->options.owner,
            .actions = actions,
            .label = o->spec.kind == QA_MODE_OBJECT_RELIC  ? runes[o->spec.relic]
                     : o->spec.kind == QA_MODE_OBJECT_FLAG ? "Flag"
                                                           : "Tag Token"};
    }
    ok = !count ||
         qa_inventory_bind_definitions(m->options.services.inventory, actor, m->options.owner,
                                       added, count, item_action, player, &player->items, e);
    free(old);
    free(added);
    return ok;
}
