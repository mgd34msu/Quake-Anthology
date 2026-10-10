#include "unified_presentations.h"
#include "unified_frame_private.h"
#include "unified_native_q2_models.h"
#include "map_players_private.h"
#include "equipment_gear_presentation.h"
#include "qa/application_equipment.h"
#include "qa/application_native_q3_presentation.h"
#include "qa/application_native_q2_presentation.h"
#include "qa/application_selected_q3_character.h"
#include "qa/application_q3_asset_selection.h"
#include "qa/game_q3_configstrings.h"
#include "qa/game_q3_source.h"
#include "qa/game_q1_bots.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const application_player_record *player_record(const qa_application *app, qa_actor_id actor)
{
    for (size_t i = 0; i < app->players->count; ++i)
        if (qa_actor_id_equal(app->players->records[i].actor, actor)) return app->players->records + i;
    return NULL;
}

static bool stable(qa_application *app, const application_unified_source *source,
    uint64_t actors, qa_error *error)
{
    return (application_unified_source_current(app, source) &&
        qa_actors_revision(qa_session_actors(source->session)) == actors) ||
        application_fail(error, QA_ERROR_ARGUMENT, "Unified presentations changed their Source or full actor roster");
}

static bool model(qa_unified_frame *frame, size_t *capacity,
    const qa_application_visual_view *v, const char *path, const char *content,
    const application_provider *render_source, bool view_weapon,
    const qa_application_equipment_view *equipment, const qa_launch_instance *equipment_source,
    qa_error *error)
{
    qa_unified_frame_visuals *out = frame->visuals;
    if (!path || !content || (unsigned)v->family >= 3)
        return application_fail(error, QA_ERROR_FORMAT, "Unified model lost its actual content, family or path");
    if (out->model_count == *capacity) {
        size_t next = *capacity ? *capacity * 2 : 32;
        if (next < *capacity || next > SIZE_MAX / sizeof(*out->models))
            return application_fail(error, QA_ERROR_MEMORY, "Unified model roster exceeds allocation extent");
        qa_unified_model_state *rows = application_unified_frame_alloc(frame->lease, next, sizeof(*rows), error);
        if (!rows) return application_fail(error, QA_ERROR_MEMORY, "Retaining actual model roster");
        if (out->model_count) memcpy(rows, out->models, out->model_count * sizeof(*rows));
        if (!frame->lease) free(out->models);
        out->models = rows; *capacity = next;
    }
    qa_unified_model_state *row = out->models + out->model_count++;
    *row = (qa_unified_model_state){.actor = v->actor, .family = v->family, .visual = v->visual,
        .q1_effects = v->q1_effects, .origin = v->body.origin, .angles = v->body.angles,
        .view_weapon = view_weapon, .has_previous_origin = (v->visual.render_flags & 128u) && v->family == QA_GAME_Q2,
        .previous_origin = v->previous_origin, .has_alpha = v->family != QA_GAME_Q3 && !view_weapon};
    if (row->visual.old_frame < 0) row->visual.old_frame = row->visual.frame;
    if (!application_unified_frame_string(frame->lease, &row->content, content, error) ||
        !application_unified_frame_string(frame->lease, &row->path, path, error) ||
        !application_unified_frame_string(frame->lease, &row->skin_path, v->skin_path, error)) return false;
    if (render_source) {
        row->render_source = application_unified_frame_alloc(frame->lease, 1, sizeof(*row->render_source), error);
        if (!row->render_source) return application_fail(error, QA_ERROR_MEMORY, "Retaining model Source identity");
        row->render_source->provider = render_source->owner;
        if (!application_unified_frame_string(frame->lease, &row->render_source->instance, render_source->launch->selection.instance, error)) return false;
    }
    if (equipment) {
        row->render_equipment = application_unified_frame_alloc(frame->lease, 1, sizeof(*row->render_equipment), error);
        if (!row->render_equipment) return application_fail(error, QA_ERROR_MEMORY, "Retaining equipment Source identity");
        row->render_equipment->provider = equipment->provider; row->equipment_slot = equipment->equipment_slot;
        if (!application_unified_frame_string(frame->lease, &row->render_equipment->instance, equipment_source->selection.instance, error)) return false;
    }
    if (v->has_flare) {
        const qa_entity_flare *f = &v->flare;
        row->flare = application_unified_frame_alloc(frame->lease, 1, sizeof(*row->flare), error);
        if (!row->flare) return application_fail(error, QA_ERROR_MEMORY, "Retaining actual Q2 flare");
        *row->flare = *f;
        char *image = NULL;
        if (!application_unified_frame_string(frame->lease, &image, f->image, error)) return false;
        row->flare->image = image;
    }
    return true;
}

static qa_trajectory trajectory(const qa_q3_trajectory *t)
{
    return (qa_trajectory){.type = (qa_trajectory_type)t->type, .time_ms = t->time,
        .duration_ms = t->duration, .base = qa_v3(t->base[0], t->base[1], t->base[2]),
        .delta = qa_v3(t->delta[0], t->delta[1], t->delta[2])};
}

static const char *missile(int32_t weapon)
{
    switch (weapon) {
    case QA_Q3_W_GRAPPLE: case QA_Q3_W_ROCKET: return "models/ammo/rocket/rocket.md3";
    case QA_Q3_W_GRENADE: return "models/ammo/grenade1.md3";
    case QA_Q3_W_PROX: return "models/weaphits/proxmine.md3";
    case QA_Q3_W_NAIL: return "models/weaphits/nail.md3";
    case QA_Q3_W_BFG: return "models/weaphits/bfg.md3";
    default: return NULL;
    }
}

static bool q3_provider_models(qa_application *app, const application_unified_source *source,
    application_provider *provider, const qa_application_visual_visibility *visibility,
    uint64_t actors, qa_unified_frame *frame,
    size_t *capacity, qa_error *error)
{
    qa_q3_game *game = provider->state.q3;
    const qa_product *product = provider->product;
    const qa_launch_instance *launch = provider->launch;
    if (!game || !launch || !product || !product->identity ||
        !provider->constructed || !provider->attached || provider->close_pending ||
        !qa_q3_destroy_ready(game))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified Q3 models lost their real published GAME owner");
    qa_q3_product edition; int32_t time, start; uint32_t count;
    if (!qa_q3_source_match_context_read(game, &edition, &start, error) ||
        !qa_q3_source_clock(game, &time, error) ||
        !qa_q3_source_entity_count(game, &count, error)) return false;
    size_t item_count;
    const qa_q3_item *items = qa_q3_items(edition, &item_count);
    for (uint32_t slot = 0; slot < count; ++slot) {
        struct { qa_q3_source_binding binding; qa_q3_entity state; qa_q3_wire_visibility visibility; } row;
        if (!qa_q3_source_binding_read(game, slot, &row.binding, error)) return false;
        if (!row.binding.in_use) continue;
        if (!qa_q3_wire_entity_read(game, slot, &row.state, &row.visibility, error)) return false;
        if (!row.visibility.present || row.binding.client_slot >= 0 ||
            !row.visibility.linked || (row.visibility.server_flags & 1u) || ((uint32_t)row.state.eFlags & 128u)) continue;
        bool visible;
        if (!qa_application_visual_visibility_actor(visibility, row.binding.actor, NULL, &visible, error)) return false;
        if (!visible) continue;
        const char *paths[2] = {0};
        char inline_path[32];
        if (row.state.eType == 2) {
            if (row.state.modelindex < 0 || (size_t)row.state.modelindex >= item_count)
                return application_fail(error, QA_ERROR_FORMAT, "Unified native item has an invalid real source index");
            paths[0] = items[row.state.modelindex].model;
            paths[1] = items[row.state.modelindex].secondary_model;
        } else if (row.state.eType == 3) paths[0] = missile(row.state.weapon);
        else {
            if (row.state.eType == 4 && row.state.solid == 0xffffff) {
                int written = snprintf(inline_path, sizeof(inline_path), "*%u", (uint32_t)row.state.modelindex);
                if (written < 0 || (size_t)written >= sizeof(inline_path))
                    return application_fail(error, QA_ERROR_FORMAT, "Unified native inline model exceeds its authored path");
                paths[0] = inline_path;
            } else if (row.state.modelindex > 0) {
                if ((uint32_t)row.state.modelindex >= 256)
                    return application_fail(error, QA_ERROR_FORMAT, "Unified native model exceeds the source model configstrings");
                if (!qa_q3_configstring_read(game, 32u + (uint32_t)row.state.modelindex,
                        &paths[0], error)) return false;
            }
            if (row.state.eType == 4 && row.state.modelindex2 > 0) {
                if ((uint32_t)row.state.modelindex2 >= 256)
                    return application_fail(error, QA_ERROR_FORMAT, "Unified native model exceeds the source model configstrings");
                if (!qa_q3_configstring_read(game, 32u + (uint32_t)row.state.modelindex2,
                        &paths[1], error)) return false;
            }
        }
        qa_application_visual_view v = {.actor = row.binding.actor, .family = QA_GAME_Q3,
            .visual.frame = row.state.frame, .visual.old_frame = row.state.frame, .visual.effects = (uint32_t)row.state.eFlags,
            .visual.scale = 1, .visual.alpha = 1, .visual.visible = true};
        qa_trajectory position = trajectory(&row.state.pos), angular = trajectory(&row.state.apos);
        if (!qa_trajectory_position(&position, time, 800, &v.body.origin, error) ||
            !qa_trajectory_position(&angular, time, 800, &v.body.angles, error) ||
            !stable(app, source, actors, error)) return false;
        for (unsigned i = 0; i < 2; ++i)
            if (paths[i] && paths[i][0] && !model(frame, capacity, &v, paths[i], product->identity, provider, false,NULL,NULL,error)) return false;
    }
    int32_t after; uint32_t extent;
    return (stable(app, source, actors, error) && provider->constructed && provider->attached &&
        !provider->close_pending && provider->state.q3 == game && provider->launch == launch &&
        provider->product == product && qa_q3_source_clock(game, &after, error) && after == time &&
        qa_q3_source_entity_count(game, &extent, error) && extent == count) ||
        application_fail(error, QA_ERROR_ARGUMENT, "Unified native model source changed during observation");
}

static bool q3_models(qa_application *app, const application_unified_source *source,
    const qa_application_visual_visibility *visibility, uint64_t actors,
    qa_unified_frame *frame, size_t *capacity, qa_error *error)
{
    for (size_t i = 0; i < app->provider_count; ++i) {
        application_provider *p = app->providers[i];
        if (p->kind == APPLICATION_PROVIDER_Q3 && p->constructed && p->attached && !p->close_pending &&
            !q3_provider_models(app, source, p, visibility, actors, frame, capacity, error)) return false;
    }
    return true;
}

static bool character(qa_unified_frame_visuals *out,
    const qa_application_selected_q3_character *v, const qa_q3_player_state *p)
{
    qa_unified_character_state *row = out->characters + out->character_count++;
    *row = (qa_unified_character_state){.actor = v->actor, .origin = v->body.origin,
        .angles = v->view_angles, .velocity = v->body.velocity, .movement_direction = v->movement_direction,
        .legs = v->legs_animation, .torso = v->torso_animation,
        .legs_timer_ms = p->legs_timer_ms, .torso_timer_ms = p->torso_timer_ms,
        .source_flags = v->source_flags, .color = {1, 1, 1, 1}, .scale = v->scale, .opacity = v->opacity};
    return true;
}

static bool equipment(qa_application *app, const application_unified_source *source, qa_actor_id actor,
    qa_actor_id recipient, qa_unified_frame *frame, size_t *capacity, qa_error *error)
{
    qa_unified_frame_visuals *out = frame->visuals;
    qa_application_equipment_view e;
    if (!qa_application_equipment_read(app, actor, &e, error)) return false;
    const char *model_name = qa_strings_cstr(qa_session_strings(app->session), e.view_model);
    if (!model_name || !*model_name) return true;
    application_provider *provider = NULL;
    for (size_t i = 0; i < app->provider_count; ++i)
        if (app->providers[i]->owner == e.provider) provider = app->providers[i];
    application_equipment_gear_presentation gear = {0}; bool gear_selected = false;
    const qa_product *product = provider ? provider->product : NULL;
    if (e.equipment_slot) {
        if (!application_equipment_gear_presentation_read(app, actor, &gear, &gear_selected, error)) return false;
        if (gear_selected)
            product = qa_catalog_product(qa_launch_snapshot_catalog(source->launch), gear.source.descriptor->selection.product);
    }
    if (!product || !product->identity || (!provider && !gear_selected))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Unified view weapon lost its true selected content owner");
    const qa_launch_instance *equipment_source=gear_selected?gear.source.descriptor:provider->launch;
    if(!equipment_source || !equipment_source->selection.instance)
        return application_fail(error,QA_ERROR_NOT_FOUND,"Unified view weapon lost its actual selected descriptor");
    qa_application_camera_view camera;
    if (!qa_application_control_camera(app, actor, &camera))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Unified view weapon lost its actual player camera");
    application_provider *primary = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (!primary) return application_fail(error, QA_ERROR_NOT_FOUND, "Unified view weapon lost its actual Source");
    bool intermission = false;
    if (!application_source_intermission_read(primary, &intermission, error)) return false;
    if (camera.cutscene || intermission) return true;
    bool local = qa_actor_id_equal(actor, recipient);
    qa_application_visual_view v = {.actor = actor, .family = e.family,
        .visual.frame = local && e.has_frame ? e.frame : 0, .visual.old_frame = local && e.has_frame ? e.frame : 0,
        .visual.skin = local && e.has_skin ? e.skin : 0, .visual.scale = 1, .visual.alpha = 1,
        .visual.render_flags = local && e.family == QA_GAME_Q2 && provider && provider->kind != APPLICATION_PROVIDER_NATIVE ? 21u : 0u,
        .visual.visible = local && e.visible};
    if (local) {
        v.body.origin = qa_vec_add(camera.origin, camera.view_offset);
        v.body.origin = qa_vec_add(v.body.origin, e.has_source_gun_pose ? e.gun_origin : e.kick_origin);
        v.body.angles = qa_vec_add(camera.angles, e.has_source_gun_pose ? e.gun_angles : e.kick_angles);
    }
    if (!application_unified_source_current(app, source) || !qa_application_equipment_current(app, &e))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified view weapon changed its actual Source or selected owner");
    /* Q3's public weapon continuation belongs to the actual selected owner. */
    if (e.family == QA_GAME_Q3) {
        qa_player_state control;
        qa_combat_state combat;
        qa_body_state body;
        if (!qa_application_control_read(app, actor, &control) ||
            !qa_combat_read_traits(app->combat, actor, &combat, error) ||
            !qa_world_body_read(source->world, actor, &body, error))
            return application_fail(error, QA_ERROR_NOT_FOUND, "Unified Q3 weapon lost its actual command or motion owner");
        bool firing = (control.buttons & 1u) != 0 && combat.health > 0;
        if (!model(frame, capacity, &v, model_name, product->identity, NULL, true, &e, equipment_source, error)) return false;
        qa_unified_model_state *row = out->models + out->model_count - 1;
        row->q3_weapon = application_unified_frame_alloc(frame->lease, 1, sizeof(*row->q3_weapon), error);
        if (!row->q3_weapon) return application_fail(error, QA_ERROR_MEMORY, "Retaining actual Q3 weapon presentation");
        *row->q3_weapon = (qa_unified_q3_weapon_view){.time_ms = e.q3_time_ms,
            .torso_animation = local && e.has_q3_source ? e.q3_source.torsoAnim : 0,
            .has_last_fire_ms = e.q3_fire.present, .last_fire_ms = e.q3_fire.time_ms,
            .firing = firing, .horizontal_speed = local ? (float)hypot(body.velocity.x, body.velocity.y) : 0,
            .bob_cycle = local && e.has_q3_source ? e.q3_source.bobCycle : 0, .weapon = (int32_t)e.q3_weapon};
        if (e.item && !application_unified_frame_string(frame->lease, &row->weapon_item,
            qa_strings_cstr(qa_session_strings(source->session), e.item), error)) return false;
        if (local && gear_selected) {
            const application_q3_grapple_definition *d = gear.source.definition;
            if (d->presentation.anchor_path && d->presentation.anchor_path[0]) {
                row->anchor = application_unified_frame_alloc(frame->lease, 1, sizeof(*row->anchor), error);
                if (!row->anchor) return application_fail(error, QA_ERROR_MEMORY, "Retaining actual equipment anchor");
                row->anchor->offset = d->presentation.anchor_offset;
                row->anchor->fov_above = d->presentation.fov_above; row->anchor->fov_scale = d->presentation.fov_scale;
                if (!application_unified_frame_string(frame->lease, &row->anchor->path, d->presentation.anchor_path, error) ||
                    !application_unified_frame_string(frame->lease, &row->anchor->tag, d->presentation.anchor_tag, error)) return false;
            }
            size_t count = d->presentation.attachment_count;
            row->attachments = count ? application_unified_frame_alloc(frame->lease, count, sizeof(*row->attachments), error) : NULL;
            if (count && !row->attachments) return application_fail(error, QA_ERROR_MEMORY, "Retaining actual equipment attachments");
            for (size_t i = 0; i < count; ++i) {
                qa_unified_model_attachment *a = row->attachments + row->attachment_count++;
                if (!application_unified_frame_string(frame->lease, &a->path, d->presentation.attachments[i].path, error) ||
                    !application_unified_frame_string(frame->lease, &a->tag, d->presentation.attachments[i].tag, error)) return false;
            }
            if (!application_equipment_gear_presentation_current(app, &gear))
                return application_fail(error, QA_ERROR_ARGUMENT, "Unified authored gear model changed its retained declaration");
        }
        return true;
    }

    return model(frame, capacity, &v, model_name, product->identity, NULL, true,&e,equipment_source,error);
}

bool application_unified_presentations_build(qa_application *app, const application_unified_source *source,
    qa_net_client_id recipient, const qa_unified_session_player *player,
    qa_application_visual_visibility *visibility, qa_unified_frame *frame, application_unified_presentations *out, qa_error *error)
{
    if (!out || out->value || !frame || frame->visuals || !source || !player ||
        !application_unified_source_current(app, source) || !application_unified_player_current(app, recipient, player))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified presentations require their actual Source recipient");
    application_unified_presentations candidate = {.source = *source,
        .actors_revision = qa_actors_revision(qa_session_actors(source->session))};
    qa_unified_frame_visuals *visuals = application_unified_frame_alloc(frame->lease, 1, sizeof(*visuals), error);
    if (!visuals) return application_fail(error, QA_ERROR_MEMORY, "Retaining actual Unified visuals");
    frame->visuals = visuals; candidate.value = visuals;
    qa_application_camera_view camera;
    if (!qa_application_control_camera(app, player->actor, &camera))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Unified model visibility lost its actual Source camera");
    if (!qa_application_visual_visibility_prepare(app, player->actor,
        qa_vec_add(camera.origin, camera.view_offset), false, visibility, error)) return false;
    size_t count = qa_actors_count(qa_session_actors(source->session));
    visuals->characters = count ? application_unified_frame_alloc(frame->lease, count, sizeof(*visuals->characters), error) : NULL;
    if (count && !visuals->characters) return application_fail(error, QA_ERROR_MEMORY, "Retaining actual character roster");
    qa_application_native_q2_presentation q2;
    bool q2_found = false;
    bool ok = qa_application_native_q2_presentation_selected(app, &q2, &q2_found, error) &&
        application_unified_native_q2_models(app, source, visibility, frame, visuals, error);
    size_t model_capacity = visuals->model_count;
    if (ok) ok = q3_models(app, source, visibility, candidate.actors_revision, frame, &model_capacity, error);
    uint32_t cursor = 0; const qa_actor_record *record;
    while (ok && qa_actors_next(qa_session_actors(source->session), &cursor, &record)) {
        qa_actor_id id = record->id;
        const application_player_record *admitted = player_record(app, id);
        if (admitted && (admitted->retiring || admitted->deferred || admitted->source_begin_pending)) continue;
        qa_application_selected_q3_character c; bool found;
        if (!qa_application_selected_q3_character_read(app, id, &c, &found, error)) { ok = false; break; }
        if (found && c.present) {
            bool visible;
            if (!qa_application_visual_visibility_actor(visibility, id, NULL, &visible, error)) { ok = false; break; }
            if (!visible) continue;
            qa_application_q3_asset_selection appearance; bool appearance_found;
            if (!qa_application_q3_asset_selection_read(app, id, QA_ROLE_SKIN,
                    &appearance, &appearance_found, error)) { ok = false; break; }
            uint32_t physical_client;
            /* Original CG owns this physical client's model/hmodel through CS_PLAYERS. */
            bool source_character = appearance_found && c.provider == source->owner &&
                appearance.provider == source->owner &&
                qa_q3_native_client_slot(c.game, id, &physical_client, NULL);
            if (!source_character) {
                qa_q3_player_state p;
                if (!qa_q3_player_read(c.game, id, &p)) {
                    ok = application_fail(error, QA_ERROR_ARGUMENT, "Unified character lost its actual selected player"); break;
                }
                if (!character(visuals, &c, &p)) { ok = false; break; }
            }
            if (!qa_application_selected_q3_character_current(app, &c)) {
                ok = application_fail(error, QA_ERROR_ARGUMENT, "Unified character changed its actual selected continuation"); break;
            }
        }
        application_provider *body = application_provider_for(app, id, QA_ROLE_BODY, "");
        qa_mode_object_view object;
        uint32_t physical_slot;
        bool q3_physical = body && body->kind == APPLICATION_PROVIDER_Q3 && body->state.q3 &&
            qa_q3_source_actor_slot(body->state.q3, id, &physical_slot, NULL);
        bool mode_model = app->modes && qa_modes_object_read(app->modes, id, &object) &&
            !q3_physical;
        bool physical_q2 = q2_found && q2.kind == QA_APPLICATION_NATIVE_Q2_ORIGINAL &&
            body && body->owner == source->owner;
        if (body && !physical_q2 && (body->kind != APPLICATION_PROVIDER_Q3 || mode_model) && !(found && c.present) &&
            qa_world_body_storage_serial(source->world, id)) {
            qa_application_visual_view v; qa_error observed = {0};
            bool visible = qa_application_visual_read(app, id, &v, &observed);
            if (!visible && observed.code != QA_OK && observed.code != QA_ERROR_NOT_FOUND) {
                if (error) *error = observed;
                ok = false; break;
            }
            if (visible) {
                bool recipient_visible;
                if (!qa_application_visual_visibility_actor(visibility, id, &v, &recipient_visible, error)) {
                    ok = false; break;
                }
                if (!recipient_visible) continue;
                const qa_product *content = qa_catalog_product(qa_launch_snapshot_catalog(source->launch), v.content);
                if (!content) { ok = application_fail(error, QA_ERROR_NOT_FOUND, "Unified model lost its actual content product"); break; }
                qa_application_map_view map;
                if (!qa_application_map_read(app, &map)) { ok = false; break; }
                if (v.family == QA_GAME_Q2 && (v.visual.render_flags & 128u) && !v.model_beam)
                    ok = model(frame, &model_capacity, &v, "", content->identity, NULL, false,NULL,NULL,error);
                else for (unsigned i = 0; ok && i < 4; ++i) {
                    const char *path = qa_strings_cstr(qa_session_strings(app->session), v.visual.models[i]);
                    if (path && *path && strcmp(path, qa_resource_path(map.resource)))
                        ok = model(frame, &model_capacity, &v, path, content->identity, NULL, false,NULL,NULL,error);
                }
                if (ok && v.has_flare)
                    ok = model(frame, &model_capacity, &v, "", content->identity, NULL, false,NULL,NULL,error);
            }
        }
        if (ok && admitted) ok = equipment(app, source, id, player->actor, frame, &model_capacity, error);
        if (ok) ok = stable(app, source, candidate.actors_revision, error);
    }
    if (ok) ok = application_unified_player_current(app, recipient, player) &&
        stable(app, source, candidate.actors_revision, error);
    if (!ok) return false;
    *out = candidate;
    return true;
}

bool application_unified_presentations_current(qa_application *app, const application_unified_presentations *v)
{ return v && v->value && stable(app, &v->source, v->actors_revision, NULL); }
