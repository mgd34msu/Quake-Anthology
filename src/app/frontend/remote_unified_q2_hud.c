#include "remote_unified_q2_hud.h"
#include "remote_unified_private.h"
#include "remote_unified_save.h"
#include "save_private.h"
#include "qa/hud.h"
#include "qa/ui_preferences.h"
#include "qa/caption_save.h"
#include "qa/text.h"
#include <float.h>
#include <math.h>
#include <stdio.h>

typedef enum rr_kind {
    RR_POI, RR_KEYED_POI, RR_REMOVE_POI, RR_HEALTHBAR, RR_DAMAGE,
    RR_PATH, RR_COOP, RR_REPORT, RR_OBJECTIVE, RR_MISSION, RR_HELP, RR_UNKNOWN
} rr_kind;
static const char *const variants[] = {
    "poi", "keyed-poi", "remove-poi", "healthbar", "directional-damage",
    "help-path", "coop-respawn", "end-of-unit", "mission-objective", "mission-status", "help-computer"
};
static const char *const coop_states[] = {"none", "in-combat", "bad-area", "blocked", "waiting", "no-lives"};
static const char *const coop_keys[] = {"", "$g_coop_respawn_in_combat", "$g_coop_respawn_bad_area",
    "$g_coop_respawn_blocked", "$g_coop_respawn_waiting", "$g_coop_respawn_no_lives"};
typedef struct rr_level {
    char *map, *name;
    double order, total_secrets, found_secrets, total_monsters, killed_monsters, time;
} rr_level;
typedef struct rr_record {
    qa_unified_document *document;
    rr_kind kind;
    char *content, *source_provider, *owner_provider;
    uint64_t owner_generation;
    double seconds, frame_milliseconds;
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
};
static qa_json_id field(const qa_json_document *j, qa_json_id row, const char *name)
{ return qa_json_get(j, row, name); }
static bool fail(qa_error *e, const char *message)
{ return frontend_unified_fail(e, QA_ERROR_FORMAT, message); }
static bool current(frontend_unified_q2_rr_hud *o, qa_error *e)
{
    return o && frontend_unified_media_current(o->media) &&
        ((o->frontend->capture || o->frontend->source_restoring) ?
            frontend_remote_unified_checkpoint_current(o->replica, e) : frontend_remote_unified_current(o->replica, e));
}
static bool number(const qa_unified_document *d, qa_json_id id, double *out, qa_error *e)
{ return (qa_unified_document_number(d, id, out, e) && isfinite(*out)) || fail(e, "RR HUD number is not finite"); }
static bool integer(const qa_unified_document *d, qa_json_id id, int32_t *out, qa_error *e)
{
    double value;
    if (!number(d, id, &value, e)) return false;
    if (value < INT32_MIN || value > INT32_MAX || trunc(value) != value) return fail(e, "RR HUD integer exceeds its source word");
    *out = (int32_t)value; return true;
}
static bool vector(const qa_unified_document *d, qa_json_id id, qa_vec3 *out, qa_error *e)
{
    const qa_json_document *j = qa_unified_document_json(d); double value[3];
    if (!number(d, field(j,id,"x"), value, e) || !number(d, field(j,id,"y"), value+1, e) ||
        !number(d, field(j,id,"z"), value+2, e)) return false;
    for (unsigned i=0; i<3; ++i) if (fabs(value[i]) > FLT_MAX) return fail(e, "RR HUD vector exceeds native float storage");
    *out = qa_v3((float)value[0], (float)value[1], (float)value[2]); return true;
}
static bool text(const qa_unified_document *d, qa_json_id id, char **out, qa_error *e)
{
    qa_buffer value={0};
    if (!qa_json_string(qa_unified_document_json(d), id, &value, e)) return false;
    if (memchr(value.data, 0, value.size)) { qa_buffer_free(&value); return fail(e, "RR HUD string contains NUL"); }
    *out=(char *)value.data; return true;
}
static bool actor_read(frontend_unified_q2_rr_hud *o, const qa_unified_document *d, qa_json_id id,
    bool retained, bool resolve, qa_actor_id *out, qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d); uint64_t slot, generation;
    if (!qa_json_u64(j,field(j,id,"slot"),&slot,e) || slot>UINT32_MAX ||
        !qa_json_u64(j,field(j,id,"generation"),&generation,e) || !generation) return false;
    if (!resolve) return true;
    return retained ? frontend_remote_unified_actor_retained(o->replica,(uint32_t)slot,generation,out,e) :
        frontend_remote_unified_actor(o->replica,(uint32_t)slot,generation,out,e);
}
static rr_kind kind_read(const qa_unified_document *d, qa_json_id row)
{
    const qa_json_document *j=qa_unified_document_json(d);
    if (!qa_json_string_equal(j,field(j,row,"kind"),"q2-rerelease")) return RR_UNKNOWN;
    qa_json_id kind=field(j,field(j,row,"event"),"kind");
    for (unsigned i=0; i<RR_UNKNOWN; ++i) if (qa_json_string_equal(j,kind,variants[i])) return (rr_kind)i;
    return RR_UNKNOWN;
}
bool frontend_unified_q2_rr_known(const qa_unified_document *d, qa_json_id row)
{ return d && kind_read(d,row)!=RR_UNKNOWN; }
static void record_clear(rr_record *r)
{
    if (!r) return;
    switch (r->kind) {
    case RR_POI: case RR_KEYED_POI: free(r->value.poi.path); break;
    case RR_HEALTHBAR: free(r->value.bar.name); break;
    case RR_REPORT:
        for (size_t i=0; i<r->value.report.count; ++i) { free(r->value.report.levels[i].map); free(r->value.report.levels[i].name); }
        free(r->value.report.levels); break;
    case RR_OBJECTIVE:
        free(r->value.objective.text);
        for (size_t i=0; i<r->value.objective.count; ++i) free(r->value.objective.args[i]);
        free(r->value.objective.args); break;
    case RR_HELP: free(r->value.help.primary); free(r->value.help.secondary); break;
    default: break;
    }
    qa_unified_document_destroy(r->document); qa_scene_image_release(r->image);
    free(r->content); free(r->source_provider); free(r->owner_provider);
    free(r->localized); free(r->secondary_localized); *r=(rr_record){0};
}
static void record_replace(rr_record *destination, rr_record *source)
{ record_clear(destination); *destination=*source; *source=(rr_record){0}; }
static bool token(const qa_unified_document *d, qa_json_id id, char **provider, uint64_t *generation, qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d);
    if (id==QA_JSON_NONE || qa_json_type(j,id)==QA_JSON_NULL) { *generation=0; return true; }
    return text(d,field(j,id,"provider"),provider,e) && **provider &&
        qa_json_u64(j,field(j,id,"generation"),generation,e) && *generation;
}
static bool parse(frontend_unified_q2_rr_hud *o, const qa_unified_document *d, qa_json_id row,
    bool retained, bool resolve, rr_record *r, qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d);
    qa_json_id event=field(j,row,"event"), source=field(j,row,"source"); uint64_t sequence;
    r->kind=kind_read(d,row);
    if (r->kind==RR_UNKNOWN) return fail(e,"RR HUD event variant is unknown");
    bool okay=text(d,field(j,row,"content"),&r->content,e) && *r->content &&
        text(d,field(j,source,"provider"),&r->source_provider,e) && *r->source_provider &&
        qa_json_string_equal(j,field(j,source,"profile"),"rerelease") &&
        number(d,field(j,source,"frameMilliseconds"),&r->frame_milliseconds,e) && r->frame_milliseconds>0 &&
        number(d,field(j,row,"seconds"),&r->seconds,e) && r->seconds>=0 && isfinite(r->seconds*1000) &&
        qa_json_u64(j,field(j,row,"sequence"),&sequence,e) &&
        token(d,field(j,row,"owner"),&r->owner_provider,&r->owner_generation,e);
    if (okay && r->kind!=RR_REPORT) okay=actor_read(o,d,field(j,event,"actor"),retained,resolve,&r->actor,e);
    switch (r->kind) {
    case RR_POI: case RR_KEYED_POI:
        r->value.poi.key=1; r->value.poi.flags=1;
        if (okay && r->kind==RR_KEYED_POI) okay=integer(d,field(j,event,"key"),&r->value.poi.key,e) &&
            integer(d,field(j,event,"flags"),&r->value.poi.flags,e);
        okay=okay && vector(d,field(j,event,"position"),&r->value.poi.origin,e) &&
            text(d,field(j,event,"image"),&r->value.poi.path,e) && *r->value.poi.path &&
            number(d,field(j,event,"duration"),&r->value.poi.duration,e) &&
            integer(d,field(j,event,"color"),&r->value.poi.color,e);
        if (okay) okay=isfinite(r->seconds*1000+r->value.poi.duration);
        r->value.poi.tint=(qa_scene_vec4){1,1,1,1}; break;
    case RR_REMOVE_POI: okay=okay && integer(d,field(j,event,"key"),&r->value.remove.key,e); break;
    case RR_HEALTHBAR:
        okay=okay && integer(d,field(j,event,"slot"),&r->value.bar.slot,e) &&
            actor_read(o,d,field(j,event,"target"),retained,resolve,&r->target,e) &&
            text(d,field(j,event,"name"),&r->value.bar.name,e) &&
            number(d,field(j,event,"fraction"),&r->value.bar.fraction,e) &&
            qa_json_bool(j,field(j,event,"visible"),&r->value.bar.visible,e); break;
    case RR_DAMAGE:
        okay=okay && vector(d,field(j,event,"direction"),&r->value.damage.direction,e) &&
            number(d,field(j,event,"damage"),&r->value.damage.amount,e) &&
            qa_json_bool(j,field(j,event,"health"),&r->value.damage.health,e) &&
            qa_json_bool(j,field(j,event,"armor"),&r->value.damage.armor,e) &&
            qa_json_bool(j,field(j,event,"shield"),&r->value.damage.shield,e); break;
    case RR_PATH:
        okay=okay && vector(d,field(j,event,"position"),&r->value.path.origin,e) &&
            vector(d,field(j,event,"direction"),&r->value.path.direction,e) &&
            qa_json_bool(j,field(j,event,"first"),&r->value.path.first,e); break;
    case RR_COOP: {
        uint32_t state=0; bool found=false;
        for (; state<6; ++state) if (qa_json_string_equal(j,field(j,event,"state"),coop_states[state])) { found=true; break; }
        okay=okay && found && number(d,field(j,event,"lives"),&r->value.coop.lives,e);
        r->value.coop.state=state; break;
    }
    case RR_REPORT: {
        qa_json_id levels=field(j,event,"levels"); size_t count=qa_json_size(j,levels);
        okay=okay && qa_json_type(j,levels)==QA_JSON_ARRAY && count<=65536 &&
            number(d,field(j,event,"buttonTime"),&r->value.report.ready,e) && isfinite(r->value.report.ready*1000);
        if (okay && count) { r->value.report.levels=calloc(count,sizeof(rr_level));
            okay=r->value.report.levels!=NULL;
            if (!okay) frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining real RR unit level report"); }
        for (size_t i=0; okay && i<count; ++i) {
            rr_level *level=r->value.report.levels+i; r->value.report.count=i+1;
            qa_json_id item=qa_json_at(j,levels,i);
            okay=text(d,field(j,item,"map"),&level->map,e) && text(d,field(j,item,"name"),&level->name,e) &&
                number(d,field(j,item,"visitOrder"),&level->order,e) &&
                number(d,field(j,item,"totalSecrets"),&level->total_secrets,e) &&
                number(d,field(j,item,"foundSecrets"),&level->found_secrets,e) &&
                number(d,field(j,item,"totalMonsters"),&level->total_monsters,e) &&
                number(d,field(j,item,"killedMonsters"),&level->killed_monsters,e) && number(d,field(j,item,"time"),&level->time,e);
        }
        break;
    }
    case RR_OBJECTIVE: {
        qa_json_id args=field(j,event,"args"); size_t count=qa_json_size(j,args);
        okay=okay && text(d,field(j,event,"text"),&r->value.objective.text,e) &&
            qa_json_type(j,args)==QA_JSON_ARRAY && count<=65536 &&
            qa_json_bool(j,field(j,event,"talkSound"),&r->value.objective.talk,e);
        if (okay && count) { r->value.objective.args=calloc(count,sizeof(char *));
            okay=r->value.objective.args!=NULL;
            if (!okay) frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining real RR objective arguments"); }
        for (size_t i=0; okay && i<count; ++i) {
            r->value.objective.count=i+1; okay=text(d,qa_json_at(j,args,i),r->value.objective.args+i,e);
        }
        break;
    }
    case RR_MISSION: okay=okay && qa_json_bool(j,field(j,event,"iconVisible"),&r->value.mission.visible,e); break;
    case RR_HELP:
        okay=okay && text(d,field(j,event,"primary"),&r->value.help.primary,e) &&
            text(d,field(j,event,"secondary"),&r->value.help.secondary,e) &&
            qa_json_bool(j,field(j,event,"visible"),&r->value.help.visible,e) &&
            qa_json_bool(j,field(j,event,"slowTime"),&r->value.help.slow,e); break;
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
bool frontend_unified_q2_rr_validate(frontend_unified_q2_rr_hud *o, const qa_unified_document *d,
    qa_json_id row, qa_error *e)
{
    if (!o || !d || !current(o,e)) return false;
    rr_record record={0}; bool okay=parse(o,d,row,false,false,&record,e); record_clear(&record); return okay;
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
        qa_ui_preferences_read(qa_application_cvars(domain->application),domain->physical_seat,&prefs,e) &&
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
static bool clone_record(const qa_unified_document *d, qa_json_id row, rr_record *r, qa_error *e)
{ return qa_unified_document_create(QA_UNIFIED_CHECKPOINT,qa_json_source(qa_unified_document_json(d),row),&r->document,e); }
static bool controls(frontend_unified_q2_rr_hud *o, bool *pois, bool *damage, double *damage_ms,
    double *edge, double *maximum, qa_error *e)
{
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->replica);
    const qa_cvar_view *p=d?qa_cvars_find(d->cvars,"scr_pois"):NULL,
        *a=d?qa_cvars_find(d->cvars,"scr_damage_indicators"):NULL,
        *t=d?qa_cvars_find(d->cvars,"scr_damage_indicator_time"):NULL,
        *f=d?qa_cvars_find(d->cvars,"scr_poi_edge_frac"):NULL,
        *s=d?qa_cvars_find(d->cvars,"scr_poi_max_scale"):NULL;
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
static bool controls_admit(frontend_unified_q2_rr_hud *o, qa_error *e)
{
    if (o->controls_registered) return true;
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->replica);
    if (!d || !d->cvars || !d->command_context.owner || o->frontend->source_restoring)
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"RR controls require their genuine fresh private CLIENT source heap");
    for (size_t i=0; i<sizeof(control_defaults)/sizeof(*control_defaults); ++i)
        if (!qa_cvars_register(d->cvars,control_defaults[i].name,control_defaults[i].value,0,
            d->command_context.owner,"Received RR HUD",e) || !current(o,e)) return false;
    o->controls_registered=true; return true;
}
static bool controls_restored(const frontend_unified_q2_rr_hud *o)
{
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->replica);
    if (!d || !d->cvars) return false;
    for (size_t i=0; i<sizeof(control_defaults)/sizeof(*control_defaults); ++i)
        if (!qa_cvars_find(d->cvars,control_defaults[i].name)) return false;
    return true;
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
static bool document_same(const qa_unified_document *a, const qa_unified_document *b)
{
    if (!a || !b || qa_unified_document_type(a)!=qa_unified_document_type(b)) return false;
    qa_bytes x=qa_json_source(qa_unified_document_json(a),qa_unified_document_root(a)),
        y=qa_json_source(qa_unified_document_json(b),qa_unified_document_root(b));
    return x.size==y.size && (!x.size || !memcmp(x.data,y.data,x.size));
}
static bool objective_apply(frontend_unified_q2_rr_hud *o, rr_record *r, qa_error *e)
{
    if (o->pending_objective.document) {
        if (!document_same(o->pending_objective.document,r->document))
            return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"RR objective retry differs from its retained delivery prefix");
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
bool frontend_unified_q2_rr_presentation(frontend_unified_q2_rr_hud *o, const qa_unified_document *d,
    qa_json_id row, bool *mirrored, qa_error *e)
{
    if (!o || !d || !mirrored || o->busy || o->frontend->capture || o->frontend->resource_inventory ||
        o->frontend->source_restoring || !current(o,e)) return false;
    *mirrored=false; rr_record r={0}; bool okay=parse(o,d,row,false,true,&r,e);
    qa_actor_id viewer; uint32_t number_id;
    if (okay) okay=frontend_remote_unified_player(o->replica,&viewer,&number_id);
    if (okay && ((r.kind!=RR_REPORT && !qa_actor_id_equal(viewer,r.actor)) || retired(o,&r))) {
        record_clear(&r); return true;
    }
    if (okay) okay=clone_record(d,row,&r,e);
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
    return o && o->help.document && o->help_open &&
        frontend_remote_unified_player(o->replica,&viewer,&number_id) && qa_actor_id_equal(o->help.actor,viewer);
}
bool frontend_unified_q2_rr_overlay(frontend_unified_q2_rr_hud *o, const qa_unified_document *d,
    qa_json_id row, qa_error *e)
{
    if (!o || !d || o->busy || !current(o,e)) return false;
    const qa_json_document *j=qa_unified_document_json(d);
    qa_json_id event=field(j,row,"event"), kind=field(j,event,"kind");
    qa_actor_id actor,viewer; uint32_t source_number;
    if (!qa_json_string_equal(j,field(j,row,"kind"),"q2-player") ||
        !actor_read(o,d,field(j,event,"actor"),false,true,&actor,e) ||
        !frontend_remote_unified_player(o->replica,&viewer,&source_number)) return false;
    if (!qa_actor_id_equal(actor,viewer)) return true;
    if (qa_json_string_equal(j,kind,"help")) {
        bool visible;
        if (!qa_json_bool(j,field(j,event,"visible"),&visible,e)) return false;
        o->help_open=visible; return true;
    }
    if (qa_json_string_equal(j,kind,"inventory")) {
        qa_json_id flag=field(j,event,"visible"); bool visible=true;
        if (flag!=QA_JSON_NONE && !qa_json_bool(j,flag,&visible,e)) return false;
        if (visible) o->help_open=false;
        return true;
    }
    if (qa_json_string_equal(j,kind,"scoreboard")) { o->help_open=false; return true; }
    if (qa_json_string_equal(j,kind,"view")) {
        uint64_t layouts;
        if (!qa_json_u64(j,field(j,field(j,event,"view"),"layouts"),&layouts,e)) return false;
        if (!(layouts&1)) o->help_open=false;
        return true;
    }
    return fail(e,"RR help interlock received an unsupported actual player overlay");
}
static bool empty_hud(void *ctx, const qa_hud_frame *frame, qa_hud_data *out, qa_error *e)
{
    frontend_unified_q2_rr_hud *o=ctx; (void)frame;
    if (!current(o,e)) return false;
    *out=(qa_hud_data){.source_vitals=true}; return true;
}
static qa_hud_options print_options(frontend_unified_q2_rr_hud *o)
{
    const frontend_remote_unified_domain *d=frontend_remote_unified_domain_read(o->replica);
    return (qa_hud_options){.ui=o->frontend->seats[d->physical_seat].ui,.application=d->application,
        .seat=d->physical_seat,.context=o,.read=empty_hud};
}
bool frontend_unified_q2_rr_create(qa_frontend *f, frontend_remote_unified *replica,
    frontend_unified_media *media, frontend_unified_events *events, frontend_unified_q2_rr_hud **out, qa_error *e)
{
    if (!f || !replica || !media || !events || !out || *out || !frontend_remote_unified_domain_read(replica))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"RR HUD requires its genuine CLIENT and retained media");
    frontend_unified_q2_rr_hud *o=calloc(1,sizeof(*o));
    if (!o) return frontend_unified_fail(e,QA_ERROR_MEMORY,"Creating actual received RR HUD owner");
    o->frontend=f; o->replica=replica; o->media=media; o->events=events;
    o->localizations=qa_localization_pool_create(e); qa_hud_options options=print_options(o);
    if (!o->localizations || !current(o,e) || !qa_hud_create(&options,&o->prints,e)) {
        qa_localization_pool_destroy(o->localizations); free(o); return false;
    }
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
    if (o->prints && !qa_hud_destroy(o->prints,e)) return false;
    for (size_t i=0; i<o->poi_count; ++i) record_clear(o->pois+i);
    for (size_t i=0; i<o->damage_count; ++i) record_clear(&o->damage[i].row);
    while (o->bars) { rr_bar *row=o->bars; o->bars=row->next; record_clear(&row->row); free(row); }
    record_clear(&o->path); record_clear(&o->coop); record_clear(&o->report); record_clear(&o->objective);
    record_clear(&o->mission); record_clear(&o->help); record_clear(&o->pending_objective);
    while (o->retired) { rr_retired *row=o->retired; o->retired=row->next; free(row->provider); free(row); }
    qa_unified_document_destroy(o->frame); qa_unified_document_destroy(o->prepared);
    qa_localization_pool_destroy(o->localizations); free(o); *slot=NULL; return true;
}
static bool owner_parse(const qa_unified_document *d, qa_json_id row, char **provider,
    uint64_t *generation, bool *is_retired, qa_error *e)
{
    const qa_json_document *j=qa_unified_document_json(d);
    qa_json_id event=field(j,row,"event"), kind=field(j,event,"kind"); double seconds; uint64_t sequence;
    *is_retired=qa_json_string_equal(j,kind,"retired");
    return qa_json_string_equal(j,field(j,row,"kind"),"presentation-owner") &&
        (*is_retired || qa_json_string_equal(j,kind,"refreshed")) &&
        token(d,field(j,event,"owner"),provider,generation,e) && *generation &&
        number(d,field(j,row,"seconds"),&seconds,e) && qa_json_u64(j,field(j,row,"sequence"),&sequence,e);
}
bool frontend_unified_q2_rr_owner_validate(frontend_unified_q2_rr_hud *o, const qa_unified_document *d,
    qa_json_id row, qa_error *e)
{
    if (!o || !d || !current(o,e)) return false;
    char *provider=NULL; uint64_t generation=0; bool is_retired;
    bool okay=owner_parse(d,row,&provider,&generation,&is_retired,e); free(provider); return okay;
}
static bool owned(const rr_record *r, const rr_retired *owner)
{ return r->document && r->owner_generation==owner->generation && r->owner_provider && !strcmp(r->owner_provider,owner->provider); }
bool frontend_unified_q2_rr_owner_retire(frontend_unified_q2_rr_hud *o, const qa_unified_document *d,
    qa_json_id row, qa_error *e)
{
    if (!o || !d || o->busy || !current(o,e)) return false;
    char *provider=NULL; uint64_t generation=0; bool is_retired;
    if (!owner_parse(d,row,&provider,&generation,&is_retired,e)) { free(provider); return false; }
    if (!is_retired) { free(provider); return true; }
    for (rr_retired *item=o->retired; item; item=item->next)
        if (item->generation==generation && !strcmp(item->provider,provider)) { free(provider); return true; }
    rr_retired *item=calloc(1,sizeof(*item));
    if (!item) { free(provider); return frontend_unified_fail(e,QA_ERROR_MEMORY,"Retaining actual RR presentation retirement"); }
    item->provider=provider; item->generation=generation; item->next=o->retired; o->retired=item;
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
    if (!o->pending_objective.document) { o->pending_printed=false; o->pending_sound=false; }
    return true;
}
static bool frame_clock(const qa_unified_document *d, double *seconds, qa_error *e)
{
    if (!d || qa_unified_document_type(d)!=QA_UNIFIED_FRAME_DOCUMENT) return fail(e,"RR HUD clock lacks its actual committed frame");
    const qa_json_document *j=qa_unified_document_json(d); qa_json_id root=qa_unified_document_root(d);
    qa_json_id time=field(j,field(j,field(j,field(j,root,"output"),"snapshot"),"frame"),"time");
    if (!number(d,field(j,time,"value"),seconds,e)) return false;
    if (qa_json_string_equal(j,field(j,time,"kind"),"milliseconds")) *seconds/=1000;
    else if (!qa_json_string_equal(j,field(j,time,"kind"),"seconds")) return false;
    return *seconds>=0 && isfinite(*seconds*1000);
}
bool frontend_unified_q2_rr_frame_prepare(frontend_unified_q2_rr_hud *o, const qa_unified_document *d, qa_error *e)
{
    if (!o || o->busy || o->prepared || !current(o,e) || !frame_clock(d,&o->prepared_seconds,e)) return false;
    return frontend_unified_clone(d,&o->prepared,e);
}
bool frontend_unified_q2_rr_frame_ready(frontend_unified_q2_rr_hud *o, const qa_unified_document *d, qa_error *e)
{
    double seconds;
    return o && !o->busy && o->prepared && qa_hud_idle(o->prints) && current(o,e) &&
        frame_clock(d,&seconds,e) && seconds==o->prepared_seconds && document_same(o->prepared,d);
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
    if (x->level->order<y->level->order) return -1;
    if (x->level->order>y->level->order) return 1;
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
        if (!qa_format_ecmascript_number(level->killed_monsters,killed,e) ||
            !qa_format_ecmascript_number(level->total_monsters,monsters,e) ||
            !qa_format_ecmascript_number(level->found_secrets,secrets,e) ||
            !qa_format_ecmascript_number(level->total_secrets,total,e) ||
            !qa_format_ecmascript_number(floor(level->time/60),minutes,e) ||
            !qa_format_ecmascript_number(floor(fmod(level->time,60)),seconds,e)) return false;
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
        qa_ui_preferences_read(qa_application_cvars(domain->application),domain->physical_seat,&draw.prefs,e);
    if (!okay) return false;
    draw.scale=fminf((float)viewport.width/640.f,(float)viewport.height/480.f);
    draw.x=(float)viewport.x+((float)viewport.width-640*draw.scale)*.5f; draw.y=(float)viewport.y+((float)viewport.height-480*draw.scale)*.5f;
    const char *content=o->report.document?o->report.content:o->bars?o->bars->row.content:
        o->help.document?o->help.content:o->path.document?o->path.content:o->coop.document?o->coop.content:
        o->poi_count?o->pois[0].content:o->damage_count?o->damage[0].row.content:NULL;
    if (content) { frontend_unified_bank_view bank={0};
        okay=media_bank(o,content,false,&bank,e); if (okay) draw.white=qa_scene_white(bank.images); }
    o->busy=true;
    if (okay && ((o->objective.document && qa_actor_id_equal(o->objective.actor,viewer)) ||
        (o->pending_objective.document && qa_actor_id_equal(o->pending_objective.actor,viewer))))
        okay=qa_hud_draw(o->prints,&(qa_hud_frame){.seat=domain->physical_seat,.actor=viewer,
        .time_ns=nanoseconds(o->seconds),.viewport=viewport,.safe_area=viewport,.scale=1,.visible=true},frame,e);
    bool show_pois=false,show_damage=false; double lifetime=0,edge=0,maximum=1;
    if (okay && (o->poi_count || o->damage_count)) okay=controls(o,&show_pois,&show_damage,&lifetime,&edge,&maximum,e);
    if (okay && o->have_view && show_pois) okay=draw_pois(o,viewer,&draw,edge,maximum,e);
    rr_draw grouped=draw;
    grouped.scale=draw.scale*draw.prefs.hud_scale;
    grouped.x=draw.x+320*draw.scale*(1-draw.prefs.hud_scale);
    grouped.y=draw.y+240*draw.scale*(1-draw.prefs.hud_scale);
    if (okay && o->have_view && show_damage) okay=draw_damage(o,viewer,&grouped,lifetime,e);
    if (okay && o->have_view && o->path.document && qa_actor_id_equal(o->path.actor,viewer) &&
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
    if (okay && o->mission.document && qa_actor_id_equal(o->mission.actor,viewer) &&
        o->mission.value.mission.visible && o->objective.document && qa_actor_id_equal(o->objective.actor,viewer) &&
        o->objective.localized && *o->objective.localized)
        okay=draw_text(&draw,"New objective",320,394,QA_FONT_ALIGN_CENTER,(qa_scene_vec4){1,.8f,.3f,1},e);
    if (okay && o->coop.document && qa_actor_id_equal(o->coop.actor,viewer)) {
        if (o->coop.value.coop.state)
            okay=draw_text(&draw,o->coop.localized,320,360,QA_FONT_ALIGN_CENTER,(qa_scene_vec4){1,.8f,.3f,1},e);
        if (okay && o->coop.value.coop.lives!=0) {
            char lives[32]; okay=qa_format_ecmascript_number(o->coop.value.coop.lives,lives,e) &&
                draw_text(&draw,lives,624,2,QA_FONT_ALIGN_RIGHT,(qa_scene_vec4){1,1,1,1},e) &&
                draw_text(&draw,o->coop.secondary_localized,624,28,QA_FONT_ALIGN_RIGHT,(qa_scene_vec4){1,1,1,1},e);
        }
    }
    if (okay && o->report.document) okay=draw_report(o,&draw,e);
    else if (okay && frontend_unified_q2_rr_help_visible(o))
        okay=draw_fill(&draw,48,48,544,360,(qa_scene_vec4){0,0,0,.8f},e) &&
            draw_text(&draw,"Help computer",320,68,QA_FONT_ALIGN_CENTER,(qa_scene_vec4){1,.8f,.3f,1},e) &&
            draw_text(&draw,o->help.localized,68,104,QA_FONT_ALIGN_LEFT,(qa_scene_vec4){1,1,1,1},e) &&
            draw_text(&draw,o->help.secondary_localized,68,144,QA_FONT_ALIGN_LEFT,(qa_scene_vec4){1,1,1,1},e);
    o->busy=false; return okay;
}
static bool blob(qa_source_save_io *io, qa_buffer *bytes)
{
    size_t size=bytes->size;
    if (!qa_source_save_count(io,&size,io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) {
        bytes->data=size?malloc(size):NULL; bytes->size=size;
        if (size && !bytes->data) return frontend_unified_fail(io->error,QA_ERROR_MEMORY,"Retaining RR HUD continuation bytes");
    }
    return qa_source_save_bytes(io,bytes->data,size);
}
static bool document_fields(qa_source_save_io *io, qa_unified_document **document, qa_unified_document_kind type)
{
    bool present=*document!=NULL;
    if (!qa_source_save_bool(io,&present)) return false;
    if (!present) return true;
    qa_buffer bytes={0}; bool read=io->direction==QA_SOURCE_SAVE_READ;
    if (!read) {
        if (qa_unified_document_type(*document)!=type) return false;
        qa_bytes source=qa_json_source(qa_unified_document_json(*document),qa_unified_document_root(*document));
        bytes=(qa_buffer){(uint8_t *)source.data,source.size};
    }
    bool okay=blob(io,&bytes);
    if (okay && read) okay=qa_unified_document_create(type,(qa_bytes){bytes.data,bytes.size},document,io->error);
    if (read) qa_buffer_free(&bytes);
    return okay;
}
static bool record_fields(qa_source_save_io *io, frontend_unified_q2_rr_hud *o,
    const frontend_unified_q2_refs *refs, rr_record *r)
{
    bool read=io->direction==QA_SOURCE_SAVE_READ;
    if (!document_fields(io,&r->document,QA_UNIFIED_CHECKPOINT)) return false;
    if (!r->document) return true;
    if (read && !parse(o,r->document,qa_unified_document_root(r->document),true,true,r,io->error)) return false;
    if (!frontend_save_text(io,&r->localized) || !frontend_save_text(io,&r->secondary_localized)) return false;
    bool image=r->image!=NULL;
    if (!qa_source_save_bool(io,&image)) return false;
    if (image) {
        uint64_t id=0; const qa_scene_image *decoded=NULL;
        if ((!read && (!refs->effects.image_encode ||
            !refs->effects.image_encode(refs->effects.context,r->image,&id,io->error))) ||
            !qa_source_save_u64(io,&id) || !id) return false;
        if (read) {
            frontend_unified_bank_view bank={0};
            if (!refs->effects.image_decode ||
                !refs->effects.image_decode(refs->effects.context,id,&decoded,io->error) || !decoded ||
                !media_bank(o,r->content,false,&bank,io->error) || qa_scene_image_resource_owner(decoded)!=bank.images) return false;
            qa_scene_image_retain(decoded); r->image=(qa_scene_image *)decoded;
        }
    }
    if (r->kind==RR_POI || r->kind==RR_KEYED_POI) {
        qa_scene_vec4 *tint=&r->value.poi.tint;
        if (!qa_source_save_f32(io,&tint->x) || !qa_source_save_f32(io,&tint->y) ||
            !qa_source_save_f32(io,&tint->z) || !qa_source_save_f32(io,&tint->w) ||
            !isfinite(tint->x) || !isfinite(tint->y) || !isfinite(tint->z) || !isfinite(tint->w)) return false;
    }
    if ((r->kind==RR_HEALTHBAR || r->kind==RR_COOP || r->kind==RR_OBJECTIVE || r->kind==RR_HELP) && !r->localized) return false;
    if ((r->kind==RR_HELP || r->kind==RR_COOP) && !r->secondary_localized) return false;
    return !retired(o,r);
}
static bool fields(qa_source_save_io *io, frontend_unified_q2_rr_hud *o, const frontend_unified_q2_refs *refs)
{
    bool read=io->direction==QA_SOURCE_SAVE_READ; uint8_t magic[4]={'Q','U','R','H'}; if (!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QURH",4) ||
        !qa_source_save_f64(io,&o->seconds) ||
        !isfinite(o->seconds) || o->seconds<0 || !document_fields(io,&o->frame,QA_UNIFIED_FRAME_DOCUMENT)) return false;
    if (o->frame) { double actual_seconds;
        if (!frame_clock(o->frame,&actual_seconds,io->error) || actual_seconds!=o->seconds) return false;
    } else if (o->seconds!=0) return false;
    if(!document_fields(io,&o->prepared,QA_UNIFIED_FRAME_DOCUMENT))return false;
    if(o->prepared){double actual_seconds;
        if(!qa_source_save_f64(io,&o->prepared_seconds) || !isfinite(o->prepared_seconds) ||
            !frame_clock(o->prepared,&actual_seconds,io->error) || actual_seconds!=o->prepared_seconds)return false;}
    size_t count=0; rr_retired **tail=&o->retired;
    if (!read) for (rr_retired *item=o->retired; item; item=item->next) ++count;
    if (!qa_source_save_count(io,&count,65536)) return false;
    for (size_t i=0; i<count; ++i) {
        if (read) { *tail=calloc(1,sizeof(**tail));
            if (!*tail) return frontend_unified_fail(io->error,QA_ERROR_MEMORY,"Restoring RR retirement ownership"); }
        rr_retired *item=*tail;
        if (!frontend_save_text(io,&item->provider) || !item->provider || !*item->provider ||
            !qa_source_save_u64(io,&item->generation) || !item->generation) return false;
        for (rr_retired *prior=o->retired; prior!=item; prior=prior->next)
            if (prior->generation==item->generation && !strcmp(prior->provider,item->provider)) return false;
        tail=&item->next;
    }
    size_t pois=o->poi_count;
    if (!qa_source_save_count(io,&pois,32)) return false;
    if (read) o->poi_count=pois;
    for (size_t i=0; i<pois; ++i) {
        rr_record *r=o->pois+i;
        if (!record_fields(io,o,refs,r) || !r->document || (r->kind!=RR_POI && r->kind!=RR_KEYED_POI)) return false;
        if (r->value.poi.key) for (size_t k=0; k<i; ++k) if (o->pois[k].value.poi.key==r->value.poi.key) return false;
    }
    size_t damage=o->damage_count;
    if (!qa_source_save_count(io,&damage,32)) return false;
    if (read) o->damage_count=damage;
    for (size_t i=0; i<damage; ++i) {
        rr_damage *entry=o->damage+i;
        if (!record_fields(io,o,refs,&entry->row) || !entry->row.document || entry->row.kind!=RR_DAMAGE ||
            !qa_source_save_vec3(io,&entry->direction) || !qa_source_save_vec3(io,&entry->color) ||
            !qa_source_save_f64(io,&entry->amount) || !qa_source_save_f64(io,&entry->expires_ms) ||
            !qa_source_save_bool(io,&entry->health) || !qa_source_save_bool(io,&entry->armor) ||
            !qa_source_save_bool(io,&entry->shield) || !qa_vec_finite(entry->direction) || !qa_vec_finite(entry->color) ||
            !isfinite(entry->amount) || !isfinite(entry->expires_ms)) return false;
    }
    size_t bars=0; rr_bar **bar_tail=&o->bars;
    if (!read) for (rr_bar *bar=o->bars; bar; bar=bar->next) ++bars;
    if (!qa_source_save_count(io,&bars,65536)) return false;
    int32_t previous=0;
    for (size_t i=0; i<bars; ++i) {
        if (read) { *bar_tail=calloc(1,sizeof(**bar_tail));
            if (!*bar_tail) return frontend_unified_fail(io->error,QA_ERROR_MEMORY,"Restoring actual RR health bar"); }
        rr_bar *bar=*bar_tail;
        if (!record_fields(io,o,refs,&bar->row) || !bar->row.document || bar->row.kind!=RR_HEALTHBAR ||
            !bar->row.value.bar.visible || (i && bar->row.value.bar.slot<=previous)) return false;
        previous=bar->row.value.bar.slot; bar_tail=&bar->next;
    }
    rr_record *records[]={&o->path,&o->coop,&o->report,&o->objective,&o->mission,&o->help,&o->pending_objective};
    const rr_kind kinds[]={RR_PATH,RR_COOP,RR_REPORT,RR_OBJECTIVE,RR_MISSION,RR_HELP,RR_OBJECTIVE};
    for (size_t i=0; i<sizeof(records)/sizeof(*records); ++i)
        if (!record_fields(io,o,refs,records[i]) || (records[i]->document && records[i]->kind!=kinds[i])) return false;
    if (!qa_source_save_bool(io,&o->controls_registered) || (o->controls_registered && !controls_restored(o)) ||
        (!o->controls_registered && (o->poi_count || o->damage_count || o->bars || o->path.document ||
            o->coop.document || o->report.document || o->objective.document || o->mission.document ||
            o->help.document || o->pending_objective.document)) ||
        !qa_source_save_bool(io,&o->help_open) || !qa_source_save_bool(io,&o->pending_printed) || !qa_source_save_bool(io,&o->pending_sound) ||
        ((!o->pending_objective.document) && (o->pending_printed || o->pending_sound)) ||
        (o->pending_sound && !o->pending_objective.value.objective.talk)) return false;
    qa_buffer catalogs={0};
    bool okay=read?blob(io,&catalogs):qa_localization_pool_checkpoint(o->localizations,&catalogs,io->error) && blob(io,&catalogs);
    if (okay && read) okay=qa_localization_pool_restore(o->localizations,(qa_bytes){catalogs.data,catalogs.size},io->error);
    qa_buffer_free(&catalogs); if (!okay) return false;
    qa_hud_checkpoint_refs hud_refs={.context=refs->effects.context,
        .image_encode=refs->effects.image_encode,.image_decode=refs->effects.image_decode};
    qa_buffer prints={0};
    okay=read?blob(io,&prints):qa_hud_checkpoint(o->prints,&hud_refs,&prints,io->error) && blob(io,&prints);
    if (okay && read) {
        qa_hud_options options=print_options(o);
        okay=qa_hud_destroy(o->prints,io->error); if (okay) o->prints=NULL;
        if (okay) okay=qa_hud_restore((qa_bytes){prints.data,prints.size},&options,&hud_refs,&o->prints,io->error);
    }
    qa_buffer_free(&prints); return okay;
}
bool frontend_unified_q2_rr_checkpoint(frontend_unified_q2_rr_hud *o, const frontend_unified_q2_refs *refs,
    qa_buffer *out, qa_error *e)
{
    if (!o || !refs || !refs->content || !out || out->data || out->size ||
        !frontend_unified_q2_rr_checkpoint_ready(o) || !current(o,e))
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"RR HUD checkpoint requires its genuine idle dictionary prefix");
    qa_source_save_io io={0}; o->busy=true;
    bool okay=qa_source_save_writer(&io,NULL,e) && fields(&io,o,refs) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); o->busy=false;
    if (!okay && (!e || e->code==QA_OK)) fail(e,"Invalid RR HUD ownership continuation");
    return okay;
}
bool frontend_unified_q2_rr_restore(qa_frontend *f, frontend_remote_unified *replica,
    frontend_unified_media *media, frontend_unified_events *events, const frontend_unified_q2_refs *refs,
    qa_bytes bytes, frontend_unified_q2_rr_hud **out, qa_error *e)
{
    if (!f || !f->source_restoring || !refs || !refs->content || !out || *out)
        return frontend_unified_fail(e,QA_ERROR_ARGUMENT,"RR HUD import requires the isolated restored CLIENT graph");
    frontend_unified_q2_rr_hud *o=NULL;
    if (!frontend_unified_q2_rr_create(f,replica,media,events,&o,e)) return false;
    qa_source_save_io io={0}; o->busy=true;
    bool okay=qa_source_save_reader(&io,NULL,bytes,e) && fields(&io,o,refs) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io); o->busy=false;
    if (!okay) {
        (void)frontend_unified_q2_rr_destroy(&o,NULL);
        if (!e || e->code==QA_OK) fail(e,"Invalid saved RR HUD source or dictionary identity");
        return false;
    }
    *out=o; return true;
}
