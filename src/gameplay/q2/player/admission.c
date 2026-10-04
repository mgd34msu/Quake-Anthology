#include "internal.h"
#include "../entities/internal.h"

static bool info_value(const char *info, const char *key, char *out, size_t capacity) {
    size_t wanted = strlen(key);
    bool found = false;
    out[0] = 0;
    const char *p = info;
    if (*p == '\\')
        p++;
    while (*p) {
        const char *k = p;
        while (*p && *p != '\\')
            p++;
        size_t n = (size_t)(p - k);
        if (!*p)
            break;
        p++;
        const char *v = p;
        while (*p && *p != '\\')
            p++;
        size_t length = (size_t)(p - v);
        if (n == wanted && !memcmp(k, key, n)) {
            if (length >= capacity)
                length = capacity - 1;
            memcpy(out, v, length);
            out[length] = 0;
            found = true;
        }
        if (*p)
            p++;
    }
    return found;
}
static bool info_remove(char *info, size_t capacity, const char *key, size_t *length) {
    size_t wanted = strlen(key), used = 0;
    if (*info && *info != '\\') {
        size_t bytes = strlen(info) + 1;
        if (bytes >= capacity)
            return false;
        memmove(info + 1, info, bytes);
        info[0] = '\\';
    }
    const char *read = info;
    if (*read == '\\')
        read++;
    while (*read) {
        const char *name = read;
        while (*read && *read != '\\')
            read++;
        size_t name_length = (size_t)(read - name);
        if (!*read)
            break;
        read++;
        while (*read && *read != '\\')
            read++;
        const char *next = *read ? read + 1 : read;
        if (name_length != wanted || memcmp(name, key, name_length)) {
            size_t bytes = (size_t)(read - name);
            info[used++] = '\\';
            memmove(info + used, name, bytes);
            used += bytes;
        }
        read = next;
    }
    info[used] = 0;
    *length = used;
    return true;
}
static int info_integer(const char *info, const char *key) {
    char value[64];
    info_value(info, key, value, sizeof(value));
    char *end;
    long n = strtol(value, &end, 10);
    return end == value ? 0 : n > INT_MAX ? INT_MAX : n < INT_MIN ? INT_MIN : (int)n;
}
static bool wants_spectator(qa_q2_game *g, const char *info) {
    char value[128];
    info_value(info, "spectator", value, sizeof(value));
    return g->options.deathmatch && *value && strcmp(value, "0");
}
static bool rerelease_inventory(qa_q2_game *g, q2_actor *a, qa_error *e) {
    if (!a->client->use_inventory)
        return true;
    if (!g->options.deathmatch) {
        const qa_q2_item_definition *compass = qa_q2_item_lookup(g, "item_compass");
        if (compass &&
            !qa_inventory_configure(
                g->services.inventory, a->id,
                &(qa_inventory_entry){compass->item, 1, 1, QA_COUNT_SOURCE_INT32}, NULL, NULL, e))
            return false;
    }
    if (!q2_actor_live(g, a->id) || !g->options.cooperative)
        return true;
    qa_builtin_snapshot_frame *players = q2_player_roster(g, e);
    if (!players)
        return false;
    bool okay = true;
    for (size_t i = 0; i < players->snapshot.count; i++) {
        qa_actor_id id = players->snapshot.ids[i];
        if (qa_actor_id_equal(a->id, id) || !q2_actor_live(g, id))
            continue;
        q2_actor *other = q2_actor_get(g, id, false, NULL);
        if (other && other->client) {
            if (!other->client->info.connected || other->client->info.spectator ||
                other->client->info.noclip)
                continue;
        } else {
            qa_builtin_actor_traits traits = {0};
            if (g->services.actor_traits &&
                g->services.actor_traits(g->services.context, id, &traits) && traits.spectator)
                continue;
            qa_q2_player_movement movement;
            qa_q2_player_services *services = &g->player_runtime->services;
            if (!services->movement(services->context, id, &movement, e)) {
                okay = false;
                break;
            }
            if (!q2_actor_live(g, a->id))
                break;
            if (!q2_actor_live(g, id) || movement.noclip)
                continue;
            other = q2_actor_get(g, id, false, NULL);
        }
        qa_inventory_entry *entries = NULL;
        size_t count = 0;
        uint32_t cubes = other && other->powers ? other->powers->power_cubes : 0;
        okay = q2_player_inventory_copy(g, id, &entries, &count, e);
        for (size_t entry = 0; okay && entry < count && q2_actor_live(g, a->id); entry++)
            okay = qa_inventory_configure(g->services.inventory, a->id, &entries[entry], NULL, NULL,
                                          e);
        free(entries);
        if (okay && q2_actor_live(g, a->id))
            a->powers->power_cubes = cubes;
        break;
    }
    qa_builtin_snapshot_release(players);
    return okay;
}
bool qa_q2_player_connect(qa_q2_game *g, const char *info, bool bot, qa_q2_connection_result *out,
                          qa_error *e) {
    if (!g || !info || !out) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 connection");
        return false;
    }
    q2_players *p = g->player_runtime;
    qa_q2_connection_result r = {0};
    bool spectator = wants_spectator(g, info);
    char value[256];
    const char *reason = NULL;
    bool rr = g->options.edition == QA_Q2_RERELEASE;
    info_value(info, "ip", value, sizeof(value));
    if (!rr && p->services.banned && p->services.banned(p->services.context, value))
        reason = "Banned.";
    const char *password = spectator ? p->rules.spectator_password : p->rules.password;
    info_value(info, spectator ? "spectator" : "password", value, sizeof(value));
    if (!reason && (spectator || !rr || !bot) && *password && strcmp(password, "none") &&
        strcmp(password, value))
        reason = spectator ? "Spectator password required or incorrect."
                           : "Password required or incorrect.";
    if (!reason && spectator) {
        size_t count = 0;
        for (size_t i = 0; i < g->capacity; i++) {
            q2_actor *a = g->actors[i];
            if (a && a->client && a->client->info.connected && a->client->requested_spectator)
                count++;
        }
        if (count >= p->rules.max_spectators)
            reason = "Server spectator limit is full.";
    }
    r.allowed = reason == NULL;
    snprintf(r.userinfo, sizeof(r.userinfo), "%.*s", rr ? 2047 : 511, info);
    if (reason) {
        snprintf(r.reason, sizeof(r.reason), "%s", reason);
        size_t n;
        if (!info_remove(r.userinfo, sizeof(r.userinfo), "rejmsg", &n)) {
            qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 rejection userinfo exceeds its buffer");
            return false;
        }
        snprintf(r.userinfo + n, sizeof(r.userinfo) - n, "\\rejmsg\\%s", reason);
    }
    *out = r;
    return true;
}
typedef struct player_userinfo_call {
    qa_q2_game *game;
    const char *source;
} player_userinfo_call;

static bool player_userinfo(void *context, qa_actor_id id, qa_error *e) {
    player_userinfo_call *call = context;
    qa_q2_game *g = call->game;
    const char *source = call->source;
    q2_actor *a = q2_client(g, id, e);
    if (!a || !source)
        return false;
    q2_client_state *s = a->client;
    bool rr = g->options.edition == QA_Q2_RERELEASE;
    if (!rr && (strchr(source, '"') || strchr(source, ';')))
        source = "\\name\\badinfo\\skin\\male/grunt";
    snprintf(s->userinfo, sizeof(s->userinfo), "%.*s", rr ? 2047 : 511, source);
    char name[32], gender[32];
    bool named = info_value(s->userinfo, "name", name, rr ? sizeof(name) : 16);
    if (!named && rr)
        snprintf(name, sizeof(name), "badinfo");
    bool skinned = info_value(s->userinfo, "skin", s->info.skin, sizeof(s->info.skin));
    if (rr && !skinned)
        snprintf(s->info.skin, sizeof(s->info.skin), "male/grunt");
    info_value(s->userinfo, "gender", gender, sizeof(gender));
    s->gender = tolower((unsigned char)gender[0]) == 'f'   ? 1
                : tolower((unsigned char)gender[0]) == 'm' ? 0
                                                           : 2;
    s->requested_spectator = wants_spectator(g, s->userinfo);
    int fov = info_integer(s->userinfo, "fov");
    s->fov = rr ? q2_clamp((float)fov, 1, 160)
             : g->options.deathmatch && (g->options.deathmatch_flags & 32768) ? 90
             : fov < 1 ? 90
                       : fminf((float)fov, 160);
    int hand = info_integer(s->userinfo, "hand");
    if (rr)
        hand = (int)q2_clamp((float)hand, 0, 2);
    s->hand = hand == 1 ? QA_Q2_LEFT_HAND : hand == 2 ? QA_Q2_CENTER_HAND : QA_Q2_RIGHT_HAND;
    if (rr) {
        char value[32];
        s->auto_switch = (int)q2_clamp((float)info_integer(s->userinfo, "autoswitch"), 0, 3);
        info_value(s->userinfo, "bobskip", value, sizeof(value));
        s->bob_skip = value[0] == '1';
        s->auto_shield = info_value(s->userinfo, "autoshield", value, sizeof(value))
                             ? info_integer(s->userinfo, "autoshield")
                             : -1;
        info_value(s->userinfo, "dogtag", s->dogtag, sizeof(s->dogtag));
    }
    snprintf(s->info.name, sizeof(s->info.name), "%s", name);
    if (!q2_player_emit(g,
                        &(qa_q2_player_event){.kind = QA_Q2_PLAYER_USERINFO,
                                              .actor = id,
                                              .slot = s->info.slot,
                                              .text = name,
                                              .skin = s->info.skin},
                        e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    if (rr) {
        if (!q2_player_emit(
                g,
                &(qa_q2_player_event){.kind = QA_Q2_PLAYER_DOGTAG, .actor = id, .text = s->dogtag},
                e))
            return false;
        if (!q2_actor_live(g, id))
            return true;
        if (!s->bot)
            snprintf(s->info.name, sizeof(s->info.name), "##P%u", s->info.slot);
    }
    return true;
}
bool qa_q2_player_userinfo(qa_q2_game *g, qa_actor_id id, const char *source, qa_error *e) {
    player_userinfo_call call = {.game = g, .source = source};
    return qa_q2_run_actor(g, id, player_userinfo, &call, e);
}
static bool userinfo_storage(void *context,qa_actor_id actor,qa_error *e) {
    player_userinfo_call *call=context;
    q2_actor *a=q2_client(call->game,actor,e);
    if(!a || !call->source) return false;
    size_t length=strlen(call->source);
    if(length>=sizeof(a->client->userinfo)) {
        qa_error_set(e,QA_ERROR_FORMAT,0,"Q2 stored userinfo exceeds its actual client storage");return false;
    }
    memcpy(a->client->userinfo,call->source,length+1);return true;
}
bool qa_q2_player_userinfo_storage(qa_q2_game *g,qa_actor_id actor,const char *text,qa_error *e) {
    if(!text) {qa_error_set(e,QA_ERROR_ARGUMENT,0,"Q2 stored userinfo is absent");return false;}
    player_userinfo_call call={.game=g,.source=text};
    return qa_q2_run_actor(g,actor,userinfo_storage,&call,e);
}
typedef struct player_admission_call {
    qa_q2_game *game;
    qa_q2_player_admission admission;
} player_admission_call;

static bool player_admit(void *context, qa_actor_id id, qa_error *e) {
    player_admission_call *call = context;
    qa_q2_game *g = call->game;
    const qa_q2_player_admission *admission = &call->admission;
    q2_actor *a = q2_actor_get(g, id, true, e);
    if (!a)
        return false;
    if (a->client) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 actor already has player state");
        return false;
    }
    if (admission->slot >= g->wire_clients ||
        !q2_wire_bind(g, a, admission->slot + 1, e)) {
        if (e && e->code == QA_OK)
            qa_error_set(e, QA_ERROR_ARGUMENT, admission->slot, "Q2 player exceeds its genuine source client table");
        return false;
    }
    for (size_t i = 0; i < g->capacity; i++) {
        q2_actor *other = g->actors[i];
        if (other && other->client && other->client->info.connected &&
            other->client->info.slot == admission->slot) {
            qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 client slot is occupied");
            return false;
        }
    }
    q2_client_state *s = calloc(1, sizeof(*s));
    if (!s) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating Q2 player");
        return false;
    }
    a->client = s;
    a->wire_movement = (qa_q2_wire_movement){.present = true,
        .state.kind = g->options.edition == QA_Q2_RERELEASE ? QA_MOVEMENT_Q2_RERELEASE : QA_MOVEMENT_Q2_CLASSIC,
        .frame = g->wire_frame, .time_ns = g->now_ns};
    s->info = (qa_q2_player_info){
        .slot = admission->slot, .seat = admission->seat, .connected = true, .view_height = 22};
    s->use_weapons = admission->use_q2_weapons;
    s->use_inventory = admission->use_q2_inventory;
    s->pending_start_items = admission->carry == NULL;
    s->bot = admission->bot;
    s->entered_ns = g->now_ns;
    s->air_ns = q2_deadline(g->now_ns, 12 * Q2_NS);
    s->drown_damage = 2;
    s->animation_end = 39;
    s->auto_shield = -1;
    s->visual = (qa_q2_visual){.old_frame = -1, .scale = 1, .alpha = 1, .visible = true};
    snprintf(s->social_id, sizeof(s->social_id), "%s",
             admission->social_id ? admission->social_id : "");
    if (g->options.cooperative && g->player_runtime->rules.coop_lives)
        s->info.lives = g->player_runtime->rules.coop_num_lives + 1;
    if (!qa_q2_player_userinfo(g, id, admission->userinfo, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    qa_combat_state combat;
    qa_error missing = {0};
    if (!qa_combat_read(g->services.combat, id, &combat, &missing)) {
        if (missing.code != QA_ERROR_NOT_FOUND) {
            if (e)
                *e = missing;
            return false;
        }
        combat = (qa_combat_state){.health = 100, .mass = 200, .can_take_damage = true};
        if (!qa_combat_create_actor(g->services.combat, id, &combat, e))
            return false;
    }
    if (!qa_inventory_has(g->services.inventory, id) &&
        !qa_inventory_create_actor(g->services.inventory, id, NULL, 0, e))
        return false;
    if (!q2_powers(g, id, e))
        return false;
    if (admission->initialize_inventory && s->use_inventory &&
        !qa_q2_items_admit_player(g, id, true, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    if (s->use_weapons && !qa_q2_weapon_bind(g, id, QA_Q2_BLASTER, e))
        return false;
    s->info.selected_item = s->use_inventory ? g->items[QA_Q2_BLASTER] : 0;
    if (admission->carry && !qa_q2_player_carry_restore(g, id, admission->carry, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    if (g->options.edition == QA_Q2_RERELEASE) {
        if (!q2_campaign_enter(g, e)) return false;
        if (s->auto_shield >= 0)
            s->auto_shield_enabled = true;
        s->spawned = true;
        if (!qa_q2_entities_player_reset(g, id, e))
            return false;
        if (!q2_actor_live(g, id))
            return true;
        if (!rerelease_inventory(g, a, e))
            return false;
        if (!q2_actor_live(g, id))
            return true;
    }
    if (!q2_player_inventory_copy(g, id, &s->spawn_inventory, &s->spawn_count, e))
        return false;
    if (!qa_q2_player_carry_capture(g, id, &s->coop, e))
        return false;
    s->has_coop = true;
    s->info.spectator = s->requested_spectator;
    s->spawned = true;
    return true;
}
bool qa_q2_player_admit(qa_q2_game *g, qa_actor_id id, const qa_q2_player_admission *admission,
                        qa_error *e) {
    if (!g || !admission || !admission->userinfo) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 player admission");
        return false;
    }
    player_admission_call call = {.game = g, .admission = *admission};
    return qa_q2_run_actor(g, id, player_admit, &call, e);
}
bool q2_player_collision(qa_q2_game *g, q2_actor *a, bool solid, qa_error *e) {
    qa_actor_collision collision = {.family = QA_COLLISION_Q2,
                                    .shape = QA_SHAPE_BOX,
                                    .contents = Q2_PLAYER_CONTENTS,
                                    .role = QA_COLLISION_SOLID,
                                    .dead_monster = a->client->info.dead};
    return qa_world_set_collision(g->services.world, a->id, solid ? &collision : NULL, e);
}
static bool player_disconnect(void *context, qa_actor_id id, qa_error *e) {
    qa_q2_game *g = context;
    q2_actor *a = q2_client(g, id, e);
    if (!a)
        return false;
    q2_client_state *s = a->client;
    char text[96];
    snprintf(text, sizeof(text), "%s disconnected\n", s->info.name);
    if (!q2_player_print(g, (qa_actor_id){0}, 2, text, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    if (!qa_q2_clear_trackers(g, id, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    if (a->powers && q2_actor_live(g, a->powers->sphere) &&
        !qa_session_release(g->services.session, a->powers->sphere, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    qa_q2_player_services *services = &g->player_runtime->services;
    if (services->disconnect && !services->disconnect(services->context, id, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    s->info.connected = false;
    s->spawned = false;
    s->visual.visible = false;
    if (!q2_player_loop(g, a, 0, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    if (!q2_player_collision(g, a, false, e) || !q2_publish_visual(g, id, &s->visual, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    qa_body_state body;
    qa_string_id effect;
    if (!qa_world_body_read(g->services.world, id, &body, e))
        return !q2_actor_live(g, id);
    if (!q2_actor_live(g, id))
        return true;
    if (!qa_builtin_resource(&g->services, "q2:logout", &effect, e) ||
        !qa_builtin_emit(&g->services,
                         &(qa_builtin_event){.kind = QA_BUILTIN_EFFECT,
                                             .family = QA_GAME_Q2,
                                             .provider = g->options.owner,
                                             .resource = effect,
                                             .origin = body.origin,
                                             .count = 1,
                                             .code = 10,
                                             .time_ns = g->now_ns},
                         e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    return q2_player_emit(g,
                          &(qa_q2_player_event){.kind = QA_Q2_PLAYER_USERINFO,
                                                .actor = id,
                                                .slot = s->info.slot,
                                                .text = "",
                                                .skin = ""},
                          e);
}
bool qa_q2_player_disconnect(qa_q2_game *g, qa_actor_id id, qa_error *e) {
    return qa_q2_run_actor(g, id, player_disconnect, g, e);
}
