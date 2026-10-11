#include "../entities/internal.h"
#include "qa/text.h"

bool q2_player_map_is(const char *map, const char *name) {
    while (*map && *name) {
        unsigned char byte = (unsigned char)*map++;
        if (byte >= 'A' && byte <= 'Z')
            byte = (unsigned char)(byte + 'a' - 'A');
        if (byte != (unsigned char)*name++)
            return false;
    }
    return !*map && !*name;
}
bool q2_player_same_target(qa_q2_game *g, qa_string_id a, qa_string_id b, bool *same, qa_error *e) {
    if (a == b) {
        *same = true;
        return true;
    }
    qa_strings *strings = qa_session_strings(g->services.session);
    qa_bytes first = qa_strings_text(strings, a), second = qa_strings_text(strings, b);
    size_t common = first.size < second.size ? first.size : second.size;
    bool ascii = true;
    for (size_t i = 0; i < common; i++) {
        uint8_t left = first.data[i], right = second.data[i];
        if (left >= 128 || right >= 128) {
            ascii = false;
            break;
        }
        if (left >= 'A' && left <= 'Z')
            left = (uint8_t)(left + 'a' - 'A');
        if (right >= 'A' && right <= 'Z')
            right = (uint8_t)(right + 'a' - 'A');
        if (left != right) {
            *same = false;
            return true;
        }
    }
    if (ascii && first.size == second.size) {
        *same = true;
        return true;
    }
    if (ascii && (!first.size || !second.size)) {
        *same = false;
        return true;
    }
    qa_buffer left = {0}, right = {0};
    if (!qa_utf8_lower(first, &left, e) || !qa_utf8_lower(second, &right, e)) {
        qa_buffer_free(&left);
        qa_buffer_free(&right);
        return false;
    }
    *same = left.size == right.size && (!left.size || !memcmp(left.data, right.data, left.size));
    qa_buffer_free(&left);
    qa_buffer_free(&right);
    return true;
}
bool q2_player_map_spawn(qa_q2_game *g, q2_actor *a, bool *handled, qa_error *e) {
    q2_entity_state *s = a->entity;
    const char *name = qa_strings_cstr(qa_session_strings(g->services.session), s->classname);
    bool start = !strcmp(name, "info_player_start"), coop = !strcmp(name, "info_player_coop"),
         deathmatch = !strcmp(name, "info_player_deathmatch"),
         intermission = !strcmp(name, "info_player_intermission"),
         lava = g->options.edition == QA_Q2_RERELEASE && !strcmp(name, "info_player_coop_lava");
    *handled = start || coop || deathmatch || intermission || lava;
    if (!*handled)
        return true;
    if (((coop || lava) && !g->options.cooperative) || (deathmatch && !g->options.deathmatch))
        return qa_session_release(g->services.session, a->id, e);
    if (intermission)
        return true;
    s->kind = Q2E_POINT;
    if (deathmatch) {
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, a->id, &body, e) ||
            !qa_builtin_resource(&g->services, "models/objects/dmspot/tris.md2",
                                 &s->visual.models[0], e))
            return false;
        body.bounds = (qa_bounds){{-32, -32, -24}, {32, 32, -16}};
        s->visual.skin = 1;
        s->visual.visible = true;
        return q2_entity_body(g, a, &body, false, e) &&
               (!q2_actor_live(g, a->id) ||
                (q2_entity_solid(g, a, QA_PHYSICS_BOX, e) &&
                 (!q2_actor_live(g, a->id) || q2_entity_show(g, a, e))));
    }
    if (g->options.edition == QA_Q2_RERELEASE) {
        qa_body_state body;
        qa_trace_result hit;
        const qa_bounds bounds = {{-16, -16, -24}, {16, 16, 32}};
        if (!qa_world_body_read(g->services.world, a->id, &body, e) ||
            !q2_player_trace(g, a->id, body.origin, body.origin, &bounds, 1, &hit, e))
            return false;
        if (hit.start_solid) {
            bool fixed;
            qa_vec3 position;
            if (!q2_player_fix_stuck(g, a->id, body.origin, bounds, &position, &fixed, e))
                return false;
            if (fixed) {
                body.origin = position;
                if (!q2_entity_body(g, a, &body, true, e))
                    return false;
                if (!q2_actor_live(g, a->id))
                    return true;
            }
        }
        if (!lava && !strncmp(g->player_runtime->rules.map_name, "q64/", 4))
            q2_entity_schedule(g, a, Q2ET_PLAYER_START_DROP, (float)g->frame_ns / Q2_NS);
        return true;
    }
    if (start && g->options.cooperative &&
        q2_player_map_is(g->player_runtime->rules.map_name, "security"))
        return q2_entity_schedule(g, a, Q2ET_PLAYER_SECURITY, .1f);
    if (coop) {
        static const char *const repairs[] = {"jail2", "jail4",   "mine1",  "mine2", "mine3",
                                              "mine4", "lab",     "boss1",  "fact3", "biggun",
                                              "space", "command", "power2", "strike"};
        for (size_t i = 0; i < sizeof(repairs) / sizeof(*repairs); i++)
            if (q2_player_map_is(g->player_runtime->rules.map_name, repairs[i]))
                return q2_entity_schedule(g, a, Q2ET_PLAYER_COOP_FIX, .1f);
    }
    return true;
}
bool q2_player_map_think(qa_q2_game *g, q2_actor *a, q2_entity_think think, qa_error *e) {
    if (think == Q2ET_PLAYER_SECURITY) {
        static const float x[] = {124, 252, 316};
        qa_string_id target;
        if (!qa_builtin_resource(&g->services, "jail3", &target, e))
            return false;
        for (size_t i = 0; i < 3; i++) {
            qa_body_state body = {.origin = {x[i], -164, 80}, .angles = {0, 90, 0}};
            q2_actor *spot;
            if (!q2_entity_native_spawn(g, "info_player_coop", &body, Q2E_POINT, &spot, e) ||
                !qa_q2_entity_set_targetname(g, spot->id, target, e))
                return false;
            if (!q2_actor_live(g, a->id))
                return true;
        }
        return true;
    }
    if (think == Q2ET_PLAYER_START_DROP) {
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, a->id, &body, e))
            return false;
        body.bounds = (qa_bounds){{-16, -16, -24}, {16, 16, 32}};
        a->physics.motion = QA_PHYSICS_TOSS;
        return q2_entity_body(g, a, &body, false, e) &&
               (!q2_actor_live(g, a->id) || q2_entity_solid(g, a, QA_PHYSICS_TRIGGER, e));
    }
    if (think == Q2ET_PLAYER_COOP_FIX) {
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, a->id, &body, e))
            return false;
        qa_actor_id id;
        qa_target_cursor cursor = {0};
        while (qa_targets_next_authored(g->entity_runtime->services.targets, g->runtime_names[Q2_NAME_INFO_PLAYER_START],
                                        &cursor, &id)) {
            qa_authored_target fields;
            qa_body_state start;
            if (!qa_targets_read(g->entity_runtime->services.targets, id, &fields) ||
                !fields.targetname)
                continue;
            if (!qa_world_body_read(g->services.world, id, &start, e))
                return false;
            if (qa_vec_length(qa_vec_sub(start.origin, body.origin)) >= 384)
                continue;
            bool same;
            if (!q2_player_same_target(g, a->entity->targetname, fields.targetname, &same, e))
                return false;
            return same || qa_q2_entity_set_targetname(g, a->id, fields.targetname, e);
        }
        return true;
    }
    qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid Q2 player start continuation");
    return false;
}
