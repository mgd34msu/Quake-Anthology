#include "native_q1_save.h"
#include "map_players_private.h"
#include "qa/game_q1_checkpoint.h"
#include "native_q1_wire.h"
#include "qa/game_q1_bots.h"

bool application_q1_native_save_admit(const qa_application *app, const qa_product *product,
    const qa_q1_save_data *save, bool *supported, qa_error *error) {
    if (!app || !product || !save || !supported || !app->catalog ||
        app->operation != APPLICATION_IDLE || app->state != QA_APPLICATION_READY ||
        app->world || app->provider_count || !qa_session_safe(app->session))
        return application_fail(error, QA_ERROR_ARGUMENT, "Original admission requires its fresh isolated application");
    *supported = false;
    if (product->family != QA_GAME_Q1 || product->program_kind != QA_PROGRAM_BUILTIN ||
        product->edition == QA_EDITION_QUAKEWORLD) return true;
    qa_vfs *files = NULL; qa_qc_program *program = NULL;
    bool okay = qa_catalog_open(app->catalog, product->id, &files, error) &&
        qa_qc_program_load_vfs(files, "progs.dat", &program, error) &&
        qa_q1_game_original_admit(application_q1_program(product->campaign),
            product->edition == QA_EDITION_RERELEASE ? QA_Q1_RERELEASE : QA_Q1_CLASSIC,
            program, save, supported, error);
    qa_qc_program_destroy(program); qa_vfs_destroy(files); return okay;
}

static application_player_record *local_player(qa_application *app,
    application_provider *provider, qa_error *error) {
    if (!app || app->operation != APPLICATION_IDLE || app->state != QA_APPLICATION_RUNNING ||
        !app->session || !app->world || !qa_session_safe(app->session) ||
        !qa_world_idle(app->world) || !application_guests_idle(app) ||
        !provider || provider != application_world_provider(app, QA_ROLE_ENTITIES, "") ||
        provider->kind != APPLICATION_PROVIDER_Q1 || !provider->state.q1 ||
        !provider->constructed || !provider->attached || provider->close_pending ||
        !app->players || app->players->map_provider != provider) {
        application_fail(error, QA_ERROR_ARGUMENT, "Original native save requires its idle physical Source");
        return NULL;
    }
    application_player_record *player = NULL;
    for (size_t i = 0; i < app->players->count; ++i) {
        application_player_record *row = app->players->records + i;
        if (row->retiring) continue;
        if (row->deferred || row->source_begin_pending || !row->q1_entry ||
            !qa_actors_get(qa_session_actors(app->session), row->actor)) {
            application_fail(error, QA_ERROR_ARGUMENT, "Original native save requires one admitted local Source client");
            return NULL;
        }
        if (player || row->remote || row->bot || row->spectator || row->client_slot != 0) {
            application_fail(error, QA_ERROR_UNSUPPORTED, "Original text save cannot represent this admitted Source client roster");
            return NULL;
        }
        player = row;
    }
    if (!player) application_fail(error, QA_ERROR_ARGUMENT, "Original native save has no physical player");
    return player;
}
bool application_q1_native_save_capture(qa_application *app, application_provider *provider,
    qa_q1_save_data *save, qa_error *error) {
    const application_player_record *player = local_player(app, provider, error);
    if (!player || !save) return false;
    qa_q1_options source;
    double source_seconds;
    if (!qa_q1_source_respawn_options_read(provider->state.q1, &source, &source_seconds, error)) return false;
    save->skill = source.skill;
    qa_combat_state combat;
    if (!qa_combat_read(app->combat, player->actor, &combat, error)) return false;
    if (!(combat.health > 0))
        return application_fail(error, QA_ERROR_ARGUMENT, "Cannot save a dead original Quake player");
    qa_application_control_view control;
    if (!qa_application_control_read(app, player->actor, &control) || control.state.kind != QA_RULESET_NETQUAKE)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original native save lost its actual NetQuake movement");
    qa_qc_program *program = NULL;
    qa_vfs *content = application_native_q1_wire_content(provider, error);
    bool okay = content && qa_qc_program_load_vfs(content, "progs.dat", &program, error) &&
        qa_q1_travel_original_parameters(provider->state.q1, player->q1_entry, save->spawn_parameters, error) &&
        qa_q1_game_original_capture(provider->state.q1, program, &control.state, save, error);
    qa_qc_program_destroy(program); return okay;
}
bool application_q1_native_save_fit(qa_application *app, application_provider *provider,
    const qa_q1_save_data *save, bool *supported, qa_error *error) {
    if (!save || !supported)
        return application_fail(error, QA_ERROR_ARGUMENT, "Original native fit requires its save and admission output");
    if (!local_player(app, provider, error)) return false;
    return qa_q1_game_original_fit(provider->state.q1, save, supported, error);
}
bool application_q1_native_save_restore(qa_application *app, application_provider *provider,
    const qa_q1_save_data *save, qa_error *error) {
    application_player_record *player = local_player(app, provider, error);
    if (!player || !save) return false;
    application_control_record *control;
    if (!application_control_ensure(app, player->actor, qa_v3(0,0,0), &control, error)) return false;
    qa_qc_program *program = NULL;
    qa_vfs *content = application_native_q1_wire_content(provider, error);
    bool okay = content && qa_qc_program_load_vfs(content, "progs.dat", &program, error) &&
        qa_q1_game_original_restore(provider->state.q1, program, save, &control->state, error);
    qa_q1_travel_state *entry = NULL;
    if (okay) okay = qa_q1_travel_original_parameters_restore(provider->state.q1,player->actor,
        save->spawn_parameters,&entry,error);
    if (okay) {
        qa_q1_travel_destroy(player->q1_entry); player->q1_entry = entry;
        control->view_angles = control->command_angles = control->state.data.nq.view_angles;
        qa_q1_character_view character;
        okay = qa_q1_character_read(provider->state.q1,player->actor,&character);
        if (!okay) application_fail(error,QA_ERROR_ARGUMENT,"Original restore lost its actual Source character");
        if (okay) { control->view_offset = character.view_offset; control->view_height = character.view_offset.z; }
        control->water_level = control->state.data.nq.water_level; control->water_type = control->state.data.nq.water_type;
        control->ground = control->state.data.nq.ground;
        qa_body_state body;
        if (okay) okay = qa_world_body_read(app->world,player->actor,&body,error);
        if (okay) control->bounds = control->standing_bounds = body.bounds;
    }
    qa_qc_program_destroy(program); return okay;
}
