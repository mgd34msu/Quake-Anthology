#include "rankings.h"
#include "map_players_private.h"
#include "native_q3_clients.h"
#include "native_q3_console.h"
#include "qa/game_q3_configstrings.h"
#include "qa/game_q3_source.h"
#include "qa/source_save.h"
#include "qa/text.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ranking_report {
    qa_ranking_source_report value;
    char *text;
    struct ranking_report *next;
} ranking_report;
typedef struct ranking_client {
    qa_actor_id actor;
    uint32_t slot;
    qa_ranking_player observed;
    qa_q3_weapon weapon;
    int32_t since;
    bool has_observed, has_weapon;
    struct ranking_client *next;
} ranking_client;
struct application_rankings {
    qa_actor_owner provider;
    uint64_t source_serial;
    uint64_t map_revision;
    bool enabled, ended, busy, restoring, backend_failed;
    ranking_report *reports, **report_tail;
    size_t report_count;
    ranking_client *clients, **client_tail;
    size_t client_count;
};

static void reports_free(ranking_report *report)
{
    while (report) {
        ranking_report *next = report->next;
        free(report->text); free(report); report = next;
    }
}
static void owner_free(application_rankings *owner)
{
    if (!owner) return;
    reports_free(owner->reports);
    while (owner->clients) {
        ranking_client *next = owner->clients->next;
        free(owner->clients); owner->clients = next;
    }
    free(owner);
}
void application_rankings_dispose(qa_application *app)
{
    if (!app) return;
    owner_free(app->ranked_source); app->ranked_source = NULL;
}
bool application_rankings_idle(const qa_application *app)
{
    return !app || !app->ranked_source || !app->ranked_source->busy;
}
static application_provider *source(qa_application *app)
{
    if (!app || !app->primary_mode_ready) return NULL;
    application_provider *provider = application_mode_provider(app, app->primary_mode);
    return provider && provider->kind == APPLICATION_PROVIDER_Q3 && provider->state.q3 &&
        provider->constructed && provider->attached && !provider->close_pending ? provider : NULL;
}
static bool agreement(qa_application *app, application_rankings *owner, qa_error *error)
{
    application_provider *provider = source(app);
    return (provider && provider->launch && owner->provider == provider->owner &&
        owner->map_revision == app->map_revision &&
        owner->source_serial && owner->source_serial == provider->launch->identity) ||
        application_fail(error, QA_ERROR_ARGUMENT, "Ranked source outlived its actual Q3 map owner");
}
bool application_rankings_warmup(void *context)
{
    application_provider *provider = context;
    const char *value = NULL;
    if (!provider || provider != source(provider->application) ||
        !qa_q3_configstring_read(provider->state.q3, 5, &value, NULL)) return true;
    return strtol(value, NULL, 10) != 0;
}
static bool publish_active(qa_application *app, application_provider *provider, qa_error *error)
{
    qa_cvars *cvars = application_native_q3_console_registry(provider);
    const qa_cvar_view *enabled = qa_cvars_find(cvars, "sv_enableRankings");
    const qa_cvar_view *active = qa_cvars_find(cvars, "sv_rankingsActive");
    if (!enabled || !active || enabled->owner != provider->owner || active->owner != provider->owner)
        return application_fail(error, QA_ERROR_ARGUMENT, "Rankings require the actual Q3 source cvar declarations");
    const char *text = app->ranked_source && enabled->number != 0 &&
        qa_rankings_state(app->rankings).kind == QA_RANKING_ACTIVE ? "1" : "0";
    if (!strcmp(active->value, text)) return true;
    if (!application_native_q3_console_borrow(provider, error)) return false;
    bool ok = qa_cvars_set(cvars, "sv_rankingsActive", text, true, error);
    application_native_q3_console_release(provider); return ok;
}
static bool backend_result(qa_application *app, application_rankings *owner, bool ok)
{
    if (!ok && qa_rankings_backend_failed(app->rankings)) owner->backend_failed=true;
    return ok;
}
static bool flush(qa_application *app, application_rankings *owner, qa_error *error)
{
    ranking_report *reports = owner->reports;
    owner->reports = NULL; owner->report_tail = &owner->reports; owner->report_count = 0;
    bool ok = true;
    for (ranking_report *row = reports; row && ok; row = row->next) {
        if (qa_rankings_state(app->rankings).kind != QA_RANKING_ACTIVE) break;
        qa_ranking_source_report *value = &row->value;
        ok = backend_result(app,owner,value->stat.kind == QA_RANKING_STRING
            ? qa_rankings_report_string(app->rankings, value->self, value->other,
                value->stat.key, row->text, error)
            : qa_rankings_report_integer(app->rankings, value->self, value->other,
                value->stat.key, value->stat.value.integer.value,
                value->stat.value.integer.accumulate, error));
    }
    reports_free(reports); return ok;
}
bool application_rankings_report(void *context, const qa_ranking_source_report *value, qa_error *error)
{
    application_provider *provider = context;
    qa_application *app = provider ? provider->application : NULL;
    if (!app || !value || provider != source(app))
        return application_fail(error, QA_ERROR_ARGUMENT, "Ranking report has no authoritative source");
    application_rankings *owner = app->ranked_source;
    if (!owner || owner->ended || qa_rankings_state(app->rankings).kind != QA_RANKING_ACTIVE) return true;
    if (owner->restoring || app->operation==APPLICATION_PERSISTING || !agreement(app, owner, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Ranking reports cannot enter a pending source continuation");
    if (owner->report_count == 65536) {
        reports_free(owner->reports); owner->reports = NULL;
        owner->report_tail = &owner->reports; owner->report_count = 0;
        return qa_rankings_unavailable(app->rankings,
            "Ranking provider cannot keep up with source reports; this match cannot be submitted completely.", error);
    }
    if ((value->stat.kind != QA_RANKING_INTEGER && value->stat.kind != QA_RANKING_STRING) ||
        (value->stat.kind == QA_RANKING_STRING && !value->stat.value.string))
        return application_fail(error, QA_ERROR_ARGUMENT, "Invalid source ranking report");
    ranking_report *row = calloc(1, sizeof(*row));
    if (!row) return application_fail(error, QA_ERROR_MEMORY, "Retaining source ranking report");
    row->value = *value;
    if (value->stat.kind == QA_RANKING_STRING) {
        size_t length = strlen(value->stat.value.string);
        if (length == SIZE_MAX || !(row->text = malloc(length + 1))) {
            free(row); return application_fail(error, QA_ERROR_MEMORY, "Retaining source ranking report text");
        }
        memcpy(row->text, value->stat.value.string, length + 1);
        row->value.stat.value.string = row->text;
    }
    *owner->report_tail = row; owner->report_tail = &row->next; ++owner->report_count;
    return true;
}
static application_player_record *player(qa_application *app, application_provider *provider,
    uint32_t slot, qa_actor_id actor)
{
    if (app->players) for (size_t i = 0; i < app->players->count; ++i) {
        application_player_record *row = app->players->records + i;
        uint32_t native_slot;
        if (!row->retiring && !row->source_begin_pending &&
            row->client_slot == slot && (!actor.registry || qa_actor_id_equal(row->actor, actor)) &&
            qa_q3_native_client_slot(provider->state.q3, row->actor, &native_slot, NULL) &&
            native_slot == slot) return row;
    }
    return NULL;
}
bool qa_application_rankings_client_slot(const qa_application *app, qa_actor_id actor,
    uint32_t *out)
{
    application_provider *provider=source((qa_application *)app);
    uint32_t slot;
    if (!out || !provider || !actor.registry ||
        !qa_q3_native_client_slot(provider->state.q3,actor,&slot,NULL) ||
        !player((qa_application *)app,provider,slot,actor)) return false;
    *out=slot; return true;
}
static bool output(qa_application *app, application_provider *provider, qa_actor_id actor,
    qa_application_ranking_effect effect, const qa_ranking_player *state, qa_error *error)
{
    if (!app->ranking_effect) return true;
    return app->ranking_effect(app->guest_context, app, provider->owner, actor,
        effect, state, error);
}
static application_rankings *account_owner(qa_application *app, int32_t slot, qa_error *error)
{
    application_rankings *owner=app ? app->ranked_source : NULL;
    application_provider *provider=source(app);
    if (!owner || !provider || app->operation!=APPLICATION_IDLE || app->q3_round_active || app->frame_preparing || owner->busy || owner->restoring ||
        slot<0 || !player(app,provider,(uint32_t)slot,(qa_actor_id){0}) || !agreement(app,owner,error)) {
        application_fail(error,QA_ERROR_ARGUMENT,"Ranking account requires an idle connected native source client");
        return NULL;
    }
    return owner;
}
bool qa_application_rankings_account(qa_application *app, int32_t slot,
    const qa_ranking_request *request, qa_error *error)
{
    application_rankings *owner=account_owner(app,slot,error);
    if (!owner) return false;
    owner->busy=true; bool ok=qa_rankings_account(app->rankings,slot,request,error);
    qa_error active_error={0}; bool published=publish_active(app,source(app),&active_error);
    if (!published) { ok=false; owner->backend_failed=false; if (error) *error=active_error; }
    owner->busy=false; return ok;
}
bool qa_application_rankings_reset(qa_application *app, int32_t slot, qa_error *error)
{
    application_rankings *owner=account_owner(app,slot,error);
    if (!owner) return false;
    owner->busy=true; bool ok=qa_rankings_reset(app->rankings,slot,error); owner->busy=false; return ok;
}
bool qa_application_rankings_spectate(qa_application *app, int32_t slot, qa_error *error)
{
    application_rankings *owner=account_owner(app,slot,error);
    if (!owner) return false;
    owner->busy=true;
    bool ok=flush(app,owner,error) && qa_rankings_spectate(app->rankings,slot,error);
    qa_error active_error={0}; bool published=publish_active(app,source(app),&active_error);
    if (ok && !published) { ok=false; if (error) *error=active_error; }
    owner->busy=false; return ok;
}
static int number_digit(unsigned char value)
{
    if (value>='0' && value<='9') return value-'0';
    if (value>='a' && value<='f') return value-'a'+10;
    if (value>='A' && value<='F') return value-'A'+10;
    return -1;
}
/* Number(cvar text) is separate from the registry's binary32 atof cache. */
static bool cvar_number(const char *text, double *out, qa_error *error)
{
    qa_bytes bytes={(const uint8_t *)text,strlen(text)};
    size_t cursor=0, first=bytes.size, last=0;
    uint32_t scalar;
    while (cursor<bytes.size) {
        size_t begin=cursor;
        (void)qa_utf8_next(bytes,&cursor,&scalar);
        if (!qa_unicode_whitespace(scalar)) {
            if (first==bytes.size) first=begin;
            last=cursor;
        }
    }
    *out=NAN;
    if (!last) { *out=0; return true; }
    bytes=(qa_bytes){bytes.data+first,last-first};
    if (bytes.size>=2 && bytes.data[0]=='0') {
        unsigned bits=bytes.data[1]=='x' || bytes.data[1]=='X' ? 4u :
            bytes.data[1]=='o' || bytes.data[1]=='O' ? 3u :
            bytes.data[1]=='b' || bytes.data[1]=='B' ? 1u : 0u;
        if (bits) {
            if (bytes.size==2) return true;
            uint64_t mantissa=0;
            unsigned count=0;
            bool guard=false, sticky=false;
            for (size_t i=2;i<bytes.size;++i) {
                int digit=number_digit(bytes.data[i]);
                if (digit<0 || (unsigned)digit>=(1u<<bits)) return true;
                for (unsigned bit=bits;bit;--bit) {
                    bool set=((unsigned)digit&(1u<<(bit-1u)))!=0;
                    if (!count && !set) continue;
                    if (count<53) mantissa=(mantissa<<1)|(set ? 1u : 0u);
                    else if (count==53) guard=set;
                    else sticky=sticky || set;
                    if (count<1025) ++count;
                }
            }
            if (count<=53) { *out=(double)mantissa; return true; }
            if (guard && (sticky || (mantissa&1u))) ++mantissa;
            *out=count>1024 || (count==1024 && mantissa==UINT64_C(9007199254740992))
                ? INFINITY : ldexp((double)mantissa,(int)count-53);
            return true;
        }
    }
    cursor=0;
    bool negative=bytes.data[0]=='-';
    if (negative || bytes.data[0]=='+') ++cursor;
    if (bytes.size-cursor==8 && !memcmp(bytes.data+cursor,"Infinity",8)) {
        *out=negative ? -INFINITY : INFINITY; return true;
    }
    bool digit=false;
    while (cursor<bytes.size && bytes.data[cursor]>='0' && bytes.data[cursor]<='9') {
        digit=true; ++cursor;
    }
    if (cursor<bytes.size && bytes.data[cursor]=='.') {
        ++cursor;
        while (cursor<bytes.size && bytes.data[cursor]>='0' && bytes.data[cursor]<='9') {
            digit=true; ++cursor;
        }
    }
    if (!digit) return true;
    if (cursor<bytes.size && (bytes.data[cursor]=='e' || bytes.data[cursor]=='E')) {
        ++cursor;
        if (cursor<bytes.size && (bytes.data[cursor]=='+' || bytes.data[cursor]=='-')) ++cursor;
        size_t start=cursor;
        while (cursor<bytes.size && bytes.data[cursor]>='0' && bytes.data[cursor]<='9') ++cursor;
        if (cursor==start) return true;
    }
    return cursor!=bytes.size || qa_parse_number(bytes,out,error);
}
static bool game_over(qa_application *app, application_rankings *owner,
    application_provider *provider, qa_error *error)
{
    if (owner->ended || application_rankings_warmup(provider)) return true;
    if (!flush(app, owner, error)) return false;
    owner->ended = true;
    static const struct { const char *name; int32_t key; } strings[] = {
        {"sv_hostname",1000010000},{"mapname",1000010001},{"fs_game",1000010002},{"version",1000010011}},
        integers[] = {{"g_gametype",1010010003},{"fraglimit",1010010004},{"timelimit",1010010005},
        {"sv_maxclients",1010010006},{"sv_maxRate",1010010007},{"sv_minPing",1010010008},
        {"sv_maxPing",1010010009},{"dedicated",1010010010}};
    qa_cvars *cvars = application_native_q3_console_registry(provider);
    for (size_t i = 0; i < sizeof(strings)/sizeof(strings[0]); ++i) {
        const qa_cvar_view *value = qa_cvars_find(cvars, strings[i].name);
        if (!backend_result(app,owner,qa_rankings_report_string(app->rankings,-1,-1,strings[i].key,value ? value->value : "",error))) return false;
    }
    for (size_t i = 0; i < sizeof(integers)/sizeof(integers[0]); ++i) {
        const qa_cvar_view *value = qa_cvars_find(cvars, integers[i].name);
        double number=0;
        if (value && !cvar_number(value->value,&number,error)) return false;
        number=trunc(number);
        if (number == 0.0 || isnan(number)) number=0;
        if (!backend_result(app,owner,qa_rankings_report_integer(app->rankings,-1,-1,integers[i].key,number,false,error))) return false;
    }
    return true;
}
static void client_tail(application_rankings *owner)
{
    owner->client_tail = &owner->clients;
    while (*owner->client_tail) owner->client_tail = &(*owner->client_tail)->next;
}
static bool disconnect(qa_application *app, application_rankings *owner,
    qa_actor_id actor, qa_error *error)
{
    ranking_client **link = &owner->clients;
    while (*link && !qa_actor_id_equal((*link)->actor, actor)) link = &(*link)->next;
    if (!*link) return true;
    if (!flush(app, owner, error)) return false;
    ranking_client *row = *link;
    bool ok = backend_result(app,owner,qa_rankings_disconnect(app->rankings, (int32_t)row->slot, error));
    *link = row->next; --owner->client_count; free(row); client_tail(owner); return ok;
}
bool application_rankings_disconnect(qa_application *app, qa_actor_id actor, qa_error *error)
{
    application_rankings *owner = app ? app->ranked_source : NULL;
    if (!owner) return true;
    if (owner->busy || owner->restoring)
        return application_fail(error, QA_ERROR_ARGUMENT, "Ranking disconnect reentered its source lifecycle");
    owner->busy = true; bool ok = disconnect(app, owner, actor, error); owner->busy = false;
    return ok;
}
static ranking_client *observed_client(application_rankings *owner, uint32_t slot,
    qa_actor_id actor, qa_error *error)
{
    ranking_client *row=owner->clients;
    while (row && row->slot!=slot) row=row->next;
    if (row) return row;
    row=calloc(1,sizeof(*row));
    if (!row) {
        application_fail(error,QA_ERROR_MEMORY,"Retaining ranked source client identity");
        return NULL;
    }
    row->actor=actor; row->slot=slot;
    *owner->client_tail=row; owner->client_tail=&row->next; ++owner->client_count;
    return row;
}
static bool reconcile_clients(qa_application *app, application_rankings *owner,
    application_provider *provider, qa_error *error)
{
    ranking_client *row = owner->clients;
    while (row) {
        ranking_client *next = row->next;
        if (!player(app, provider, row->slot, row->actor) && !disconnect(app, owner, row->actor, error)) return false;
        row = next;
    }
    if (app->players) for (size_t i = 0; i < app->players->count; ++i) {
        application_player_record value = app->players->records[i];
        if (!player(app, provider, value.client_slot, value.actor)) continue;
        if (!observed_client(owner,value.client_slot,value.actor,error)) return false;
    }
    return true;
}
static bool frame_players(qa_application *app, application_rankings *owner,
    application_provider *provider, qa_error *error)
{
    if (!flush(app, owner, error) || !backend_result(app,owner,qa_rankings_poll(app->rankings,error))) return false;
    if (qa_rankings_state(app->rankings).kind != QA_RANKING_ACTIVE || owner->ended) return true;
    int32_t now;
    uint32_t max_clients;
    if (!qa_q3_source_clock(provider->state.q3,&now,error) ||
        !qa_q3_source_max_clients(provider->state.q3,&max_clients,error)) return false;
    qa_cvars *cvars=application_native_q3_console_registry(provider);
    for (uint32_t slot=0; slot<max_clients; ++slot) {
        application_player_record *actual=player(app,provider,slot,(qa_actor_id){0});
        if (!actual) continue;
        qa_actor_id actor=actual->actor;
        if (actual->bot) {
            if (!application_rankings_source_effect(app,provider,actor,APPLICATION_RANKING_DROP_BOT,error)) return false;
            continue;
        }
        ranking_client *row=observed_client(owner,slot,actor,error);
        if (!row) return false;
        qa_q3_player_state native;
        qa_mode_player_view member;
        if (!qa_q3_player_read(provider->state.q3,actor,&native) ||
            !qa_modes_player_read(app->modes,app->primary_mode,actor,&member,error))
            return application_fail(error,QA_ERROR_ARGUMENT,"Ranked player lost its actual native source state");
        if (row->has_weapon && row->weapon!=native.weapon) {
            int64_t seconds=((int64_t)now-row->since)/1000;
            if (!qa_q3_ranking_weapon_time(provider->state.q3,actor,row->weapon,(int32_t)seconds,error)) return false;
        }
        if (!row->has_weapon || row->weapon!=native.weapon) {
            row->has_weapon=true; row->weapon=native.weapon; row->since=now;
        }
        qa_ranking_player state=qa_rankings_player(app->rankings,(int32_t)row->slot);
        bool changed=!row->has_observed || row->observed.kind!=state.kind;
        row->observed=state; row->has_observed=true;
        if (changed && !output(app,provider,actor,QA_APPLICATION_RANKING_STATUS,&state,error)) return false;
        if (!player(app,provider,slot,actor))
            return application_fail(error,QA_ERROR_ARGUMENT,"Ranking output changed its actual source client generation");
        if ((state.kind==QA_RANKING_NEW_PLAYER || state.kind==QA_RANKING_SPECTATOR) && !member.state.spectator) {
            if (!application_rankings_source_effect(app,provider,actor,APPLICATION_RANKING_SPECTATOR,error)) return false;
            if (!player(app,provider,slot,actor))
                return application_fail(error,QA_ERROR_ARGUMENT,"Ranking spectator changed its actual source client generation");
            if (!output(app,provider,actor,QA_APPLICATION_RANKING_MENU,&state,error)) return false;
        } else if (state.kind==QA_RANKING_DENIED_PLAYER) {
            if (!backend_result(app,owner,qa_rankings_reset(app->rankings,(int32_t)row->slot,error))) return false;
        } else if (state.kind==QA_RANKING_ACTIVE_PLAYER) {
            const qa_cvar_view *game_type=qa_cvars_find(cvars,"g_gametype");
            if (!game_type || game_type->owner!=provider->owner)
                return application_fail(error,QA_ERROR_ARGUMENT,"Ranked source lacks its actual game type");
            if (member.state.spectator && game_type->number<3 &&
                !application_rankings_source_effect(app,provider,actor,APPLICATION_RANKING_ACTIVATE,error)) return false;
            if (changed) for (uint32_t other=0; other<max_clients; ++other) {
                application_player_record *peer=player(app,provider,other,(qa_actor_id){0});
                if (!peer || peer->bot) continue;
                qa_actor_id peer_actor=peer->actor;
                if (other!=slot && qa_rankings_player(app->rankings,(int32_t)other).kind==QA_RANKING_ACTIVE_PLAYER &&
                    !backend_result(app,owner,qa_rankings_report_integer(app->rankings,(int32_t)slot,(int32_t)other,1210000002,1,false,error))) return false;
                if (!player(app,provider,slot,actor) || !player(app,provider,other,peer_actor))
                    return application_fail(error,QA_ERROR_ARGUMENT,"Ranking report changed its actual source client generation");
                if (!application_rankings_source_effect(app,provider,peer_actor,APPLICATION_RANKING_SCOREBOARD,error)) return false;
            }
        }
    }
    const qa_cvar_view *frag=qa_cvars_find(cvars,"fraglimit"), *time=qa_cvars_find(cvars,"timelimit");
    if (!frag || !time) return application_fail(error,QA_ERROR_ARGUMENT,"Ranked source lacks its actual limits");
    double fraglimit, timelimit;
    if (!cvar_number(frag->value,&fraglimit,error) || !cvar_number(time->value,&timelimit,error)) return false;
    if ((fraglimit==0 || fraglimit>100) && (timelimit==0 || timelimit>1000)) {
        if (!application_native_q3_console_borrow(provider,error)) return false;
        bool ok=qa_cvars_set(cvars,"timelimit","1000",true,error);
        application_native_q3_console_release(provider); if (!ok) return false;
    }
    return true;
}
bool application_rankings_frame(qa_application *app, qa_error *error)
{
    application_provider *provider=source(app);
    if (!provider) return true;
    application_rankings *owner=app->ranked_source;
    if (owner && (owner->busy || owner->restoring || !agreement(app,owner,error)))
        return application_fail(error,QA_ERROR_ARGUMENT,"Ranking frame reentered or changed its actual source owner");
    qa_cvars *cvars=application_native_q3_console_registry(provider);
    const qa_cvar_view *enable=qa_cvars_find(cvars,"sv_enableRankings");
    const qa_cvar_view *game_type=qa_cvars_find(cvars,"g_gametype");
    qa_mode_view mode;
    if (!enable || enable->owner!=provider->owner || !game_type || game_type->owner!=provider->owner ||
        !qa_modes_read(app->modes,app->primary_mode,&mode,error))
        return application_fail(error,QA_ERROR_ARGUMENT,"Rankings lack actual native source settings and mode");
    bool enabled=enable->number!=0, fresh=!owner;
    if (!owner) {
        owner=calloc(1,sizeof(*owner));
        if (!owner) return application_fail(error,QA_ERROR_MEMORY,"Allocating native ranked match owner");
        owner->provider=provider->owner; owner->source_serial=provider->launch->identity;
        owner->map_revision=app->map_revision; owner->enabled=enabled;
        owner->report_tail=&owner->reports; owner->client_tail=&owner->clients;
        app->ranked_source=owner;
    }
    owner->busy=true;
    owner->backend_failed=false;
    bool ok=true;
    if (fresh || owner->enabled!=enabled) {
        if (!fresh && !enabled) ok=backend_result(app,owner,qa_rankings_end(app->rankings,error));
        owner->enabled=enabled;
        if (ok) ok=backend_result(app,owner,qa_rankings_begin(app->rankings,enabled,game_type->number==2,
            app->ranking_game_key ? app->ranking_game_key : "",error));
        if (ok) ok=publish_active(app,provider,error) && frame_players(app,owner,provider,error);
        if (ok) {
            qa_ranking_state state=qa_rankings_state(app->rankings);
            if (state.kind==QA_RANKING_UNAVAILABLE) {
                char message[sizeof(state.reason)+32];
                (void)snprintf(message,sizeof(message),"Rankings unavailable: %s\n",state.reason);
                ok=application_native_q3_console_print(provider,message,error);
            }
        }
    }
    if (ok) ok=publish_active(app,provider,error);
    if (ok) ok=reconcile_clients(app,owner,provider,error);
    if (ok) ok=frame_players(app,owner,provider,error);
    if (ok) ok=qa_modes_read(app->modes,app->primary_mode,&mode,error);
    if (ok && (mode.phase==QA_MODE_INTERMISSION || mode.phase==QA_MODE_FINISHED))
        ok=game_over(app,owner,provider,error);
    qa_error active_error={0};
    bool published=publish_active(app,provider,&active_error);
    if (!published) { ok=false; owner->backend_failed=false; if (error) *error=active_error; }
    owner->busy=false; return ok;
}
bool application_rankings_frame_ordinary(qa_application *app, qa_error *error)
{
    application_rankings *owner=app ? app->ranked_source : NULL;
    if (owner && !owner->busy) owner->backend_failed=false;
    application_provider *provider=source(app);
    if (owner && provider && !owner->busy && !owner->restoring) {
        const qa_cvar_view *enable=qa_cvars_find(application_native_q3_console_registry(provider),"sv_enableRankings");
        qa_ranking_service_kind kind=qa_rankings_state(app->rankings).kind;
        if (enable && enable->owner==provider->owner && owner->enabled==(enable->number!=0) &&
            (kind==QA_RANKING_DISABLED || kind==QA_RANKING_UNAVAILABLE))
            return agreement(app,owner,error) && publish_active(app,provider,error);
    }
    qa_error local={0};
    if (application_rankings_frame(app,&local)) return true;
    owner=app ? app->ranked_source : NULL;
    if (owner && !owner->busy && owner->backend_failed) {
        char text[sizeof(local.message)+32];
        (void)snprintf(text,sizeof(text),"Ranking service failed: %s\n",local.message);
        return application_native_q3_console_print(source(app),text,error);
    }
    if (error) *error=local;
    return false;
}
bool qa_application_rankings_start(qa_application *app, qa_error *error)
{
    if (!app || app->operation!=APPLICATION_IDLE || app->q3_round_active || app->frame_preparing || app->q1_original_save ||
        app->publication_started || app->destroy_requested || app->finalizing || app->pending_close ||
        (app->state!=QA_APPLICATION_READY && app->state!=QA_APPLICATION_RUNNING) ||
        !application_guests_idle(app) || !qa_session_safe(app->session) ||
        (app->world && !qa_world_idle(app->world)))
        return application_fail(error,QA_ERROR_ARGUMENT,"Initial rankings require the actual published idle map and output owner");
    bool ok=app->ranked_source ? agreement(app,app->ranked_source,error) :
        application_rankings_frame(app,error);
    return ok && application_native_q3_clients_drain(app,error);
}
bool application_rankings_round_close(qa_application *app, application_provider *provider, qa_error *error)
{
    application_rankings *owner=app ? app->ranked_source : NULL;
    if (!owner) return true;
    if (!provider || owner->provider!=provider->owner || owner->busy || owner->restoring)
        return application_fail(error,QA_ERROR_ARGUMENT,"Ranking close requires its idle actual source lifecycle");
    owner->busy=true;
    qa_mode_view mode;
    bool ok=qa_modes_read(app->modes,app->primary_mode,&mode,error);
    if (ok && (mode.phase==QA_MODE_INTERMISSION || mode.phase==QA_MODE_FINISHED)) ok=game_over(app,owner,provider,error);
    if (ok) ok=flush(app,owner,error);
    qa_error end_error={0};
    bool ended=qa_rankings_end(app->rankings,&end_error);
    if (ok && !ended) { ok=false; if (error) *error=end_error; }
    app->ranked_source=NULL;
    qa_error publish_error={0}; bool published=publish_active(app,provider,&publish_error);
    if (ok && !published) { ok=false; if (error) *error=publish_error; }
    owner_free(owner); return ok;
}
bool application_rankings_close(qa_application *app, qa_error *error)
{
    application_rankings *owner=app ? app->ranked_source : NULL;
    if (!owner) return true;
    if (owner->busy) return application_fail(error,QA_ERROR_ARGUMENT,"Ranked source is in an operation");
    if (owner->restoring) { application_rankings_dispose(app); return true; }
    application_provider *provider=source(app);
    if (!provider || !agreement(app,owner,error))
        return application_fail(error,QA_ERROR_ARGUMENT,"Ranked source must close before its actual map retires");
    return application_rankings_round_close(app,provider,error);
}

static size_t remaining(const qa_source_save_io *io)
{
    return io->direction==QA_SOURCE_SAVE_READ && io->offset<=io->input.size
        ? io->input.size-io->offset : SIZE_MAX;
}
static bool text(qa_source_save_io *io, char **value)
{
    bool present=*value!=NULL;
    if (!qa_source_save_bool(io,&present)) return false;
    if (!present) { if (io->direction==QA_SOURCE_SAVE_READ) *value=NULL; return true; }
    size_t length=io->direction==QA_SOURCE_SAVE_WRITE ? strlen(*value) : 0;
    if (!qa_source_save_count(io,&length,remaining(io)) || length==SIZE_MAX) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) {
        if (length>remaining(io))
            return application_fail(io->error,QA_ERROR_FORMAT,"Truncated ranked source text");
        if (length && memchr(io->input.data+io->offset,0,length))
            return application_fail(io->error,QA_ERROR_FORMAT,"Ranked source text contains NUL");
        *value=malloc(length+1);
        if (!*value) return application_fail(io->error,QA_ERROR_MEMORY,"Allocating ranked source text");
        (*value)[length]=0;
    }
    return qa_source_save_bytes(io,*value,length);
}
static bool span(qa_source_save_io *io, qa_bytes *value)
{
    size_t length=value->size;
    if (!qa_source_save_count(io,&length,remaining(io))) return false;
    if (io->direction==QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io,(void *)value->data,length);
    if (length>remaining(io)) return application_fail(io->error,QA_ERROR_FORMAT,"Truncated ranked source frame");
    *value=(qa_bytes){length ? io->input.data+io->offset : NULL,length}; io->offset+=length; return true;
}
typedef struct ranking_record {
    char *key;
    bool effect;
    qa_bytes bridge, owner;
} ranking_record;
static bool record_fields(qa_source_save_io *io, ranking_record *record)
{
    uint8_t magic[4]={'Q','A','R','S'};
    return qa_source_save_bytes(io,magic,4) && !memcmp(magic,"QARS",4) &&
        text(io,&record->key) &&
        qa_source_save_bool(io,&record->effect) && span(io,&record->bridge) &&
        record->effect==(record->bridge.size!=0) && span(io,&record->owner) && record->owner.size;
}
static bool record_read(qa_bytes bytes, ranking_record *record, qa_error *error)
{
    qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && record_fields(&io,record) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if (!ok && error && error->code==QA_OK) application_fail(error,QA_ERROR_FORMAT,"Invalid ranked source framing");
    return ok;
}
static bool key_equal(const char *a,const char *b)
{
    return (!a && !b) || (a && b && !strcmp(a,b));
}
static bool player_fields(qa_source_save_io *io, qa_ranking_player *state)
{
    uint32_t kind=(uint32_t)state->kind;
    if (!qa_source_save_u32(io,&kind) || kind>QA_RANKING_DENIED_PLAYER ||
        !qa_source_save_u64(io,&state->account.player_id) || !qa_source_save_f64(io,&state->account.rank) ||
        !qa_source_save_bytes(io,state->reason,sizeof(state->reason)) || !memchr(state->reason,0,sizeof(state->reason))) return false;
    state->kind=(qa_ranking_player_kind)kind;
    return kind!=QA_RANKING_ACTIVE_PLAYER || isfinite(state->account.rank);
}
static bool owner_fields(qa_source_save_io *io, application_rankings **value)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ, present=*value!=NULL;
    if (!qa_source_save_bool(io,&present)) return false;
    if (!present) return true;
    if (reading) {
        *value=calloc(1,sizeof(**value));
        if (!*value) return application_fail(io->error,QA_ERROR_MEMORY,"Allocating saved ranked source owner");
        (*value)->report_tail=&(*value)->reports; (*value)->client_tail=&(*value)->clients;
        (*value)->restoring=true;
    }
    application_rankings *owner=*value;
    if (!qa_source_save_string(io,&owner->provider) || !owner->provider ||
        !qa_source_save_u64(io,&owner->map_revision) ||
        !qa_source_save_bool(io,&owner->enabled) || !qa_source_save_bool(io,&owner->ended)) return false;
    size_t count=owner->report_count;
    if (!qa_source_save_count(io,&count,65536) || (reading && count>remaining(io)/25)) return false;
    ranking_report *report=owner->reports;
    for (size_t i=0;i<count;++i) {
        if (reading) {
            report=calloc(1,sizeof(*report));
            if (!report) return application_fail(io->error,QA_ERROR_MEMORY,"Allocating saved ranking report");
            *owner->report_tail=report; owner->report_tail=&report->next; ++owner->report_count;
        }
        if (!report) return application_fail(io->error,QA_ERROR_ARGUMENT,"Ranking FIFO changed during capture");
        qa_ranking_source_report *row=&report->value; uint32_t kind=(uint32_t)row->stat.kind;
        if (!qa_source_save_i32(io,&row->self) || !qa_source_save_i32(io,&row->other) ||
            !qa_source_save_i32(io,&row->stat.key) || !qa_source_save_u32(io,&kind) || kind>QA_RANKING_STRING) return false;
        row->stat.kind=(qa_ranking_report_kind)kind;
        if (kind==QA_RANKING_STRING) {
            if (!text(io,&report->text) || !report->text) return false;
            row->stat.value.string=report->text;
        } else if (!qa_source_save_f64(io,&row->stat.value.integer.value) ||
            !qa_source_save_bool(io,&row->stat.value.integer.accumulate)) return false;
        if (!reading) report=report->next;
    }
    if (!reading && report) return application_fail(io->error,QA_ERROR_ARGUMENT,"Ranking FIFO extent changed");
    count=owner->client_count;
    if (!qa_source_save_count(io,&count,QA_Q3_SOURCE_CLIENTS) ||
        (reading && count>remaining(io)/303)) return false;
    ranking_client *client=owner->clients;
    for (size_t i=0;i<count;++i) {
        if (reading) {
            client=calloc(1,sizeof(*client));
            if (!client) return application_fail(io->error,QA_ERROR_MEMORY,"Allocating saved ranked client");
            *owner->client_tail=client; owner->client_tail=&client->next; ++owner->client_count;
        }
        uint32_t weapon=client ? (uint32_t)client->weapon : 0;
        if (!client || !qa_source_save_actor(io,&client->actor) || !client->actor.registry ||
            !qa_source_save_u32(io,&client->slot) || client->slot>=QA_Q3_SOURCE_CLIENTS ||
            !qa_source_save_bool(io,&client->has_observed) || !player_fields(io,&client->observed) ||
            !qa_source_save_bool(io,&client->has_weapon) || !qa_source_save_u32(io,&weapon) || weapon>=QA_Q3_WEAPON_COUNT ||
            !qa_source_save_i32(io,&client->since)) return false;
        client->weapon=(qa_q3_weapon)weapon;
        for (ranking_client *prior=owner->clients; prior!=client; prior=prior->next)
            if (prior->slot==client->slot || qa_actor_id_equal(prior->actor,client->actor))
                return application_fail(io->error,QA_ERROR_FORMAT,"Duplicate ranked client source identity");
        if (!reading) client=client->next;
    }
    return reading || !client || application_fail(io->error,QA_ERROR_ARGUMENT,"Ranked client extent changed");
}
bool application_rankings_prepare(const qa_application_options *options,
    const qa_application_persistence_ops *ops, qa_bytes bytes, qa_error *error)
{
    if (!options) return application_fail(error,QA_ERROR_ARGUMENT,"Ranked source admission lacks actual application options");
    ranking_record record={0}; bool ok=record_read(bytes,&record,error);
    if (ok && (!key_equal(options->ranking_game_key,record.key) ||
        record.effect!=(options->ranking_effect!=NULL)))
        ok=application_fail(error,QA_ERROR_FORMAT,"Saved ranked source key or consumer presence changed");
    if (ok && record.effect && (!ops || !ops->ranking_source ||
        !ops->ranking_source->capture || !ops->ranking_source->resolve))
        ok=application_fail(error,QA_ERROR_UNSUPPORTED,"Ranked source output bridge lacks its real logical binding codec");
    free(record.key); return ok;
}
bool application_rankings_capture(qa_application *app, const qa_application_persistence_ops *ops,
    qa_buffer *out, qa_error *error)
{
    if (!app || !out || out->data || out->size || !application_rankings_idle(app))
        return application_fail(error,QA_ERROR_ARGUMENT,"Ranked source capture requires idle actual owners and an empty output");
    qa_buffer bridge={0}, body={0};
    bool ok=true;
    if (app->ranking_effect) {
        const qa_application_ranking_checkpoint_refs *refs=ops ? ops->ranking_source : NULL;
        if (!refs || !refs->capture || !refs->resolve)
            ok=application_fail(error,QA_ERROR_UNSUPPORTED,"Ranked source output bridge lacks actual continuation bindings");
        else ok=refs->capture(refs->context,app->ranking_effect,app->guest_context,&bridge,error) && bridge.size;
    }
    qa_source_save_io io={0};
    if (ok && app->ranked_source) ok=agreement(app,app->ranked_source,error);
    if (ok) ok=qa_source_save_writer(&io,app->session,error) && owner_fields(&io,&app->ranked_source) &&
        qa_source_save_finish(&io,&body);
    qa_source_save_dispose(&io); io=(qa_source_save_io){0};
    ranking_record record={.key=app->ranking_game_key,.effect=app->ranking_effect!=NULL,
        .bridge={bridge.data,bridge.size},.owner={body.data,body.size}};
    if (ok) ok=qa_source_save_writer(&io,NULL,error) && record_fields(&io,&record) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); qa_buffer_free(&bridge); qa_buffer_free(&body);
    if (!ok && error && error->code==QA_OK) application_fail(error,QA_ERROR_FORMAT,"Unqualified ranked source continuation");
    return ok;
}
bool application_rankings_restore(qa_application *app, const qa_application_persistence_ops *ops,
    qa_bytes bytes, qa_error *error)
{
    if (!app || app->operation!=APPLICATION_PERSISTING || app->ranked_source)
        return application_fail(error,QA_ERROR_ARGUMENT,"Ranked source import requires an empty leased candidate");
    ranking_record record={0}; application_rankings *owner=NULL; qa_source_save_io io={0};
    bool ok=record_read(bytes,&record,error);
    if (ok && (!key_equal(app->ranking_game_key,record.key) || record.effect!=(app->ranking_effect!=NULL)))
        ok=application_fail(error,QA_ERROR_FORMAT,"Ranked source differs from actual candidate construction");
    if (ok && record.effect) {
        const qa_application_ranking_checkpoint_refs *refs=ops ? ops->ranking_source : NULL;
        qa_application_ranking_effect_fn effect=NULL; void *context=NULL;
        ok=refs && refs->resolve && refs->resolve(refs->context,record.bridge,&effect,&context,error);
        if (ok && (effect!=app->ranking_effect || context!=app->guest_context))
            ok=application_fail(error,QA_ERROR_FORMAT,"Ranked source consumer differs from its actual prepared binding");
    }
    if (ok) ok=qa_source_save_reader(&io,app->session,record.owner,error) && owner_fields(&io,&owner) &&
        qa_source_save_finish(&io,NULL);
    if (ok && owner) {
        application_provider *provider=source(app);
        if (!provider || !provider->launch || provider->owner!=owner->provider)
            ok=application_fail(error,QA_ERROR_FORMAT,"Saved ranked source owner is absent");
        else {
            owner->source_serial=provider->launch->identity;
            ok=agreement(app,owner,error);
        }
    }
    if (ok) { app->ranked_source=owner; owner=NULL; }
    owner_free(owner); free(record.key); qa_source_save_dispose(&io);
    if (!ok && error && error->code==QA_OK) application_fail(error,QA_ERROR_FORMAT,"Invalid ranked source continuation");
    return ok;
}
bool application_rankings_restore_ready(qa_application *app, qa_error *error)
{
    if (!app) return application_fail(error,QA_ERROR_ARGUMENT,"Ranked source readiness lacks its actual application");
    application_rankings *owner=app->ranked_source;
    if (!owner) return true;
    if (owner->busy) return application_fail(error,QA_ERROR_ARGUMENT,"Ranked source readiness cannot interrupt an operation");
    if (!agreement(app,owner,error)) return false;
    application_provider *provider=source(app);
    uint32_t max_clients;
    if (!qa_q3_source_max_clients(provider->state.q3,&max_clients,error) ||
        owner->client_count>max_clients)
        return application_fail(error,QA_ERROR_FORMAT,"Ranked clients exceed their actual source capacity");
    const qa_actor_registry *actors=qa_session_actors(app->session);
    for (ranking_client *client=owner->clients; client; client=client->next) {
        if (client->slot>=max_clients)
            return application_fail(error,QA_ERROR_FORMAT,"Ranked client exceeds its actual source slot range");
        const qa_actor_record *actor=qa_actors_get(actors,client->actor);
        if (actor) {
            uint32_t native_slot;
            if (!qa_q3_native_client_slot(provider->state.q3,client->actor,&native_slot,error) ||
                native_slot!=client->slot)
                return application_fail(error,QA_ERROR_FORMAT,"Ranked client changed its actual source-slot ownership");
        }
    }
    return true;
}
void application_rankings_publish_restored(qa_application *app)
{
    if (app && app->ranked_source && !app->ranked_source->busy) app->ranked_source->restoring=false;
}
void application_rankings_relinquish(qa_application *app)
{
    if (app && app->ranked_source && !app->ranked_source->busy) app->ranked_source->restoring=true;
}
