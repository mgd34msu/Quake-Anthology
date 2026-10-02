#include "internal.h"
#include "source_weapon_library.h"
#include "source_weapon_standalone.h"

#define FIELD(type, field, text, kind)                                                             \
    {text, offsetof(type, field), sizeof(((type *)0)->field), kind}
static const bot_field projectile_fields[] = {
    FIELD(qa_bot_projectile_info, name, "name", BOT_FIELD_STRING),
    FIELD(qa_bot_projectile_info, model, "model", BOT_FIELD_STRING),
    FIELD(qa_bot_projectile_info, flags, "flags", BOT_FIELD_INT),
    FIELD(qa_bot_projectile_info, gravity, "gravity", BOT_FIELD_FLOAT),
    FIELD(qa_bot_projectile_info, damage, "damage", BOT_FIELD_INT),
    FIELD(qa_bot_projectile_info, radius, "radius", BOT_FIELD_FLOAT),
    FIELD(qa_bot_projectile_info, visible_damage, "visdamage", BOT_FIELD_INT),
    FIELD(qa_bot_projectile_info, damage_type, "damagetype", BOT_FIELD_INT),
    FIELD(qa_bot_projectile_info, health_increase, "healthinc", BOT_FIELD_INT),
    FIELD(qa_bot_projectile_info, push, "push", BOT_FIELD_FLOAT),
    FIELD(qa_bot_projectile_info, detonation, "detonation", BOT_FIELD_FLOAT),
    FIELD(qa_bot_projectile_info, bounce, "bounce", BOT_FIELD_FLOAT),
    FIELD(qa_bot_projectile_info, bounce_friction, "bouncefric", BOT_FIELD_FLOAT),
    FIELD(qa_bot_projectile_info, bounce_stop, "bouncestop", BOT_FIELD_FLOAT)};
static const bot_field weapon_fields[] = {
    FIELD(qa_bot_weapon_info, number, "number", BOT_FIELD_INT),
    FIELD(qa_bot_weapon_info, name, "name", BOT_FIELD_STRING),
    FIELD(qa_bot_weapon_info, model, "model", BOT_FIELD_STRING),
    FIELD(qa_bot_weapon_info, level, "level", BOT_FIELD_INT),
    FIELD(qa_bot_weapon_info, weapon_inventory, "weaponindex", BOT_FIELD_INT),
    FIELD(qa_bot_weapon_info, flags, "flags", BOT_FIELD_INT),
    FIELD(qa_bot_weapon_info, projectile, "projectile", BOT_FIELD_STRING),
    FIELD(qa_bot_weapon_info, projectile_count, "numprojectiles", BOT_FIELD_INT),
    FIELD(qa_bot_weapon_info, horizontal_spread, "hspread", BOT_FIELD_FLOAT),
    FIELD(qa_bot_weapon_info, vertical_spread, "vspread", BOT_FIELD_FLOAT),
    FIELD(qa_bot_weapon_info, speed, "speed", BOT_FIELD_FLOAT),
    FIELD(qa_bot_weapon_info, acceleration, "acceleration", BOT_FIELD_FLOAT),
    FIELD(qa_bot_weapon_info, recoil, "recoil", BOT_FIELD_VECTOR),
    FIELD(qa_bot_weapon_info, offset, "offset", BOT_FIELD_VECTOR),
    FIELD(qa_bot_weapon_info, angle_offset, "angleoffset", BOT_FIELD_VECTOR),
    FIELD(qa_bot_weapon_info, extra_z_velocity, "extrazvelocity", BOT_FIELD_FLOAT),
    FIELD(qa_bot_weapon_info, ammo_amount, "ammoamount", BOT_FIELD_INT),
    FIELD(qa_bot_weapon_info, ammo_inventory, "ammoindex", BOT_FIELD_INT),
    FIELD(qa_bot_weapon_info, activate, "activate", BOT_FIELD_FLOAT),
    FIELD(qa_bot_weapon_info, reload, "reload", BOT_FIELD_FLOAT),
    FIELD(qa_bot_weapon_info, spin_up, "spinup", BOT_FIELD_FLOAT),
    FIELD(qa_bot_weapon_info, spin_down, "spindown", BOT_FIELD_FLOAT)};
#undef FIELD
void qa_bot_weapons_retain(qa_bot_weapons *c) {
    if (c != NULL)
        atomic_fetch_add_explicit(&c->references, 1, memory_order_relaxed);
}
void qa_bot_weapons_release(qa_bot_weapons *c) {
    if (c != NULL && atomic_fetch_sub_explicit(&c->references, 1, memory_order_acq_rel) == 1) {
        bot_weapon_resource_destroy(c->source);
        free(c->weapons);
        free(c->projectiles);
        qa_arena_destroy(&c->arena);
        free(c);
    }
}
const qa_bot_weapons_view *qa_bot_weapons_read(const qa_bot_weapons *c) {
    return c == NULL ? NULL : bot_weapons_source_view((qa_bot_weapons *)c,NULL);
}
bool qa_bot_weapons_load(qa_bot_library *library, const char *path, size_t weapon_capacity,
                         size_t projectile_capacity, qa_bot_weapons **out, qa_error *e) {
    bool source_failure;
    return bot_weapons_load_source(library,path,weapon_capacity,projectile_capacity,NULL,out,&source_failure,e);
}
static bool fields_valid(const void *data, const bot_field *fields, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        const uint8_t *bytes = (const uint8_t *)data + fields[i].offset;
        if (fields[i].kind == BOT_FIELD_STRING) {
            if (memchr(bytes, 0, fields[i].size) == NULL)
                return false;
        } else if (fields[i].kind == BOT_FIELD_FLOAT) {
            float value;
            memcpy(&value, bytes, sizeof(value));
            if (!isfinite(value))
                return false;
        } else if (fields[i].kind == BOT_FIELD_VECTOR) {
            qa_vec3 value;
            memcpy(&value, bytes, sizeof(value));
            if (!qa_vec_finite(value))
                return false;
        }
    }
    return true;
}
bool qa_bot_weapons_restore(const qa_bot_weapons_view *view, qa_bot_weapons **out, qa_error *e) {
    if (view == NULL || out == NULL || view->path == NULL || view->weapon_capacity > INT32_MAX ||
        view->projectile_capacity > INT32_MAX ||
        view->weapon_capacity > SIZE_MAX / sizeof(qa_bot_weapon_info) ||
        view->projectile_capacity > SIZE_MAX / sizeof(qa_bot_projectile_info) ||
        view->weapon_count > view->weapon_capacity ||
        view->projectile_count > view->projectile_capacity ||
        (view->weapon_capacity != 0 && view->weapons == NULL) ||
        (view->projectile_count != 0 && view->projectiles == NULL))
        goto invalid;
    for (size_t i = 0; i < view->weapon_capacity; ++i)
        if (view->weapons[i].valid &&
            (view->weapons[i].number != (int64_t)i ||
             !fields_valid(view->weapons + i, weapon_fields,
                           sizeof(weapon_fields) / sizeof(*weapon_fields))))
            goto invalid;
    for (size_t i = 0; i < view->projectile_count; ++i)
        if (!fields_valid(view->projectiles + i, projectile_fields,
                          sizeof(projectile_fields) / sizeof(*projectile_fields)))
            goto invalid;
    qa_bot_weapons *c=calloc(1,sizeof(*c));
    if(!c) {qa_error_set(e,QA_ERROR_MEMORY,0,"Restoring actual weapon resource binding");return false;}
    atomic_init(&c->references,1);
    if(!bot_weapon_standalone_restore(view,&c->source,e)) {qa_bot_weapons_release(c);return false;}
    *out = c;
    return true;
invalid:
    qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid bot weapon checkpoint");
    return false;
}
bool qa_bot_weapon_selector_create(qa_bot_weapons *config, qa_bot_weights *weights,
                                   qa_bot_weapon_selector **out, qa_error *e) {
    if (config == NULL || weights == NULL || out == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing bot weapon selector resource/output");
        return false;
    }
    qa_bot_weapon_selector *s = calloc(1, sizeof(*s));
    if (s == NULL) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating bot weapon selector");
        return false;
    }
    s->config = config;
    s->weights = weights;
    qa_bot_weapons_retain(config);
    qa_bot_weights_retain(weights);
    int32_t capacity=0;
    if(!bot_weapons_source_capacity(config,&capacity,e) || capacity<0 ||
       (uint64_t)(uint32_t)capacity*sizeof(*s->indices)>SIZE_MAX) {
        qa_bot_weapon_selector_destroy(s);
        if(capacity<0 || (uint64_t)(uint32_t)capacity*sizeof(*s->indices)>SIZE_MAX)
            qa_error_set(e,QA_ERROR_ARGUMENT,0,"Weapon mapping exceeds its source capacity domain");
        return false;
    }
    s->index_count=(uint32_t)capacity;
    s->indices = malloc((s->index_count?s->index_count:1)*sizeof(*s->indices));
    if (s->indices == NULL) {
        qa_bot_weapon_selector_destroy(s);
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating bot weapon weight mapping");
        return false;
    }
    if (!qa_bot_weight_workspace_create(&s->workspace, e)) {
        qa_bot_weapon_selector_destroy(s);
        return false;
    }
    for (uint32_t i = 0; i < s->index_count; ++i) {
        char name[81];
        if(!bot_weapons_source_name(config,i,name,e) ||
           !qa_bot_weights_find_value(weights,name,&s->indices[i],e)) {
            qa_bot_weapon_selector_destroy(s);return false;
        }
    }
    *out = s;
    return true;
}
void qa_bot_weapon_selector_destroy(qa_bot_weapon_selector *s) {
    if (s == NULL)
        return;
    qa_bot_weight_workspace_destroy(s->workspace);
    free(s->indices);
    qa_bot_weapons_release(s->config);
    qa_bot_weights_release(s->weights);
    free(s);
}
bool qa_bot_weapon_weight(qa_bot_weapon_selector *s, uint32_t weapon, const int32_t *inventory,
                          size_t count, float *out, bool *found, qa_error *e) {
    qa_bot_inventory_view view = {.data = inventory, .count = count};
    return qa_bot_weapon_weight_view(s, weapon, &view, out, found, e);
}
bool qa_bot_weapon_weight_view(qa_bot_weapon_selector *s, uint32_t weapon,
                               const qa_bot_inventory_view *inventory, float *out, bool *found,
                               qa_error *e) {
    if (s == NULL || out == NULL || found == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, weapon, "Invalid bot weapon evaluation request");
        return false;
    }
    bool member;
    if(!bot_weapons_source_valid(s->config,weapon,&member,e)) return false;
    if(!member || weapon>=s->index_count) {*found=false;return true;}
    *found = s->indices[weapon] >= 0;
    if (!*found)
        return true;
    return qa_bot_weights_evaluate_view(s->weights, (uint32_t)s->indices[weapon], inventory, NULL,
                                        s->workspace, out, e);
}
bool qa_bot_weapon_choose(qa_bot_weapon_selector *s, const int32_t *inventory, size_t count,
                          uint32_t *out, qa_error *e) {
    qa_bot_inventory_view view = {.data = inventory, .count = count};
    return qa_bot_weapon_choose_view(s, &view, out, e);
}
bool qa_bot_weapon_choose_view(qa_bot_weapon_selector *s, const qa_bot_inventory_view *inventory,
                               uint32_t *out, qa_error *e) {
    if (s == NULL || out == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing bot weapon choice selector/output");
        return false;
    }
    float best = 0;
    uint32_t choice = 0;
    for (uint32_t i = 0;; ++i) {
        int32_t capacity;bool member;
        if(!bot_weapons_source_capacity(s->config,&capacity,e)) return false;
        if(capacity<=0 || i>=(uint32_t)capacity) break;
        if(!bot_weapons_source_valid(s->config,i,&member,e)) return false;
        if (!member || i>=s->index_count || s->indices[i] < 0)
            continue;
        float value;
        if (!qa_bot_weights_evaluate_view(s->weights, (uint32_t)s->indices[i], inventory, NULL,
                                          s->workspace, &value, e))
            return false;
        if (value > best) {
            best = value;
            choice = i;
        }
    }
    *out = choice;
    return true;
}
