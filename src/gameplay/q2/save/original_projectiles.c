#include "original_projectiles.h"
#include "original_edicts.h"
#include "qa/text.h"

enum { FLIGHT, OPENING, ACTIVE, WARNING, CONSUMING, FINISHED };

static bool unsupported(q2_original_record_io *io, size_t field, const char *message)
{
    qa_error_set(io->error, QA_ERROR_UNSUPPORTED, field, "%s", message);
    return false;
}

static bool scalar(q2_original_record_io *io, const char *name,
    q2_original_field_kind kind, uint16_t offset, void *value)
{
    return q2_original_scalar(io, name, kind, offset, offset, offset, value);
}

static bool callback(qa_q2_game *g, q2_original_record_io *io,
    const char *field, uint16_t offset, const char *name)
{
    if (!io->reading) return q2_original_function(g, io, field, offset, name);
    bool matches;
    if (!q2_original_function_matches(g, io, field, offset, name, &matches)) return false;
    return matches || unsupported(io, offset,
        "Original Q2 projectile has a different Source callback");
}

static bool matches(qa_q2_game *g, q2_original_record_io *io,
    const char *field, uint16_t offset, const char *name, bool *out)
{
    return q2_original_function_matches(g, io, field, offset, name, out);
}

static const char *classname(qa_q2_game *g, q2_actor *a)
{
    const qa_actor_record *record = qa_actors_get(qa_session_actors(g->services.session), a->id);
    return qa_strings_cstr(qa_session_strings(g->services.session),
        a->projectile.classname ? a->projectile.classname : record ? record->definition : 0);
}

/* The source callbacks identify edicts with no authored classname as well. */
static bool identify(qa_q2_game *g, q2_original_record_io *io,
    q2_actor *a, q2_projectile_kind *kind)
{
    const char *name = classname(g, a);
    static const struct { const char *name; q2_projectile_kind kind; } names[] = {
        {"bolt", Q2_BOLT}, {"rocket", Q2_ROCKET}, {"grenade", Q2_GRENADE},
        {"hgrenade", Q2_GRENADE}, {"hand_grenade", Q2_GRENADE},
        {"bfg blast", Q2_BFG_BALL}, {"ion", Q2_ION}, {"plasma", Q2_PLASMA},
        {"flechette", Q2_FLECHETTE}, {"tracker", Q2_TRACKER},
        {"pain daemon", Q2_TRACKER_DAEMON}, {"prox", Q2_PROX}, {"prox_mine", Q2_PROX},
        {"prox_field", Q2_PROX_FIELD}, {"tesla", Q2_TESLA}, {"tesla_mine", Q2_TESLA},
        {"tesla trigger", Q2_TESLA_FIELD}, {"bad_area", Q2_BAD_AREA},
        {"htrap", Q2_TRAP}, {"food_cube_trap", Q2_TRAP}, {"nuke", Q2_NUKE},
        {"gib", Q2_GIB}, {"widowlegs", Q2_GIB}, {"debris", Q2_DEBRIS}, {"spawngro", Q2_SPAWN_GROWTH},
        {"spawngro_beam", Q2_RERELEASE_SPAWN_BEAM},
        {"loogie", Q2_LOOGIE}
    };
    *kind = Q2_PROJECTILE_NONE;
    for (size_t i = 0; name && i < sizeof(names) / sizeof(names[0]); ++i)
        if (!strcmp(name, names[i].name)) { *kind = names[i].kind; break; }
    if (*kind == Q2_SPAWN_GROWTH && io->edition == QA_Q2_RERELEASE)
        *kind = Q2_RERELEASE_SPAWN_GROWTH;
    bool found;
    if (!matches(g, io, "think", 436, "heat_think", &found)) return false;
    if (found) { *kind = Q2_HEAT_ROCKET; return true; }
    if (!matches(g, io, "think", 436, "Trap_Gib_Think", &found)) return false;
    if (found) { *kind = Q2_TRAP_ORBIT_GIB; return true; }
    if (!matches(g, io, "think", 436, "bfg_laser_update", &found)) return false;
    if (found) { *kind = Q2_BFG_LASER; return true; }
    if (!matches(g, io, "think", 436, "widowlegs_think", &found)) return false;
    if (found) { *kind = Q2_GIB; return true; }
    if (!matches(g, io, "touch", 444, "widow_gib_touch", &found)) return false;
    if (found) { *kind = Q2_GIB; return true; }
    if (*kind == Q2_PROJECTILE_NONE) {
        bool debris, gib;
        if (!matches(g, io, "die", 456, "debris_die", &debris) ||
            !matches(g, io, "die", 456, "gib_die", &gib)) return false;
        if (debris || gib) {
            uint64_t effects = 0;
            if (!scalar(io, "s.effects", Q2_ORIGINAL_U64, 64, &effects)) return false;
            /* Retail folds the two free-on-damage functions to one address. */
            *kind = gib && (!debris || (effects & 2u)) ? Q2_GIB : Q2_DEBRIS;
            return true;
        }
    }
    if (!matches(g, io, "touch", 444, "blaster2_touch", &found)) return false;
    if (found) { *kind = Q2_GREEN_BOLT; return true; }
    static const struct { const char *field; uint16_t offset; const char *name;
        q2_projectile_kind kind; } callbacks[] = {
        {"touch", 444, "ionripper_touch", Q2_ION},
        {"touch", 444, "plasma_touch", Q2_PLASMA},
        {"touch", 444, "flechette_touch", Q2_FLECHETTE},
        {"touch", 444, "loogie_touch", Q2_LOOGIE},
        {"think", 436, "spawngrow_think", Q2_SPAWN_GROWTH},
        {"think", 436, "SpawnGro_laser_think", Q2_RERELEASE_SPAWN_BEAM}
    };
    for (size_t i = 0; i < sizeof(callbacks) / sizeof(callbacks[0]); ++i) {
        if (!matches(g, io, callbacks[i].field, callbacks[i].offset, callbacks[i].name, &found))
            return false;
        if (found) {
            *kind = callbacks[i].kind == Q2_SPAWN_GROWTH && io->edition == QA_Q2_RERELEASE ?
                Q2_RERELEASE_SPAWN_GROWTH : callbacks[i].kind;
            return true;
        }
    }
    if (*kind == Q2_PROJECTILE_NONE && name && !strcmp(name, "noclass")) {
        bool free_think, no_touch, no_die;
        uint32_t flags = 0;
        uint64_t effects = 0;
        int32_t dead = 0;
        if (!matches(g, io, "think", 436, "G_FreeEdict", &free_think) ||
            !matches(g, io, "touch", 444, NULL, &no_touch) ||
            !matches(g, io, "die", 456, NULL, &no_die) ||
            !scalar(io, "svflags", Q2_ORIGINAL_U32, 184, &flags) ||
            !scalar(io, "s.effects", Q2_ORIGINAL_U64, 64, &effects) ||
            !scalar(io, "deadflag", Q2_ORIGINAL_I32, 492, &dead)) return false;
        if (free_think && no_touch && no_die && (flags & 4u) && (effects & 2u) && dead == 2)
            *kind = Q2_TRAP_GIB;
    }
    return true;
}

static bool read_phase(qa_q2_game *g, q2_original_record_io *io, q2_projectile *p)
{
    bool first, second, third;
    switch (p->kind) {
    case Q2_GRENADE:
        if (!matches(g, io, "think", 436, "Grenade4_Think", &first)) return false;
        p->armed = first;
        break;
    case Q2_BFG_BALL:
        if (!matches(g, io, "think", 436, "bfg_explode", &first) ||
            !matches(g, io, "think", 436, "G_FreeEdict", &second)) return false;
        p->armed = first || second;
        if (second) p->phase = FINISHED;
        break;
    case Q2_TRACKER:
        if (!matches(g, io, "think", 436, "tracker_fly", &first)) return false;
        p->armed = first;
        break;
    case Q2_PROX:
        if (!matches(g, io, "think", 436, "prox_open", &first) ||
            !matches(g, io, "think", 436, "prox_seek", &second) ||
            !matches(g, io, "touch", 444, "prox_land", &third)) return false;
        p->phase = first ? OPENING : second ? ACTIVE : third ? FLIGHT : WARNING;
        p->armed = p->phase == ACTIVE || p->phase == WARNING;
        break;
    case Q2_TESLA:
        if (!matches(g, io, "think", 436, "tesla_activate", &first) ||
            !matches(g, io, "think", 436, "tesla_think_active", &second)) return false;
        p->phase = first ? OPENING : second ? ACTIVE : FLIGHT;
        break;
    case Q2_TRAP:
        if (!matches(g, io, "think", 436, "G_FreeEdict", &first)) return false;
        p->phase = first ? FINISHED : p->frame > 4 ? CONSUMING : FLIGHT;
        p->armed = p->frame >= 4;
        break;
    case Q2_GIB:
        if (!matches(g, io, "think", 436, "gib_think", &first) ||
            !matches(g, io, "touch", 444, NULL, &second) ||
            !matches(g, io, "think", 436, "widowlegs_think", &third)) return false;
        p->phase = first ? 1 : 0;
        p->armed = second;
        if (third) {
            p->gib_flags = Q2_GIB_WIDOW_LEGS;
            p->armed = false;
            if (!scalar(io, "count", Q2_ORIGINAL_I32, 532, &p->phase)) return false;
        } else {
            if (!matches(g, io, "touch", 444, "widow_gib_touch", &third)) return false;
            if (third) p->gib_flags = Q2_GIB_WIDOW | Q2_GIB_WIDOW_SIZED;
        }
        break;
    case Q2_NUKE:
        if (!matches(g, io, "think", 436, "Nuke_Quake", &first)) return false;
        p->phase = first ? 1 : 0;
        break;
    default: break;
    }
    return true;
}

static bool callbacks(qa_q2_game *g, q2_original_record_io *io, q2_actor *a)
{
    q2_projectile *p = &a->projectile;
    bool rr = io->edition == QA_Q2_RERELEASE;
    const char *think = "G_FreeEdict", *touch = NULL, *die = NULL;
    switch (p->kind) {
    case Q2_BOLT: case Q2_BLUE_BOLT: touch = "blaster_touch"; break;
    case Q2_GREEN_BOLT: touch = "blaster2_touch"; break;
    case Q2_ROCKET: touch = "rocket_touch"; break;
    case Q2_HEAT_ROCKET: think = "heat_think"; touch = "rocket_touch"; break;
    case Q2_GRENADE:
        think = rr && p->armed ? "Grenade4_Think" : "Grenade_Explode";
        touch = "Grenade_Touch"; break;
    case Q2_BFG_BALL:
        think = p->phase == FINISHED || (p->armed && p->frame >= 5) ? "G_FreeEdict" :
            p->armed ? "bfg_explode" : "bfg_think";
        touch = p->armed ? NULL : "bfg_touch"; break;
    case Q2_BFG_LASER: think = "bfg_laser_update"; break;
    case Q2_ION: think = "ionripper_sparks"; touch = "ionripper_touch"; break;
    case Q2_PLASMA: touch = "plasma_touch"; break;
    case Q2_FLECHETTE: touch = "flechette_touch"; break;
    case Q2_TRACKER:
        think = p->armed || qa_actor_reference_present(p->enemy) ? "tracker_fly" : "G_FreeEdict";
        touch = "tracker_touch"; break;
    case Q2_TRACKER_DAEMON: think = "tracker_pain_daemon_think"; break;
    case Q2_PROX:
        think = p->phase == OPENING ? "prox_open" : p->phase == ACTIVE ? "prox_seek" :
            rr && p->phase == FLIGHT ? "Prox_Think" : "Prox_Explode";
        touch = p->phase == FLIGHT ? "prox_land" : NULL;
        die = p->phase == FLIGHT ? NULL : "prox_die"; break;
    case Q2_PROX_FIELD: {
        think = NULL;
        touch = p->armed ? "Prox_Field_Touch" : NULL;
        if (io->reading) {
            bool enabled;
            if (!matches(g, io, "touch", 444, "Prox_Field_Touch", &enabled)) return false;
            touch = enabled ? "Prox_Field_Touch" : NULL;
            p->armed = enabled;
        }
        break;
    }
    case Q2_TESLA:
        think = p->phase == ACTIVE ? "tesla_think_active" :
            p->phase == OPENING ? "tesla_activate" : "tesla_think";
        touch = "tesla_lava"; die = "tesla_die"; break;
    case Q2_TESLA_FIELD: think = NULL; touch = "tesla_zap"; break;
    case Q2_BAD_AREA: think = p->expire_ns ? "G_FreeEdict" : NULL; touch = "badarea_touch"; break;
    case Q2_TRAP:
        think = p->phase == FINISHED ? "G_FreeEdict" : "Trap_Think";
        die = rr && p->frame < 5 ? "trap_die" : NULL; break;
    case Q2_TRAP_ORBIT_GIB:
        think = "Trap_Gib_Think"; die = "gib_die";
        touch = p->gib_flags & Q2_GIB_UPRIGHT ? "gib_touch" : NULL; break;
    case Q2_TRAP_GIB: break;
    case Q2_GIB:
        if (p->gib_flags & Q2_GIB_WIDOW_LEGS) {
            think = "widowlegs_think";
            break;
        }
        think = !rr && p->phase == 1 ? "gib_think" : "G_FreeEdict";
        touch = p->gib_flags & Q2_GIB_WIDOW_SIZED ? (!p->armed ? "widow_gib_touch" : NULL) :
            rr ? (p->gib_flags & Q2_GIB_UPRIGHT ? "gib_touch" : NULL) :
            !p->armed && !(p->gib_flags & Q2_GIB_METALLIC) ? "gib_touch" : NULL;
        die = "gib_die"; break;
    case Q2_DEBRIS: die = rr ? "gib_die" : "debris_die"; break;
    case Q2_NUKE:
        think = p->phase ? "Nuke_Quake" : "Nuke_Think";
        touch = "nuke_bounce"; die = "nuke_die"; break;
    case Q2_SPAWN_GROWTH: think = "spawngrow_think"; break;
    case Q2_RERELEASE_SPAWN_GROWTH: think = "spawngrow_think"; break;
    case Q2_RERELEASE_SPAWN_BEAM: think = "SpawnGro_laser_think"; break;
    case Q2_LOOGIE: touch = "loogie_touch"; break;
    default: return unsupported(io, 436, "Q2 projectile has no original Source continuation");
    }
    return callback(g, io, "prethink", 432, NULL) && callback(g, io, "think", 436, think) &&
        callback(g, io, "blocked", 440, NULL) && callback(g, io, "touch", 444, touch) &&
        callback(g, io, "use", 448, NULL) && callback(g, io, "pain", 452, NULL) &&
        callback(g, io, "die", 456, die);
}

static bool references(qa_q2_game *g, q2_original_record_io *io, q2_actor *a)
{
    q2_projectile *p = &a->projectile;
    bool mine = p->kind == Q2_PROX || p->kind == Q2_TESLA || p->kind == Q2_NUKE ||
        (p->kind == Q2_TRAP && io->edition == QA_Q2_RERELEASE);
    qa_actor_reference owner = p->owner;
    if (!io->reading) {
        if (mine) {
            qa_actor_collision collision;
            owner = qa_world_get_collision(g->services.world, a->id, &collision, NULL) ?
                collision.owner : (qa_actor_reference){0};
        } else if ((p->kind == Q2_GIB && !(p->gib_flags & Q2_GIB_WIDOW_SIZED)) ||
            p->kind == Q2_DEBRIS || p->kind == Q2_TRAP_GIB)
            owner = (qa_actor_reference){0};
    }
    if (!q2_original_source_reference(g, io, "owner", 256, &owner) ||
        !q2_original_source_reference(g, io, "enemy", 540, &p->enemy)) return false;
    if (mine) {
        if (!q2_original_source_reference(g, io, "teammaster", 564, &p->owner)) return false;
    } else if (p->kind == Q2_PROX_FIELD) {
        qa_actor_reference master = owner;
        if (!q2_original_source_reference(g, io, "teammaster", 564, &master)) return false;
        if (io->reading && !qa_actor_reference_equal(master, owner))
            return unsupported(io, 564, "Original Q2 prox field has distinct parent pointers");
    } else if (p->kind == Q2_BFG_BALL) {
        uint32_t slot;
        if (!qa_q2_wire_entity_number(g, a->id, &slot, io->error)) return false;
        qa_actor_reference master = qa_actor_reference_source(g->options.owner, slot);
        if (!q2_original_source_reference(g, io, "teammaster", 564, &master)) return false;
        if (io->reading && (master.kind != QA_ACTOR_REFERENCE_SOURCE ||
            master.value.source.owner != g->options.owner || master.value.source.slot != slot))
            return unsupported(io, 564, "Original Q2 BFG has a distinct team master");
    }
    if (p->kind == Q2_PROX || p->kind == Q2_TESLA || p->kind == Q2_TESLA_FIELD ||
        p->kind == Q2_BAD_AREA)
        if (!q2_original_source_reference(g, io, "teamchain", 560, &p->child)) return false;
    if (p->kind == Q2_RERELEASE_SPAWN_GROWTH &&
        !q2_original_source_reference(g, io, "target_ent", UINT16_MAX, &p->child)) return false;
    if (io->reading) {
        if (!mine) p->owner = owner;
        p->attack.attacker = qa_actor_reference_resolve(qa_session_actors(g->services.session), p->owner);
        p->attack.inflictor = p->attack.projectile = a->id;
        qa_actor_collision collision;
        if (qa_world_get_collision(g->services.world, a->id, &collision, NULL)) {
            collision.owner = owner;
            if (!qa_world_set_collision(g->services.world, a->id, &collision, io->error)) return false;
        }
    }
    return true;
}

static bool deadlines(q2_original_record_io *io, q2_projectile *p)
{
    bool rr = io->edition == QA_Q2_RERELEASE;
    uint64_t next = p->next_ns;
    bool expiry_think = p->kind == Q2_BOLT || p->kind == Q2_BLUE_BOLT || p->kind == Q2_GREEN_BOLT ||
        p->kind == Q2_ROCKET || p->kind == Q2_ION || p->kind == Q2_PLASMA ||
        p->kind == Q2_FLECHETTE || p->kind == Q2_LOOGIE || p->kind == Q2_DEBRIS ||
        p->kind == Q2_TRAP_GIB || p->kind == Q2_BAD_AREA ||
        (p->kind == Q2_GRENADE && !(rr && p->armed)) ||
        (p->kind == Q2_TRACKER && !p->armed && !qa_actor_reference_present(p->enemy)) ||
        (p->kind == Q2_GIB && p->phase != 1 && !(p->gib_flags & Q2_GIB_WIDOW_LEGS)) ||
        (p->kind == Q2_TRAP && p->phase == FINISHED) ||
        (p->kind == Q2_PROX && !rr && p->phase == FLIGHT);
    if (!io->reading && expiry_think) next = p->expire_ns;
    if (!scalar(io, "nextthink", Q2_ORIGINAL_TIME, 428, &next)) return false;
    if (io->reading) {
        p->next_ns = next;
        p->expire_ns = expiry_think ? next : UINT64_MAX;
    }
    if ((p->kind == Q2_GRENADE && rr && p->armed) || p->kind == Q2_TRAP || p->kind == Q2_BFG_LASER ||
        (p->kind == Q2_PROX && rr && p->phase == FLIGHT)) {
        uint64_t expires = p->expire_ns;
        if (p->kind == Q2_TRAP && p->phase == FINISHED) expires = 0;
        if (!scalar(io, "timestamp", Q2_ORIGINAL_TIME, 288, &expires)) return false;
        if (io->reading && p->phase != FINISHED) p->expire_ns = expires;
    }
    if (p->kind == Q2_PROX && (p->phase == ACTIVE || p->phase == WARNING)) {
        uint64_t expires = p->phase == WARNING && !p->armed ? 0 : p->expire_ns;
        if (!q2_original_seconds(io, "wait", 592, &expires)) return false;
        if (io->reading) {
            p->expire_ns = expires;
            p->armed = expires != 0;
        }
    }
    if (p->kind == Q2_TESLA) {
        uint64_t launched_until = q2_deadline(p->born_ns, 30 * Q2_NS), active_until = 0;
        if (!io->reading && p->phase == ACTIVE) active_until = p->expire_ns;
        if (!q2_original_seconds(io, "wait", 592, &launched_until) ||
            !scalar(io, "air_finished", Q2_ORIGINAL_TIME, 404, &active_until)) return false;
        if (io->reading) {
            p->born_ns = launched_until >= 30 * Q2_NS ? launched_until - 30 * Q2_NS : 0;
            p->expire_ns = p->phase == ACTIVE ? active_until : launched_until;
        }
    }
    if (p->kind == Q2_TRACKER_DAEMON) {
        if (!scalar(io, "timestamp", Q2_ORIGINAL_TIME, 288, &p->born_ns)) return false;
        if (io->reading) p->expire_ns = q2_deadline(p->born_ns, 500 * Q2_MS);
    }
    if (p->kind == Q2_NUKE) {
        if (p->phase) {
            if (!scalar(io, "timestamp", Q2_ORIGINAL_TIME, 288, &p->expire_ns) ||
                !scalar(io, "last_move_time", Q2_ORIGINAL_TIME, 476, &p->effect_ns)) return false;
        } else if (!q2_original_seconds(io, "wait", 592, &p->expire_ns) ||
            !scalar(io, "timestamp", Q2_ORIGINAL_TIME, 288, &p->effect_ns)) return false;
    }
    if (p->kind == Q2_SPAWN_GROWTH && !q2_original_seconds(io, "wait", 592, &p->effect_ns)) return false;
    if (p->kind == Q2_RERELEASE_SPAWN_GROWTH) {
        if (!scalar(io, "teleport_time", Q2_ORIGINAL_TIME, UINT16_MAX, &p->born_ns) ||
            !scalar(io, "timestamp", Q2_ORIGINAL_TIME, 288, &p->expire_ns) ||
            !scalar(io, "wait", Q2_ORIGINAL_F32, 592, &p->delay)) return false;
        if (!isfinite(p->delay) || p->delay <= 0)
            return unsupported(io, 592, "Original Q2 rerelease growth has no positive lifespan");
    }
    if (p->kind == Q2_GIB && (p->gib_flags & Q2_GIB_WIDOW_LEGS)) {
        if (!scalar(io, "wait", Q2_ORIGINAL_F32, 592, &p->delay)) return false;
        if (!isfinite(p->delay) || p->delay < 0 ||
            (double)p->delay * (double)Q2_NS >= (double)UINT64_MAX)
            return unsupported(io, 592, "Original Q2 Widow legs wait exceeds the native clock");
        if (!io->reading && !scalar(io, "count", Q2_ORIGINAL_I32, 532, &p->phase)) return false;
    }
    return true;
}

static void attack(qa_q2_game *g, q2_actor *a, uint32_t flags)
{
    q2_projectile *p = &a->projectile;
    qa_q2_weapon weapon = QA_Q2_WEAPON_COUNT;
    p->kick = 0;
    switch (p->kind) {
    case Q2_BOLT: case Q2_BLUE_BOLT:
        p->direct_mod = flags & 1u ? 10 : 1; p->kick = 1;
        weapon = flags & 1u ? QA_Q2_HYPERBLASTER : QA_Q2_BLASTER; break;
    case Q2_GREEN_BOLT: p->direct_mod = 1; break;
    case Q2_ROCKET: case Q2_HEAT_ROCKET:
        p->direct_mod = 8; p->splash_mod = 9; weapon = QA_Q2_ROCKETLAUNCHER; break;
    case Q2_GRENADE:
        p->hand = (flags & 1u) != 0; p->held = (flags & 2u) != 0;
        p->direct_mod = p->hand ? 15 : 6; p->splash_mod = p->held ? 24 : p->hand ? 16 : 7;
        weapon = p->hand ? QA_Q2_GRENADES : QA_Q2_GRENADELAUNCHER; break;
    case Q2_BFG_BALL: p->direct_mod = 13; p->splash_mod = 14; weapon = QA_Q2_BFG; break;
    case Q2_ION: p->direct_mod = 34; weapon = QA_Q2_IONRIPPER; break;
    case Q2_PLASMA: p->direct_mod = p->splash_mod = 35; weapon = QA_Q2_PHALANX; break;
    case Q2_FLECHETTE: p->direct_mod = 44; weapon = QA_Q2_ETF_RIFLE; break;
    case Q2_TRACKER: case Q2_TRACKER_DAEMON: p->direct_mod = 51; weapon = QA_Q2_DISINTEGRATOR; break;
    case Q2_PROX: p->direct_mod = p->splash_mod = 46; weapon = QA_Q2_PROXLAUNCHER; break;
    case Q2_TESLA: p->direct_mod = p->splash_mod = 45; weapon = QA_Q2_TESLA; break;
    case Q2_TRAP: p->direct_mod = p->splash_mod = 39; weapon = QA_Q2_TRAP; p->held = (flags & 2u) != 0; break;
    case Q2_NUKE: p->direct_mod = p->splash_mod = 47; break;
    case Q2_LOOGIE: p->direct_mod = 38; p->kick = 1; break;
    default: break;
    }
    p->attack = (qa_attack){.attacker = qa_actor_reference_resolve(
        qa_session_actors(g->services.session), p->owner), .inflictor = a->id, .projectile = a->id,
        .weapon = weapon < QA_Q2_WEAPON_COUNT ? g->items[weapon] : 0,
        .weapon_provider = g->options.owner, .powerup_owner = g->options.owner,
        .powerup_applied = true,
        .cause = qa_q2_damage_cause(g->options.edition, g->options.product, p->direct_mod, 0)};
}

bool q2_original_projectile_record(qa_q2_game *g, q2_original_record_io *io,
    q2_actor *a, const qa_q2_save_level *engine, qa_error *error)
{
    (void)error;
    q2_projectile *p = &a->projectile;
    if (io->reading && !io->references_only) {
        q2_projectile_kind kind;
        if (!identify(g, io, a, &kind)) return false;
        if (kind == Q2_PROJECTILE_NONE) return true;
        *p = (q2_projectile){.kind = kind, .scale = 1, .alpha = 1};
        if (!q2_original_string(g, io, "classname", 280, &p->classname) ||
            !scalar(io, "s.frame", Q2_ORIGINAL_I32, 56, &p->frame)) return false;
        if (kind == Q2_BOLT) {
            qa_string_id model = 0;
            if (!q2_original_resource(g, io, engine, "s.modelindex", 40, 40, 40,
                32, &model)) return false;
            const char *path = qa_strings_cstr(qa_session_strings(g->services.session), model);
            if (path && !strcmp(path, "models/objects/blaser/tris.md2")) p->kind = Q2_BLUE_BOLT;
        }
        if (!read_phase(g, io, p)) return false;
    }
    if (p->kind == Q2_PROJECTILE_NONE) return true;
    if ((p->kind == Q2_BFG_LASER || p->kind == Q2_RERELEASE_SPAWN_GROWTH ||
        p->kind == Q2_RERELEASE_SPAWN_BEAM) && io->edition != QA_Q2_RERELEASE)
        return unsupported(io, 436, "Original Q2 projectile requires the rerelease");
    if (p->kind == Q2_SPAWN_GROWTH && io->edition == QA_Q2_RERELEASE)
        return unsupported(io, 436, "Original Q2 rerelease growth requires its native controller");
    if (io->edition == QA_Q2_CLASSIC &&
        ((io->product != QA_Q2_XATRIX && (p->kind == Q2_ION || p->kind == Q2_PLASMA ||
            p->kind == Q2_TRAP || p->kind == Q2_BLUE_BOLT || p->kind == Q2_HEAT_ROCKET)) ||
         (io->product != QA_Q2_ROGUE && (p->kind == Q2_FLECHETTE || p->kind == Q2_TRACKER ||
            p->kind == Q2_TRACKER_DAEMON || p->kind == Q2_PROX || p->kind == Q2_TESLA ||
            p->kind == Q2_PROX_FIELD || p->kind == Q2_TESLA_FIELD || p->kind == Q2_BAD_AREA ||
            p->kind == Q2_NUKE || p->kind == Q2_SPAWN_GROWTH || p->kind == Q2_GREEN_BOLT))))
        return unsupported(io, 436, "Q2 projectile does not belong to the selected original game");
    if (!references(g, io, a)) return false;
    if (p->kind == Q2_RERELEASE_SPAWN_BEAM && !qa_actor_reference_present(p->owner))
        return unsupported(io, 256, "Original Q2 rerelease growth beam has no Source owner");
    if (io->references_only) return true;
    if (io->reading && p->kind == Q2_GIB && !(p->gib_flags & Q2_GIB_WIDOW_LEGS) &&
        qa_actor_reference_present(p->owner) && a->physics.gravity_scale == .25f)
        p->gib_flags |= Q2_GIB_WIDOW | Q2_GIB_WIDOW_SIZED;
    if (io->edition == QA_Q2_CLASSIC && io->product != QA_Q2_ROGUE &&
        (p->gib_flags & (Q2_GIB_WIDOW | Q2_GIB_WIDOW_SIZED | Q2_GIB_WIDOW_LEGS)))
        return unsupported(io, 436, "Q2 widow gib does not belong to the selected original game");
    uint32_t flags = p->kind == Q2_GRENADE ? (p->hand ? 1u : 0) | (p->held ? 2u : 0) :
        p->kind == Q2_TRAP && io->edition == QA_Q2_CLASSIC ? 1u | (p->held ? 2u : 0) :
        (p->kind == Q2_BOLT || p->kind == Q2_BLUE_BOLT) && p->direct_mod == 10 ? 1u : 0;
    bool growth = p->kind == Q2_RERELEASE_SPAWN_GROWTH || p->kind == Q2_RERELEASE_SPAWN_BEAM;
    int32_t damage = io->reading || growth || p->kind == Q2_BFG_BALL ? 0 : qa_source_float_to_i32(p->damage);
    int32_t radius_damage = io->reading || growth ? 0 : qa_source_float_to_i32(
        p->kind == Q2_BFG_BALL ? p->damage : p->radius_damage);
    float radius = growth ? 0 : p->kind == Q2_FLECHETTE ? p->kick : p->kind == Q2_ION ? 100 : p->radius;
    if (!scalar(io, "spawnflags", Q2_ORIGINAL_U32, 284, &flags) ||
        !scalar(io, "dmg", Q2_ORIGINAL_I32, 516, &damage) ||
        !scalar(io, "radius_dmg", Q2_ORIGINAL_I32, 520, &radius_damage) ||
        !scalar(io, "dmg_radius", Q2_ORIGINAL_F32, 524, &radius) ||
        !scalar(io, p->kind == Q2_BFG_LASER || p->kind == Q2_RERELEASE_SPAWN_BEAM ?
            "s.old_origin" : "movedir", Q2_ORIGINAL_VECTOR,
            p->kind == Q2_BFG_LASER || p->kind == Q2_RERELEASE_SPAWN_BEAM ? 28 : 340, &p->movedir) ||
        !deadlines(io, p)) return false;
    if (io->reading) {
        p->damage = p->kind == Q2_BFG_BALL ? (float)radius_damage : (float)damage;
        p->radius_damage = (float)radius_damage;
        p->radius = radius;
        attack(g, a, flags);
        if (p->kind == Q2_FLECHETTE) p->kick = radius;
    }
    if ((p->kind == Q2_TRACKER || (p->kind == Q2_HEAT_ROCKET && io->edition == QA_Q2_RERELEASE) ||
        (p->kind == Q2_GRENADE && io->edition == QA_Q2_RERELEASE && p->armed) ||
        (p->kind == Q2_NUKE && p->phase)) &&
        !scalar(io, "speed", Q2_ORIGINAL_F32, 328, &p->speed)) return false;
    if (p->kind == Q2_HEAT_ROCKET && io->edition == QA_Q2_RERELEASE &&
        !scalar(io, "accel", Q2_ORIGINAL_F32, 332, &p->turn_fraction)) return false;
    if (p->kind == Q2_RERELEASE_SPAWN_GROWTH &&
        (!scalar(io, "accel", Q2_ORIGINAL_F32, 332, &p->radius) ||
         !scalar(io, "decel", Q2_ORIGINAL_F32, 336, &p->radius_damage))) return false;
    if (p->kind == Q2_RERELEASE_SPAWN_BEAM &&
        !scalar(io, "angle", Q2_ORIGINAL_F32, UINT16_MAX, &p->radius)) return false;
    if (p->kind == Q2_TRAP) {
        float wait = (float)p->wait;
        int32_t mass = io->reading ? 0 : qa_source_float_to_i32(
            p->captured_mass / (g->options.deathmatch ? 4 : 10));
        if (!scalar(io, "wait", Q2_ORIGINAL_F32, 592, &wait) ||
            !scalar(io, "delay", Q2_ORIGINAL_F32, 596, &p->delay) ||
            !scalar(io, "mass", Q2_ORIGINAL_I32, 400, &mass)) return false;
        if (io->reading) {
            p->wait = qa_source_float_to_i32(wait);
            p->captured_mass = (float)mass * (g->options.deathmatch ? 4 : 10);
        }
        if (io->edition == QA_Q2_RERELEASE &&
            !scalar(io, "accel", Q2_ORIGINAL_F32, 332, &p->captured_mass)) return false;
        if (io->reading && qa_actor_reference_present(p->enemy)) {
            q2_actor *enemy = q2_actor_get(g, qa_actor_reference_resolve(
                qa_session_actors(g->services.session), p->enemy), false, NULL);
            const char *name = enemy ? classname(g, enemy) : NULL;
            p->gekk = name && !strcmp(name, "monster_gekk");
        }
    }
    if (p->kind == Q2_GIB && (p->gib_flags & Q2_GIB_WIDOW_SIZED)) {
        qa_string_id sound = 0;
        if (!io->reading && (p->gib_flags & Q2_GIB_WIDOW_HIT_SOUND) &&
            !qa_strings_intern_cstr(qa_session_strings(g->services.session), "misc/fhit3.wav",
                &sound, io->error)) return false;
        if (!q2_original_resource(g, io, engine,
            io->edition == QA_Q2_RERELEASE ? "style" : "plat2flags",
            UINT16_MAX, UINT16_MAX, 992, 288, &sound)) return false;
        if (io->reading && sound) {
            const char *path = qa_strings_cstr(qa_session_strings(g->services.session), sound);
            if (!path || strcmp(path, "misc/fhit3.wav"))
                return unsupported(io, 992, "Original Q2 Widow impact sound differs from Source");
            p->gib_flags |= Q2_GIB_WIDOW_HIT_SOUND;
        }
    }
    if (io->reading && (p->kind == Q2_GIB || p->kind == Q2_DEBRIS || p->kind == Q2_TRAP_ORBIT_GIB) &&
        !(p->gib_flags & Q2_GIB_WIDOW_LEGS)) {
        p->gib_flags |= a->physics.motion == QA_PHYSICS_BOUNCE ? Q2_GIB_METALLIC : 0;
        if (io->edition == QA_Q2_RERELEASE) {
            uint64_t source_flags = 0;
            if (!scalar(io, "flags", Q2_ORIGINAL_U64, 264, &source_flags)) return false;
            p->gib_flags |= source_flags & UINT64_C(0x10000000) ? Q2_GIB_UPRIGHT : 0;
            uint64_t effects = 0;
            if (!scalar(io, "s.effects", Q2_ORIGINAL_U64, 64, &effects)) return false;
            if (p->kind == Q2_GIB && !(effects & (UINT64_C(2) | (UINT64_C(1) << 21))))
                p->gib_flags |= Q2_GIB_DEBRIS;
        }
    }
    return callbacks(g, io, a);
}
