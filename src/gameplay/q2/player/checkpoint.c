#include "../entities/checkpoint_internal.h"
#include "internal.h"

void qa_q2_player_checkpoint_free(qa_q2_player_checkpoint *s) {
    if (!s)
        return;
    qa_q2_player_carry_free(&s->value.rule.coop);
    free(s->value.rule.spawn_inventory);
    free(s->value.rule.help_points);
    *s = (qa_q2_player_checkpoint){0};
}
static bool copy_arrays(const qa_q2_player_rule_tail *from, qa_q2_player_rule_tail *to, qa_error *e) {
    to->coop.inventory = NULL;
    to->spawn_inventory = NULL;
    to->help_points = NULL;
    void *copy;
    if (!q2_saved_array(from->coop.inventory, from->coop.count, sizeof(*from->coop.inventory),
                        &copy, e))
        return false;
    to->coop.inventory = copy;
    if (!q2_saved_array(from->spawn_inventory, from->spawn_count, sizeof(*from->spawn_inventory),
                        &copy, e))
        return false;
    to->spawn_inventory = copy;
    if (!q2_saved_array(from->help_points, from->help_count, sizeof(*from->help_points), &copy, e))
        return false;
    to->help_points = copy;
    to->help_capacity = to->help_count;
    return true;
}
bool qa_q2_player_capture(qa_q2_game *g, qa_actor_id id, qa_q2_player_checkpoint *out,
                          qa_error *e) {
    if (!g || !out || !q2_actor_live(g, id)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 player checkpoint actor");
        return false;
    }
    if (!q2_checkpoint_idle(g, e))
        return false;
    qa_q2_player_checkpoint saved = {0};
    q2_actor *a = q2_actor_get(g, id, false, NULL);
    if (!a || !a->client) {
        *out = saved;
        return true;
    }
    const q2_client_state *s = a->client;
    saved.present = true;
    saved.value.rule = s->rule;
    saved.value.bot = s->player->bot;
    q2_player_source_info_read(g, s, &saved.value.info);
    if (!copy_arrays(&s->rule, &saved.value.rule, e))
        goto fail;
    if (!q2_save_reference(g, s->info.chase_target, &saved.chase_target, e) ||
        !q2_save_reference(g, s->rule.noise[0], &saved.noise[0], e) ||
        !q2_save_reference(g, s->rule.noise[1], &saved.noise[1], e) ||
        !q2_save_reference(g, s->rule.sphere_camera, &saved.sphere_camera, e) ||
        !q2_save_reference(g, s->rule.pending_landmark.player, &saved.landmark_player, e))
        goto fail;
    saved.value.info.chase_target = (qa_actor_id){0};
    saved.value.rule.noise[0] = saved.value.rule.noise[1] = saved.value.rule.sphere_camera = (qa_actor_id){0};
    saved.value.rule.pending_landmark.player = (qa_actor_id){0};
    *out = saved;
    return true;
fail:
    qa_q2_player_checkpoint_free(&saved);
    return false;
}
static bool valid_state(qa_q2_game *g, const qa_q2_player_state *s, qa_error *e) {
    const float values[] = {s->info.view_height,   s->rule.fov,
                            s->rule.damage_blood,       s->rule.damage_armor,
                            s->rule.damage_power,       s->rule.damage_knockback,
                            s->rule.damage_alpha,       s->rule.bonus_alpha,
                            s->rule.damage_pitch,       s->rule.damage_roll,
                            s->rule.fall_value,         s->rule.bob_time,
                            s->rule.bob_move,           s->rule.killer_yaw,
                            s->rule.fog_transition,     s->rule.coop.health,
                            s->rule.coop.maximum_health};
    for (size_t i = 0; i < sizeof(values) / sizeof(*values); i++)
        if (!isfinite(values[i]))
            return false;
    const qa_vec3 vectors[] = {s->rule.damage_from,     s->rule.damage_blend,     s->rule.old_velocity,
                               s->rule.old_view_angles, s->rule.slow_view_angles, s->rule.help_location,
                               s->rule.squad_origin,    s->rule.squad_angles};
    for (size_t i = 0; i < sizeof(vectors) / sizeof(*vectors); i++)
        if (!qa_vec_finite(vectors[i]))
            return false;
    const struct {
        const char *text;
        size_t size;
    } strings[] = {{s->rule.userinfo, sizeof(s->rule.userinfo)},
                   {s->rule.social_id, sizeof(s->rule.social_id)},
                   {s->rule.dogtag, sizeof(s->rule.dogtag)},
                   {s->info.name, sizeof(s->info.name)},
                   {s->info.skin, sizeof(s->info.skin)}};
    for (size_t i = 0; i < sizeof(strings) / sizeof(*strings); i++)
        if (!memchr(strings[i].text, 0, strings[i].size))
            return false;
    if (s->info.chase_target.registry || s->rule.noise[0].registry || s->rule.noise[1].registry ||
        s->rule.sphere_camera.registry || !q2_saved_landmark(g, &s->rule.pending_landmark) ||
        (unsigned)s->rule.hand > QA_Q2_CENTER_HAND || s->rule.gender < 0 || s->rule.gender > 2 ||
        s->rule.old_water < 0 || s->rule.old_water > 3 || s->rule.flood_count > 10 ||
        (s->rule.help_count && !s->rule.help_points) || s->rule.help_count > SIZE_MAX / sizeof(qa_vec3) ||
        !q2_saved_visual(g, &s->rule.visual) || !q2_saved_fog(&s->rule.fog) ||
        !q2_saved_fog(&s->rule.wanted_fog) || !q2_saved_resource(g, s->rule.loop_sound) ||
        !q2_saved_resource(g, s->rule.help_image) || !q2_saved_resource(g, s->info.selected_item) ||
        !q2_saved_resource(g, s->rule.coop.selected_item) ||
        (unsigned)s->rule.coop.weapon >= QA_Q2_WEAPON_COUNT ||
        !q2_saved_inventory(g, s->rule.coop.inventory, s->rule.coop.count, e) ||
        !q2_saved_inventory(g, s->rule.spawn_inventory, s->rule.spawn_count, e))
        return false;
    const qa_armor *armor = &s->rule.coop.armor;
    if (s->rule.character_configured) {
        const char *model = qa_strings_cstr(qa_session_strings(g->services.session),
                                            s->rule.character_model);
        if (!model || !*model || s->rule.character_skin < 0 || s->rule.corpse ||
            !s->info.connected || s->rule.awaiting_respawn)
            return false;
    } else if (s->rule.character_model || s->rule.character_skin)
        return false;
    if (s->rule.hit_marker_damage < INT16_MIN || s->rule.hit_marker_damage > INT16_MAX ||
        (g->options.edition != QA_Q2_RERELEASE && s->rule.hit_marker_damage)) return false;
    if (!qa_armor_validate(armor, e) || !q2_saved_resource(g, armor->powered.source_owner))
        return false;
    if ((unsigned)armor->regular.kind > QA_ARMOR_SOURCE ||
        (unsigned)armor->powered.kind > QA_POWER_SHIELD ||
        !q2_saved_resource(g, armor->regular.item) || !isfinite(armor->regular.points) ||
        !isfinite(armor->powered.cells))
        return false;
    if (armor->regular.kind == QA_ARMOR_Q2 && (!isfinite(armor->regular.protection.q2.normal) ||
                                               !isfinite(armor->regular.protection.q2.energy)))
        return false;
    if (armor->regular.kind == QA_ARMOR_Q1 && !isfinite(armor->regular.protection.q1_absorption))
        return false;
    if (armor->regular.kind == QA_ARMOR_Q3 && !isfinite(armor->regular.protection.q3_protection))
        return false;
    for (size_t i = 0; i < s->rule.help_count; i++)
        if (!qa_vec_finite(s->rule.help_points[i]))
            return false;
    return true;
}
bool qa_q2_player_restore(qa_q2_game *g, qa_actor_id id, const qa_q2_player_checkpoint *saved,
                          qa_error *e) {
    if (!g || !saved || !q2_actor_live(g, id) ||
        (saved->present && !valid_state(g, &saved->value, e))) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid Q2 player checkpoint");
        return false;
    }
    if (!q2_checkpoint_idle(g, e))
        return false;
    q2_actor *a = q2_actor_get(g, id, saved->present, saved->present ? e : NULL);
    if (!saved->present) {
        if (a)
            q2_client_release_state(a);
        return true;
    }
    if (!a)
        return false;
    q2_client_state *state = malloc(sizeof(*state));
    if (!state) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Restoring Q2 player state");
        return false;
    }
    state->rule = saved->value.rule;
    state->player = qa_actors_player(qa_world_actors(g->services.world), id);
    const qa_q2_player_info *info = &saved->value.info;
    state->info = (q2_player_source_info){
        .slot = info->slot,
        .seat = info->seat,
        .score = info->score,
        .lives = info->lives,
        .chase_target = info->chase_target,
        .selected_item = info->selected_item,
        .view_height = info->view_height,
        .connected = info->connected,
        .spectator = info->spectator,
        .dead = info->dead,
        .god = info->god,
        .notarget = info->notarget,
        .noclip = info->noclip,
        .flashlight = info->flashlight,
    };
    qa_strings *strings = qa_session_strings(g->services.session);
    if (!qa_strings_intern_cstr(strings, info->name, &state->source_name, e) ||
        !qa_strings_intern_cstr(strings, info->skin, &state->source_skin, e)) {
        free(state); return false;
    }
    /* Shared identity is restored by the actor checkpoint. Standalone/legacy
     * source imports admit that component through their saved projection. */
    if (!state->rule.corpse && !state->player->present) {
        char name[32];
        bool named = q2_player_userinfo_value(state->rule.userinfo, "name", name,
            g->options.edition == QA_Q2_RERELEASE ? sizeof(name) : 16u);
        state->player->name = state->source_name;
        if (named && !qa_strings_intern_cstr(strings, name, &state->player->name, e)) {
            free(state); return false;
        }
        state->player->skin = state->source_skin;
        state->player->ping = info->ping;
        state->player->bot = saved->value.bot;
        state->player->spectator = info->spectator;
        state->player->present = true;
    }
    if (!copy_arrays(&saved->value.rule, &state->rule, e) ||
        !q2_resolve_reference(g, saved->chase_target, &state->info.chase_target, e) ||
        !q2_resolve_reference(g, saved->noise[0], &state->rule.noise[0], e) ||
        !q2_resolve_reference(g, saved->noise[1], &state->rule.noise[1], e) ||
        !q2_resolve_reference(g, saved->sphere_camera, &state->rule.sphere_camera, e) ||
        !q2_resolve_reference(g, saved->landmark_player, &state->rule.pending_landmark.player, e)) {
        q2_actor temporary = {.client = state};
        q2_client_release_state(&temporary);
        return false;
    }
    q2_client_release_state(a);
    a->client = state;
    return true;
}
bool qa_q2_players_capture(qa_q2_game *g, bool level_only, qa_q2_players_checkpoint *out, qa_error *e) {
    if (!g || !out) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 players checkpoint");
        return false;
    }
    if (!q2_checkpoint_idle(g, e))
        return false;
    q2_players *p = g->player_runtime;
    qa_q2_players_checkpoint s = {.corpse_index = p->corpse_index,
                                  .death_animation = p->death_animation,
                                  .pain_animation = p->pain_animation,
                                  .intermission = p->intermission,
                                  .exit = p->exit,
                                  .camera_set = p->camera_set,
                                  .has_landmark = p->has_landmark,
                                  .deadly_killbox = p->deadly_killbox,
                                  .intermission_flags = p->intermission_flags,
                                  .intermission_ns = p->intermission_ns,
                                  .fade_ns = p->fade_ns,
                                  .restart_ns = p->restart_ns,
                                  .next_map = p->next_map,
                                  .landmark = p->landmark,
                                  .camera_origin = p->camera_origin,
                                  .camera_angles = p->camera_angles,
                                  .map_list_count=level_only?0:p->rules.map_list_count,
                                  .next_map_rule=level_only?QA_STRING_NONE:p->rules.next_map,
                                  .map_list_shuffle=!level_only&&p->rules.map_list_shuffle};
    for (size_t i = 0; i < 8; i++)
        if (!q2_save_reference(g, p->corpses[i], &s.corpses[i], e))
            return false;
    for (size_t i = 0; i < 2; i++) {
        s.noise[i] = p->noise[i];
        s.noise[i].owner = (qa_actor_id){0};
        if (!q2_save_reference(g, p->noise[i].owner, &s.noise_owner[i], e))
            return false;
    }
    if (!q2_save_reference(g, p->landmark.player, &s.landmark_player, e))
        return false;
    s.landmark.player = (qa_actor_id){0};
    if(s.map_list_count) {
        s.map_list=malloc(s.map_list_count*sizeof(*s.map_list));
        if(!s.map_list) {qa_error_set(e,QA_ERROR_MEMORY,0,"Capturing actual Q2 rotation order");return false;}
        memcpy(s.map_list,p->rotation_maps,s.map_list_count*sizeof(*s.map_list));
    }
    *out = s;
    return true;
}
bool qa_q2_players_restore(qa_q2_game *g, const qa_q2_players_checkpoint *s, qa_error *e) {
    if (!g || !s || s->corpse_index >= 8 || s->death_animation >= 3 ||
        s->pain_animation >= 3 || !q2_saved_resource(g, s->next_map) ||
        !q2_saved_landmark(g, &s->landmark) || !qa_vec_finite(s->camera_origin) ||
        !qa_vec_finite(s->camera_angles) || !q2_saved_resource(g,s->next_map_rule) ||
        s->map_list_count>UINT32_MAX || s->map_list_count>SIZE_MAX/sizeof(*s->map_list) ||
        (s->map_list_count && !s->map_list)) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid Q2 players runtime checkpoint");
        return false;
    }
    if (!q2_checkpoint_idle(g, e))
        return false;
    for(size_t i=0;i<s->map_list_count;++i) if(!q2_saved_resource(g,s->map_list[i])) {
        qa_error_set(e,QA_ERROR_FORMAT,i,"Invalid Q2 source rotation map");return false;
    }
    q2_players next = *g->player_runtime;
    for (size_t i = 0; i < 8; i++)
        if (!q2_resolve_reference(g, s->corpses[i], &next.corpses[i], e))
            return false;
    for (size_t i = 0; i < 2; i++) {
        if (s->noise[i].owner.registry || !qa_vec_finite(s->noise[i].origin)) {
            qa_error_set(e, QA_ERROR_FORMAT, i, "Invalid Q2 player noise checkpoint");
            return false;
        }
        next.noise[i] = s->noise[i];
        if (!q2_resolve_reference(g, s->noise_owner[i], &next.noise[i].owner, e))
            return false;
    }
    next.landmark = s->landmark;
    if (!q2_resolve_reference(g, s->landmark_player, &next.landmark.player, e))
        return false;
    next.corpse_index = s->corpse_index;
    next.death_animation = s->death_animation;
    next.pain_animation = s->pain_animation;
    next.intermission = s->intermission;
    next.exit = s->exit;
    next.camera_set = s->camera_set;
    next.has_landmark = s->has_landmark;
    next.deadly_killbox = s->deadly_killbox;
    next.intermission_flags = s->intermission_flags;
    next.intermission_ns = s->intermission_ns;
    next.fade_ns = s->fade_ns;
    next.restart_ns = s->restart_ns;
    next.next_map = s->next_map;
    next.camera_origin = s->camera_origin;
    next.camera_angles = s->camera_angles;
    qa_string_id *rotation=s->map_list_count?malloc(s->map_list_count*sizeof(*rotation)):NULL;
    if(s->map_list_count && !rotation) {qa_error_set(e,QA_ERROR_MEMORY,0,"Restoring actual Q2 rotation order");return false;}
    if(s->map_list_count) memcpy(rotation,s->map_list,s->map_list_count*sizeof(*rotation));
    free(next.rotation_maps);next.rotation_maps=rotation;next.rules.map_list=rotation;
    next.rules.map_list_count=s->map_list_count;next.rules.next_map=s->next_map_rule;
    next.rules.map_list_shuffle=s->map_list_shuffle;
    *g->player_runtime = next;
    return true;
}
void qa_q2_players_checkpoint_free(qa_q2_players_checkpoint *checkpoint) {
    if(!checkpoint) return;
    free(checkpoint->map_list);*checkpoint=(qa_q2_players_checkpoint){0};
}
