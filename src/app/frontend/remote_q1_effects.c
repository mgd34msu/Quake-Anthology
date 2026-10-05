#include "remote_q1_effects.h"
#include "remote_q1_private.h"
#include "remote_q1_hud.h"
#include "internal.h"
#include "selected_effects_particles.h"
#include "legacy_render_policy.h"
#include "received_music.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

enum { LIGHTS = 64, BEAMS = 32 };
typedef struct remote_light {
    qa_vec3 origin, color;
    double birth, die;
    float radius, decay, minimum;
    uint32_t entity;
    uint64_t identity;
    bool active;
} remote_light;
typedef struct remote_beam {
    qa_vec3 start, end;
    double die;
    uint32_t entity;
    uint8_t type;
    bool active;
} remote_beam;
typedef struct remote_trail {
    uint32_t entity, model;
    qa_vec3 origin;
    uint64_t sample;
} remote_trail;
typedef struct remote_ambient {
    qa_audio_asset *asset;
    qa_audio_mixer *mixer;
    qa_vec3 origin;
    float volume,attenuation;
    uint64_t identity;
} remote_ambient;
struct frontend_remote_q1_effects {
    frontend_received_music *music;
    bool music_retiring;
    frontend_fx_particles particles;
    qa_builtin_random random;
    qa_scene_image *image;
    remote_ambient *ambient;
    qa_audio_engine *audio;
    size_t ambient_count,ambient_capacity;
    remote_light lights[LIGHTS];
    qa_scene_light scene_lights[LIGHTS];
    remote_beam beams[BEAMS];
    remote_trail *trails;
    size_t trail_count, trail_capacity;
    uint64_t sample;
    double sampled_seconds, previous_sample;
    double bonus_until;
    bool sampled;
};

static bool owner(frontend_remote_q1 *row, qa_error *error)
{
    if (row->effects) return true;
    frontend_remote_q1_effects *fx = calloc(1, sizeof(*fx));
    if (!fx) return remote_q1_fail(error, QA_ERROR_MEMORY, "Retaining received Q1 effects");
    fx->particles.family = QA_GAME_Q1;
    qa_builtin_random_seed(&fx->random, 1);
    row->effects = fx;
    return true;
}
static qa_vec3 vector(const float value[3]) { return qa_v3(value[0], value[1], value[2]); }
static bool music_source(frontend_remote_q1 *row,qa_application_client_source *out,qa_error *error)
{
    if(row->frontend->source_restoring || row->frontend->capture || row->frontend->resource_inventory ||
        (row->effects && row->effects->music_retiring))
        return frontend_remote_q1_application_metadata_read(row,out,error);
    return frontend_remote_q1_application_read(row,out,error);
}
static bool music_current(void *context,const frontend_music_origin *origin)
{
    frontend_remote_q1 *row=context;qa_application_client_source source;
    return row && origin && music_source(row,&source,NULL) && source.descriptor && origin->descriptor &&
        source.descriptor->storage==origin->descriptor->storage && source.context.receiver==origin->receiver &&
        source.context.physical_seat==origin->physical_seat && row->content.catalog==origin->catalog &&
        row->content.product==origin->product && row->content.mounts==origin->files;
}
static bool music_origin(frontend_remote_q1 *row,frontend_music_origin *out,qa_error *error)
{
    qa_application_client_source source;
    if(!music_source(row,&source,error))return false;
    *out=(frontend_music_origin){.kind=FRONTEND_MUSIC_REMOTE,.physical_seat=source.context.physical_seat,
        .receiver=source.context.receiver,.descriptor=source.descriptor,.catalog=row->content.catalog,
        .product=row->content.product,.files=row->content.mounts,.context=row,.current=music_current};
    return true;
}
static bool music_track(frontend_remote_q1 *row,uint8_t track,qa_error *error)
{
    if(!row->frontend->audio)return true;
    if(!owner(row,error))return false;
    frontend_music_origin origin;
    if(!music_origin(row,&origin,error) || (!row->effects->music &&
        !frontend_received_music_create(row->frontend,&origin,&row->effects->music,error)))return false;
    char cue[4];snprintf(cue,sizeof(cue),"%u",(unsigned)track);
    return frontend_received_music_play(row->effects->music,cue,error);
}
static bool sound(frontend_remote_q1 *row, const char *name, uint32_t number,
    qa_vec3 origin, uint32_t channel, float volume, float attenuation, bool ambient,
    bool required, qa_error *error)
{
    qa_frontend *f = row->frontend;
    if (!f->audio) return true;
    if (!row->sound_bank || !name || !*name)
        return remote_q1_fail(error, QA_ERROR_FORMAT, "Received Q1 sound lacks its real precache bank");
    qa_audio_asset *asset = NULL;
    if (!qa_audio_bank_register(row->sound_bank, name, QA_AUDIO_Q1, &asset, error)) return false;
    if (!asset) return !required || remote_q1_fail(error,QA_ERROR_IO,"Unresolved received NetQuake sound");
    uint64_t actor = QA_AUDIO_NO_ACTOR;
    if (number && !ambient) {
        qa_actor_id source;
        if (!remote_q1_actor_read(row, number, &source, error) ||
            (actor = frontend_audio_actor(f, source, error)) == QA_AUDIO_NO_ACTOR) {
            qa_audio_asset_release(asset); return false;
        }
    }
    qa_audio_play play = {.sample = qa_audio_asset_sample(asset), .asset = asset,
        .resource_id = qa_resource_id(qa_audio_asset_resource(asset)), .name = name,
        .family = QA_AUDIO_Q1, .actor = actor, .owner = row->options.domain.actor_owner,
        .audience = row->options.domain.physical_seat, .origin_kind = QA_AUDIO_FIXED,
        .origin = origin, .channel = (int32_t)channel, .volume = volume,
        .attenuation = attenuation, .server_milliseconds = row->seconds * 1000,
        .has_server_time = true};
    bool ok;
    if(ambient) {
        if(play.sample->loop_start==QA_AUDIO_NO_LOOP) { qa_audio_asset_release(asset); return true; }
        if(!owner(row,error)) { qa_audio_asset_release(asset); return false; }
        frontend_remote_q1_effects *fx=row->effects;
        if(fx->ambient_count==fx->ambient_capacity) {
            size_t capacity=fx->ambient_capacity?fx->ambient_capacity*2:16;
            if(capacity>65536) capacity=65536;
            if(fx->ambient_count==capacity) { qa_audio_asset_release(asset); return remote_q1_fail(error,QA_ERROR_FORMAT,"Q1 static sound capacity exhausted"); }
            remote_ambient *values=realloc(fx->ambient,capacity*sizeof(*values));
            if(!values) { qa_audio_asset_release(asset); return remote_q1_fail(error,QA_ERROR_MEMORY,"Retaining remote Q1 ambient sound"); }
            fx->ambient=values; fx->ambient_capacity=capacity;
        }
        fx->ambient[fx->ambient_count++]=(remote_ambient){.asset=asset,.origin=origin,
            .volume=volume,.attenuation=attenuation,.identity=qa_scene_identity()};
        return true;
    } else ok=qa_audio_engine_play(f->audio, &play, (int32_t)((f->time_ns / 1000000) & INT32_MAX), error);
    qa_audio_asset_release(asset);
    return ok;
}
static void light_at(frontend_remote_q1 *row, uint32_t entity, qa_vec3 origin,
    float radius, double duration, float decay, float minimum, qa_vec3 color,double seconds)
{
    frontend_remote_q1_effects *fx = row->effects;
    size_t selected = LIGHTS;
    if (entity) for (size_t i = 0; i < LIGHTS; ++i)
        if (fx->lights[i].active && fx->lights[i].entity == entity) { selected = i; break; }
    if (selected == LIGHTS) for (size_t i = 0; i < LIGHTS; ++i)
        if (!fx->lights[i].active || fx->lights[i].die < seconds) { selected = i; break; }
    if (selected == LIGHTS) selected = 0;
    uint64_t identity=fx->lights[selected].identity;
    if(!identity) identity=qa_scene_identity();
    fx->lights[selected] = (remote_light){origin, color, seconds,
        seconds + duration, radius, decay, minimum, entity, identity, true};
}
static void light(frontend_remote_q1 *row,uint32_t entity,qa_vec3 origin,
    float radius,double duration,float decay,float minimum,qa_vec3 color)
{ light_at(row,entity,origin,radius,duration,decay,minimum,color,row->seconds); }
static bool temporary(frontend_remote_q1 *row, const qa_q1_temp *event, qa_error *error)
{
    frontend_remote_q1_effects *fx = row->effects;
    qa_vec3 origin = vector(event->origin), zero = qa_v3(0,0,0);
    const char *path = NULL;
    if (event->kind == QA_Q1_TEMP_BEAM) {
        size_t slot = BEAMS;
        for (size_t i = 0; i < BEAMS; ++i)
            if (fx->beams[i].active && fx->beams[i].entity == event->entity) { slot = i; break; }
        if (slot == BEAMS) for (size_t i = 0; i < BEAMS; ++i)
            if (!fx->beams[i].active || fx->beams[i].die < row->seconds) { slot = i; break; }
        if (slot == BEAMS) return true;
        fx->beams[slot] = (remote_beam){origin, vector(event->end), row->seconds + .2,
            event->entity, event->type, true};
        return true;
    }
    if (event->kind == QA_Q1_TEMP_COLORS) {
        if (!event->color_length) return remote_q1_fail(error, QA_ERROR_FORMAT, "Q1 explosion has an empty palette range");
        frontend_fx_q1_color_explosion(&fx->particles, &fx->random, origin, row->seconds,
            event->color_start, event->color_length);
        light(row, 0, origin, 350, .5, 300, 0, qa_v3(1,1,1));
        path = "weapons/r_exp3.wav";
    } else switch (event->type) {
    case 0: case 1:
        frontend_fx_q1_impact(&fx->particles, &fx->random, origin, zero, 0,
            event->type ? 20 : 10, row->seconds);
        if (qa_builtin_random_integer(&fx->random) % 5) path = "weapons/tink1.wav";
        else { uint32_t n = qa_builtin_random_integer(&fx->random) & 3;
            path = n == 1 ? "weapons/ric1.wav" : n == 2 ? "weapons/ric2.wav" : "weapons/ric3.wav"; }
        break;
    case 2: frontend_fx_q1_impact(&fx->particles, &fx->random, origin, zero, 0,
        qa_q1_is_qw(row->options.domain.protocol)?20*event->count:20, row->seconds); break;
    case 3: case 4:
        frontend_fx_q1_explosion(&fx->particles, &fx->random, origin, row->seconds, event->type == 4);
        if (event->type == 3) light(row, 0, origin, 350, .5, 300, 0, qa_v3(1,1,1));
        path = "weapons/r_exp3.wav"; break;
    case 7: case 8:
        frontend_fx_q1_impact(&fx->particles, &fx->random, origin, zero,
            event->type == 7 ? 20 : 226, event->type == 7 ? 30 : 20, row->seconds);
        path = event->type == 7 ? "wizard/hit.wav" : "hknight/hit.wav"; break;
    case 10: case 11: frontend_fx_q1_splash(&fx->particles, &fx->random, origin, row->seconds, event->type == 10); break;
    case 12: case 13:
        if(!qa_q1_is_qw(row->options.domain.protocol))
            return remote_q1_fail(error,QA_ERROR_FORMAT,"NQ blood effect lacks its genuine protocol form");
        frontend_fx_q1_impact(&fx->particles,&fx->random,origin,zero,event->type==12?73:225,
            event->type==12?20*event->count:50,row->seconds); break;
    default: return remote_q1_fail(error, QA_ERROR_FORMAT, "Unsupported received Q1 temporary effect");
    }
    return !path || sound(row, path, 0, origin, 0, 1, 1, false, false, error);
}
bool remote_q1_effects_service(frontend_remote_q1 *row, const qa_nq_message *message, qa_error *error)
{
    if (!row || !message || !remote_q1_mutable(row) || !remote_q1_live(row, error)) return false;
    switch (message->op) {
    case QA_NQ_CDTRACK:return music_track(row,message->data.cd.track,error);
    case QA_NQ_PAUSE:return !row->frontend->audio || frontend_music_sources_received_pause(row->frontend->music_sources,message->data.value!=0,error);
    case QA_NQ_SOUND: case QA_NQ_STATICSOUND:
        if (!message->data.sound.index || message->data.sound.index > row->sound_count)
            return remote_q1_fail(error, QA_ERROR_FORMAT, "Unknown received Q1 sound index");
        return sound(row, row->sounds[message->data.sound.index - 1], message->data.sound.entity,
            vector(message->data.sound.origin), message->data.sound.channel,
            message->data.sound.volume / 255.0f, message->data.sound.attenuation,
            message->op == QA_NQ_STATICSOUND, message->op == QA_NQ_SOUND, error);
    case QA_NQ_STOPSOUND: {
        if (!row->frontend->audio) return true;
        qa_actor_id source;
        if (!remote_q1_actor_read(row, message->data.stop_sound.entity, &source, error)) return false;
        uint64_t actor = frontend_audio_actor(row->frontend, source, error);
        if (actor == QA_AUDIO_NO_ACTOR) return false;
        qa_audio_engine_stop_channel(row->frontend->audio, actor, row->options.domain.actor_owner,
            QA_AUDIO_Q1, message->data.stop_sound.channel);
        return remote_q1_live(row, error);
    }
    case QA_NQ_PARTICLE:
        if (!owner(row,error)) return false;
        frontend_fx_q1_impact(&row->effects->particles, &row->effects->random,
            vector(message->data.particle.origin), vector(message->data.particle.direction),
            message->data.particle.color, message->data.particle.count == 255 ? 1024 : message->data.particle.count, row->seconds);
        return true;
    case QA_NQ_TEMPENTITY: return owner(row,error) && temporary(row,&message->data.temporary,error);
    case QA_NQ_BONUSFLASH:
        if (!owner(row,error)) return false;
        row->effects->bonus_until = row->seconds + .5; return true;
    case QA_NQ_STUFFTEXT: {
        if(!message->data.text) return remote_q1_fail(error,QA_ERROR_FORMAT,"Received Q1 server command has no text");
        size_t length=strlen(message->data.text);
        char *text=malloc(length+1);
        if(!text) return remote_q1_fail(error,QA_ERROR_MEMORY,"Reading received Q1 effect commands");
        memcpy(text,message->data.text,length+1);
        bool ok=true;
        for(size_t at=0;ok && at<length;) {
            size_t count=qa_command_separator(text+at,length-at,QA_CONSOLE_Q1);
            text[at+count]=0;
            qa_command_tokens tokens={0};
            ok=qa_command_tokenize(text+at,QA_CONSOLE_Q1,false,&tokens,error);
            if(ok && tokens.count) {
                const char *name=tokens.values[0];
                if((name[0]=='b' || name[0]=='B') && (name[1]=='f' || name[1]=='F') && !name[2]) {
                    ok=owner(row,error);
                    if(ok) row->effects->bonus_until=row->seconds+.5;
                }
            }
            qa_command_tokens_free(&tokens);
            at+=count+1;
        }
        free(text); return ok;
    }
    case QA_NQ_CENTERPRINT: case QA_NQ_FINALE: case QA_NQ_CUTSCENE: {
        uint32_t seat = row->options.domain.physical_seat;
        if (seat >= row->frontend->options.seats || !row->frontend->seats[seat].hud) return true;
        const qa_cvar_view *duration = qa_cvars_find(row->options.domain.cvars,"scr_centertime");
        double seconds = duration && isfinite(duration->number) ? fmax(0,fmin(86400,duration->number)) : 2;
        return qa_hud_center_print(row->frontend->seats[seat].hud,message->data.text,
            row->frontend->time_ns,(uint64_t)(seconds * 1e9),message->op == QA_NQ_CENTERPRINT,
            message->op == QA_NQ_CENTERPRINT ? 0 : UINT64_C(125000000),error);
    }
    default: return true;
    }
}
bool remote_q1_effects_audio_detach(frontend_remote_q1 *row,qa_error *error)
{
    frontend_remote_q1_effects *fx=row?row->effects:NULL;
    if(!fx) return true;
    qa_audio_engine *audio=row->frontend->audio;
    qa_audio_mixer *mixer=audio?qa_audio_engine_seat_mixer(audio,row->options.domain.physical_seat):NULL;
    for(size_t i=0;i<fx->ambient_count;++i) if(fx->ambient[i].mixer &&
        (fx->audio!=audio || fx->ambient[i].mixer!=mixer || !qa_audio_mixer_callbacks_idle(mixer)))
        return remote_q1_fail(error,QA_ERROR_ARGUMENT,"Remote Q1 static audio lost its returned physical mixer");
    for(size_t i=0;i<fx->ambient_count;++i) {
        remote_ambient *value=fx->ambient+i;
        if(value->mixer) qa_audio_mixer_remove_static(value->mixer,value->identity);
        value->mixer=NULL;
    }
    fx->audio=NULL; return true;
}
bool remote_q1_effects_clear(frontend_remote_q1 *row, qa_error *error)
{
    if (!row) return true;
    frontend_remote_q1_effects *fx = row->effects;
    if(fx && row->frontend->audio && !qa_audio_engine_round_ready(row->frontend->audio,error)) return false;
    if(fx){fx->music_retiring=true;
        bool closed=frontend_received_music_destroy(&fx->music,error);fx->music_retiring=false;
        if(!closed)return false;}
    if(!remote_q1_effects_audio_detach(row,error)) return false;
    if (row->frontend->audio && !qa_audio_engine_stop_owner(row->frontend->audio,
        row->options.domain.actor_owner,row->options.domain.physical_seat,error)) return false;
    if (fx) {
        for(size_t i=0;i<fx->ambient_count;++i) {
            remote_ambient *value=fx->ambient+i;
            qa_audio_asset_release(value->asset);
        }
        qa_scene_image_release(fx->image); free(fx->ambient); free(fx->trails); free(fx); row->effects = NULL;
    }
    remote_q1_hud_clear(row);
    return true;
}
bool remote_q1_effects_music(const frontend_remote_q1 *row,uint64_t *bus,qa_audio_music **player)
{
    if(!row || !row->effects || !player || !frontend_received_music_bus(row->effects->music,bus))return false;
    *player=frontend_received_music_player(row->effects->music);return true;
}
bool remote_q1_effects_idle(const frontend_remote_q1 *row)
{
    const frontend_remote_q1_effects *fx=row?row->effects:NULL;
    if(!fx)return true;
    if(fx->music_retiring || !frontend_received_music_idle(fx->music))return false;
    for(size_t i=0;i<fx->ambient_count;++i)if(fx->ambient[i].mixer && !qa_audio_mixer_callbacks_idle(fx->ambient[i].mixer))return false;
    return true;
}
const qa_scene_image *remote_q1_effects_particle_image(const frontend_remote_q1 *row)
{ return row && row->effects ? row->effects->image : NULL; }

static bool entities(frontend_remote_q1 *row, double seconds, qa_error *error)
{
    frontend_remote_q1_effects *fx = row->effects;
    ++fx->sample;
    for (size_t i = 0; i < row->current.count; ++i) {
        const qa_q1_entity *entity = row->current.rows + i;
        if (!entity->model || entity->model > row->model_count) continue;
        frontend_remote_q1_entity_view view = {.entity = *entity, .model = row->models[entity->model-1]};
        if (view.model[0] == '*') continue;
        remote_q1_model *model;
        if (!remote_q1_model_read(row,&view,&model,error)) return false;
        qa_vec3 point = vector(entity->origin), angles = vector(entity->angles);
        for (size_t j=0;j<row->previous.count;++j) if (row->previous.rows[j].number==entity->number) {
            const qa_q1_entity *old=row->previous.rows+j;
            if (!entity->step && fabsf(point.x-old->origin[0])<=100 && fabsf(point.y-old->origin[1])<=100 && fabsf(point.z-old->origin[2])<=100)
                point=qa_vec_add(vector(old->origin),qa_vec_scale(qa_vec_sub(point,vector(old->origin)),(float)row->fraction));
            break;
        }
        size_t at=0; while(at<fx->trail_count && fx->trails[at].entity!=entity->number) ++at;
        if(at==fx->trail_count) {
            if(at==fx->trail_capacity) {
                size_t capacity=fx->trail_capacity?fx->trail_capacity*2:32;
                if(capacity>65536) capacity=65536;
                if(at==capacity) return remote_q1_fail(error,QA_ERROR_FORMAT,"Q1 trail table exceeds received actors");
                remote_trail *values=realloc(fx->trails,capacity*sizeof(*values));
                if(!values) return remote_q1_fail(error,QA_ERROR_MEMORY,"Retaining Q1 trails");
                fx->trails=values; fx->trail_capacity=capacity;
            }
            fx->trails[fx->trail_count++]=(remote_trail){entity->number,entity->model,point,fx->sample};
        }
        remote_trail *trail=fx->trails+at; qa_vec3 start=trail->origin,delta=qa_vec_sub(point,start);
        if(trail->model!=entity->model || fabsf(delta.x)>100 || fabsf(delta.y)>100 || fabsf(delta.z)>100) start=point;
        *trail=(remote_trail){entity->number,entity->model,point,fx->sample};
        if(entity->effects&1) frontend_fx_q1_entity(&fx->particles,&fx->random,point,seconds);
        if(entity->effects&2) { qa_vec3 forward; qa_builtin_angle_vectors(angles,&forward,NULL,NULL);
            light_at(row,entity->number,qa_vec_add(qa_vec_add(point,qa_v3(0,0,16)),qa_vec_scale(forward,18)),
                200+(float)(qa_builtin_random_integer(&fx->random)&31),.1,0,32,qa_v3(1,1,1),seconds); }
        if(entity->effects&4) light_at(row,entity->number,qa_vec_add(point,qa_v3(0,0,16)),400+(float)(qa_builtin_random_integer(&fx->random)&31),.001,0,0,qa_v3(1,1,1),seconds);
        if(entity->effects&8) light_at(row,entity->number,point,200+(float)(qa_builtin_random_integer(&fx->random)&31),.001,0,0,qa_v3(1,1,1),seconds);
        const qa_product *product=qa_catalog_product(row->content.catalog,row->content.product);
        if(product && product->edition==QA_EDITION_RERELEASE) {
            if(entity->effects&16) light_at(row,entity->number,point,200+(float)(qa_builtin_random_integer(&fx->random)&31),.001,0,0,qa_v3(.25f,.25f,1),seconds);
            if(entity->effects&32) light_at(row,entity->number,point,200+(float)(qa_builtin_random_integer(&fx->random)&31),.001,0,0,qa_v3(1,.25f,.25f),seconds);
            if(entity->effects&64) light_at(row,entity->number,point,64+(float)(qa_builtin_random_integer(&fx->random)&31),
                (double)(float)(seconds+.001)-seconds,0,0,qa_v3(1,192.0f/255,120.0f/255),seconds);
        }
        uint32_t flags=model->source?(uint32_t)model->source->flags:0;
        int type=flags&4?2:flags&32?4:flags&16?3:flags&64?5:flags&1?0:flags&2?1:flags&128?6:-1;
        if(type>=0) frontend_fx_q1_trail(&fx->particles,&fx->random,start,point,(uint32_t)type,seconds);
        if(type==0) light_at(row,entity->number,point,200,.01,0,0,qa_v3(1,1,1),seconds);
    }
    size_t retained=0;
    for(size_t i=0;i<fx->trail_count;++i) if(fx->trails[i].sample==fx->sample) fx->trails[retained++]=fx->trails[i];
    fx->trail_count=retained;
    return true;
}
bool remote_q1_effects_scene(frontend_remote_q1 *row,double seconds,
    const qa_scene_light **out,size_t *count,qa_error *error)
{
    if(!row || !out || !count || !remote_q1_mutable(row) || !isfinite(seconds) || !remote_q1_live(row,error) || !owner(row,error)) return false;
    frontend_remote_q1_effects *fx=row->effects;
    if(!fx->sampled || seconds!=fx->sampled_seconds) {
        if(fx->sample==UINT64_MAX) return remote_q1_fail(error,QA_ERROR_FORMAT,"Q1 effect samples exhausted");
        fx->previous_sample=fx->sampled?fx->sampled_seconds:seconds;
        fx->sampled_seconds=seconds; fx->sampled=true;
        if(!entities(row,seconds,error)) return false;
    }
    *count=0; *out=fx->scene_lights;
    for(size_t i=0;i<LIGHTS;++i) {
        remote_light *value=fx->lights+i;
        float radius=value->radius-value->decay*(float)fmax(0,seconds-value->birth);
        if(!value->active || value->die<seconds || radius<=0) continue;
        fx->scene_lights[(*count)++]=(qa_scene_light){.origin=value->origin,.color=value->color,
            .radius=radius,.minimum=value->minimum,.additive=true,.scale=1,.family=QA_SCENE_Q1,
            .identity=value->identity,.revision=row->revision};
    }
    return true;
}
static bool beam(frontend_remote_q1 *row,const remote_beam *value,const qa_scene_view *view,
    const qa_scene_world_input *world,qa_vec3 viewer_origin,qa_error *error)
{
    const char *path=value->type==5?"progs/bolt.mdl":value->type==6?"progs/bolt2.mdl":value->type==9?"progs/bolt3.mdl":"progs/beam.mdl";
    remote_q1_model *model;
    if(!remote_q1_model_read(row,&(frontend_remote_q1_entity_view){.model=path},&model,error)) return false;
    qa_vec3 start=value->start,end=value->end;
    if(value->entity==row->view_entity) start=viewer_origin;
    qa_vec3 delta=qa_vec_sub(end,start); float distance=qa_vec_length(delta);
    qa_vec3 direction=distance!=0.0f?qa_vec_scale(delta,1/distance):qa_v3(0,0,0);
    qa_vec3 angles=qa_v3((float)(atan2(delta.z,sqrt(delta.x*delta.x+delta.y*delta.y))*180/3.141592653589793),
        (float)(atan2(delta.y,delta.x)*180/3.141592653589793),0);
    while(distance>0) {
        angles.z=(float)(qa_builtin_random_integer(&row->effects->random)%360);
        qa_vec3 axes[3]; frontend_camera_axes(angles,axes);
        qa_model_transform transform; qa_model_transform_identity(&transform);
        transform.origin[0]=start.x; transform.origin[1]=start.y; transform.origin[2]=start.z;
        for(unsigned i=0;i<3;++i) { transform.axes[i][0]=axes[i].x; transform.axes[i][1]=axes[i].y; transform.axes[i][2]=axes[i].z; }
        qa_scene_model_input input={.view=*view,.transform=transform,.previous_origin=start,.color={1,1,1,1},
            .family=QA_SCENE_Q1,.seconds=world->seconds,.source_path=path,.entity=value->entity,.identity_light=1};
        if(!frontend_legacy_model_input(row->world,world,&input,error) ||
            !remote_q1_model_lighting(row,world,&input,error) ||
            !qa_scene_model_submit(model->scene,&input,&row->frontend->frame,error)) return false;
        start=qa_vec_add(start,qa_vec_scale(direction,30)); distance-=30;
    }
    return true;
}
bool remote_q1_effects_models(frontend_remote_q1 *row,const qa_scene_view *view,
    const qa_scene_world_input *world,qa_vec3 viewer_origin,qa_error *error)
{
    if(!row || !view || !world || !remote_q1_mutable(row) || !remote_q1_live(row,error)) return false;
    frontend_remote_q1_effects *fx=row->effects;
    if(!fx) return true;
    for(size_t i=0;i<BEAMS;++i) if(fx->beams[i].active && fx->beams[i].die>=world->seconds &&
        !beam(row,fx->beams+i,view,world,viewer_origin,error)) return false;
    return remote_q1_live(row,error);
}
bool remote_q1_effects_blend(frontend_remote_q1 *row,const qa_scene_view *view,
    double seconds,qa_scene_vec4 blend,qa_error *error)
{
    if(!row || !view || !remote_q1_mutable(row) || !remote_q1_live(row,error)) return false;
    frontend_remote_q1_effects *fx=row->effects;
    float bonus=fx?(float)fmin(50,fmax(0,(fx->bonus_until-seconds)*100))/255:0;
    if(bonus>0) {
        float alpha=blend.w+(1-blend.w)*bonus;
        float weight=bonus/alpha;
        blend.x=blend.x*(1-weight)+(215.0f/255)*weight;
        blend.y=blend.y*(1-weight)+(186.0f/255)*weight;
        blend.z=blend.z*(1-weight)+(69.0f/255)*weight; blend.w=alpha;
    }
    return blend.w<=0 || qa_scene_frame_picture(&row->frontend->frame,qa_scene_white(row->images),
        view->viewport,view->viewport,(qa_scene_vec4){0,0,1,1},blend,error);
}
bool remote_q1_effects_draw(frontend_remote_q1 *row,const qa_scene_view *view,
    const qa_scene_world_input *world,qa_error *error)
{
    if(!row || !view || !world || !remote_q1_mutable(row) || !remote_q1_live(row,error)) return false;
    frontend_remote_q1_effects *fx=row->effects; if(!fx) return true;
    for(size_t i=0;i<fx->ambient_count;++i) {
        remote_ambient *value=fx->ambient+i;
        qa_audio_mixer *mixer=row->frontend->audio?qa_audio_engine_seat_mixer(row->frontend->audio,row->options.domain.physical_seat):NULL;
        if(value->mixer && (fx->audio!=row->frontend->audio || value->mixer!=mixer))
            return remote_q1_fail(error,QA_ERROR_ARGUMENT,"Q1 static mixer must detach before parent replacement");
        if(!mixer || value->mixer==mixer) continue;
        if(value->mixer) {
            if(!qa_audio_mixer_callbacks_idle(value->mixer)) return remote_q1_fail(error,QA_ERROR_ARGUMENT,"Q1 ambient mixer replacement overlaps a callback");
            qa_audio_mixer_remove_static(value->mixer,value->identity);
            value->mixer=NULL;
        }
        if(!qa_audio_mixer_static_asset(mixer,value->identity,value->asset,value->origin,
            truncf(value->volume*255),truncf(value->attenuation*64),error)) return false;
        qa_audio_static_view installed;
        if(qa_audio_mixer_static_read(mixer,value->identity,&installed)) {
            value->mixer=mixer; fx->audio=row->frontend->audio;
        }
    }
    if(!fx->image && !qa_scene_particle_image(row->images,QA_SCENE_Q1,&fx->image,error)) return false;
    qa_bytes palette;
    if(!qa_scene_resources_palette(row->images,QA_SCENE_Q1,&palette,error) || palette.size<768) return false;
    for(size_t i=fx->particles.count;i>0;--i) {
        qa_scene_q1_particle_state *p=fx->particles.values.q1+i-1;
        if(p->die<world->seconds) continue;
        uint32_t n=(p->color&255)*3;
        qa_scene_vec4 color={palette.data[n]/255.0f,palette.data[n+1]/255.0f,palette.data[n+2]/255.0f,1};
        if(!qa_scene_indexed_particle(&row->frontend->frame,view,QA_SCENE_Q1,p->origin,1,color,fx->image,error)) return false;
    }
    double elapsed=fmax(0,fx->sampled_seconds-fx->previous_sample);
    size_t retained=0;
    for(size_t i=0;i<fx->particles.count;++i) {
        qa_scene_q1_particle_state p=fx->particles.values.q1[i];
        if(p.die<world->seconds) continue;
        qa_scene_q1_particle_advance(&p,elapsed,800);
        fx->particles.values.q1[retained++]=p;
    }
    fx->particles.count=retained; fx->previous_sample=fx->sampled_seconds;
    return remote_q1_live(row,error);
}

size_t remote_q1_effects_light_count(const frontend_remote_q1 *row)
{
    if(!row || !row->effects) return 0;
    for(size_t i=LIGHTS;i>0;--i) if(row->effects->lights[i-1].identity) return i;
    return 0;
}
bool remote_q1_effects_light_at(const frontend_remote_q1 *row,size_t at,uint64_t *out)
{
    if(!row || !row->effects || !out || at>=LIGHTS || !row->effects->lights[at].identity) return false;
    *out=row->effects->lights[at].identity; return true;
}
size_t remote_q1_effects_static_count(const frontend_remote_q1 *row)
{ return row && row->effects?row->effects->ambient_count:0; }
bool remote_q1_effects_static_at(const frontend_remote_q1 *row,size_t at,uint64_t *key,
    const qa_audio_asset **asset,qa_audio_mixer **mixer)
{
    if(!row || !row->effects || at>=row->effects->ambient_count || !key || !asset || !mixer) return false;
    const remote_ambient *value=row->effects->ambient+at;
    *key=value->identity; *asset=value->asset; *mixer=value->mixer; return true;
}
