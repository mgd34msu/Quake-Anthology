#include "internal.h"
#include "rankings.h"
#include "qa/application_players.h"
#include "qa/binary.h"
#include <limits.h>

bool frontend_ranking_effect(void *context, qa_application *application, qa_actor_owner source,
    qa_actor_id actor, qa_application_ranking_effect effect, const qa_ranking_player *status,
    qa_error *error)
{
    qa_frontend *frontend = context;
    if (!frontend || frontend->application != application || !source || !status ||
        (effect != QA_APPLICATION_RANKING_STATUS && effect != QA_APPLICATION_RANKING_MENU))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "ranking output lacks its actual frontend binding");
    uint32_t source_slot;
    if (!qa_application_rankings_client_slot(application, actor, &source_slot) || source_slot > INT32_MAX)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "ranking output lacks its installed GAME client binding");
    uint32_t launch_seat,index;
    if (frontend->options.dedicated || !qa_application_player_seat(application,actor,&launch_seat)) return true;
    const qa_launch_choices *choices = qa_launch_snapshot_choices(qa_application_launch(application));
    bool local = false;
    if (choices) for (size_t i = 0; i < choices->seat_count; ++i)
        if (choices->seats[i].id == launch_seat && choices->seats[i].local && !choices->seats[i].bot) local = true;
    if (!local) return true;
    if (!frontend_seat_ordinal_read(frontend,launch_seat,&index) || !frontend->seats || !frontend->seats[index].ui ||
        !frontend->seats[index].rankings)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "ranking output precedes its actual local menu owner");
    if (effect == QA_APPLICATION_RANKING_STATUS) return true;
    frontend_seat *seat = frontend->seats + index;
    return qa_ui_rankings_set_slot(seat->rankings, (int32_t)source_slot, error) &&
        qa_ui_open(seat->ui, FRONTEND_RANKINGS, (double)frontend->time_ns / 1000000.0, error);
}

static bool scope_ready(const qa_frontend *frontend)
{
    return frontend && frontend->application && frontend->options.seats >= 1 &&
        frontend->options.seats <= 4;
}
static bool capture(void *context, qa_application_ranking_effect_fn installed, void *binding,
    qa_buffer *out, qa_error *error)
{
    qa_frontend *frontend = context;
    if (!scope_ready(frontend) || installed != frontend_ranking_effect || binding != frontend ||
        !out || out->data || out->size)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "ranking checkpoint requires its actual installed frontend bridge");
    uint8_t *bytes = malloc(20);
    if (!bytes) return frontend_fail(error, QA_ERROR_MEMORY, "retaining ranking frontend scope");
    memcpy(bytes, "QFRK", 4);
    qa_store_u64le(bytes + 4, QA_FRONTEND_COMMAND_OWNER);
    qa_store_u32le(bytes + 12, frontend->options.seats);
    qa_store_u32le(bytes + 16, frontend->options.dedicated ? 1 : 0);
    *out = (qa_buffer){bytes, 20}; return true;
}
static bool resolve(void *context, qa_bytes bytes, qa_application_ranking_effect_fn *installed,
    void **binding, qa_error *error)
{
    qa_frontend *frontend = context;
    if (!scope_ready(frontend) || !installed || !binding || !bytes.data || bytes.size != 20 ||
        memcmp(bytes.data, "QFRK", 4) ||
        qa_load_u64le(bytes.data + 4) != QA_FRONTEND_COMMAND_OWNER ||
        qa_load_u32le(bytes.data + 12) != frontend->options.seats ||
        qa_load_u32le(bytes.data + 16) != (frontend->options.dedicated ? 1u : 0u))
        return frontend_fail(error, QA_ERROR_FORMAT, "saved ranking bridge differs from its prepared frontend scope");
    *installed = frontend_ranking_effect; *binding = frontend; return true;
}
qa_application_ranking_checkpoint_refs frontend_ranking_refs(qa_frontend *frontend)
{
    return (qa_application_ranking_checkpoint_refs){frontend, capture, resolve};
}
