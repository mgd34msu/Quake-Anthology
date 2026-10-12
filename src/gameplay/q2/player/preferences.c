#include "internal.h"
#include "qa/game_q2_preferences.h"
#include "qa/text.h"
#include <limits.h>

bool qa_q2_userinfo_field_of_view_read(const char *text, double *out, qa_error *e)
{
    if (!text || !out) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 requested FOV needs its actual userinfo");
        return false;
    }
    qa_bytes field;
    if (qa_info_find(text,(qa_bytes){(const uint8_t *)"fov",3},0,&field)) {
        char *end;
        const char *start=(const char *)field.data;
        long value=strtol(start,&end,10);
        *out=end==start ? 0 : value>INT_MAX ? INT_MAX :
            value<INT_MIN ? INT_MIN : (int)value;
        return true;
    }
    *out = 0;
    return true;
}

bool qa_q2_userinfo_field_of_view(const char *text, double fov,
    qa_buffer *out, qa_error *e)
{
    if (!text || !out || out->data || out->size || !isfinite(fov) || fov < 60 || fov > 160) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 FOV needs a value between 60 and 160");
        return false;
    }
    size_t length = strlen(text);
    if (length >= 2048) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 userinfo leaves its largest actual Source extent");
        return false;
    }
    char number[32];
    if (!qa_format_number(fov, number, e)) return false;
    char *copy = malloc(length + sizeof(number) + 6);
    if (!copy) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Retaining Q2 requested FOV dictionary");
        return false;
    }
    size_t used = 0;
    bool changed = false;
    const char *p=text;
    qa_bytes key,value,first;
    while (qa_info_next(&p,&key,&value)) {
        qa_info_find(text,key,0,&first);
        if (first.data!=value.data) continue;
        if (key.size==3 && !memcmp(key.data,"fov",3)) {
            value=(qa_bytes){(const uint8_t *)number,strlen(number)}; changed=true;
        }
        copy[used++]='\\'; memcpy(copy+used,key.data,key.size); used+=key.size;
        copy[used++]='\\'; memcpy(copy+used,value.data,value.size); used+=value.size;
    }
    if (!changed) {
        memcpy(copy + used, "\\fov\\", 5); used += 5;
        memcpy(copy + used, number, strlen(number)); used += strlen(number);
    }
    copy[used] = 0;
    *out = (qa_buffer){(uint8_t *)copy, used + 1};
    return true;
}

bool qa_q2_player_field_of_view_read(qa_q2_game *g, qa_actor_id id,
    double *out, bool *found, qa_error *e)
{
    if (!g || !out || !found || !q2_actor_live(g, id)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 FOV observation lost its actual player generation");
        return false;
    }
    q2_actor *a = q2_actor_get(g, id, false, NULL);
    *found = a && a->client && a->client->info.connected;
    *out = *found ? (a->wire_view.present ? a->wire_view.view.fov : a->client->rule.fov) : 0;
    return true;
}

bool qa_q2_player_userinfo_read(qa_q2_game *g, qa_actor_id id, const char **out, qa_error *e)
{
    if (!out) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 userinfo observation needs its borrowed output");
        return false;
    }
    q2_actor *a = q2_client(g, id, e);
    if (!a) return false;
    *out = a->client->rule.userinfo;
    return true;
}

typedef struct fov_call {
    qa_q2_game *game;
    double value;
    bool source, preserve, intermission, restoring;
} fov_call;

static bool set_fov(void *context, qa_actor_id id, qa_error *e)
{
    fov_call *call = context;
    qa_q2_game *g = call->game;
    q2_actor *a = q2_actor_get(g, id, false, NULL);
    if (!a || !a->client || !a->client->info.connected) return true;
    q2_client_state *state = a->client;
    float previous = state->rule.fov;
    bool intermission = call->intermission || (call->source && g->player_runtime->intermission);
    if (call->source) {
        double requested;
        if (!qa_q2_userinfo_field_of_view_read(state->rule.userinfo, &requested, e)) return false;
        bool fixed = g->options.rules != QA_RULESET_Q2_RERELEASE && g->options.deathmatch &&
            (g->options.deathmatch_flags & 32768u);
        float ordinary = fixed ? 90 : g->options.rules == QA_RULESET_Q2_RERELEASE ?
            q2_clamp((float)requested, 1, 160) : requested < 1 ? 90 : fminf((float)requested, 160);
        bool preserve = !fixed && (call->preserve || state->info.chase_target.registry || state->rule.sphere_vehicle ||
            (call->restoring && previous != ordinary));
        qa_buffer text = {0};
        if (!qa_q2_userinfo_field_of_view(state->rule.userinfo, call->value, &text, e)) return false;
        bool ok = qa_q2_player_userinfo(g, id, (const char *)text.data, e);
        qa_buffer_free(&text);
        if (!ok) return false;
        a = q2_client(g, id, e);
        if (!a || a->client != state) {
            qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 FOV callback replaced its actual player state");
            return false;
        }
        if (preserve) state->rule.fov = previous;
    } else state->rule.fov = (float)call->value;
    if (!intermission && a->wire_view.present && a->wire_view.view.fov == previous)
        a->wire_view.view.fov = state->rule.fov;
    return true;
}

bool qa_q2_player_field_of_view_set(qa_q2_game *g, qa_actor_id id, double value,
    bool source, bool preserve, bool intermission, bool restoring, qa_error *e)
{
    if (!isfinite(value) || value < 60 || value > 160) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Q2 FOV needs a value between 60 and 160");
        return false;
    }
    fov_call call = {g, value, source, preserve, intermission, restoring};
    return qa_q2_run_actor(g, id, set_fov, &call, e);
}
