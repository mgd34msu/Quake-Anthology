#include "internal.h"

bool qa_q2_character_configure(qa_q2_game *g, qa_actor_id id, qa_string_id model,
                               int skin, qa_error *e) {
    q2_actor *a = q2_client(g, id, e);
    const char *path = g ? qa_strings_cstr(qa_session_strings(g->services.session), model) : NULL;
    if (!a || a->client->rule.corpse || a->client->rule.awaiting_respawn || !model || !path ||
        !*path || skin < 0 || !g->player_runtime->services.request_respawn ||
        !qa_world_body_storage_serial(g->services.world, id) || g->continuation_pending ||
        g->continuation_failed || g->restoring_continuation) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0,
                     "Q2 CHARACTER requires an admitted campaign actor and authored appearance");
        return false;
    }
    q2_client_state *s = a->client;
    if (s->rule.character_configured &&
        (s->rule.character_model != model || s->rule.character_skin != skin)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 CHARACTER appearance is already configured");
        return false;
    }
    s->rule.character_model = model;
    s->rule.character_skin = skin;
    s->rule.character_configured = true;
    return true;
}

static bool respawned(void *context, qa_actor_id id, qa_error *e) {
    qa_q2_game *g = context;
    q2_actor *a = q2_client(g, id, e);
    qa_clock_state clock;
    if (!a || a->client->rule.corpse || !a->client->rule.character_configured ||
        a->client->rule.awaiting_respawn || !g->player_runtime->services.request_respawn ||
        !qa_world_body_storage_serial(g->services.world, id) ||
        !qa_session_clock(g->services.session, g->options.owner, &clock) ||
        clock.frame.provider != g->options.owner ||
        clock.frame.kind != (g->options.edition == QA_Q2_RERELEASE
                                 ? QA_RULESET_Q2_RERELEASE : QA_RULESET_Q2_CLASSIC) ||
        clock.frame.time_ns > UINT64_MAX - 12 * Q2_NS ||
        a->character_birth_epoch == UINT64_MAX) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0,
                     "Q2 CHARACTER respawn requires its actual campaign member and source clock");
        return false;
    }
    q2_client_state *s = a->client;
    const char *model = qa_strings_cstr(qa_session_strings(g->services.session), s->rule.character_model);
    if (!model || !*model || s->rule.character_skin < 0) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 CHARACTER lost its authored appearance");
        return false;
    }
    g->now_ns = clock.frame.time_ns;
    ++a->character_birth_epoch;
    a->pickup_icon = a->pickup_text = a->selected_item_name = 0;
    a->pickup_until_ns = a->selected_item_name_until_ns = 0;
    a->character_no_damage_effects = false;
    s->info.dead = s->rule.gibbed = false;
    s->rule.respawn_ns = g->now_ns;
    s->rule.air_ns = g->now_ns + 12 * Q2_NS;
    s->rule.drown_damage = 2;
    s->rule.old_water = 0;
    s->rule.old_velocity = qa_v3(0, 0, 0);
    s->rule.damage_alpha = s->rule.bonus_alpha = 0;
    s->rule.fall_ns = s->rule.damage_ns = 0;
    s->rule.damage_blood = s->rule.damage_armor = s->rule.damage_power = s->rule.damage_knockback = 0;
    s->rule.animation_priority = 0;
    s->rule.animation_end = 39;
    s->rule.visual.frame = 0;
    s->rule.visual.models[0] = s->rule.character_model;
    s->rule.visual.skin = s->rule.character_skin;
    s->rule.visual.effects = 0;
    s->rule.visual.render_flags = g->options.edition == QA_Q2_RERELEASE ? 32768 : 0;
    s->info.view_height = 22;
    s->rule.event = 6;
    return q2_publish_visual(g, id, &s->rule.visual, e);
}

bool qa_q2_character_respawned(qa_q2_game *g, qa_actor_id id, qa_error *e) {
    if (g && g->restoring_continuation) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 CHARACTER birth cannot run during restoration");
        return false;
    }
    return qa_q2_run_actor(g, id, respawned, g, e);
}
