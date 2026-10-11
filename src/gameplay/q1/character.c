#include "internal.h"
#include <stdio.h>

static q1_player *character(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    q1_player *player = q1_player_get(g, actor);
    if (!player || !player->character) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, actor.slot, "Q1 character is not attached");
        return NULL;
    }
    return player;
}
static bool refresh_pose(qa_q1_game *g, q1_player *player, qa_error *error) {
    q1_character *c = &player->character_state;
    if (c->life != QA_Q1_ALIVE)
        return true;
    qa_q1_character_pose pose = {.axe_pose = c->input.axe_pose};
    if (g->host.character_pose)
        (void)g->host.character_pose(g->host.context, player->id, &pose);
    if (pose.custom_model && (!pose.model || !pose.stand.count || !pose.run.count ||
                              !pose.pain.count || !pose.death.count)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, player->id.slot,
                     "Q1 character model needs nonempty animation ranges");
        return false;
    }
    qa_string_id model = pose.custom_model ? pose.model : g->player_model;
    c->pose = pose;
    if (model != c->model) {
        c->model = model;
        c->animation.count = 0;
        c->attack_animation = false;
        c->walk_frame = 0;
        c->locomotion = 0;
        c->frame = pose.custom_model ? pose.stand.first : pose.axe_pose ? 17 : 12;
        c->next_animation = g->time;
    }
    return true;
}
static void animate(qa_q1_game *g, q1_character *c, qa_q1_frame_range range, bool death,
                    bool attack) {
    c->animation = range;
    c->death_animation = death;
    c->attack_animation = attack;
    c->animation_frame = 0;
    c->frame = range.first;
    c->next_animation = g->time + 0.1;
}
bool qa_q1_character_attach(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    if (!g) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q1 character game missing");
        return false;
    }
    q1_player *player = q1_player_allocate(g, actor, error);
    if (!player)
        return false;
    if (player->character)
        return true;
    player->character = true;
    player->character_state = (q1_character){.life = QA_Q1_ALIVE,
                                             .birth_epoch = 1,
                                             .frame = 12,
                                             .view_offset = {0, 0, 22},
                                             .air_until = g->time + 12,
                                             .drown_damage = 2};
    return refresh_pose(g, player, error);
}
bool qa_q1_character_read(const qa_q1_game *g, qa_actor_id actor, qa_q1_character_view *out) {
    if (!g || !out || actor.slot >= g->capacity ||
        !qa_actors_get(qa_session_actors(g->services.session), actor))
        return false;
    const q1_player *player = g->players[actor.slot];
    if (!player || !player->character || !qa_actor_id_equal(player->id, actor))
        return false;
    const q1_character *c = &player->character_state;
    bool alive = c->life == QA_Q1_ALIVE;
    qa_string_id model = alive && c->input.invisible ? g->eyes_model : c->model;
    bool head = c->model == g->player_head_model;
    *out = (qa_q1_character_view){.model = model,
                                  .frame = alive && c->input.invisible     ? 0
                                           : alive && c->pose.source_frame ? c->pose.frame
                                                                           : c->frame,
                                  .view_offset = c->view_offset,
                                  .life = c->life,
                                  .solid = alive ? QA_PHYSICS_BOX : QA_PHYSICS_NOT_SOLID,
                                  .motion = alive  ? QA_PHYSICS_STEP
                                            : head ? QA_PHYSICS_BOUNCE
                                                   : QA_PHYSICS_TOSS,
                                  .weapon_visible = alive && !c->weapon_hidden,
                                  .next_frame_seconds = c->next_animation,
                                  .animation_frame = c->frame};
    return true;
}
bool qa_q1_character_cutscene(qa_q1_game *g, qa_actor_id actor, qa_vec3 view_offset,
                              bool weapon_visible, qa_error *error) {
    if (!g || !qa_vec_finite(view_offset)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "invalid Q1 cinematic presentation");
        return false;
    }
    q1_player *player = character(g, actor, error);
    if (!player)
        return false;
    q1_character *c = &player->character_state;
    c->view_offset = view_offset;
    c->weapon_hidden = !weapon_visible;
    c->input.attack = c->input.jump = false;
    return true;
}
bool qa_q1_character_frame(qa_q1_game *g, qa_actor_id actor, const qa_q1_character_input *input,
                           qa_error *error) {
    q1_player *player = character(g, actor, error);
    if (!player || !input || input->water_level > 3)
        return false;
    q1_character *c = &player->character_state;
    c->input = *input;
    if (!refresh_pose(g, player, error))
        return false;
    if (c->life == QA_Q1_DEAD && c->next_animation == -1)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, actor, &body, error))
        return false;
    bool source_attack = c->life == QA_Q1_ALIVE && c->pose.source_frame;
    if (source_attack) {
        c->animation.count = 0;
        c->attack_animation = false;
    }
    const q1_actor *native = q1_entity_const(g, actor);
    bool grounded = native && (native->physics.flags & QA_PHYSICS_ONGROUND);
    qa_builtin_actor_traits traits;
    if (g->services.actor_traits && g->services.actor_traits(g->services.context, actor, &traits))
        grounded = traits.grounded;
    if ((c->life == QA_Q1_DEAD || c->life == QA_Q1_RESPAWNABLE) && grounded) {
        float speed = qa_vec_length(body.velocity), next = fmaxf(0, speed - 20);
        body.velocity = speed > 0 ? qa_vec_scale(body.velocity, next / speed) : qa_v3(0, 0, 0);
        if (!qa_world_body_write(g->services.world, actor, &body, error))
            return false;
    }
    if (g->time >= c->next_animation && !source_attack) {
        c->next_animation = g->time + 0.1;
        if (c->animation.count) {
            ++c->animation_frame;
            if (c->animation_frame < c->animation.count)
                c->frame = c->animation.first + c->animation_frame;
            else {
                c->animation.count = 0;
                c->attack_animation = false;
                if (c->death_animation)
                    c->life = QA_Q1_DEAD;
            }
        }
        if (!c->animation.count && c->life == QA_Q1_ALIVE) {
            bool running = body.velocity.x != 0 || body.velocity.y != 0;
            uint8_t locomotion = running ? 2 : 1;
            if (c->locomotion != locomotion)
                c->walk_frame = 0;
            c->locomotion = locomotion;
            qa_q1_frame_range range = c->pose.custom_model
                                          ? running ? c->pose.run : c->pose.stand
                                          : (qa_q1_frame_range){running ? c->pose.axe_pose ? 0 : 6
                                                                : c->pose.axe_pose ? 17
                                                                                   : 12,
                                                                running            ? 6
                                                                : c->pose.axe_pose ? 12
                                                                                   : 5};
            c->walk_frame %= range.count;
            c->frame = range.first + c->walk_frame++;
        }
    }
    bool pressed = input->attack || input->jump || input->use;
    if (c->life == QA_Q1_DEAD && !pressed)
        c->life = QA_Q1_RESPAWNABLE;
    else if (c->life == QA_Q1_RESPAWNABLE && pressed) {
        if (!g->host.request_respawn) {
            qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
                         "Q1 character respawn dispatcher missing");
            return false;
        }
        return g->host.request_respawn(g->host.context, actor, error);
    }
    return true;
}
bool qa_q1_character_attack_frame(qa_q1_game *g, qa_actor_id actor, qa_q1_character_attack attack,
                                  unsigned variant, qa_error *error) {
    q1_player *player = character(g, actor, error);
    if (!player)
        return false;
    q1_character *c = &player->character_state;
    if (c->life != QA_Q1_ALIVE)
        return true;
    if (attack > QA_Q1_CHARACTER_LIGHTNING || (attack == QA_Q1_CHARACTER_AXE && variant > 3)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, attack, "invalid Q1 character attack animation");
        return false;
    }
    if (!refresh_pose(g, player, error))
        return false;
    if (c->pose.custom_model || c->pose.source_frame) {
        c->animation.count = 0;
        c->attack_animation = false;
        return true;
    }
    qa_q1_frame_range range =
        attack == QA_Q1_CHARACTER_AXE
            ? (qa_q1_frame_range){(uint16_t)(119 + variant * 6), 6}
            : (qa_q1_frame_range){
                  attack == QA_Q1_CHARACTER_SHOTGUN  ? 113
                  : attack == QA_Q1_CHARACTER_ROCKET ? 107
                  : attack == QA_Q1_CHARACTER_NAIL   ? 103
                                                     : 105,
                  attack == QA_Q1_CHARACTER_NAIL || attack == QA_Q1_CHARACTER_LIGHTNING ? 2 : 6};
    animate(g, c, range, false, true);
    return true;
}
static bool cause_named(qa_q1_game *g, qa_actor_id actor, const char *name) {
    qa_string_id classname = 0;
    q1_actor *entity = q1_entity(g, actor);
    if (entity)
        classname = entity->classname;
    else if (g->services.actor_traits) {
        qa_builtin_actor_traits traits;
        if (g->services.actor_traits(g->services.context, actor, &traits))
            classname = traits.classname;
    }
    qa_bytes text = qa_strings_text(qa_session_strings(g->services.session), classname);
    return text.size == strlen(name) && !memcmp(text.data, name, text.size);
}
static bool bubbles(qa_q1_game *g, qa_actor_id actor, unsigned count, qa_error *error) {
    q1_actor *timer;
    if (!q1_create(g, g->runtime_names[Q1_NAME_CLASS_DEATH_BUBBLES], Q1_TIMER, actor, &timer, error))
        return false;
    timer->count = (float)count;
    return q1_schedule(g, timer, 0.1, Q1_THINK_DEATH_BUBBLES, error);
}
static bool pain(qa_q1_game *g, q1_player *player, const qa_damage_outcome *outcome,
                 qa_error *error) {
    q1_character *c = &player->character_state;
    qa_actor_id actor = player->id;
    if (c->life != QA_Q1_ALIVE || c->input.invisible || c->attack_animation || c->pose.source_frame)
        return true;
    if (!refresh_pose(g, player, error))
        return false;
    qa_string_id sound = 0;
    float attenuation = 1;
    if (cause_named(g, outcome->request.attack.attacker, "teledeath")) {
        sound = g->runtime_names[Q1_NAME_RESOURCE_PLAYER_TELEDTH1_WAV];
        attenuation = 0;
    } else if (c->input.water_level == 3 && c->input.water_type == -3) {
        if (!bubbles(g, actor, 1, error))
            return false;
        sound = q1_random(g) > 0.5f ? g->runtime_names[Q1_NAME_RESOURCE_PLAYER_DROWN1_WAV] : g->runtime_names[Q1_NAME_RESOURCE_PLAYER_DROWN2_WAV];
    } else if (c->input.water_type == -4 || c->input.water_type == -5)
        sound = q1_random(g) > 0.5f ? g->runtime_names[Q1_NAME_RESOURCE_PLAYER_LBURN1_WAV] : g->runtime_names[Q1_NAME_RESOURCE_PLAYER_LBURN2_WAV];
    else if (c->pain_until <= g->time) {
        c->pain_until = g->time + 0.5;
        if (outcome->request.attack.weapon == g->weapons[QA_Q1_AXE])
            sound = g->runtime_names[Q1_NAME_RESOURCE_PLAYER_AXHIT1_WAV];
        else {
            static const q1_runtime_name pains[] = {Q1_NAME_RESOURCE_PLAYER_PAIN1_WAV, Q1_NAME_RESOURCE_PLAYER_PAIN2_WAV, Q1_NAME_RESOURCE_PLAYER_PAIN3_WAV, Q1_NAME_RESOURCE_PLAYER_PAIN4_WAV, Q1_NAME_RESOURCE_PLAYER_PAIN5_WAV, Q1_NAME_RESOURCE_PLAYER_PAIN6_WAV};
            sound = g->runtime_names[pains[(int)floorf(q1_random(g) * 5 + 1.5f) - 1]];
        }
    }
    if (sound && !q1_sound_resource(g, actor, sound, 2, attenuation, 1, error))
        return false;
    if (!q1_alive(g, actor))
        return true;
    qa_q1_frame_range range =
        c->pose.custom_model ? c->pose.pain : (qa_q1_frame_range){c->pose.axe_pose ? 29 : 35, 6};
    animate(g, c, range, false, false);
    return true;
}
static bool die(qa_q1_game *g, q1_player *player, const qa_damage_outcome *outcome,
                qa_error *error) {
    q1_character *c = &player->character_state;
    qa_actor_id actor = player->id;
    if (c->life != QA_Q1_ALIVE)
        return true;
    if (!refresh_pose(g, player, error))
        return false;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, actor, &body, error))
        return false;
    qa_vec3 gib_origin = body.origin;
    c->life = QA_Q1_DYING;
    c->view_offset = qa_v3(0, 0, -8);
    if (q1_health(g, actor) < -99 && !qa_combat_set_health(g->services.combat, actor, -99, error))
        return false;
    qa_combat_state combat;
    if (!qa_combat_read_traits(g->services.combat, actor, &combat, error))
        return false;
    combat.can_take_damage = false;
    if (!qa_combat_set_traits(g->services.combat, actor, &combat, error))
        return false;
    if (!qa_q1_player_powers_clear(g, actor, error)) return false;
    if (!q1_alive(g, actor))
        return true;
    if (g->options.deathmatch || g->options.coop) {
        if (!g->host.drop_inventory) {
            qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot,
                         "Q1 death inventory dispatcher missing");
            return false;
        }
        if (!g->host.drop_inventory(g->host.context, actor, error))
            return false;
        if (!q1_alive(g, actor))
            return true;
    }
    if (body.velocity.z < 10)
        body.velocity.z += q1_random(g) * 300;
    body.ground = (qa_actor_reference){0};
    if (!qa_world_body_write(g->services.world, actor, &body, error))
        return false;
    float health = q1_health(g, actor);
    if (health < -40) {
        c->model = g->player_head_model;
        c->frame = 0;
        c->animation.count = 0;
        c->life = QA_Q1_DEAD;
        c->view_offset = qa_v3(0, 0, 8);
        float scale = health > -50 ? 0.7f : health > -200 ? 2 : 10;
        float x = 100 * (q1_random(g) * 2 - 1), y = 100 * (q1_random(g) * 2 - 1),
              z = 200 + 100 * q1_random(g);
        body.origin = qa_vec_add(gib_origin, qa_v3(0, 0, -24));
        body.velocity = qa_vec_scale(qa_v3(x, y, z), scale);
        body.bounds = (qa_bounds){{-16, -16, 0}, {16, 16, 56}};
        if (!qa_world_body_write(g->services.world, actor, &body, error))
            return false;
        static const char *models[] = {"gib1", "gib2", "gib3"};
        for (unsigned i = 0; i < 3; ++i)
            if (!q1_gib_at(g, actor, gib_origin, health, models[i], error))
                return false;
        bool teledeath = cause_named(g, outcome->request.attack.attacker, "teledeath") ||
                         cause_named(g, outcome->request.attack.attacker, "teledeath2");
        return q1_sound_resource(g, actor, teledeath             ? g->runtime_names[Q1_NAME_RESOURCE_PLAYER_TELEDTH1_WAV]
                        : q1_random(g) < 0.5f ? g->runtime_names[Q1_NAME_RESOURCE_PLAYER_GIB_WAV]
                                              : g->runtime_names[Q1_NAME_RESOURCE_PLAYER_UDEATH_WAV], 2, 0, 1, error);
    }
    if (c->input.water_level == 3) {
        if (!bubbles(g, actor, 20, error) ||
            !q1_sound_resource(g, actor, g->runtime_names[Q1_NAME_RESOURCE_PLAYER_H2ODEATH_WAV], 2, 0, 1, error))
            return false;
    } else {
        static const q1_runtime_name deaths[] = {Q1_NAME_RESOURCE_PLAYER_DEATH1_WAV, Q1_NAME_RESOURCE_PLAYER_DEATH2_WAV, Q1_NAME_RESOURCE_PLAYER_DEATH3_WAV, Q1_NAME_RESOURCE_PLAYER_DEATH4_WAV, Q1_NAME_RESOURCE_PLAYER_DEATH5_WAV};
        qa_string_id sound = g->runtime_names[deaths[(int)floorf(q1_random(g) * 4 + 1.5f) - 1]];
        if (!q1_sound_resource(g, actor, sound, 2, 0, 1, error))
            return false;
    }
    if (!q1_alive(g, actor))
        return true;
    if (!qa_world_body_read(g->services.world, actor, &body, error))
        return false;
    body.angles.x = body.angles.z = 0;
    if (!qa_world_body_write(g->services.world, actor, &body, error))
        return false;
    qa_q1_frame_range range;
    if (c->pose.custom_model)
        range = c->pose.death;
    else if (c->pose.axe_pose)
        range = (qa_q1_frame_range){41, 9};
    else {
        static const qa_q1_frame_range deaths[] = {{50, 11}, {61, 9}, {70, 15},
                                                   {85, 9},  {94, 9}, {94, 9}};
        unsigned choice = (unsigned)(q1_random(g) * 6);
        if (choice >= 6) {
            qa_error_set(error, QA_ERROR_FORMAT, choice,
                         "Q1 death animation random outside authored range");
            return false;
        }
        range = deaths[choice];
    }
    animate(g, c, range, true, false);
    return true;
}
bool qa_q1_character_reaction(qa_q1_game *g, const qa_damage_outcome *outcome, qa_error *error) {
    q1_player *player = q1_player_get(g, outcome->request.target);
    if (!player || !player->character)
        return true;
    if (outcome->result.reaction == QA_REACTION_DEATH)
        return die(g, player, outcome, error);
    if (outcome->result.reaction == QA_REACTION_PAIN)
        return pain(g, player, outcome, error);
    return true;
}
bool qa_q1_character_respawn(qa_q1_game *g, qa_actor_id actor, const float *health,
                             qa_error *error) {
    q1_player *player = character(g, actor, error);
    if (!player)
        return false;
    if (player->character_state.birth_epoch == UINT64_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "Q1 character birth epoch exhausted");
        return false;
    }
    uint64_t birth_epoch = player->character_state.birth_epoch + 1;
    player->character_state = (q1_character){.life = QA_Q1_ALIVE,
                                             .birth_epoch = birth_epoch,
                                             .frame = 12,
                                             .view_offset = {0, 0, 22},
                                             .air_until = g->time + 12,
                                             .drown_damage = 2};
    if (!refresh_pose(g, player, error))
        return false;
    qa_combat_state combat;
    if (!qa_combat_read_traits(g->services.combat, actor, &combat, error))
        return false;
    combat.can_take_damage = true;
    return qa_combat_set_traits(g->services.combat, actor, &combat, error) &&
           (!health || qa_combat_set_health(g->services.combat, actor, *health, error));
}
bool qa_q1_character_suicide_pose(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    q1_player *player = character(g, actor, error);
    if (!player)
        return false;
    q1_character *c = &player->character_state;
    c->frame = 60;
    c->life = QA_Q1_DEAD;
    c->animation.count = 0;
    c->attack_animation = false;
    c->next_animation = INFINITY;
    return true;
}
bool qa_q1_character_disconnect_pose(qa_q1_game *g, qa_actor_id actor, bool *applied,
    qa_error *error) {
    if (!applied) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "Q1 disconnect pose needs its actual result");
        return false;
    }
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error)) return false;
    q1_player *player = character(g, actor, error);
    qa_q1_character_view view;
    bool okay = player && qa_q1_character_read(g, actor, &view);
    bool changed = false;
    if (okay) {
        const char *model = qa_strings_cstr(qa_session_strings(g->services.session), view.model);
        if (model && !strcmp(model, "progs/player.mdl")) {
            okay = qa_q1_character_suicide_pose(g, actor, error);
            if (okay) {
                player->character_state.next_animation = -1;
                changed = true;
            }
        }
    }
    if (okay) *applied = changed;
    qa_q1_game_operation_end(&operation);
    return okay;
}
bool qa_q1_character_post_move(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    q1_player *player = character(g, actor, error);
    if (!player)
        return false;
    q1_character *c = &player->character_state;
    if (c->life != QA_Q1_ALIVE)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, actor, &body, error))
        return false;
    const q1_actor *native = q1_entity_const(g, actor);
    bool grounded = native && (native->physics.flags & QA_PHYSICS_ONGROUND);
    qa_builtin_actor_traits traits;
    if (g->services.actor_traits && g->services.actor_traits(g->services.context, actor, &traits))
        grounded = traits.grounded;
    if (c->fall_speed < -300 && grounded && q1_health(g, actor) > 0) {
        if (c->input.water_type == -3) {
            if (!q1_sound_resource(g, actor, g->runtime_names[Q1_NAME_RESOURCE_PLAYER_H2OJUMP_WAV], 4, 1, 1, error))
                return false;
        } else if (c->fall_speed < -650 && (!g->host.fall_damage_allowed ||
                                            g->host.fall_damage_allowed(g->host.context, actor))) {
            if (!q1_environment_damage(g, actor, 5, QA_HAZARD_FALL, error) ||
                !q1_sound_resource(g, actor, g->runtime_names[Q1_NAME_RESOURCE_PLAYER_LAND2_WAV], 2, 1, 1, error))
                return false;
        } else if (!q1_sound_resource(g, actor, g->runtime_names[Q1_NAME_RESOURCE_PLAYER_LAND_WAV], 2, 1, 1, error))
            return false;
        c->fall_speed = 0;
    }
    if (!grounded)
        c->fall_speed = body.velocity.z;
    return true;
}
bool qa_q1_character_environment(qa_q1_game *g, qa_actor_id actor, bool suit, bool noclip,
                                 qa_error *error) {
    q1_player *player = character(g, actor, error);
    if (!player)
        return false;
    q1_character *c = &player->character_state;
    if (c->life != QA_Q1_ALIVE || noclip)
        return true;
    bool lava_suit = player->power_expires[QA_Q1_LAVA_SUIT] > g->time;
    if (c->input.water_level != 3) {
        qa_string_id sound = c->air_until < g->time       ? g->runtime_names[Q1_NAME_RESOURCE_PLAYER_GASP2_WAV]
                            : c->air_until < g->time + 9 ? g->runtime_names[Q1_NAME_RESOURCE_PLAYER_GASP1_WAV]
                                                         : 0;
        if (sound && !q1_sound_resource(g, actor, sound, 2, 1, 1, error))
            return false;
        c->air_until = g->time + 12;
        c->drown_damage = 2;
    } else if (suit || lava_suit)
        c->air_until = g->time + 12;
    else if (c->air_until < g->time && c->pain_until < g->time) {
        c->drown_damage += 2;
        if (c->drown_damage > 15)
            c->drown_damage = 10;
        if (!q1_environment_damage(g, actor, c->drown_damage, QA_HAZARD_DROWN, error))
            return false;
        c->pain_until = g->time + 1;
    }
    if (!q1_alive(g, actor))
        return true;
    if (!c->input.water_level) {
        if (c->in_water && !q1_sound_resource(g, actor, g->runtime_names[Q1_NAME_RESOURCE_MISC_OUTWATER_WAV], 4, 1, 1, error))
            return false;
        c->in_water = false;
        return true;
    }
    if (c->hazard_at < g->time && c->input.water_type == -5 && !lava_suit) {
        c->hazard_at = g->time + (suit ? 1 : 0.2);
        if (!q1_environment_damage(g, actor, 10.0f * c->input.water_level, QA_HAZARD_LAVA, error))
            return false;
    } else if (c->hazard_at < g->time && c->input.water_type == -4 && !suit && !lava_suit) {
        c->hazard_at = g->time + 1;
        if (!q1_environment_damage(g, actor, 4.0f * c->input.water_level, QA_HAZARD_SLIME, error))
            return false;
    }
    if (!q1_alive(g, actor))
        return true;
    if (!c->in_water) {
        qa_string_id sound = c->input.water_type == -5   ? g->runtime_names[Q1_NAME_RESOURCE_PLAYER_INLAVA_WAV]
                            : c->input.water_type == -4 ? g->runtime_names[Q1_NAME_RESOURCE_PLAYER_SLIMBRN2_WAV]
                                                        : g->runtime_names[Q1_NAME_RESOURCE_PLAYER_INH2O_WAV];
        if (!q1_sound_resource(g, actor, sound, 4, 1, 1, error))
            return false;
        c->in_water = true;
        c->hazard_at = 0;
    }
    return true;
}

bool q1_spawn_bubble(qa_q1_game *g, qa_vec3 origin, qa_vec3 velocity, bool split, qa_error *error) {
    q1_actor *bubble;
    if (!q1_create(g, g->runtime_names[Q1_NAME_CLASS_BUBBLE], Q1_TIMER, (qa_actor_id){0}, &bubble, error))
        return false;
    bubble->physics.motion = QA_PHYSICS_NOCLIP;
    bubble->physics.solid = QA_PHYSICS_NOT_SOLID;
    bubble->frame = split ? 1 : 0;
    bubble->count = split ? 10 : 0;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, bubble->id, &body, error))
        return false;
    body.origin = origin;
    body.velocity = velocity;
    body.bounds = (qa_bounds){{-8, -8, -8}, {8, 8, 8}};
    return q1_model(g, bubble, "progs/s_bubble.spr", error) &&
           qa_world_body_write(g->services.world, bubble->id, &body, error) &&
           q1_link(g, bubble, error) && q1_schedule(g, bubble, 0.5, Q1_THINK_BUBBLE, error);
}
bool q1_character_bubbles(qa_q1_game *g, q1_actor *timer, qa_error *error) {
    q1_player *player = q1_player_get(g, q1_ref_actor(g, timer->owner));
    if (!player)
        return q1_remove(g, timer, error);
    uint8_t water_level =
        player->character ? player->character_state.input.water_level : player->input.water_level;
    if (water_level != 3)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, q1_ref_actor(g, timer->owner), &body, error))
        return false;
    if (!q1_spawn_bubble(g, qa_vec_add(body.origin, qa_v3(0, 0, 24)), qa_v3(0, 0, 15), false,
                         error))
        return false;
    timer->count -= 1;
    return timer->count <= 0 ? q1_remove(g, timer, error)
                             : q1_schedule(g, timer, 0.1, Q1_THINK_DEATH_BUBBLES, error);
}
bool q1_bubble_think(qa_q1_game *g, q1_actor *bubble, qa_error *error) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, bubble->id, &body, error))
        return false;
    bubble->count += 1;
    if (bubble->count == 4) {
        if (!q1_spawn_bubble(g, body.origin, body.velocity, true, error))
            return false;
        bubble->frame = 1;
        bubble->count = 10;
    }
    qa_point_query query = {.point = body.origin,
                            .policy = qa_collision_default_policy(QA_GAME_Q1)};
    qa_point_contents contents;
    if (!qa_world_point_contents(g->services.world, &query, &contents, error))
        return false;
    int32_t medium = qa_collision_point_contents_export(contents.contents, QA_GAME_Q1,
                                                   contents.q1_opaque_token);
    if (bubble->count >= 20 || (medium != -3 && medium != -4 && medium != -5))
        return q1_remove(g, bubble, error);
    float x = body.velocity.x - 10 + q1_random(g) * 20,
          y = body.velocity.y - 10 + q1_random(g) * 20,
          z = body.velocity.z + 10 + q1_random(g) * 10;
    body.velocity = qa_v3(x > 10    ? 5
                          : x < -10 ? -5
                                    : x,
                          y > 10    ? 5
                          : y < -10 ? -5
                                    : y,
                          z > 30   ? 25
                          : z < 10 ? 15
                                   : z);
    return qa_world_body_write(g->services.world, bubble->id, &body, error) &&
           q1_schedule(g, bubble, 0.5, Q1_THINK_BUBBLE, error);
}
