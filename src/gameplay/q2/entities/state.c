#include "internal.h"
#include "qa/text.h"
#include <float.h>

static bool number(qa_q2_game *g, qa_string_id id, double *out) {
    qa_bytes text = qa_strings_text(qa_session_strings(g->services.session), id);
    return text.size && qa_parse_number(text, out, NULL) && isfinite(*out);
}

q2_actor *q2_ent(qa_q2_game *g, qa_actor_id id) {
    q2_actor *a = q2_actor_get(g, id, false, NULL);
    return a && a->entity ? a : NULL;
}
qa_string_id q2_actor_field(qa_q2_game *g, qa_actor_id id, const char *key) {
    qa_target_field value;
    qa_targets *targets = g->entity_runtime->services.targets;
    return targets && qa_targets_field(targets, id, key, &value) &&
                   value.kind == QA_TARGET_FIELD_TEXT
               ? value.value.text
               : 0;
}
float q2_actor_field_float(qa_q2_game *g, qa_actor_id id, const char *key, float fallback) {
    double value;
    return qa_targets_number(g->entity_runtime->services.targets, id, key, &value) &&
                   value >= -FLT_MAX && value <= FLT_MAX
               ? (float)value
               : fallback;
}
uint32_t q2_actor_field_flags(qa_q2_game *g, qa_actor_id id, const char *key) {
    double value;
    if (!qa_targets_number(g->entity_runtime->services.targets, id, key, &value) ||
        value < INT32_MIN || value > UINT32_MAX)
        return 0;
    value = trunc(value);
    return (uint32_t)(value < 0 ? value + 4294967296.0 : value);
}
bool q2_entities_init(qa_q2_game *g, qa_error *e) {
    g->entity_runtime = calloc(1, sizeof(*g->entity_runtime));
    if (!g->entity_runtime) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating Q2 entity state");
        return false;
    }
    return true;
}
void q2_entities_close(qa_q2_game *g) {
    if (g->entity_runtime)
        free(g->entity_runtime->wind);
    free(g->entity_runtime);
    g->entity_runtime = NULL;
}
void q2_entity_release_state(q2_actor *a) {
    if (!a->entity)
        return;
    q2_entity_unbind(a->entity_game, a);
    free(a->entity->fields);
    free(a->entity->mover);
    free(a->entity->turret);
    free(a->entity->q64);
    free(a->entity);
    a->entity = NULL;
    a->entity_game = NULL;
}
static bool authored(void *context, qa_actor_id id, qa_authored_target *fields) {
    return qa_q2_entity_authored(context, id, fields);
}
static bool use(void *context, qa_actor_id target, qa_actor_id other, qa_actor_id activator,
                qa_error *e) {
    return qa_q2_entity_use(context, target, other, activator, e);
}
static bool field(void *context, qa_actor_id id, const char *key, qa_target_field *value) {
    qa_q2_game *g = context;
    q2_actor *a = q2_ent(g, id);
    if (!a)
        return false;
    q2_entity_state *s = a->entity;
    if (!strcmp(key, "origin") || !strcmp(key, "angles") || !strcmp(key, "velocity") ||
        !strcmp(key, "mins") || !strcmp(key, "maxs")) {
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, id, &body, NULL))
            return false;
        qa_vec3 vector = !strcmp(key, "origin")     ? body.origin
                         : !strcmp(key, "angles")   ? body.angles
                         : !strcmp(key, "velocity") ? body.velocity
                         : !strcmp(key, "mins")     ? body.bounds.mins
                                                    : body.bounds.maxs;
        *value = (qa_target_field){.kind = QA_TARGET_FIELD_VECTOR, .value.vector = vector};
        return true;
    }
    if (!strcmp(key, "movedir")) {
        *value = (qa_target_field){.kind = QA_TARGET_FIELD_VECTOR, .value.vector = s->direction};
        return true;
    }
    if (!strcmp(key, "health")) {
        qa_combat_state combat;
        *value = (qa_target_field){
            .kind = QA_TARGET_FIELD_NUMBER,
            .value.number =
                qa_combat_read(g->services.combat, id, &combat, NULL) ? combat.health : s->health};
        return true;
    }
    const struct {
        const char *key;
        double value;
    } numbers[] = {{"spawnflags", a->item ? a->item->spawn.spawnflags : s->spawnflags},
                   {"speed", s->speed},
                   {"accel", s->accel},
                   {"decel", s->decel},
                   {"wait", s->wait},
                   {"delay", a->item ? a->item->spawn.delay : s->delay},
                   {"dmg", s->damage},
                   {"count", a->item ? a->item->spawn.count : s->count},
                   {"style", s->style},
                   {"volume", s->volume},
                   {"attenuation", s->attenuation}};
    for (size_t i = 0; i < sizeof(numbers) / sizeof(*numbers); i++)
        if (!strcmp(key, numbers[i].key)) {
            *value =
                (qa_target_field){.kind = QA_TARGET_FIELD_NUMBER, .value.number = numbers[i].value};
            return true;
        }
    const struct {
        const char *key;
        qa_string_id value;
    } texts[] = {{"classname", a->item && a->item->definition
                                   ? q2_item_classname(g, a->item->definition)
                                   : s->classname},
                 {"targetname", s->targetname},
                 {"target", a->item ? a->item->spawn.target : s->target},
                 {"killtarget", a->item ? a->item->spawn.killtarget : s->killtarget},
                 {"message", a->item ? a->item->spawn.message : s->message},
                 {"team", s->team},
                 {"map", s->map},
                 {"noise", s->noise}};
    for (size_t i = 0; i < sizeof(texts) / sizeof(*texts); i++)
        if (!strcmp(key, texts[i].key)) {
            *value = (qa_target_field){.kind = QA_TARGET_FIELD_TEXT, .value.text = texts[i].value};
            return true;
        }
    qa_string_id raw = q2_field_id(g, s, key);
    if (!raw)
        return false;
    *value = (qa_target_field){.kind = QA_TARGET_FIELD_TEXT, .value.text = raw};
    return true;
}
bool q2_entity_bind(qa_q2_game *g, q2_actor *a, qa_error *e) {
    qa_targets *targets = g->entity_runtime->services.targets;
    if (!targets) {
        qa_error_set(e, QA_ERROR_UNSUPPORTED, 0, "Q2 entities require shared target registry");
        return false;
    }
    if (!qa_targets_bind(targets,
                         &(qa_target_binding){.actor = a->id,
                                              .source = g->options.edition == QA_Q2_CLASSIC
                                                            ? QA_CLOCK_Q2_CLASSIC
                                                            : QA_CLOCK_Q2_RERELEASE,
                                              .context = g,
                                              .read = authored,
                                              .use = use,
                                              .field = field},
                         e))
        return false;
    a->entity_game = g;
    a->entity_targets = targets;
    return true;
}
void q2_entity_unbind(qa_q2_game *g, q2_actor *a) {
    if (!a || !a->entity_targets)
        return;
    qa_targets_unbind_context(a->entity_targets, a->id, g);
    a->entity_targets = NULL;
}
bool qa_q2_entities_configure(qa_q2_game *g, const qa_q2_entity_services *s, qa_error *e) {
    if (!g || !s || !s->visual || !s->event || !s->targets) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 entity services");
        return false;
    }
    if (g->entity_runtime->services.targets != s->targets)
        for (q2_actor *a = g->all_actors; a; a = a->all_next)
            if (a->entity_targets) {
                qa_error_set(e, QA_ERROR_ARGUMENT, 0,
                             "Q2 target registry cannot change while entities are bound");
                return false;
            }
    g->entity_runtime->services = *s;
    return true;
}
qa_string_id q2_field_id(qa_q2_game *g, const q2_entity_state *s, const char *key) {
    qa_strings *strings = qa_session_strings(g->services.session);
    for (size_t i = 0; i < s->field_count; i++)
        if (!strcmp(qa_strings_cstr(strings, s->fields[i].key), key))
            return s->fields[i].value;
    return 0;
}
const char *q2_field_text(qa_q2_game *g, const q2_entity_state *s, const char *key) {
    const char *text =
        qa_strings_cstr(qa_session_strings(g->services.session), q2_field_id(g, s, key));
    return text ? text : "";
}
float q2_field_float(qa_q2_game *g, const q2_entity_state *s, const char *key, float fallback) {
    double value;
    return number(g, q2_field_id(g, s, key), &value) && value >= -FLT_MAX && value <= FLT_MAX
               ? (float)value
               : fallback;
}
qa_vec3 q2_field_vec(qa_q2_game *g, const q2_entity_state *s, const char *key, qa_vec3 fallback) {
    const char *text = q2_field_text(g, s, key);
    float values[3];
    for (size_t i = 0; i < 3; i++) {
        text += strspn(text, " \t\r\n\v\f");
        size_t length = strcspn(text, " \t\r\n\v\f");
        double value;
        if (!qa_parse_number((qa_bytes){(const uint8_t *)text, length}, &value, NULL) ||
            !isfinite(value) || value < -FLT_MAX || value > FLT_MAX)
            return fallback;
        values[i] = (float)value;
        text += length;
    }
    text += strspn(text, " \t\r\n\v\f");
    return *text ? fallback : qa_v3(values[0], values[1], values[2]);
}
bool q2_publish_visual(qa_q2_game *g, qa_actor_id id, const qa_q2_visual *v, qa_error *e) {
    qa_q2_entity_services *s = &g->entity_runtime->services;
    if (!s->visual) {
        qa_error_set(e, QA_ERROR_UNSUPPORTED, 0, "Q2 presentation service is not installed");
        return false;
    }
    qa_q2_visual visual = *v;
    visual.effects |= qa_q2_actor_extra_effects(g, id);
    return s->visual(s->context, id, &visual, e);
}
bool q2_map_event(qa_q2_game *g, const qa_q2_map_event *event, qa_error *e) {
    qa_q2_entity_services *s = &g->entity_runtime->services;
    if (!s->event) {
        qa_error_set(e, QA_ERROR_UNSUPPORTED, 0, "Q2 map event service is not installed");
        return false;
    }
    return s->event(s->context, event, e);
}
bool q2_entity_show(qa_q2_game *g, q2_actor *a, qa_error *e) {
    return q2_publish_visual(g, a->id, &a->entity->visual, e);
}
bool qa_q2_entity_visual(qa_q2_game *g, qa_actor_id id, qa_q2_visual *out, qa_error *e) {
    q2_actor *a = g ? q2_actor_get(g, id, false, NULL) : NULL;
    if (!out || !g || !q2_actor_live(g, id)) {
        qa_error_set(e, QA_ERROR_NOT_FOUND, 0, "Q2 visual actor is missing");
        return false;
    }
    if (a && a->projectile.kind == Q2_PROJECTILE_NONE && a->client)
        *out = a->client->visual;
    else if (a && a->projectile.kind == Q2_PROJECTILE_NONE && a->item)
        *out = a->item->visual;
    else if (a && a->projectile.kind == Q2_PROJECTILE_NONE && a->entity && !a->monster)
        *out = a->entity->visual;
    else if (g->entity_runtime->services.read_visual)
        return g->entity_runtime->services.read_visual(g->entity_runtime->services.context, id, out,
                                                       e);
    else {
        qa_error_set(e, QA_ERROR_NOT_FOUND, 0, "Q2 visual is missing");
        return false;
    }
    out->effects |= qa_q2_actor_extra_effects(g, id);
    return true;
}
bool q2_entity_schedule(qa_q2_game *g, q2_actor *a, q2_entity_think think, float seconds) {
    a->entity->think = think;
    a->entity->due_ns = think == Q2ET_NONE ? 0 : q2_deadline(g->now_ns, q2_item_seconds(seconds));
    return true;
}
bool q2_entity_body(qa_q2_game *g, q2_actor *a, const qa_body_state *b, bool link, qa_error *e) {
    return qa_world_body_write(g->services.world, a->id, b, e) &&
           (!link || !q2_actor_live(g, a->id) || qa_world_link(g->services.world, a->id, NULL, e));
}
bool q2_entity_solid(qa_q2_game *g, q2_actor *a, qa_physics_solid solid, qa_error *e) {
    q2_entity_state *s = a->entity;
    a->physics_bound = true;
    a->physics.solid = solid;
    s->collision.family = QA_COLLISION_Q2;
    s->collision.shape = QA_SHAPE_BOX;
    s->collision.role = solid == QA_PHYSICS_TRIGGER ? QA_COLLISION_TRIGGER : QA_COLLISION_SOLID;
    s->collision.inline_model = s->has_inline;
    if (!s->collision.contents)
        s->collision.contents = 1;
    return qa_world_set_collision(g->services.world, a->id,
                                  solid == QA_PHYSICS_NOT_SOLID ? NULL : &s->collision, e) &&
           (!q2_actor_live(g, a->id) || qa_world_link(g->services.world, a->id, NULL, e));
}
bool q2_entity_native_spawn(qa_q2_game *g, const char *name, const qa_body_state *body,
                            q2_entity_kind kind, q2_actor **out, qa_error *e) {
    qa_string_id classname;
    qa_actor_id id;
    if (!qa_builtin_resource(&g->services, name, &classname, e) ||
        !qa_builtin_spawn_actor(
            &g->services,
            &(qa_builtin_spawn){.owner = g->options.owner, .definition = classname, .body = *body},
            &id, e))
        return false;
    q2_actor *a = q2_actor_get(g, id, true, e);
    if (!a) {
        qa_session_release(g->services.session, id, NULL);
        return false;
    }
    a->entity = calloc(1, sizeof(*a->entity));
    if (!a->entity) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating Q2 helper entity");
        qa_session_release(g->services.session, id, NULL);
        return false;
    }
    a->entity->classname = classname;
    a->entity_game = g;
    a->entity->kind = kind;
    a->entity->visual.scale = 1;
    a->entity->visual.alpha = 1;
    a->physics = qa_physics_properties_default(QA_COLLISION_Q2);
    a->physics.q2_rerelease = g->options.edition == QA_Q2_RERELEASE;
    if (!q2_entity_bind(g, a, e)) {
        qa_session_release(g->services.session, id, NULL);
        return false;
    }
    *out = a;
    return true;
}
bool q2_entity_sound(qa_q2_game *g, q2_actor *a, const char *path, int channel, float volume,
                     float attenuation, int loop, qa_error *e) {
    if (!path || !*path)
        return true;
    qa_string_id resource;
    qa_body_state body;
    if (!qa_builtin_resource(&g->services, path, &resource, e) ||
        !qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    return qa_builtin_emit(
        &g->services,
        &(qa_builtin_event){.kind = loop < 0 ? QA_BUILTIN_STOP_SOUND : QA_BUILTIN_SOUND,
                            .family = QA_GAME_Q2,
                            .provider = g->options.owner,
                            .actor = a->id,
                            .resource = resource,
                            .time_ns = g->now_ns,
                            .origin = body.origin,
                            .channel = channel,
                            .volume = volume,
                            .attenuation = attenuation,
                            .flags = loop > 0 ? 1u : 0u},
        e);
}
bool q2_entity_message(qa_q2_game *g, q2_actor *a, qa_actor_id recipient, const char *text,
                       qa_error *e) {
    qa_string_id message;
    if (!qa_builtin_resource(&g->services, text ? text : "", &message, e))
        return false;
    return qa_builtin_emit(&g->services,
                           &(qa_builtin_event){.kind = QA_BUILTIN_CENTERPRINT,
                                               .family = QA_GAME_Q2,
                                               .provider = g->options.owner,
                                               .actor = recipient,
                                               .other = a->id,
                                               .text = message,
                                               .time_ns = g->now_ns},
                           e);
}
bool q2_entity_targets(qa_q2_game *g, q2_actor *a, qa_actor_id activator, bool ignore_delay,
                       qa_error *e) {
    qa_authored_target fields;
    if (!qa_q2_entity_authored(g, a->id, &fields)) {
        qa_error_set(e, QA_ERROR_NOT_FOUND, 0, "Q2 target source is missing");
        return false;
    }
    if (!fields.target && !fields.killtarget && !fields.message)
        return true;
    qa_targets *targets = g->entity_runtime->services.targets;
    if (!targets) {
        qa_error_set(e, QA_ERROR_UNSUPPORTED, 0, "Q2 target graph service is not installed");
        return false;
    }
    if (!ignore_delay)
        return qa_targets_use(targets, a->id, activator, g->now_ns, e);
    return qa_targets_use_now(targets,
                              &(qa_target_use){.source = a->id,
                                               .activator = activator,
                                               .dialect = g->options.edition == QA_Q2_CLASSIC
                                                              ? QA_CLOCK_Q2_CLASSIC
                                                              : QA_CLOCK_Q2_RERELEASE,
                                               .fields = fields,
                                               .time_ns = g->now_ns},
                              e);
}
bool qa_q2_entity_authored(qa_q2_game *g, qa_actor_id id, qa_authored_target *out) {
    q2_actor *a = g ? q2_actor_get(g, id, false, NULL) : NULL;
    if (!a || !out || (!a->entity && !a->item))
        return false;
    *out = (qa_authored_target){0};
    if (a->entity) {
        const q2_entity_state *s = a->entity;
        *out = (qa_authored_target){.classname = s->classname,
                                    .targetname = s->targetname,
                                    .target = s->target,
                                    .killtarget = s->killtarget,
                                    .message = s->message,
                                    .delay_seconds = s->delay,
                                    .wait_seconds = s->wait};
    }
    if (a->item) {
        const qa_q2_item_spawn *s = &a->item->spawn;
        if (a->item->definition)
            out->classname = q2_item_classname(g, a->item->definition);
        out->target = s->target;
        out->killtarget = s->killtarget;
        out->message = s->message;
        out->delay_seconds = s->delay;
    }
    return true;
}
bool qa_q2_entity_use_targets(qa_q2_game *g, qa_actor_id id, qa_actor_id activator,
                              bool ignore_delay, qa_error *e) {
    q2_actor *a = g ? q2_actor_get(g, id, false, NULL) : NULL;
    if (!a) {
        qa_error_set(e, QA_ERROR_NOT_FOUND, 0, "Q2 target source is missing");
        return false;
    }
    return q2_entity_targets(g, a, activator, ignore_delay, e);
}
bool q2_entity_damage(qa_q2_game *g, q2_actor *a, qa_actor_id target, qa_actor_id credit,
                      float amount, float kick, int mod, uint32_t flags, qa_error *e) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, target, &body, e))
        return false;
    qa_attack attack = {.time_ns = g->now_ns,
                        .inflictor = a->id,
                        .attacker = credit,
                        .combat_provider = g->options.owner,
                        .cause =
                            qa_q2_damage_cause(g->options.edition, g->options.product, mod, flags)};
    return q2_damage(g, &attack, target, amount, kick, qa_v3(0, 0, 0), body.origin, qa_v3(0, 0, 0),
                     false, e);
}
bool q2_entity_radius(qa_q2_game *g, q2_actor *a, qa_actor_id credit, float amount, float radius,
                      int mod, qa_error *e) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    qa_builtin_radius r = {
        .attack = {.time_ns = g->now_ns,
                   .inflictor = a->id,
                   .attacker = credit,
                   .combat_provider = g->options.owner,
                   .cause = qa_q2_damage_cause(g->options.edition, g->options.product, mod, 1)},
        .origin = body.origin,
        .radius = radius,
        .damage = amount,
        .distance_scale = .5f,
        .self_scale = .5f,
        .knockback_scale = 1,
        .trace = {.family = QA_COLLISION_Q2, .contents_mask = 1},
        .check_visibility = true};
    r.context = g;
    r.prepare = q2_prepare_radius_damage;
    return q2_radius_damage(g, &r, NULL, e);
}
bool q2_map_find(qa_q2_game *g, const char *classname, qa_string_id target, size_t ordinal,
                 qa_actor_id *out) {
    qa_strings *strings = qa_session_strings(g->services.session);
    qa_targets *targets = g->entity_runtime->services.targets;
    if (!targets)
        return false;
    if (target != UINT32_MAX && target != 0) {
        qa_target_cursor cursor = {0};
        qa_actor_id id;
        while (qa_targets_next(targets, target, &cursor, &id)) {
            qa_authored_target fields;
            if (classname) {
                if (!qa_targets_read(targets, id, &fields))
                    continue;
                const char *name = qa_strings_cstr(strings, fields.classname);
                if (!name || strcmp(name, classname))
                    continue;
            }
            if (!ordinal--) {
                *out = id;
                return true;
            }
        }
        return false;
    }
    qa_target_cursor cursor = {0};
    qa_actor_id id;
    while (qa_targets_next_authored(targets, classname, &cursor, &id)) {
        qa_authored_target fields;
        if (!qa_targets_read(targets, id, &fields) ||
            (target != UINT32_MAX && fields.targetname != target))
            continue;
        if (!ordinal--) {
            *out = id;
            return true;
        }
    }
    return false;
}
bool q2_entity_pick(qa_q2_game *g, qa_string_id target, qa_actor_id *out) {
    qa_actor_id ids[8];
    size_t n = 0;
    if (!target)
        return false;
    while (n < 8 && q2_map_find(g, NULL, target, n, &ids[n]))
        n++;
    if (!n)
        return false;
    uint32_t index = g->options.edition == QA_Q2_RERELEASE ? q2_random_bounded(g, (uint32_t)n)
                                                           : (uint32_t)(q2_random(g) * (float)n);
    *out = ids[index < n ? index : n - 1];
    return true;
}
uint32_t q2_map_flags(qa_q2_game *g, qa_actor_id id) {
    return q2_actor_field_flags(g, id, "spawnflags");
}
bool qa_q2_entity_target(qa_q2_game *g, qa_actor_id id, qa_string_id *name, qa_string_id *target,
                         qa_string_id *kill, qa_error *e) {
    q2_actor *a = q2_ent(g, id);
    if (!a || !name || !target || !kill) {
        qa_error_set(e, QA_ERROR_NOT_FOUND, 0, "Q2 target actor is missing");
        return false;
    }
    *name = a->entity->targetname;
    *target = a->entity->target;
    *kill = a->entity->killtarget;
    return true;
}
bool q2_map_navigation(qa_q2_game *g, qa_actor_id id, qa_vec3 start, qa_vec3 goal, qa_vec3 *points,
                       size_t cap, size_t *count, bool *reachable, qa_error *e) {
    qa_q2_entity_services *s = &g->entity_runtime->services;
    if (!s->navigation) {
        *count = 0;
        *reachable = true;
        return true;
    }
    return s->navigation(s->context, id, start, goal, points, cap, count, reachable, e);
}
bool q2_map_searching(qa_q2_game *g, qa_actor_id id) {
    qa_q2_entity_services *s = &g->entity_runtime->services;
    return s->monsters_searching && s->monsters_searching(s->context, id);
}
bool q2_map_camera_player(qa_q2_game *g, qa_actor_id id, qa_vec3 origin, qa_vec3 angles,
                          bool entering, qa_error *e) {
    qa_q2_entity_services *services = &g->entity_runtime->services;
    if (!services->camera_player) {
        qa_error_set(e, QA_ERROR_UNSUPPORTED, 0, "Q2 camera requires selected character service");
        return false;
    }
    return services->camera_player(services->context, id, origin, angles, entering, e);
}
bool q2_map_in_phs(qa_q2_game *g, qa_vec3 from, qa_vec3 to) {
    qa_q2_entity_services *s = &g->entity_runtime->services;
    if (s->in_phs)
        return s->in_phs(s->context, from, to);
    qa_collision_geometry *geometry = qa_world_geometry(g->services.world);
    qa_collision_leaf a, b;
    bool visible = false;
    return qa_collision_point_leaf(geometry, from, &a, NULL) &&
           qa_collision_point_leaf(geometry, to, &b, NULL) &&
           qa_collision_cluster_visible(geometry, (int32_t)a.cluster, (int32_t)b.cluster, true,
                                        &visible, NULL) &&
           visible;
}
bool q2_map_transition(qa_q2_game *g, qa_actor_id source, qa_actor_id activator, qa_string_id map,
                       const qa_q2_landmark *landmark, bool end, qa_error *e) {
    qa_q2_entity_services *s = &g->entity_runtime->services;
    if (!s->transition) {
        qa_error_set(e, QA_ERROR_UNSUPPORTED, 0, "Q2 campaign transition service is not installed");
        return false;
    }
    return s->transition(s->context, source, activator, map, landmark, end, e);
}
bool q2_map_poi(qa_q2_game *g, qa_actor_id player, qa_vec3 *origin, qa_string_id *image,
                bool *present, qa_error *e) {
    (void)player;
    q2_entities *r = g->entity_runtime;
    qa_actor_id id = q2_actor_live(g, r->poi_dynamic) ? r->poi_dynamic : r->poi;
    *present = q2_actor_live(g, id);
    if (!*present)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, e))
        return false;
    *origin = body.origin;
    *image = r->poi_image;
    return true;
}
bool q2_entity_flags(qa_q2_game *g, bool write, bool cross, uint32_t *flags, qa_error *e) {
    qa_q2_entity_services *s = &g->entity_runtime->services;
    if (!s->server_flags) {
        qa_error_set(e, QA_ERROR_UNSUPPORTED, 0, "Q2 campaign flags service is not installed");
        return false;
    }
    return s->server_flags(s->context, write, cross, flags, e);
}
bool q2_entity_traits(qa_q2_game *g, qa_actor_id id, qa_builtin_actor_traits *out) {
    q2_actor *a = q2_ent(g, id);
    if (!a)
        return false;
    out->classname = a->entity->classname;
    out->max_health = a->entity->health;
    out->owner = a->entity->owner;
    out->damageable_target = a->entity->health > 0;
    return true;
}
