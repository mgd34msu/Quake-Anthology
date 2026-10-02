#include "remote_q2_effects_private.h"
#include "qa/material.h"
#include "../../gameplay/q2/monsters/muzzle_data.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

const char *const q2fx_model_paths[Q2FX_MODEL_COUNT] = {
    "models/objects/explode/tris.md2", "models/objects/smoke/tris.md2",
    "models/objects/flash/tris.md2", "models/monsters/parasite/segment/tris.md2",
    "models/ctf/segment/tris.md2", "models/objects/r_explode/tris.md2",
    "sprites/s_bfg2.sp2", "models/proj/lightning/tris.md2",
    "models/proj/beam/tris.md2", "models/objects/r_explode2/tris.md2",
    "models/weapons/v_machn/flash/tris.md2", "models/weapons/v_shotg2/flash/tris.md2",
    "models/weapons/v_shotg/flash/tris.md2", "models/weapons/v_rocket/flash/tris.md2",
    "models/weapons/v_rail/flash/tris.md2", "models/weapons/v_launch/flash/tris.md2",
    "models/weapons/v_etf_rifle/flash/tris.md2", "models/weapons/v_dist/flash/tris.md2",
    "models/weapons/v_boomer/flash/tris.md2", "models/weapons/v_blast/flash/tris.md2",
    "models/weapons/v_bfg/flash/tris.md2", "models/weapons/v_beamer/flash/tris.md2"
};
bool q2fx_fail(qa_error *error, qa_status code, const char *message)
{ if (error) { error->code = code; snprintf(error->message, sizeof(error->message), "%s", message); } return false; }
static int color_hex(char c)
{
    if (c>='0' && c<='9') return c-'0';
    if (c>='a' && c<='f') return c-'a'+10;
    if (c>='A' && c<='F') return c-'A'+10;
    return -1;
}
bool frontend_remote_q2_effects_color(const char *text, uint32_t *out)
{
    if (!text || !*text || !out) return false;
    if (*text=='#') {
        size_t length=strlen(++text);
        if (length!=3 && length!=6 && length!=8) return false;
        uint32_t lanes[4]={0,0,0,255};
        for (size_t i=0;i<(length==8?4:3);++i) {
            int high=color_hex(text[length==3?i:i*2]);
            int low=length==3?high:color_hex(text[i*2+1]);
            if (high<0 || low<0) return false;
            lanes[i]=(uint32_t)(high*16+low);
        }
        *out=lanes[0]|lanes[1]<<8|lanes[2]<<16|lanes[3]<<24;
        return true;
    }
    static const char *const names[] = {"black","red","green","yellow","blue","cyan","magenta","white"};
    static const uint32_t values[] = {0xff000000,0xff0000ff,0xff00ff00,0xff00ffff,0xffff0000,0xffffff00,0xffff00ff,0xffffffff};
    for (size_t i=0;i<8;++i) if (!strcmp(text,names[i])) { *out=values[i]; return true; }
    uint32_t index=0;
    for (const char *p=text;*p;++p) {
        if (*p<'0' || *p>'9') return false;
        index=index*10+(uint32_t)(*p-'0');
        if (index>=8) return false;
    }
    *out=values[index]; return true;
}
bool q2fx_source_valid(const frontend_remote_q2_effects_source *s)
{
    const qa_scene_image *(*start)(void *, const char *, qa_error *) = NULL;
    void *context = NULL;
    return s && s->identity && s->content_generation && s->map && s->files &&
        (s->profile==FRONTEND_REMOTE_Q2_EFFECTS_CLASSIC || s->profile==FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE) &&
        s->images && s->materials && s->world && s->white && s->current && s->actor && s->model && s->sound && s->controls && s->render_clock &&
        (!s->video_context || s->video_frame) &&
        qa_material_library_video_start_read(s->materials,&start,&context) && (!start || s->video_frame) &&
        (s->profile!=FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE || (s->controls && s->viewer && s->frame_milliseconds && s->render_clock));
}
bool q2fx_source_current(const frontend_remote_q2_effects *owner, qa_error *error)
{ return owner && q2fx_source_valid(&owner->source) && owner->source.current(owner->source.context, &owner->source, error); }
bool frontend_remote_q2_effects_idle(const frontend_remote_q2_effects *owner)
{ return !owner || (!owner->busy && !owner->pending); }
const qa_scene_image *frontend_remote_q2_effects_particle_image(const frontend_remote_q2_effects *owner)
{ return owner ? owner->particle_image : NULL; }
bool frontend_remote_q2_effects_current(const frontend_remote_q2_effects *owner,
    const frontend_remote_q2_effects_source *s, qa_error *error)
{
    if (!owner || !s) return false;
    const frontend_remote_q2_effects_source *a = &owner->source;
    return a->session == s->session && a->identity == s->identity && a->content_generation == s->content_generation && a->profile == s->profile &&
        a->protocol.kind == s->protocol.kind && a->protocol.revision == s->protocol.revision && a->protocol.flags == s->protocol.flags &&
        a->map == s->map && a->files == s->files && a->images == s->images && a->materials == s->materials && a->world == s->world &&
        a->white == s->white && a->video_frame == s->video_frame && a->video_context == s->video_context &&
        a->context == s->context && a->current == s->current && a->actor == s->actor &&
        a->actor_pose == s->actor_pose && a->actor_live == s->actor_live && a->viewer == s->viewer && a->model == s->model && a->sound == s->sound &&
        a->hit_marker == s->hit_marker && a->controls == s->controls && a->frame_milliseconds == s->frame_milliseconds &&
        a->render_clock == s->render_clock &&
        a->footstep == s->footstep && a->trace == s->trace && q2fx_source_current(owner, error);
}
bool q2fx_model_admit(frontend_remote_q2_effects *owner, q2fx_model model, qa_error *error)
{
    if (owner->model_admitted[model]) return true;
    qa_scene_model *scene = NULL;
    if (!owner->source.model(owner->source.context, q2fx_model_paths[model], true, &scene, error) ||
        !q2fx_source_current(owner, error)) return false;
    owner->models[model] = scene; owner->model_admitted[model] = true; return true;
}
bool frontend_remote_q2_effects_create(const frontend_remote_q2_effects_source *source,
    frontend_remote_q2_effects **out, qa_error *error)
{
    if (!out || *out || !q2fx_source_valid(source)) return q2fx_fail(error, QA_ERROR_ARGUMENT, "Q2 effects require their actual private CLIENT resources");
    frontend_remote_q2_effects *owner = calloc(1, sizeof(*owner));
    if (!owner) return q2fx_fail(error, QA_ERROR_MEMORY, "Allocating remote Q2 effect pools");
    owner->source = *source; owner->particles.family = QA_GAME_Q2; qa_builtin_random_seed(&owner->random, 1);
    if (source->profile==FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE)
        for (size_t i=0;i<QA_BYTE_NORMAL_COUNT;++i) {
            float x=(float)(qa_builtin_random_integer(&owner->random)&255)*.01f;
            float y=(float)(qa_builtin_random_integer(&owner->random)&255)*.01f;
            float z=(float)(qa_builtin_random_integer(&owner->random)&255)*.01f;
            owner->particles.angular[i]=qa_v3(x,y,z);
        }
    *out = owner;
    owner->sampled_lights=calloc(Q2FX_LIGHT_CAPACITY,sizeof(*owner->sampled_lights));
    if (!owner->sampled_lights) return q2fx_fail(error,QA_ERROR_MEMORY,"Allocating actual Q2 sampled light rows");
    owner->light_capacity=Q2FX_LIGHT_CAPACITY;
    qa_scene_image *image = NULL; qa_bytes palette;
    if (!q2fx_source_current(owner, error) || !qa_scene_particle_image(source->images, QA_SCENE_Q2, &image, error)) return false;
    owner->particle_image = image;
    if (!qa_scene_resources_palette(source->images, QA_SCENE_Q2, &palette, error)) return false;
    size_t models=source->profile==FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE?Q2FX_MODEL_COUNT:Q2FX_MUZZLE_MACHINE;
    for (size_t i = 0; i < models; ++i)
        if (!q2fx_model_admit(owner, (q2fx_model)i, error)) return false;
    return q2fx_source_current(owner, error);
}
bool frontend_remote_q2_effects_destroy(frontend_remote_q2_effects **slot, qa_error *error)
{
    if (!slot) return false;
    frontend_remote_q2_effects *owner = *slot;
    if (!owner) return true;
    if (!frontend_remote_q2_effects_idle(owner)) return q2fx_fail(error, QA_ERROR_ARGUMENT, "Q2 effects still own an active source callback or policy");
    qa_scene_image_release(owner->particle_image); free(owner->trails); free(owner->draws);
    free(owner->sampled_lights); free(owner->source_beams); free(owner->source_lights); free(owner->flashlights);
    free(owner); *slot = NULL; return true;
}
static uint32_t random_word(frontend_remote_q2_effects *o) { return qa_builtin_random_integer(&o->random); }
static double random_unit(frontend_remote_q2_effects *o) { return (double)(random_word(o) & 32767) / 32767; }
static float random_signed(frontend_remote_q2_effects *o) { return (float)(2 * random_unit(o) - 1); }
qa_vec3 q2fx_random_direction(frontend_remote_q2_effects *o)
{
    if (o->source.profile==FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE) {
        float x,y,square;
        do { x=random_signed(o); y=random_signed(o); square=x*x+y*y; } while (square>1);
        float scale=2*sqrtf(1-square);
        return qa_v3(x*scale,y*scale,-1+2*square);
    }
    float x = random_signed(o), y = random_signed(o), z = random_signed(o);
    return qa_vec_normalize(qa_v3(x, y, z));
}
static void axes(qa_vec3 a, qa_vec3 out[3])
{
    float p = a.x * .017453292519943295f, y = a.y * .017453292519943295f, r = a.z * .017453292519943295f;
    float sp = sinf(p), cp = cosf(p), sy = sinf(y), cy = cosf(y), sr = sinf(r), cr = cosf(r);
    out[0] = qa_v3(cp * cy, cp * sy, -sp);
    out[1] = qa_v3(sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, sr * cp);
    out[2] = qa_v3(cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp);
}
static qa_vec3 direction_angles(qa_vec3 d)
{
    float yaw = d.x != 0 ? atan2f(d.y, d.x) * 57.29577951308232f : d.y > 0 ? 90 : d.y < 0 ? 270 : 0;
    return qa_v3(acosf(fminf(1, fmaxf(-1, d.z))) * 57.29577951308232f, yaw, 0);
}
static bool sound(frontend_remote_q2_effects *o, const char *path, qa_vec3 origin,
    qa_actor_id actor, double time, int32_t channel, float volume, float attenuation, double delay, qa_error *e)
{ return o->source.sound(o->source.context, path, origin, actor, time, channel, volume, attenuation, delay, e) && q2fx_source_current(o, e); }
static void light(frontend_remote_q2_effects *o, qa_actor_id actor, qa_vec3 origin,
    double time, float radius, double duration, qa_vec3 color, float decay, float minimum)
{
    q2fx_light *row = NULL;
    if (actor.registry) for (size_t i = 0; i < Q2FX_POOL; ++i)
        if (o->lights[i].active && qa_actor_id_equal(o->lights[i].actor, actor)) { row = &o->lights[i]; break; }
    if (!row) for (size_t i = 0; i < Q2FX_POOL; ++i)
        if (!o->lights[i].active || o->lights[i].die < time) { row = &o->lights[i]; break; }
    if (!row) row = &o->lights[0];
    *row = (q2fx_light){true, actor, origin, color, time, time + duration, radius, decay, minimum};
}
static q2fx_explosion *explosion(frontend_remote_q2_effects *o, uint8_t kind, q2fx_model model,
    qa_vec3 origin, double start, int32_t frames, int32_t base, uint32_t flags, int32_t skin,
    float radius, qa_vec3 color, qa_vec3 angles, float scale)
{
    q2fx_explosion *row = NULL;
    for (size_t i = 0; i < Q2FX_POOL; ++i) if (!o->explosions[i].active) { row = &o->explosions[i]; break; }
    if (!row) { row = &o->explosions[0]; for (size_t i = 1; i < Q2FX_POOL; ++i) if (o->explosions[i].start < row->start) row = &o->explosions[i]; }
    *row = (q2fx_explosion){.active = true, .kind = kind, .model = (uint8_t)model, .frames = frames,
        .base = base, .skin = skin, .flags = flags, .origin = origin, .angles = angles,
        .light_color = color, .start = start, .light = radius, .scale = scale}; return row;
}
static void smoke_flash(frontend_remote_q2_effects *o, qa_vec3 origin, double server, double interval)
{
    explosion(o, 1, Q2FX_SMOKE, origin, server - interval, 4, 0, 32, 0, 0, qa_v3(0,0,0), qa_v3(0,0,0), 1);
    explosion(o, 2, Q2FX_FLASH, origin, server - interval, 2, 0, 8, 0, 0, qa_v3(0,0,0), qa_v3(0,0,0), 1);
}
static const qa_q2_temp_field *field(const qa_q2_temp_entity *t, qa_q2_temp_field_name name)
{ for (size_t i = 0; i < t->field_count; ++i) if (t->fields[i].name == name) return &t->fields[i]; return NULL; }
static bool vector_field(const qa_q2_temp_entity *t, qa_q2_temp_field_name name, qa_vec3 *out, qa_error *e)
{
    const qa_q2_temp_field *f = field(t, name);
    if (!f || f->kind != QA_Q2_TEMP_VECTOR) return q2fx_fail(e, QA_ERROR_FORMAT, "Q2 effect lacks its actual vector operand");
    *out = qa_v3(f->value.vector[0], f->value.vector[1], f->value.vector[2]); return qa_vec_finite(*out);
}
static bool integer_field(const qa_q2_temp_entity *t, qa_q2_temp_field_name name, int32_t *out, qa_error *e)
{
    const qa_q2_temp_field *f = field(t, name);
    if (!f || f->kind != QA_Q2_TEMP_INTEGER) return q2fx_fail(e, QA_ERROR_FORMAT, "Q2 effect lacks its actual integer operand");
    *out = f->value.integer; return true;
}
static bool field_actor(frontend_remote_q2_effects *o, const qa_q2_temp_entity *t,
    qa_q2_temp_field_name name, const qa_actor_id actors[7], qa_actor_id *out, qa_error *e)
{
    const qa_q2_temp_field *f = field(t, name);
    if (!f || f->kind != QA_Q2_TEMP_INTEGER || f->value.integer <= 0)
        return q2fx_fail(e, QA_ERROR_FORMAT, "Q2 beam lacks its actual received entity");
    if (actors) { *out = actors[(size_t)(f - t->fields)]; return out->registry && out->generation; }
    frontend_remote_q2_effects_pose pose;
    if (!o->source.actor(o->source.context, (uint32_t)f->value.integer, &pose, e) || !q2fx_source_current(o, e)) return false;
    *out = pose.actor; return out->registry && out->generation;
}
static q2fx_beam *beam(frontend_remote_q2_effects *o, qa_actor_id actor, qa_actor_id destination,
    qa_vec3 start, qa_vec3 end, qa_vec3 offset, double time, q2fx_model model, bool player, bool monster)
{
    q2fx_beam *pool = player ? o->player_beams : o->beams, *row = NULL;
    for (size_t i = 0; i < Q2FX_POOL; ++i)
        if (pool[i].active && qa_actor_id_equal(pool[i].actor, actor) &&
            (player || (o->source.profile==FRONTEND_REMOTE_Q2_EFFECTS_CLASSIC && model!=Q2FX_LIGHTNING) ||
                qa_actor_id_equal(pool[i].destination, destination))) { row = &pool[i]; break; }
    double duration = player && (o->source.profile==FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE || !row) ? 100 : 200;
    if (!row) for (size_t i = 0; i < Q2FX_POOL; ++i)
        if (!pool[i].active || pool[i].die < time) { row = &pool[i]; break; }
    if (row) {
        double sound_until=row->sound_until;
        *row = (q2fx_beam){.active=true,.player=player,.monster=monster,.model=(uint8_t)model,
            .actor=actor,.destination=destination,.start=start,.end=end,.offset=offset,
            .die=time+duration,.sound_until=sound_until};
    }
    return row;
}
static void laser(frontend_remote_q2_effects *o, qa_vec3 start, qa_vec3 end, double time)
{
    size_t capacity=o->source.profile==FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE?Q2FX_LASER_CAPACITY:Q2FX_POOL;
    for (size_t i = 0; i < capacity; ++i) if (!o->lasers[i].active ||
        (o->source.profile==FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE?o->lasers[i].die<=time:o->lasers[i].die<time)) {
        uint32_t color = (UINT32_C(0xd0d1d2d3) >> ((random_word(o) % 4) * 8)) & 255;
        o->lasers[i] = (q2fx_laser){.active=true,.start=start,.end=end,.born=time,.die=time+100,.color=color,.width=4}; return;
    }
}
static void radial_particles(frontend_remote_q2_effects *o, qa_vec3 origin, double time,
    int32_t count, float radius, float speed, uint32_t palette, bool instant)
{
    static const uint32_t colors[4] = {16,104,168,144};
    for (int32_t i = 0; i < count && o->particles.count < FRONTEND_FX_PARTICLE_CAPACITY; ++i) {
        uint32_t index = palette == 0xe0 ? 0 : random_word(o) & 3;
        frontend_fx_q2_particle p = {.spawn_milliseconds = time, .alpha = 1,
            .color = palette == 0 ? colors[index] : palette + (palette == 110 ? index * 2 : index)};
        qa_vec3 dir = q2fx_random_direction(o); p.origin = qa_vec_add(origin, qa_vec_scale(dir, radius));
        p.velocity = qa_vec_scale(dir, speed);
        p.alpha_velocity = instant ? -10000 : (float)((palette == 208 ? -1 : -.8) / (.5 + random_unit(o) * .3));
        o->particles.values.q2[o->particles.count++] = p;
    }
}
static void special_trail(frontend_remote_q2_effects *o, qa_vec3 start, qa_vec3 end, double time, bool bubbles)
{
    qa_vec3 delta = qa_vec_sub(end, start), direction = qa_vec_normalize(delta); double length = qa_vec_length(delta);
    for (double distance = 0; distance < length && o->particles.count < FRONTEND_FX_PARTICLE_CAPACITY; distance += bubbles ? 8 : 3) {
        frontend_fx_q2_particle p = {.spawn_milliseconds = time, .alpha = 1};
        p.origin = qa_vec_add(start, qa_vec_scale(direction, (float)distance));
        if (!bubbles) { p.color = 0x74 + (random_word(o) & 7); p.alpha_velocity = -.1f; }
        else {
            p.alpha_velocity = (float)(-1 / (1 + random_unit(o) * .1)); p.color = 4 + (random_word(o) & 7);
            p.origin.x += random_signed(o) * 2; p.velocity.x = random_signed(o) * 10;
            p.origin.y += random_signed(o) * 2; p.velocity.y = random_signed(o) * 10;
            p.origin.z += random_signed(o) * 2 - 4; p.velocity.z = random_signed(o) * 10 + 20;
        }
        o->particles.values.q2[o->particles.count++] = p;
    }
}
static void bfg_explosion(frontend_remote_q2_effects *o, qa_vec3 origin, double server, double interval)
{ explosion(o, 3, Q2FX_BFG, origin, server - interval, 4, 0, 8 | 32, 0, 350, qa_v3(0,1,0), qa_v3(0,0,0), 1); }
static void rail(frontend_remote_q2_effects *o, qa_vec3 start, qa_vec3 end, double time,
    bool modern, const frontend_remote_q2_effects_controls *controls)
{
    if (!controls->rail_type && !modern) { frontend_fx_q2_rail(&o->particles,&o->random,start,end,time*.001); return; }
    if (controls->rail_width>0) for (size_t i=0;i<Q2FX_LASER_CAPACITY;++i) {
        q2fx_laser *row=&o->lasers[i];
        if (row->active && row->die>time) continue;
        double duration=(int32_t)((float)controls->rail_seconds*1000);
        *row=(q2fx_laser){.active=true,.start=start,.end=end,.born=time,.die=time+duration,
            .color=UINT32_MAX,.rgba=controls->rail_core_rgba,.width=(float)controls->rail_width};
        break;
    }
    if (controls->rail_type>1) frontend_fx_q2_rail_spiral(&o->particles,&o->random,start,end,time*.001,
        controls->rail_seconds,controls->rail_radius,controls->rail_spiral_rgba);
}
static bool temporary(frontend_remote_q2_effects *o, const qa_q2_temp_entity *t,
    const qa_actor_id actors[7], double time, double server, qa_error *e)
{
    qa_vec3 pos = {0}, end = {0}, dir = {0}, offset = {0}; int32_t count = 0, color = 0, id = 0, magnitude = 0, duration = 0;
    qa_actor_id actor = {0}, destination = {0}; double seconds = time * .001;
    frontend_fx_particles *p = &o->particles; qa_builtin_random *r = &o->random;
    frontend_remote_q2_effects_controls controls;
    if (!q2fx_controls(o,&controls,e)) return false;
    double interval;
    if (!q2fx_frame_milliseconds(o,&interval,e)) return false;
    bool rerelease=o->source.profile==FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE;
    if (t->type != QA_Q2_TE_POWER_SPLASH && t->type != QA_Q2_TE_Q2PRO_DAMAGE_DEALT &&
        !vector_field(t, QA_Q2_TEMP_POSITION1, &pos, e)) return false;
    if (field(t, QA_Q2_TEMP_POSITION2) && !vector_field(t, QA_Q2_TEMP_POSITION2, &end, e)) return false;
    if (field(t, QA_Q2_TEMP_DIRECTION) && !vector_field(t, QA_Q2_TEMP_DIRECTION, &dir, e)) return false;
    if (field(t, QA_Q2_TEMP_COUNT) && !integer_field(t, QA_Q2_TEMP_COUNT, &count, e)) return false;
    if (field(t, QA_Q2_TEMP_COLOR) && !integer_field(t, QA_Q2_TEMP_COLOR, &color, e)) return false;
    switch (t->type) {
    case QA_Q2_TE_BLOOD: case QA_Q2_TE_MOREBLOOD:
        if (!(controls.disable_particles&16))
            frontend_fx_q2_impact_particles(p,r,pos,dir,0xe8,t->type == QA_Q2_TE_BLOOD ? 60 : 250,seconds,FRONTEND_FX_Q2_NORMAL);
        break;
    case QA_Q2_TE_GUNSHOT: case QA_Q2_TE_SHOTGUN: case QA_Q2_TE_SPARKS: case QA_Q2_TE_BULLET_SPARKS:
        frontend_fx_q2_impact_particles(p,r,pos,dir,t->type == QA_Q2_TE_GUNSHOT || t->type == QA_Q2_TE_SHOTGUN ? 0 : 0xe0,
            t->type == QA_Q2_TE_GUNSHOT ? 40 : t->type == QA_Q2_TE_SHOTGUN ? 20 : 6,seconds,FRONTEND_FX_Q2_NORMAL);
        if (t->type != QA_Q2_TE_SPARKS) smoke_flash(o,pos,server,interval);
        if (t->type == QA_Q2_TE_GUNSHOT || t->type == QA_Q2_TE_BULLET_SPARKS) {
            uint32_t n = random_word(o) & 15;
            if (n >= 1 && n <= 3) { const char *paths[] = {"world/ric1.wav","world/ric2.wav","world/ric3.wav"}; return sound(o,paths[n-1],pos,actor,time,0,1,1,0,e); }
        } break;
    case QA_Q2_TE_SCREEN_SPARKS: case QA_Q2_TE_SHIELD_SPARKS: case QA_Q2_TE_ELECTRIC_SPARKS:
        frontend_fx_q2_impact_particles(p,r,pos,dir,t->type == QA_Q2_TE_SCREEN_SPARKS ? 0xd0 : t->type == QA_Q2_TE_SHIELD_SPARKS ? 0xb0 : 0x75,40,seconds,FRONTEND_FX_Q2_NORMAL);
        return sound(o,"weapons/lashit.wav",pos,actor,time,0,1,1,0,e);
    case QA_Q2_TE_SPLASH: {
        const uint32_t colors[] = {0,0xe0,0xb0,0x50,0xd0,0xe0,0xe8};
        if (rerelease && color==7) {
            frontend_fx_q2_impact_particles(p,r,pos,dir,0x6c,count/2,seconds,FRONTEND_FX_Q2_NORMAL);
            frontend_fx_q2_impact_particles(p,r,pos,dir,0xb0,count/2+count%2,seconds,FRONTEND_FX_Q2_NORMAL);
            color=1;
        } else frontend_fx_q2_impact_particles(p,r,pos,dir,color >= 0 && color <= 6 ? colors[color] : 0,count,seconds,FRONTEND_FX_Q2_NORMAL);
        if (color == 1) { uint32_t n = random_word(o) & 3; const char *paths[] = {"world/spark5.wav","world/spark6.wav","world/spark7.wav"}; return sound(o,paths[n < 2 ? n : 2],pos,actor,time,0,1,3,0,e); } break;
    }
    case QA_Q2_TE_LASER_SPARKS: case QA_Q2_TE_WELDING_SPARKS: case QA_Q2_TE_TUNNEL_SPARKS:
        frontend_fx_q2_impact_particles(p,r,pos,dir,(uint32_t)color,count,seconds,t->type == QA_Q2_TE_TUNNEL_SPARKS ? FRONTEND_FX_Q2_UP : FRONTEND_FX_Q2_FIXED);
        if (t->type == QA_Q2_TE_WELDING_SPARKS) explosion(o,2,Q2FX_FLASH,pos,rerelease?server-interval:server-.1,2,0,128,0,100+(float)(random_word(o)%75),qa_v3(1,1,.3f),qa_v3(0,0,0),1);
        break;
    case QA_Q2_TE_GREENBLOOD: frontend_fx_q2_impact_particles(p,r,pos,dir,0xdf,30,seconds,FRONTEND_FX_Q2_FIXED); break;
    case QA_Q2_TE_BLUEHYPERBLASTER:
        frontend_fx_q2_impact_particles(p,r,pos,end,0xe0,40,seconds,FRONTEND_FX_Q2_BLASTER); break;
    case QA_Q2_TE_BLUEHYPERBLASTER_2: case QA_Q2_TE_BLASTER: case QA_Q2_TE_BLASTER2: case QA_Q2_TE_FLECHETTE: {
        bool green=t->type==QA_Q2_TE_BLASTER2, blue=t->type==QA_Q2_TE_FLECHETTE, hyper=t->type==QA_Q2_TE_BLUEHYPERBLASTER_2;
        frontend_fx_q2_impact_particles(p,r,pos,dir,green?0xd0:blue?0x6f:hyper?0xb0:0xe0,40,seconds,FRONTEND_FX_Q2_BLASTER);
        explosion(o,1,Q2FX_EXPLODE,pos,server-interval,4,0,8|32,green?1:blue || hyper?2:0,rerelease && t->type==QA_Q2_TE_BLASTER?200:150,
            green?qa_v3(0,1,0):blue?qa_v3(.19f,.41f,.75f):hyper?qa_v3(0,0,1):qa_v3(1,1,0),direction_angles(dir),1);
        return sound(o,"weapons/lashit.wav",pos,actor,time,0,1,1,0,e);
    }
    case QA_Q2_TE_RAILTRAIL: case QA_Q2_TE_RAILTRAIL2:
        rail(o,pos,end,time,t->type==QA_Q2_TE_RAILTRAIL2,&controls); return sound(o,"weapons/railgf1a.wav",end,actor,time,0,1,1,0,e);
    case QA_Q2_TE_BUBBLETRAIL: frontend_fx_q2_bubbles(p,r,pos,end,seconds); break;
    case QA_Q2_TE_BUBBLETRAIL2: special_trail(o,pos,end,time,true); return sound(o,"weapons/lashit.wav",pos,actor,time,0,1,1,0,e);
    case QA_Q2_TE_DEBUGTRAIL: special_trail(o,pos,end,time,false); break;
    case QA_Q2_TE_BFG_LASER: case QA_Q2_TE_BFG_ZAP:
        laser(o,pos,end,time); if (t->type == QA_Q2_TE_BFG_ZAP) bfg_explosion(o,end,server,interval); break;
    case QA_Q2_TE_BFG_EXPLOSION: bfg_explosion(o,pos,server,interval); break;
    case QA_Q2_TE_BFG_BIGEXPLOSION: frontend_fx_q2_explosion(p,r,pos,seconds,true); break;
    case QA_Q2_TE_BOSSTPORT: frontend_fx_q2_big_teleport(p,r,pos,seconds); return sound(o,"misc/bigtele.wav",pos,actor,time,0,1,0,0,e);
    case QA_Q2_TE_TELEPORT_EFFECT: case QA_Q2_TE_DBALL_GOAL: frontend_fx_q2_teleport(p,r,pos,seconds); break;
    case QA_Q2_TE_FORCEWALL: frontend_fx_q2_force_wall(p,r,pos,end,(uint32_t)color,seconds); break;
    case QA_Q2_TE_CHAINFIST_SMOKE: (void)frontend_fx_q2_steam(p,r,pos,qa_v3(0,0,1),0,20,20,seconds,true); break;
    case QA_Q2_TE_HEATBEAM_SPARKS: case QA_Q2_TE_HEATBEAM_STEAM:
        (void)frontend_fx_q2_steam(p,r,pos,dir,t->type == QA_Q2_TE_HEATBEAM_SPARKS ? 8 : 0xe0,t->type == QA_Q2_TE_HEATBEAM_SPARKS ? 50 : 20,60,seconds,false);
        return sound(o,"weapons/lashit.wav",pos,actor,time,0,1,1,0,e);
    case QA_Q2_TE_TRACKER_EXPLOSION:
        frontend_fx_q2_color_explosion(p,r,pos,seconds,0,1); light(o,actor,pos,time,150,100,qa_v3(-1,-1,-1),0,250);
        return sound(o,"weapons/disrupthit.wav",pos,actor,time,0,1,1,0,e);
    case QA_Q2_TE_STEAM:
        if (!integer_field(t,QA_Q2_TEMP_ENTITY1,&id,e) || !integer_field(t,QA_Q2_TEMP_ENTITY2,&magnitude,e)) return false;
        if (id == -1) (void)frontend_fx_q2_steam(p,r,pos,dir,(uint32_t)color,count,(float)magnitude,seconds,false);
        else {
            if (!integer_field(t,QA_Q2_TEMP_TIME,&duration,e)) return false;
            for (size_t i=0;i<Q2FX_POOL;++i) if (!o->sustains[i].active) { o->sustains[i]=(q2fx_sustain){true,0,id,count,color,magnitude,pos,dir,time+duration,time}; break; }
        } break;
    case QA_Q2_TE_WIDOWBEAMOUT: case QA_Q2_TE_NUKEBLAST:
        if (t->type == QA_Q2_TE_WIDOWBEAMOUT && !integer_field(t,QA_Q2_TEMP_ENTITY1,&id,e)) return false;
        for (size_t i=0;i<Q2FX_POOL;++i) if (!o->sustains[i].active) {
            bool nuke=t->type==QA_Q2_TE_NUKEBLAST; o->sustains[i]=(q2fx_sustain){true,nuke?2:1,nuke?21000:id,0,0,0,pos,{0,0,0},time+(nuke?1000:2100),time}; break;
        } break;
    case QA_Q2_TE_WIDOWSPLASH: radial_particles(o,pos,time,256,45,40,0,false); break;
    case QA_Q2_TE_BERSERK_SLAM:
        frontend_fx_q2_berserk(p,r,pos,dir,seconds); explosion(o,1,Q2FX_EXPLODE,pos,server-interval,4,0,8|32,2,550,qa_v3(.19f,.41f,.75f),direction_angles(dir),3); break;
    case QA_Q2_TE_FLASHLIGHT:
        if (!field_actor(o,t,QA_Q2_TEMP_ENTITY1,actors,&actor,e)) return false;
        light(o,actor,pos,time,400,100,qa_v3(1,1,1),0,250); break;
    case QA_Q2_TE_POWER_SPLASH: {
        if (!integer_field(t,QA_Q2_TEMP_ENTITY1,&id,e) || id<=0) return false;
        frontend_remote_q2_effects_pose pose;
        if (actors) {
            if (!field_actor(o,t,QA_Q2_TEMP_ENTITY1,actors,&actor,e) || !o->source.actor_pose ||
                !o->source.actor_pose(o->source.context,actor,&pose,e) || !qa_actor_id_equal(actor,pose.actor)) return false;
        } else if (!o->source.actor(o->source.context,(uint32_t)id,&pose,e)) return false;
        if (!q2fx_source_current(o,e)) return false;
        if (!pose.bounds_present || !qa_vec_finite(pose.bounds.mins) || !qa_vec_finite(pose.bounds.maxs) || !isfinite(pose.radius))
            return q2fx_fail(e,QA_ERROR_ARGUMENT,"Power splash requires its actual received collision bounds");
        radial_particles(o,qa_vec_add(pose.origin,qa_vec_scale(qa_vec_add(pose.bounds.mins,pose.bounds.maxs),.5f)),time,256,pose.radius,40,208,false); break;
    }
    case QA_Q2_TE_Q2PRO_DAMAGE_DEALT:
        return count<=0 || (o->source.hit_marker && o->source.hit_marker(o->source.context,count,e) && q2fx_source_current(o,e));
    case QA_Q2_TE_PARASITE_ATTACK: case QA_Q2_TE_MEDIC_CABLE_ATTACK: case QA_Q2_TE_GRAPPLE_CABLE:
    case QA_Q2_TE_LIGHTNING: case QA_Q2_TE_HEATBEAM: case QA_Q2_TE_MONSTER_HEATBEAM:
    case QA_Q2_TE_GRAPPLE_CABLE_2: case QA_Q2_TE_LIGHTNING_BEAM: {
        if (!field_actor(o,t,QA_Q2_TEMP_ENTITY1,actors,&actor,e)) return false;
        if (t->type==QA_Q2_TE_LIGHTNING && !field_actor(o,t,QA_Q2_TEMP_ENTITY2,actors,&destination,e)) return false;
        if (t->type==QA_Q2_TE_GRAPPLE_CABLE && !vector_field(t,QA_Q2_TEMP_OFFSET,&offset,e)) return false;
        bool player=t->type==QA_Q2_TE_HEATBEAM || t->type==QA_Q2_TE_MONSTER_HEATBEAM || t->type==QA_Q2_TE_GRAPPLE_CABLE_2 || t->type==QA_Q2_TE_LIGHTNING_BEAM;
        bool heat=t->type==QA_Q2_TE_HEATBEAM || t->type==QA_Q2_TE_MONSTER_HEATBEAM;
        q2fx_model model=heat?Q2FX_HEAT:t->type==QA_Q2_TE_LIGHTNING || t->type==QA_Q2_TE_LIGHTNING_BEAM?Q2FX_LIGHTNING:t->type==QA_Q2_TE_GRAPPLE_CABLE || t->type==QA_Q2_TE_GRAPPLE_CABLE_2?Q2FX_CABLE:Q2FX_PARASITE;
        if (t->type==QA_Q2_TE_HEATBEAM) offset=qa_v3(2,7,-3);
        if (t->type==QA_Q2_TE_GRAPPLE_CABLE_2) offset=qa_v3(9,12,-3);
        if (t->type==QA_Q2_TE_LIGHTNING_BEAM) offset=qa_v3(0,12,-12);
        q2fx_beam *retained=beam(o,actor,destination,pos,end,offset,time,model,player,t->type==QA_Q2_TE_MONSTER_HEATBEAM);
        if (t->type==QA_Q2_TE_LIGHTNING && (!rerelease || (retained && retained->sound_until<time))) {
            if (!sound(o,"weapons/tesla.wav",pos,actor,time,1,1,1,0,e)) return false;
            if (rerelease) retained->sound_until=time+500;
        }
        break;
    }
    case QA_Q2_TE_EXPLOSION1: case QA_Q2_TE_EXPLOSION2: case QA_Q2_TE_ROCKET_EXPLOSION:
    case QA_Q2_TE_GRENADE_EXPLOSION: case QA_Q2_TE_ROCKET_EXPLOSION_WATER: case QA_Q2_TE_GRENADE_EXPLOSION_WATER:
    case QA_Q2_TE_PLASMA_EXPLOSION: case QA_Q2_TE_PLAIN_EXPLOSION: case QA_Q2_TE_EXPLOSION1_BIG:
    case QA_Q2_TE_EXPLOSION1_NP: case QA_Q2_TE_EXPLOSION1_NL: case QA_Q2_TE_EXPLOSION2_NL: {
        bool grenade=t->type==QA_Q2_TE_EXPLOSION2 || t->type==QA_Q2_TE_EXPLOSION2_NL || t->type==QA_Q2_TE_GRENADE_EXPLOSION || t->type==QA_Q2_TE_GRENADE_EXPLOSION_WATER;
        bool water=t->type==QA_Q2_TE_ROCKET_EXPLOSION_WATER || t->type==QA_Q2_TE_GRENADE_EXPLOSION_WATER;
        bool big=t->type==QA_Q2_TE_EXPLOSION1_BIG, unlit=t->type==QA_Q2_TE_EXPLOSION1_NL || t->type==QA_Q2_TE_EXPLOSION2_NL;
        bool grenade_weapon=t->type==QA_Q2_TE_GRENADE_EXPLOSION || t->type==QA_Q2_TE_GRENADE_EXPLOSION_WATER;
        bool rocket_weapon=t->type==QA_Q2_TE_ROCKET_EXPLOSION || t->type==QA_Q2_TE_ROCKET_EXPLOSION_WATER;
        qa_vec3 angles=qa_v3(0,(float)(random_word(o)%360),0);
        int32_t base=rerelease?(int32_t)(random_word(o)&1)*15:grenade?30:random_unit(o)<.5?15:0;
        if (grenade) base=30;
        bool only_light=(grenade_weapon && (controls.disable_explosions&1)) || (rocket_weapon && (controls.disable_explosions&2));
        float radius=unlit?0:((grenade_weapon || rocket_weapon) && (controls.dlight_hacks&2))?200:350;
        explosion(o,only_light?5:3,big && !rerelease?Q2FX_BIG:Q2FX_ROCKET,pos,server-interval,grenade?19:15,base,8,0,radius,
            qa_v3(1,.5f,.5f),angles,big && rerelease?2:1);
        if (!big && t->type!=QA_Q2_TE_EXPLOSION1_NP && t->type!=QA_Q2_TE_PLAIN_EXPLOSION &&
            !(grenade_weapon && (controls.disable_particles&1)) && !(rocket_weapon && (controls.disable_particles&4)))
            frontend_fx_q2_explosion(p,r,pos,seconds,false);
        if (rerelease && t->type==QA_Q2_TE_PLAIN_EXPLOSION) break;
        return sound(o,water?"weapons/xpld_wat.wav":grenade?"weapons/grenlx1a.wav":"weapons/rocklx1a.wav",pos,actor,time,0,1,1,0,e);
    }
    default: return q2fx_fail(e,QA_ERROR_FORMAT,"Q2 effect has no admitted source recipe");
    }
    return true;
}
bool frontend_remote_q2_effects_temporary(frontend_remote_q2_effects *owner,
    const qa_q2_temp_entity *value, const qa_actor_id actors[7], double time, double server, qa_error *error)
{
    if (!owner || !frontend_remote_q2_effects_idle(owner) || !value || value->field_count>7 || !isfinite(time) || !isfinite(server) || !q2fx_source_current(owner,error)) return false;
    for (size_t i=0;i<value->field_count;++i) {
        if (value->fields[i].name>QA_Q2_TEMP_OFFSET || value->fields[i].kind>QA_Q2_TEMP_VECTOR) return false;
        for (size_t j=0;j<i;++j) if (value->fields[j].name==value->fields[i].name) return false;
    }
    ++owner->busy; bool ok=temporary(owner,value,actors,time,server,error); owner->dirty=true; --owner->busy;
    return ok && q2fx_source_current(owner,error);
}
bool frontend_remote_q2_effects_named_effect(frontend_remote_q2_effects *o,
    const char *name,qa_vec3 origin,qa_vec3 direction,int32_t count,int32_t color,double time,qa_error *e)
{
    static const struct { const char *name; uint8_t type; } recipes[]={
        {"gunshot",QA_Q2_TE_GUNSHOT},{"blood",QA_Q2_TE_BLOOD},{"blaster",QA_Q2_TE_BLASTER},
        {"shotgun",QA_Q2_TE_SHOTGUN},{"sparks",QA_Q2_TE_SPARKS},{"screen-sparks",QA_Q2_TE_SCREEN_SPARKS},
        {"shield-sparks",QA_Q2_TE_SHIELD_SPARKS},{"bullet-sparks",QA_Q2_TE_BULLET_SPARKS},{"greenblood",QA_Q2_TE_GREENBLOOD},
        {"blaster2",QA_Q2_TE_BLASTER2},{"flechette",QA_Q2_TE_FLECHETTE},{"moreblood",QA_Q2_TE_MOREBLOOD},
        {"electric-sparks",QA_Q2_TE_ELECTRIC_SPARKS},{"splash",QA_Q2_TE_SPLASH},{"laser-sparks",QA_Q2_TE_LASER_SPARKS},
        {"welding-sparks",QA_Q2_TE_WELDING_SPARKS},{"tunnel-sparks",QA_Q2_TE_TUNNEL_SPARKS},
        {"explosion1",QA_Q2_TE_EXPLOSION1},{"explosion2",QA_Q2_TE_EXPLOSION2},{"rocket-explosion",QA_Q2_TE_ROCKET_EXPLOSION},
        {"grenade-explosion",QA_Q2_TE_GRENADE_EXPLOSION},{"rocket-explosion-water",QA_Q2_TE_ROCKET_EXPLOSION_WATER},
        {"grenade-explosion-water",QA_Q2_TE_GRENADE_EXPLOSION_WATER},{"bfg-explosion",QA_Q2_TE_BFG_EXPLOSION},
        {"bfg-bigexplosion",QA_Q2_TE_BFG_BIGEXPLOSION},{"boss-teleport",QA_Q2_TE_BOSSTPORT},{"other-teleport",QA_Q2_TE_TELEPORT_EFFECT},
        {"player-teleport",QA_Q2_TE_TELEPORT_EFFECT},{"heatbeam-sparks",QA_Q2_TE_HEATBEAM_SPARKS},
        {"heatbeam-steam",QA_Q2_TE_HEATBEAM_STEAM},{"chainfist-smoke",QA_Q2_TE_CHAINFIST_SMOKE},
        {"tracker-explosion",QA_Q2_TE_TRACKER_EXPLOSION},{"bluehyperblaster",QA_Q2_TE_BLUEHYPERBLASTER_2},
        {"berserk-slam",QA_Q2_TE_BERSERK_SLAM},{"plain-explosion",QA_Q2_TE_PLAIN_EXPLOSION}
    };
    if (!name || !qa_vec_finite(origin) || !qa_vec_finite(direction)) return false;
    char canonical[64]; size_t offset=!strncmp(name,"q2:",3)?3:0, length=strlen(name+offset);
    if (length>=sizeof(canonical)) return q2fx_fail(e,QA_ERROR_FORMAT,"Q2 effect recipe exceeds its source name");
    for (size_t i=0;i<=length;++i) canonical[i]=name[offset+i]=='_'?'-':name[offset+i];
    if (!strcmp(canonical,"item-respawn") || !strcmp(canonical,"logout")) {
        if (!o || !frontend_remote_q2_effects_idle(o) || !isfinite(time) || !q2fx_source_current(o,e)) return false;
        ++o->busy;
        frontend_fx_q2_respawn_particles(&o->particles,&o->random,origin,time*.001,
            !strcmp(canonical,"logout")?FRONTEND_FX_Q2_LOGOUT:FRONTEND_FX_Q2_ITEM);
        o->dirty=true; --o->busy; return q2fx_source_current(o,e);
    }
    for (size_t i=0;i<sizeof(recipes)/sizeof(*recipes);++i) if (!strcmp(canonical,recipes[i].name)) {
        qa_q2_temp_entity t={.type=recipes[i].type,.field_count=4};
        t.fields[0]=(qa_q2_temp_field){.name=QA_Q2_TEMP_POSITION1,.kind=QA_Q2_TEMP_VECTOR,.value.vector={origin.x,origin.y,origin.z}};
        t.fields[1]=(qa_q2_temp_field){.name=QA_Q2_TEMP_DIRECTION,.kind=QA_Q2_TEMP_VECTOR,.value.vector={direction.x,direction.y,direction.z}};
        t.fields[2]=(qa_q2_temp_field){.name=QA_Q2_TEMP_COUNT,.kind=QA_Q2_TEMP_INTEGER,.value.integer=count};
        t.fields[3]=(qa_q2_temp_field){.name=QA_Q2_TEMP_COLOR,.kind=QA_Q2_TEMP_INTEGER,.value.integer=color};
        return frontend_remote_q2_effects_temporary(o,&t,NULL,time,time,e);
    }
    return q2fx_fail(e,QA_ERROR_FORMAT,"Q2 normalized effect has no source recipe");
}
bool frontend_remote_q2_effects_named_beam(frontend_remote_q2_effects *o,
    const char *name,qa_actor_id actor_id,qa_vec3 start,qa_vec3 end,double duration,double time,qa_error *e)
{
    if (!o || !frontend_remote_q2_effects_idle(o) || !name || !qa_vec_finite(start) || !qa_vec_finite(end) ||
        !isfinite(time) || !isfinite(duration) || !q2fx_source_current(o,e)) return false;
    double die=time+(duration>0?duration*1000:100);
    if (!isfinite(die)) return q2fx_fail(e,QA_ERROR_FORMAT,"Q2 normalized beam lifetime exceeds its source clock");
    double interval;
    if (!q2fx_frame_milliseconds(o,&interval,e)) return false;
    frontend_remote_q2_effects_controls controls;
    if (!q2fx_controls(o,&controls,e)) return false;
    ++o->busy; bool ok=true;
    if (!strcmp(name,"rail") || !strcmp(name,"rail-water")) rail(o,start,end,time,false,&controls);
    else if (!strcmp(name,"bubble-trail")) frontend_fx_q2_bubbles(&o->particles,&o->random,start,end,time*.001);
    else if (!strcmp(name,"bfg-laser") || !strcmp(name,"bfg-zap")) {
        size_t capacity=o->source.profile==FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE?Q2FX_LASER_CAPACITY:Q2FX_POOL;
        for (size_t i=0;i<capacity;++i) if (!o->lasers[i].active ||
            (o->source.profile==FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE?o->lasers[i].die<=time:o->lasers[i].die<time)) {
            o->lasers[i]=(q2fx_laser){.active=true,.start=start,.end=end,.born=time,.die=die,.color=0xd0+(random_word(o)&3),.width=4}; break;
        }
        if (!strcmp(name,"bfg-zap")) bfg_explosion(o,end,time,interval);
    } else if (!strcmp(name,"bfg-lightning")) {
        for (size_t i=0;i<Q2FX_POOL;++i) if (!o->beams[i].active || o->beams[i].die<time) {
            o->beams[i]=(q2fx_beam){.active=true,.unkeyed=!actor_id.registry,.model=Q2FX_LIGHTNING,.actor=actor_id,
                .start=start,.end=end,.die=die}; break;
        }
    } else if (!strcmp(name,"heatbeam") || !strcmp(name,"monster-heatbeam")) {
        if (!actor_id.registry || !actor_id.generation) ok=q2fx_fail(e,QA_ERROR_FORMAT,"Q2 normalized player beam lost its full actor");
        else {
            bool monster=!strcmp(name,"monster-heatbeam");
            beam(o,actor_id,(qa_actor_id){0},start,end,monster?qa_v3(0,0,0):qa_v3(2,7,-3),time,Q2FX_HEAT,true,monster);
            for (size_t i=0;i<Q2FX_POOL;++i) if (o->player_beams[i].active && qa_actor_id_equal(o->player_beams[i].actor,actor_id)) o->player_beams[i].die=die;
        }
    } else ok=q2fx_fail(e,QA_ERROR_FORMAT,"Q2 normalized beam has no source recipe");
    o->dirty=true; --o->busy; return ok && q2fx_source_current(o,e);
}
static bool soldier_flash(uint32_t flash, unsigned kind)
{
    static const uint32_t values[3][8] = {{39,40,83,86,89,92,95,98},{41,42,84,87,90,93,96,99},{43,44,85,88,91,94,97,100}};
    for (size_t i=0;i<8;++i) if (values[kind][i]==flash) return true;
    return false;
}
bool q2fx_controls(frontend_remote_q2_effects *o, frontend_remote_q2_effects_controls *out, qa_error *e)
{
    *out=(frontend_remote_q2_effects_controls){0};
    if (!o->source.controls || !o->source.controls(o->source.context,out,e) || !q2fx_source_current(o,e)) return false;
    if (!isfinite(out->gun_fov) || !isfinite(out->rail_seconds) || out->rail_seconds<0 || out->rail_seconds>2073600 ||
        !isfinite(out->rail_radius)) return q2fx_fail(e,QA_ERROR_FORMAT,"Q2 effects have no finite qualified CLIENT gun or rail controls");
    if (out->muzzlelight_milliseconds<0) out->muzzlelight_milliseconds=0;
    if (out->muzzlelight_milliseconds>1000) out->muzzlelight_milliseconds=1000;
    return true;
}
bool q2fx_frame_milliseconds(frontend_remote_q2_effects *o, double *out, qa_error *e)
{
    if (o->source.profile==FRONTEND_REMOTE_Q2_EFFECTS_CLASSIC) { *out=100; return true; }
    if (!o->source.frame_milliseconds || !o->source.frame_milliseconds(o->source.context,out,e) ||
        !q2fx_source_current(o,e)) return false;
    if (!isfinite(*out) || *out<=0)
        return q2fx_fail(e,QA_ERROR_FORMAT,"Q2 effect start has no actual positive CLIENT frame interval");
    return true;
}
static bool monster_muzzle_at(frontend_remote_q2_effects *o, qa_actor_id actor_id,
    qa_vec3 origin, const qa_vec3 *angles, float scale, uint32_t flash, double time, double server, qa_error *e)
{
    bool rerelease=o->source.profile==FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE;
    const qa_vec3 *offsets=rerelease?q2m_rerelease_muzzle_offsets:q2m_classic_muzzle_offsets;
    size_t size=rerelease?sizeof(q2m_rerelease_muzzle_offsets)/sizeof(*offsets):sizeof(q2m_classic_muzzle_offsets)/sizeof(*offsets);
    if (!flash || flash>=size-1) return q2fx_fail(e,QA_ERROR_FORMAT,"Received monster muzzle leaves its source offset table");
    uint32_t profile=flash;
    if (rerelease) {
        if ((flash>=232 && flash<=239) || flash==260) profile=26;
        else if (flash==251) profile=39; else if (flash==252) profile=41; else if (flash==253) profile=43;
        else if ((flash>=256 && flash<=259) || flash==261 || flash==262) profile=53; else if (flash==263) profile=62;
        else if (flash==264 || (flash>=277 && flash<=288)) profile=144;
        else if (flash>=265 && flash<=276) profile=60;
        else if (flash==74 || flash==134) profile=58;
    }
    frontend_remote_q2_effects_controls controls;
    if (!q2fx_controls(o,&controls,e)) return false;
    double interval;
    if (!q2fx_frame_milliseconds(o,&interval,e)) return false;
    qa_vec3 color=qa_v3(1,1,0); float radius=200+(float)(random_word(o)&31);
    double duration=rerelease?controls.muzzlelight_milliseconds:0;
    const char *path=NULL; bool particles=false, smoke=false, tank_sound=false; float attenuation=1; char random_path[64];
    if ((profile>=26 && profile<=38) || profile==63 || (profile>=64 && profile<=69) || profile==141) {
        particles=smoke=true; path="infantry/infatck1.wav";
    } else if (soldier_flash(profile,2)) { particles=smoke=true; path="soldier/solatck3.wav"; }
    else if (profile>=45 && profile<=52) { particles=smoke=true; path="gunner/gunatck2.wav"; }
    else if ((profile>=73 && profile<=77) || profile==138 || profile==152) {
        particles=smoke=true; path="infantry/infatck1.wav"; attenuation=0;
    } else if ((profile>=133 && profile<=137) || profile==139 || profile==153 || (profile>=126 && profile<=131)) particles=smoke=true;
    else if (profile>=120 && profile<=125) { particles=smoke=true; path="boss3/xfire.wav"; }
    else if (profile>=4 && profile<=22) {
        particles=smoke=tank_sound=true;
    } else if (soldier_flash(profile,0) || profile==143) path="soldier/solatck2.wav";
    else if (soldier_flash(profile,1)) { smoke=true; path="soldier/solatck1.wav"; }
    else if (profile>=1 && profile<=3) path="tank/tnkatck3.wav";
    else if (profile==58 || profile==59) path="flyer/flyatck3.wav";
    else if (profile==60) path="medic/medatck1.wav";
    else if (profile==62) path="hover/hovatck1.wav";
    else if (profile==82) path="floater/fltatck1.wav";
    else if (profile>=102 && profile<=118) path="makron/blaster.wav";
    else if (profile==57 || profile==142 || (profile>=23 && profile<=25) ||
        (profile>=70 && profile<=72) || (profile>=78 && profile<=81) || (profile>=191 && profile<=194)) {
        color=qa_v3(1,.5f,.2f); path=profile==57 || profile==142?"chick/chkatck2.wav":profile<=25?"tank/tnkatck1.wav":"tank/rocket.wav";
    } else if ((profile>=53 && profile<=56) || profile==140) { color=qa_v3(1,.5f,0); path="gunner/gunatck3.wav"; }
    else if (profile==61 || profile==119 || profile==147 || profile==150 || profile==154 || profile==155 || (rerelease && flash>=228 && flash<=231)) color=qa_v3(.5f,.5f,1);
    else if (profile==101 || profile==132) color=qa_v3(.5f,1,.5f);
    else if ((profile>=144 && profile<=146) || profile==149 || (profile>=156 && profile<=190)) { color=qa_v3(0,1,0); path="tank/tnkatck3.wav"; }
    else if (profile==148) { color=qa_v3(-1,-1,-1); path="weapons/disint2.wav"; }
    else if (profile==151 || (profile>=195 && profile<=210)) { radius=300+(float)(random_word(o)&100); duration=200; }
    else if (rerelease && ((flash>=211 && flash<=218) || flash==254)) { color=qa_v3(1,.5f,.5f); path="weapons/rippfire.wav"; }
    else if (rerelease && ((flash>=219 && flash<=226) || flash==255)) { color=qa_v3(0,0,1); path="weapons/hyprbf1a.wav"; }
    else if (rerelease && flash==227) path="weapons/hyprbf1a.wav";
    else if (rerelease && (flash==240 || flash==241)) { color=qa_v3(0,0,1); path="guncmdr/gcdratck2.wav"; }
    else if (rerelease && flash>=242 && flash<=250) { color=qa_v3(1,.5f,0); path="guncmdr/gcdratck3.wav"; }
    else return q2fx_fail(e,QA_ERROR_FORMAT,"Received monster muzzle lacks its source effect definition");
    light(o,actor_id,origin,time,radius,duration,color,0,rerelease?0:32);
    if (particles) frontend_fx_q2_impact_particles(&o->particles,&o->random,origin,qa_v3(0,0,0),0,40,time*.001,FRONTEND_FX_Q2_NORMAL);
    if (smoke) smoke_flash(o,origin,server,interval);
    if (tank_sound) { snprintf(random_path,sizeof(random_path),"tank/tnkatk2%c.wav",(int)('a'+random_word(o)%5)); path=random_path; }
    if (path && !sound(o,path,origin,actor_id,time,1,1,attenuation,0,e)) return false;
    if (rerelease && controls.muzzleflashes && angles) {
        q2fx_model model=Q2FX_MUZZLE_BLAST; int32_t skin=0; float muzzle_scale=8;
        if (particles) { model=Q2FX_MUZZLE_MACHINE;
            muzzle_scale=tank_sound?20:soldier_flash(profile,2)?13:profile>=45 && profile<=52?24:
                profile>=26 && profile<=38?18:32;
        } else if (soldier_flash(profile,1)) { model=Q2FX_MUZZLE_SHOTGUN; muzzle_scale=17; }
        else if (profile==57 || profile==142 || (profile>=23 && profile<=25) ||
            (profile>=70 && profile<=72) || (profile>=78 && profile<=81) || (profile>=191 && profile<=194))
            { model=Q2FX_MUZZLE_ROCKET; muzzle_scale=profile==57 || profile==142?16:28; }
        else if ((profile>=53 && profile<=56) || profile==140 || (flash>=242 && flash<=250)) { model=Q2FX_MUZZLE_LAUNCH; muzzle_scale=18; }
        else if (profile==61 || profile==119 || profile==147 || profile==150 || profile==154 || profile==155 || (flash>=228 && flash<=231))
            { model=Q2FX_MUZZLE_RAIL; muzzle_scale=32; }
        else if (profile==101 || profile==132) { model=Q2FX_MUZZLE_BFG; muzzle_scale=64; }
        else if (profile==148) { model=Q2FX_MUZZLE_DIST; muzzle_scale=32; }
        else if (profile==151 || (profile>=195 && profile<=210)) { model=Q2FX_MUZZLE_BEAM; muzzle_scale=32; }
        else if ((flash>=211 && flash<=218) || flash==254) { model=Q2FX_MUZZLE_BOOMER; muzzle_scale=32; }
        else if (flash==240 || flash==241) { model=Q2FX_MUZZLE_ETF; muzzle_scale=16; }
        else if ((flash>=219 && flash<=226) || flash==255) skin=1;
        else if (color.x==0 && color.y==1 && color.z==0) { skin=2; muzzle_scale=22; }
        else if (profile>=1 && profile<=3) muzzle_scale=24;
        else if (profile>=102 && profile<=118) muzzle_scale=22;
        else if (flash==74 || flash==134) muzzle_scale=12;
        else if (flash==227) muzzle_scale=16;
        if (o->models[model]) {
            qa_vec3 axis[3]; axes(*angles,axis);
            qa_vec3 flash_origin=qa_vec_add(origin,qa_vec_scale(axis[0],4*scale)), rotation=*angles;
            if (model!=Q2FX_MUZZLE_BOOMER) rotation.z=(float)(random_word(o)%360);
            explosion(o,4,model,flash_origin,server-interval,2,0,8|32|8192,skin,0,qa_v3(0,0,0),rotation,muzzle_scale*scale);
        }
    }
    return true;
}
static bool monster_muzzle(frontend_remote_q2_effects *o, frontend_remote_q2_effects_pose pose,
    uint32_t flash, double time, double server, qa_error *e)
{
    bool rerelease=o->source.profile==FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE;
    const qa_vec3 *offsets=rerelease?q2m_rerelease_muzzle_offsets:q2m_classic_muzzle_offsets;
    size_t size=rerelease?sizeof(q2m_rerelease_muzzle_offsets)/sizeof(*offsets):sizeof(q2m_classic_muzzle_offsets)/sizeof(*offsets);
    if (!flash || flash>=size-1) return q2fx_fail(e,QA_ERROR_FORMAT,"Received monster muzzle leaves its source offset table");
    float scale=rerelease && pose.scale!=0?pose.scale:1;
    if (!isfinite(scale) || scale<=0) return q2fx_fail(e,QA_ERROR_FORMAT,"Q2 monster muzzle has invalid received scale");
    qa_vec3 axis[3]; axes(pose.angles,axis); qa_vec3 offset=qa_vec_scale(offsets[flash],scale);
    qa_vec3 origin=qa_vec_sub(qa_vec_add(pose.origin,qa_vec_scale(axis[0],offset.x)),qa_vec_scale(axis[1],offset.y)); origin.z+=offset.z;
    return monster_muzzle_at(o,pose.actor,origin,&pose.angles,scale,flash,time,server,e);
}
static bool weapon_muzzle(frontend_remote_q2_effects *o, qa_actor_id actor_id, uint32_t flash,
    const frontend_remote_q2_effects_controls *controls, double server, double interval, qa_error *e)
{
    if (o->source.profile!=FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE || !controls->muzzleflashes) return true;
    qa_actor_id viewer;
    if (!o->source.viewer(o->source.context,&viewer,e) || !q2fx_source_current(o,e)) return false;
    if (!qa_actor_id_equal(viewer,actor_id)) return true;
    q2fx_model model; qa_vec3 offset; float scale;
    switch (flash) {
    case 0: model=Q2FX_MUZZLE_BLAST; offset=qa_v3(27,7.4f,-6.6f); scale=8; break;
    case 14: model=Q2FX_MUZZLE_BLAST; offset=qa_v3(23.5f,6,-6); scale=9; break;
    case 1: model=Q2FX_MUZZLE_MACHINE; offset=qa_v3(29,9.7f,-8); scale=12; break;
    case 2: model=Q2FX_MUZZLE_SHOTGUN; offset=qa_v3(26.5f,8.6f,-9.5f); scale=12; break;
    case 13: model=Q2FX_MUZZLE_SSHOTGUN; offset=qa_v3(25,7,-5.5f); scale=12; break;
    case 3: case 4: case 5:
        model=Q2FX_MUZZLE_MACHINE; offset=qa_v3(29,9.7f,-10); scale=flash==3?12:flash==4?16:20; break;
    case 6: model=Q2FX_MUZZLE_RAIL; offset=qa_v3(20,5.2f,-7); scale=12; break;
    case 7: model=Q2FX_MUZZLE_ROCKET; offset=qa_v3(20.8f,5,-11); scale=10; break;
    case 8: case 31: model=Q2FX_MUZZLE_LAUNCH; offset=qa_v3(18,6,-6.5f); scale=9; break;
    case 19: model=Q2FX_MUZZLE_BFG; offset=qa_v3(18,8,-7.5f); scale=16; break;
    case 20: model=Q2FX_MUZZLE_ROCKET; offset=qa_v3(18,10,-6); scale=9; break;
    case 16: model=Q2FX_MUZZLE_BOOMER; offset=qa_v3(24,3.8f,-5.5f); scale=15; break;
    case 30: case 32: model=Q2FX_MUZZLE_ETF; offset=qa_v3(24,flash==30?5.25f:4,-5.5f); scale=4; break;
    case 33: model=Q2FX_MUZZLE_BEAM; offset=qa_v3(18,6,-8.5f); scale=16; break;
    case 35: model=Q2FX_MUZZLE_DIST; offset=qa_v3(18,6,-6.5f); scale=10; break;
    default: return true;
    }
    if (!o->models[model]) return true;
    float roll=model==Q2FX_MUZZLE_MACHINE || model==Q2FX_MUZZLE_BEAM?(float)(random_word(o)%360):0;
    o->weapon_muzzle=(q2fx_weapon_muzzle){.active=true,.model=(uint8_t)model,.actor=actor_id,
        .offset=offset,.scale=scale,.roll=roll,.start=server-interval};
    return true;
}
static bool player_muzzle(frontend_remote_q2_effects *o, frontend_remote_q2_effects_pose pose,
    uint32_t flash, bool silenced, double time, double server, qa_error *e)
{
    if (!((flash<=20 && flash!=15) || (flash>=30 && flash<=39)))
        return q2fx_fail(e,QA_ERROR_FORMAT,"Received player muzzle lacks its source effect definition");
    qa_vec3 axis[3]; axes(pose.angles,axis);
    qa_vec3 origin=qa_vec_sub(qa_vec_add(pose.origin,qa_vec_scale(axis[0],18)),qa_vec_scale(axis[1],16));
    bool extended=o->source.profile==FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE;
    frontend_remote_q2_effects_controls controls;
    if (!q2fx_controls(o,&controls,e)) return false;
    double interval;
    if (!q2fx_frame_milliseconds(o,&interval,e)) return false;
    qa_vec3 color=flash==6?qa_v3(.5f,.5f,1):flash==7?qa_v3(1,.5f,.2f):flash==8 || flash==4 || flash==31?qa_v3(1,.5f,0):
        flash==3?qa_v3(1,.25f,0):flash==12 || flash==19 || flash==34 || flash==9?qa_v3(0,1,0):
        flash==10 || flash==36?qa_v3(1,0,0):flash==35?qa_v3(-1,-1,-1):flash==17 || flash==38?qa_v3(0,0,1):
        flash==39?qa_v3(0,1,1):flash==16 || flash==18 || flash==20?qa_v3(1,.5f,.5f):flash==30 || (flash==32 && extended)?qa_v3(.9f,.7f,0):qa_v3(1,1,0);
    float radius=(silenced?100:200)+(float)(random_word(o)&31);
    if (flash>=3 && flash<=5) radius=(flash==4?225:flash==5?250:200)+(float)(random_word(o)&31);
    double duration=flash==33 || (flash>=36 && flash<=39)?100:extended?controls.muzzlelight_milliseconds:
        flash>=9 && flash<=11?1:flash==4 || flash==5?.1:0;
    bool suppress=extended && (controls.dlight_hacks&4) && (flash==1 || (flash>=3 && flash<=5));
    light(o,pose.actor,origin,time,suppress?0:radius,duration,color,0,extended?0:32);
    float volume=silenced?.2f:1; const char *path=NULL; const char *reload=NULL;
    if (flash==0 || flash==34) path="weapons/blastf1a.wav";
    else if (flash==14 || flash==17) path="weapons/hyprbf1a.wav";
    else if (flash==2) { path="weapons/shotgf1b.wav"; reload="weapons/shotgr1b.wav"; }
    else if (flash==13) path="weapons/sshotf1b.wav";
    else if (flash==6) path="weapons/railgf1a.wav";
    else if (flash==7) { path="weapons/rocklf1a.wav"; reload="weapons/rocklr1b.wav"; }
    else if (flash==8 || flash==31) { path="weapons/grenlf1a.wav"; reload=flash==31?"weapons/proxlr1a.wav":"weapons/grenlr1b.wav"; }
    else if (flash==12) path="weapons/bfg__f1y.wav";
    else if (flash>=9 && flash<=11) {
        path="weapons/grenlf1a.wav"; volume=1;
        frontend_fx_q2_respawn_particles(&o->particles,&o->random,pose.origin,time*.001,
            flash==9?FRONTEND_FX_Q2_LOGIN:flash==10?FRONTEND_FX_Q2_LOGOUT:FRONTEND_FX_Q2_RESPAWN);
    } else if (flash==16) path="weapons/rippfire.wav";
    else if (flash==18) path="weapons/plasshot.wav";
    else if (flash==30) path="weapons/nail1.wav";
    else if (flash==32) {
        path=extended?"weapons/nail1.wav":"weapons/shotg2.wav";
    }
    else if (flash==35) path="weapons/disint2.wav";
    if (flash==1 || (flash>=3 && flash<=5)) {
        unsigned shots=flash==4?2:flash==5?3:1;
        for (unsigned i=0;i<shots;++i) {
            char name[48]; snprintf(name,sizeof(name),"weapons/machgf%ub.wav",random_word(o)%5+1);
            if (!sound(o,name,origin,pose.actor,time,1,volume,1,flash==4?i*.05:flash==5?i*.033:0,e)) return false;
        }
    }
    if (path && !sound(o,path,origin,pose.actor,time,1,volume,1,0,e)) return false;
    if (reload && !sound(o,reload,origin,pose.actor,time,0,volume,1,
        extended && controls.rerelease_effects?(flash==2?.35:.15):.1,e)) return false;
    if (extended && controls.rerelease_effects && flash==6 &&
        !sound(o,"weapons/railgr1b.wav",origin,pose.actor,time,7,volume,1,.4,e)) return false;
    return weapon_muzzle(o,pose.actor,flash,&controls,server,interval,e);
}
bool frontend_remote_q2_effects_muzzle(frontend_remote_q2_effects *o,
    uint32_t entity, uint32_t flash, bool monster, bool silenced, double time, double server, qa_error *e)
{
    if (!o || !frontend_remote_q2_effects_idle(o) || !isfinite(time) || !isfinite(server) || !entity || !q2fx_source_current(o,e)) return false;
    frontend_remote_q2_effects_pose pose;
    ++o->busy;
    bool ok=o->source.actor(o->source.context,entity,&pose,e) && q2fx_source_current(o,e) && pose.actor.registry && pose.actor.generation &&
        qa_vec_finite(pose.origin) && qa_vec_finite(pose.angles) &&
        (monster?monster_muzzle(o,pose,flash,time,server,e):player_muzzle(o,pose,flash,silenced,time,server,e));
    o->dirty=true; --o->busy; return ok && q2fx_source_current(o,e);
}
bool frontend_remote_q2_effects_actor_muzzle(frontend_remote_q2_effects *o,
    qa_actor_id actor_id,uint32_t flash,bool monster,bool silenced,double time,double server,qa_error *e)
{
    if (!o || !frontend_remote_q2_effects_idle(o) || !o->source.actor_pose || !actor_id.registry || !actor_id.generation ||
        !isfinite(time) || !isfinite(server) || !q2fx_source_current(o,e)) return false;
    frontend_remote_q2_effects_pose pose;
    ++o->busy;
    bool ok=o->source.actor_pose(o->source.context,actor_id,&pose,e) && qa_actor_id_equal(pose.actor,actor_id) &&
        q2fx_source_current(o,e) && qa_vec_finite(pose.origin) && qa_vec_finite(pose.angles) &&
        (monster?monster_muzzle(o,pose,flash,time,server,e):player_muzzle(o,pose,flash,silenced,time,server,e));
    o->dirty=true; --o->busy; return ok && q2fx_source_current(o,e);
}
bool frontend_remote_q2_effects_monster_muzzle(frontend_remote_q2_effects *o,
    qa_actor_id actor_id, uint32_t flash, qa_vec3 origin, qa_vec3 direction,
    double time, double server, qa_error *e)
{
    if (!o || !frontend_remote_q2_effects_idle(o) || !actor_id.registry || !actor_id.generation ||
        !isfinite(time) || !isfinite(server) || !qa_vec_finite(origin) || !qa_vec_finite(direction) ||
        !q2fx_source_current(o,e)) return false;
    ++o->busy;
    bool ok=monster_muzzle_at(o,actor_id,origin,NULL,1,flash,time,server,e);
    o->dirty=true; --o->busy; return ok && q2fx_source_current(o,e);
}
bool frontend_remote_q2_effects_monster_muzzle_pose(frontend_remote_q2_effects *o,
    qa_actor_id actor_id,uint32_t flash,qa_vec3 origin,qa_vec3 angles,float scale,
    double time,double server,qa_error *e)
{
    if (!o || !frontend_remote_q2_effects_idle(o) || !actor_id.registry || !actor_id.generation ||
        !isfinite(time) || !isfinite(server) || !qa_vec_finite(origin) || !qa_vec_finite(angles) ||
        !isfinite(scale) || scale<0 || !q2fx_source_current(o,e)) return false;
    ++o->busy;
    bool ok=monster_muzzle_at(o,actor_id,origin,&angles,scale==0?1:scale,flash,time,server,e);
    o->dirty=true; --o->busy; return ok && q2fx_source_current(o,e);
}
bool frontend_remote_q2_effects_weapon_draw(frontend_remote_q2_effects *o,qa_actor_id viewer,
    const qa_scene_model_input *weapon,qa_scene_frame *frame,qa_error *e)
{
    if (!o || !frontend_remote_q2_effects_idle(o) || !weapon || !frame ||
        !isfinite(weapon->seconds) || weapon->family!=QA_SCENE_Q2 ||
        weapon->material_library!=o->source.materials || !q2fx_source_current(o,e)) return false;
    q2fx_weapon_muzzle *m=&o->weapon_muzzle;
    if (!m->active || !qa_actor_id_equal(m->actor,viewer)) return true;
    double time=weapon->seconds*1000;
    if (time-m->start>50) { m->active=false; return true; }
    qa_actor_id actual;
    if (!o->source.viewer || !o->source.viewer(o->source.context,&actual,e) ||
        !qa_actor_id_equal(actual,viewer) || !q2fx_source_current(o,e)) return false;
    qa_scene_model_input input=*weapon;
    qa_vec3 origin=qa_v3(input.transform.origin[0],input.transform.origin[1],input.transform.origin[2]);
    for (size_t i=0;i<3;++i) {
        float offset=i==0?m->offset.x:i==1?-m->offset.y:m->offset.z;
        origin.x+=input.transform.axes[i][0]*offset;
        origin.y+=input.transform.axes[i][1]*offset;
        origin.z+=input.transform.axes[i][2]*offset;
    }
    input.transform.origin[0]=origin.x; input.transform.origin[1]=origin.y; input.transform.origin[2]=origin.z;
    float angle=m->roll*.017453292519943295f,cosine=cosf(angle),sine=sinf(angle);
    qa_vec3 basis[3]={input.view.axis[0],qa_vec_add(qa_vec_scale(input.view.axis[1],cosine),qa_vec_scale(input.view.axis[2],sine)),
        qa_vec_add(qa_vec_scale(input.view.axis[1],-sine),qa_vec_scale(input.view.axis[2],cosine))};
    for (size_t i=0;i<3;++i) {
        input.transform.axes[i][0]=basis[i].x; input.transform.axes[i][1]=basis[i].y;
        input.transform.axes[i][2]=basis[i].z; input.transform.scale[i]=m->scale;
    }
    input.previous_origin=origin; input.flags=8|16|4|32; input.color=(qa_scene_vec4){1,1,1,1};
    input.frame=0; input.old_frame=0; input.skin=0; input.back_lerp=0;
    input.ambient=qa_v3(1,1,1); input.source_path=q2fx_model_paths[m->model];
    input.pose=NULL; input.pose_count=0; input.custom_material=NULL; input.custom_skin=NULL;
    input.custom_skin_materials=NULL; input.custom_skin_material_count=0; input.indexed_skin=NULL;
    input.replacement=NULL; input.animation=NULL; input.attachments=NULL; input.attachment_count=0;
    input.video_frame=o->source.video_frame; input.video_context=o->source.video_context;
    ++o->busy; bool ok=qa_scene_model_submit(o->models[m->model],&input,frame,e); --o->busy;
    return ok && q2fx_source_current(o,e);
}
void q2fx_sampled_light(frontend_remote_q2_effects *o, qa_vec3 origin, float radius, qa_vec3 color, float minimum)
{
    if (radius<=0 || o->light_count==Q2FX_LIGHT_CAPACITY) return;
    o->sampled_lights[o->light_count++]=(qa_scene_light){.origin=origin,.color=color,.radius=radius,.minimum=minimum,
        .scale=1,.additive=true,.family=QA_SCENE_Q2};
}
static bool model_draw(frontend_remote_q2_effects *o, q2fx_model model, qa_vec3 origin, qa_vec3 angles,
    int32_t frame, int32_t old_frame, float back_lerp, int32_t skin, uint32_t flags, float alpha, float scale, qa_error *e)
{
    if (!o->models[model]) return true;
    if (o->draw_count==o->draw_capacity) {
        size_t capacity=o->draw_capacity?o->draw_capacity*2:64;
        if (capacity<o->draw_capacity || capacity>SIZE_MAX/sizeof(*o->draws)) return q2fx_fail(e,QA_ERROR_MEMORY,"Q2 transient model draw capacity overflow");
        q2fx_model_draw *next=realloc(o->draws,capacity*sizeof(*next));
        if (!next) return q2fx_fail(e,QA_ERROR_MEMORY,"Retaining Q2 transient model draws");
        o->draws=next; o->draw_capacity=capacity;
    }
    o->draws[o->draw_count++]=(q2fx_model_draw){(uint8_t)model,origin,angles,frame,old_frame,skin,flags,alpha,back_lerp,{scale,scale,scale}}; return true;
}
static bool slow_prepare(frontend_remote_q2_effects *o, qa_error *e)
{
    if (o->source.profile!=FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE) return true;
    uint64_t wall, frame;
    if (!o->source.render_clock(o->source.context,&wall,&frame,e) || !q2fx_source_current(o,e)) return false;
    uint32_t bin=(uint32_t)wall/16;
    if (o->slow_bin!=bin) { o->slow_bin=bin; o->slow_base=random_word(o); }
    if (o->slow_frame!=frame) { o->slow_frame=frame; o->slow_seed=o->slow_base; }
    return true;
}
static bool beam_roll(frontend_remote_q2_effects *o, uint32_t *out, qa_error *e)
{
    if (o->source.profile==FRONTEND_REMOTE_Q2_EFFECTS_CLASSIC) { *out=random_word(o)%360; return true; }
    if (!slow_prepare(o,e)) return false;
    uint32_t value=o->slow_seed;
    value^=value<<13; value^=value>>17; value^=value<<5;
    o->slow_seed=value;
    *out=value%360;
    return true;
}
static bool rerelease_regular_beam(frontend_remote_q2_effects *o, const q2fx_beam *beam_row,
    qa_vec3 start, qa_vec3 delta, qa_vec3 angles, qa_error *e)
{
    float remaining=qa_vec_length(delta), segment=beam_row->model==Q2FX_LIGHTNING?35:30;
    if (!isfinite(remaining)) return q2fx_fail(e,QA_ERROR_FORMAT,"Q2 beam length exceeds its source vector");
    double steps=ceil((double)remaining/segment);
    if (steps>(double)(SIZE_MAX/sizeof(*o->draws)))
        return q2fx_fail(e,QA_ERROR_FORMAT,"Q2 beam exceeds its physical model draw capacity");
    qa_vec3 direction=qa_vec_normalize(delta);
    bool lightning=beam_row->model==Q2FX_LIGHTNING;
    if (lightning) { angles.x=-angles.x; angles.y+=180; }
    while (remaining>0) {
        float used=fminf(remaining,segment), longitudinal=used/segment;
        if (!lightning) start=qa_vec_add(start,qa_vec_scale(direction,.5f*used));
        uint32_t roll;
        if (!beam_roll(o,&roll,e)) return false;
        angles.z=(float)roll;
        size_t first=o->draw_count;
        if (!model_draw(o,(q2fx_model)beam_row->model,start,angles,0,0,0,0,
            lightning?8|8192:8192,1,1,e)) return false;
        if (o->draw_count>first) o->draws[first].scale.x=longitudinal;
        start=qa_vec_add(start,qa_vec_scale(direction,(lightning?1:.5f)*segment));
        float next=remaining-segment;
        if (next>=remaining) return q2fx_fail(e,QA_ERROR_FORMAT,"Q2 beam segment exceeds its source float precision");
        remaining=next;
    }
    return true;
}
static void heat_particles(frontend_remote_q2_effects *o, qa_vec3 origin, qa_vec3 direction,
    const frontend_remote_q2_effects_sample *s)
{
    if (qa_vec_length(direction)==0) return;
    qa_vec3 right=qa_vec_scale(s->view.axis[1],-1), up=s->view.axis[2], move=origin;
    if (s->hardware) move=qa_vec_sub(move,qa_vec_scale(qa_vec_add(right,up),.5f));
    float begin=fmodf((float)(s->milliseconds*.096),32);
    move=qa_vec_add(move,qa_vec_scale(direction,begin));
    for (float distance=begin;distance<4096 && distance<=160;distance+=32) {
        for (double rotation=0;rotation<6.283185307179586;rotation+=.3141592653589793) {
            if (o->particles.count==FRONTEND_FX_PARTICLE_CAPACITY) return;
            float taper=distance<10?distance/10:1;
            qa_vec3 radial=qa_vec_add(qa_vec_scale(right,(float)cos(rotation)*.5f*taper),qa_vec_scale(up,(float)sin(rotation)*.5f*taper));
            frontend_fx_q2_particle p={.spawn_milliseconds=s->milliseconds,.origin=qa_vec_add(move,qa_vec_scale(radial,3)),
                .color=223-(random_word(o)&7),.alpha=.5f,.alpha_velocity=-1000};
            o->particles.values.q2[o->particles.count++]=p;
        }
        move=qa_vec_add(move,qa_vec_scale(direction,32));
    }
}
static qa_vec3 beam_angles(const frontend_remote_q2_effects *o, qa_vec3 delta)
{
    float horizontal=hypotf(delta.x,delta.y), yaw=horizontal==0?0:atan2f(delta.y,delta.x)*57.29577951308232f;
    bool rerelease=o->source.profile==FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE;
    float pitch=horizontal==0?(delta.z>0?90:270):atan2f(delta.z,horizontal)*(rerelease?57.29577951308232f:-57.29577951308232f);
    if (yaw<0) yaw+=360;
    if (pitch<0) pitch+=360;
    return qa_v3(rerelease?-pitch:pitch,yaw,0);
}
bool q2fx_prepare_beams(frontend_remote_q2_effects *o, q2fx_beam *pool, size_t count,
    const frontend_remote_q2_effects_sample *s, const frontend_remote_q2_effects_controls *controls,
    bool advance, qa_error *e)
{
    for (size_t i=0;i<count;++i) {
        q2fx_beam *b=&pool[i];
        if (!b->active) continue;
        if (b->die<s->milliseconds) { b->active=false; continue; }
        if (!o->models[b->model]) continue;
        qa_vec3 start=b->start, offset=b->offset, delta;
        bool local=!b->unkeyed && b->actor.registry && s->viewer.registry && qa_actor_id_equal(b->actor,s->viewer);
        bool heat=b->model==Q2FX_HEAT, lightning=b->model==Q2FX_LIGHTNING;
        bool rerelease=o->source.profile==FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE;
        float hand=s->hand==2?0:s->hand==1?-1:1;
        if (rerelease && controls->gun==3) hand=-1;
        else if (rerelease && controls->gun==2) hand=1;
        if (b->player && local) {
            if (rerelease && controls->gun_fov>0) {
                float tangent=tanf(fminf(160,fmaxf(30,controls->gun_fov))*.008726646259971648f);
                if (!isfinite(s->player_fov) || s->player_fov<=0 || s->player_fov>=180)
                    return q2fx_fail(e,QA_ERROR_ARGUMENT,"Q2 player beam lost its actual player field of view");
                float ratio=tanf(s->player_fov*.008726646259971648f)/tangent;
                offset.x*=ratio;
                offset.z*=ratio;
            }
            start=qa_vec_add(s->view.origin,s->gun_offset);
            start=qa_vec_add(start,qa_vec_scale(s->view.axis[1],-hand*offset.x));
            start=qa_vec_add(start,qa_vec_scale(s->view.axis[0],offset.y));
            start=qa_vec_add(start,qa_vec_scale(s->view.axis[2],offset.z));
            if (hand==0) start=qa_vec_sub(start,s->view.axis[2]);
        } else if (!b->player && local) {
            if (o->source.profile==FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE) {
                if (!s->viewer_origin_present || !qa_vec_finite(s->viewer_origin))
                    return q2fx_fail(e,QA_ERROR_ARGUMENT,"Q2 beam lost its actual rendered player body origin");
                start=qa_vec_add(s->viewer_origin,offset);
            } else { start=s->view.origin; start.z-=22; start=qa_vec_add(start,offset); }
        }
        else if (!(rerelease && b->player)) start=qa_vec_add(start,offset);
        delta=qa_vec_sub(b->end,start);
        if (rerelease && b->player && !local) {
            if (offset.x!=0 || offset.y!=0 || offset.z!=0) {
                qa_vec3 rotation=beam_angles(o,delta), axis[3];
                axes(qa_v3(-rotation.x,rotation.y+180,0),axis);
                start=qa_vec_add(start,qa_vec_scale(axis[1],offset.x-1));
                start=qa_vec_sub(start,qa_vec_scale(axis[0],offset.y));
                start=qa_vec_add(start,qa_vec_scale(axis[2],-offset.z-10));
            } else if (heat && advance) radial_particles(o,b->start,s->milliseconds,40,10,0,0xe0,true);
            delta=qa_vec_sub(b->end,start);
        }
        if (rerelease && b->player && local && b->model!=Q2FX_CABLE)
            delta=qa_vec_scale(s->view.axis[0],qa_vec_length(delta));
        else if (!rerelease && heat && local) {
            delta=qa_vec_scale(s->view.axis[0],qa_vec_length(delta));
            delta=qa_vec_sub(delta,qa_vec_scale(s->view.axis[1],hand*offset.x));
            delta=qa_vec_add(delta,qa_vec_scale(s->view.axis[0],offset.y));
            delta=qa_vec_add(delta,qa_vec_scale(s->view.axis[2],offset.z));
            if (s->hand==2) start=qa_vec_sub(start,s->view.axis[2]);
        }
        qa_vec3 rotation=beam_angles(o,delta);
        float yaw=rotation.y,pitch=rotation.x;
        if (!b->player && o->source.profile==FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE) {
            if (!rerelease_regular_beam(o,b,start,delta,qa_v3(pitch,yaw,0),e)) return false;
            continue;
        }
        qa_vec3 direction=qa_vec_normalize(delta);
        if (!rerelease && heat && !local) {
            if (!b->monster) {
                qa_vec3 axis[3]; axes(qa_v3(-pitch,yaw+180,0),axis);
                start=qa_vec_add(start,qa_vec_scale(axis[1],offset.x-1));
                start=qa_vec_sub(start,qa_vec_scale(axis[0],offset.y));
                start=qa_vec_add(start,qa_vec_scale(axis[2],-offset.z-10));
            } else if (advance) radial_particles(o,b->start,s->milliseconds,40,10,0,0xe0,true);
        }
        if (heat && local && advance) heat_particles(o,start,direction,s);
        float length=qa_vec_length(delta)-(lightning?20:0), segment=heat?32:lightning?35:30;
        if (rerelease && b->player && local && b->model==Q2FX_CABLE && hand!=0) {
            start=qa_vec_add(start,qa_vec_scale(direction,segment*.5f));
            length-=segment*.5f;
        }
        bool short_lightning=lightning && length<=segment;
        if (length<=0 && !short_lightning) continue;
        double steps=short_lightning?1:ceil((double)length/segment);
        if (!isfinite(steps) || steps>(double)(SIZE_MAX/sizeof(*o->draws))) return q2fx_fail(e,QA_ERROR_FORMAT,"Q2 beam exceeds physical model draw capacity");
        float spacing=steps>1?(length-segment)/(float)(steps-1):0;
        for (size_t j=0;j<(size_t)steps;++j) {
            qa_vec3 origin=short_lightning?b->end:qa_vec_add(start,qa_vec_scale(direction,(float)j*spacing));
            uint32_t roll=0;
            if (!heat && !beam_roll(o,&roll,e)) return false;
            qa_vec3 angles=qa_v3(short_lightning || (!lightning && !heat)?pitch:-pitch,
                short_lightning || (!lightning && !heat)?yaw:yaw+180,heat?(float)fmod(s->milliseconds,360):(float)roll);
            int32_t frame=heat?(local?1:2):0;
            if (!model_draw(o,(q2fx_model)b->model,origin,angles,frame,frame,0,0,heat || lightning?8:rerelease?8192:0,1,1,e)) return false;
        }
    }
    return true;
}
bool frontend_remote_q2_effects_prepare(frontend_remote_q2_effects *o,
    const frontend_remote_q2_effects_sample *s, const qa_scene_light **lights_out, size_t *count_out, qa_error *e)
{
    if (!o || !frontend_remote_q2_effects_idle(o) || !s || !lights_out || !count_out || !isfinite(s->milliseconds) || !isfinite(s->server_milliseconds) ||
        !isfinite(s->fraction) || s->fraction<0 || s->fraction>1 || !isfinite(s->frame_seconds) || s->frame_seconds<0 ||
        (s->entity_count && !s->entities) ||
        s->entity_count>SIZE_MAX/sizeof(q2fx_trail) || !q2fx_source_current(o,e)) return false;
    if (o->sampled && s->milliseconds<o->time) return q2fx_fail(e,QA_ERROR_ARGUMENT,"Remote Q2 source time rewound without replacing its effects owner");
    uint64_t render_wall, render_frame;
    if (!o->source.render_clock(o->source.context,&render_wall,&render_frame,e) || !q2fx_source_current(o,e)) return false;
    for (size_t i=0;i<s->entity_count;++i) {
        if (!s->entities[i].actor.registry || !s->entities[i].actor.generation || !qa_vec_finite(s->entities[i].origin) || !qa_vec_finite(s->entities[i].angles)) return false;
        for (size_t j=0;j<i;++j) if (qa_actor_id_equal(s->entities[i].actor,s->entities[j].actor)) return false;
    }
    bool advance=!o->sampled || s->milliseconds>o->time, events=!o->sampled || s->frame_sequence!=o->frame_sequence;
    frontend_remote_q2_effects_controls controls;
    if (!q2fx_controls(o,&controls,e)) return false;
    bool policy_changed=o->sampled_dlight_hacks!=controls.dlight_hacks || o->sampled_disable_particles!=controls.disable_particles ||
        o->sampled_gun!=controls.gun || o->sampled_gun_fov!=controls.gun_fov;
    if (advance || events || o->dirty || policy_changed || o->render_frame!=render_frame) {
        q2fx_trail *trails=s->entity_count?calloc(s->entity_count,sizeof(*trails)):NULL;
        if (s->entity_count && !trails) return q2fx_fail(e,QA_ERROR_MEMORY,"Retaining received Q2 entity trails");
        ++o->busy; o->light_count=0; o->draw_count=0;
        size_t retained=0, sampled_retained=0;
        for (size_t i=0;i<o->particles.count;++i) {
            frontend_fx_q2_particle p=o->particles.values.q2[i]; qa_vec3 origin; float alpha;
            if ((advance && o->sampled && i<o->sampled_particle_count && p.alpha_velocity==-10000) ||
                !frontend_fx_q2_sample(&p,s->milliseconds,&origin,&alpha)) continue;
            if (i<o->sampled_particle_count) ++sampled_retained;
            o->particles.values.q2[retained++]=p;
        }
        o->particles.count=retained; o->sampled_particle_count=sampled_retained;
        for (size_t i=0;i<Q2FX_POOL;++i) {
            q2fx_sustain *row=&o->sustains[i];
            if (!row->active) continue;
            if (row->end<s->milliseconds) { row->active=false; continue; }
            if (advance && row->next<=s->milliseconds) {
                if (!row->kind) {
                    bool emitted=frontend_fx_q2_steam(&o->particles,&o->random,row->origin,row->direction,(uint32_t)row->color,row->count,(float)row->magnitude,s->milliseconds*.001,false);
                    if (emitted || o->source.profile==FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE) row->next+=100;
                } else { bool nuke=row->kind==2; double ratio=1-(row->end-s->milliseconds)/(nuke?1000:2100);
                    radial_particles(o,row->origin,s->milliseconds,nuke?700:300,(float)((nuke?200:45)*ratio),0,nuke?110:0,true); }
            }
        }
        for (size_t i=0;i<Q2FX_POOL;++i) {
            q2fx_light *row=&o->lights[i]; if (!row->active) continue;
            if (row->die<s->milliseconds) { row->active=false; continue; }
            float radius;
            if (o->source.profile==FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE && row->die>row->born)
                radius=(float)(row->radius*(1-(s->milliseconds-row->born)/(row->die-row->born)));
            else radius=(float)(row->radius-(s->milliseconds-row->born)*.001*row->decay);
            q2fx_sampled_light(o,row->origin,radius,row->color,row->minimum);
        }
        bool ok=q2fx_entities(o,s,trails,advance,e) && q2fx_prepare_beams(o,o->beams,Q2FX_POOL,s,&controls,advance,e) &&
            q2fx_prepare_beams(o,o->player_beams,Q2FX_POOL,s,&controls,advance,e) &&
            q2fx_semantic_prepare(o,s,&controls,advance,e);
        for (size_t i=0;ok && i<Q2FX_POOL;++i) {
            q2fx_explosion *row=&o->explosions[i]; if (!row->active) continue;
            if (row->kind==4) {
                if (s->milliseconds-row->start>50) { row->active=false; continue; }
                ok=model_draw(o,(q2fx_model)row->model,row->origin,row->angles,0,0,0,row->skin,row->flags,1,row->scale,e);
                continue;
            }
            double fraction=(s->milliseconds-row->start)/100, floored=floor(fraction);
            if (floored>=row->frames-1) { row->active=false; continue; }
            int32_t frame=floored<0?0:(int32_t)floored, skin=row->skin; uint32_t flags=row->flags;
            float alpha=row->kind==2?1:row->kind==1 || row->kind==5?(float)(1-fraction/(row->frames-1)):(16-(float)frame)/16;
            if (row->kind==3) {
                if (o->source.profile==FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE) {
                    double fade=fraction/(row->frames-1); alpha=(float)(1-fade*fade*fade); flags|=32;
                }
                skin=frame<10?frame>>1:frame<13?5:6; if (frame>=10) flags|=32;
            }
            if (row->light) q2fx_sampled_light(o,row->origin,row->light*alpha,row->light_color,0);
            if (!(flags&128) && row->kind!=5) ok=model_draw(o,(q2fx_model)row->model,row->origin,row->angles,row->base+frame+1,row->base+frame,
                o->source.profile==FRONTEND_REMOTE_Q2_EFFECTS_RERELEASE?(float)(1-(fraction-frame)):1-s->fraction,skin,flags,alpha,row->scale,e);
        }
        o->transient_light_count=o->light_count;
        --o->busy;
        if (!ok) { free(trails); return false; }
        free(o->trails); o->trails=trails; o->trail_count=s->entity_count;
        o->sampled_particle_count=o->particles.count; o->time=s->milliseconds; o->server_time=s->server_milliseconds;
        o->frame_sequence=s->frame_sequence; o->sampled=true; o->dirty=false;
        o->render_frame=render_frame;
        o->sampled_dlight_hacks=controls.dlight_hacks; o->sampled_disable_particles=controls.disable_particles;
        o->sampled_gun=controls.gun; o->sampled_gun_fov=controls.gun_fov;
    }
    return frontend_remote_q2_effects_view_lights(o,s,lights_out,count_out,e);
}
bool frontend_remote_q2_effects_draw(frontend_remote_q2_effects *o,
    const frontend_remote_q2_effects_sample *s, bool particles, bool entities, qa_scene_frame *frame, qa_error *e)
{
    if (!o || !frontend_remote_q2_effects_idle(o) || !s || !frame || !o->sampled || o->dirty || o->time!=s->milliseconds ||
        o->frame_sequence!=s->frame_sequence || !q2fx_source_current(o,e)) return false;
    ++o->busy; bool ok=true;
    if (entities) for (size_t i=0;ok && i<o->draw_count;++i) {
        const q2fx_model_draw *row=&o->draws[i]; qa_vec3 basis[3]; axes(row->angles,basis);
        qa_model_transform transform; qa_model_transform_identity(&transform);
        transform.origin[0]=row->origin.x; transform.origin[1]=row->origin.y; transform.origin[2]=row->origin.z;
        for (size_t j=0;j<3;++j) { transform.axes[j][0]=basis[j].x; transform.axes[j][1]=basis[j].y; transform.axes[j][2]=basis[j].z;
            transform.scale[j]=j==0?row->scale.x:j==1?row->scale.y:row->scale.z; }
        qa_vec3 ambient=qa_v3(1,1,1), directed=qa_v3(0,0,0), direction=qa_v3(0,0,1);
        if (!(row->flags&8)) {
            if (!s->world_input) { ok=q2fx_fail(e,QA_ERROR_ARGUMENT,"Q2 transient model lost its actual world lighting scope"); break; }
            if (!s->world_input->no_world && !qa_scene_world_sample_light_input(o->source.world,s->world_input,row->origin,&ambient,&directed,&direction,e)) { ok=false; break; }
        }
        qa_scene_model_input input={.view=s->view,.transform=transform,.previous_origin=row->origin,.color={1,1,1,row->alpha},
            .family=QA_SCENE_Q2,.frame=(uint32_t)row->frame,.old_frame=(uint32_t)row->old_frame,.skin=(uint32_t)row->skin,
            .flags=row->flags,.back_lerp=row->back_lerp,.seconds=s->milliseconds*.001,.material_library=o->source.materials,
            .ambient=qa_vec_add(ambient,directed),.light_direction=direction,.source_path=q2fx_model_paths[row->model],
            .video_frame=o->source.video_frame,.video_context=o->source.video_context};
        ok=qa_scene_model_submit(o->models[row->model],&input,frame,e);
    }
    qa_bytes palette={0};
    if (ok && (particles || entities)) ok=qa_scene_resources_palette_read(o->source.images,QA_SCENE_Q2,&palette) && palette.size>=768;
    if (entities) for (size_t i=0;ok && i<Q2FX_LASER_CAPACITY;++i) {
        const q2fx_laser *row=&o->lasers[i]; if (!row->active || row->die<=s->milliseconds) continue;
        uint32_t index=row->color&255; qa_scene_vec4 color;
        if (row->color==UINT32_MAX) {
            if (row->die<=row->born) continue;
            float alpha=(float)((row->die-s->milliseconds)/(row->die-row->born));
            color=(qa_scene_vec4){(float)(row->rgba&255)/255.f,(float)((row->rgba>>8)&255)/255.f,(float)((row->rgba>>16)&255)/255.f,
                floorf((float)(row->rgba>>24)*alpha)/255.f};
        } else color=(qa_scene_vec4){palette.data[index*3]/255.f,palette.data[index*3+1]/255.f,palette.data[index*3+2]/255.f,.3f};
        ok=qa_scene_beam(frame,&s->view,row->start,row->end,row->width,color,o->source.white,e);
    }
    if (entities && ok) ok=q2fx_semantic_draw(o,s,frame,e);
    if (particles) for (size_t i=o->particles.count;ok && i>0;--i) {
        const frontend_fx_q2_particle *row=&o->particles.values.q2[i-1]; qa_vec3 origin; float alpha;
        if (!frontend_fx_q2_sample(row,s->milliseconds,&origin,&alpha)) continue;
        uint32_t index=row->color&255; qa_scene_vec4 color;
        if (row->color==UINT32_MAX) color=(qa_scene_vec4){(float)(row->rgba&255)/255.f,(float)((row->rgba>>8)&255)/255.f,
            (float)((row->rgba>>16)&255)/255.f,floorf((float)(row->rgba>>24)*alpha)/255.f};
        else color=(qa_scene_vec4){palette.data[index*3]/255.f,palette.data[index*3+1]/255.f,palette.data[index*3+2]/255.f,alpha};
        ok=qa_scene_indexed_particle(frame,&s->view,QA_SCENE_Q2,origin,1,color,o->particle_image,e);
    }
    --o->busy; return ok && q2fx_source_current(o,e);
}
bool frontend_remote_q2_effects_entity_beam(frontend_remote_q2_effects *o,
    const qa_scene_view *view, qa_vec3 start, qa_vec3 end, uint32_t packed_colors,
    int32_t width, qa_scene_frame *frame, qa_error *e)
{
    if (!o || !frontend_remote_q2_effects_idle(o) || !view || !frame ||
        !qa_vec_finite(start) || !qa_vec_finite(end) || !q2fx_source_current(o,e)) return false;
    qa_bytes palette={0};
    if (!qa_scene_resources_palette_read(o->source.images,QA_SCENE_Q2,&palette) || palette.size<768) return false;
    ++o->busy;
    uint32_t color=(packed_colors>>((random_word(o)%4)*8))&255;
    qa_scene_vec4 rgba={palette.data[color*3]/255.f,palette.data[color*3+1]/255.f,palette.data[color*3+2]/255.f,.3f};
    float diameter=(float)(width/2)*2;
    bool ok=qa_scene_beam(frame,view,start,end,diameter,rgba,o->source.white,e);
    --o->busy; return ok && q2fx_source_current(o,e);
}
