#include "unified_prediction.h"
#include "unified_frame_private.h"
#include "guest_q3_private.h"
#include "guest_q3_weapons.h"
#include "guest_q3_catalog.h"
#include "qa/application_qc_presentation.h"
#include "qa/application_native_q2_prediction.h"

static bool selected_item(qa_application *app, application_provider *p, qa_item_id item,
    const application_q3_catalog_weapon *original_q3, size_t original_q3_count)
{
    if (p == application_world_provider(app, QA_ROLE_ENTITIES, "")) return true;
    if (p->kind == APPLICATION_PROVIDER_Q1) {
        for (int i = 0; i < QA_Q1_WEAPON_COUNT; ++i)
            if (qa_q1_weapon_item(p->state.q1, (qa_q1_weapon)i) == item) return true;
        for (int i = 0; i < QA_Q1_AMMO_COUNT; ++i)
            if (qa_q1_ammo_item(p->state.q1, (qa_q1_ammo)i) == item) return true;
        return false;
    }
    if (p->kind == APPLICATION_PROVIDER_Q2) {
        const char *name = qa_strings_cstr(qa_session_strings(app->session), item);
        for (int i = 1; name && i < QA_Q2_WEAPON_COUNT; ++i) {
            const qa_q2_weapon_definition *v = qa_q2_weapon_definition_at(p->state.q2, (qa_q2_weapon)i);
            if (v && ((v->item && !strcmp(v->item, name)) || (v->ammo && !strcmp(v->ammo, name)))) return true;
        }
        return false;
    }
    if (p->kind == APPLICATION_PROVIDER_Q3) {
        for (int i = 1; i < QA_Q3_WEAPON_COUNT; ++i)
            if (qa_q3_weapon_item(p->state.q3, (qa_q3_weapon)i, false) == item ||
                qa_q3_weapon_item(p->state.q3, (qa_q3_weapon)i, true) == item) return true;
        return false;
    }
    if (p->kind == APPLICATION_PROVIDER_QVM) {
        for (size_t i = 0; i < original_q3_count; ++i)
            if (original_q3[i].item == item || original_q3[i].ammo == item) return true;
        return false;
    }
    return true;
}

static bool inventory(qa_application *app, application_provider *p,
    qa_unified_frame_prediction *out, qa_unified_frame_lease *lease,
    const qa_inventory_entry *entries, size_t count, qa_error *e)
{
    bool ok = true;
    const application_q3_catalog_weapon *original_q3 = NULL;
    size_t original_q3_count = 0;
    if (ok && p != application_world_provider(app, QA_ROLE_ENTITIES, "") && p->kind == APPLICATION_PROVIDER_QVM) {
        struct application_q3_guest *engine = q3g_engine(p);
        ok = engine && engine->game && engine->game->catalog &&
            application_q3_catalog_weapons(engine->game->catalog, &original_q3, &original_q3_count, e);
        if (!ok && (!e || e->code == QA_OK)) application_fail(e, QA_ERROR_NOT_FOUND, "Unified prediction lost its original Q3 item catalog");
    }
    if (ok && count) {
        out->ammo = application_unified_frame_alloc(lease, count, sizeof(*out->ammo), e);
        if (!out->ammo) ok = application_fail(e, QA_ERROR_MEMORY, "Retaining Unified prediction inventory");
    }
    for (size_t i = 0; ok && i < count; ++i) {
        const qa_inventory_entry *v = entries + i;
        if (!selected_item(app, p, v->item, original_q3, original_q3_count)) continue;
        qa_unified_inventory_entry *row = out->ammo + out->ammo_count++;
        row->count = v->count; row->capacity = v->capacity; row->policy = v->policy;
        ok = application_unified_frame_string(lease, &row->item,
            qa_strings_cstr(qa_session_strings(app->session), v->item), e);
    }
    return ok;
}

static bool weapon(qa_application *app, application_provider *p, qa_actor_id id,
    qa_unified_frame_prediction *out, qa_unified_frame_lease *lease,
    const qa_inventory_entry *entries, size_t count, qa_error *e)
{
    qa_item_id active;
    if (!p->launch || !qa_application_weapon_read(app, id, &active, e) ||
        !application_unified_frame_string(lease, &out->arsenal_provider, p->launch->selection.instance, e) ||
        !application_unified_frame_string(lease, &out->active_weapon,
            active ? qa_strings_cstr(qa_session_strings(app->session), active) : NULL, e)) return false;
    qa_unified_weapon_state *w = &out->weapon;
    if (p->kind == APPLICATION_PROVIDER_Q1) {
        qa_q1_player_view v;
        if (!qa_q1_player_read(p->state.q1, id, &v))
            return application_fail(e, QA_ERROR_NOT_FOUND, "Unified prediction lost its actual selected Q1 arsenal");
        w->kind = QA_UNIFIED_WEAPON_Q1; w->frame = v.weapon_frame;
        w->attack_finished_seconds = v.attack_finished; w->source_weapon = v.source_weapon;
    } else if (p->kind == APPLICATION_PROVIDER_QC) {
        qa_application_qc_animation v;
        if (!qa_application_qc_animation_read(app, id, QA_ROLE_ARSENAL, &v, e)) return false;
        w->kind = QA_UNIFIED_WEAPON_Q1; w->frame = v.frame;
        w->attack_finished_seconds = v.attack_finished_seconds; w->source_weapon = v.source_weapon;
        if (!qa_application_qc_animation_current(app, &v)) return false;
    } else if (p->kind == APPLICATION_PROVIDER_Q2) {
        qa_q2_weapon_state v;
        if (!qa_q2_weapon_read(p->state.q2, id, &v, e)) return false;
        w->kind = QA_UNIFIED_WEAPON_Q2; w->gun_frame = v.frame;
        switch (v.phase) {
        case QA_Q2_READY: w->state = 0; break;
        case QA_Q2_ACTIVATING: w->state = 1; break;
        case QA_Q2_DROPPING: w->state = 2; break;
        case QA_Q2_FIRING: w->state = 3; break;
        default: return application_fail(e, QA_ERROR_FORMAT, "Unified prediction has an unknown Q2 weapon phase");
        }
        const qa_q2_weapon_definition *pending = v.pending != QA_Q2_WEAPON_NONE ?
            qa_q2_weapon_definition_at(p->state.q2, v.pending) : NULL;
        if (v.pending != QA_Q2_WEAPON_NONE && (!pending || !pending->item))
            return application_fail(e, QA_ERROR_NOT_FOUND, "Unified prediction lost its actual pending Q2 weapon definition");
        if (pending && !application_unified_frame_string(lease, &w->pending_weapon, pending->item, e)) return false;
        w->machinegun_shots = v.machinegun_shots; w->grenade_seconds = (double)v.grenade_ns / 1e9;
        w->grenade_blew_up = v.grenade_blew_up;
    } else if (p->kind == APPLICATION_PROVIDER_NATIVE && p->state.native.q2_engine) {
        qa_application_native_q2_prediction v; bool found;
        if (!qa_application_native_q2_prediction_read(app, id, QA_ROLE_ARSENAL, &v, &found, e)) return false;
        if (!found) return application_fail(e, QA_ERROR_NOT_FOUND, "Unified prediction lost its original Q2 arsenal");
        w->kind = QA_UNIFIED_WEAPON_Q2; w->gun_frame = v.gun_frame; w->state = v.weapon_state;
        if (!application_unified_frame_string(lease, &w->pending_weapon,
            v.pending_weapon ? qa_strings_cstr(qa_session_strings(app->session), v.pending_weapon) : NULL, e)) return false;
        w->machinegun_shots = v.machinegun_shots; w->grenade_blew_up = v.grenade_blew_up;
        if (v.grenade_time_kind == QA_NATIVE_Q2_PREDICTION_SECONDS) w->grenade_seconds = v.grenade_time.seconds;
        else if (v.grenade_time_kind == QA_NATIVE_Q2_PREDICTION_MILLISECONDS) {
            w->grenade_milliseconds = true; w->grenade_time_ms = v.grenade_time.milliseconds;
        } else return application_fail(e, QA_ERROR_FORMAT, "Unified prediction has an unknown native Q2 grenade timer domain");
        if (!qa_application_native_q2_prediction_current(app, &v)) return false;
    } else if (p->kind == APPLICATION_PROVIDER_Q3 || p->kind == APPLICATION_PROVIDER_QVM || p->kind == APPLICATION_PROVIDER_NATIVE) {
        application_q3_weapon_prediction v;
        if (p->kind == APPLICATION_PROVIDER_Q3) {
            qa_q3_player_state source;
            if (!qa_q3_player_read(p->state.q3, id, &source))
                return application_fail(e, QA_ERROR_NOT_FOUND, "Unified prediction lost its actual selected Q3 arsenal player");
            v = (application_q3_weapon_prediction){(int32_t)source.weapon, (int32_t)source.weapon_phase, source.weapon_time_ms};
        } else {
            struct application_q3_guest *engine = q3g_engine(p);
            if (!engine || !engine->game) return application_fail(e, QA_ERROR_NOT_FOUND, "Unified prediction lost its actual original Q3 GAME");
            if (engine->game->weapons) {
                if (!application_q3_weapons_prediction_read(engine->game->weapons, id, &v, e)) return false;
            } else {
                qa_q3_player source; uint32_t slot;
                if (!application_q3_guest_actor_client(p, id, &slot) ||
                    !qa_q3_host_source_player(engine->game->host, slot, &source, e)) return false;
                v = (application_q3_weapon_prediction){source.weapon, source.weaponState, source.weaponTime};
            }
        }
        w->kind = QA_UNIFIED_WEAPON_Q3; w->source_weapon = v.source_weapon;
        w->state = v.state; w->time_ms = v.time_ms;
    } else return application_fail(e, QA_ERROR_UNSUPPORTED, "Unified prediction has no selected weapon continuation");
    return inventory(app, p, out, lease, entries, count, e);
}

static bool animation(qa_application *app, application_provider *p, qa_actor_id id,
    qa_unified_frame_prediction *out, qa_unified_frame_lease *lease, qa_error *e)
{
    if (!p->launch || !application_unified_frame_string(lease, &out->character_provider, p->launch->selection.instance, e)) return false;
    qa_unified_animation_state *a = &out->animation;
    if (p->kind == APPLICATION_PROVIDER_Q1) {
        qa_q1_character_view v;
        if (!qa_q1_character_read(p->state.q1, id, &v))
            return application_fail(e, QA_ERROR_NOT_FOUND, "Unified prediction lost its actual Q1 character animation");
        a->family = QA_GAME_Q1; a->frame = v.animation_frame; a->next_frame_seconds = v.next_frame_seconds;
    } else if (p->kind == APPLICATION_PROVIDER_QC) {
        qa_application_qc_animation v;
        if (!qa_application_qc_animation_read(app, id, QA_ROLE_CHARACTER, &v, e)) return false;
        a->family = QA_GAME_Q1; a->frame = v.frame; a->next_frame_seconds = v.next_frame_seconds;
        if (!qa_application_qc_animation_current(app, &v)) return false;
    } else if (p->kind == APPLICATION_PROVIDER_Q2) {
        qa_q2_player_animation_view v;
        if (!qa_q2_player_animation_read(p->state.q2, id, &v, e)) return false;
        a->family = QA_GAME_Q2; a->frame = v.frame; a->end_frame = v.end_frame; a->priority = v.priority;
        a->duck = v.duck; a->run = v.run;
    } else if (p->kind == APPLICATION_PROVIDER_NATIVE && p->state.native.q2_engine) {
        qa_application_native_q2_prediction v; bool found;
        if (!qa_application_native_q2_prediction_read(app, id, QA_ROLE_CHARACTER, &v, &found, e)) return false;
        if (!found) return application_fail(e, QA_ERROR_NOT_FOUND, "Unified prediction lost its original Q2 character");
        a->family = QA_GAME_Q2; a->frame = v.animation_frame; a->end_frame = v.animation_end;
        a->priority = v.animation_priority; a->duck = v.animation_duck; a->run = v.animation_run;
        if (!qa_application_native_q2_prediction_current(app, &v)) return false;
    } else if (p->kind == APPLICATION_PROVIDER_Q3) {
        qa_q3_player_state v;
        if (!qa_q3_player_read(p->state.q3, id, &v) || !(v.selections & QA_Q3_CHARACTER))
            return application_fail(e, QA_ERROR_NOT_FOUND, "Unified prediction lost its actual selected Q3 character animation");
        a->family = QA_GAME_Q3; a->legs = v.legs_animation; a->torso = v.torso_animation;
        a->legs_timer_ms = v.legs_timer_ms; a->torso_timer_ms = v.torso_timer_ms;
    } else if (p->kind == APPLICATION_PROVIDER_QVM || p->kind == APPLICATION_PROVIDER_NATIVE) {
        struct application_q3_guest *engine = q3g_engine(p); qa_q3_player v; uint32_t slot;
        if (!engine || !engine->game || !application_q3_guest_actor_client(p, id, &slot) ||
            !qa_q3_host_source_player(engine->game->host, slot, &v, e)) return false;
        a->family = QA_GAME_Q3; a->legs = v.legsAnim; a->torso = v.torsoAnim;
        a->legs_timer_ms = v.legsTimer; a->torso_timer_ms = v.torsoTimer;
    } else return application_fail(e, QA_ERROR_UNSUPPORTED, "Unified prediction requires its actual selected character animation continuation");
    return true;
}

bool application_unified_prediction_build(qa_application *app,
    const application_unified_source *source, qa_net_client_id client,
    const qa_unified_session_player *player, int64_t acknowledged_input,
    qa_unified_frame *frame, const qa_inventory_entry *entries, size_t entry_count, qa_error *e)
{
    if (!app || !source || !player || !frame || frame->prediction || acknowledged_input < -1 ||
        !application_unified_source_current(app, source) || !application_unified_player_current(app, client, player))
        return application_fail(e, QA_ERROR_ARGUMENT, "Unified prediction requires its actual completed Source player and acknowledgement");
    const qa_actor_registry *registry = qa_session_actors(source->session);
    uint64_t revision = qa_actors_revision(registry);
    qa_application_control_prediction_configuration configuration;
    if (!qa_application_control_prediction_read(app, player->actor, &configuration, e) ||
        !application_control_prediction_numeric_current(app, player->actor, &configuration.prediction_numeric, e)) return false;
    application_provider *m = application_provider_for(app, player->actor, QA_ROLE_MOVEMENT, "");
    application_provider *a = application_provider_for(app, player->actor, QA_ROLE_ARSENAL, "");
    application_provider *c = application_provider_for(app, player->actor, QA_ROLE_CHARACTER, "");
    if (!m || !a || !c || configuration.movement != m->owner || configuration.arsenal != a->owner ||
        configuration.character != c->owner || configuration.input.state.kind != player->movement)
        return application_fail(e, QA_ERROR_ARGUMENT, "Unified prediction lost its real selected movement, arsenal or character");
    const qa_launch_instance *md = m->launch, *ad = a->launch, *cd = c->launch;
    qa_unified_frame_lease *lease = frame->lease;
    qa_unified_frame_prediction *out = application_unified_frame_alloc(lease, 1, sizeof(*out), e);
    if (!out) return application_fail(e, QA_ERROR_MEMORY, "Retaining Unified prediction state");
    frame->prediction = out;
    const qa_movement_input *input = &configuration.input;
    const qa_application_movement_numeric *numeric = &configuration.prediction_numeric;
    out->actor = player->actor; out->sequence = acknowledged_input;
    out->command_time_ms = input->state.kind == QA_RULESET_Q3 ? input->state.data.q3.command_time_ms :
        (double)source->frame.time_ns / 1e6;
    out->clock = configuration.clock; out->profile = input->profile; out->state = input->state;
    out->numeric = (qa_unified_movement_numeric){.radix = numeric->radix,
        .scalar_mantissa_bits = numeric->scalar_mantissa_bits, .double_mantissa_bits = numeric->double_mantissa_bits,
        .evaluation_method = numeric->evaluation_method, .rounding = (int32_t)numeric->rounding,
        .native_c = numeric->native_c, .qw_origin_binary64 = numeric->qw_origin_binary64};
    if (!numeric->native_c) return application_fail(e, QA_ERROR_UNSUPPORTED, "Unified prediction has no genuine native C arithmetic recipe");
    out->standing = input->standing; out->crouched = input->crouched; out->dead = input->dead;
    out->invulnerability_bounds = input->invulnerability_bounds; out->bounds = input->current_bounds;
    out->view_angles = configuration.view_angles; out->view_height = configuration.view_height;
    out->view_offset = qa_v3(0, 0, configuration.view_height); out->environment = input->environment;
    out->has_client_view_offset = configuration.has_client_view_offset;
    out->client_view_offset = configuration.client_view_offset;
    out->ground = configuration.ground; out->water_level = configuration.water_level;
    out->water_type = configuration.water_type;
    out->has_rerelease_origin = input->state.kind == QA_RULESET_Q2_RERELEASE;
    if (out->has_rerelease_origin) out->rerelease_origin = configuration.q2r_pml_origin;
    bool ok = application_unified_frame_string(lease, &out->profile_id,
        qa_strings_cstr(qa_session_strings(app->session), configuration.profile_id), e) &&
        application_unified_frame_string(lease, &out->numeric.id,
        qa_strings_cstr(qa_session_strings(app->session), numeric->id), e) &&
        weapon(app, a, player->actor, out, lease, entries, entry_count, e) &&
        animation(app, c, player->actor, out, lease, e);
    if (ok) {
        ok = application_unified_source_current(app, source) && application_unified_player_current(app, client, player) &&
            qa_actors_revision(registry) == revision &&
            application_provider_for(app, player->actor, QA_ROLE_MOVEMENT, "") == m &&
            application_provider_for(app, player->actor, QA_ROLE_ARSENAL, "") == a &&
            application_provider_for(app, player->actor, QA_ROLE_CHARACTER, "") == c &&
            m->launch == md && a->launch == ad && c->launch == cd &&
            m->constructed && m->attached && !m->close_pending && a->constructed && a->attached && !a->close_pending &&
            c->constructed && c->attached && !c->close_pending &&
            application_control_prediction_numeric_current(app, player->actor, numeric, e);
        if (!ok) application_fail(e, QA_ERROR_ARGUMENT, "Unified prediction changed its Source, actor inventory or selected provider receipt");
    }
    return ok;
}
