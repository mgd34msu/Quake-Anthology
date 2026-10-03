#include "internal.h"
#include <float.h>
#include <limits.h>

static q1_actor *brush(qa_q1_game *g, qa_actor_id id) {
    q1_actor *e = q1_entity(g, id);
    return e && e->map && q1_map_is_addon_brush(e->map->kind) ? e : NULL;
}
static bool cooldown(q1_actor *e, double value, qa_error *error) {
    if (!isfinite(value) || fabs(value) >= 0x1.ffffffp127)
        return q1_map_fail(error, "Q1 addon brush cooldown exceeds native range");
    e->map->cooldown = fabs(value) > FLT_MAX
                           ? (value < 0 ? -FLT_MAX : FLT_MAX) : (float)value;
    return true;
}
static bool publish(qa_q1_game *g, qa_actor_id id, const qa_body_state *body, qa_error *error) {
    if (!qa_world_body_write(g->services.world, id, body, error))
        return false;
    q1_actor *e = brush(g, id);
    return !e || q1_link(g, e, error);
}
static void push_brush(q1_actor *e) {
    e->physics.solid = QA_PHYSICS_BRUSH;
    e->physics.motion = QA_PHYSICS_PUSH;
}
static bool model_first(q1_actor *e, qa_error *error) {
    if (!isfinite(e->count) || e->count < (double)INT32_MIN ||
        e->count >= (double)INT32_MAX+1)
        return q1_map_fail(error,"Addon model starting frame exceeds native range");
    e->frame = (int32_t)e->count;
    return true;
}
static qa_vec3 random_vector(qa_q1_game *g, qa_vec3 scale) {
    float x = (q1_random(g)*2-1)*scale.x;
    float y = (q1_random(g)*2-1)*scale.y;
    float z = (q1_random(g)*2-1)*scale.z;
    return qa_v3(x,y,z);
}
static bool bob(qa_q1_game *g, q1_actor *e, bool frame, qa_error *error) {
    qa_actor_id id = e->id;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world,id,&body,error))
        return false;
    e = brush(g,id);
    if (!e)
        return true;
    double delta = e->physics.angular_velocity.x*(frame ? g->elapsed : .05);
    if (!isfinite(delta) || fabs(delta) > FLT_MAX)
        return q1_map_fail(error,"Addon bob frame step exceeds native range");
    float step = (float)delta;
    if (fabs((double)e->count + step) > FLT_MAX)
        return q1_map_fail(error,"Addon bob phase exceeds native range");
    float phase = e->count + step;
    e->count = fmodf(phase,360);
    qa_vec3 position = qa_vec_add(qa_vec_scale(e->map->dest,sinf(e->count)),
                                  qa_vec_scale(e->map->dest2,cosf(e->count)));
    if (frame)
        body.origin = position;
    else
        body.velocity = qa_vec_scale(qa_vec_sub(position,body.origin),20);
    if (!publish(g,id,&body,error))
        return false;
    e = brush(g,id);
    return !e || frame || q1_map_schedule(g,e,.05,Q1_MAP_ADDON_BOB_STEP,error);
}
static float mod_angle(float value) {
    if (value > 360)
        return (float)((double)value-ceil(((double)value-360)/360)*360);
    if (value < 0)
        return (float)((double)value+ceil(-(double)value/360)*360);
    return value;
}
bool q1_map_addon_brush_frame(qa_q1_game *g, q1_actor *e, qa_error *error) {
    if (e->map->kind == Q1_MAP_ADDON_BOB)
        return bob(g,e,true,error);
    if (e->map->kind != Q1_MAP_ADDON_ROTATE)
        return q1_map_fail(error,"Invalid addon brush frame continuation");
    qa_actor_id id = e->id;
    uint8_t phase = e->map->pending.brush.phase;
    float scale = 1;
    if (phase == 1 || phase == 3) {
        double delta = e->map->distance*g->elapsed;
        if (delta > FLT_MAX)
            e->speed = 1;
        else if (delta < -FLT_MAX)
            e->speed = 0;
        else {
            float step = (float)delta;
            double sum = (double)e->speed + step;
            if (sum > FLT_MAX)
                e->speed = 1;
            else if (sum < -FLT_MAX)
                e->speed = 0;
            else {
                float value = e->speed + step;
                e->speed = fmaxf(0,fminf(1,value));
            }
        }
        if (e->speed == 0) {
            e->map->pending.brush.phase = 0;
            q1_map_frame_tick_remove(g,id);
            return true;
        }
        if (e->speed == 1)
            e->map->pending.brush.phase = 2;
        scale = e->speed;
    }
    qa_vec3 rate = e->physics.angular_velocity;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world,id,&body,error))
        return false;
    e = brush(g,id);
    if (!e)
        return true;
    body.angles = qa_vec_add(body.angles,qa_vec_scale(rate,(float)(g->elapsed*scale)));
    body.angles.x = mod_angle(body.angles.x);
    body.angles.y = mod_angle(body.angles.y);
    /* The donor's ModAngles leaves negative z unchanged. */
    if (body.angles.z > 360)
        body.angles.z = mod_angle(body.angles.z);
    return publish(g,id,&body,error);
}
bool q1_map_addon_brush_use(qa_q1_game *g, q1_actor *e, qa_error *error) {
    qa_actor_id id = e->id;
    switch (e->map->kind) {
    case Q1_MAP_ADDON_BOB: {
        bool active = e->map->pending.brush.phase != 0;
        bool solid = e->physics.solid != QA_PHYSICS_NOT_SOLID;
        e->map->pending.brush.phase = active ? 0 : 1;
        if (!solid) {
            if (active) {
                q1_map_frame_tick_remove(g,id);
                return true;
            }
            return q1_map_frame_tick_add(g,id,error);
        }
        if (!active)
            return bob(g,e,false,error);
        q1_map_cancel(g,e);
        qa_body_state body;
        if (!qa_world_body_read(g->services.world,id,&body,error))
            return false;
        if (!brush(g,id))
            return true;
        body.velocity = qa_v3(0,0,0);
        return publish(g,id,&body,error);
    }
    case Q1_MAP_ADDON_TOSS:
        e->map->use_enabled = false;
        return e->delay != 0 ? q1_map_schedule(g,e,e->delay,Q1_MAP_ADDON_TOSS_START,error)
                             : q1_map_addon_brush_think(g,e,Q1_MAP_ADDON_TOSS_START,error);
    case Q1_MAP_ADDON_SHATTER: {
        e->map->use_enabled = false;
        e->physics.solid = QA_PHYSICS_NOT_SOLID;
        e->physics.motion = QA_PHYSICS_TOSS;
        qa_vec3 velocity = e->map->movedir;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world,id,&body,error))
            return false;
        if (!brush(g,id))
            return true;
        body.velocity = velocity;
        return publish(g,id,&body,error);
    }
    case Q1_MAP_ADDON_DEBRIS:
        return q1_map_schedule(g,e,q1_random(g),Q1_MAP_ADDON_DEBRIS_WAKE,error);
    case Q1_MAP_ADDON_HURT:
        e->map->pending.brush.phase = !e->map->pending.brush.phase;
        e->map->cooldown = 0;
        return true;
    case Q1_MAP_ADDON_FADE:
        if ((int64_t)1-e->frame > INT32_MAX)
            return q1_map_fail(error,"Addon wall frame exceeds native range");
        e->frame = (int32_t)((int64_t)1-e->frame);
        return true;
    case Q1_MAP_ADDON_MODEL:
        if (e->spawnflags & 2) {
            if (!model_first(e,error))
                return false;
            return q1_map_schedule(g,e,.1,Q1_MAP_ADDON_MODEL_ONCE,error);
        }
        e->spawnflags ^= 4;
        if (e->spawnflags & 4) {
            q1_map_cancel(g,e);
            return true;
        }
        return q1_map_addon_brush_think(g,e,Q1_MAP_ADDON_MODEL_LOOP,error);
    case Q1_MAP_ADDON_ROTATE: {
        uint8_t phase = e->map->pending.brush.phase;
        if (e->delay <= 0) {
            e->map->pending.brush.phase = phase == 0 ? 2 : 0;
            if (phase != 0) {
                q1_map_frame_tick_remove(g,id);
                return true;
            }
            return q1_map_frame_tick_add(g,id,error);
        }
        bool start = phase == 0 || phase == 3;
        e->map->pending.brush.phase = start ? 1 : 3;
        e->map->distance = fabsf(e->map->distance)*(start ? 1 : -1);
        return phase != 0 || q1_map_frame_tick_add(g,id,error);
    }
    default:
        return true;
    }
}
bool q1_map_addon_brush_touch(qa_q1_game *g, q1_actor *e, qa_actor_id other, qa_error *error) {
    if (e->map->kind != Q1_MAP_ADDON_HURT || !e->map->pending.brush.phase ||
        e->map->cooldown > g->time)
        return true;
    qa_actor_id id = e->id;
    if (q1_health(g,other) <= 0 || !q1_damageable(g,other))
        return true;
    qa_builtin_actor_traits traits = {0};
    bool found = g->services.actor_traits(g->services.context,other,&traits);
    e = brush(g,id);
    if (!e || !q1_alive(g,other) || !found || (!traits.player && !traits.monster))
        return true;
    float amount = e->damage, wait = e->wait;
    qa_actor_id world = g->maps->world_actor;
    if (!q1_damage(g,other,id,world,amount,QA_Q1_WEAPON_COUNT,error))
        return false;
    e = brush(g,id);
    return !e || cooldown(e, g->time + wait, error);
}
bool q1_map_addon_brush_blocked(qa_q1_game *g, q1_actor *e, qa_actor_id other, qa_error *error) {
    if (e->map->kind != Q1_MAP_ADDON_BOB || e->map->cooldown > g->time)
        return true;
    qa_actor_id id = e->id;
    float damage = e->damage;
    if (!q1_damage(g,other,id,id,damage,QA_Q1_WEAPON_COUNT,error))
        return false;
    e = brush(g,id);
    return !e || cooldown(e, g->time + .5, error);
}
bool q1_map_addon_brush_reaction(qa_q1_game *g, q1_actor *e,
                                 const qa_damage_outcome *outcome, qa_error *error) {
    qa_actor_id id = e->id;
    qa_reaction reaction = outcome->result.reaction;
    if (e->map->kind == Q1_MAP_ADDON_EXPLODE && reaction == QA_REACTION_DEATH) {
        e->activator = outcome->request.attack.attacker;
        if (!q1_map_damageable(g,e,false,error))
            return false;
        e = brush(g,id);
        return !e || q1_map_schedule(g,e,.15,Q1_MAP_ADDON_EXPLODE_FIRE,error);
    }
    if (e->map->kind != Q1_MAP_ADDON_BREAKABLE)
        return true;
    if (reaction == QA_REACTION_DEATH)
        return q1_remove(g,e,error);
    if (reaction != QA_REACTION_PAIN)
        return true;
    if (!qa_combat_set_health(g->services.combat,id,10000,error))
        return false;
    e = brush(g,id);
    if (!e)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world,id,&body,error))
        return false;
    if (!brush(g,id))
        return true;
    body.velocity = qa_v3(0,0,-20);
    if (!publish(g,id,&body,error))
        return false;
    e = brush(g,id);
    return !e || q1_map_schedule(g,e,1,Q1_MAP_ADDON_BREAKABLE_STOP,error);
}
bool q1_map_addon_brush_think(qa_q1_game *g, q1_actor *e, q1_map_action action,
                              qa_error *error) {
    if (!q1_map_addon_action_matches(e->map->kind,action))
        return q1_map_fail(error,"Addon brush callback has the wrong source owner");
    qa_actor_id id = e->id;
    if (action == Q1_MAP_ADDON_BOB_STEP)
        return bob(g,e,false,error);
    if (action == Q1_MAP_ADDON_MODEL_LOOP || action == Q1_MAP_ADDON_MODEL_ONCE) {
        if (e->frame == INT32_MAX)
            return q1_map_fail(error,"Addon model animation exceeds native frame range");
        ++e->frame;
    if (action == Q1_MAP_ADDON_MODEL_LOOP && (float)e->frame == e->map->counter_value &&
            !model_first(e,error))
            return false;
    return action == Q1_MAP_ADDON_MODEL_ONCE && (float)e->frame >= e->map->counter_value
                   ? true : q1_map_schedule(g,e,.1,action,error);
    }
    if (action == Q1_MAP_ADDON_TOSS_CASCADE) {
        qa_string_id targetname = e->targetname;
        qa_body_state self;
        if (!qa_world_body_read(g->services.world,id,&self,error))
            return false;
        if (!brush(g,id))
            return true;
        qa_vec3 position = qa_vec_scale(qa_vec_add(self.bounds.mins,self.bounds.maxs),.5f);
        double nearest = 16384;
        q1_actor_snapshot *list;
        if (!q1_snapshot_actors(g,&list,error))
            return false;
        bool ok = true;
        for (size_t i = 0; i < list->count; ++i) {
            q1_actor *other = q1_entity(g,list->actors[i]);
            if (!other || !other->map || other->targetname != targetname)
                continue;
            qa_bytes marker = qa_strings_text(qa_session_strings(g->services.session),other->map->netname);
            if (marker.size != 12 || memcmp(marker.data,"_toss_origin",12))
                continue;
            float speed = other->speed;
            qa_body_state body;
            if (!qa_world_body_read(g->services.world,other->id,&body,error)) {
                ok = false;
                break;
            }
            if (!brush(g,id))
                break;
            if (!q1_alive(g,list->actors[i]))
                continue;
            qa_vec3 center = qa_vec_scale(qa_vec_add(body.bounds.mins,body.bounds.maxs),.5f);
            double distance = qa_vec_length(qa_vec_sub(position,center))/speed;
            if (distance < nearest)
                nearest = distance;
        }
        list->borrowed = false;
        e = brush(g,id);
        if (ok && e && nearest < 16384)
            e->delay = (float)nearest;
        return ok;
    }
    qa_body_state body;
    if (!qa_world_body_read(g->services.world,id,&body,error))
        return false;
    e = brush(g,id);
    if (!e)
        return true;
    switch (action) {
    case Q1_MAP_ADDON_TOSS_START: {
        e->physics.solid = QA_PHYSICS_BOX;
        e->physics.motion = e->spawnflags & 4 ? QA_PHYSICS_TOSS : QA_PHYSICS_BOUNCE;
        body.velocity = e->map->movedir;
        body.bounds.mins = qa_vec_add(body.bounds.mins,qa_v3(4,4,0));
        qa_string_id sound = e->map->noise[0];
        if (!publish(g,id,&body,error))
            return false;
        e = brush(g,id);
        if (!e)
            return true;
        if (sound && !q1_sound_resource(g,id,sound,0,1,1,error))
            return false;
        e = brush(g,id);
        return !e || q1_map_schedule(g,e,.25,Q1_MAP_ADDON_TOSS_STOP,error);
    }
    case Q1_MAP_ADDON_TOSS_STOP:
        if (qa_vec_length(body.velocity) >= .01f)
            return q1_map_schedule(g,e,.5,action,error);
        if ((e->spawnflags & 2) && (!(e->spawnflags & 8) ||
            qa_vec_length(qa_vec_sub(e->map->pending.brush.origin,body.origin)) > 64))
            return q1_remove(g,e,error);
        body.velocity = qa_v3(0,0,0);
        push_brush(e);
        return publish(g,id,&body,error);
    case Q1_MAP_ADDON_DEBRIS_WAKE:
        e->map->use_enabled = false;
        e->physics.solid = QA_PHYSICS_NOT_SOLID;
        e->physics.motion = QA_PHYSICS_TOSS;
        e->alpha = 1;
        body.velocity.x = (q1_random(g)*2-1)*8;
        body.velocity.y = (q1_random(g)*2-1)*8;
        if (!publish(g,id,&body,error))
            return false;
        e = brush(g,id);
        return !e || q1_map_schedule(g,e,e->delay,Q1_MAP_ADDON_DEBRIS_FADE,error);
    case Q1_MAP_ADDON_DEBRIS_FADE: {
        if (e->wait == 0)
            return q1_map_fail(error,"Addon debris fade requires a nonzero period");
        double reciprocal = 1.0/(double)e->wait;
        if (!isfinite(reciprocal) || fabs(reciprocal) > FLT_MAX)
            return q1_map_fail(error,"Addon debris fade rate exceeds native range");
        float rate = (float)reciprocal;
        double delta = rate*g->elapsed;
        if (!isfinite(delta) || fabs(delta) > FLT_MAX)
            return q1_map_fail(error,"Addon debris fade step exceeds native range");
        float step = (float)delta;
        double remaining = (double)e->alpha - step;
        if (remaining > FLT_MAX)
            return q1_map_fail(error,"Addon debris alpha exceeds native range");
        bool reset = remaining < -FLT_MAX;
        if (!reset)
            e->alpha -= step;
        if (reset || e->alpha < 0) {
            e->alpha = 1;
            body.origin = e->map->pending.brush.origin;
            e->physics.motion = QA_PHYSICS_TOSS;
            float x = (q1_random(g)*2-1)*8;
            float y = (q1_random(g)*2-1)*8;
            body.velocity = qa_v3(x,y,0);
            if (!publish(g,id,&body,error))
                return false;
            e = brush(g,id);
            return !e || q1_map_schedule(g,e,e->delay,action,error);
        }
        return q1_map_schedule(g,e,.01,action,error);
    }
    case Q1_MAP_ADDON_EXPLODE_FIRE: {
        qa_vec3 position = qa_vec_scale(qa_vec_add(body.bounds.mins,body.bounds.maxs),.5f);
        qa_actor_id activator = e->activator;
        e->model = QA_STRING_NONE;
        body.origin = position;
        if (!publish(g,id,&body,error))
            return false;
        if (!brush(g,id))
            return true;
        if (!q1_radius(g,id,id,160,(qa_actor_id){0},QA_Q1_WEAPON_COUNT,error))
            return false;
        if (!brush(g,id))
            return true;
        if (!q1_sound(g,id,"weapons/r_exp3.wav",0,1,error))
            return false;
        if (!brush(g,id))
            return true;
        if (!q1_effect(g,QA_BUILTIN_EXPLOSION,id,position,0,0,error))
            return false;
        e = brush(g,id);
        if (!e)
            return true;
        if (!q1_map_targets(g,e,activator,error))
            return false;
        e = brush(g,id);
        return !e || q1_remove(g,e,error);
    }
    case Q1_MAP_ADDON_BREAKABLE_STOP:
        body.velocity = qa_v3(0,0,0);
        return publish(g,id,&body,error);
    default:
        return q1_map_fail(error,"Invalid addon brush continuation");
    }
}
bool q1_map_addon_brush_spawn(qa_q1_game *g, q1_actor *e, qa_error *error) {
    qa_actor_id id = e->id;
    q1_map_kind kind = e->map->kind;
    if (kind == Q1_MAP_ADDON_AXIS)
        return q1_remove(g,e,error);
    qa_body_state body;
    if (!qa_world_body_read(g->services.world,id,&body,error))
        return false;
    e = brush(g,id);
    if (!e)
        return true;
    bool inline_required = kind != Q1_MAP_ADDON_MODEL &&
        !(kind == Q1_MAP_ADDON_BOB && (e->spawnflags & 1)) &&
        !(kind == Q1_MAP_ADDON_ROTATE && !(e->spawnflags & 4));
    if (inline_required && !e->map->has_inline_model)
        return q1_map_fail(error,"Addon brush requires an inline model");
    e->map->pending.brush.origin = body.origin;
    e->map->use_enabled = true;
    push_brush(e);
    switch (kind) {
    case Q1_MAP_ADDON_BOB:
        if (qa_vec_length(e->map->dest) == 0)
            e->map->dest = qa_v3(0,0,64);
        if (e->wait == 0) e->wait = 10;
        if (e->damage == 0) e->damage = 1;
        e->physics.angular_velocity = qa_v3(360/e->wait,0,0);
        e->count = 360*e->delay;
        if (!qa_vec_finite(e->physics.angular_velocity) || !isfinite(e->count))
            return q1_map_fail(error,"Addon bob phase or rate exceeds native range");
        if (e->spawnflags & 1) {
            e->physics.solid = QA_PHYSICS_NOT_SOLID;
            e->physics.motion = QA_PHYSICS_STATIONARY;
        }
        break;
    case Q1_MAP_ADDON_TOSS:
        if (qa_vec_length(e->map->movedir) == 0)
            e->map->movedir = qa_v3(0,0,200);
        if (qa_vec_length(e->map->dest) != 0)
            e->map->movedir = qa_vec_add(e->map->movedir,random_vector(g,e->map->dest));
        if (e->spawnflags & 1) {
            if (e->speed == 0) e->speed = 200;
            if (!qa_builtin_resource(&g->services,"_toss_origin",&e->map->netname,error))
                return false;
        }
        break;
    case Q1_MAP_ADDON_SHATTER: {
        qa_vec3 center = qa_vec_add(body.origin,qa_vec_scale(qa_vec_add(body.bounds.mins,body.bounds.maxs),.5f));
        qa_vec3 destination = e->map->pos2;
        if (qa_vec_length(destination) == 0)
            destination = qa_vec_sub(body.origin,qa_v3(0,0,100));
        qa_vec3 delta = qa_vec_sub(center,destination);
        float length = qa_vec_length(delta);
        if (length == 0)
            return q1_map_fail(error,"Addon shatter origin equals its destination");
        e->speed = (e->speed != 0 ? e->speed : 200)*10000/(length*length);
        if (e->wait == 0) e->wait = 10;
        e->map->movedir = qa_vec_add(qa_vec_scale(qa_vec_normalize(delta),e->speed),
                                     random_vector(g,qa_v3(e->wait,e->wait,e->wait)));
        break;
    }
    case Q1_MAP_ADDON_DEBRIS:
        if (e->delay == 0) e->delay = 1.5f;
        if (e->wait == 0) e->wait = .1f;
        e->map->movedir = qa_v3(0,0,200);
        e->alpha = 1;
        break;
    case Q1_MAP_ADDON_EXPLODE:
    case Q1_MAP_ADDON_BREAKABLE:
        e->map->use_enabled = false;
        e->aimed_damage = kind == Q1_MAP_ADDON_EXPLODE;
        if (kind == Q1_MAP_ADDON_BREAKABLE)
            e->max_health = 10000;
        if (kind == Q1_MAP_ADDON_EXPLODE)
            body.angles = qa_v3(0,0,0);
        if (!qa_combat_set_health(g->services.combat,id,kind == Q1_MAP_ADDON_EXPLODE ? 20 : 10000,error))
            return false;
        e = brush(g,id);
        if (!e)
            return true;
        if (!q1_map_damageable(g,e,true,error))
            return false;
        e = brush(g,id);
        if (!e)
            return true;
        break;
    case Q1_MAP_ADDON_HURT:
        if (e->damage == 0) e->damage = 10;
        if (e->wait == 0) e->wait = .2f;
        e->map->pending.brush.phase = (e->spawnflags & 1) != 0;
        e->map->touch_enabled = true;
        break;
    case Q1_MAP_ADDON_FADE:
        break;
    case Q1_MAP_ADDON_MODEL:
        if (!e->model)
            return q1_map_fail(error,"Addon misc_model requires a model");
        e->physics.solid = QA_PHYSICS_NOT_SOLID;
        e->physics.motion = QA_PHYSICS_STATIONARY;
        e->map->use_enabled = (e->spawnflags & 7) != 0;
        if (!e->spawnflags)
            break;
        if (e->spawnflags && (e->map->counter_value < (float)e->frame || !e->targetname))
            return q1_map_fail(error,"Addon misc_model requires a valid range and targetname");
        e->count = (float)e->frame;
        if (e->count >= (double)INT32_MAX+1 || e->count < (double)INT32_MIN)
            return q1_map_fail(error,"Addon model starting frame exceeds native range");
        break;
    case Q1_MAP_ADDON_ROTATE:
        if (qa_vec_length(e->physics.angular_velocity) == 0)
            e->physics.angular_velocity = qa_v3(0,30,0);
        if (e->delay > 0)
            e->map->distance = 1/e->delay;
        if (!isfinite(e->map->distance))
            return q1_map_fail(error,"Addon rotation rate exceeds native range");
        if (!(e->spawnflags & 4)) {
            e->physics.solid = QA_PHYSICS_NOT_SOLID;
            e->physics.motion = QA_PHYSICS_STATIONARY;
        }
        body.angles = qa_v3(0,0,0);
        if (qa_vec_length(e->map->pos2) != 0)
            body.origin = e->map->pos2;
        e->map->pending.brush.phase = e->spawnflags & 1 ? 0 : 2;
        if (!(e->spawnflags & 1)) e->speed = 1;
        break;
    default:
        return q1_map_fail(error,"Unknown addon brush spawn");
    }
    if (!publish(g,id,&body,error))
        return false;
    e = brush(g,id);
    if (!e)
        return true;
    if (kind == Q1_MAP_ADDON_BOB && (e->spawnflags & 2))
        return q1_map_addon_brush_use(g,e,error);
    if (kind == Q1_MAP_ADDON_TOSS && !(e->spawnflags & 1) && e->delay == 0)
        return q1_map_schedule(g,e,.2,Q1_MAP_ADDON_TOSS_CASCADE,error);
    if (kind == Q1_MAP_ADDON_MODEL && !(e->spawnflags & 2) && (e->spawnflags & 5)) {
        e->spawnflags ^= 4;
        return q1_map_addon_brush_use(g,e,error);
    }
    if (kind == Q1_MAP_ADDON_ROTATE && !(e->spawnflags & 1))
        return q1_map_frame_tick_add(g,id,error);
    return true;
}
