#include "guest_native_q2_private.h"
#include "map_players_private.h"
#include "qa/application_native_q2_client.h"

bool qa_application_native_q2_presentation_local(qa_application *app,
    const qa_application_native_q2_presentation *source, uint32_t seat,
    qa_application_native_q2_client *out, bool *found, qa_error *error)
{
    if (!out || !found || !qa_application_native_q2_presentation_current(app, source))
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Q2 local recipient requires its completed physical GAME source");
    *found = false;
    application_provider *provider = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (!app->players) return true;
    if (!provider || app->players->map_provider != provider || provider->owner != source->source_owner)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Q2 local recipient lost its actual world player roster");
    qa_application_native_q2_client client = {0};
    bool present = false;
    for (size_t i = 0; i < app->players->count; ++i) {
        const application_player_record *row = &app->players->records[i];
        if (row->seat != seat || row->retiring || row->remote || application_player_identity(row)->bot || row->source_begin_pending ||
            !qa_actors_get(qa_session_actors(app->session), row->actor)) continue;
        if (source->kind == QA_APPLICATION_NATIVE_Q2_BUILTIN) {
            qa_builtin_player_info physical;
            if (provider->kind != APPLICATION_PROVIDER_Q2 || provider->state.q2 != source->source.game ||
                !qa_q2_player_projection(provider->state.q2, row->actor, NULL, &physical) ||
                !physical.connected || physical.slot != row->client_slot)
                return application_fail(error, QA_ERROR_ARGUMENT,
                    "Q2 local recipient differs from its physical GAME client");
        } else {
            const struct application_native_q2 *engine = provider->state.native.q2_engine;
            if (provider->kind != APPLICATION_PROVIDER_NATIVE || !engine ||
                provider->state.native.host != source->source.original.host || row->client_slot >= 256)
                return application_fail(error, QA_ERROR_ARGUMENT,
                    "Q2 local recipient lost its original GAME source");
            uint32_t slot = row->client_slot + 1;
            const application_native_q2_client *physical = engine->clients + slot;
            qa_native_slot_binding binding;
            if (!physical->connected || !physical->begun || physical->bot || physical->disconnect_started)
                continue;
            if (!qa_actor_id_equal(physical->actor, row->actor) ||
                !qa_native_slot(qa_native_host_instance(provider->state.native.host), slot, &binding, error) ||
                binding.kind == QA_NATIVE_SLOT_FREE || !qa_actor_id_equal(binding.actor, row->actor))
                return application_fail(error, QA_ERROR_ARGUMENT,
                    "Q2 local recipient differs from its full original source binding");
        }
        if (present)
            return application_fail(error, QA_ERROR_FORMAT, "Q2 local recipient repeats its actual viewing seat");
        client = (qa_application_native_q2_client){row->actor, seat, row->client_slot};
        present = true;
    }
    if (!qa_application_native_q2_presentation_current(app, source))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 local recipient left its completed source frame");
    if (present) { *out = client; *found = true; }
    return true;
}

bool qa_application_native_q2_presentation_player(qa_application *app,
    const qa_application_native_q2_presentation *source,
    const qa_application_native_q2_client *client,
    qa_application_native_q2_player_sample *out,qa_error *error)
{
    qa_application_native_q2_client actual;
    bool found;
    if (!client || !out ||
        !qa_application_native_q2_presentation_local(app,source,client->seat,&actual,&found,error) ||
        !found || actual.client_slot!=client->client_slot || !qa_actor_id_equal(actual.actor,client->actor))
        return application_fail(error,QA_ERROR_ARGUMENT,"Q2 player sample lost its physical local client");
    qa_application_native_q2_player_sample value={.edition=source->edition};
    if (source->kind==QA_APPLICATION_NATIVE_Q2_ORIGINAL) {
        qa_q2_player player;
        if (!qa_native_host_q2_wire_player((qa_native_host *)source->source.original.host,
                client->client_slot+1,client->actor,&player,error)) return false;
        value=(qa_application_native_q2_player_sample){
            .origin={player.pmove.origin_f[0],player.pmove.origin_f[1],player.pmove.origin_f[2]},
            .view_angles={player.viewangles[0],player.viewangles[1],player.viewangles[2]},
            .view_offset={player.viewoffset[0],player.viewoffset[1],player.viewoffset[2]},
            .gun_offset={player.gunoffset[0],player.gunoffset[1],player.gunoffset[2]},
            .kick_angles={player.kick_angles[0],player.kick_angles[1],player.kick_angles[2]},
            .gun_angles={player.gunangles[0],player.gunangles[1],player.gunangles[2]},
            .gun_model=player.gunindex,.gun_frame=player.gunframe,.gun_rate=(float)player.gunrate,
            .fov=player.fov,.view_height=(float)player.pmove.viewheight,
            .movement_type=player.pmove.type,.movement_flags=(uint32_t)player.pmove.flags,
            .render_flags=player.rdflags,.edition=source->edition,.present=true};
    } else {
        qa_q2_wire_view view;
        if (!qa_q2_wire_view_read(source->source.game,client->actor,&view,error)) return false;
        if (view.present) {
            qa_movement_result movement;
            if (!qa_q2_player_movement_read(source->source.game,client->actor,&movement, NULL, error)) return false;
            application_provider *arsenal = application_provider_for(app, client->actor, QA_ROLE_ARSENAL, "");
            if (!arsenal || !arsenal->constructed || !arsenal->attached || arsenal->close_pending)
                return application_fail(error, QA_ERROR_ARGUMENT, "Q2 player sample lost its selected arsenal");
            qa_q2_weapon_state weapon = {0};
            if (arsenal->kind == APPLICATION_PROVIDER_Q2 &&
                !qa_q2_weapon_read(arsenal->state.q2, client->actor, &weapon, error)) return false;
            value=(qa_application_native_q2_player_sample){.origin=qa_movement_origin(&movement.state),
                .view_angles=view.view.angles,.view_offset=view.view.offset,
                .kick_angles=view.view.kick_angles,.gun_angles=view.view.gun_angles,
                .gun_offset=view.view.gun_offset,.fov=view.view.fov,
                .gun_model=weapon.view_model,.gun_frame=weapon.frame>=0 ? (uint32_t)weapon.frame : 0,
                .gun_rate=weapon.gun_rate,
                .movement_type=source->edition==QA_Q2_CLASSIC ? movement.state.data.q2.type : movement.state.data.q2r.type,
                .movement_flags=source->edition==QA_Q2_CLASSIC ? movement.state.data.q2.flags : movement.state.data.q2r.flags,
                .view_height=movement.view_height,.edition=source->edition,.present=true};
        }
    }
    if (!qa_vec_finite(value.origin) || !qa_vec_finite(value.view_angles) ||
        !qa_vec_finite(value.view_offset) || !qa_vec_finite(value.gun_offset) ||
        !qa_application_native_q2_presentation_local(app,source,client->seat,&actual,&found,error) ||
        !found || actual.client_slot!=client->client_slot || !qa_actor_id_equal(actual.actor,client->actor))
        return application_fail(error,QA_ERROR_ARGUMENT,"Q2 player sample changed its Source receipt");
    *out=value;
    return true;
}
