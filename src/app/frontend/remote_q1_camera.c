#include "remote_q1_camera.h"
#include "remote_q1_private.h"
#include "remote_q1_prediction.h"
#include "internal.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static qa_vec3 vector(const float *v) { return qa_v3(v[0], v[1], v[2]); }
static double length(qa_vec3 v) { return sqrt((double)v.x*v.x + (double)v.y*v.y + (double)v.z*v.z); }
static bool setting(const frontend_remote_q1 *row, const char *name)
{ const qa_cvar_view *v = qa_cvars_find(row->options.domain.cvars, name); return v && v->number != 0; }
static bool spectator_info(const char *info)
{
    const char *value = NULL; size_t size = 0;
    const char *p = info ? info : ""; if (*p == '\\') ++p;
    while (*p) {
        const char *key = p; while (*p && *p != '\\') ++p;
        if (!*p) break;
        size_t count = (size_t)(p-key); const char *text = ++p;
        while (*p && *p != '\\') ++p;
        if (count == 10 && !memcmp(key, "*spectator", 10)) { value = text; size = (size_t)(p-text); }
        if (*p) ++p;
    }
    return value && size != 0;
}
static bool eligible(const frontend_remote_q1 *row, unsigned slot)
{
    return slot < 32 && row->clients[slot].userinfo && row->clients[slot].name &&
        *row->clients[slot].name && !spectator_info(row->clients[slot].userinfo);
}
static bool unlock(frontend_remote_q1 *row, qa_error *error)
{
    remote_q1_camera *c = &row->camera;
    if (c->tracking && !qa_network_q1_client_command(row->options.domain.runtime,
        row->options.domain.client, "ptrack", error)) return false;
    c->tracking = c->locked = c->has_view = false; return true;
}
static bool lock(frontend_remote_q1 *row, unsigned slot, qa_error *error)
{
    char text[24]; snprintf(text, sizeof(text), "ptrack %u", slot);
    if (!qa_network_q1_client_command(row->options.domain.runtime, row->options.domain.client, text, error)) return false;
    row->camera.slot = (uint8_t)slot; row->camera.locked = row->camera.has_view = false; return true;
}
static bool high_target(frontend_remote_q1 *row, qa_error *error)
{
    int best = -1; int32_t frags = -9999;
    for (unsigned slot = 0; slot < 32; ++slot)
        if (eligible(row, slot) && row->clients[slot].frags > frags) { best = (int)slot; frags = row->clients[slot].frags; }
    if (best < 0) return unlock(row, error);
    return (row->camera.locked && frags <= row->clients[row->camera.slot].frags) || lock(row, (unsigned)best, error);
}
static bool trace(frontend_remote_q1 *row, qa_vec3 start, qa_vec3 end, qa_trace_result *out, qa_error *error)
{
    if (!remote_q1_prediction_camera_trace(row, start, end, out, error)) return false;
    if (out->start_solid) out->fraction = 0;
    return true;
}
static bool visible(frontend_remote_q1 *row, const qa_qw_player *target, bool *out, qa_error *error)
{
    qa_vec3 origin = vector(target->origin); qa_trace_result result;
    if (!trace(row, origin, row->camera.desired, &result, error)) return false;
    *out = result.fraction == 1 && !result.in_water && length(qa_vec_sub(origin, row->camera.desired)) >= 16;
    return true;
}
static bool flyby(frontend_remote_q1 *row, const qa_qw_player *self, const qa_qw_player *target,
    bool check_visibility, bool *found, qa_error *error)
{
    double yaw = target->command.angles[1] * 0.017453292519943295;
    double roll = target->command.angles[2] * 0.017453292519943295;
    qa_vec3 f = qa_v3((float)cos(yaw), (float)sin(yaw), 0);
    qa_vec3 r = qa_v3((float)(cos(roll)*sin(yaw)), (float)(-cos(roll)*cos(yaw)), (float)-sin(roll));
    qa_vec3 u = qa_v3((float)(sin(roll)*sin(yaw)), (float)(-sin(roll)*cos(yaw)), (float)cos(roll));
    qa_vec3 directions[] = {qa_vec_add(qa_vec_add(f,u),r),qa_vec_sub(qa_vec_add(f,u),r),
        qa_vec_add(f,r),qa_vec_sub(f,r),qa_vec_add(f,u),qa_vec_sub(f,u),
        qa_vec_sub(qa_vec_add(u,r),f),qa_vec_sub(qa_vec_sub(u,r),f),
        qa_v3(-f.x,-f.y,-f.z),f,qa_v3(-r.x,-r.y,-r.z),r};
    double best = 1000; qa_vec3 position = {0}; *found = false;
    qa_vec3 origin = vector(target->origin), own = vector(self->origin);
    for (unsigned i = 0; i < 12; ++i) {
        qa_vec3 d = directions[i]; double magnitude = length(d);
        qa_vec3 unit = qa_v3((float)(d.x/magnitude),(float)(d.y/magnitude),(float)(d.z/magnitude));
        qa_vec3 end = qa_v3((float)(origin.x+800.0*unit.x),(float)(origin.y+800.0*unit.y),(float)(origin.z+800.0*unit.z));
        qa_trace_result hit;
        if (!trace(row, origin, end, &hit, error)) return false;
        if (hit.in_water) continue;
        double distance = length(qa_vec_sub(hit.end, origin));
        if (distance < 32 || distance > 800) continue;
        if (check_visibility) {
            distance = length(qa_vec_sub(hit.end, own)); qa_trace_result sight;
            if (!trace(row, own, hit.end, &sight, error)) return false;
            if (sight.fraction != 1 || sight.in_water) continue;
        }
        if (distance < best) { best = distance; position = hit.end; *found = true; }
    }
    if (*found) { row->camera.locked = true; row->camera.desired = position; }
    return true;
}
static qa_vec3 angles(qa_vec3 v)
{
    if (v.x == 0 && v.y == 0) return qa_v3(v.z > 0 ? -90 : -270, 0, 0);
    double yaw = trunc(atan2(v.y,v.x)*57.29577951308232);
    double pitch = trunc(atan2(v.z,sqrt((double)v.x*v.x+(double)v.y*v.y))*57.29577951308232);
    return qa_v3((float)-(pitch < 0 ? pitch+360 : pitch),(float)(yaw < 0 ? yaw+360 : yaw),0);
}
static bool finish(frontend_remote_q1 *row, const qa_qw_command *command, qa_error *error)
{
    remote_q1_camera *c = &row->camera;
    if (command->buttons & 1) {
        if (c->old_buttons & 1) return true;
        c->old_buttons |= 1;
        if (c->tracking) return unlock(row,error);
        c->tracking = true;
    } else { c->old_buttons &= (uint8_t)~1; if (!c->tracking) return true; }
    if (setting(row,"cl_hightrack")) return high_target(row,error);
    if (c->locked) {
        if ((command->buttons & 2) && (c->old_buttons & 2)) return true;
        if (!(command->buttons & 2)) { c->old_buttons &= (uint8_t)~2; return true; }
        c->old_buttons |= 2;
    }
    unsigned start = c->locked ? ((unsigned)c->slot+1u)%32u : (unsigned)c->slot;
    for (unsigned i = 0; i < 32; ++i) if (eligible(row,(start+i)%32)) return lock(row,(start+i)%32,error);
    frontend_console_print(row->frontend,&row->options.domain.command_context,"No target found ...\n");
    c->tracking = c->locked = c->has_view = false; return remote_q1_live(row,error);
}
bool remote_q1_camera_command(frontend_remote_q1 *row, qa_qw_command *command, uint64_t now, qa_error *error)
{
    if (!row || !command || !row->qw.spectator || row->qw_intermission ||
        !row->qw_player_valid[row->qw.player_slot]) return true;
    remote_q1_camera *c = &row->camera; double seconds = (double)now/1000000000.0;
    remote_q1_camera_view previous = c->view; bool previous_present = c->tracking && c->locked && c->has_view;
    if (setting(row,"cl_hightrack") && !c->locked && !high_target(row,error)) return false;
    if (c->tracking) {
        if (c->locked && !eligible(row,c->slot)) {
            c->locked = false;
            if (setting(row,"cl_hightrack") ? !high_target(row,error) : !unlock(row,error)) return false;
        } else if (row->qw_player_valid[c->slot]) {
            const qa_qw_player *target = row->qw_players+c->slot;
            qa_qw_player own = row->qw_players[row->qw.player_slot];
            if (c->self_present) { own.origin[0]=c->self_origin.x; own.origin[1]=c->self_origin.y; own.origin[2]=c->self_origin.z; }
            const qa_qw_player *self = &own;
            bool seen = false;
            if (c->locked && !visible(row,target,&seen,error)) return false;
            if (!c->locked || !seen) {
                if (!c->locked || seconds-c->last_view_seconds > 0.1) {
                    bool found;
                    if (!flyby(row,self,target,true,&found,error) || (!found && !flyby(row,self,target,false,&found,error))) return false;
                    c->last_view_seconds = seconds;
                }
            } else c->last_view_seconds = seconds;
            if (c->locked) {
                bool chase = setting(row,"cl_chasecam");
                if (chase) c->desired = vector(target->origin);
                qa_vec3 delta = qa_vec_sub(c->desired,vector(self->origin));
                if (chase ? delta.x != 0 || delta.y != 0 || delta.z != 0 : length(delta) > 16) {
                    c->teleport = c->desired; c->has_teleport = true;
                }
                c->view = (remote_q1_camera_view){.origin=c->desired,.angles=chase?vector(target->command.angles):angles(qa_vec_sub(vector(target->origin),c->desired)),
                    .target_slot=c->slot,.target_flags=target->flags,.target_weapon_frame=target->weapon_frame,.chase=chase};
                c->has_view = true;
                command->angles[0]=c->view.angles.x; command->angles[1]=c->view.angles.y; command->angles[2]=c->view.angles.z;
                command->forward=command->side=command->up=0;
            }
        }
    }
    if (!finish(row,command,error)) return false;
    const remote_q1_camera_view *view = remote_q1_camera_read(row);
    if (!view && previous_present) view = &previous;
    if (view) {
        c->self_origin=view->origin; c->self_present=true;
        if (!remote_q1_prediction_camera_receive(row,view->origin,error)) return false;
    }
    return remote_q1_live(row,error);
}
const remote_q1_camera_view *remote_q1_camera_read(const frontend_remote_q1 *row)
{ return row && row->camera.tracking && row->camera.locked && row->camera.has_view ? &row->camera.view : NULL; }
void remote_q1_camera_reset(frontend_remote_q1 *row)
{
    if (!row) return;
    remote_q1_camera old=row->camera;
    row->camera=(remote_q1_camera){.desired=old.desired,.last_view_seconds=old.last_view_seconds};
}
bool frontend_remote_q1_chase_camera(frontend_remote_q1 *row,const frontend_q1_view_settings *settings,
    qa_vec3 eye,qa_vec3 aim_angles,qa_vec3 *origin,qa_vec3 *out_angles,qa_error *error)
{
    if (!row || !settings || !origin || !out_angles || row->busy || !remote_q1_mutable(row) ||
        !remote_q1_live(row,error) || !row->loaded || qa_q1_is_qw(row->protocol) || !settings->chase ||
        !qa_vec_finite(eye) || !qa_vec_finite(aim_angles) || !isfinite(settings->back) ||
        !isfinite(settings->right) || !isfinite(settings->up)) return false;
    qa_collision_geometry *geometry;
    return remote_q1_collision_acquire(row,&geometry,error) &&
        frontend_view_q1_chase(settings,geometry,row->collision_scratch,eye,aim_angles,origin,out_angles,error) &&
        remote_q1_live(row,error);
}
bool remote_q1_camera_take_teleport(frontend_remote_q1 *row, qa_vec3 *out, bool *present, qa_error *error)
{
    if (!row || !out || !present || !remote_q1_mutable(row) || !remote_q1_live(row,error)) return false;
    *present=row->camera.has_teleport; if (*present) *out=row->camera.teleport;
    row->camera.has_teleport=false; return true;
}
bool remote_q1_camera_fields(frontend_remote_q1 *row, qa_source_save_io *io)
{
    remote_q1_camera *c=&row->camera; remote_q1_camera_view *v=&c->view;
    return qa_source_save_bool(io,&c->tracking) && qa_source_save_bool(io,&c->locked) &&
        qa_source_save_bool(io,&c->has_view) && qa_source_save_bool(io,&c->has_teleport) &&
        qa_source_save_bool(io,&c->self_present) && qa_source_save_vec3(io,&c->self_origin) &&
        qa_source_save_u8(io,&c->slot) && c->slot<32 && qa_source_save_u8(io,&c->old_buttons) && !(c->old_buttons&~3u) &&
        qa_source_save_vec3(io,&c->desired) && qa_source_save_vec3(io,&c->teleport) && qa_source_save_f64(io,&c->last_view_seconds) &&
        qa_source_save_vec3(io,&v->origin) && qa_source_save_vec3(io,&v->angles) &&
        qa_source_save_u32(io,&v->target_flags) && qa_source_save_u32(io,&v->target_weapon_frame) &&
        qa_source_save_u8(io,&v->target_slot) && v->target_slot<32 && qa_source_save_bool(io,&v->chase) &&
        isfinite(c->last_view_seconds) && c->last_view_seconds>=0 &&
        qa_vec_finite(c->desired) && qa_vec_finite(c->teleport) && qa_vec_finite(c->self_origin) &&
        qa_vec_finite(v->origin) && qa_vec_finite(v->angles) &&
        (!c->has_view || (c->tracking && c->locked));
}

bool remote_q1_camera_contents_blend(frontend_remote_q1 *row,const qa_scene_view *view,
    qa_scene_vec4 *out,qa_error *error)
{
    if (!row || !view || !out || !row->world || view->seat!=row->options.domain.physical_seat ||
        !remote_q1_mutable(row) || !remote_q1_live(row,error)) return false;
    int32_t contents;
    if (!qa_scene_world_q1_contents(row->world,view->origin,&contents,error)) return false;
    bool quakeworld=qa_q1_is_qw(row->options.domain.protocol);
    frontend_q1_motion_settings settings;
    if (!frontend_view_settings_q1_motion_sample(&row->motion_refs,quakeworld,&settings,error)) return false;
    *out=frontend_view_q1_blend(&settings,&row->view_motion,contents,quakeworld,row->has_data ? row->data.items : 0);
    return remote_q1_live(row,error);
}
