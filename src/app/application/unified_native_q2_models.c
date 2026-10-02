#include "unified_native_q2_models.h"
#include "native_q2_appearance.h"
#include "unified_output_json.h"
#include "internal.h"
#include "map_players_private.h"

static bool text(application_unified_json *j, const char *s, qa_error *e)
{ return application_unified_json_text(j, s, e); }
static bool number(application_unified_json *j, double n, qa_error *e)
{ return application_unified_json_number(j, n, e); }
static bool vector(application_unified_json *j, const float v[3], qa_error *e)
{ return application_unified_json_vector(j, qa_v3(v[0], v[1], v[2]), e); }

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

static bool model(application_unified_json *j, bool *first,
    const application_native_q2_appearance *view, unsigned index,
    const char *content, bool held, qa_error *error)
{
    const qa_q2_entity *state = &view->entity.state;
    bool rerelease = view->source.edition == QA_Q2_RERELEASE;
    double scale = rerelease && state->scale != 0 ? state->scale : 1;
    double alpha = rerelease ? state->alpha != 0 ? state->alpha : (state->renderfx & 32u) ? .3 : 1 : 1;
    bool ok = (*first || text(j, ",", error)) && text(j, "{\"actor\":", error) &&
        application_unified_json_actor(j, view->entity.binding.actor, error) &&
        text(j, ",\"content\":", error) && application_unified_json_string(j, content, error) &&
        text(j, ",\"family\":\"q2\",\"path\":", error) &&
        application_unified_json_string(j, view->models[index], error) &&
        text(j, ",\"frame\":", error) && number(j, state->frame, error) &&
        text(j, ",\"oldFrame\":", error) && number(j, rerelease ? state->old_frame : state->frame, error) &&
        text(j, ",\"skin\":", error) && number(j, index ? 0 : view->skin, error) &&
        text(j, ",\"skinPath\":", error) &&
        (!index && view->skin_path ? application_unified_json_string(j, view->skin_path, error) : text(j, "null", error)) &&
        text(j, ",\"effects\":", error) && application_unified_json_natural(j, state->effects, error) &&
        text(j, ",\"renderFlags\":", error) && number(j, state->renderfx, error) &&
        text(j, ",\"origin\":", error) && vector(j, state->origin, error) &&
        text(j, ",\"previousOrigin\":", error) && vector(j, state->old_origin, error) &&
        text(j, ",\"angles\":", error) && vector(j, state->angles, error) &&
        text(j, ",\"scale\":", error) && number(j, scale, error) &&
        text(j, ",\"alpha\":", error) && number(j, alpha, error) &&
        text(j, ",\"visible\":true,\"viewWeapon\":false", error);
    if (ok && held) ok = text(j, ",\"nativeHeldWeapon\":true", error);
    if (ok) ok = text(j, "}", error);
    if (ok) *first = false;
    return ok;
}

bool application_unified_native_q2_models(qa_application *app,
    const application_unified_source *source, qa_unified_document **out, qa_error *error)
{
    if (!out || *out || !source || !application_unified_source_current(app, source))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified Q2 models require their actual returned Source");
    application_unified_json json = {0};
    bool first = true, ok = text(&json, "[", error);
    qa_application_native_q2_presentation cut;
    bool found = false;
    if (ok) ok = qa_application_native_q2_presentation_selected(app, &cut, &found, error);
    uint64_t actors = qa_actors_revision(qa_session_actors(source->session));
    if (ok && found && cut.kind == QA_APPLICATION_NATIVE_Q2_ORIGINAL) {
        const qa_product *product = qa_catalog_product(qa_launch_snapshot_catalog(source->launch), cut.content_product);
        uint32_t count = 0;
        if (cut.source_owner != source->owner || !product || !product->identity)
            ok = application_fail(error, QA_ERROR_ARGUMENT, "Unified Q2 models lost their physical content owner");
        if (ok) ok = qa_native_host_q2_wire_count((qa_native_host *)cut.source.original.host, &count, error);
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
            application_native_q2_appearance appearance = {0};
            ok = application_native_q2_appearance_read(app, &cut, slot, &appearance, error);
            bool replacement = false;
            if (ok) ok = replacement_arsenal(app, row.binding.actor, source->owner, &replacement, error);
            for (unsigned i = 0; ok && i < 4; ++i)
                if (appearance.models[i] && appearance.models[i][0])
                    ok = model(&json, &first, &appearance, i, product->identity, i == 1 && replacement, error);
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
    if (ok) ok = text(&json, "]", error) && qa_unified_document_create(QA_UNIFIED_CHECKPOINT,
        (qa_bytes){json.bytes.data, json.bytes.size}, out, error);
    application_unified_json_dispose(&json);
    return ok;
}
