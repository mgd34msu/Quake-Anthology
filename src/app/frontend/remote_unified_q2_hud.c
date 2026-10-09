#include "remote_unified_q2_hud.h"
#include "remote_unified_private.h"
#include "remote_unified_save.h"
#include "save_private.h"
#include "qa/hud.h"
#include "qa/ui_preferences.h"
#include "qa/caption_save.h"
#include "qa/text.h"
#include "qa/network_unified_frame.h"
#include <float.h>
#include <math.h>
#include <stdio.h>

typedef enum rr_kind {
    RR_POI, RR_KEYED_POI, RR_REMOVE_POI, RR_HEALTHBAR, RR_DAMAGE,
    RR_PATH, RR_COOP, RR_REPORT, RR_OBJECTIVE, RR_MISSION, RR_HELP, RR_UNKNOWN
} rr_kind;
static const char *const coop_keys[] = {"", "$g_coop_respawn_in_combat", "$g_coop_respawn_bad_area",
    "$g_coop_respawn_blocked", "$g_coop_respawn_waiting", "$g_coop_respawn_no_lives"};
typedef qa_unified_q2_campaign_level rr_level;
typedef struct rr_record {
    qa_unified_presentation_event *event;
    rr_kind kind;
    char *content, *source_provider, *owner_provider;
    uint64_t owner_generation;
    double seconds;
    qa_actor_id actor, target;
    qa_scene_image *image;
    char *localized, *secondary_localized;
    union {
        struct { int32_t key, flags, color; qa_vec3 origin; double duration; char *path; qa_scene_vec4 tint; } poi;
        struct { int32_t key; } remove;
        struct { int32_t slot; char *name; double fraction; bool visible; } bar;
        struct { qa_vec3 direction; double amount; bool health, armor, shield; } damage;
        struct { qa_vec3 origin, direction; bool first; } path;
        struct { uint32_t state; double lives; } coop;
        struct { rr_level *levels; size_t count; double ready; } report;
        struct { char *text; char **args; size_t count; bool talk; } objective;
        struct { bool visible; } mission;
        struct { char *primary, *secondary; bool visible, slow; } help;
    } value;
} rr_record;
typedef struct rr_bar { struct rr_bar *next; rr_record row; } rr_bar;
typedef struct rr_damage {
    rr_record row;
    qa_vec3 direction, color;
    double amount, expires_ms;
    bool health, armor, shield;
} rr_damage;
typedef struct rr_retired {
    struct rr_retired *next;
    char *provider;
    uint64_t generation;
} rr_retired;
struct frontend_unified_q2_rr_hud {
    qa_frontend *frontend;
    frontend_remote_unified *replica;
    frontend_unified_media *media;
    frontend_unified_events *events;
    qa_localization_pool *localizations;
    qa_hud *prints;
    qa_unified_document *frame, *prepared;
    double seconds, prepared_seconds;
    rr_record pois[32]; size_t poi_count;
    rr_damage damage[32]; size_t damage_count;
    rr_bar *bars;
    rr_record path, coop, report, objective, mission, help;
    rr_record pending_objective;
    bool pending_printed, pending_sound;
    rr_retired *retired;
    qa_scene_view view;
    bool have_view, busy, help_open, controls_registered;
    struct { qa_cvar_handle pois, damage, damage_ms, edge, maximum; } controls;
};
static bool fail(qa_error *e, const char *message)
{ return frontend_unified_fail(e, QA_ERROR_FORMAT, message); }
static bool current(frontend_unified_q2_rr_hud *o, qa_error *e)
{
    return o && frontend_unified_media_current(o->media) &&
        ((o->frontend->capture || o->frontend->source_restoring) ?
            frontend_remote_unified_checkpoint_current(o->replica, e) : frontend_remote_unified_current(o->replica, e));
}
static bool actor_read(frontend_unified_q2_rr_hud *o, qa_actor_id actor,
    bool retained, bool resolve, qa_actor_id *out, qa_error *e)
{
    if (!actor.registry) return fail(e,"RR HUD actor has no Source identity");
    if (!resolve) return true;
    return frontend_remote_unified_source_actor(o->replica,qa_unified_document_frame(o->frame),actor,retained,out,e);
}
static rr_kind kind_read(const qa_unified_presentation_event *row)
{
    if (!row || row->q2_profile!=2) return RR_UNKNOWN;
    switch (row->payload.kind) {
    case QA_UNIFIED_PRESENTATION_Q2_MAP:
        switch (row->payload.value.q2_map.kind) {
        case QA_Q2_MAP_POI: return RR_POI;
        case QA_Q2_MAP_REMOVE_POI: return RR_REMOVE_POI;
        case QA_Q2_MAP_HEALTHBAR: return RR_HEALTHBAR;
        case QA_Q2_MAP_END_UNIT: return RR_REPORT;
        case QA_Q2_MAP_MISSION_OBJECTIVE: return RR_OBJECTIVE;
        case QA_Q2_MAP_MISSION_STATUS: return RR_MISSION;
        case QA_Q2_MAP_HELP_COMPUTER: return RR_HELP;
        default: return RR_UNKNOWN;
        }
    case QA_UNIFIED_PRESENTATION_Q2_PLAYER:
        switch (row->payload.value.q2_player.kind) {
        case QA_Q2_PLAYER_DIRECTIONAL_DAMAGE: return RR_DAMAGE;
        case QA_Q2_PLAYER_HELP_PATH: return RR_PATH;
        case QA_Q2_PLAYER_RESPAWN_STATUS: return RR_COOP;
        default: return RR_UNKNOWN;
        }
    case QA_UNIFIED_PRESENTATION_Q2_PROTOCOL:
        switch (row->payload.value.q2_protocol.kind) {
        case QA_Q2_SVC_POI: return row->payload.value.q2_protocol.poi.remove?RR_REMOVE_POI:RR_KEYED_POI;
        case QA_Q2_SVC_DAMAGE: return RR_DAMAGE;
        case QA_Q2_SVC_HELP_PATH: return RR_PATH;
        default: return RR_UNKNOWN;
        }
    default: return RR_UNKNOWN;
    }
}
bool frontend_unified_q2_rr_known(const qa_unified_presentation_event *row)
{ return kind_read(row)!=RR_UNKNOWN; }
static void record_clear(rr_record *r)
{
    if (!r) return;
    if (r->kind==RR_OBJECTIVE) free(r->value.objective.args);
    if (r->event) { qa_unified_presentation_event_dispose(r->event); free(r->event); }
    qa_scene_image_release(r->image);
    free(r->localized); free(r->secondary_localized); *r=(rr_record){0};
}
static void record_replace(rr_record *destination, rr_record *source)
{ record_clear(destination); *destination=*source; *source=(rr_record){0}; }
static bool parse(frontend_unified_q2_rr_hud *o, const qa_unified_presentation_event *row,
    bool retained, bool resolve, rr_record *r, qa_error *e)
{
    r->kind=kind_read(row);
    if (r->kind==RR_UNKNOWN) return fail(e,"RR HUD event variant is unknown");
    if (!row->content || !*row->content || !row->provider || !*row->provider || !row->q2_interval_ns ||
        !isfinite(row->seconds) || row->seconds<0 || !isfinite(row->seconds*1000) ||
        (row->owner.generation && (!row->owner.provider || !*row->owner.provider)))
        return fail(e,"RR HUD event lacks its actual Source clock and owner");
    r->content=row->content; r->source_provider=row->provider;
    r->owner_provider=row->owner.provider; r->owner_generation=row->owner.generation;
    r->seconds=row->seconds;
    const qa_unified_q2_map_event *map=row->payload.kind==QA_UNIFIED_PRESENTATION_Q2_MAP?&row->payload.value.q2_map:NULL;
    const qa_unified_q2_player_event *player=row->payload.kind==QA_UNIFIED_PRESENTATION_Q2_PLAYER?&row->payload.value.q2_player:NULL;
    const qa_unified_q2_protocol_event *protocol=row->payload.kind==QA_UNIFIED_PRESENTATION_Q2_PROTOCOL?&row->payload.value.q2_protocol:NULL;
    qa_actor_id actor=map?map->recipient:player?player->actor:protocol->actor;
    bool okay=r->kind==RR_REPORT || actor_read(o,actor,retained,resolve,&r->actor,e);
    switch (r->kind) {
    case RR_POI: case RR_KEYED_POI:
        r->value.poi.key=map?1:protocol->poi.key;
        r->value.poi.flags=map?1:protocol->poi.flags;
        r->value.poi.origin=map?map->origin:protocol->poi.position;
        r->value.poi.path=map?map->resource:protocol->resource;
        r->value.poi.duration=map?map->duration:protocol->poi.duration;
        r->value.poi.color=map?map->count:protocol->poi.color;
        r->value.poi.tint=(qa_scene_vec4){1,1,1,1};
        okay=okay && qa_vec_finite(r->value.poi.origin) && r->value.poi.path && *r->value.poi.path &&
            isfinite(r->value.poi.duration) && isfinite(r->seconds*1000+r->value.poi.duration); break;
    case RR_REMOVE_POI: r->value.remove.key=map?map->slot:protocol->poi.key; break;
    case RR_HEALTHBAR:
        r->value.bar.slot=map->slot; r->value.bar.name=map->text;
        r->value.bar.fraction=map->value; r->value.bar.visible=map->visible;
        okay=okay && actor_read(o,map->target,retained,resolve,&r->target,e) &&
            r->value.bar.name && isfinite(r->value.bar.fraction); break;
    case RR_DAMAGE:
        r->value.damage.direction=player?player->direction:protocol->direction;
        r->value.damage.amount=player?player->damage:protocol->damage;
        r->value.damage.health=player?player->health:protocol->health;
        r->value.damage.armor=player?player->armor:protocol->armor;
        r->value.damage.shield=player?player->shield:protocol->shield;
        okay=okay && qa_vec_finite(r->value.damage.direction) && isfinite(r->value.damage.amount); break;
    case RR_PATH:
        r->value.path.origin=player?player->origin:protocol->origin;
        r->value.path.direction=player?player->direction:protocol->direction;
        r->value.path.first=player?player->first:protocol->first;
        okay=okay && qa_vec_finite(r->value.path.origin) && qa_vec_finite(r->value.path.direction); break;
    case RR_COOP:
        r->value.coop.state=(uint32_t)player->respawn_status; r->value.coop.lives=player->lives;
        okay=okay && r->value.coop.state<6; break;
    case RR_REPORT:
        r->value.report.levels=map->levels; r->value.report.count=map->level_count;
        r->value.report.ready=(double)map->button_time_ns/1e9;
        okay=okay && map->level_count<=QA_Q2_CAMPAIGN_LEVEL_LIMIT && (!map->level_count || map->levels);
        for (size_t i=0; okay && i<map->level_count; ++i)
            okay=map->levels[i].map && map->levels[i].name && isfinite(map->levels[i].time_seconds);
        break;
    case RR_OBJECTIVE:
        r->value.objective.text=map->text; r->value.objective.talk=(map->flags&1u)!=0;
        okay=okay && map->text && map->argument_count<=65536 && (!map->argument_count || map->arguments);
        if (okay && map->argument_count) {
            r->value.objective.args=calloc(map->argument_count,sizeof(char *));
            if (!r->value.objective.args) return frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining RR objective argument view");
        }
        for (size_t i=0; okay && i<map->argument_count; ++i) {
            okay=map->arguments[i].kind==QA_BUILTIN_MESSAGE_STRING && map->arguments[i].text;
            if (okay) r->value.objective.args[r->value.objective.count++]=map->arguments[i].text;
        }
        break;
    case RR_MISSION: r->value.mission.visible=map->visible; break;
    case RR_HELP:
        r->value.help.primary=map->text; r->value.help.secondary=map->resource;
        r->value.help.visible=map->visible; r->value.help.slow=(map->flags&1u)!=0;
        okay=okay && map->text && map->resource; break;
    case RR_UNKNOWN: break;
    }
    if (!okay && (!e || e->code==QA_OK)) fail(e,"RR HUD event lacks its actual Source, actor or typed payload");
    return okay;
}
static bool retired(const frontend_unified_q2_rr_hud *o, const rr_record *r)
{
    if (!r->owner_generation) return false;
    for (rr_retired *item=o->retired; item; item=item->next)
        if (item->generation==r->owner_generation && !strcmp(item->provider,r->owner_provider)) return true;
    return false;
}
bool frontend_unified_q2_rr_validate(frontend_unified_q2_rr_hud *o,
    const qa_unified_presentation_event *row, qa_error *e)
{
    if (!o || !row || !current(o,e)) return false;
    rr_record record={0}; bool okay=parse(o,row,false,false,&record,e); record_clear(&record); return okay;
}
static bool media_bank(frontend_unified_q2_rr_hud *o, const char *content, bool fresh,
    frontend_unified_bank_view *out, qa_error *e)
{
    if (fresh) {
        qa_scene_resources *images; qa_material_library *materials; qa_font_library *fonts; qa_audio_bank *sounds;
        if (!frontend_unified_media_bank(o->media,content,&images,&materials,&fonts,&sounds,e)) return false;
    }
    for (size_t i=0; i<frontend_unified_media_bank_count(o->media); ++i)
        if (frontend_unified_media_bank_read(o->media,i,out) && !strcmp(out->content,content))
            return out->product && out->product->family==QA_GAME_Q2 && out->files && out->images;
    return fail(e,"RR HUD content has no genuine retained Q2 media bank");
}
static bool localized(frontend_unified_q2_rr_hud *o, const rr_record *r, const char *source,
    const char *const *args, size_t count, char **out, qa_error *e)
{
    frontend_unified_bank_view bank={0}; qa_ui_preferences prefs; qa_localization *catalog=NULL;
    const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
    qa_localization_options options={.profile=QA_LOCALIZATION_Q2_RERELEASE};
    bool okay=domain && media_bank(o,r->content,true,&bank,e) &&
        qa_ui_preferences_read(qa_application_cvars(domain->application),qa_application_ui_preference_handles(domain->application),domain->physical_seat,&prefs,e) &&
        qa_localization_acquire(o->localizations,bank.files,prefs.language,&options,&catalog,e);
    if (okay) {
        char result[1024]; size_t size=qa_localize_presentation(catalog,source,args,count,false,result,sizeof(result));
        *out=malloc(size+1); okay=*out!=NULL;
        if (okay) memcpy(*out,result,size+1);
        else frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining actual localized RR HUD text");
    }
    qa_localization_release(catalog); return okay;
}
static bool picture(frontend_unified_q2_rr_hud *o, rr_record *r, const char *path, qa_error *e)
{
    frontend_unified_bank_view bank={0};
    qa_scene_image_options options={.family=QA_SCENE_Q2,.usage=QA_IMAGE_USAGE_PICTURE,.wrap=QA_SCENE_CLAMP,
        .filter=QA_SCENE_LINEAR,.transparent=true,.transparent_index=255};
    return media_bank(o,r->content,true,&bank,e) && qa_scene_image_load(bank.images,path,&options,&r->image,e);
}
static uint64_t nanoseconds(double seconds)
{
    long double value=(long double)seconds*1e9L;
    return value<=0?0:value>=UINT64_MAX?UINT64_MAX:(uint64_t)value;
}
static bool clone_record(frontend_unified_q2_rr_hud *o, const qa_unified_presentation_event *row,
    rr_record *r, qa_error *e)
{
    r->event=calloc(1,sizeof(*r->event));
    if (!r->event) return frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining RR Source event");
    if (!qa_unified_presentation_event_clone(row,r->event,e)) return false;
    if (r->kind==RR_OBJECTIVE) {
        free(r->value.objective.args); r->value.objective.args=NULL; r->value.objective.count=0;
    }
    return parse(o,r->event,false,false,r,e);
}
static bool controls(frontend_unified_q2_rr_hud *o, bool *pois, bool *damage, double *damage_ms,
    double *edge, double *maximum, qa_error *e)
{
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->replica);
    const qa_cvar_view *p=d?qa_cvars_read(d->cvars,o->controls.pois):NULL,
        *a=d?qa_cvars_read(d->cvars,o->controls.damage):NULL,
        *t=d?qa_cvars_read(d->cvars,o->controls.damage_ms):NULL,
        *f=d?qa_cvars_read(d->cvars,o->controls.edge):NULL,
        *s=d?qa_cvars_read(d->cvars,o->controls.maximum):NULL;
    if (!p || !a || !t || !f || !s || !isfinite(t->number) || t->number<0 ||
        !isfinite(f->number) || f->number<0 || f->number>FLT_MAX ||
        !isfinite(s->number) || s->number<1 || s->number>FLT_MAX)
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"RR HUD has no genuine valid private CLIENT controls");
    *pois=p->integer!=0; *damage=a->integer!=0; *damage_ms=t->number; *edge=f->number; *maximum=s->number; return true;
}
static const struct { const char *name, *value; } control_defaults[]={
    {"scr_pois","1"},{"scr_poi_edge_frac","0.15"},{"scr_poi_max_scale","1"},
    {"scr_damage_indicators","1"},{"scr_damage_indicator_time","1000"}
};
static void controls_bind(frontend_unified_q2_rr_hud *o, const qa_cvars *cvars)
{
    o->controls.pois=qa_cvars_resolve(cvars,"scr_pois");
    o->controls.damage=qa_cvars_resolve(cvars,"scr_damage_indicators");
    o->controls.damage_ms=qa_cvars_resolve(cvars,"scr_damage_indicator_time");
    o->controls.edge=qa_cvars_resolve(cvars,"scr_poi_edge_frac");
    o->controls.maximum=qa_cvars_resolve(cvars,"scr_poi_max_scale");
}
static bool controls_admit(frontend_unified_q2_rr_hud *o, qa_error *e)
{
    if (o->controls_registered) return true;
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->replica);
    if (!d || !d->cvars || !d->command_context.owner || o->frontend->source_restoring)
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"RR controls require their genuine fresh private CLIENT source heap");
    for (size_t i=0; i<sizeof(control_defaults)/sizeof(*control_defaults); ++i)
        if (!qa_cvars_register(d->cvars,control_defaults[i].name,control_defaults[i].value,0,
            d->command_context.owner,"Received RR HUD",e) || !current(o,e)) return false;
    controls_bind(o,d->cvars); o->controls_registered=true; return true;
}
static bool poi_apply(frontend_unified_q2_rr_hud *o, rr_record *r, qa_error *e)
{
    double now=r->seconds*1000, oldest=INFINITY; size_t index=SIZE_MAX;
    if (r->value.poi.key) for (size_t i=0; i<o->poi_count; ++i)
        if (o->pois[i].value.poi.key==r->value.poi.key) { index=i; break; }
    if (index==SIZE_MAX) for (size_t i=0; i<o->poi_count; ++i)
        if (o->pois[i].seconds*1000+o->pois[i].value.poi.duration<=now) { index=i; break; }
    if (index==SIZE_MAX && o->poi_count<32) index=o->poi_count;
    if (index==SIZE_MAX) for (size_t i=0; i<o->poi_count; ++i)
        if (!o->pois[i].value.poi.key && o->pois[i].seconds*1000+o->pois[i].value.poi.duration<oldest) {
            oldest=o->pois[i].seconds*1000+o->pois[i].value.poi.duration; index=i;
        }
    if (index==SIZE_MAX) return true;
    size_t size=strlen(r->value.poi.path);
    if (size>SIZE_MAX-10) return fail(e,"RR POI image path exceeds storage");
    char *path=malloc(size+10);
    if (!path) return frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining RR POI image request");
    snprintf(path,size+10,"pics/%s.pcx",r->value.poi.path);
    bool okay=picture(o,r,path,e); free(path);
    frontend_unified_bank_view bank={0}; qa_bytes palette={0};
    if (okay) okay=media_bank(o,r->content,false,&bank,e);
    if (okay) {
        qa_error issue={0};
        if (!qa_scene_resources_palette(bank.images,QA_SCENE_Q2,&palette,&issue) && issue.code!=QA_ERROR_NOT_FOUND) {
            if (e) *e=issue;
            okay=false;
        }
    }
    if (okay && palette.data && palette.size>=768) {
        size_t color=(uint32_t)r->value.poi.color&255u;
        r->value.poi.tint=(qa_scene_vec4){palette.data[color*3]/255.f,palette.data[color*3+1]/255.f,palette.data[color*3+2]/255.f,1};
    }
    if (!okay) return false;
    if (index==o->poi_count) ++o->poi_count;
    record_replace(o->pois+index,r); return true;
}
static qa_vec3 normalize(qa_vec3 value)
{ float length=qa_vec_length(value); return length>0?qa_vec_scale(value,1/length):value; }
static bool damage_apply(frontend_unified_q2_rr_hud *o, rr_record *r, double lifetime, qa_error *e)
{
    double now=r->seconds*1000; size_t index=SIZE_MAX; bool retain=false;
    for (size_t i=0; i<o->damage_count; ++i)
        if (o->damage[i].expires_ms<=now || qa_vec_dot(o->damage[i].direction,r->value.damage.direction)>=.95f) {
            index=i; retain=o->damage[i].expires_ms>now &&
                qa_vec_dot(o->damage[i].direction,r->value.damage.direction)>=.95f; break;
        }
    if (index==SIZE_MAX) index=o->damage_count<32?o->damage_count:0;
    if (!picture(o,r,"pics/damage_indicator.pcx",e)) return false;
    rr_damage *entry=o->damage+index;
    qa_vec3 color=normalize(qa_v3((float)(r->value.damage.health+r->value.damage.armor),
        (float)(r->value.damage.shield+r->value.damage.armor),(float)r->value.damage.armor));
    double amount=r->value.damage.amount+(retain?entry->amount:0);
    if (!isfinite(amount)) return fail(e,"Accumulated RR directional damage exceeds finite storage");
    if (retain) color=normalize(qa_vec_add(color,entry->color));
    bool health=r->value.damage.health || (retain && entry->health),
        armor=r->value.damage.armor || (retain && entry->armor), shield=r->value.damage.shield || (retain && entry->shield);
    if (!isfinite(now+lifetime)) return fail(e,"RR damage deadline exceeds finite storage");
    entry->direction=r->value.damage.direction; entry->color=color; entry->amount=amount;
    entry->health=health; entry->armor=armor; entry->shield=shield; entry->expires_ms=now+lifetime;
    record_replace(&entry->row,r); if (index==o->damage_count) ++o->damage_count; return true;
}
static bool objective_apply(frontend_unified_q2_rr_hud *o, rr_record *r, qa_error *e)
{
    if (o->pending_objective.event) {
        if (o->pending_objective.event->sequence!=r->event->sequence) {
            return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"RR objective retry differs from its retained delivery prefix");
        }
    } else {
        if (!localized(o,r,r->value.objective.text,(const char *const *)r->value.objective.args,
            r->value.objective.count,&r->localized,e)) return false;
        record_replace(&o->pending_objective,r);
    }
    rr_record *pending=&o->pending_objective;
    if (!o->pending_printed) {
        if (!qa_hud_center_print(o->prints,pending->localized,nanoseconds(pending->seconds),UINT64_C(5000000000),
            false,UINT64_C(40000000),e)) return false;
        o->pending_printed=true;
    }
    if (!o->pending_sound && pending->value.objective.talk) {
        if (!frontend_unified_events_sound_path(o->events,pending->content,"misc/talk.wav",(qa_actor_id){0},
            qa_v3(0,0,0),pending->seconds*1000,0,1,0,0,e)) return false;
        o->pending_sound=true;
    }
    record_replace(&o->objective,pending); o->pending_printed=false; o->pending_sound=false; return true;
}
bool frontend_unified_q2_rr_presentation(frontend_unified_q2_rr_hud *o,
    const qa_unified_presentation_event *row, bool *mirrored, qa_error *e)
{
    if (!o || !row || !mirrored || o->busy || o->frontend->capture || o->frontend->resource_inventory ||
        o->frontend->source_restoring || !current(o,e)) return false;
    *mirrored=false; rr_record r={0}; bool okay=parse(o,row,false,true,&r,e);
    qa_actor_id viewer; uint32_t number_id;
    if (okay) okay=frontend_remote_unified_player(o->replica,&viewer,&number_id);
    if (okay && ((r.kind!=RR_REPORT && !qa_actor_id_equal(viewer,r.actor)) || retired(o,&r))) {
        record_clear(&r); return true;
    }
    if (okay) okay=clone_record(o,row,&r,e);
    if (!okay) { record_clear(&r); return false; }
    o->busy=true;
    okay=controls_admit(o,e);
    bool show_pois,show_damage; double lifetime,edge,maximum;
    if (okay && (r.kind==RR_POI || r.kind==RR_KEYED_POI || r.kind==RR_REMOVE_POI || r.kind==RR_DAMAGE))
        okay=controls(o,&show_pois,&show_damage,&lifetime,&edge,&maximum,e);
    if (!okay) { record_clear(&r); o->busy=false; return false; }
    switch (r.kind) {
    case RR_POI: case RR_KEYED_POI: if (okay && show_pois) okay=poi_apply(o,&r,e); break;
    case RR_REMOVE_POI:
        if (okay && show_pois && r.value.remove.key) for (size_t i=0; i<o->poi_count; ++i)
            if (o->pois[i].value.poi.key==r.value.remove.key) {
                record_clear(o->pois+i);
                memmove(o->pois+i,o->pois+i+1,(o->poi_count-i-1)*sizeof(*o->pois));
                o->pois[--o->poi_count]=(rr_record){0}; break;
            }
        break;
    case RR_HEALTHBAR: {
        rr_bar **next=&o->bars;
        while (*next && (*next)->row.value.bar.slot<r.value.bar.slot) next=&(*next)->next;
        if (!r.value.bar.visible) {
            if (*next && (*next)->row.value.bar.slot==r.value.bar.slot) {
                rr_bar *item=*next; *next=item->next; record_clear(&item->row); free(item);
            }
        } else {
            okay=localized(o,&r,r.value.bar.name,NULL,0,&r.localized,e);
            if (okay) {
                if (!*next || (*next)->row.value.bar.slot!=r.value.bar.slot) {
                    rr_bar *item=calloc(1,sizeof(*item));
                    if (!item) okay=frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining genuine RR health bar");
                    else { item->next=*next; *next=item; }
                }
                if (okay) record_replace(&(*next)->row,&r);
            }
        }
        break;
    }
    case RR_DAMAGE: if (okay && show_damage) okay=damage_apply(o,&r,lifetime,e); break;
    case RR_PATH: record_replace(&o->path,&r); break;
    case RR_COOP:
        okay=localized(o,&r,coop_keys[r.value.coop.state],NULL,0,&r.localized,e) &&
            localized(o,&r,"$g_lives",NULL,0,&r.secondary_localized,e);
        if (okay) record_replace(&o->coop,&r);
        break;
    case RR_REPORT: record_replace(&o->report,&r); break;
    case RR_OBJECTIVE: okay=objective_apply(o,&r,e); break;
    case RR_MISSION: record_replace(&o->mission,&r); break;
    case RR_HELP:
        okay=localized(o,&r,r.value.help.primary,NULL,0,&r.localized,e) &&
            localized(o,&r,r.value.help.secondary,NULL,0,&r.secondary_localized,e);
        if (okay) { o->help_open=r.value.help.visible; record_replace(&o->help,&r); }
        break;
    case RR_UNKNOWN: okay=false; break;
    }
    record_clear(&r); o->busy=false; return okay && current(o,e);
}
bool frontend_unified_q2_rr_help_visible(const frontend_unified_q2_rr_hud *o)
{
    qa_actor_id viewer; uint32_t number_id;
    return o && o->help.event && o->help_open &&
        frontend_remote_unified_player(o->replica,&viewer,&number_id) && qa_actor_id_equal(o->help.actor,viewer);
}
bool frontend_unified_q2_rr_overlay(frontend_unified_q2_rr_hud *o,
    const qa_unified_presentation_event *row, qa_error *e)
{
    if (!o || !row || o->busy || !current(o,e)) return false;
    if (row->payload.kind!=QA_UNIFIED_PRESENTATION_Q2_PLAYER)
        return fail(e,"RR help interlock received no actual player event");
    const qa_unified_q2_player_event *player=&row->payload.value.q2_player;
    qa_actor_id actor,viewer; uint32_t source_number;
    if (!actor_read(o,player->actor,false,true,&actor,e) ||
        !frontend_remote_unified_player(o->replica,&viewer,&source_number)) return false;
    if (!qa_actor_id_equal(actor,viewer)) return true;
    switch (player->kind) {
    case QA_Q2_PLAYER_HELP: o->help_open=player->visible; return true;
    case QA_Q2_PLAYER_INVENTORY: if (player->visible) o->help_open=false; return true;
    case QA_Q2_PLAYER_SCOREBOARD: o->help_open=false; return true;
    case QA_Q2_PLAYER_VIEW: if (!(player->view.layouts&1)) o->help_open=false; return true;
    default: return fail(e,"RR help interlock received an unsupported actual player overlay");
    }
}
bool frontend_unified_q2_rr_create(qa_frontend *f, frontend_remote_unified *replica,
    frontend_unified_media *media, frontend_unified_events *events, frontend_unified_q2_rr_hud **out, qa_error *e)
{
    if (!f || !replica || !media || !events || !out || *out || !frontend_remote_unified_domain_read(replica))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"RR HUD requires its genuine CLIENT and retained media");
    frontend_unified_q2_rr_hud *o=calloc(1,sizeof(*o));
    if (!o) return frontend_unified_fail(e,QA_ERROR_MEMORY,"Creating actual received RR HUD owner");
    o->frontend=f; o->replica=replica; o->media=media; o->events=events;
    const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(replica);
    o->prints=f->seats[domain->physical_seat].hud;
    o->localizations=qa_localization_pool_create(e);
    if (!o->localizations || !current(o,e)) {
        qa_localization_pool_destroy(o->localizations); free(o); return false;
    }
    if (f->source_restoring) controls_bind(o,domain->cvars);
    *out=o; return true;
}
bool frontend_unified_q2_rr_checkpoint_ready(const frontend_unified_q2_rr_hud *o)
{ return !o || (!o->busy && (!o->prints || qa_hud_idle(o->prints))); }
bool frontend_unified_q2_rr_idle(const frontend_unified_q2_rr_hud *o)
{ return (!o || !o->prepared) && frontend_unified_q2_rr_checkpoint_ready(o); }
bool frontend_unified_q2_rr_destroy(frontend_unified_q2_rr_hud **slot, qa_error *e)
{
    if (!slot || !*slot) return true;
    frontend_unified_q2_rr_hud *o=*slot;
    if (o->busy || (o->prints && !qa_hud_idle(o->prints)))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"RR HUD still owns active callbacks");
    for (size_t i=0; i<o->poi_count; ++i) record_clear(o->pois+i);
    for (size_t i=0; i<o->damage_count; ++i) record_clear(&o->damage[i].row);
    while (o->bars) { rr_bar *row=o->bars; o->bars=row->next; record_clear(&row->row); free(row); }
    record_clear(&o->path); record_clear(&o->coop); record_clear(&o->report); record_clear(&o->objective);
    record_clear(&o->mission); record_clear(&o->help); record_clear(&o->pending_objective);
    while (o->retired) { rr_retired *row=o->retired; o->retired=row->next; free(row->provider); free(row); }
    qa_unified_document_destroy(o->frame); qa_unified_document_destroy(o->prepared);
    qa_localization_pool_destroy(o->localizations); free(o); *slot=NULL; return true;
}
static bool owner_parse(const qa_unified_presentation_event *row, const char **provider,
    uint64_t *generation, bool *is_retired, qa_error *e)
{
    if (!row || row->payload.kind!=QA_UNIFIED_PRESENTATION_OWNER || !isfinite(row->seconds))
        return fail(e,"RR owner event lacks its actual Source payload");
    const qa_unified_owner_event *event=&row->payload.value.owner;
    if ((unsigned)event->kind>QA_UNIFIED_OWNER_REFRESHED || !event->owner.provider ||
        !*event->owner.provider || !event->owner.generation)
        return fail(e,"RR owner event lacks its actual lifetime token");
    *provider=event->owner.provider; *generation=event->owner.generation;
    *is_retired=event->kind==QA_UNIFIED_OWNER_RETIRED; return true;
}
bool frontend_unified_q2_rr_owner_validate(frontend_unified_q2_rr_hud *o,
    const qa_unified_presentation_event *row, qa_error *e)
{
    if (!o || !row || !current(o,e)) return false;
    const char *provider; uint64_t generation; bool is_retired;
    return owner_parse(row,&provider,&generation,&is_retired,e);
}
static bool owned(const rr_record *r, const rr_retired *owner)
{ return r->event && r->owner_generation==owner->generation && r->owner_provider && !strcmp(r->owner_provider,owner->provider); }
bool frontend_unified_q2_rr_owner_retire(frontend_unified_q2_rr_hud *o,
    const qa_unified_presentation_event *row, qa_error *e)
{
    if (!o || !row || o->busy || !current(o,e)) return false;
    const char *provider; uint64_t generation; bool is_retired;
    if (!owner_parse(row,&provider,&generation,&is_retired,e)) return false;
    if (!is_retired) return true;
    for (rr_retired *item=o->retired; item; item=item->next)
        if (item->generation==generation && !strcmp(item->provider,provider)) return true;
    rr_retired *item=calloc(1,sizeof(*item));
    if (!item) return frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining actual RR presentation retirement");
    size_t size=strlen(provider)+1;
    item->provider=malloc(size);
    if (!item->provider) { free(item); return frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining actual RR owner identity"); }
    memcpy(item->provider,provider,size);
    item->generation=generation; item->next=o->retired; o->retired=item;
    size_t kept=0;
    for (size_t i=0; i<o->poi_count; ++i) {
        if (owned(o->pois+i,item)) record_clear(o->pois+i);
        else { if (kept!=i) { o->pois[kept]=o->pois[i]; o->pois[i]=(rr_record){0}; } ++kept; }
    }
    o->poi_count=kept; kept=0;
    for (size_t i=0; i<o->damage_count; ++i) {
        if (owned(&o->damage[i].row,item)) record_clear(&o->damage[i].row);
        else { if (kept!=i) { o->damage[kept]=o->damage[i]; o->damage[i]=(rr_damage){0}; } ++kept; }
    }
    o->damage_count=kept;
    rr_bar **bar=&o->bars;
    while (*bar) {
        rr_bar *entry=*bar;
        if (owned(&entry->row,item)) { *bar=entry->next; record_clear(&entry->row); free(entry); }
        else bar=&entry->next;
    }
    rr_record *records[]={&o->path,&o->coop,&o->report,&o->objective,&o->mission,&o->help,&o->pending_objective};
    for (size_t i=0; i<sizeof(records)/sizeof(*records); ++i) if (owned(records[i],item)) {
        if (records[i]==&o->objective || records[i]==&o->pending_objective)
            if (!qa_hud_clear_center(o->prints,e)) return false;
        if (records[i]==&o->help) o->help_open=false;
        record_clear(records[i]);
    }
    if (!o->pending_objective.event) { o->pending_printed=false; o->pending_sound=false; }
    return true;
}
static bool frame_clock(const qa_unified_document *d, double *seconds, qa_error *e)
{
    if (!d || qa_unified_document_type(d)!=QA_UNIFIED_FRAME_DOCUMENT) return fail(e,"RR HUD clock lacks its actual committed frame");
    const qa_unified_frame *frame=qa_unified_document_frame(d);
    if (!frame || !frame->world) return fail(e,"RR HUD clock lost its typed Source frame");
    *seconds=frame->world->presentation_seconds;
    return true;
}
bool frontend_unified_q2_rr_frame_prepare(frontend_unified_q2_rr_hud *o, const qa_unified_document *d, qa_error *e)
{
    if (!o || o->busy || o->prepared || !current(o,e) || !frame_clock(d,&o->prepared_seconds,e)) return false;
    return qa_unified_document_retain(d,&o->prepared,e);
}
bool frontend_unified_q2_rr_frame_ready(frontend_unified_q2_rr_hud *o, const qa_unified_document *d, qa_error *e)
{
    double seconds;
    return o && !o->busy && o->prepared && qa_hud_idle(o->prints) && current(o,e) &&
        frame_clock(d,&seconds,e) && seconds==o->prepared_seconds && o->prepared==d;
}
void frontend_unified_q2_rr_frame_commit(frontend_unified_q2_rr_hud *o)
{
    if (!o || o->busy || !o->prepared) return;
    qa_unified_document_destroy(o->frame); o->frame=o->prepared; o->prepared=NULL;
    o->seconds=o->prepared_seconds; o->have_view=false;
}
void frontend_unified_q2_rr_frame_abort(frontend_unified_q2_rr_hud *o)
{ if (o && !o->busy) { qa_unified_document_destroy(o->prepared); o->prepared=NULL; } }
bool frontend_unified_q2_rr_world(frontend_unified_q2_rr_hud *o, const qa_scene_view *view,
    const qa_scene_world_input *input, qa_scene_frame *frame, qa_error *e)
{
    if (!o || !view || !input || !frame || o->busy || !current(o,e) || !o->frame ||
        input->seconds!=o->seconds || frame!=&o->frontend->frame || !qa_vec_finite(view->origin)) return false;
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->replica);
    if (!d || view->seat!=d->physical_seat || !view->viewport.width || !view->viewport.height) return false;
    for (unsigned i=0; i<3; ++i) if (!qa_vec_finite(view->axis[i])) return false;
    o->view=*view; o->have_view=true; return true;
}
typedef struct rr_draw {
    qa_scene_frame *frame;
    qa_scene_rect viewport;
    qa_ui_presentation ui;
    qa_ui_preferences prefs;
    const qa_scene_image *white;
    float scale, x, y;
} rr_draw;
static bool draw_text(rr_draw *draw, const char *text_value, float x, float y,
    qa_font_alignment alignment, qa_scene_vec4 color, qa_error *e)
{
    if (!text_value || !*text_value) return true;
    qa_font_layout layout;
    qa_font_layout_options options={.text={(const uint8_t *)text_value,strlen(text_value)},
        .scale=draw->scale*draw->ui.text_scale,.color=color,.alignment=alignment,
        .color_codes=QA_FONT_COLOR_LITERAL,.max_width=544*draw->scale};
    if (!qa_font_layout_build(&draw->ui.fonts,&options,&draw->frame->storage,&layout,e)) return false;
    return qa_font_draw_layout(draw->frame,&layout,&(qa_font_draw_options){.seat=draw->ui.fonts.seat,
        .target=draw->viewport,.origin={draw->x+x*draw->scale,draw->y+y*draw->scale},
        .space=QA_FONT_PIXELS,.shadow_offset=draw->scale},e);
}
static bool draw_fill(rr_draw *draw, float x, float y, float width, float height, qa_scene_vec4 color, qa_error *e)
{
    return draw->white && qa_scene_frame_picture_f(draw->frame,draw->white,draw->viewport,
        (qa_scene_rect_f){draw->x+x*draw->scale,draw->y+y*draw->scale,width*draw->scale,height*draw->scale},
        (qa_scene_vec4){0,0,1,1},color,e);
}
static bool draw_pois(frontend_unified_q2_rr_hud *o, qa_actor_id viewer, rr_draw *draw, double edge, double maximum, qa_error *e)
{
    qa_scene_matrix projector=qa_scene_matrix_multiply(o->view.projection,qa_scene_view_matrix(&o->view));
    float left=(float)draw->viewport.x, top=(float)draw->viewport.y,
        width=(float)draw->viewport.width, height=(float)draw->viewport.height;
    for (size_t i=0; i<o->poi_count; ++i) {
        rr_record *row=o->pois+i;
        if (!qa_actor_id_equal(row->actor,viewer) || row->seconds*1000+row->value.poi.duration<=o->seconds*1000 || !row->image) continue;
        qa_scene_vec4 clip=qa_scene_matrix_point(projector,row->value.poi.origin);
        float divisor=clip.w==0?1:clip.w, x=left+(clip.x/divisor*.5f+.5f)*width,
            y=top+(-clip.y/divisor*.5f+.5f)*height;
        if (clip.w<0) {
            x=left*2+width-x; y=top*2+height-y;
            if (y>top) { x=x<left+width*.5f?left:left+width; y=fminf(y,top+height*.75f); }
        }
        float image_scale=1, distance=fminf(width,height)*(float)edge;
        if (maximum!=1 && distance>0) {
            float positions[2]={x-left,y-top}, extents[2]={width,height};
            for (unsigned axis=0; axis<2; ++axis) {
                float fraction=1;
                if (positions[axis]<distance) fraction=positions[axis]/distance;
                else if (positions[axis]>extents[axis]-distance) fraction=(extents[axis]-positions[axis])/distance;
                image_scale=fmaxf(image_scale,fminf((float)maximum,1+(1-fraction)*((float)maximum-1)));
            }
        }
        float w=(float)row->image->logical_width*draw->scale*image_scale, h=(float)row->image->logical_height*draw->scale*image_scale;
        qa_scene_vec4 tint=row->value.poi.tint;
        if (row->value.poi.flags&1) tint.w*=fmaxf(.25f,fminf(1,hypotf(x-left-width*.5f,y-top-height*.5f)/fmaxf(1,w*3)));
        qa_scene_rect_f rect={fmaxf(left,fminf(left+width-w,x-w*.5f)),fmaxf(top,fminf(top+height-h,y-h*.5f)),w,h};
        if (!qa_scene_frame_picture_f(draw->frame,row->image,draw->viewport,rect,(qa_scene_vec4){0,0,1,1},tint,e)) return false;
    }
    return true;
}
static bool draw_damage(frontend_unified_q2_rr_hud *o, qa_actor_id viewer, rr_draw *draw, double lifetime, qa_error *e)
{
    if (draw->prefs.reduced_flashes || lifetime<=0) return true;
    float yaw=atan2f(o->view.axis[0].y,o->view.axis[0].x);
    for (size_t i=0; i<o->damage_count; ++i) {
        rr_damage *entry=o->damage+i;
        if (!qa_actor_id_equal(entry->row.actor,viewer) || entry->expires_ms<=o->seconds*1000 || !entry->row.image) continue;
        const qa_scene_image *image=entry->row.image;
        float angle=yaw-atan2f(entry->direction.y,entry->direction.x)-3.14159265358979323846f,
            width=(float)fmin(image->logical_width,3*entry->amount), height=(float)image->logical_height,
            radius=(draw->prefs.crosshair?draw->prefs.crosshair_size:0)+height*.5f;
        qa_scene_vec4 tint={entry->color.x,entry->color.y,entry->color.z,
            (float)fmax(0,fmin(1,(entry->expires_ms-o->seconds*1000)/lifetime))};
        if (width<=0 || height<=0) continue;
        if (!qa_scene_frame_picture_f(draw->frame,image,draw->viewport,
            (qa_scene_rect_f){draw->x+(320+radius*sinf(angle)-width*.5f)*draw->scale,
                draw->y+(240-radius*cosf(angle)-height*.5f)*draw->scale,width*draw->scale,height*draw->scale},
            (qa_scene_vec4){0,0,1,1},tint,e)) return false;
    }
    return true;
}
typedef struct rr_level_order { const rr_level *level; size_t index; } rr_level_order;
static int level_compare(const void *a, const void *b)
{
    const rr_level_order *x=a,*y=b;
    if (x->level->visit_order<y->level->visit_order) return -1;
    if (x->level->visit_order>y->level->visit_order) return 1;
    return x->index<y->index?-1:x->index>y->index?1:0;
}
static bool draw_report(frontend_unified_q2_rr_hud *o, rr_draw *draw, qa_error *e)
{
    const qa_scene_vec4 white={1,1,1,1}, accent={1,.8f,.3f,1};
    if (!draw_fill(draw,48,48,544,360,(qa_scene_vec4){0,0,0,.8f},e) ||
        !draw_text(draw,"Unit complete",320,68,QA_FONT_ALIGN_CENTER,accent,e)) return false;
    size_t count=o->report.value.report.count;
    rr_level_order *order=count?qa_arena_alloc(&draw->frame->storage,count*sizeof(*order),_Alignof(rr_level_order),e):NULL;
    if (count && !order) return false;
    for (size_t i=0; i<count; ++i) order[i]=(rr_level_order){o->report.value.report.levels+i,i};
    if (count) qsort(order,count,sizeof(*order),level_compare);
    for (size_t i=0; i<count; ++i) {
        const rr_level *level=order[i].level; const char *name=*level->name?level->name:level->map;
        char killed[32],monsters[32],secrets[32],total[32],minutes[32],seconds[32];
        if (!qa_format_number(level->killed_monsters,killed,e) ||
            !qa_format_number(level->total_monsters,monsters,e) ||
            !qa_format_number(level->found_secrets,secrets,e) ||
            !qa_format_number(level->total_secrets,total,e) ||
            !qa_format_number(floor(level->time_seconds/60),minutes,e) ||
            !qa_format_number(floor(fmod(level->time_seconds,60)),seconds,e)) return false;
        size_t size=strlen(name); if (size>SIZE_MAX-256) return fail(e,"RR unit report row exceeds storage");
        char *line=qa_arena_alloc(&draw->frame->storage,size+256,1,e);
        if (!line) return false;
        snprintf(line,size+256,"%s: %s/%s kills  %s/%s secrets  %s:%s%s",name,killed,monsters,secrets,total,minutes,
            strlen(seconds)<2?"0":"",seconds);
        if (!draw_text(draw,line,68,104+(float)i*20,QA_FONT_ALIGN_LEFT,white,e)) return false;
    }
    return o->seconds*1000<o->report.value.report.ready*1000 ||
        draw_text(draw,"[ ] Press attack to continue",68,384,QA_FONT_ALIGN_LEFT,accent,e);
}
bool frontend_unified_q2_rr_draw(frontend_unified_q2_rr_hud *o, qa_ui *ui,
    qa_scene_rect viewport, qa_scene_frame *frame, qa_error *e)
{
    if (!o || !ui || !frame || o->busy || !current(o,e)) return false;
    const frontend_remote_unified_domain *domain=frontend_remote_unified_domain_read(o->replica);
    qa_actor_id viewer; uint32_t source_number;
    if (!domain || ui!=o->frontend->seats[domain->physical_seat].ui ||
        !frontend_remote_unified_player(o->replica,&viewer,&source_number)) return false;
    rr_draw draw={.frame=frame,.viewport=viewport};
    bool okay=qa_ui_presentation_read(ui,&draw.ui,e) &&
        qa_ui_preferences_read(qa_application_cvars(domain->application),qa_application_ui_preference_handles(domain->application),domain->physical_seat,&draw.prefs,e);
    if (!okay) return false;
    draw.scale=fminf((float)viewport.width/640.f,(float)viewport.height/480.f);
    draw.x=(float)viewport.x+((float)viewport.width-640*draw.scale)*.5f; draw.y=(float)viewport.y+((float)viewport.height-480*draw.scale)*.5f;
    const char *content=o->report.event?o->report.content:o->bars?o->bars->row.content:
        o->help.event?o->help.content:o->path.event?o->path.content:o->coop.event?o->coop.content:
        o->poi_count?o->pois[0].content:o->damage_count?o->damage[0].row.content:NULL;
    if (content) { frontend_unified_bank_view bank={0};
        okay=media_bank(o,content,false,&bank,e); if (okay) draw.white=qa_scene_white(bank.images); }
    o->busy=true;
    bool show_pois=false,show_damage=false; double lifetime=0,edge=0,maximum=1;
    if (okay && (o->poi_count || o->damage_count)) okay=controls(o,&show_pois,&show_damage,&lifetime,&edge,&maximum,e);
    if (okay && o->have_view && show_pois) okay=draw_pois(o,viewer,&draw,edge,maximum,e);
    rr_draw grouped=draw;
    grouped.scale=draw.scale*draw.prefs.hud_scale;
    grouped.x=draw.x+320*draw.scale*(1-draw.prefs.hud_scale);
    grouped.y=draw.y+240*draw.scale*(1-draw.prefs.hud_scale);
    if (okay && o->have_view && show_damage) okay=draw_damage(o,viewer,&grouped,lifetime,e);
    if (okay && o->have_view && o->path.event && qa_actor_id_equal(o->path.actor,viewer) &&
        o->path.seconds*1000+10000>o->seconds*1000) {
        qa_scene_matrix projector=qa_scene_matrix_multiply(o->view.projection,qa_scene_view_matrix(&o->view));
        for (unsigned i=0; okay && i<3; ++i) {
            qa_scene_vec4 point=qa_scene_matrix_point(projector,qa_vec_add(o->path.value.path.origin,
                qa_vec_scale(o->path.value.path.direction,(float)i*24)));
            if (point.w>0) okay=qa_scene_frame_picture_f(frame,draw.white,viewport,
                (qa_scene_rect_f){(float)viewport.x+(point.x/point.w*.5f+.5f)*(float)viewport.width-3,
                    (float)viewport.y+(-point.y/point.w*.5f+.5f)*(float)viewport.height-3,6,6},
                (qa_scene_vec4){0,0,1,1},(qa_scene_vec4){1,.8f,.3f,1},e);
        }
    }
    size_t index=0;
    grouped.y=draw.y;
    for (rr_bar *bar=o->bars; okay && bar; bar=bar->next) {
        if (!qa_actor_id_equal(bar->row.actor,viewer)) continue;
        rr_record *row=&bar->row; float y=24+(float)index*36;
        okay=draw_text(&grouped,row->localized,320,y,QA_FONT_ALIGN_CENTER,(qa_scene_vec4){1,1,1,1},e) &&
            draw_fill(&grouped,160,y+20,320,8,(qa_scene_vec4){0,0,0,.8f},e) &&
            draw_fill(&grouped,160,y+20,320*(float)fmax(0,fmin(1,row->value.bar.fraction)),8,(qa_scene_vec4){.8f,.12f,.08f,1},e);
        ++index;
    }
    if (okay && o->mission.event && qa_actor_id_equal(o->mission.actor,viewer) &&
        o->mission.value.mission.visible && o->objective.event && qa_actor_id_equal(o->objective.actor,viewer) &&
        o->objective.localized && *o->objective.localized)
        okay=draw_text(&draw,"New objective",320,394,QA_FONT_ALIGN_CENTER,(qa_scene_vec4){1,.8f,.3f,1},e);
    if (okay && o->coop.event && qa_actor_id_equal(o->coop.actor,viewer)) {
        if (o->coop.value.coop.state)
            okay=draw_text(&draw,o->coop.localized,320,360,QA_FONT_ALIGN_CENTER,(qa_scene_vec4){1,.8f,.3f,1},e);
        if (okay && o->coop.value.coop.lives!=0) {
            char lives[32]; okay=qa_format_number(o->coop.value.coop.lives,lives,e) &&
                draw_text(&draw,lives,624,2,QA_FONT_ALIGN_RIGHT,(qa_scene_vec4){1,1,1,1},e) &&
                draw_text(&draw,o->coop.secondary_localized,624,28,QA_FONT_ALIGN_RIGHT,(qa_scene_vec4){1,1,1,1},e);
        }
    }
    if (okay && o->report.event) okay=draw_report(o,&draw,e);
    else if (okay && frontend_unified_q2_rr_help_visible(o))
        okay=draw_fill(&draw,48,48,544,360,(qa_scene_vec4){0,0,0,.8f},e) &&
            draw_text(&draw,"Help computer",320,68,QA_FONT_ALIGN_CENTER,(qa_scene_vec4){1,.8f,.3f,1},e) &&
            draw_text(&draw,o->help.localized,68,104,QA_FONT_ALIGN_LEFT,(qa_scene_vec4){1,1,1,1},e) &&
            draw_text(&draw,o->help.secondary_localized,68,144,QA_FONT_ALIGN_LEFT,(qa_scene_vec4){1,1,1,1},e);
    o->busy=false; return okay;
}
