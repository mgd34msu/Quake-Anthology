#include "unified_native_q2_models.h"
#include "native_q2_appearance.h"
#include "unified_frame_private.h"
#include "internal.h"
#include "map_players_private.h"
#include "qa/game_q2.h"

static bool replacement_arsenal(qa_application *app, qa_actor_id actor,
    qa_actor_owner source, bool *out, qa_error *error)
{
    *out = false;
    application_provider *arsenal = application_provider_for(app, actor, QA_ROLE_ARSENAL, "");
    if (!arsenal || arsenal->owner == source) return true;
    for (size_t i = 0; i < app->players->count; ++i) {
        const application_player_record *player = app->players->records + i;
        if (!qa_actor_id_equal(player->actor, actor) || player->retiring || player->deferred ||
            player->source_begin_pending) continue;
        qa_item_id weapon;
        if (!qa_application_weapon_read(app, actor, &weapon, error)) return false;
        *out = true;
        break;
    }
    return true;
}

static bool model(qa_unified_frame_lease *lease, qa_unified_frame_visuals *out,
    const application_native_q2_appearance *view, unsigned index,
    const char *content, const char *path, bool held, qa_error *error)
{
    const qa_q2_entity *state = &view->entity.state;
    bool rerelease = view->source.edition == QA_Q2_RERELEASE;
    qa_unified_model_state *row = out->models + out->model_count++;
    *row = (qa_unified_model_state){.actor = view->entity.binding.actor, .family = QA_GAME_Q2,
        .visual.frame = (int32_t)state->frame, .visual.old_frame = (int32_t)(rerelease ? state->old_frame : state->frame),
        .visual.skin = index ? 0 : (int32_t)view->skin, .visual.effects = state->effects, .visual.render_flags = state->renderfx,
        .origin = qa_v3(state->origin[0], state->origin[1], state->origin[2]),
        .previous_origin = qa_v3(state->old_origin[0], state->old_origin[1], state->old_origin[2]),
        .angles = qa_v3(state->angles[0], state->angles[1], state->angles[2]),
        .visual.scale = rerelease && state->scale != 0 ? state->scale : 1,
        .visual.alpha = rerelease ? state->alpha != 0 ? state->alpha : (state->renderfx & 32u) ? .3f : 1 : 1,
        .has_previous_origin = true, .has_alpha = true, .visual.visible = true, .native_held_weapon = held};
    return application_unified_frame_string(lease, &row->content, content, error) &&
        application_unified_frame_string(lease, &row->path, path, error) &&
        application_unified_frame_string(lease, &row->skin_path, index || !*path ? NULL : qa_strings_cstr(qa_session_strings(view->source.session), view->skin_path), error);
}

bool application_unified_native_q2_models(qa_application *app,
    const application_unified_source *source, const qa_application_visual_visibility *visibility,
    qa_unified_frame *target,
    qa_unified_frame_visuals *out, qa_error *error)
{
    if (!target || !target->lease || !out || (out->model_count && !out->models) ||
        !source || !application_unified_source_current(app, source))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified Q2 models require their actual returned Source");
    qa_application_native_q2_presentation cut;
    bool found = false;
    bool ok = qa_application_native_q2_presentation_selected(app, &cut, &found, error);
    uint64_t actors = qa_actors_revision(qa_session_actors(source->session));
    if (ok && found && cut.kind == QA_APPLICATION_NATIVE_Q2_ORIGINAL) {
        const qa_product *product = qa_catalog_product(qa_launch_snapshot_catalog(source->launch), cut.content_product);
        uint32_t count = 0;
        if (cut.source_owner != source->owner || !product || !product->identity)
            ok = application_fail(error, QA_ERROR_ARGUMENT, "Unified Q2 models lost their physical content owner");
        if (ok) ok = qa_native_host_q2_wire_count((qa_native_host *)cut.source.original.host, &count, error);
        if (ok && count > 1) {
            size_t extra = (size_t)(count - 1) * 4;
            if (extra / 4 != count - 1 || extra > SIZE_MAX - out->model_count ||
                out->model_count + extra > SIZE_MAX / sizeof(*out->models))
                ok = application_fail(error, QA_ERROR_MEMORY, "Unified Q2 model roster exceeds allocation extent");
            else {
                qa_unified_model_state *models = qa_unified_frame_lease_alloc(target->lease,
                    out->model_count + extra, sizeof(*models), _Alignof(qa_unified_model_state), error);
                if (!models) ok = application_fail(error, QA_ERROR_MEMORY, "Retaining native Q2 model roster");
                else {
                    if (out->model_count) memcpy(models, out->models, out->model_count * sizeof(*models));
                    out->models = models;
                }
            }
        }
        for (uint32_t slot = 1; ok && slot < count; ++slot) {
            qa_native_host_q2_entity row;
            if (!qa_native_host_q2_wire_entity((qa_native_host *)cut.source.original.host, slot, &row, error)) { ok = false; break; }
            if (!row.in_use || !row.binding.actor.registry || (row.server_flags & 1u)) continue;
            application_provider *body = application_provider_for(app, row.binding.actor, QA_ROLE_BODY, "");
            if (body && body->owner != source->owner) continue;
            const qa_actor_record *actor = qa_actors_get(qa_session_actors(source->session), row.binding.actor);
            if (!actor || actor->owner != source->owner || !actor->has_source || actor->source_slot != slot) {
                ok = application_fail(error, QA_ERROR_ARGUMENT, "Unified Q2 model lost its full physical source actor"); break;
            }
            bool visible;
            if (!qa_application_visual_visibility_actor(visibility, row.binding.actor, NULL, &visible, error)) {
                ok = false; break;
            }
            if (!visible) continue;
            application_native_q2_appearance appearance = {0};
            ok = application_native_q2_appearance_read(app, &cut, slot, &appearance, error);
            bool replacement = false;
            if (ok) ok = replacement_arsenal(app, row.binding.actor, source->owner, &replacement, error);
            bool flat_beam = (appearance.entity.state.renderfx & 128u) &&
                !qa_q2_model_beam(appearance.source.edition, appearance.entity.state.renderfx,
                    appearance.entity.state.modelindex > 1);
            if (ok && flat_beam)
                ok = model(target->lease, out, &appearance, 0, product->identity, "", false, error);
            else for (unsigned i = 0; ok && i < 4; ++i) {
                const char *path = qa_strings_cstr(qa_session_strings(app->session), appearance.models[i]);
                if (path && *path)
                    ok = model(target->lease, out, &appearance, i, product->identity, path, i == 1 && replacement, error);
            }
            if (ok && !application_native_q2_appearance_current(app, &appearance))
                ok = application_fail(error, QA_ERROR_ARGUMENT, "Unified Q2 appearance changed while serializing");
            application_native_q2_appearance_dispose(&appearance);
        }
        if (ok && !qa_application_native_q2_presentation_current(app, &cut))
            ok = application_fail(error, QA_ERROR_ARGUMENT, "Unified Q2 model Source changed during observation");
    }
    if (ok && (!application_unified_source_current(app, source) ||
        qa_actors_revision(qa_session_actors(source->session)) != actors))
        ok = application_fail(error, QA_ERROR_ARGUMENT, "Unified Q2 model Source or actor roster changed");
    return ok;
}
