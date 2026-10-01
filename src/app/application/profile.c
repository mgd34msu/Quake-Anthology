#include "internal.h"
#include "rankings.h"
#include "qa/application_profile.h"

bool qa_application_player_profile_bind(qa_application *app, qa_fs_root *root,
    qa_error *error)
{
    if (!app || !root || app->destroy_requested || app->operation != APPLICATION_IDLE ||
        app->frame_preparing || app->q3_round_active || app->q3_world_restart ||
        app->state == QA_APPLICATION_FAULTED || !app->session ||
        !qa_session_safe(app->session) || !application_rankings_idle(app))
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Player profile binding requires its idle input configuration owner");
    if (app->user_files)
        return (app->user_files == root && app->progress) ||
            application_fail(error, QA_ERROR_ARGUMENT,
                "Application already retains a different input configuration profile");
    qa_player_progress *profile = NULL;
    if (!qa_player_progress_open(root, "player-progress.json", &profile, error)) return false;
    qa_fs_root_retain(root);
    app->user_files = root;
    app->progress = profile;
    return true;
}

qa_fs_root *qa_application_player_profile_root(const qa_application *app)
{ return app ? app->user_files : NULL; }
