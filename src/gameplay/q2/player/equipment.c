#include "qa/q2_sound.h"
#include "internal.h"

bool q2_client_sphere_camera(qa_q2_game *g, qa_actor_id id, qa_actor_id sphere, qa_vec3 origin,
                             qa_vec3 angles, qa_error *e) {
    q2_actor *a = q2_actor_get(g, id, false, NULL);
    qa_q2_item_options *options = &g->item_runtime->options;
    if (!a || !a->client) {
        if (options->sphere_camera)
            return options->sphere_camera(options->context, id, sphere, origin, angles, e);
        qa_error_set(e, QA_ERROR_ARGUMENT, 0,
                     "Foreign sphere camera requires selected character integration");
        return false;
    }
    q2_client_state *s = a->client;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, e))
        return false;
    if (sphere.registry) {
        bool first = !s->rule.sphere_camera.registry;
        if (first) {
            if (!q2_player_sound(g, id, QA_Q2_SOUND_MISC_UDEATH, 4, e))
                return false;
            for (int i = 0; i < 5 && q2_actor_live(g, id); i++)
                if (!q2_spawn_gib(g, id,
                                  i == 4 ? "models/objects/gibs/skull/tris.md2"
                                         : "models/objects/gibs/sm_meat/tris.md2",
                                  50, 0, 0, 1, e))
                    return false;
            if (!q2_actor_live(g, id))
                return true;
            s->rule.visual.models[0] = s->rule.visual.models[1] = 0;
            s->info.view_height = 8;
            s->rule.sphere_vehicle = true;
            body.bounds = (qa_bounds){qa_v3(-5, -5, -5), qa_v3(5, 5, 5)};
        } else {
            s->info.view_height = origin.z - body.origin.z;
            body.bounds = (qa_bounds){0};
        }
        s->rule.sphere_camera = sphere;
        body.origin = origin;
        body.angles = angles;
        qa_body_state tracked;
        if (!qa_world_body_read(g->services.world, sphere, &tracked, e))
            return false;
        body.velocity = tracked.velocity;
        if (a->physics_bound) {
            a->physics.motion = QA_PHYSICS_FLY_MISSILE;
            a->physics.solid = QA_PHYSICS_NOT_SOLID;
        }
        if (!q2_player_collision(g, a, false, e))
            return false;
    } else {
        s->rule.sphere_camera = (qa_actor_id){0};
        body.velocity = qa_v3(0, 0, 0);
        if (a->physics_bound)
            a->physics.motion = QA_PHYSICS_STATIONARY;
    }
    if (!qa_world_body_write(g->services.world, id, &body, e) ||
        !qa_world_link(g->services.world, id, NULL, e) || !q2_publish_visual(g, id, &s->rule.visual, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    if (options->sphere_camera)
        return options->sphere_camera(options->context, id, sphere, body.origin, angles, e);
    return q2_player_move(g, a,
                          &(qa_q2_player_motion){
                              .kind = QA_Q2_PLAYER_FREEZE, .origin = body.origin, .angles = angles},
                          e);
}
static bool send_poi(qa_q2_game *g, q2_actor *a, qa_error *e) {
    q2_client_state *s = a->client;
    s->rule.help_marker_ns = q2_deadline(g->now_ns, 10 * Q2_NS);
    return q2_map_event(g,
                        &(qa_q2_map_event){.kind = QA_Q2_MAP_POI,
                                           .recipient = a->id,
                                           .origin = s->rule.help_location,
                                           .resource = s->rule.help_image,
                                           .duration = 10000,
                                           .count = 208},
                        e);
}
bool q2_player_compass_update(qa_q2_game *g, q2_actor *a, bool first, qa_error *e) {
    q2_client_state *s = a->client;
    if (s->rule.help_index >= s->rule.help_count || s->rule.help_draw_ns >= g->now_ns)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, e))
        return false;
    qa_vec3 point = s->rule.help_points[s->rule.help_index];
    if (qa_vec_length(qa_vec_sub(point, body.origin)) > 4096 ||
        !q2_map_in_phs(g, body.origin, point)) {
        s->rule.help_count = 0;
        return true;
    }
    qa_vec3 next =
        s->rule.help_index + 1 < s->rule.help_count ? s->rule.help_points[s->rule.help_index + 1] : s->rule.help_location;
    s->rule.help_index++;
    s->rule.help_draw_ns = q2_deadline(g->now_ns, 200 * Q2_MS);
    if (!q2_player_emit(
            g,
            &(qa_q2_player_event){.kind = QA_Q2_PLAYER_HELP_PATH,
                                  .actor = a->id,
                                  .first = first,
                                  .origin = point,
                                  .direction = qa_vec_normalize(qa_vec_sub(next, point))},
            e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    if (!send_poi(g, a, e))
        return false;
    if (!q2_actor_live(g, a->id))
        return true;
    qa_string_id sound;
    if (!qa_builtin_resource(&g->services, QA_Q2_SOUND_MISC_HELP_MARKER, &sound, e))
        return false;
    return qa_builtin_emit(&g->services,
                           &(qa_builtin_event){.kind = QA_BUILTIN_SOUND,
                                               .actor = a->id,
                                               .provider = g->options.owner,
                                               .family = QA_GAME_Q2,
                                               .origin = point,
                                               .resource = sound,
                                               .volume = 1,
                                               .attenuation = 1,
                                               .time_ns = g->now_ns},
                           e);
}
bool q2_client_item_action(qa_q2_game *g, qa_actor_id id, bool flashlight, bool *used,
                           qa_error *e) {
    q2_actor *a = q2_client(g, id, e);
    if (!a || !used)
        return false;
    q2_client_state *s = a->client;
    *used = false;
    if (flashlight) {
        s->info.flashlight = !s->info.flashlight;
        *used = true;
        qa_combat_state combat;
        if (!qa_combat_read(g->services.combat, id, &combat, e))
            return false;
        return q2_player_emit(g,
                              &(qa_q2_player_event){.kind = QA_Q2_PLAYER_FLASHLIGHT,
                                                    .actor = id,
                                                    .hand = s->rule.hand,
                                                    .visible = s->info.flashlight &&
                                                               !g->player_runtime->intermission &&
                                                               combat.health > 0},
                              e);
    }
    bool present;
    if (!q2_map_poi(g, id, &s->rule.help_location, &s->rule.help_image, &present, e))
        return false;
    if (!present)
        return q2_player_print(g, id, 2, "$no_valid_poi", e);
    if (!q2_actor_live(g, id))
        return true;
    if (s->rule.help_capacity < 129) {
        qa_vec3 *points = realloc(s->rule.help_points, 129 * sizeof(*points));
        if (!points) {
            qa_error_set(e, QA_ERROR_MEMORY, 0, "Retaining Q2 compass path");
            return false;
        }
        s->rule.help_points = points;
        s->rule.help_capacity = 129;
    }
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, e))
        return false;
    size_t count = 0;
    bool reachable;
    if (!q2_map_navigation(g, id, body.origin, s->rule.help_location, s->rule.help_points, 128, &count,
                           &reachable, e))
        return false;
    if (!q2_actor_live(g, id))
        return true;
    *used = true;
    if (!reachable || !count) {
        if (!send_poi(g, a, e))
            return false;
        return !q2_actor_live(g, id) || q2_player_sound(g, id, QA_Q2_SOUND_MISC_HELP_MARKER, 0, e);
    }
    if (count > 128)
        count = 128;
    s->rule.help_count = count;
    s->rule.help_index = 0;
    while (s->rule.help_index + 1 < count &&
           qa_vec_length(qa_vec_sub(s->rule.help_points[s->rule.help_index], body.origin)) <= 192)
        s->rule.help_index++;
    qa_q2_player_movement m;
    if (!q2_player_observe(g, a, &m, e))
        return false;
    qa_vec3 forward;
    qa_builtin_angle_vectors(m.view_angles, &forward, NULL, NULL);
    if (qa_vec_dot(qa_vec_normalize(qa_vec_sub(s->rule.help_points[s->rule.help_index], body.origin)),
                   forward) < .3f) {
        qa_trace_result hit;
        if (!q2_player_trace(g, (qa_actor_id){0},
                             qa_vec_add(body.origin, qa_v3(0, 0, s->info.view_height)),
                             qa_vec_add(body.origin, qa_vec_scale(forward, 64)), NULL, 1, &hit, e))
            return false;
        qa_vec3 point = hit.fraction < 1 && hit.contact
                            ? qa_vec_add(hit.end, qa_vec_scale(hit.contact_plane.normal, 8))
                            : hit.end;
        memmove(s->rule.help_points + s->rule.help_index + 1, s->rule.help_points + s->rule.help_index,
                (s->rule.help_count - s->rule.help_index) * sizeof(*s->rule.help_points));
        s->rule.help_points[s->rule.help_index] = point;
        s->rule.help_count++;
    }
    s->rule.help_draw_ns = 0;
    return q2_player_compass_update(g, a, true, e);
}
