/* id Software cg_event.c and CG_PlayBufferedSounds; GPL-2.0-or-later. */
#include "events_internal.h"
#include "weapon.h"
#include "../q3/internal.h"
#include "qa/application_native_q3_wire.h"

bool q3n_events_create(const q3n_event_options *options, q3n_events **out, qa_error *error)
{
    if(!options || !out || !options->assets || !options->print || !options->center_print ||
       !options->trace || !options->point_contents || !options->mark_fragments ||
       !options->weapon_event ||
       (options->product==QA_Q3_TEAM_ARENA && !options->voice_chat) ||
       (options->product!=QA_Q3_ARENA && options->product!=QA_Q3_TEAM_ARENA))
        return q3ne_fail(error,QA_ERROR_ARGUMENT,"Native Q3 events require their genuine presentation services");
    q3n_events *o=calloc(1,sizeof(*o));
    if(!o)return q3ne_fail(error,QA_ERROR_MEMORY,"Allocating native Q3 event continuation");
    o->options=*options; o->smoke_seed=0x92; q3ne_local_reset(o); *out=o; return true;
}
bool q3n_events_create_effects(const q3n_event_options *options, q3n_events **out, qa_error *error)
{
    if (!options || !out || !options->assets || !options->trace || !options->point_contents ||
        !options->mark_fragments || options->compiled_source || options->event_replacement || options->weapon_event ||
        options->print || options->center_print || options->voice_chat ||
        (options->product != QA_Q3_ARENA && options->product != QA_Q3_TEAM_ARENA))
        return q3ne_fail(error, QA_ERROR_ARGUMENT, "Standalone Q3 effects require only their genuine world services");
    q3n_events *owner = calloc(1, sizeof(*owner));
    if (!owner) return q3ne_fail(error, QA_ERROR_MEMORY, "Allocating standalone Q3 effects continuation");
    owner->options = *options; owner->standalone_effects = true;
    owner->smoke_seed = 0x92; q3ne_local_reset(owner); *out = owner; return true;
}
bool q3n_events_create_remote(const q3n_event_options *options, q3n_events **out, qa_error *error)
{
    if(!out || *out || !options || options->compiled_source)return q3ne_fail(error,QA_ERROR_ARGUMENT,"Remote Q3 events require an empty actual owner");
    if(!q3n_events_create(options,out,error))return false;
    (*out)->remote_source=true; return true;
}
bool q3n_events_create_compiled(const q3n_event_options *options,q3n_events **out,qa_error *error)
{
    q3n_compiled_source_view view;
    if (!options || !options->compiled_source || !out || *out ||
        !q3n_compiled_source_read(options->compiled_source,&view,error) ||
        view.basis.assets!=options->assets || view.basis.product!=options->product)
        return q3ne_fail(error,QA_ERROR_ARGUMENT,"Compiled Q3 events require their actual source and assets");
    return q3n_events_create(options,out,error);
}
void q3n_events_destroy(q3n_events *o) { if(o && !o->busy)free(o); }
bool q3n_events_idle(const q3n_events *o) { return !o || !o->busy; }
const q3n_event_state *q3n_events_state(const q3n_events *o) { return o?&o->state:NULL; }
void q3n_events_clear_pickup_time(q3n_events *o) { if(o)o->state.item_pickup_time=0; }
void q3n_events_clear_killer(q3n_events *o) { if(o && !o->busy)o->state.killer_name[0]=0; }
int32_t q3n_events_rand(q3n_events *o) { o->seed=69069u*o->seed+1u; return (int32_t)(o->seed&32767u); }
float q3n_events_random(q3n_events *o) { return q3ne_div((float)q3n_events_rand(o),32767); }
float q3n_events_crandom(q3n_events *o) { return q3ne_mul(2,q3ne_add(q3n_events_random(o),-0.5f)); }
qa_vec3 q3n_events_direction(int32_t value)
{ return value>=0 && value<(int32_t)QA_BYTE_NORMAL_COUNT?qa_byte_normals[value]:qa_v3(0,0,0); }
void q3n_events_round(q3n_events *o) { if(o && !o->busy) { q3ne_local_reset(o); o->mark_count=0; } }

qa_vec3 q3ne_perpendicular(qa_vec3 v)
{
    float a[3]={fabsf(v.x),fabsf(v.y),fabsf(v.z)}; unsigned index=0;
    for(unsigned i=1;i<3;++i)if(a[i]<a[index])index=i;
    qa_vec3 temp=index==0?qa_v3(1,0,0):index==1?qa_v3(0,1,0):qa_v3(0,0,1);
    float inverse=q3ne_div(1,q3ne_dot(v,v));
    float distance=q3ne_mul(q3ne_dot(v,temp),inverse);
    return q3ne_normalize(q3ne_difference(temp,q3ne_scale(q3ne_scale(v,inverse),distance)));
}
static void multiply(const qa_vec3 a[3], const qa_vec3 b[3], qa_vec3 out[3])
{
    qa_vec3 columns[3]={{b[0].x,b[1].x,b[2].x},{b[0].y,b[1].y,b[2].y},{b[0].z,b[1].z,b[2].z}};
    for(unsigned i=0;i<3;++i)out[i]=qa_v3(q3ne_dot(a[i],columns[0]),q3ne_dot(a[i],columns[1]),q3ne_dot(a[i],columns[2]));
}
qa_vec3 q3ne_rotate(qa_vec3 forward, qa_vec3 point, float degrees)
{
    qa_vec3 radial=q3ne_perpendicular(forward), vertical=q3ne_cross(radial,forward);
    float radians=q3ne_div(q3ne_mul(degrees,3.14159274101257324219f),180);
    float c=(float)cos((double)radians), s=(float)sin((double)radians);
    qa_vec3 basis[3]={{radial.x,vertical.x,forward.x},{radial.y,vertical.y,forward.y},{radial.z,vertical.z,forward.z}};
    qa_vec3 rotation[3]={{c,s,0},{-s,c,0},{0,0,1}},inverse[3]={radial,vertical,forward},first[3],last[3];
    multiply(basis,rotation,first); multiply(first,inverse,last);
    return qa_v3(q3ne_dot(last[0],point),q3ne_dot(last[1],point),q3ne_dot(last[2],point));
}
bool q3ne_current(const q3n_frame *f, qa_error *error)
{
    const q3n_media_view *media=f && f->media?q3n_media_read(f->media):NULL;
    if (f && f->compiled) {
        if (!f->events || f->events->standalone_effects || f->events->remote_source ||
            f->events->options.compiled_source!=f->compiled->source.owner || !f->event_settings ||
            !f->presentation || !f->clients || !media || f->events->options.assets!=f->assets ||
            f->events->options.product!=q3n_frame_product(f) || media->product!=q3n_frame_product(f) ||
            !q3n_frame_current(f) || !q3n_clients_compiled_current(f->clients,&f->compiled->source,error) ||
            !q3n_media_compiled_current(f->media,&f->compiled->source,error))
            return q3ne_fail(error,QA_ERROR_ARGUMENT,"Compiled Q3 effects lost their real CLIENT cache and media");
        return true;
    }
    if (f && f->unified_effects) {
        const q3n_unified_effect_source *source=f->unified_effects;
        if (!f->events || !f->events->standalone_effects || f->events->remote_source ||
            f->remote || f->compiled || f->events->options.compiled_source || f->effects_source || f->effect_event || f->clients || f->has_local_player ||
            !f->event_settings || !f->presentation || !media || f->assets!=source->assets ||
            f->events->options.assets!=source->assets || f->events->options.product!=source->product ||
            f->time!=source->time || !q3n_media_unified_effects_current(f->media,source,error))
            return q3ne_fail(error,QA_ERROR_ARGUMENT,"Unified effects lost their real CLIENT event and resource cut");
        return true;
    }
    if (f && f->effects_source) {
        const qa_application_selected_effects *source = f->effects_source;
        bool clock = f->time == source->sample_time_ms;
        if (f->effect_event) {
            const qa_builtin_event *event = f->effect_event->event;
            clock = event && event->family == QA_GAME_Q3 && event->time_ns % UINT64_C(1000000) == 0 &&
                event->time_ns / UINT64_C(1000000) <= UINT32_MAX &&
                f->time == q3ne_word((uint32_t)(event->time_ns / UINT64_C(1000000))) &&
                f->effect_pose_current && f->effect_pose_current(f->effect_output_context);
        }
        if (!f->events || !f->events->standalone_effects || f->events->remote_source || f->remote || f->compiled || f->events->options.compiled_source || !f->event_settings || !f->presentation ||
            !media || f->events->options.assets != f->assets ||
            f->events->options.product != source->q3_product || media->product != source->q3_product ||
            f->clients || f->has_local_player || !clock ||
            !q3n_media_effects_current(f->media, f->application, source, f->effect_event, error))
            return q3ne_fail(error, QA_ERROR_ARGUMENT, "Standalone Q3 effects lost their actual producer, registry or clock");
        return true;
    }
    if(f && f->remote) {
        if(!f->events || !f->events->remote_source || f->events->standalone_effects || f->events->options.compiled_source ||
           !f->event_settings || !f->presentation || !f->clients || !media || f->effect_event ||
           f->events->options.assets!=f->assets || f->events->options.product!=q3n_frame_product(f) ||
           media->product!=q3n_frame_product(f) || q3n_media_assets(f->media)!=f->assets ||
           q3n_clients_assets(f->clients)!=f->assets ||
           !q3n_frame_current(f) || !q3n_clients_remote_current(f->clients,&f->remote->source,error) ||
           !q3n_media_remote_current(f->media,&f->remote->source,error))
            return q3ne_fail(error,QA_ERROR_ARGUMENT,"Remote Q3 effects lost their actual Network/cache presentation cut");
        return true;
    }
    if(!f || !f->events || !f->event_settings || !f->presentation || !f->clients || !f->media ||
       f->events->standalone_effects || f->events->remote_source || f->events->options.compiled_source || f->effect_event ||
       f->events->options.assets!=f->assets || f->events->options.product!=f->source.product ||
       !media || media->product!=f->source.product ||
       !qa_application_native_q3_presentation_current(f->application,&f->source))
        return q3ne_fail(error,QA_ERROR_ARGUMENT,"Native Q3 effects require the current actual GAME and presentation cut");
    return true;
}
bool q3ne_sound(const q3n_frame *f, int32_t sound, const qa_vec3 *origin, int32_t number, int32_t channel, bool local, qa_error *error)
{
    if (!q3ne_current(f, error)) return false;
    if (f->effects_source || f->unified_effects) {
        if (sound < 0 || (size_t)sound > f->assets->sound_count) return true;
        qa_audio_asset *asset = q3p_sound(f->assets, sound);
        if (!asset) return true;
        if (local || !origin || !f->effect_sound_output)
            return q3ne_fail(error, QA_ERROR_ARGUMENT, "Standalone Q3 effect sound requires its actual fixed world origin");
        return f->effect_sound_output(f->effect_output_context, f, asset, origin, channel, error) &&
            q3ne_current(f, error);
    }
    return qa_q3_presentation_sound(f->presentation,sound,origin,number,channel,local,error) && q3ne_current(f,error);
}
bool q3n_events_trace(const q3n_frame *f, qa_vec3 start, qa_vec3 end, qa_bounds bounds, int32_t skip, uint32_t mask, qa_trace_result *out, qa_error *error)
{
    return out && q3ne_current(f,error) && f->events->options.trace(f->events->options.context,f,start,end,bounds,skip,mask,out,error) && q3ne_current(f,error);
}
bool q3n_events_point_contents(const q3n_frame *f, qa_vec3 point, int32_t pass, uint32_t *out, qa_error *error)
{
    return out && q3ne_current(f,error) && f->events->options.point_contents(f->events->options.context,f,point,pass,out,error) && q3ne_current(f,error);
}
bool q3n_events_trace_number(const q3n_frame *f, const qa_trace_result *trace, int32_t *out, qa_error *error)
{
    if(!trace || !out || !q3ne_current(f,error))return false;
    if(trace->hit==QA_TRACE_HIT_NONE) { *out=1023; return true; }
    if(trace->hit==QA_TRACE_HIT_WORLD) { *out=1022; return true; }
    if(f->remote)return q3n_remote_frame_trace_number(f->remote,trace,out,error) && q3ne_current(f,error);
    if(f->compiled)return q3n_compiled_frame_trace_number(f->compiled,trace,out,error) && q3ne_current(f,error);
    for(uint32_t i=0;i<f->source.entity_count;++i) {
        qa_application_native_q3_entity actual;
        if(!qa_application_native_q3_presentation_entity(f->application,&f->source,i,&actual,error))return false;
        if(actual.present && qa_actor_id_equal(actual.binding.actor,trace->actor)) { *out=actual.binding.number; return true; }
    }
    return q3ne_fail(error,QA_ERROR_FORMAT,"Actual traced actor has no native GAME physical entity binding");
}
static bool custom(const q3n_frame *f, int32_t number, int32_t channel, const char *name, qa_error *error)
{
    int32_t sound;
    return q3n_clients_custom_sound(f->clients,number,name,&sound,error) && q3ne_current(f,error) && q3ne_sound(f,sound,NULL,number,channel,false,error);
}
static bool center(const q3n_frame *f, const char *text, qa_error *error)
{
    return f->events->options.center_print(f->events->options.context,f,text,q3n_frame_product(f)==QA_Q3_ARENA?143:144,16,error) && q3ne_current(f,error);
}
static bool pain(const q3n_frame *f, q3n_entity *cent, int32_t number, int32_t health, qa_error *error)
{
    if(q3ne_sub(f->time,cent->player.pain_time)<500)return true;
    char name[32]; snprintf(name,sizeof(name),"*pain%d_1.wav",health<25?25:health<50?50:health<75?75:100);
    if(!custom(f,number,3,name,error))return false;
    cent->player.pain_time=f->time; cent->player.pain_direction=!cent->player.pain_direction; return true;
}
static bool remote_cache(const q3n_frame *f,const q3n_entity *cent,qa_error *error)
{
    if (f->compiled) {
        if (cent==f->compiled->predicted_entity) return q3n_compiled_frame_current(f->compiled);
        q3n_compiled_entity row;
        return q3n_compiled_frame_entity(f->compiled,cent->physical,&row,error) &&
            row.presentation==cent && row.published && q3n_compiled_entity_current(&row);
    }
    if(!f->remote)return true;
    if(cent==f->remote->predicted_entity)return q3n_remote_frame_current(f->remote);
    q3n_remote_entity row;
    return q3n_remote_frame_entity(f->remote,cent->physical,&row,error) &&
        row.presentation==cent && row.published && q3n_remote_entity_current(&row);
}
bool q3n_events_pain(const q3n_frame *f, q3n_entity *cent, int32_t number, int32_t health, qa_error *error)
{
    if(!cent || !q3ne_current(f,error) || f->events->busy || !remote_cache(f,cent,error))return q3ne_fail(error,QA_ERROR_ARGUMENT,"Native Q3 pain requires its idle event owner");
    f->events->busy=true; bool ok=pain(f,cent,number,health,error); f->events->busy=false; return ok;
}
bool q3n_events_buffer(q3n_events *o, int32_t sound, qa_error *error)
{
    if(!o || sound<0)return q3ne_fail(error,QA_ERROR_ARGUMENT,"Invalid native Q3 buffered sound");
    if(!sound)return true;
    o->sound_buffer[o->sound_in]=sound; o->sound_in=(o->sound_in+1)%20;
    /* Keep the source unmodded overflow increment. Out==20 is rejected at
     * playback rather than silently changing the retained donor cursor. */
    if(o->sound_in==o->sound_out)++o->sound_out;
    return true;
}
static bool buffered(const q3n_frame *f, q3n_sound key, qa_error *error)
{ return q3n_events_buffer(f->events,q3n_media_read(f->media)->sounds[key],error); }
static bool team_sound(const q3n_frame *f, int32_t event, qa_error *error)
{
    const q3n_client_info *ci=q3n_clients_get(f->clients,f->viewing_client); int32_t team=ci?ci->team:0;
    const qa_q3_player *ps=q3n_frame_snapshot_player(f);
    switch(event) {
    case 0:case 1:return buffered(f,team==(event==0?1:2)?Q3N_S_CAPTURE_YOUR_TEAM:Q3N_S_CAPTURE_OPPONENT,error);
    case 2:case 3:return buffered(f,team==(event==2?1:2)?Q3N_S_RETURN_YOUR_TEAM:Q3N_S_RETURN_OPPONENT,error) && buffered(f,event==2?Q3N_S_BLUE_FLAG_RETURNED:Q3N_S_RED_FLAG_RETURNED,error);
    case 4:case 5: {
        if(ps->powerups[event==4?8:7] || ps->powerups[9])return true;
        if(team!=1 && team!=2)return true;
        int32_t threatened=event==4?2:1;
        if(q3n_frame_product(f)==QA_Q3_TEAM_ARENA && q3n_frame_game_type(f)==5)
            return buffered(f,team==threatened?Q3N_S_YOUR_TEAM_TOOK_FLAG:Q3N_S_ENEMY_TOOK_FLAG,error);
        return buffered(f,team==threatened?Q3N_S_ENEMY_TOOK_YOUR_FLAG:Q3N_S_YOUR_TEAM_TOOK_ENEMY_FLAG,error);
    }
    case 6:case 7:return team==(event==6?1:2)?buffered(f,Q3N_S_BASE_UNDER_ATTACK,error):true;
    case 8:return buffered(f,Q3N_S_RED_SCORED,error);
    case 9:return buffered(f,Q3N_S_BLUE_SCORED,error);
    case 10:return buffered(f,Q3N_S_RED_LEADS,error);
    case 11:return buffered(f,Q3N_S_BLUE_LEADS,error);
    case 12:return buffered(f,Q3N_S_TEAMS_TIED,error);
    case 13:return q3n_frame_product(f)==QA_Q3_TEAM_ARENA?q3ne_sound(f,q3n_media_read(f->media)->sounds[Q3N_S_KAMIKAZE_FAR],NULL,q3n_frame_snapshot_player(f)->clientNum,7,true,error):true;
    default:return true;
    }
}
static bool reached_configstring(const q3n_frame *f, uint32_t index,
    const char **text, uint64_t *revision, qa_error *error)
{
    if(f->remote || f->compiled)return q3n_frame_configstring(f,index,text,revision,error) && q3ne_current(f,error);
    qa_native_q3_wire_basis basis;
    if(!q3ne_current(f,error) ||
       !qa_native_q3_wire_reader_basis(f->reader,&basis,error))return false;
    if(basis.application!=f->application || basis.session!=f->source.session ||
       basis.source_game!=f->source.source_game || basis.source_owner!=f->source.source_owner ||
       basis.product!=f->source.product || basis.seat!=f->seat ||
       basis.physical_client!=f->viewing_client ||
       !qa_actor_id_equal(basis.actor,f->viewing_actor) ||
       basis.publication_generation!=f->source.publication_generation ||
       basis.map_revision!=f->source.map_revision)
        return q3ne_fail(error,QA_ERROR_ARGUMENT,"Native Q3 event configstrings require their actual local client reader");
    return qa_native_q3_wire_reader_configstring(f->reader,index,text,revision,error);
}
static bool player_name(const q3n_frame *f, int32_t client, char out[32], bool *present, qa_error *error)
{
    const char *text; uint64_t revision; char name[QA_Q3_GAMESTATE_CHARS];
    if(!reached_configstring(f,544u+(uint32_t)client,&text,&revision,error))return false;
    *present=text!=NULL;
    if(!*present) { out[0]=0; return true; }
    if(!qa_q3_info_value(text,"n",name,sizeof(name),error))return false;
    size_t length=strlen(name); if(length>29)length=29;
    memcpy(out,name,length); memcpy(out+length,"^7",3); return true;
}
static void place(int32_t rank, char out[64])
{
    bool tied=(rank&0x4000)!=0; rank&=~0x4000; const char *prefix=tied?"Tied for ":"";
    if(rank==1 || rank==2 || rank==3)snprintf(out,64,"%s%s",prefix,rank==1?"^41st^7":rank==2?"^12nd^7":"^33rd^7");
    else snprintf(out,64,"%s%d%s",prefix,rank,(rank==11 || rank==12 || rank==13)?"th":rank%10==1?"st":rank%10==2?"nd":rank%10==3?"rd":"th");
}
static bool obituary(const q3n_frame *f, const qa_q3_entity *s, qa_error *error)
{
    const qa_q3_player *ps=q3n_frame_snapshot_player(f);
    int32_t target=s->otherEntityNum, attacker=s->otherEntityNum2, mod=s->eventParm;
    if(target<0 || target>=64)return q3ne_fail(error,QA_ERROR_FORMAT,"CG_Obituary: target out of range");
    char victim[32], killer[32], output[256], own[80]; bool vp=false,kp=false;
    if(attacker<0 || attacker>=64)attacker=1022;
    else if(!player_name(f,attacker,killer,&kp,error))return false;
    if(!player_name(f,target,victim,&vp,error))return false;
    if(!vp)return true;
    const q3n_client_info *ci=q3n_clients_get(f->clients,(uint32_t)target);
    qa_model_gender gender=ci?ci->animations.gender:QA_MODEL_MALE;
    const char *message=NULL,*suffix="";
    switch(mod) {
    case 20:message="suicides";break; case 19:message="cratered";break;case 17:message="was squished";break;
    case 14:message="sank like a rock";break;case 15:message="melted";break;case 16:message="does a back flip into the lava";break;
    case 21:message="saw the light";break;case 22:message="was in the wrong place";break;
    }
    bool mission=q3n_frame_product(f)==QA_Q3_TEAM_ARENA;
    if(attacker==target) {
        const char *self=gender==QA_MODEL_FEMALE?"herself":gender==QA_MODEL_NEUTER?"itself":"himself";
        const char *possessive=gender==QA_MODEL_FEMALE?"her":gender==QA_MODEL_NEUTER?"its":"his";
        if(mission && mod==26)message="goes out with a bang";
        else if(mod==5) { snprintf(own,sizeof(own),"tripped on %s own grenade",possessive); message=own; }
        else if(mod==7) { snprintf(own,sizeof(own),"blew %s up",self); message=own; }
        else if(mod==9) { snprintf(own,sizeof(own),"melted %s",self); message=own; }
        else if(mod==13)message="should have used a smaller gun";
        else if(mission && mod==25) { snprintf(own,sizeof(own),"found %s prox mine",gender==QA_MODEL_NEUTER?"it's":possessive); message=own; }
        else { snprintf(own,sizeof(own),"killed %s",self); message=own; }
    }
    if(message) { snprintf(output,sizeof(output),"%s %s.\n",victim,message); f->events->options.print(f->events->options.context,output); return q3ne_current(f,error); }
    if(attacker==ps->clientNum) {
        if(q3n_frame_game_type(f)<3) {
            char rank[64]; place(q3ne_plus(ps->persistant[2],1),rank);
            snprintf(output,sizeof(output),"You fragged %s\n%s place with %d",victim,rank,ps->persistant[0]);
        } else snprintf(output,sizeof(output),"You fragged %s",victim);
        if((!mission || !(f->event_settings->single_player_active && f->event_settings->camera_orbit)) && !center(f,output,error))return false;
    }
    if(!kp) { attacker=1022; memcpy(killer,"noname",7); }
    else if(target==ps->clientNum)memcpy(f->events->state.killer_name,killer,strlen(killer)+1);
    if(attacker!=1022) {
        switch(mod) {
        case 2:message="was pummeled by";break;case 3:message="was machinegunned by";break;case 1:message="was gunned down by";break;
        case 4:message="ate";suffix="'s grenade";break;case 5:message="was shredded by";suffix="'s shrapnel";break;
        case 6:message="ate";suffix="'s rocket";break;case 7:message="almost dodged";suffix="'s rocket";break;
        case 8:case 9:message="was melted by";suffix="'s plasmagun";break;case 10:message="was railed by";break;
        case 11:message="was electrocuted by";break;case 12:case 13:message="was blasted by";suffix="'s BFG";break;
        case 18:message="tried to invade";suffix="'s personal space";break;
        default:
            if(mod==(mission?28:23))message="was caught by";
            else if(mission && mod==23)message="was nailed by";
            else if(mission && mod==24) { message="got lead poisoning from"; suffix="'s Chaingun"; }
            else if(mission && mod==25) { message="was too close to"; suffix="'s Prox Mine"; }
            else if(mission && mod==26) { message="falls to"; suffix="'s Kamikaze blast"; }
            else if(mission && mod==27)message="was juiced by";
            else message="was killed by";
        }
        snprintf(output,sizeof(output),"%s %s %s%s\n",victim,message,killer,suffix);
    } else snprintf(output,sizeof(output),"%s died.\n",victim);
    f->events->options.print(f->events->options.context,output); return q3ne_current(f,error);
}
static bool use_item(const q3n_frame *f, const qa_q3_entity *s, int32_t event, qa_error *error)
{
    int32_t holdable=event-Q3N_EV_USE_ITEM0; if(holdable<0 || holdable>6)holdable=0;
    if(s->number==q3n_frame_snapshot_player(f)->clientNum) {
        char text[160];
        if(!holdable)memcpy(text,"No item to use",15);
        else {
            size_t count; const qa_q3_item *items=qa_q3_items(q3n_frame_product(f),&count),*found=NULL;
            for(size_t i=1;i<count;++i)if(items[i].kind==QA_Q3_ITEM_HOLDABLE && items[i].tag==holdable) { found=&items[i]; break; }
            if(!found)return q3ne_fail(error,QA_ERROR_FORMAT,"BG_FindItemForHoldable has no authored product item");
            snprintf(text,sizeof(text),"Use %s",found->name);
        }
        if(!center(f,text,error))return false;
    }
    const q3n_media_view *m=q3n_media_read(f->media);
    if(holdable==1)return true;
    if(holdable==2) {
        if(s->clientNum>=0 && s->clientNum<64) {
            const q3n_client_info *ci=q3n_clients_get(f->clients,(uint32_t)s->clientNum);
            if(!ci)return q3ne_fail(error,QA_ERROR_FORMAT,"Medkit event has no actual client-info row");
            q3n_client_dynamic dynamic=ci->dynamic; dynamic.medkit_usage_time=f->time;
            bool ok=f->compiled?q3n_clients_compiled_dynamic_write(f->clients,&f->compiled->source,(uint32_t)s->clientNum,
                ci->configstring_revision,ci->media_revision,&dynamic,error):
                f->remote?q3n_clients_remote_dynamic_write(f->clients,&f->remote->source,(uint32_t)s->clientNum,
                ci->configstring_revision,ci->media_revision,&dynamic,error):
                q3n_clients_dynamic_write(f->clients,f->application,&f->source,(uint32_t)s->clientNum,
                ci->configstring_revision,ci->media_revision,&dynamic,error);
            if(!ok || !q3ne_current(f,error))return false;
        }
        return q3ne_sound(f,m->sounds[Q3N_S_MEDKIT],NULL,s->number,5,false,error);
    }
    if(q3n_frame_product(f)==QA_Q3_TEAM_ARENA) {
        if(holdable==3 || holdable==4)return true;
        if(holdable==5)return q3ne_sound(f,m->sounds[Q3N_S_USE_INVULNERABILITY],NULL,s->number,5,false,error);
    }
    return q3ne_sound(f,m->sounds[Q3N_S_USE_NOTHING],NULL,s->number,5,false,error);
}
static bool pickup(const q3n_frame *f, const qa_q3_entity *s, bool global, qa_error *error)
{
    size_t count; const qa_q3_item *items=qa_q3_items(q3n_frame_product(f),&count);
    if(s->eventParm<1 || (size_t)s->eventParm>=count)return true;
    const qa_q3_item *item=&items[s->eventParm]; const q3n_media_view *m=q3n_media_read(f->media);
    int32_t sound=0, number=s->number; bool emit=true;
    if(global) {
        number=q3n_frame_snapshot_player(f)->clientNum; emit=item->sound!=NULL;
        if(emit && (!qa_q3_register_sound(f->assets,item->sound,false,&sound,error) || !q3ne_current(f,error)))return false;
    } else if(item->kind==QA_Q3_ITEM_POWERUP || item->kind==QA_Q3_ITEM_TEAM)sound=m->sounds[Q3N_S_NORMAL_HEALTH];
    else if(item->kind==QA_Q3_ITEM_PERSISTENT) {
        emit=q3n_frame_product(f)==QA_Q3_TEAM_ARENA && item->tag>=10 && item->tag<=13;
        if(emit) { const q3n_sound types[]={Q3N_S_SCOUT,Q3N_S_GUARD,Q3N_S_DOUBLER,Q3N_S_AMMOREGEN}; sound=m->sounds[types[item->tag-10]]; }
    } else if(!qa_q3_register_sound(f->assets,item->sound,false,&sound,error) || !q3ne_current(f,error))return false;
    if(emit && !q3ne_sound(f,sound,NULL,number,0,false,error))return false;
    if(s->number==q3n_frame_snapshot_player(f)->clientNum) {
        q3n_event_state *state=&f->events->state;
        state->item_pickup=s->eventParm; state->item_pickup_time=state->item_pickup_blend_time=f->time;
        if(item->kind==QA_Q3_ITEM_WEAPON && f->event_settings->autoswitch && item->tag!=2)
            q3n_weapons_set_selected(f->weapons,item->tag,f->time);
    }
    return true;
}
static const char *event_name(int32_t event)
{
    static const char *const names[]={
        "ZEROEVENT","EV_FOOTSTEP","EV_FOOTSTEP_METAL","EV_FOOTSPLASH","EV_FOOTWADE","EV_SWIM",
        "EV_STEP","EV_STEP","EV_STEP","EV_STEP","EV_FALL_SHORT","EV_FALL_MEDIUM","EV_FALL_FAR",
        "EV_JUMP_PAD","EV_JUMP","EV_WATER_TOUCH","EV_WATER_LEAVE","EV_WATER_UNDER","EV_WATER_CLEAR",
        "EV_ITEM_PICKUP","EV_GLOBAL_ITEM_PICKUP","EV_NOAMMO","EV_CHANGE_WEAPON","EV_FIRE_WEAPON",
        "EV_USE_ITEM0","EV_USE_ITEM1","EV_USE_ITEM2","EV_USE_ITEM3","EV_USE_ITEM4","EV_USE_ITEM5",
        "EV_USE_ITEM6","EV_USE_ITEM7","EV_USE_ITEM8","EV_USE_ITEM9","EV_USE_ITEM10","EV_USE_ITEM11",
        "EV_USE_ITEM12","EV_USE_ITEM13","EV_USE_ITEM14","UNKNOWN","EV_ITEM_RESPAWN","EV_ITEM_POP",
        "EV_PLAYER_TELEPORT_IN","EV_PLAYER_TELEPORT_OUT","EV_GRENADE_BOUNCE","EV_GENERAL_SOUND",
        "EV_GLOBAL_SOUND","EV_GLOBAL_TEAM_SOUND","EV_BULLET_HIT_FLESH","EV_BULLET_HIT_WALL",
        "EV_MISSILE_HIT","EV_MISSILE_MISS","EV_MISSILE_MISS_METAL","EV_RAILTRAIL","EV_SHOTGUN",
        "UNKNOWN","EV_PAIN","EV_DEATHx","EV_DEATHx","EV_DEATHx","EV_OBITUARY","EV_POWERUP_QUAD",
        "EV_POWERUP_BATTLESUIT","EV_POWERUP_REGEN","EV_GIB_PLAYER","EV_SCOREPLUM",
        "EV_PROXIMITY_MINE_STICK","EV_PROXIMITY_MINE_TRIGGER","EV_KAMIKAZE","EV_OBELISKEXPLODE",
        "EV_OBELISKPAIN","EV_INVUL_IMPACT","EV_JUICED","EV_LIGHTNINGBOLT","EV_DEBUG_LINE",
        "EV_STOPLOOPINGSOUND","EV_TAUNT","EV_TAUNT_YES","EV_TAUNT_NO","EV_TAUNT_FOLLOWME",
        "EV_TAUNT_GETFLAG","EV_TAUNT_GUARDBASE","EV_TAUNT_PATROL"
    };
    return event>=0 && (size_t)event<sizeof(names)/sizeof(names[0])?names[event]:"UNKNOWN";
}
static bool dispatch(const q3n_frame *f, qa_q3_entity *s, q3n_entity *cent, qa_vec3 position,
    const q3n_remote_entity *actual, qa_error *error)
{
    q3n_events *o=f->events; const q3n_media_view *m=q3n_media_read(f->media);
    const qa_q3_player *ps=q3n_frame_snapshot_player(f);
    const qa_q3_player *predicted=f->remote || f->compiled?q3n_frame_predicted_player(f):ps;
    int32_t event=s->event&~0x300, client=s->clientNum<0 || s->clientNum>=64?0:s->clientNum;
    const q3n_client_info *ci=q3n_clients_get(f->clients,(uint32_t)client);
    if(f->event_settings->debug_events) {
        char text[80]; snprintf(text,sizeof(text),"ent:%3d  event:%3d ",s->number,event);
        o->options.print(o->options.context,text); if(!q3ne_current(f,error))return false;
    }
    bool unknown=event<0 || event>Q3N_EV_TAUNT_PATROL || event==Q3N_EV_USE_ITEM15 || event==Q3N_EV_BULLET ||
       (q3n_frame_product(f)==QA_Q3_ARENA && ((event>=Q3N_EV_PROX_STICK && event<=Q3N_EV_LIGHTNING) || event>=Q3N_EV_TAUNT_YES));
    if(f->event_settings->debug_events) {
        char text[80]; snprintf(text,sizeof(text),"%s\n",unknown?"UNKNOWN":event_name(event));
        o->options.print(o->options.context,text); if(!q3ne_current(f,error))return false;
    }
    if(!event)return true;
    if(unknown) {
        qa_error_set(error,QA_ERROR_FORMAT,0,"Unknown event: %d",event); return false;
    }
    if(event>=Q3N_EV_USE_ITEM0 && event<=Q3N_EV_USE_ITEM14)return use_item(f,s,event,error);
    switch(event) {
    case Q3N_EV_FOOTSTEP:case Q3N_EV_FOOTSTEP_METAL:case Q3N_EV_FOOTSPLASH:case Q3N_EV_FOOTWADE:case Q3N_EV_SWIM:
        if(f->event_settings->footsteps) {
            uint32_t kind=event==Q3N_EV_FOOTSTEP?(ci?(uint32_t)ci->animations.footsteps:0):event==Q3N_EV_FOOTSTEP_METAL?5:6;
            if(kind>=7)return q3ne_fail(error,QA_ERROR_FORMAT,"Invalid actual footstep media kind");
            return q3ne_sound(f,m->footsteps[kind][q3n_events_rand(o)&3],NULL,s->number,5,false,error);
        } break;
    case Q3N_EV_FALL_SHORT:case Q3N_EV_FALL_MEDIUM:case Q3N_EV_FALL_FAR:
        if(event==Q3N_EV_FALL_SHORT) { if(!q3ne_sound(f,m->sounds[Q3N_S_LAND],NULL,s->number,0,false,error))return false; }
        else if(!custom(f,s->number,event==Q3N_EV_FALL_MEDIUM?3:0,event==Q3N_EV_FALL_MEDIUM?"*pain100_1.wav":"*fall1.wav",error))return false;
        if(event==Q3N_EV_FALL_FAR)cent->player.pain_time=f->time;
        if(predicted && client==predicted->clientNum) { o->state.land_change=(float)(-8*(event-Q3N_EV_FALL_SHORT+1)); o->state.land_time=f->time; } break;
    case Q3N_EV_STEP4:case Q3N_EV_STEP8:case Q3N_EV_STEP12:case Q3N_EV_STEP16: {
        if(!predicted || client!=predicted->clientNum || f->event_settings->demo_playback || (ps->pmFlags&4096) || f->event_settings->no_predict || f->event_settings->synchronous_clients)break;
        int32_t delta=q3ne_sub(f->time,o->state.step_time);
        float old=delta<200?q3ne_div(q3ne_mul(o->state.step_change,(float)q3ne_sub(200,delta)),200):0;
        o->state.step_change=fminf(32,q3ne_add(old,(float)(4*(event-Q3N_EV_STEP4+1)))); o->state.step_time=f->time; break;
    }
    case Q3N_EV_JUMP_PAD: {
        q3n_smoke smoke={.origin=cent->lerp_origin,.velocity={0,0,1},.radius=32,.color={1,1,1,0.33f},.duration=1000,.start_time=f->time,.flags=1,.shader=m->graphics[Q3N_G_SMOKE_PUFF]};
        q3n_effect_smoke(f,&smoke);
        if(!q3ne_sound(f,m->sounds[Q3N_S_JUMP_PAD],&cent->lerp_origin,-1,3,false,error))return false;
        return custom(f,s->number,3,"*jump1.wav",error);
    }
    case Q3N_EV_JUMP:return custom(f,s->number,3,"*jump1.wav",error);
    case Q3N_EV_TAUNT:return custom(f,s->number,3,"*taunt.wav",error);
    case Q3N_EV_TAUNT_YES:case Q3N_EV_TAUNT_NO:case Q3N_EV_TAUNT_FOLLOW:case Q3N_EV_TAUNT_FLAG:case Q3N_EV_TAUNT_BASE:case Q3N_EV_TAUNT_PATROL: {
        const char *commands[]={"yes","no","followme","ongetflag","ondefense","onpatrol"};
        return o->options.voice_chat(o->options.context,f,1,false,s->number,53,commands[event-Q3N_EV_TAUNT_YES],error) && q3ne_current(f,error);
    }
    case Q3N_EV_WATER_TOUCH:return q3ne_sound(f,m->sounds[Q3N_S_WATER_IN],NULL,s->number,0,false,error);
    case Q3N_EV_WATER_LEAVE:return q3ne_sound(f,m->sounds[Q3N_S_WATER_OUT],NULL,s->number,0,false,error);
    case Q3N_EV_WATER_UNDER:return q3ne_sound(f,m->sounds[Q3N_S_WATER_UNDER],NULL,s->number,0,false,error);
    case Q3N_EV_WATER_CLEAR:return custom(f,s->number,0,"*gasp.wav",error);
    case Q3N_EV_ITEM_PICKUP:case Q3N_EV_GLOBAL_ITEM_PICKUP:return pickup(f,s,event==Q3N_EV_GLOBAL_ITEM_PICKUP,error);
    case Q3N_EV_NOAMMO:
        if(s->number!=ps->clientNum)break;
        return o->options.weapon_event(o->options.context,f,cent,s,event,position,error) && q3ne_current(f,error);
    case Q3N_EV_CHANGE_WEAPON:return q3ne_sound(f,m->sounds[Q3N_S_SELECT],NULL,s->number,0,false,error);
    case Q3N_EV_FIRE_WEAPON:case Q3N_EV_MISSILE_HIT:case Q3N_EV_MISSILE_MISS:case Q3N_EV_MISSILE_METAL:
    case Q3N_EV_BULLET_WALL:case Q3N_EV_BULLET_FLESH:case Q3N_EV_SHOTGUN:case Q3N_EV_RAIL:
        if(event==Q3N_EV_RAIL) {
            if(f->remote && (!actual ||
                !q3n_remote_frame_entity_weapon(actual,actual->current->weapon,7,error) ||
                !q3ne_current(f,error)))return false;
            if (f->compiled) {
                q3n_compiled_entity row;
                if (!(cent==f->compiled->predicted_entity?q3n_compiled_frame_predicted(f->compiled,&row,error):
                    q3n_compiled_frame_entity(f->compiled,cent->physical,&row,error)) ||
                    !q3n_compiled_frame_entity_weapon(&row,row.current->weapon,7,error) || !q3ne_current(f,error)) return false;
            }
            s->weapon=7;
        }
        return o->options.weapon_event(o->options.context,f,cent,s,event,position,error) && q3ne_current(f,error);
    case Q3N_EV_TELEPORT_IN:case Q3N_EV_TELEPORT_OUT:
        if(!q3ne_sound(f,m->sounds[event==Q3N_EV_TELEPORT_IN?Q3N_S_TELE_IN:Q3N_S_TELE_OUT],NULL,s->number,0,false,error))return false;
        q3n_effect_spawn(f,position); break;
    case Q3N_EV_ITEM_RESPAWN:cent->misc_time=f->time; /* fall through */
    case Q3N_EV_ITEM_POP:return q3ne_sound(f,m->sounds[Q3N_S_RESPAWN],NULL,s->number,0,false,error);
    case Q3N_EV_GRENADE_BOUNCE:return q3ne_sound(f,m->sounds[(q3n_events_rand(o)&1)?Q3N_S_GRENADE_BOUNCE1:Q3N_S_GRENADE_BOUNCE2],NULL,s->number,0,false,error);
    case Q3N_EV_PROX_STICK:return q3ne_sound(f,m->sounds[(s->eventParm&64)?Q3N_S_PROX_FLESH:(s->eventParm&4096)?Q3N_S_PROX_METAL:Q3N_S_PROX_HIT],NULL,s->number,0,false,error);
    case Q3N_EV_PROX_TRIGGER:return q3ne_sound(f,m->sounds[Q3N_S_PROX_ACTIVATE],NULL,s->number,0,false,error);
    case Q3N_EV_KAMIKAZE:case Q3N_EV_OBELISK_EXPLODE:case Q3N_EV_OBELISK_PAIN:case Q3N_EV_INVUL_IMPACT:case Q3N_EV_JUICED:
        return q3n_effect_mission(f,event,cent->lerp_origin,q3ne_array(s->angles),error);
    case Q3N_EV_LIGHTNING:return q3n_effect_mission(f,event,q3ne_array(s->origin2),q3ne_array(s->pos.base),error);
    case Q3N_EV_SCORE:q3n_effect_score(f,s->otherEntityNum,cent->lerp_origin,s->time);break;
    case Q3N_EV_GENERAL_SOUND:case Q3N_EV_GLOBAL_SOUND: {
        if(s->eventParm<0 || s->eventParm>=256)return q3ne_fail(error,QA_ERROR_FORMAT,"Unregistered game sound index");
        int32_t sound=m->game_sounds[s->eventParm];
        if(!sound) {
            const char *name; uint64_t revision, current_revision;
            uint32_t index=288u+(uint32_t)s->eventParm;
            if(!reached_configstring(f,index,&name,&revision,error) ||
               !q3n_clients_custom_sound(f->clients,s->number,name?name:"",&sound,error) ||
               !reached_configstring(f,index,&name,&current_revision,error))return false;
            if(current_revision!=revision)
                return q3ne_fail(error,QA_ERROR_ARGUMENT,"Native Q3 reached event sound changed during registration");
        }
        return q3ne_sound(f,sound,NULL,event==Q3N_EV_GENERAL_SOUND?s->number:ps->clientNum,event==Q3N_EV_GENERAL_SOUND?3:0,false,error);
    }
    case Q3N_EV_TEAM_SOUND:return team_sound(f,s->eventParm,error);
    case Q3N_EV_PAIN:return s->number==ps->clientNum?true:pain(f,cent,s->number,s->eventParm,error);
    case Q3N_EV_DEATH1:case Q3N_EV_DEATH2:case Q3N_EV_DEATH3: {
        char name[24]; snprintf(name,sizeof(name),"*death%d.wav",event-Q3N_EV_DEATH1+1); return custom(f,s->number,3,name,error);
    }
    case Q3N_EV_OBITUARY:return obituary(f,s,error);
    case Q3N_EV_QUAD:case Q3N_EV_BATTLESUIT:case Q3N_EV_REGEN:
        if(s->number==ps->clientNum) { o->state.powerup_active=event==Q3N_EV_QUAD?1:event==Q3N_EV_BATTLESUIT?2:5; o->state.powerup_time=f->time; }
        return q3ne_sound(f,m->sounds[event==Q3N_EV_QUAD?Q3N_S_QUAD:event==Q3N_EV_BATTLESUIT?Q3N_S_PROTECT:Q3N_S_REGEN],NULL,s->number,4,false,error);
    case Q3N_EV_GIB:
        if(!(s->eFlags&512) && !q3ne_sound(f,m->sounds[Q3N_S_GIB],NULL,s->number,5,false,error))return false;
        q3n_effect_gib_player(f,cent->lerp_origin);break;
    case Q3N_EV_STOP_LOOP:
        if(!qa_q3_presentation_stop_loop(f->presentation,s->number,error) || !q3ne_current(f,error))return false;
        cent->loop_stopped=true; s->loopSound=0; return true;
    case Q3N_EV_DEBUG_LINE: {
        qa_q3_ref_entity ref={.kind=QA_Q3_REF_BEAM,.origin=q3ne_array(s->pos.base),.old_origin=q3ne_array(s->origin2),.flags=64};
        q3ne_identity(ref.axis); return qa_q3_presentation_entity(f->presentation,&ref,error) && q3ne_current(f,error);
    }
    default:return q3ne_fail(error,QA_ERROR_FORMAT,"Unknown native Q3 source event");
    }
    return q3ne_current(f,error);
}
static bool admit(const q3n_frame *f, qa_error *error)
{
    if(!q3ne_current(f,error) || f->events->busy || !f->weapons || !q3n_frame_snapshot_player(f) ||
       ((f->remote || f->compiled) && !q3n_frame_predicted_player(f)) ||
       q3n_frame_snapshot_player(f)->product!=q3n_frame_product(f))
        return q3ne_fail(error,QA_ERROR_ARGUMENT,"Native Q3 events require an idle owner and actual snapshot player");
    f->events->busy=true; return true;
}
static bool present(const q3n_frame *f, qa_q3_entity *s, q3n_entity *cent, qa_vec3 position,
    const q3n_remote_entity *actual, qa_error *error)
{
    q3n_events *o=f->events;
    if(o->options.event_replacement) {
        bool suppressed=false;
        if(!o->options.event_replacement(o->options.context,f,cent,s,position,&suppressed,error) ||
           !q3ne_current(f,error))return false;
        if(suppressed)return true;
    }
    return dispatch(f,s,cent,position,actual,error);
}
bool q3n_events_player(const q3n_frame *f, const qa_q3_entity *source, q3n_entity *cent, qa_error *error)
{
    if(!source || !cent || !f || !remote_cache(f,cent,error) || !admit(f,error))return false;
    q3n_remote_entity actual; const q3n_remote_entity *row=NULL;
    bool ok=true;
    if(f->remote) {
        ok=cent==f->remote->predicted_entity?q3n_remote_frame_predicted(f->remote,&actual,error):
            q3n_remote_frame_entity(f->remote,cent->physical,&actual,error);
        if(ok) { row=&actual; ok=actual.presentation==cent && (actual.predicted || actual.published); }
    }
    qa_q3_entity scratch=*source;
    if(ok)ok=present(f,&scratch,cent,cent->lerp_origin,row,error) &&
        (!row || q3n_remote_entity_current(row));
    f->events->busy=false; return ok;
}
bool q3n_events_apply(const q3n_frame *f, const qa_application_native_q3_entity *actual, q3n_entity *cent, qa_error *error)
{
    if(!f || f->remote || f->compiled || !actual || !cent || !actual->present || !cent->valid || !qa_actor_id_equal(actual->binding.actor,cent->actor) ||
       actual->binding.number!=(int32_t)cent->physical || actual->state.number!=(int32_t)cent->physical)
        return q3ne_fail(error,QA_ERROR_ARGUMENT,"Native Q3 event has stale actor generation or physical source binding");
    if(!admit(f,error))return false;
    qa_q3_entity s=actual->state; bool ok=true;
    if(s.eType>13) {
        if(cent->previous_event)goto end;
        if(s.eFlags&16)s.number=s.otherEntityNum;
        cent->previous_event=1; cent->event_only_fired=true; s.event=q3ne_sub(s.eType,13);
    } else {
        if(s.event==cent->previous_event)goto end;
        cent->previous_event=s.event;
        if(!(s.event&~0x300))goto end;
    }
    if(!q3n_trajectory(&s.pos,f->source.source_time_ms,&cent->lerp_origin,error)) { ok=false; goto end; }
    qa_vec3 sound_origin=cent->lerp_origin;
    if(s.solid==0xffffff) {
        const q3n_media_view *m=q3n_media_read(f->media);
        if(s.modelindex<0 || (size_t)s.modelindex>=m->inline_count) { ok=q3ne_fail(error,QA_ERROR_FORMAT,"Event sound has no actual inline midpoint"); goto end; }
        sound_origin=q3ne_sum(sound_origin,m->inline_models[s.modelindex].midpoint);
    }
    ok=qa_q3_presentation_sound_position(f->presentation,s.number,sound_origin,error) && q3ne_current(f,error) && present(f,&s,cent,cent->lerp_origin,NULL,error);
end:
    f->events->busy=false; return ok;
}
bool q3n_events_apply_remote(const q3n_frame *f,const q3n_remote_entity *actual,
    const qa_q3_entity *source,qa_vec3 position,qa_error *error)
{
    if(!f || !f->remote || !actual || actual->frame!=f->remote || !actual->published ||
       actual->predicted || f->remote->snapshots.stage!=Q3N_REMOTE_SNAPSHOT_CALLBACK ||
       !actual->presentation || !source || !qa_vec_finite(position) ||
       !q3n_remote_entity_current(actual))
        return q3ne_fail(error,QA_ERROR_ARGUMENT,"Remote Q3 event lacks its actual deduplicated cache callback");
    const qa_q3_entity *raw=actual->current;
    int32_t number=raw->eType>13 && (raw->eFlags&16)?raw->otherEntityNum:raw->number;
    int32_t event=raw->eType>13?q3ne_sub(raw->eType,13):raw->event;
    if(source->number!=number || source->event!=event ||
       actual->presentation->previous_event!=(raw->eType>13?1:raw->event))
        return q3ne_fail(error,QA_ERROR_ARGUMENT,"Remote Q3 event differs from its actual deduplicated source ES");
    if(!admit(f,error))return false;
    qa_q3_entity scratch=*source;
    qa_vec3 sound_origin=position; bool ok=true;
    if(scratch.solid==0xffffff) {
        const q3n_media_view *m=q3n_media_read(f->media);
        if(scratch.modelindex<0 || (size_t)scratch.modelindex>=m->inline_count)
            ok=q3ne_fail(error,QA_ERROR_FORMAT,"Remote event sound has no actual inline midpoint");
        else sound_origin=q3ne_sum(sound_origin,m->inline_models[scratch.modelindex].midpoint);
    }
    if(ok)ok=qa_q3_presentation_sound_position(f->presentation,scratch.number,sound_origin,error) &&
        q3ne_current(f,error) && q3n_remote_entity_current(actual) &&
        present(f,&scratch,actual->presentation,position,actual,error) && q3n_remote_entity_current(actual);
    f->events->busy=false; return ok;
}
bool q3n_events_apply_compiled(const q3n_frame *f,const q3n_compiled_entity *actual,
    const qa_q3_entity *source,qa_vec3 position,qa_error *error)
{
    if (!f || !f->compiled || !actual || actual->frame!=f->compiled || !actual->published ||
        actual->predicted || f->compiled->stage!=Q3N_COMPILED_SNAPSHOT_CALLBACK ||
        !source || !qa_vec_finite(position) || !q3n_compiled_entity_current(actual))
        return q3ne_fail(error,QA_ERROR_ARGUMENT,"Compiled event requires its actual deduplicated snapshot callback");
    const qa_q3_entity *raw=actual->current;
    int32_t number=raw->eType>13 && (raw->eFlags&16)?raw->otherEntityNum:raw->number;
    int32_t event=raw->eType>13?q3ne_sub(raw->eType,13):raw->event;
    if (source->number!=number || source->event!=event ||
        actual->presentation->previous_event!=(raw->eType>13?1:raw->event))
        return q3ne_fail(error,QA_ERROR_ARGUMENT,"Compiled event differs from its actual checked source ES");
    if (!admit(f,error)) return false;
    qa_q3_entity scratch=*source; qa_vec3 sound_origin=position; bool ok=true;
    if (scratch.solid==0xffffff) {
        const q3n_media_view *m=q3n_media_read(f->media);
        if (scratch.modelindex<0 || (size_t)scratch.modelindex>=m->inline_count)
            ok=q3ne_fail(error,QA_ERROR_FORMAT,"Compiled event has no actual inline sound midpoint");
        else sound_origin=q3ne_sum(sound_origin,m->inline_models[scratch.modelindex].midpoint);
    }
    if (ok) ok=qa_q3_presentation_sound_position(f->presentation,scratch.number,sound_origin,error) &&
        q3n_compiled_entity_current(actual) && q3ne_current(f,error) &&
        present(f,&scratch,actual->presentation,position,NULL,error) && q3n_compiled_entity_current(actual);
    f->events->busy=false; return ok;
}
bool q3n_events_finish(const q3n_frame *f, qa_error *error)
{
    if(!admit(f,error))return false;
    q3n_events *o=f->events;
    bool ok=true;
    if(ok && o->sound_time<f->time && o->sound_out!=o->sound_in) {
        if(o->sound_out<0 || o->sound_out>=20)ok=q3ne_fail(error,QA_ERROR_FORMAT,"CG_PlayBufferedSounds: sound buffer index outside 0..19");
        else {
            int32_t sound=o->sound_buffer[o->sound_out];
            if(sound) {
                ok=q3ne_sound(f,sound,NULL,q3n_frame_snapshot_player(f)->clientNum,7,true,error);
                if(ok) { o->sound_buffer[o->sound_out]=0; o->sound_out=(o->sound_out+1)%20; o->sound_time=q3ne_plus(f->time,750); }
            }
        }
    }
    o->busy=false; return ok;
}
