#include "unified_q3_client.h"
#include "../application/native_q3_client_settings.h"
#include "remote_unified_save.h"
#include "video_guests.h"
#include "qa/network_q3_fields_save.h"
#include "qa/application_native_q3_cvars.h"
#include "qa/application_native_q3_client.h"

#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct client_snapshot {
    qa_q3_snapshot value;
    qa_q3_entity entities[256];
    qa_actor_id actors[256], player_actor;
    qa_actor_id bindings[QA_Q3_ENTITIES];
} client_snapshot;
typedef struct client_command {
    int32_t sequence;
    qa_command_tokens arguments;
} client_command;
typedef struct client_history {
    client_snapshot snapshots[32];
    client_command commands[64];
    qa_q3_gamestate authority, reached;
    uint64_t string_revisions[QA_Q3_CONFIGSTRINGS];
    qa_actor_id actors[QA_Q3_ENTITIES];
    int32_t number, time, reliable, command_sequence;
    uint64_t event_sequence;
    bool has_event_sequence, unsealed_snapshot;
} client_history;
struct frontend_unified_q3_client {
    frontend_remote_unified *replica;
    frontend_unified_q3_sources *sources;
    frontend_unified_q3_source_view constructor;
    const frontend_remote_unified_domain *domain;
    uint64_t receiver;
    qa_command_context command_context;
    char *provider_name, *instance;
    client_history *history;
    frontend_unified_q3_client_frame *prepared;
    frontend_unified_q3_client_video *video;
    frontend_unified_q3_source_retirement *retirement;
    q3n_compiled_source *source;
    uint64_t revision;
    qa_native_q3_client_cvar *cvar_cache;
    size_t cvar_count;
    int32_t local_server;
    bool busy, registered, initialized;
};
struct frontend_unified_q3_client_frame {
    frontend_unified_q3_client *owner;
    frontend_unified_q3_source_view source;
    client_history *history;
    uint64_t revision;
    q3n_compiled_source_rebind_ticket *rebind;
    q3n_compiled_source_basis candidate;
    qa_command_context command_context;
};
struct frontend_unified_q3_client_video {
    frontend_unified_q3_client *owner;
    qa_frontend *frontend;
    const frontend_video_guests *aggregate;
    frontend_unified_q3_source_view source;
    client_history *history;
    uint64_t revision;
    int32_t message, time, reliable, reached;
    bool begun;
};
static bool fail(qa_error *e, qa_status code, const char *s)
{ qa_error_set(e,code,0,"%s",s); return false; }
static char *copy(const char *s)
{ size_t n = strlen(s)+1; char *p = malloc(n); if (p) memcpy(p,s,n); return p; }
static bool activation(const frontend_unified_q3_client *c, const frontend_unified_q3_source_view *v)
{
    const frontend_unified_q3_source_view *a = &c->constructor;
    return v->provider_name && v->owner == c->sources && v->epoch == a->epoch && v->provider == a->provider &&
        v->publication == a->publication && v->map_revision == a->map_revision && v->product == a->product &&
        v->files == a->files && v->assets == a->assets && v->max_clients == a->max_clients &&
        v->has_client && v->client_number == a->client_number &&
        !strcmp(v->provider_name,c->provider_name) && !strcmp(v->instance,c->instance);
}
static bool identity(const frontend_unified_q3_client *c, const frontend_unified_q3_source_view *v)
{ return activation(c,v) && qa_actor_id_equal(v->viewer,c->constructor.viewer) && v->snapshot_bit == c->constructor.snapshot_bit; }
static bool observation(const frontend_unified_q3_client *c, frontend_unified_q3_source_view *out)
{
    if (!c || c->domain != frontend_remote_unified_domain_read(c->replica) ||
        !frontend_unified_q3_sources_current(c->sources)) return false;
    for (size_t i = 0; i < frontend_unified_q3_sources_committed_count(c->sources); ++i) {
        frontend_unified_q3_source_view v;
        if (!frontend_unified_q3_sources_committed_read(c->sources,i,&v,NULL)) return false;
        if (identity(c,&v)) { *out = v; return true; }
    }
    for (size_t i = 0; i < frontend_unified_q3_sources_count(c->sources); ++i) {
        frontend_unified_q3_source_view v;
        if (!frontend_unified_q3_sources_read(c->sources,i,&v,NULL)) return false;
        if (identity(c,&v)) { *out = v; return true; }
    }
    return false;
}
bool frontend_unified_q3_client_current(const frontend_unified_q3_client *c)
{ frontend_unified_q3_source_view v; return observation(c,&v); }
static bool cold_source_observation(const frontend_unified_q3_client *c,frontend_unified_q3_source_view *out)
{
    if (!c || c->domain != frontend_remote_unified_domain_read(c->replica) ||
        !frontend_unified_q3_sources_checkpoint_current(c->sources)) return false;
    if (c->retirement && frontend_unified_q3_source_retirement_checkpoint_current(c->retirement) &&
        frontend_unified_q3_source_retirement_read(c->retirement,out,NULL) && identity(c,out)) return true;
    for (size_t i = 0; i < frontend_unified_q3_sources_committed_count(c->sources); ++i) {
        frontend_unified_q3_source_view v;
        if (!frontend_unified_q3_sources_checkpoint_read(c->sources,i,&v,NULL)) return false;
        if (identity(c,&v)) { *out = v; return true; }
    }
    if (frontend_unified_q3_source_checkpoint_current(&c->constructor)) { *out = c->constructor; return true; }
    return false;
}
static bool cold_observation(const frontend_unified_q3_client *c,frontend_unified_q3_source_view *out)
{ return c && !c->prepared && cold_source_observation(c,out); }
bool frontend_unified_q3_client_checkpoint_current(const frontend_unified_q3_client *c)
{ frontend_unified_q3_source_view v; return frontend_unified_q3_client_idle(c) && cold_observation(c,&v); }
bool frontend_unified_q3_client_checkpoint_matches(const frontend_unified_q3_client *c,const frontend_unified_q3_source_view *v)
{
    frontend_unified_q3_source_view retained;
    return c && v && identity(c,v) && (frontend_unified_q3_source_checkpoint_current(v) ||
        (c->retirement && frontend_unified_q3_source_retirement_checkpoint_current(c->retirement) &&
            frontend_unified_q3_source_retirement_read(c->retirement,&retained,NULL) &&
            retained.source == v->source && retained.revision == v->revision));
}
bool frontend_unified_q3_client_matches(const frontend_unified_q3_client *c, const frontend_unified_q3_source_view *v)
{ return c && v && activation(c,v) &&
    (qa_actor_id_equal(v->viewer,c->constructor.viewer) || v->snapshot_bit != c->constructor.snapshot_bit) &&
    frontend_unified_q3_source_current(v); }
bool frontend_unified_q3_client_event_matches(const frontend_unified_q3_client *c,const char *provider_name,
    const char *content,uint32_t source_epoch)
{
    frontend_unified_q3_source_view v;
    return provider_name && content && observation(c,&v) && v.epoch == source_epoch &&
        !strcmp(v.provider_name,provider_name) && !strcmp(v.content,content) && frontend_unified_q3_source_current(&v);
}
bool frontend_unified_q3_client_idle(const frontend_unified_q3_client *c)
{ return !c || (!c->busy && !c->prepared && !c->video); }
bool frontend_unified_q3_client_retirement_current(const frontend_unified_q3_client *c)
{
    frontend_unified_q3_source_view view;
    return c && c->retirement && !c->prepared && !c->video && c->domain == frontend_remote_unified_domain_read(c->replica) &&
        frontend_unified_q3_source_retirement_read(c->retirement,&view,NULL) && identity(c,&view);
}
bool frontend_unified_q3_client_retirement_departed(const frontend_unified_q3_client *c)
{
    return frontend_unified_q3_client_idle(c) && frontend_unified_q3_client_retirement_current(c) &&
        frontend_unified_q3_source_retirement_departed(c->retirement);
}
bool frontend_unified_q3_client_retirement_bind(frontend_unified_q3_client *c,
    frontend_unified_q3_source_retirement *t,qa_error *e)
{
    frontend_unified_q3_source_view view;
    if (!c || c->retirement || !frontend_unified_q3_client_idle(c) ||
        !frontend_unified_q3_source_retirement_read(t,&view,e) || !identity(c,&view) ||
        !frontend_unified_q3_source_retirement_client_hold(t,c,e))
        return fail(e,QA_ERROR_ARGUMENT,"Compiled CLIENT cleanup requires its exact retained Source row");
    c->retirement = t; return true;
}
const qa_command_context *frontend_unified_q3_client_retirement_context(const frontend_unified_q3_client *c)
{ return frontend_unified_q3_client_retirement_current(c) ? &c->command_context : NULL; }
qa_cvars *frontend_unified_q3_client_retirement_cvars(const frontend_unified_q3_client *c)
{ return frontend_unified_q3_client_retirement_current(c) ? c->domain->cvars : NULL; }
bool frontend_unified_q3_client_retirement_unbind(frontend_unified_q3_client *c,
    frontend_unified_q3_source_retirement *t,qa_error *e)
{
    frontend_unified_q3_source_view view;
    if (!c || c->retirement != t || !frontend_unified_q3_client_idle(c) ||
        !frontend_unified_q3_source_retirement_read(t,&view,e) || !identity(c,&view) ||
        !frontend_unified_q3_source_current(&view) || !frontend_unified_q3_source_retirement_client_drop(t,c,e))
        return fail(e,QA_ERROR_ARGUMENT,"Compiled retirement rollback requires its still-published actual Source");
    c->retirement = NULL; return true;
}
static void history_free(client_history *h)
{ if (h) { for (size_t i = 0; i < 64; ++i) qa_command_tokens_free(&h->commands[i].arguments); free(h); } }
static bool arguments(const char *const *values, size_t count, qa_command_tokens *out, qa_error *e)
{
    size_t bytes = 1;
    for (size_t i = 0; i < count; ++i) {
        size_t n = strlen(values[i])+1;
        if (n > SIZE_MAX-bytes) return fail(e,QA_ERROR_MEMORY,"Compiled reliable arguments overflow");
        bytes += n;
    }
    qa_command_tokens t = {.count=count};
    t.values = count ? calloc(count,sizeof(*t.values)) : NULL;
    t.storage = malloc(bytes); t.args_text = malloc(bytes);
    if ((count && !t.values) || !t.storage || !t.args_text) {
        qa_command_tokens_free(&t); return fail(e,QA_ERROR_MEMORY,"Retaining compiled reliable arguments");
    }
    size_t at = 0, joined = 0;
    for (size_t i = 0; i < count; ++i) {
        size_t n = strlen(values[i]); t.values[i] = t.storage+at;
        memcpy(t.storage+at,values[i],n+1); at += n+1;
        if (i) { if (i > 1) t.args_text[joined++] = ' '; memcpy(t.args_text+joined,values[i],n); joined += n; }
    }
    t.storage[at] = 0; t.args_text[joined] = 0; *out = t; return true;
}
static bool history_clone(const client_history *old, client_history **out, qa_error *e)
{
    client_history *h = malloc(sizeof(*h));
    if (!h) return fail(e,QA_ERROR_MEMORY,"Retaining compiled CLIENT history candidate");
    *h = *old;
    for (size_t i = 0; i < 64; ++i) h->commands[i].arguments = (qa_command_tokens){0};
    for (size_t i = 0; i < 32; ++i) h->snapshots[i].value.entities = h->snapshots[i].entities;
    for (size_t i = 0; i < 64; ++i) {
        const qa_command_tokens *a = &old->commands[i].arguments;
        if (!arguments((const char *const *)a->values,a->count,&h->commands[i].arguments,e)) { history_free(h); return false; }
        free(h->commands[i].arguments.args_text);
        h->commands[i].arguments.args_text = copy(a->args_text ? a->args_text : "");
        if (!h->commands[i].arguments.args_text) { history_free(h); return fail(e,QA_ERROR_MEMORY,"Retaining compiled command tail"); }
    }
    *out = h; return true;
}
static bool append(client_history *h, const char *const *values, size_t count, qa_error *e)
{
    if (h->reliable == INT32_MAX) return fail(e,QA_ERROR_FORMAT,"Compiled reliable command history is exhausted");
    qa_command_tokens t = {0};
    if (!arguments(values,count,&t,e)) return false;
    int32_t sequence = h->reliable+1; client_command *row = h->commands+((uint32_t)sequence&63u);
    qa_command_tokens_free(&row->arguments); *row = (client_command){sequence,t}; h->reliable = sequence; return true;
}
static int32_t source_integer(const char *text)
{
    while (*text && (signed char)*text <= 32) ++text;
    bool negative = *text == '-';
    if (*text == '+' || *text == '-') ++text;
    uint32_t value = 0;
    while (*text >= '0' && *text <= '9') value = value*10u+(uint32_t)(*text++-'0');
    value = negative ? 0u-value : value;
    int32_t result; memcpy(&result,&value,sizeof(result)); return result;
}
static bool receive(client_history *h, const frontend_unified_q3_source_view *v, bool initial, bool round, qa_error *e)
{
    if (!v->has_client || !v->area_mask || v->visible_count > 256 || (h->number && v->time < h->time))
        return fail(e,QA_ERROR_FORMAT,"Compiled CLIENT cannot receive a rewound or unbound actual Source");
    const frontend_unified_q3_source_player *player = NULL;
    for (size_t i = 0; i < v->player_count; ++i)
        if (v->players[i].source_number == v->client_number && qa_actor_id_equal(v->players[i].actor,v->viewer)) player = v->players+i;
    if (!player) return fail(e,QA_ERROR_FORMAT,"Compiled CLIENT lost its actual physical Source player");
    if (initial) { h->authority = *v->game_state; h->reached = *v->game_state; }
    else for (uint32_t i = 0; i < QA_Q3_CONFIGSTRINGS; ++i) {
        const char *value = qa_q3_configstring(v->game_state,i);
        if (!strcmp(qa_q3_configstring(&h->authority,i),value)) continue;
        char index[16]; snprintf(index,sizeof(index),"%u",i);
        const char *argv[] = {"cs",index,value};
        if (!append(h,argv,3,e) || !qa_q3_configstring_set(&h->authority,i,value,e)) return false;
    }
    if (round) {
        const char *argv[] = {"map_restart"};
        if (!append(h,argv,1,e)) return false;
        memset(h->actors,0,sizeof(h->actors));
    }
    for (uint32_t i = 0; i < QA_Q3_ENTITY_NONE; ++i)
        if (v->entities[i].present) h->actors[i] = v->entities[i].actor;
    for (size_t i = 0; i < v->player_count; ++i) h->actors[v->players[i].source_number] = v->players[i].actor;
    if (h->number && v->time == h->time) return true;
    if (h->number == INT32_MAX) return fail(e,QA_ERROR_FORMAT,"Compiled snapshot history is exhausted");
    int32_t previous = h->number; client_snapshot *row = h->snapshots+((uint32_t)(previous+1)&31u);
    memset(row,0,sizeof(*row));
    row->value = (qa_q3_snapshot){.valid=true,.message_number=previous+1,.server_time=v->time,
        .delta_number=previous ? previous : -1,.server_command_number=h->reliable,.area_bytes=32,
        .flags=v->snapshot_bit,
        .player=player->state,.entity_count=v->visible_count,.entities=row->entities};
    memcpy(row->value.area_mask,v->area_mask,sizeof(row->value.area_mask)); row->player_actor = player->actor;
    memcpy(row->bindings,h->actors,sizeof(row->bindings));
    for (size_t i = 0; i < v->visible_count; ++i) {
        uint16_t number = v->visible_entities[i];
        row->entities[i] = v->entities[number].state; row->actors[i] = v->entities[number].actor;
    }
    h->number = previous+1; h->time = v->time; h->unsealed_snapshot = true; return true;
}
static bool source_basis(frontend_unified_q3_client *c,const frontend_unified_q3_source_view *v,
    const client_history *h,uint64_t revision,q3n_compiled_source_basis *out,qa_error *e)
{
    char value[8192]; const char *info = qa_q3_configstring(&h->reached,0);
    if (!qa_q3_info_value(info,"g_gametype",value,sizeof(value),e)) return false;
    int32_t game_type = source_integer(value);
    if (!qa_q3_info_value(info,"sv_maxclients",value,sizeof(value),e)) return false;
    int32_t max_clients = source_integer(value);
    *out = (q3n_compiled_source_basis){.application=c->domain->application,
        .registry=frontend_remote_unified_registry(c->replica),.provider=v->provider->source_owner,.receiver=c->receiver,
        .instance=c->instance,.content=v->files,.assets=v->assets,.product=v->product,
        .publication=v->publication,.map_revision=v->map_revision,.serial=revision,.viewer=v->viewer,
        .seat=c->domain->seat.index,.physical_seat=c->domain->physical_seat,.client_number=(int32_t)v->client_number,
        .time=h->time,.game_type=game_type,.max_clients=max_clients,.level_start_time=source_integer(qa_q3_configstring(&h->reached,21)),
        .snapshot_bit=v->snapshot_bit,
        .initial_command=0,.reached_command=h->command_sequence,.initialized=c->initialized}; return true;
}
static bool source_read(void *context, q3n_compiled_source_basis *out, qa_error *e)
{
    frontend_unified_q3_client *c = context; frontend_unified_q3_source_view v;
    if (!out || !observation(c,&v)) return fail(e,QA_ERROR_ARGUMENT,"Compiled CLIENT source receipt has retired");
    return source_basis(c,&v,c->history,c->revision,out,e);
}
static bool checkpoint_read(void *context,q3n_compiled_source_basis *out,qa_error *e)
{
    frontend_unified_q3_client *c = context; frontend_unified_q3_source_view v;
    if (!out || !c || c->busy || c->video || !(observation(c,&v) || cold_source_observation(c,&v)))
        return fail(e,QA_ERROR_ARGUMENT,"Compiled CLIENT cold receipt lost its actual retained owner");
    return source_basis(c,&v,c->history,c->revision,out,e);
}
static bool basis_equal(const q3n_compiled_source_basis *a,const q3n_compiled_source_basis *b)
{
    return a->application == b->application && a->session == b->session && a->registry == b->registry &&
        a->provider == b->provider && a->receiver == b->receiver && a->instance == b->instance &&
        a->content == b->content && a->assets == b->assets && a->product == b->product &&
        a->publication == b->publication && a->map_revision == b->map_revision && a->serial == b->serial &&
        qa_actor_id_equal(a->viewer,b->viewer) && a->seat == b->seat && a->physical_seat == b->physical_seat &&
        a->client_number == b->client_number && a->time == b->time && a->game_type == b->game_type &&
        a->max_clients == b->max_clients && a->level_start_time == b->level_start_time &&
        a->initial_command == b->initial_command && a->reached_command == b->reached_command &&
        a->snapshot_bit == b->snapshot_bit && a->initialized == b->initialized;
}
static bool source_current(void *context, const q3n_compiled_source_basis *b)
{
    frontend_unified_q3_client *c = context; q3n_compiled_source_basis a;
    if (source_read(context,&a,NULL) && basis_equal(&a,b)) return true;
    const frontend_unified_q3_client_frame *t = c ? c->prepared : NULL;
    return t && t->owner == c && frontend_unified_q3_source_current(&t->source) &&
        source_basis(c,&t->source,t->history,t->revision,&a,NULL) && basis_equal(&a,b);
}
static bool checkpoint_current(void *context,const q3n_compiled_source_basis *b)
{
    frontend_unified_q3_client *c = context; q3n_compiled_source_basis a;
    if (checkpoint_read(context,&a,NULL) && basis_equal(&a,b)) return true;
    const frontend_unified_q3_client_frame *t = c ? c->prepared : NULL;
    return t && !c->busy && !c->video && t->owner == c &&
        frontend_unified_q3_source_checkpoint_current(&t->source) &&
        source_basis(c,&t->source,t->history,t->revision,&a,NULL) && basis_equal(&a,b);
}
static bool configstring(void *context, uint32_t index, const char **out, uint64_t *revision, qa_error *e)
{
    frontend_unified_q3_client *c = context;
    if (!out || !revision || index >= QA_Q3_CONFIGSTRINGS || !frontend_unified_q3_client_current(c))
        return fail(e,QA_ERROR_ARGUMENT,"Compiled CG configstring lost its actual reached owner");
    *out = qa_q3_configstring(&c->history->reached,index); *revision = c->history->string_revisions[index]; return true;
}
static bool source_idle(void *context) { return frontend_unified_q3_client_idle(context); }
bool frontend_unified_q3_client_actor_fields(frontend_unified_q3_client *c, qa_source_save_io *io, qa_actor_id *actor)
{
    if (!c || !io || !actor) return false;
    bool present = actor->registry != 0; qa_saved_actor_id wire = {0};
    if (!qa_source_save_bool(io,&present)) return false;
    if (!present) { if (io->direction == QA_SOURCE_SAVE_READ) *actor = (qa_actor_id){0}; return true; }
    if (io->direction == QA_SOURCE_SAVE_WRITE && !frontend_remote_unified_wire_actor(c->replica,*actor,&wire)) return false;
    return qa_source_save_u32(io,&wire.slot) && qa_source_save_u64(io,&wire.generation) &&
        (io->direction != QA_SOURCE_SAVE_READ || frontend_remote_unified_actor_retained(c->replica,wire.slot,wire.generation,actor,io->error));
}
static bool actor_known(void *context,qa_actor_id actor)
{
    frontend_unified_q3_client *c = context; qa_saved_actor_id wire;
    return c && actor.registry == c->constructor.viewer.registry &&
        frontend_remote_unified_wire_actor(c->replica,actor,&wire);
}
static bool client_actor(void *context, uint32_t slot, qa_actor_id *actor, bool *present, qa_error *e)
{
    frontend_unified_q3_client *c = context; frontend_unified_q3_source_view v;
    if (!actor || !present || !observation(c,&v) || slot >= v.max_clients)
        return fail(e,QA_ERROR_ARGUMENT,"Compiled voice sender lost its actual physical client roster");
    *actor = (qa_actor_id){0}; *present = false;
    for (size_t i = 0; i < v.player_count; ++i)
        if (v.players[i].source_number == slot && v.players[i].client_slot == slot) {
            *actor = v.players[i].actor; *present = true; break;
        }
    return frontend_unified_q3_source_current(&v);
}
static bool create_source(frontend_unified_q3_client *c,bool restoring,qa_error *e)
{
    q3n_compiled_source_options options = {.context=c,.read=source_read,.current=source_current,
        .configstring=configstring,.idle=source_idle,.client_actor=client_actor,.actor_known=actor_known,
        .checkpoint_read=checkpoint_read,.checkpoint_current=checkpoint_current};
    return restoring ? q3n_compiled_source_create_restored(&options,&c->source,e) :
        q3n_compiled_source_create(&options,&c->source,e);
}
static bool create(frontend_remote_unified *replica, frontend_unified_q3_sources *sources,
    const frontend_unified_q3_source_view *v, uint64_t receiver,bool restoring,
    frontend_unified_q3_source_retirement *retirement,frontend_unified_q3_client **out, qa_error *e)
{
    if (!replica || !sources || !v || v->owner != sources || !v->has_client || !receiver || !out || *out ||
        !(restoring ? retirement ? frontend_unified_q3_source_retirement_checkpoint_current(retirement) :
            frontend_unified_q3_source_checkpoint_current(v) : frontend_unified_q3_source_current(v)))
        return fail(e,QA_ERROR_ARGUMENT,"Compiled CLIENT requires its real bound Source and receiver namespace");
    frontend_unified_q3_client *c = calloc(1,sizeof(*c));
    if (!c) return fail(e,QA_ERROR_MEMORY,"Retaining compiled CLIENT transport");
    c->replica = replica; c->sources = sources; c->constructor = *v; c->receiver = receiver;
    c->domain = frontend_remote_unified_domain_read(replica); c->revision = 1;
    if (c->domain) { c->command_context = c->domain->command_context;
        c->command_context.owner = receiver; c->command_context.actor = v->viewer;
        c->command_context.registry = v->viewer.registry; c->command_context.generation = v->publication;
        c->command_context.dialect = QA_CONSOLE_Q3; }
    c->provider_name = copy(v->provider_name); c->instance = copy(v->instance); c->history = calloc(1,sizeof(*c->history));
    bool ok = c->domain && c->provider_name && c->instance && c->history && (!retirement || frontend_unified_q3_client_retirement_bind(c,retirement,e)) &&
        (restoring || (receive(c->history,v,true,false,e) && create_source(c,false,e)));
    if (!ok) { frontend_unified_q3_client_destroy(&c,NULL); return e && e->code ? false : fail(e,QA_ERROR_MEMORY,"Retaining compiled CLIENT source declaration"); }
    *out = c; return true;
}
bool frontend_unified_q3_client_create(frontend_remote_unified *replica,frontend_unified_q3_sources *sources,
    const frontend_unified_q3_source_view *v,uint64_t receiver,frontend_unified_q3_client **out,qa_error *e)
{ return create(replica,sources,v,receiver,false,NULL,out,e); }
bool frontend_unified_q3_client_prepare(frontend_unified_q3_client *c, const frontend_unified_q3_source_view *v,
    frontend_unified_q3_client_frame **out, qa_error *e)
{
    if (!c || !v || !out || *out || c->busy || c->prepared || c->video || c->revision == UINT64_MAX ||
        !frontend_unified_q3_client_matches(c,v))
        return fail(e,QA_ERROR_ARGUMENT,"Compiled CLIENT frame requires its actual same Source activation");
    frontend_unified_q3_client_frame *t = calloc(1,sizeof(*t));
    if (!t) return fail(e,QA_ERROR_MEMORY,"Retaining compiled CLIENT publication");
    t->owner = c; t->source = *v; t->revision = c->revision+1;
    t->command_context = c->command_context; t->command_context.actor = v->viewer;
    bool round = v->snapshot_bit != c->constructor.snapshot_bit;
    bool ok = history_clone(c->history,&t->history,e) && receive(t->history,v,false,round,e);
    if (!ok) { history_free(t->history); free(t); return false; }
    c->prepared = t;
    if (round) {
        q3n_compiled_source_view before;
        ok = q3n_compiled_source_read(c->source,&before,e) &&
            source_basis(c,v,t->history,t->revision,&t->candidate,e) &&
            q3n_compiled_source_rebind_prepare(c->source,&before,&t->candidate,&t->rebind,e);
    }
    if (!ok) { frontend_unified_q3_client_abort(&t); return false; }
    *out = t; return true;
}
bool frontend_unified_q3_client_ready(const frontend_unified_q3_client_frame *t)
{ return t && t->owner->prepared == t && !t->owner->busy && t->revision == t->owner->revision+1 &&
    frontend_unified_q3_client_matches(t->owner,&t->source) && frontend_unified_q3_source_current(&t->source) &&
    (!t->rebind || q3n_compiled_source_rebind_ready(t->rebind)); }
bool frontend_unified_q3_client_frame_owned(const frontend_unified_q3_client *c,
    const frontend_unified_q3_client_frame *t)
{ return c && t && c->prepared == t && t->owner == c; }
const q3n_compiled_source_rebind_ticket *frontend_unified_q3_client_frame_rebind(const frontend_unified_q3_client_frame *t)
{ return t && t->owner->prepared == t ? t->rebind : NULL; }
const qa_command_context *frontend_unified_q3_client_frame_context(const frontend_unified_q3_client_frame *t)
{ return t && t->owner->prepared == t ? &t->command_context : NULL; }
void frontend_unified_q3_client_commit(frontend_unified_q3_client_frame **out)
{
    if (!out || !*out) return;
    frontend_unified_q3_client_frame *t = *out; frontend_unified_q3_client *c = t->owner;
    history_free(c->history); c->history = t->history; c->revision = t->revision;
    c->constructor.viewer = t->source.viewer; c->constructor.snapshot_bit = t->source.snapshot_bit;
    c->command_context = t->command_context;
    q3n_compiled_source_rebind_commit(&t->rebind);
    c->prepared = NULL; free(t); *out = NULL;
}
void frontend_unified_q3_client_abort(frontend_unified_q3_client_frame **out)
{
    if (!out || !*out) return;
    frontend_unified_q3_client_frame *t = *out;
    q3n_compiled_source_rebind_abort(&t->rebind);
    if (t->owner->prepared == t) t->owner->prepared = NULL;
    history_free(t->history); free(t); *out = NULL;
}
bool frontend_unified_q3_client_destroy(frontend_unified_q3_client **out, qa_error *e)
{
    if (!out || !*out) return true;
    frontend_unified_q3_client *c = *out;
    if (!frontend_unified_q3_client_idle(c)) return fail(e,QA_ERROR_ARGUMENT,"Compiled CLIENT retains entered consumers or a candidate frame");
    if (!q3n_compiled_source_destroy(&c->source,e)) return false;
    if (c->retirement && !frontend_unified_q3_source_retirement_client_drop(c->retirement,c,e)) return false;
    history_free(c->history); free(c->cvar_cache); free(c->provider_name); free(c->instance); free(c); *out = NULL; return true;
}
bool frontend_unified_q3_client_video_current(const frontend_unified_q3_client_video *t)
{
    frontend_unified_q3_client *c = t ? t->owner : NULL; frontend_unified_q3_source_view v;
    return c && c->video == t && !c->busy && !c->prepared && c->history == t->history &&
        frontend_video_guests_parent_is(t->frontend,t->aggregate) &&
        qa_frontend_application(t->frontend) == c->domain->application && observation(c,&v) &&
        v.source == t->source.source && v.revision == t->source.revision &&
        c->history->number == t->message && c->history->time == t->time &&
        c->history->reliable == t->reliable && c->history->command_sequence == t->reached &&
        (c->revision == t->revision || (c->initialized && t->revision != UINT64_MAX && c->revision == t->revision+1));
}
bool frontend_unified_q3_client_video_prepare(frontend_unified_q3_client *c,qa_frontend *f,
    const frontend_video_guests *aggregate,frontend_unified_q3_client_video **out,qa_error *e)
{
    frontend_unified_q3_source_view v; bool linked = false;
    if (f && c) for (size_t i = 0; i < frontend_remote_unified_count(f); ++i)
        if (frontend_remote_unified_at(f,i) == c->replica) linked = true;
    if (!c || !f || !out || *out || !linked || !frontend_unified_q3_client_idle(c) || !c->initialized ||
        !c->registered || c->history->unsealed_snapshot || c->revision >= UINT64_MAX-1 || !observation(c,&v) ||
        !frontend_video_guests_parent_is(f,aggregate) || qa_frontend_application(f) != c->domain->application)
        return fail(e,QA_ERROR_ARGUMENT,"Compiled CLIENT video reset requires its actual returned CG and physical video aggregate");
    frontend_unified_q3_client_video *t = calloc(1,sizeof(*t));
    if (!t) return fail(e,QA_ERROR_MEMORY,"Retaining actual compiled CLIENT video baseline");
    *t = (frontend_unified_q3_client_video){.owner=c,.frontend=f,.aggregate=aggregate,.source=v,
        .history=c->history,.revision=c->revision,.message=c->history->number,.time=c->history->time,
        .reliable=c->history->reliable,.reached=c->history->command_sequence};
    c->video = t; *out = t; return true;
}
bool frontend_unified_q3_client_video_begin(frontend_unified_q3_client_video *t,void *context,
    bool (*closed)(const void *),qa_error *e)
{
    if (!frontend_unified_q3_client_video_current(t) || !context || !closed || !closed(context) ||
        t->owner->revision >= UINT64_MAX-1)
        return fail(e,QA_ERROR_ARGUMENT,"Compiled CLIENT video Init requires its actual old CG child to close");
    frontend_unified_q3_client *c = t->owner;
    c->initialized = false; c->registered = false;
    free(c->cvar_cache); c->cvar_cache = NULL; c->cvar_count = 0;
    ++c->revision; t->revision = c->revision;
    t->begun = true; return true;
}
bool frontend_unified_q3_client_constructor_reset(frontend_unified_q3_client *c,void *context,
    bool (*closed)(const void *),qa_error *e)
{
    if (!c || c->initialized || !frontend_unified_q3_client_idle(c) || c->revision == UINT64_MAX ||
        !context || !closed || !closed(context) || !frontend_unified_q3_client_current(c))
        return fail(e,QA_ERROR_ARGUMENT,"Compiled constructor retry requires its actual failed CG children to close");
    free(c->cvar_cache); c->cvar_cache = NULL; c->cvar_count = 0; c->registered = false; c->local_server = 0;
    ++c->revision; return true;
}
bool frontend_unified_q3_client_video_baseline(const frontend_unified_q3_client_video *t,
    const frontend_unified_q3_client *c,int32_t *message,int32_t *reached,qa_error *e)
{
    if (!message || !reached || !t || t->owner != c || !t->begun || !frontend_unified_q3_client_video_current(t))
        return fail(e,QA_ERROR_ARGUMENT,"Compiled video cache requires its genuine held CLIENT baseline");
    *message = t->message; *reached = t->reached; return true;
}
bool frontend_unified_q3_client_video_finish(frontend_unified_q3_client_video **out,qa_error *e)
{
    if (!out || !*out) return true;
    frontend_unified_q3_client_video *t = *out;
    if (!frontend_unified_q3_client_video_current(t) || !t->begun || !t->owner->initialized || !t->owner->registered)
        return fail(e,QA_ERROR_ARGUMENT,"Compiled video retains unfinished actual CG registration and Init");
    t->owner->video = NULL; free(t); *out = NULL; return true;
}
bool frontend_unified_q3_client_video_abort(frontend_unified_q3_client_video **out,qa_error *e)
{
    if (!out || !*out) return true;
    frontend_unified_q3_client_video *t = *out;
    if (t->begun) return frontend_unified_q3_client_video_finish(out,e);
    if (!frontend_unified_q3_client_video_current(t)) return fail(e,QA_ERROR_ARGUMENT,"Compiled video cancellation lost its actual CLIENT parent");
    t->owner->video = NULL; free(t); *out = NULL; return true;
}
q3n_compiled_source *frontend_unified_q3_client_source(frontend_unified_q3_client *c) { return c ? c->source : NULL; }
const qa_command_context *frontend_unified_q3_client_context(const frontend_unified_q3_client *c)
{ return c && frontend_unified_q3_client_current(c) ? &c->command_context : NULL; }
qa_cvars *frontend_unified_q3_client_cvars(const frontend_unified_q3_client *c)
{ return c && frontend_unified_q3_client_current(c) ? c->domain->cvars : NULL; }
const qa_command_context *frontend_unified_q3_client_checkpoint_context(const frontend_unified_q3_client *c)
{ return c && frontend_unified_q3_client_checkpoint_current(c) ? &c->command_context : NULL; }
qa_cvars *frontend_unified_q3_client_checkpoint_cvars(const frontend_unified_q3_client *c)
{ return c && frontend_unified_q3_client_checkpoint_current(c) ? c->domain->cvars : NULL; }
bool frontend_unified_q3_client_latest(const frontend_unified_q3_client *c, int32_t *number, int32_t *time, qa_error *e)
{
    if (!number || !time || !frontend_unified_q3_client_current(c)) return fail(e,QA_ERROR_ARGUMENT,"Compiled snapshot history lost its real Source");
    *number = c->history->number; *time = c->history->time; return true;
}
bool frontend_unified_q3_client_snapshot(const frontend_unified_q3_client *c, int32_t number, const qa_q3_snapshot **out, qa_error *e)
{
    if (!out || number < 1 || !frontend_unified_q3_client_current(c) || c->history->unsealed_snapshot)
        return fail(e,QA_ERROR_ARGUMENT,"Compiled snapshot read requires its actual Source events to finish publication");
    const client_snapshot *row = c->history->snapshots+((uint32_t)number&31u);
    *out = row->value.valid && row->value.message_number == number ? &row->value : NULL; return true;
}
bool frontend_unified_q3_client_snapshot_actor(const frontend_unified_q3_client *c,int32_t message,
    uint32_t number,qa_actor_id *actor,bool *present,qa_error *e)
{
    const qa_q3_snapshot *snapshot;
    if (!actor || !present || number >= QA_Q3_ENTITY_NONE ||
        !frontend_unified_q3_client_snapshot(c,message,&snapshot,e) || !snapshot)
        return fail(e,QA_ERROR_ARGUMENT,"Compiled historical actor requires its genuine retained snapshot");
    const client_snapshot *row = c->history->snapshots+((uint32_t)message&31u);
    *actor = row->bindings[number]; *present = actor->registry != 0; return true;
}
bool frontend_unified_q3_client_snapshot_number(const frontend_unified_q3_client *c,int32_t message,
    qa_actor_id actor,uint32_t *number,bool *present,qa_error *e)
{
    const qa_q3_snapshot *snapshot;
    if (!number || !present || !actor.registry || !frontend_unified_q3_client_idle(c) ||
        !frontend_unified_q3_client_snapshot(c,message,&snapshot,e) || !snapshot)
        return fail(e,QA_ERROR_ARGUMENT,"Compiled Source ownership requires its actual retained snapshot");
    const client_snapshot *row = c->history->snapshots+((uint32_t)message&31u);
    *number = QA_Q3_ENTITY_NONE; *present = false;
    for (uint32_t i = 0; i < QA_Q3_ENTITY_NONE; ++i) {
        if (!qa_actor_id_equal(row->bindings[i],actor)) continue;
        *number = i; *present = true; break;
    }
    return true;
}
bool frontend_unified_q3_client_command(frontend_unified_q3_client *c, int32_t sequence, frontend_unified_q3_command *out, qa_error *e)
{
    if (!c || !out || c->busy || c->prepared || c->video || c->revision == UINT64_MAX || sequence < 1 ||
        sequence > c->history->reliable || (int64_t)sequence > (int64_t)c->history->command_sequence+1 ||
        !frontend_unified_q3_client_current(c)) return fail(e,QA_ERROR_ARGUMENT,"Compiled command must reach its actual next retained sequence");
    client_command *row = c->history->commands+((uint32_t)sequence&63u);
    if (row->sequence != sequence) return fail(e,QA_ERROR_FORMAT,"Compiled reliable history lost an unreached command");
    if (sequence > c->history->command_sequence) {
        qa_command_tokens *a = &row->arguments;
        if (a->count && !strcmp(a->values[0],"cs")) {
            char *end = NULL; unsigned long index = a->count > 1 ? strtoul(a->values[1],&end,10) : ULONG_MAX;
            if (a->count != 3 || !end || *end || index >= QA_Q3_CONFIGSTRINGS || c->history->string_revisions[index] == UINT64_MAX)
                return fail(e,QA_ERROR_FORMAT,"Compiled reached configstring command has invalid actual arguments");
            if (!qa_q3_configstring_set(&c->history->reached,(uint32_t)index,a->values[2],e)) return false;
            ++c->history->string_revisions[index];
        }
        c->history->command_sequence = sequence; ++c->revision;
    }
    *out = (frontend_unified_q3_command){c,c->revision,sequence,true,&row->arguments}; return true;
}
bool frontend_unified_q3_command_current(const frontend_unified_q3_command *r)
{
    if (!r || !r->present || !frontend_unified_q3_client_current(r->owner) || r->revision != r->owner->revision) return false;
    const client_command *row = r->owner->history->commands+((uint32_t)r->sequence&63u);
    return row->sequence == r->sequence && &row->arguments == r->arguments;
}
bool frontend_unified_q3_client_server_command(frontend_unified_q3_client *c, uint64_t event_sequence,
    int32_t recipient, const char *text, qa_error *e)
{
    if (!c || !text || c->busy || c->prepared || c->video || c->revision == UINT64_MAX || !frontend_unified_q3_client_current(c))
        return fail(e,QA_ERROR_ARGUMENT,"Compiled source command requires its real returned CLIENT");
    client_history *h = c->history;
    if (h->has_event_sequence && event_sequence <= h->event_sequence) return true;
    if (recipient < 0 || recipient == (int32_t)c->constructor.client_number) {
        qa_command_tokens args = {0};
        if (!qa_command_tokenize(text,QA_CONSOLE_Q3,false,&args,e)) return false;
        bool ok = append(h,(const char *const *)args.values,args.count,e);
        qa_command_tokens_free(&args); if (!ok) return false;
    }
    h->event_sequence = event_sequence; h->has_event_sequence = true; ++c->revision; return true;
}
bool frontend_unified_q3_client_seal(frontend_unified_q3_client *c, qa_error *e)
{
    if (!c || c->busy || c->prepared || c->video || !frontend_unified_q3_client_current(c))
        return fail(e,QA_ERROR_ARGUMENT,"Compiled snapshot sealing requires its actual returned FRAME owner");
    if (!c->history->unsealed_snapshot) return true;
    if (c->revision == UINT64_MAX) return fail(e,QA_ERROR_FORMAT,"Compiled CLIENT sealing revision is exhausted");
    client_snapshot *row = c->history->snapshots+((uint32_t)c->history->number&31u);
    row->value.server_command_number = c->history->reliable;
    c->history->unsealed_snapshot = false; ++c->revision; return true;
}

static bool cache_cvar(frontend_unified_q3_client *c, size_t index, bool force, qa_error *e)
{
    qa_native_q3_cvar_definition definition;
    if (!qa_native_q3_cvar_definition_at(c->constructor.product,index,&definition))
        return fail(e,QA_ERROR_ARGUMENT,"Compiled CG cvar definition has retired");
    const qa_cvar_view *actual = qa_cvars_find(c->domain->cvars,definition.name);
    if (!actual) return fail(e,QA_ERROR_NOT_FOUND,"Compiled CG registered cvar is absent from its real registry");
    return application_q3_client_cache_copy(c->cvar_cache+index,actual,force,
        "Compiled Cvar_Update exceeds MAX_CVAR_VALUE_STRING",e);
}
bool frontend_unified_q3_client_register(frontend_unified_q3_client *c, qa_error *e)
{
    if (!c || c->busy || c->prepared || c->registered || !c->domain->cvars || !frontend_unified_q3_client_current(c))
        return fail(e,QA_ERROR_ARGUMENT,"Compiled CG registration requires its actual pre-Init registry owner");
    c->cvar_count = qa_native_q3_cvar_definition_count(c->constructor.product);
    c->cvar_cache = calloc(c->cvar_count,sizeof(*c->cvar_cache));
    if (!c->cvar_cache) return fail(e,QA_ERROR_MEMORY,"Retaining genuine compiled CG cvar cache");
    c->busy = true; bool ok = true;
    for (size_t i = 0; ok && i < c->cvar_count; ++i) {
        qa_native_q3_cvar_definition definition;
        ok = qa_native_q3_cvar_definition_at(c->constructor.product,i,&definition) &&
            qa_cvars_register(c->domain->cvars,definition.name,definition.reset,definition.flags,c->receiver,
                "Compiled Q3 CLIENT",e) && frontend_unified_q3_client_current(c) && cache_cvar(c,i,true,e);
    }
    if (ok) {
        const qa_cvar_view *running = qa_cvars_find(c->domain->cvars,"sv_running");
        const char *running_value = running ? running->value : "";
        char token[1024]; size_t length = strlen(running_value);
        if (length >= sizeof(token)) length = sizeof(token)-1;
        memcpy(token,running_value,length); token[length] = 0;
        c->local_server = source_integer(token);
        const char *team = c->constructor.product == QA_Q3_TEAM_ARENA ? "james" : "sarge";
        const char *head = c->constructor.product == QA_Q3_TEAM_ARENA ? "*james" : "sarge";
        const char *names[] = {"model","headmodel","team_model","team_headmodel"};
        const char *values[] = {"sarge","sarge",team,head};
        for (size_t i = 0; ok && i < 4; ++i)
            ok = qa_cvars_register(c->domain->cvars,names[i],values[i],QA_CVAR_USERINFO|QA_CVAR_ARCHIVE,
                c->receiver,"Compiled Q3 CLIENT",e) && frontend_unified_q3_client_current(c);
    }
    c->busy = false;
    if (ok) c->registered = true;
    else { free(c->cvar_cache); c->cvar_cache = NULL; c->cvar_count = 0; }
    return ok || (e && e->code ? false : fail(e,QA_ERROR_ARGUMENT,"Compiled CG registration lost its actual source"));
}
bool frontend_unified_q3_client_cvars_update(frontend_unified_q3_client *c, qa_error *e)
{
    if (!c || !c->registered || c->busy || c->prepared || !frontend_unified_q3_client_current(c))
        return fail(e,QA_ERROR_ARGUMENT,"Compiled CG cvar update requires its real returned registry");
    c->busy = true; bool ok = true;
    for (size_t i = 0; ok && i < c->cvar_count; ++i) ok = cache_cvar(c,i,false,e) && frontend_unified_q3_client_current(c);
    c->busy = false; return ok;
}
bool frontend_unified_q3_client_cvar_read(const frontend_unified_q3_client *c, const char *symbol,
    qa_native_q3_client_cvar *out, qa_error *e)
{
    if (!symbol || !out || !c || !c->registered || !frontend_unified_q3_client_current(c))
        return fail(e,QA_ERROR_ARGUMENT,"Compiled CG cvar read requires its actual registered cache");
    for (size_t i = 0; i < c->cvar_count; ++i) {
        qa_native_q3_cvar_definition definition;
        if (!qa_native_q3_cvar_definition_at(c->constructor.product,i,&definition)) return false;
        if (!strcmp(definition.symbol,symbol)) { *out = c->cvar_cache[i]; return true; }
    }
    return fail(e,QA_ERROR_NOT_FOUND,"Compiled CG cvar symbol is absent for its genuine product");
}
bool frontend_unified_q3_client_initialization_complete(frontend_unified_q3_client *c, qa_error *e)
{
    if (!c || !c->registered || c->initialized || c->busy || c->prepared ||
        (c->video && (!c->video->begun || !frontend_unified_q3_client_video_current(c->video))) ||
        c->revision == UINT64_MAX || !frontend_unified_q3_client_current(c))
        return fail(e,QA_ERROR_ARGUMENT,"Compiled CG Init completion requires its actual returned constructor");
    c->initialized = true; ++c->revision; return true;
}
bool frontend_unified_q3_client_cvar_number(frontend_unified_q3_client *c,const char *symbol,float value,qa_error *e)
{
    if (!c || !symbol || !c->registered || c->busy || c->prepared || !frontend_unified_q3_client_current(c))
        return fail(e,QA_ERROR_ARGUMENT,"Compiled VM cvar number requires its actual returned registered cache");
    for (size_t i = 0; i < c->cvar_count; ++i) {
        qa_native_q3_cvar_definition definition;
        if (!qa_native_q3_cvar_definition_at(c->constructor.product,i,&definition)) return false;
        if (!strcmp(definition.symbol,symbol)) { c->cvar_cache[i].number = value; return true; }
    }
    return fail(e,QA_ERROR_NOT_FOUND,"Compiled VM cvar number has no genuine registered symbol");
}

bool frontend_unified_q3_client_local_server_read(const frontend_unified_q3_client *c,int32_t *out,qa_error *e)
{
    if (!c || !out || !c->registered || !frontend_unified_q3_client_current(c))
        return fail(e,QA_ERROR_ARGUMENT,"Compiled local-server observation requires its actual CG constructor");
    *out = c->local_server; return true;
}
static bool saved_text(qa_source_save_io *io, char **text, size_t maximum)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t size = reading ? 0 : strlen(*text ? *text : "");
    if (!qa_source_save_count(io,&size,maximum)) return false;
    if (reading) {
        char *value = malloc(size+1);
        if (!value) return fail(io->error,QA_ERROR_MEMORY,"Restoring compiled CLIENT text");
        bool ok = qa_source_save_bytes(io,value,size) && !memchr(value,0,size);
        if (!ok) { free(value); return false; }
        value[size] = 0; *text = value; return true;
    }
    return qa_source_save_bytes(io,*text,size);
}
static bool saved_record(qa_source_save_io *io, void *record, int kind, qa_q3_product product)
{
    size_t capacity = kind == 2 ? 512u*1024u : 4096u;
    uint8_t *data = malloc(capacity); size_t size = 0;
    if (!data) return fail(io->error,QA_ERROR_MEMORY,"Retaining compiled CLIENT cold record");
    bool reading = io->direction == QA_SOURCE_SAVE_READ, ok = true;
    if (!reading) {
        qa_net_writer writer; qa_net_writer_init(&writer,data,capacity,io->error);
        ok = kind == 0 ? qa_q3_save_entity_fields(&writer,record) : kind == 1 ?
            qa_q3_save_player_fields(&writer,record) : qa_q3_save_gamestate_fields(&writer,record);
        if (ok) size = qa_net_writer_size(&writer);
    }
    if (ok) ok = qa_source_save_count(io,&size,capacity) && qa_source_save_bytes(io,data,size);
    if (ok && reading) {
        qa_net_reader reader; qa_net_reader_init(&reader,(qa_bytes){data,size},io->error);
        ok = (kind == 0 ? qa_q3_restore_entity_fields(&reader,record) : kind == 1 ?
            qa_q3_restore_player_fields(&reader,record,product) : qa_q3_restore_gamestate_fields(&reader,record)) && qa_net_reader_finish(&reader);
    }
    free(data); return ok;
}
static bool history_fields(frontend_unified_q3_client *c, qa_source_save_io *io, client_history *h)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    qa_q3_product product = c->constructor.product;
    bool ok = qa_source_save_i32(io,&h->number) && h->number > 0 && qa_source_save_i32(io,&h->time) &&
        qa_source_save_i32(io,&h->reliable) && h->reliable >= 0 && qa_source_save_i32(io,&h->command_sequence) &&
        h->command_sequence >= 0 && h->command_sequence <= h->reliable &&
        qa_source_save_bool(io,&h->has_event_sequence) && qa_source_save_u64(io,&h->event_sequence) &&
        qa_source_save_bool(io,&h->unsealed_snapshot) &&
        saved_record(io,&h->authority,2,product) && saved_record(io,&h->reached,2,product);
    for (uint32_t i = 0; ok && i < QA_Q3_CONFIGSTRINGS; ++i) ok = qa_source_save_u64(io,h->string_revisions+i);
    for (uint32_t i = 0; ok && i < QA_Q3_ENTITIES; ++i) ok = frontend_unified_q3_client_actor_fields(c,io,h->actors+i);
    for (size_t i = 0; ok && i < 64; ++i) {
        client_command *row = h->commands+i; size_t count = row->arguments.count;
        ok = qa_source_save_i32(io,&row->sequence) && row->sequence >= 0 && row->sequence <= h->reliable &&
            (!row->sequence || ((uint32_t)row->sequence&63u) == i) && qa_source_save_count(io,&count,1024);
        char **values = reading && count ? calloc(count,sizeof(*values)) : NULL;
        if (reading && count && !values) ok = fail(io->error,QA_ERROR_MEMORY,"Restoring compiled command argument list");
        for (size_t k = 0; ok && k < count; ++k) {
            char *value = reading ? NULL : row->arguments.values[k];
            ok = saved_text(io,&value,1024u*1024u);
            if (reading && values) values[k] = value;
        }
        char *tail = reading ? NULL : row->arguments.args_text;
        if (ok) ok = saved_text(io,&tail,1024u*1024u);
        if (ok && reading) {
            ok = arguments((const char *const *)values,count,&row->arguments,io->error);
            if (ok) { free(row->arguments.args_text); row->arguments.args_text = tail; tail = NULL; }
        }
        if (reading) { for (size_t k = 0; k < count; ++k) free(values ? values[k] : NULL); free(values); free(tail); }
    }
    for (size_t i = 0; ok && i < 32; ++i) {
        client_snapshot *row = h->snapshots+i; qa_q3_snapshot *s = &row->value;
        ok = qa_source_save_bool(io,&s->valid); if (!ok || !s->valid) continue;
        ok = qa_source_save_i32(io,&s->message_number) && s->message_number > 0 && s->message_number <= h->number &&
            (int64_t)s->message_number > (int64_t)h->number-32 && ((uint32_t)s->message_number&31u) == i &&
            qa_source_save_i32(io,&s->server_time) && s->server_time <= h->time && qa_source_save_i32(io,&s->delta_number) &&
            s->delta_number == (s->message_number == 1 ? -1 : s->message_number-1) &&
            qa_source_save_i32(io,&s->server_command_number) && s->server_command_number >= 0 && s->server_command_number <= h->reliable &&
            qa_source_save_u8(io,&s->flags) && !(s->flags & ~4u) && qa_source_save_u8(io,&s->area_bytes) && s->area_bytes == 32 &&
            qa_source_save_bytes(io,s->area_mask,sizeof(s->area_mask)) && saved_record(io,&s->player,1,product) &&
            frontend_unified_q3_client_actor_fields(c,io,&row->player_actor) && qa_source_save_count(io,&s->entity_count,256);
        for (size_t k = 0; ok && k < QA_Q3_ENTITIES; ++k)
            ok = frontend_unified_q3_client_actor_fields(c,io,row->bindings+k);
        s->entities = row->entities;
        for (size_t k = 0; ok && k < s->entity_count; ++k) {
            ok = saved_record(io,row->entities+k,0,product) && row->entities[k].number >= 0 && row->entities[k].number < QA_Q3_ENTITY_NONE &&
                (!k || row->entities[k].number > row->entities[k-1].number) && frontend_unified_q3_client_actor_fields(c,io,row->actors+k);
        }
    }
    const qa_q3_snapshot *latest = &h->snapshots[(uint32_t)h->number&31u].value;
    return ok && latest->valid && latest->message_number == h->number && latest->server_time == h->time;
}
static bool client_fields(frontend_unified_q3_client *c, qa_source_save_io *io)
{
    char magic[4] = {'Q','3','C','T'}; uint32_t epoch = c->constructor.epoch;
    uint64_t publication = c->constructor.publication, map = c->constructor.map_revision, receiver = c->receiver;
    uint32_t client = c->constructor.client_number; qa_actor_id viewer = c->constructor.viewer;
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    bool ok = qa_source_save_bytes(io,magic,4) && !memcmp(magic,"Q3CT",4) && qa_source_save_u32(io,&epoch) && epoch == c->constructor.epoch && qa_source_save_u64(io,&publication) && publication == c->constructor.publication &&
        qa_source_save_u64(io,&map) && map == c->constructor.map_revision && qa_source_save_u64(io,&receiver) && receiver == c->receiver &&
        qa_source_save_u32(io,&client) && client == c->constructor.client_number &&
        frontend_unified_q3_client_actor_fields(c,io,&viewer) && qa_actor_id_equal(viewer,c->constructor.viewer) &&
        qa_source_save_u64(io,&c->revision) && c->revision && qa_source_save_bool(io,&c->registered) && qa_source_save_bool(io,&c->initialized) &&
        (!c->initialized || c->registered) && qa_source_save_i32(io,&c->local_server) && history_fields(c,io,c->history);
    size_t count = c->registered ? qa_native_q3_cvar_definition_count(c->constructor.product) : 0;
    if (ok) ok = qa_source_save_count(io,&count,128) && count == (c->registered ? qa_native_q3_cvar_definition_count(c->constructor.product) : 0);
    if (ok && reading && count) { c->cvar_cache = calloc(count,sizeof(*c->cvar_cache)); if (!c->cvar_cache) ok = false; }
    if (ok) c->cvar_count = count;
    for (size_t i = 0; ok && i < count; ++i) {
        qa_native_q3_client_cvar *value = c->cvar_cache+i;
        ok = qa_source_save_bytes(io,value->value,sizeof(value->value)) && memchr(value->value,0,sizeof(value->value)) &&
            qa_source_save_f32(io,&value->number) && qa_source_save_i32(io,&value->integer) && qa_source_save_u64(io,&value->modification_count);
    }
    return ok;
}
bool frontend_unified_q3_client_checkpoint(const frontend_unified_q3_client *c, qa_buffer *out, qa_error *e)
{
    if (!out || out->data || !frontend_unified_q3_client_checkpoint_current(c))
        return fail(e,QA_ERROR_ARGUMENT,"Compiled CLIENT cold capture requires its actual returned history");
    qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io,NULL,e) && client_fields((frontend_unified_q3_client *)c,&io) &&
        frontend_unified_q3_client_checkpoint_current(c) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return ok;
}
static bool frame_history_current(const frontend_unified_q3_client_frame *t)
{
    const client_history *base = t->owner->history, *next = t->history;
    return next && t->source.time >= base->time && next->time == t->source.time &&
        (int64_t)next->number == (int64_t)base->number+(t->source.time != base->time) &&
        next->reliable >= base->reliable && next->command_sequence == base->command_sequence &&
        next->has_event_sequence == base->has_event_sequence && next->event_sequence == base->event_sequence;
}
bool frontend_unified_q3_client_checkpoint_stage_current(const frontend_unified_q3_client *c,
    const frontend_unified_q3_client_frame *t)
{
    frontend_unified_q3_source_view base;
    return c && !c->busy && !c->video && c->prepared == t && cold_source_observation(c,&base) &&
        (!t || (t->owner == c && c->revision != UINT64_MAX && t->revision == c->revision+1 &&
            activation(c,&t->source) && frontend_unified_q3_source_staged_checkpoint_current(&t->source) &&
            frame_history_current(t) &&
            ((t->rebind != NULL) == (t->source.snapshot_bit != c->constructor.snapshot_bit)) &&
            (!t->rebind || q3n_compiled_source_rebind_checkpoint_current(t->rebind))));
}
bool frontend_unified_q3_client_checkpoint_stage(const frontend_unified_q3_client *c,
    const frontend_unified_q3_client_frame *t,const frontend_unified_q3_source_frame *source,
    qa_buffer *out,qa_error *e)
{
    if (!out || out->data || !frontend_unified_q3_client_checkpoint_stage_current(c,t))
        return fail(e,QA_ERROR_ARGUMENT,"Compiled CLIENT staged capture requires its exact returned frame cohort");
    bool found = c->retirement && frontend_unified_q3_source_retirement_checkpoint_current(c->retirement);
    for (size_t i = 0; !found && i < frontend_unified_q3_source_frame_count(source); ++i) {
        frontend_unified_q3_source_view v;
        if (!frontend_unified_q3_sources_checkpoint_stage_read(c->sources,source,true,i,&v,e)) return false;
        found = t ? v.source == t->source.source && v.revision == t->source.revision : identity(c,&v);
    }
    for (size_t i = 0; !found && i < frontend_unified_q3_sources_committed_count(c->sources); ++i) {
        frontend_unified_q3_source_view v;
        if (!frontend_unified_q3_sources_checkpoint_stage_read(c->sources,source,false,i,&v,e)) return false;
        found = identity(c,&v);
    }
    if (!found) return fail(e,QA_ERROR_ARGUMENT,"Compiled CLIENT is outside the actual Source frame cohort");
    qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io,NULL,e) && client_fields((frontend_unified_q3_client *)c,&io) &&
        frontend_unified_q3_client_checkpoint_stage_current(c,t) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return ok;
}
const qa_command_context *frontend_unified_q3_client_checkpoint_stage_context(
    const frontend_unified_q3_client *c,const frontend_unified_q3_client_frame *t)
{ return frontend_unified_q3_client_checkpoint_stage_current(c,t) ? &c->command_context : NULL; }
qa_cvars *frontend_unified_q3_client_checkpoint_stage_cvars(
    const frontend_unified_q3_client *c,const frontend_unified_q3_client_frame *t)
{ return frontend_unified_q3_client_checkpoint_stage_current(c,t) ? c->domain->cvars : NULL; }
static bool frame_fields(frontend_unified_q3_client_frame *t,qa_source_save_io *io)
{
    frontend_unified_q3_client *c = t->owner;
    char magic[4] = {'Q','3','C','F'}; uint32_t epoch = t->source.epoch;
    uint64_t receiver = c->receiver,publication = t->source.publication,map = t->source.map_revision;
    uint64_t source_revision = t->source.revision,base_revision = c->revision;
    uint8_t bit = t->source.snapshot_bit; int32_t time = t->source.time; qa_actor_id viewer = t->source.viewer;
    bool round = t->source.snapshot_bit != c->constructor.snapshot_bit;
    bool ok = qa_source_save_bytes(io,magic,4) && !memcmp(magic,"Q3CF",4) &&
        qa_source_save_u32(io,&epoch) && epoch == t->source.epoch &&
        qa_source_save_u64(io,&receiver) && receiver == c->receiver &&
        qa_source_save_u64(io,&publication) && publication == t->source.publication &&
        qa_source_save_u64(io,&map) && map == t->source.map_revision &&
        qa_source_save_u64(io,&source_revision) && source_revision == t->source.revision &&
        qa_source_save_u64(io,&base_revision) && base_revision == c->revision &&
        qa_source_save_u64(io,&t->revision) && c->revision != UINT64_MAX && t->revision == c->revision+1 &&
        qa_source_save_u8(io,&bit) && bit == t->source.snapshot_bit &&
        qa_source_save_i32(io,&time) && time == t->source.time &&
        frontend_unified_q3_client_actor_fields(c,io,&viewer) && qa_actor_id_equal(viewer,t->source.viewer) &&
        qa_source_save_bool(io,&round) && round == (t->source.snapshot_bit != c->constructor.snapshot_bit) &&
        history_fields(c,io,t->history) && frame_history_current(t);
    return ok;
}
bool frontend_unified_q3_client_frame_checkpoint(const frontend_unified_q3_client_frame *t,
    qa_buffer *out,qa_error *e)
{
    if (!t || !out || out->data || !frontend_unified_q3_client_checkpoint_stage_current(t->owner,t) ||
        ((t->rebind != NULL) != (t->source.snapshot_bit != t->owner->constructor.snapshot_bit)))
        return fail(e,QA_ERROR_ARGUMENT,"Compiled CLIENT frame capture requires its genuine staged history and rebind");
    qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io,NULL,e) && frame_fields((frontend_unified_q3_client_frame *)t,&io) &&
        frontend_unified_q3_client_checkpoint_stage_current(t->owner,t) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return ok;
}
bool frontend_unified_q3_client_frame_restore(frontend_unified_q3_client *c,
    const frontend_unified_q3_source_view *v,qa_bytes bytes,frontend_unified_q3_client_frame **out,qa_error *e)
{
    if (!c || !v || !out || *out || !frontend_unified_q3_client_checkpoint_current(c) ||
        c->revision == UINT64_MAX || !activation(c,v) || !frontend_unified_q3_source_staged_checkpoint_current(v) ||
        (!qa_actor_id_equal(v->viewer,c->constructor.viewer) && v->snapshot_bit == c->constructor.snapshot_bit))
        return fail(e,QA_ERROR_ARGUMENT,"Compiled CLIENT pending import requires its actual restored Source candidate");
    frontend_unified_q3_client_frame *t = calloc(1,sizeof(*t)); qa_source_save_io io = {0};
    if (!t) return fail(e,QA_ERROR_MEMORY,"Restoring compiled CLIENT frame");
    t->owner = c; t->source = *v; t->revision = c->revision+1;
    t->command_context = c->command_context; t->command_context.actor = v->viewer;
    t->history = calloc(1,sizeof(*t->history));
    bool ok = t->history && qa_source_save_reader(&io,NULL,bytes,e) && frame_fields(t,&io) && io.offset == io.input.size;
    if (ok) {
        c->prepared = t;
        if (v->snapshot_bit != c->constructor.snapshot_bit)
            ok = source_basis(c,v,t->history,t->revision,&t->candidate,e) &&
                q3n_compiled_source_rebind_restore(c->source,&t->candidate,&t->rebind,e);
        if (ok) ok = frontend_unified_q3_client_checkpoint_stage_current(c,t);
    }
    if (ok) *out = t;
    else { q3n_compiled_source_rebind_abort(&t->rebind); if (c->prepared == t) c->prepared = NULL;
        history_free(t->history); free(t); }
    qa_source_save_dispose(&io);
    return ok || (e && e->code ? false : fail(e,QA_ERROR_FORMAT,"Compiled CLIENT staged history is invalid"));
}
static bool restore(frontend_remote_unified *replica, frontend_unified_q3_sources *sources,
    const frontend_unified_q3_source_view *view, uint64_t receiver, qa_bytes bytes,
    frontend_unified_q3_source_retirement *retirement,frontend_unified_q3_client **out, qa_error *e)
{
    if (!out || *out) return fail(e,QA_ERROR_ARGUMENT,"Compiled CLIENT cold restore requires an empty isolated child");
    frontend_unified_q3_client *c = NULL; qa_source_save_io io = {0};
    bool ok = create(replica,sources,view,receiver,true,retirement,&c,e);
    if (ok) ok = qa_source_save_reader(&io,NULL,bytes,e) && client_fields(c,&io) && io.offset == io.input.size &&
        c->history->time <= view->time && frontend_unified_q3_client_checkpoint_current(c) && create_source(c,true,e);
    if (ok) *out = c; else frontend_unified_q3_client_destroy(&c,NULL);
    qa_source_save_dispose(&io);
    return ok || (e && e->code ? false : fail(e,QA_ERROR_FORMAT,"Compiled CLIENT cold continuation is invalid"));
}
bool frontend_unified_q3_client_restore(frontend_remote_unified *replica,frontend_unified_q3_sources *sources,
    const frontend_unified_q3_source_view *view,uint64_t receiver,qa_bytes bytes,frontend_unified_q3_client **out,qa_error *e)
{ return restore(replica,sources,view,receiver,bytes,NULL,out,e); }
bool frontend_unified_q3_client_restore_retired(frontend_remote_unified *replica,frontend_unified_q3_sources *sources,
    frontend_unified_q3_source_retirement *t,uint64_t receiver,qa_bytes bytes,frontend_unified_q3_client **out,qa_error *e)
{
    frontend_unified_q3_source_view view;
    if (!frontend_unified_q3_source_retirement_checkpoint_current(t) ||
        !frontend_unified_q3_source_retirement_read(t,&view,e) || view.owner != sources)
        return fail(e,QA_ERROR_ARGUMENT,"Compiled retired CLIENT import requires its actual cold Source custody");
    return restore(replica,sources,&view,receiver,bytes,t,out,e);
}
