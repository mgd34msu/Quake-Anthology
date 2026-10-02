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
    "models/proj/beam/tris.md2", "models/objects/r_explode2/tris.md2"
};
bool q2fx_fail(qa_error *error, qa_status code, const char *message)
{ if (error) { error->code = code; snprintf(error->message, sizeof(error->message), "%s", message); } return false; }
bool q2fx_source_valid(const frontend_remote_q2_effects_source *s)
{ return s && s->identity && s->content_generation && s->map && s->files &&
    s->images && s->materials && s->world && s->white && s->current && s->actor && s->model && s->sound; }
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
    return a->session == s->session && a->identity == s->identity && a->content_generation == s->content_generation &&
        a->protocol.kind == s->protocol.kind && a->protocol.revision == s->protocol.revision && a->protocol.flags == s->protocol.flags &&
        a->map == s->map && a->files == s->files && a->images == s->images && a->materials == s->materials && a->world == s->world &&
        a->white == s->white && a->context == s->context && a->current == s->current && a->actor == s->actor &&
        a->actor_pose == s->actor_pose && a->model == s->model && a->sound == s->sound &&
        a->hit_marker == s->hit_marker && a->footstep == s->footstep && q2fx_source_current(owner, error);
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
    *out = owner;
    qa_scene_image *image = NULL; qa_bytes palette;
    if (!q2fx_source_current(owner, error) || !qa_scene_particle_image(source->images, QA_SCENE_Q2, &image, error)) return false;
    owner->particle_image = image;
    if (!qa_scene_resources_palette(source->images, QA_SCENE_Q2, &palette, error)) return false;
    for (size_t i = 0; i < Q2FX_MODEL_COUNT; ++i)
        if (!q2fx_model_admit(owner, (q2fx_model)i, error)) return false;
    return q2fx_source_current(owner, error);
}
bool frontend_remote_q2_effects_destroy(frontend_remote_q2_effects **slot, qa_error *error)
{
    if (!slot) return false;
    frontend_remote_q2_effects *owner = *slot;
    if (!owner) return true;
    if (!frontend_remote_q2_effects_idle(owner)) return q2fx_fail(error, QA_ERROR_ARGUMENT, "Q2 effects still own an active source callback or policy");
    qa_scene_image_release(owner->particle_image); free(owner->trails); free(owner->draws); free(owner); *slot = NULL; return true;
}
static uint32_t random_word(frontend_remote_q2_effects *o) { return qa_builtin_random_integer(&o->random); }
static double random_unit(frontend_remote_q2_effects *o) { return (double)(random_word(o) & 32767) / 32767; }
static float random_signed(frontend_remote_q2_effects *o) { return (float)(2 * random_unit(o) - 1); }
static qa_vec3 random_direction(frontend_remote_q2_effects *o)
{
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
static void smoke_flash(frontend_remote_q2_effects *o, qa_vec3 origin, double server)
{
    explosion(o, 1, Q2FX_SMOKE, origin, server - 100, 4, 0, 32, 0, 0, qa_v3(0,0,0), qa_v3(0,0,0), 1);
    explosion(o, 2, Q2FX_FLASH, origin, server - 100, 2, 0, 8, 0, 0, qa_v3(0,0,0), qa_v3(0,0,0), 1);
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
static void beam(frontend_remote_q2_effects *o, qa_actor_id actor, qa_actor_id destination,
    qa_vec3 start, qa_vec3 end, qa_vec3 offset, double time, q2fx_model model, bool player, bool monster)
{
    q2fx_beam *pool = player ? o->player_beams : o->beams, *row = NULL;
    for (size_t i = 0; i < Q2FX_POOL; ++i)
        if (pool[i].active && qa_actor_id_equal(pool[i].actor, actor) &&
            (model != Q2FX_LIGHTNING || qa_actor_id_equal(pool[i].destination, destination))) { row = &pool[i]; break; }
    double duration = player && !row ? 100 : 200;
    if (!row) for (size_t i = 0; i < Q2FX_POOL; ++i)
        if (!pool[i].active || pool[i].die < time) { row = &pool[i]; break; }
    if (row) *row = (q2fx_beam){true, player, monster, (uint8_t)model, actor, destination, start, end, offset, time + duration};
}
static void laser(frontend_remote_q2_effects *o, qa_vec3 start, qa_vec3 end, double time)
{
    for (size_t i = 0; i < Q2FX_POOL; ++i) if (!o->lasers[i].active || o->lasers[i].die < time) {
        uint32_t color = (UINT32_C(0xd0d1d2d3) >> ((random_word(o) % 4) * 8)) & 255;
        o->lasers[i] = (q2fx_laser){true, start, end, time + 100, color, 4}; return;
    }
}
static void radial_particles(frontend_remote_q2_effects *o, qa_vec3 origin, double time,
    int32_t count, float radius, float speed, uint32_t palette, bool instant)
{
    static const uint32_t colors[4] = {16,104,168,144};
    for (int32_t i = 0; i < count && o->particles.count < FRONTEND_FX_PARTICLE_CAPACITY; ++i) {
        uint32_t index = random_word(o) & 3;
        frontend_fx_q2_particle p = {.spawn_milliseconds = time, .alpha = 1,
            .color = palette == 0 ? colors[index] : palette + (palette == 110 ? index * 2 : index)};
        qa_vec3 dir = random_direction(o); p.origin = qa_vec_add(origin, qa_vec_scale(dir, radius));
        p.velocity = qa_vec_scale(dir, speed); p.alpha_velocity = instant ? -10000 : (float)(-.8 / (.5 + random_unit(o) * .3));
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
static void bfg_explosion(frontend_remote_q2_effects *o, qa_vec3 origin, double server)
{ explosion(o, 3, Q2FX_BFG, origin, server - 100, 4, 0, 8 | 32, 0, 350, qa_v3(0,1,0), qa_v3(0,0,0), 1); }
static bool temporary(frontend_remote_q2_effects *o, const qa_q2_temp_entity *t,
    const qa_actor_id actors[7], double time, double server, qa_error *e)
{
    qa_vec3 pos = {0}, end = {0}, dir = {0}, offset = {0}; int32_t count = 0, color = 0, id = 0, magnitude = 0, duration = 0;
    qa_actor_id actor = {0}, destination = {0}; double seconds = time * .001;
    frontend_fx_particles *p = &o->particles; qa_builtin_random *r = &o->random;
    if (t->type != QA_Q2_TE_POWER_SPLASH && t->type != QA_Q2_TE_Q2PRO_DAMAGE_DEALT &&
        !vector_field(t, QA_Q2_TEMP_POSITION1, &pos, e)) return false;
    if (field(t, QA_Q2_TEMP_POSITION2) && !vector_field(t, QA_Q2_TEMP_POSITION2, &end, e)) return false;
    if (field(t, QA_Q2_TEMP_DIRECTION) && !vector_field(t, QA_Q2_TEMP_DIRECTION, &dir, e)) return false;
    if (field(t, QA_Q2_TEMP_COUNT) && !integer_field(t, QA_Q2_TEMP_COUNT, &count, e)) return false;
    if (field(t, QA_Q2_TEMP_COLOR) && !integer_field(t, QA_Q2_TEMP_COLOR, &color, e)) return false;
    switch (t->type) {
    case QA_Q2_TE_BLOOD: case QA_Q2_TE_MOREBLOOD:
        frontend_fx_q2_impact_particles(p,r,pos,dir,0xe8,t->type == QA_Q2_TE_BLOOD ? 60 : 250,seconds,FRONTEND_FX_Q2_NORMAL); break;
    case QA_Q2_TE_GUNSHOT: case QA_Q2_TE_SHOTGUN: case QA_Q2_TE_SPARKS: case QA_Q2_TE_BULLET_SPARKS:
        frontend_fx_q2_impact_particles(p,r,pos,dir,t->type == QA_Q2_TE_GUNSHOT || t->type == QA_Q2_TE_SHOTGUN ? 0 : 0xe0,
            t->type == QA_Q2_TE_GUNSHOT ? 40 : t->type == QA_Q2_TE_SHOTGUN ? 20 : 6,seconds,FRONTEND_FX_Q2_NORMAL);
        if (t->type != QA_Q2_TE_SPARKS) smoke_flash(o,pos,server);
        if (t->type == QA_Q2_TE_GUNSHOT || t->type == QA_Q2_TE_BULLET_SPARKS) {
            uint32_t n = random_word(o) & 15;
            if (n >= 1 && n <= 3) { const char *paths[] = {"world/ric1.wav","world/ric2.wav","world/ric3.wav"}; return sound(o,paths[n-1],pos,actor,time,0,1,1,0,e); }
        } break;
    case QA_Q2_TE_SCREEN_SPARKS: case QA_Q2_TE_SHIELD_SPARKS: case QA_Q2_TE_ELECTRIC_SPARKS:
        frontend_fx_q2_impact_particles(p,r,pos,dir,t->type == QA_Q2_TE_SCREEN_SPARKS ? 0xd0 : t->type == QA_Q2_TE_SHIELD_SPARKS ? 0xb0 : 0x75,40,seconds,FRONTEND_FX_Q2_NORMAL);
        return sound(o,"weapons/lashit.wav",pos,actor,time,0,1,1,0,e);
    case QA_Q2_TE_SPLASH: {
        const uint32_t colors[] = {0,0xe0,0xb0,0x50,0xd0,0xe0,0xe8};
        frontend_fx_q2_impact_particles(p,r,pos,dir,color >= 0 && color <= 6 ? colors[color] : 0,count,seconds,FRONTEND_FX_Q2_NORMAL);
        if (color == 1) { uint32_t n = random_word(o) & 3; const char *paths[] = {"world/spark5.wav","world/spark6.wav","world/spark7.wav"}; return sound(o,paths[n < 2 ? n : 2],pos,actor,time,0,1,3,0,e); } break;
    }
    case QA_Q2_TE_LASER_SPARKS: case QA_Q2_TE_WELDING_SPARKS: case QA_Q2_TE_TUNNEL_SPARKS:
        frontend_fx_q2_impact_particles(p,r,pos,dir,(uint32_t)color,count,seconds,t->type == QA_Q2_TE_TUNNEL_SPARKS ? FRONTEND_FX_Q2_UP : FRONTEND_FX_Q2_FIXED);
        if (t->type == QA_Q2_TE_WELDING_SPARKS) explosion(o,2,Q2FX_FLASH,pos,server-.1,2,0,128,0,100+(float)(random_word(o)%75),qa_v3(1,1,.3f),qa_v3(0,0,0),1);
        break;
    case QA_Q2_TE_GREENBLOOD: frontend_fx_q2_impact_particles(p,r,pos,dir,0xdf,30,seconds,FRONTEND_FX_Q2_FIXED); break;
    case QA_Q2_TE_BLUEHYPERBLASTER: case QA_Q2_TE_BLUEHYPERBLASTER_2:
        frontend_fx_q2_impact_particles(p,r,pos,t->type == QA_Q2_TE_BLUEHYPERBLASTER ? end : dir,0xe0,40,seconds,FRONTEND_FX_Q2_BLASTER); break;
    case QA_Q2_TE_BLASTER: case QA_Q2_TE_BLASTER2: case QA_Q2_TE_FLECHETTE: {
        bool green = t->type == QA_Q2_TE_BLASTER2, blue = t->type == QA_Q2_TE_FLECHETTE;
        frontend_fx_q2_impact_particles(p,r,pos,dir,green ? 0xd0 : blue ? 0x6f : 0xe0,40,seconds,FRONTEND_FX_Q2_BLASTER);
        explosion(o,1,Q2FX_EXPLODE,pos,server-100,4,0,8|32,green ? 1 : blue ? 2 : 0,150,
            green ? qa_v3(0,1,0) : blue ? qa_v3(.19f,.41f,.75f) : qa_v3(1,1,0),direction_angles(dir),1);
        return sound(o,"weapons/lashit.wav",pos,actor,time,0,1,1,0,e);
    }
    case QA_Q2_TE_RAILTRAIL: case QA_Q2_TE_RAILTRAIL2:
        frontend_fx_q2_rail(p,r,pos,end,seconds); return sound(o,"weapons/railgf1a.wav",end,actor,time,0,1,1,0,e);
    case QA_Q2_TE_BUBBLETRAIL: frontend_fx_q2_bubbles(p,r,pos,end,seconds); break;
    case QA_Q2_TE_BUBBLETRAIL2: special_trail(o,pos,end,time,true); return sound(o,"weapons/lashit.wav",pos,actor,time,0,1,1,0,e);
    case QA_Q2_TE_DEBUGTRAIL: special_trail(o,pos,end,time,false); break;
    case QA_Q2_TE_BFG_LASER: case QA_Q2_TE_BFG_ZAP:
        laser(o,pos,end,time); if (t->type == QA_Q2_TE_BFG_ZAP) bfg_explosion(o,end,server); break;
    case QA_Q2_TE_BFG_EXPLOSION: bfg_explosion(o,pos,server); break;
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
        frontend_fx_q2_berserk(p,r,pos,dir,seconds); explosion(o,1,Q2FX_EXPLODE,pos,server-100,4,0,8|32,2,550,qa_v3(.19f,.41f,.75f),direction_angles(dir),3); break;
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
            return q2fx_fail(e,QA_ERROR_ARGUMENT,"Power splash requires its received model bounds");
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
        beam(o,actor,destination,pos,end,offset,time,model,player,t->type==QA_Q2_TE_MONSTER_HEATBEAM);
        if (t->type==QA_Q2_TE_LIGHTNING) return sound(o,"weapons/tesla.wav",pos,actor,time,1,1,1,0,e);
        break;
    }
    case QA_Q2_TE_EXPLOSION1: case QA_Q2_TE_EXPLOSION2: case QA_Q2_TE_ROCKET_EXPLOSION:
    case QA_Q2_TE_GRENADE_EXPLOSION: case QA_Q2_TE_ROCKET_EXPLOSION_WATER: case QA_Q2_TE_GRENADE_EXPLOSION_WATER:
    case QA_Q2_TE_PLASMA_EXPLOSION: case QA_Q2_TE_PLAIN_EXPLOSION: case QA_Q2_TE_EXPLOSION1_BIG:
    case QA_Q2_TE_EXPLOSION1_NP: case QA_Q2_TE_EXPLOSION1_NL: case QA_Q2_TE_EXPLOSION2_NL: {
        bool grenade=t->type==QA_Q2_TE_EXPLOSION2 || t->type==QA_Q2_TE_EXPLOSION2_NL || t->type==QA_Q2_TE_GRENADE_EXPLOSION || t->type==QA_Q2_TE_GRENADE_EXPLOSION_WATER;
        bool water=t->type==QA_Q2_TE_ROCKET_EXPLOSION_WATER || t->type==QA_Q2_TE_GRENADE_EXPLOSION_WATER;
        bool big=t->type==QA_Q2_TE_EXPLOSION1_BIG, unlit=t->type==QA_Q2_TE_EXPLOSION1_NL || t->type==QA_Q2_TE_EXPLOSION2_NL;
        qa_vec3 angles=qa_v3(0,(float)(random_word(o)%360),0); int32_t base=grenade?30:random_unit(o)<.5?15:0;
        explosion(o,3,big?Q2FX_BIG:Q2FX_ROCKET,pos,server-100,grenade?19:15,base,8,0,unlit?0:350,qa_v3(1,.5f,.5f),angles,1);
        if (!big && t->type!=QA_Q2_TE_EXPLOSION1_NP && t->type!=QA_Q2_TE_PLAIN_EXPLOSION) frontend_fx_q2_explosion(p,r,pos,seconds,false);
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
static bool soldier_flash(uint32_t flash, unsigned kind)
{
    static const uint32_t values[3][8] = {{39,40,83,86,89,92,95,98},{41,42,84,87,90,93,96,99},{43,44,85,88,91,94,97,100}};
    for (size_t i=0;i<8;++i) if (values[kind][i]==flash) return true;
    return false;
}
static bool monster_muzzle(frontend_remote_q2_effects *o, frontend_remote_q2_effects_pose pose,
    uint32_t flash, double time, double server, qa_error *e)
{
    bool rerelease=o->source.protocol.kind==QA_NET_Q2KEX_2023 || o->source.protocol.kind==QA_NET_Q2REPRO_1038 ||
        o->source.protocol.kind==QA_NET_Q2PRIVATE_4038 || o->source.protocol.kind==QA_NET_Q2KEX_DEMO_2022;
    const qa_vec3 *offsets=rerelease?q2m_rerelease_muzzle_offsets:q2m_classic_muzzle_offsets;
    size_t size=rerelease?sizeof(q2m_rerelease_muzzle_offsets)/sizeof(*offsets):sizeof(q2m_classic_muzzle_offsets)/sizeof(*offsets);
    if (!flash || flash>=size-1) return q2fx_fail(e,QA_ERROR_FORMAT,"Received monster muzzle leaves its source offset table");
    qa_vec3 axis[3]; axes(pose.angles,axis); qa_vec3 offset=offsets[flash];
    qa_vec3 origin=qa_vec_sub(qa_vec_add(pose.origin,qa_vec_scale(axis[0],offset.x)),qa_vec_scale(axis[1],offset.y)); origin.z+=offset.z;
    uint32_t profile=flash;
    if (rerelease) {
        if ((flash>=232 && flash<=239) || flash==260) profile=26;
        else if (flash==251) profile=39; else if (flash==252) profile=41; else if (flash==253) profile=43;
        else if ((flash>=256 && flash<=259) || flash==261 || flash==262) profile=53; else if (flash==263) profile=62;
        else if (flash==264 || (flash>=277 && flash<=288)) profile=144;
        else if (flash>=265 && flash<=276) profile=60;
        else if (flash==74 || flash==134) profile=58;
    }
    qa_vec3 color=qa_v3(1,1,0); float radius=200; uint32_t mask=31; double duration=0;
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
    else if (profile==151 || (profile>=195 && profile<=210)) { radius=300; mask=100; duration=200; }
    else if (rerelease && ((flash>=211 && flash<=218) || flash==254)) { color=qa_v3(1,.5f,.5f); path="weapons/rippfire.wav"; }
    else if (rerelease && ((flash>=219 && flash<=226) || flash==255)) { color=qa_v3(0,0,1); path="weapons/hyprbf1a.wav"; }
    else if (rerelease && flash==227) path="weapons/hyprbf1a.wav";
    else if (rerelease && (flash==240 || flash==241)) { color=qa_v3(0,0,1); path="guncmdr/gcdratck2.wav"; }
    else if (rerelease && flash>=242 && flash<=250) { color=qa_v3(1,.5f,0); path="guncmdr/gcdratck3.wav"; }
    else return q2fx_fail(e,QA_ERROR_FORMAT,"Received monster muzzle lacks its source effect definition");
    light(o,pose.actor,origin,time,radius+(float)(random_word(o)&mask),duration,color,0,32);
    if (particles) frontend_fx_q2_impact_particles(&o->particles,&o->random,origin,qa_v3(0,0,0),0,40,time*.001,FRONTEND_FX_Q2_NORMAL);
    if (smoke) smoke_flash(o,origin,server);
    if (tank_sound) { snprintf(random_path,sizeof(random_path),"tank/tnkatk2%c.wav",(int)('a'+random_word(o)%5)); path=random_path; }
    return !path || sound(o,path,origin,pose.actor,time,1,1,attenuation,0,e);
}
static bool player_muzzle(frontend_remote_q2_effects *o, frontend_remote_q2_effects_pose pose,
    uint32_t flash, bool silenced, double time, qa_error *e)
{
    if (!((flash<=20 && flash!=15) || (flash>=30 && flash<=39)))
        return q2fx_fail(e,QA_ERROR_FORMAT,"Received player muzzle lacks its source effect definition");
    qa_vec3 axis[3]; axes(pose.angles,axis);
    qa_vec3 origin=qa_vec_sub(qa_vec_add(pose.origin,qa_vec_scale(axis[0],18)),qa_vec_scale(axis[1],16));
    qa_vec3 color=flash==6?qa_v3(.5f,.5f,1):flash==7?qa_v3(1,.5f,.2f):flash==8 || flash==4 || flash==31?qa_v3(1,.5f,0):
        flash==3?qa_v3(1,.25f,0):flash==12 || flash==19 || flash==34 || flash==9?qa_v3(0,1,0):
        flash==10 || flash==36?qa_v3(1,0,0):flash==35?qa_v3(-1,-1,-1):flash==17 || flash==38?qa_v3(0,0,1):
        flash==39?qa_v3(0,1,1):flash==16 || flash==18 || flash==20?qa_v3(1,.5f,.5f):flash==30?qa_v3(.9f,.7f,0):qa_v3(1,1,0);
    float radius=(silenced?100:200)+(float)(random_word(o)&31);
    if (flash>=3 && flash<=5) radius=(flash==4?225:flash==5?250:200)+(float)(random_word(o)&31);
    double duration=flash==33 || (flash>=36 && flash<=39)?100:flash>=9 && flash<=11?1:flash==4 || flash==5?.1:0;
    light(o,pose.actor,origin,time,radius,duration,color,0,32);
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
        bool extended=o->source.protocol.kind==QA_NET_Q2KEX_2023 || o->source.protocol.kind==QA_NET_Q2REPRO_1038 || o->source.protocol.kind==QA_NET_Q2PRIVATE_4038;
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
    return (!path || sound(o,path,origin,pose.actor,time,1,volume,1,0,e)) &&
        (!reload || sound(o,reload,origin,pose.actor,time,0,volume,1,.1,e));
}
bool frontend_remote_q2_effects_muzzle(frontend_remote_q2_effects *o,
    uint32_t entity, uint32_t flash, bool monster, bool silenced, double time, double server, qa_error *e)
{
    if (!o || !frontend_remote_q2_effects_idle(o) || !isfinite(time) || !isfinite(server) || !entity || !q2fx_source_current(o,e)) return false;
    frontend_remote_q2_effects_pose pose;
    ++o->busy;
    bool ok=o->source.actor(o->source.context,entity,&pose,e) && q2fx_source_current(o,e) && pose.actor.registry && pose.actor.generation &&
        qa_vec_finite(pose.origin) && qa_vec_finite(pose.angles) &&
        (monster?monster_muzzle(o,pose,flash,time,server,e):player_muzzle(o,pose,flash,silenced,time,e));
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
        (monster?monster_muzzle(o,pose,flash,time,server,e):player_muzzle(o,pose,flash,silenced,time,e));
    o->dirty=true; --o->busy; return ok && q2fx_source_current(o,e);
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
    o->draws[o->draw_count++]=(q2fx_model_draw){(uint8_t)model,origin,angles,frame,old_frame,skin,flags,alpha,back_lerp,scale}; return true;
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
static bool prepare_beams(frontend_remote_q2_effects *o, q2fx_beam pool[Q2FX_POOL],
    const frontend_remote_q2_effects_sample *s, bool advance, qa_error *e)
{
    for (size_t i=0;i<Q2FX_POOL;++i) {
        q2fx_beam *b=&pool[i];
        if (!b->active) continue;
        if (b->die<s->milliseconds) { b->active=false; continue; }
        qa_vec3 start=b->start, offset=b->offset, delta;
        bool local=qa_actor_id_equal(b->actor,s->viewer), heat=b->model==Q2FX_HEAT, lightning=b->model==Q2FX_LIGHTNING;
        float hand=s->hand==2?0:s->hand==1?-1:1;
        if (b->player && local) {
            start=qa_vec_add(s->view.origin,s->gun_offset);
            start=qa_vec_add(start,qa_vec_scale(s->view.axis[1],-hand*offset.x));
            start=qa_vec_add(start,qa_vec_scale(s->view.axis[0],offset.y));
            start=qa_vec_add(start,qa_vec_scale(s->view.axis[2],offset.z));
            if (s->hand==2) start=qa_vec_sub(start,s->view.axis[2]);
        } else if (!b->player && local) { start=s->view.origin; start.z-=22; start=qa_vec_add(start,offset); }
        else start=qa_vec_add(start,offset);
        delta=qa_vec_sub(b->end,start);
        if (heat && local) {
            delta=qa_vec_scale(s->view.axis[0],qa_vec_length(delta));
            delta=qa_vec_sub(delta,qa_vec_scale(s->view.axis[1],hand*offset.x));
            delta=qa_vec_add(delta,qa_vec_scale(s->view.axis[0],offset.y));
            delta=qa_vec_add(delta,qa_vec_scale(s->view.axis[2],offset.z));
            if (s->hand==2) start=qa_vec_sub(start,s->view.axis[2]);
        }
        float horizontal=hypotf(delta.x,delta.y), yaw=horizontal==0?0:atan2f(delta.y,delta.x)*57.29577951308232f;
        float pitch=horizontal==0?(delta.z>0?90:270):atan2f(delta.z,horizontal)*-57.29577951308232f;
        if (yaw<0) yaw+=360;
        if (pitch<0) pitch+=360;
        qa_vec3 direction=qa_vec_normalize(delta);
        if (heat && !local) {
            if (!b->monster) {
                qa_vec3 axis[3]; axes(qa_v3(-pitch,yaw+180,0),axis);
                start=qa_vec_add(start,qa_vec_scale(axis[1],offset.x-1));
                start=qa_vec_sub(start,qa_vec_scale(axis[0],offset.y));
                start=qa_vec_add(start,qa_vec_scale(axis[2],-offset.z-10));
            } else if (advance) radial_particles(o,b->start,s->milliseconds,40,10,0,0xe0,true);
        }
        if (heat && local && advance) heat_particles(o,start,direction,s);
        float length=qa_vec_length(delta)-(lightning?20:0), segment=heat?32:lightning?35:30;
        bool short_lightning=lightning && length<=segment;
        if (length<=0 && !short_lightning) continue;
        double steps=short_lightning?1:ceil((double)length/segment);
        if (!isfinite(steps) || steps>(double)(SIZE_MAX/sizeof(*o->draws))) return q2fx_fail(e,QA_ERROR_FORMAT,"Q2 beam exceeds physical model draw capacity");
        float spacing=steps>1?(length-segment)/(float)(steps-1):0;
        for (size_t j=0;j<(size_t)steps;++j) {
            qa_vec3 origin=short_lightning?b->end:qa_vec_add(start,qa_vec_scale(direction,(float)j*spacing));
            qa_vec3 angles=qa_v3(short_lightning || (!lightning && !heat)?pitch:-pitch,
                short_lightning || (!lightning && !heat)?yaw:yaw+180,heat?(float)fmod(s->milliseconds,360):(float)(random_word(o)%360));
            int32_t frame=heat?(local?1:2):0;
            if (!model_draw(o,(q2fx_model)b->model,origin,angles,frame,frame,0,0,heat || lightning?8:0,1,1,e)) return false;
        }
    }
    return true;
}
bool frontend_remote_q2_effects_prepare(frontend_remote_q2_effects *o,
    const frontend_remote_q2_effects_sample *s, const qa_scene_light **lights_out, size_t *count_out, qa_error *e)
{
    if (!o || !frontend_remote_q2_effects_idle(o) || !s || !lights_out || !count_out || !isfinite(s->milliseconds) || !isfinite(s->server_milliseconds) ||
        !isfinite(s->fraction) || s->fraction<0 || s->fraction>1 || (s->entity_count && !s->entities) ||
        s->entity_count>SIZE_MAX/sizeof(q2fx_trail) || !q2fx_source_current(o,e)) return false;
    if (o->sampled && s->milliseconds<o->time) return q2fx_fail(e,QA_ERROR_ARGUMENT,"Remote Q2 source time rewound without replacing its effects owner");
    for (size_t i=0;i<s->entity_count;++i) {
        if (!s->entities[i].actor.registry || !s->entities[i].actor.generation || !qa_vec_finite(s->entities[i].origin) || !qa_vec_finite(s->entities[i].angles)) return false;
        for (size_t j=0;j<i;++j) if (qa_actor_id_equal(s->entities[i].actor,s->entities[j].actor)) return false;
    }
    bool advance=!o->sampled || s->milliseconds>o->time, events=!o->sampled || s->frame_sequence!=o->frame_sequence;
    if (advance || events || o->dirty) {
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
                    if (frontend_fx_q2_steam(&o->particles,&o->random,row->origin,row->direction,(uint32_t)row->color,row->count,(float)row->magnitude,s->milliseconds*.001,false)) row->next+=100;
                } else { bool nuke=row->kind==2; double ratio=1-(row->end-s->milliseconds)/(nuke?1000:2100);
                    radial_particles(o,row->origin,s->milliseconds,nuke?700:300,(float)((nuke?200:45)*ratio),0,nuke?110:0,true); }
            }
        }
        for (size_t i=0;i<Q2FX_POOL;++i) {
            q2fx_light *row=&o->lights[i]; if (!row->active) continue;
            if (row->die<s->milliseconds) { row->active=false; continue; }
            q2fx_sampled_light(o,row->origin,(float)(row->radius-(s->milliseconds-row->born)*.001*row->decay),row->color,row->minimum);
        }
        bool ok=q2fx_entities(o,s,trails,advance,e) && prepare_beams(o,o->beams,s,advance,e) && prepare_beams(o,o->player_beams,s,advance,e);
        for (size_t i=0;ok && i<Q2FX_POOL;++i) {
            q2fx_explosion *row=&o->explosions[i]; if (!row->active) continue;
            double fraction=(s->milliseconds-row->start)/100, floored=floor(fraction);
            if (floored>=row->frames-1) { row->active=false; continue; }
            int32_t frame=floored<0?0:(int32_t)floored, skin=row->skin; uint32_t flags=row->flags;
            float alpha=row->kind==2?1:row->kind==1?(float)(1-fraction/(row->frames-1)):(16-(float)frame)/16;
            if (row->kind==3) { skin=frame<10?frame>>1:frame<13?5:6; if (frame>=10) flags|=32; }
            if (row->light) q2fx_sampled_light(o,row->origin,row->light*alpha,row->light_color,0);
            if (!(flags&128)) ok=model_draw(o,(q2fx_model)row->model,row->origin,row->angles,row->base+frame+1,row->base+frame,1-s->fraction,skin,flags,alpha,row->scale,e);
        }
        --o->busy;
        if (!ok) { free(trails); return false; }
        free(o->trails); o->trails=trails; o->trail_count=s->entity_count;
        o->sampled_particle_count=o->particles.count; o->time=s->milliseconds; o->server_time=s->server_milliseconds;
        o->frame_sequence=s->frame_sequence; o->sampled=true; o->dirty=false;
    }
    *lights_out=o->sampled_lights; *count_out=o->light_count; return q2fx_source_current(o,e);
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
        for (size_t j=0;j<3;++j) { transform.axes[j][0]=basis[j].x; transform.axes[j][1]=basis[j].y; transform.axes[j][2]=basis[j].z; transform.scale[j]=row->scale; }
        qa_vec3 ambient=qa_v3(1,1,1), directed=qa_v3(0,0,0), direction=qa_v3(0,0,1);
        if (!(row->flags&8)) {
            if (!s->world_input) { ok=q2fx_fail(e,QA_ERROR_ARGUMENT,"Q2 transient model lost its actual world lighting scope"); break; }
            if (!s->world_input->no_world && !qa_scene_world_sample_light_input(o->source.world,s->world_input,row->origin,&ambient,&directed,&direction,e)) { ok=false; break; }
        }
        qa_scene_model_input input={.view=s->view,.transform=transform,.previous_origin=row->origin,.color={1,1,1,row->alpha},
            .family=QA_SCENE_Q2,.frame=(uint32_t)row->frame,.old_frame=(uint32_t)row->old_frame,.skin=(uint32_t)row->skin,
            .flags=row->flags,.back_lerp=row->back_lerp,.seconds=s->milliseconds*.001,.material_library=o->source.materials,
            .ambient=qa_vec_add(ambient,directed),.light_direction=direction,.source_path=q2fx_model_paths[row->model]};
        ok=qa_scene_model_submit(o->models[row->model],&input,frame,e);
    }
    qa_bytes palette={0};
    if (ok && (particles || entities)) ok=qa_scene_resources_palette_read(o->source.images,QA_SCENE_Q2,&palette) && palette.size>=768;
    if (entities) for (size_t i=0;ok && i<Q2FX_POOL;++i) {
        const q2fx_laser *row=&o->lasers[i]; if (!row->active || row->die<s->milliseconds) continue;
        uint32_t index=row->color&255; qa_scene_vec4 color={palette.data[index*3]/255.f,palette.data[index*3+1]/255.f,palette.data[index*3+2]/255.f,.3f};
        ok=qa_scene_beam(frame,&s->view,row->start,row->end,row->width,color,o->source.white,e);
    }
    if (particles) for (size_t i=o->particles.count;ok && i>0;--i) {
        const frontend_fx_q2_particle *row=&o->particles.values.q2[i-1]; qa_vec3 origin; float alpha;
        if (!frontend_fx_q2_sample(row,s->milliseconds,&origin,&alpha)) continue;
        uint32_t index=row->color&255; qa_scene_vec4 color={palette.data[index*3]/255.f,palette.data[index*3+1]/255.f,palette.data[index*3+2]/255.f,alpha};
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
    bool ok=qa_scene_beam(frame,view,start,end,(float)width,rgba,o->source.white,e);
    --o->busy; return ok && q2fx_source_current(o,e);
}
