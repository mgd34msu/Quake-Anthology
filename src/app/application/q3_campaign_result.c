#include "q3_campaign_result.h"
#include "qa/game_type.h"
#include "qa/application_native_q3_presentation.h"

static bool current(qa_application *app, const qa_application_q3_campaign *campaign,
    const qa_application_native_q3_presentation *source, qa_error *error)
{
    return (qa_application_q3_campaign_current(app, campaign) &&
        qa_application_native_q3_presentation_current(app, source)) ||
        application_fail(error, QA_ERROR_ARGUMENT, "Live campaign result left its actual GAME source cut");
}

bool qa_application_q3_campaign_result_read(qa_application *app,
    const qa_application_q3_campaign *campaign, uint32_t seat,
    qa_application_q3_campaign_result *out, bool *found, qa_error *error)
{
    if (!out || !found || !qa_application_q3_campaign_current(app, campaign))
        return application_fail(error, QA_ERROR_ARGUMENT, "Live campaign result requires its actual GAME context");
    *found = false;
    if (!campaign->native_source) return true;
    qa_application_native_q3_presentation source;
    if (!qa_application_native_q3_presentation_read(app, campaign->source_owner, &source, error) ||
        source.source_game != campaign->source_game || source.game_type != campaign->game_type)
        return application_fail(error, QA_ERROR_ARGUMENT, "Live campaign result differs from its physical GAME owner");
    qa_q3_source_match_state match;
    if (!qa_q3_source_match_state_read(source.source_game, &match, error)) return false;
    if (!match.intermission_time_ms) return current(app, campaign, &source, error);
    qa_application_q3_campaign_result value = {.intermission_time_ms = match.intermission_time_ms};
    qa_q3_player player;
    bool local;
    if (!qa_application_native_q3_presentation_local(app, &source, seat,
            &value.physical_client, &value.actor, &player, &local, error)) return false;
    if (!local) return current(app, campaign, &source, error);
    qa_q3_client_session session;
    qa_q3_source_client_counts counts;
    if (!qa_q3_client_session_slot_read(source.source_game, value.physical_client, &session, error) ||
        !qa_q3_source_client_counts_read(source.source_game, &counts, error)) return false;
    value.team = session.team;
    value.leading_client = counts.sorted_clients[0];
    if (qa_game_type_is_objective(source.game_type)) {
        qa_q3_source_team_state teams;
        if (value.team < 0 || value.team >= 4 ||
            !qa_q3_source_team_state_read(source.source_game, &teams, error))
            return application_fail(error, QA_ERROR_FORMAT, "Live campaign team exceeds its actual source score array");
        value.score = teams.team_scores[value.team];
        value.opponent = teams.team_scores[value.team == 1 ? 2 : 1];
        value.won = value.score > value.opponent;
    } else {
        value.score = player.persistant[0];
        value.opponent = -9999;
        for (uint32_t slot = 0; slot < QA_Q3_SOURCE_CLIENTS; ++slot) {
            if (slot == value.physical_client) continue;
            int32_t score;
            if (!current(app, campaign, &source, error) ||
                !qa_q3_wire_client_source_score_read(source.source_game, slot, &score, error) ||
                !current(app, campaign, &source, error)) return false;
            if (score > value.opponent) value.opponent = score;
        }
        value.won = value.leading_client == value.physical_client;
    }
    if (!current(app, campaign, &source, error)) return false;
    *out = value;
    *found = true;
    return true;
}
