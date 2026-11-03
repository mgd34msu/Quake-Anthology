/* Port of src/compat/q2/rerelease/layouts.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/rerelease/layouts.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { const char *name; const char *storage; const qa_guest_layout *layout; double count; } member_t;
#define M(n, s) (member_t){(n), (s), NULL, 1}
#define MC(n, s, c) (member_t){(n), (s), NULL, (c)}
#define ML(n, l) (member_t){(n), NULL, (l), 1}
#define MLC(n, l, c) (member_t){(n), NULL, (l), (c)}
static double storage_size(const char *s) {
  if (!strcmp(s, "int8") || !strcmp(s, "uint8")) return 1;
  if (!strcmp(s, "int16") || !strcmp(s, "uint16")) return 2;
  if (!strcmp(s, "int32") || !strcmp(s, "uint32") || !strcmp(s, "float32")) return 4;
  return 8;
}
static char *xstrdup(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (!p) qa_range_error("Rerelease layouts out of memory"); memcpy(p, s, n); return p; }
static qa_guest_layout build(const char *name, const member_t *members, size_t member_count) {
  size_t field_total = 0;
  for (size_t i = 0; i < member_count; i++)
    field_total += members[i].storage ? 1 : (size_t)members[i].count * members[i].layout->field_count;
  qa_guest_field_layout *fields = calloc(field_total ? field_total : 1, sizeof *fields);
  if (!fields) qa_range_error("Rerelease layouts out of memory");
  double cursor = 0, alignment = 1; size_t fi = 0;
  for (size_t i = 0; i < member_count; i++) {
    const member_t *m = &members[i];
    double width = m->storage ? storage_size(m->storage) : m->layout->byte_length;
    double align = m->storage ? width : m->layout->alignment;
    if (align > alignment) alignment = align;
    cursor = ceil(cursor / align) * align;
    if (m->storage) {
      fields[fi++] = (qa_guest_field_layout){xstrdup(m->name), cursor, (qa_guest_storage)m->storage, m->count};
    } else {
      for (double k = 0; k < m->count; k++) for (size_t j = 0; j < m->layout->field_count; j++) {
        const qa_guest_field_layout *nested = &m->layout->fields[j];
        char buf[512];
        if (m->count == 1) snprintf(buf, sizeof buf, "%s.%s", m->name, nested->name);
        else snprintf(buf, sizeof buf, "%s[%d].%s", m->name, (int)k, nested->name);
        fields[fi++] = (qa_guest_field_layout){xstrdup(buf), cursor + k * width + nested->byte_offset, nested->storage, nested->count};
      }
    }
    cursor += width * m->count;
  }
  char id[256]; snprintf(id, sizeof id, "q2-rerelease-x64:%s", name);
  qa_guest_layout layout = {xstrdup(id), ceil(cursor / alignment) * alignment, alignment, 8, fields, fi};
  return layout;
}
double qa_compat_q2_rerelease_field_offset(const qa_guest_layout *layout, const char *name) {
  qa_compat_q2_rerelease_layouts_ensure();
  for (size_t i = 0; i < layout->field_count; i++) if (strcmp(layout->fields[i].name, name) == 0) return layout->fields[i].byte_offset;
  qa_range_error("Unknown %s field %s", layout->id, name);
}

qa_guest_layout qa_compat_q2_rerelease_vec2_layout, qa_compat_q2_rerelease_vec3_layout, qa_compat_q2_rerelease_plane_layout,
  qa_compat_q2_rerelease_surface_layout, qa_compat_q2_rerelease_trace_layout, qa_compat_q2_rerelease_cvar_layout,
  qa_compat_q2_rerelease_pmove_state_layout, qa_compat_q2_rerelease_usercmd_layout, qa_compat_q2_rerelease_touch_list_layout,
  qa_compat_q2_rerelease_pmove_layout, qa_compat_q2_rerelease_entity_state_layout, qa_compat_q2_rerelease_player_state_layout,
  qa_compat_q2_rerelease_client_layout, qa_compat_q2_rerelease_height_fog_layout, qa_compat_q2_rerelease_persistent_client_layout,
  qa_compat_q2_rerelease_private_client_prefix_layout, qa_compat_q2_rerelease_respawn_client_layout,
  qa_compat_q2_rerelease_private_client_layout, qa_compat_q2_rerelease_server_entity_layout, qa_compat_q2_rerelease_edict_layout,
  qa_compat_q2_rerelease_private_edict_prefix_layout, qa_compat_q2_rerelease_rectangle_layout;
static qa_guest_layout damage_indicator_layout, kick_layout, armor_info_layout, save_function_layout;
static bool built = false;
/* Helper to append repeated-name members (TS [...names].map). */
typedef struct { member_t *items; size_t count, cap; } members_t;
static void push(members_t *list, member_t m) {
  if (list->count == list->cap) { list->cap = list->cap ? list->cap * 2 : 32; list->items = realloc(list->items, list->cap * sizeof *list->items); if (!list->items) qa_range_error("Rerelease layouts out of memory"); }
  list->items[list->count++] = m;
}
static void push_names(members_t *list, const char *const *names, size_t n, const char *storage, double count) {
  for (size_t i = 0; i < n; i++) push(list, (member_t){names[i], storage, NULL, count});
}
void qa_compat_q2_rerelease_layouts_ensure(void) {
  if (built) return;
  built = true;
  { const member_t m[] = {MC("xy", "float32", 2)}; qa_compat_q2_rerelease_vec2_layout = build("vec2_t", m, 1); }
  { const member_t m[] = {MC("xyz", "float32", 3)}; qa_compat_q2_rerelease_vec3_layout = build("vec3_t", m, 1); }
  { const member_t m[] = {MC("normal", "float32", 3), M("dist", "float32"), M("type", "uint8"), M("signbits", "uint8"), MC("pad", "uint8", 2)};
    qa_compat_q2_rerelease_plane_layout = build("cplane_t", m, 5); }
  { const member_t m[] = {MC("name", "uint8", 32), M("flags", "uint32"), M("value", "int32"), M("id", "uint32"), MC("material", "uint8", 16)};
    qa_compat_q2_rerelease_surface_layout = build("csurface_t", m, 5); }
  { const member_t m[] = {M("allsolid", "uint8"), M("startsolid", "uint8"), M("fraction", "float32"), MC("endpos", "float32", 3),
      ML("plane", &qa_compat_q2_rerelease_plane_layout), M("surface", "pointer"), M("contents", "uint32"), M("ent", "pointer"),
      ML("plane2", &qa_compat_q2_rerelease_plane_layout), M("surface2", "pointer")};
    qa_compat_q2_rerelease_trace_layout = build("trace_t", m, 10); }
  { const member_t m[] = {M("name", "pointer"), M("string", "pointer"), M("latched_string", "pointer"), M("flags", "uint32"),
      M("modified_count", "int32"), M("value", "float32"), M("next", "pointer"), M("integer", "int32")};
    qa_compat_q2_rerelease_cvar_layout = build("cvar_t", m, 8); }
  { const member_t m[] = {M("pm_type", "int32"), MC("origin", "float32", 3), MC("velocity", "float32", 3), M("pm_flags", "uint16"),
      M("pm_time", "uint16"), M("gravity", "int16"), MC("delta_angles", "float32", 3), M("viewheight", "int8")};
    qa_compat_q2_rerelease_pmove_state_layout = build("pmove_state_t", m, 8); }
  { const member_t m[] = {M("msec", "uint8"), M("buttons", "uint8"), MC("angles", "float32", 3), M("forwardmove", "float32"),
      M("sidemove", "float32"), M("server_frame", "uint32")};
    qa_compat_q2_rerelease_usercmd_layout = build("usercmd_t", m, 6); }
  { const member_t m[] = {M("num", "uint32"), MLC("touches", &qa_compat_q2_rerelease_trace_layout, 32)};
    qa_compat_q2_rerelease_touch_list_layout = build("touch_list_t", m, 2); }
  { const member_t m[] = {ML("s", &qa_compat_q2_rerelease_pmove_state_layout), ML("cmd", &qa_compat_q2_rerelease_usercmd_layout), M("snapinitial", "uint8"),
      ML("touch", &qa_compat_q2_rerelease_touch_list_layout), MC("viewangles", "float32", 3), MC("mins", "float32", 3), MC("maxs", "float32", 3),
      M("groundentity", "pointer"), ML("groundplane", &qa_compat_q2_rerelease_plane_layout), M("watertype", "uint32"), M("waterlevel", "uint8"),
      M("player", "pointer"), M("trace", "pointer"), M("clip", "pointer"), M("pointcontents", "pointer"), MC("viewoffset", "float32", 3),
      MC("screen_blend", "float32", 4), M("rdflags", "uint8"), M("jump_sound", "uint8"), M("step_clip", "uint8"), M("impact_delta", "float32")};
    qa_compat_q2_rerelease_pmove_layout = build("pmove_t", m, 21); }
  { const member_t m[] = {M("number", "uint32"), MC("origin", "float32", 3), MC("angles", "float32", 3), MC("old_origin", "float32", 3),
      M("modelindex", "int32"), M("modelindex2", "int32"), M("modelindex3", "int32"), M("modelindex4", "int32"), M("frame", "int32"),
      M("skinnum", "int32"), M("effects", "uint64"), M("renderfx", "uint32"), M("solid", "uint32"), M("sound", "int32"), M("event", "uint8"),
      M("alpha", "float32"), M("scale", "float32"), M("instance_bits", "uint8"), M("loop_volume", "float32"), M("loop_attenuation", "float32"),
      M("owner", "int32"), M("old_frame", "int32")};
    qa_compat_q2_rerelease_entity_state_layout = build("entity_state_t", m, 22); }
  { const member_t m[] = {ML("pmove", &qa_compat_q2_rerelease_pmove_state_layout), MC("viewangles", "float32", 3), MC("viewoffset", "float32", 3),
      MC("kick_angles", "float32", 3), MC("gunangles", "float32", 3), MC("gunoffset", "float32", 3), M("gunindex", "int32"), M("gunskin", "int32"),
      M("gunframe", "int32"), M("gunrate", "int32"), MC("screen_blend", "float32", 4), MC("damage_blend", "float32", 4), M("fov", "float32"),
      M("rdflags", "uint8"), MC("stats", "int16", 64), M("team_id", "uint8")};
    qa_compat_q2_rerelease_player_state_layout = build("player_state_t", m, 16); }
  { const member_t m[] = {ML("ps", &qa_compat_q2_rerelease_player_state_layout), M("ping", "int32")};
    qa_compat_q2_rerelease_client_layout = build("gclient_shared_t", m, 2); }
  { const member_t m[] = {MC("start", "float32", 4), MC("end", "float32", 4), M("falloff", "float32"), M("density", "float32")};
    qa_compat_q2_rerelease_height_fog_layout = build("height_fog_t", m, 4); }
  { const member_t m[] = {MC("userinfo", "uint8", 2048), MC("social_id", "uint8", 256), MC("netname", "uint8", 32), M("hand", "int32"),
      M("autoswitch", "int32"), M("autoshield", "int32"), M("connected", "uint8"), M("spawned", "uint8"), M("health", "int32"), M("max_health", "int32"),
      M("savedFlags", "uint64"), M("selected_item", "int32"), M("selected_item_time", "int64"), MC("inventory", "int32", 82), MC("max_ammo", "int16", 12),
      M("weapon", "pointer"), M("lastweapon", "pointer"), M("power_cubes", "int32"), M("score", "int32"), M("game_help1changed", "int32"),
      M("game_help2changed", "int32"), M("helpchanged", "int32"), M("help_time", "int64"), M("spectator", "uint8"), M("bob_skip", "uint8"),
      MC("wanted_fog", "float32", 5), ML("wanted_heightfog", &qa_compat_q2_rerelease_height_fog_layout), M("fog_transition_time", "int64"),
      M("megahealth_time", "int64"), M("lives", "int32"), M("n64_crouch_warn_times", "uint8"), M("n64_crouch_warning", "int64")};
    qa_compat_q2_rerelease_persistent_client_layout = build("client_persistant_t", m, 32); }
  { const member_t m[] = {ML("shared", &qa_compat_q2_rerelease_client_layout), ML("pers", &qa_compat_q2_rerelease_persistent_client_layout)};
    qa_compat_q2_rerelease_private_client_prefix_layout = build("gclient_t_private_prefix", m, 2); }
  { members_t list = {0};
    push(&list, ML("coop_respawn", &qa_compat_q2_rerelease_persistent_client_layout));
    push(&list, M("entertime", "int64")); push(&list, M("score", "int32")); push(&list, MC("cmd_angles", "float32", 3));
    push(&list, M("spectator", "uint8")); push(&list, M("ctf_team", "int32")); push(&list, M("ctf_state", "int32"));
    static const char *const ctf_times[] = {"ctf_lasthurtcarrier", "ctf_lastreturnedflag", "ctf_flagsince", "ctf_lastfraggedcarrier"};
    push_names(&list, ctf_times, 4, "int64", 1);
    push(&list, M("id_state", "uint8")); push(&list, M("lastidtime", "int64")); push(&list, M("voted", "uint8"));
    push(&list, M("ready", "uint8")); push(&list, M("admin", "uint8")); push(&list, M("ghost", "pointer"));
    qa_compat_q2_rerelease_respawn_client_layout = build("client_respawn_t", list.items, list.count); free(list.items); }
  { const member_t m[] = {MC("from", "float32", 3), M("health", "int32"), M("armor", "int32"), M("power", "int32")};
    damage_indicator_layout = build("damage_indicator_t", m, 4); }
  { const member_t m[] = {MC("angles", "float32", 3), MC("origin", "float32", 3), M("time", "int64"), M("total", "int64")};
    kick_layout = build("gclient_t_kick", m, 4); }
  { members_t list = {0};
    push(&list, ML("shared", &qa_compat_q2_rerelease_client_layout));
    push(&list, ML("pers", &qa_compat_q2_rerelease_persistent_client_layout));
    push(&list, ML("resp", &qa_compat_q2_rerelease_respawn_client_layout));
    push(&list, ML("old_pmove", &qa_compat_q2_rerelease_pmove_state_layout));
    static const char *const shows[] = {"showscores", "showeou", "showinventory", "showhelp", "buttons", "oldbuttons", "latched_buttons"};
    push_names(&list, shows, 7, "uint8", 1);
    push(&list, ML("cmd", &qa_compat_q2_rerelease_usercmd_layout));
    push(&list, M("weapon_fire_finished", "int64")); push(&list, M("weapon_think_time", "int64"));
    push(&list, M("weapon_fire_buffered", "uint8")); push(&list, M("weapon_thunk", "uint8")); push(&list, M("newweapon", "pointer"));
    static const char *const dmg[] = {"damage_armor", "damage_parmor", "damage_blood", "damage_knockback"};
    push_names(&list, dmg, 4, "int32", 1);
    push(&list, MC("damage_from", "float32", 3)); push(&list, MLC("damage_indicators", &damage_indicator_layout, 4));
    push(&list, M("num_damage_indicators", "uint8")); push(&list, M("killer_yaw", "float32")); push(&list, M("weaponstate", "int32"));
    push(&list, ML("kick", &kick_layout)); push(&list, M("quake_time", "int64")); push(&list, MC("kick_origin", "float32", 3));
    push(&list, M("v_dmg_roll", "float32")); push(&list, M("v_dmg_pitch", "float32")); push(&list, M("v_dmg_time", "int64")); push(&list, M("fall_time", "int64"));
    static const char *const alphas[] = {"fall_value", "damage_alpha", "bonus_alpha"}; push_names(&list, alphas, 3, "float32", 1);
    static const char *const vecs[] = {"damage_blend", "v_angle", "v_forward"}; push_names(&list, vecs, 3, "float32", 3);
    push(&list, M("bobtime", "float32")); push(&list, MC("oldviewangles", "float32", 3)); push(&list, MC("oldvelocity", "float32", 3));
    push(&list, M("oldgroundentity", "pointer")); push(&list, M("flash_time", "int64")); push(&list, M("next_drown_time", "int64"));
    push(&list, M("old_waterlevel", "uint8")); push(&list, M("breather_sound", "int32")); push(&list, M("machinegun_shots", "int32"));
    push(&list, M("anim_end", "int32")); push(&list, M("anim_priority", "int32")); push(&list, M("anim_duck", "uint8")); push(&list, M("anim_run", "uint8"));
    static const char *const times1[] = {"anim_time", "quad_time", "invincible_time", "breather_time", "enviro_time", "invisible_time"};
    push_names(&list, times1, 6, "int64", 1);
    push(&list, M("grenade_blew_up", "uint8"));
    static const char *const times2[] = {"grenade_time", "grenade_finished_time", "quadfire_time"};
    push_names(&list, times2, 3, "int64", 1);
    push(&list, M("silencer_shots", "int32")); push(&list, M("weapon_sound", "int32")); push(&list, M("pickup_msg_time", "int64"));
    push(&list, M("flood_locktill", "int64")); push(&list, MC("flood_when", "int64", 10)); push(&list, M("flood_whenhead", "int32"));
    push(&list, M("respawn_time", "int64")); push(&list, M("chase_target", "pointer")); push(&list, M("update_chase", "uint8"));
    static const char *const times3[] = {"double_time", "ir_time", "nuke_time", "tracker_pain_time"};
    push_names(&list, times3, 4, "int64", 1);
    push(&list, M("owned_sphere", "pointer")); push(&list, M("empty_click_sound", "int64")); push(&list, M("inmenu", "uint8"));
    push(&list, M("menu", "pointer")); push(&list, M("menutime", "int64")); push(&list, M("menudirty", "uint8"));
    push(&list, M("ctf_grapple", "pointer")); push(&list, M("ctf_grapplestate", "int32"));
    static const char *const times4[] = {"ctf_grapplereleasetime", "ctf_regentime", "ctf_techsndtime", "ctf_lasttechmsg"};
    push_names(&list, times4, 4, "int64", 1);
    push(&list, M("trail_head", "pointer")); push(&list, M("trail_tail", "pointer")); push(&list, M("no_weapon_chains", "uint8"));
    push(&list, M("landmark_free_fall", "uint8")); push(&list, M("landmark_name", "pointer")); push(&list, MC("landmark_rel_pos", "float32", 3));
    push(&list, M("landmark_noise_time", "int64")); push(&list, M("invisibility_fade_time", "int64")); push(&list, M("chase_msg_time", "int64"));
    push(&list, M("menu_sign", "int32")); push(&list, MC("last_ladder_pos", "float32", 3)); push(&list, M("last_ladder_sound", "int64"));
    push(&list, M("coop_respawn_state", "int32")); push(&list, M("last_damage_time", "int64"));
    push(&list, M("sight_entity", "pointer")); push(&list, M("sight_entity_time", "int64")); push(&list, M("sound_entity", "pointer"));
    push(&list, M("sound_entity_time", "int64")); push(&list, M("sound2_entity", "pointer")); push(&list, M("sound2_entity_time", "int64"));
    push(&list, M("num_lag_origins", "uint8")); push(&list, M("next_lag_origin", "uint8")); push(&list, M("is_lag_compensated", "uint8"));
    push(&list, MC("lag_restore_origin", "float32", 3)); push(&list, MC("slow_view_angles", "float32", 3)); push(&list, M("slow_view_angle_time", "int64"));
    push(&list, M("help_draw_points", "uint8")); push(&list, M("help_draw_index", "uint64")); push(&list, M("help_draw_count", "uint64"));
    push(&list, M("help_draw_time", "int64")); push(&list, M("step_frame", "uint32")); push(&list, M("help_poi_image", "int32"));
    push(&list, MC("help_poi_location", "float32", 3)); push(&list, M("awaiting_respawn", "uint8")); push(&list, M("respawn_timeout", "int64"));
    push(&list, MC("fog", "float32", 5)); push(&list, ML("heightfog", &qa_compat_q2_rerelease_height_fog_layout));
    push(&list, M("last_attacker_time", "int64")); push(&list, M("last_firing_time", "int64"));
    qa_compat_q2_rerelease_private_client_layout = build("gclient_t", list.items, list.count); free(list.items); }
  { const member_t m[] = {M("item_id", "int32"), M("max_count", "int32")}; armor_info_layout = build("armorInfo_t", m, 2); }
  { members_t list = {0};
    push(&list, M("init", "uint8")); push(&list, M("ent_flags", "uint64")); push(&list, M("buttons", "uint8")); push(&list, M("spawnflags", "uint32"));
    static const char *const ints[] = {"item_id", "armor_type", "armor_value", "health", "max_health", "starting_health", "weapon", "team", "lobby_usernum", "respawntime", "viewheight", "last_attackertime"};
    push_names(&list, ints, 12, "int32", 1);
    push(&list, M("waterlevel", "uint8"));
    static const char *const fvecs[] = {"viewangles", "viewforward", "velocity", "start_origin", "end_origin"};
    push_names(&list, fvecs, 5, "float32", 3);
    static const char *const ptrs[] = {"enemy", "ground_entity", "classname", "targetname"};
    push_names(&list, ptrs, 4, "pointer", 1);
    push(&list, MC("netname", "uint8", 32)); push(&list, MC("inventory", "int32", 256)); push(&list, MLC("armor_info", &armor_info_layout, 3));
    qa_compat_q2_rerelease_server_entity_layout = build("sv_entity_t", list.items, list.count); free(list.items); }
  { members_t list = {0};
    push(&list, ML("s", &qa_compat_q2_rerelease_entity_state_layout)); push(&list, M("client", "pointer"));
    push(&list, ML("sv", &qa_compat_q2_rerelease_server_entity_layout)); push(&list, M("inuse", "uint8")); push(&list, M("linked", "uint8"));
    push(&list, M("linkcount", "int32")); push(&list, M("areanum", "int32")); push(&list, M("areanum2", "int32")); push(&list, M("svflags", "uint32"));
    static const char *const bounds[] = {"mins", "maxs", "absmin", "absmax", "size"}; push_names(&list, bounds, 5, "float32", 3);
    push(&list, M("solid", "uint8")); push(&list, M("clipmask", "uint32")); push(&list, M("owner", "pointer"));
    qa_compat_q2_rerelease_edict_layout = build("edict_shared_t", list.items, list.count); free(list.items); }
  { const member_t m[] = {M("value", "pointer"), M("list", "pointer")}; save_function_layout = build("save_data_t", m, 2); }
  { members_t list = {0};
    push(&list, ML("shared", &qa_compat_q2_rerelease_edict_layout)); push(&list, M("spawn_count", "int32")); push(&list, M("movetype", "int32"));
    push(&list, M("flags", "uint64")); push(&list, M("model", "pointer")); push(&list, M("freetime", "int64")); push(&list, M("message", "pointer"));
    push(&list, M("classname", "pointer")); push(&list, M("spawnflags", "uint32")); push(&list, M("timestamp", "int64")); push(&list, M("angle", "float32"));
    static const char *const targets[] = {"target", "targetname", "killtarget", "team", "pathtarget", "deathtarget", "healthtarget", "itemtarget", "combattarget", "target_ent"};
    push_names(&list, targets, 10, "pointer", 1);
    static const char *const speeds[] = {"speed", "accel", "decel"}; push_names(&list, speeds, 3, "float32", 1);
    static const char *const dirs[] = {"movedir", "pos1", "pos2", "pos3", "velocity", "avelocity"}; push_names(&list, dirs, 6, "float32", 3);
    push(&list, M("mass", "int32")); push(&list, M("air_finished", "int64")); push(&list, M("gravity", "float32"));
    push(&list, M("goalentity", "pointer")); push(&list, M("movetarget", "pointer")); push(&list, M("yaw_speed", "float32"));
    push(&list, M("ideal_yaw", "float32")); push(&list, M("nextthink", "int64"));
    static const char *const cbs[] = {"prethink", "postthink", "think", "touch", "use", "pain", "die"};
    for (size_t i = 0; i < 7; i++) push(&list, ML(cbs[i], &save_function_layout));
    static const char *const deb[] = {"touch_debounce_time", "pain_debounce_time", "damage_debounce_time", "fly_sound_debounce_time", "last_move_time"};
    push_names(&list, deb, 5, "int64", 1);
    push(&list, M("health", "int32")); push(&list, M("max_health", "int32")); push(&list, M("gib_health", "int32"));
    push(&list, M("show_hostile", "int64")); push(&list, M("powerarmor_time", "int64")); push(&list, M("map", "pointer"));
    push(&list, M("viewheight", "int32")); push(&list, M("deadflag", "uint8")); push(&list, M("takedamage", "uint8"));
    push(&list, M("dmg", "int32")); push(&list, M("radius_dmg", "int32")); push(&list, M("dmg_radius", "float32"));
    push(&list, M("sounds", "int32")); push(&list, M("count", "int32"));
    static const char *const chain[] = {"chain", "enemy", "oldenemy", "activator", "groundentity"};
    push_names(&list, chain, 5, "pointer", 1);
    push(&list, M("groundentity_linkcount", "int32"));
    qa_compat_q2_rerelease_private_edict_prefix_layout = build("edict_t_private_prefix", list.items, list.count); free(list.items); }
  { const member_t m[] = {M("x", "int32"), M("y", "int32"), M("width", "int32"), M("height", "int32")};
    qa_compat_q2_rerelease_rectangle_layout = build("vrect_t", m, 4); }
}
qa_guest_layout qa_compat_q2_rerelease_import_table_layout(const char *kind, const char *const *names, size_t count) {
  members_t list = {0};
  push(&list, M("tick_rate", "uint32")); push(&list, M("frame_time_s", "float32")); push(&list, M("frame_time_ms", "uint32"));
  push_names(&list, names, count, "pointer", 1);
  char name[128]; snprintf(name, sizeof name, "%s_import_t", kind);
  qa_guest_layout layout = build(name, list.items, list.count); free(list.items); return layout;
}
qa_guest_layout qa_compat_q2_rerelease_export_table_layout(const char *kind, const char *const *names, size_t count) {
  members_t list = {0};
  push(&list, M("apiversion", "int32"));
  push_names(&list, names, count, "pointer", 1);
  if (strcmp(kind, "game") == 0) {
    /* members.splice(20, 0, edicts, edict_size, num_edicts, max_edicts, server_flags) */
    members_t out = {0};
    const member_t extra[] = {M("edicts", "pointer"), M("edict_size", "uint64"), M("num_edicts", "uint32"), M("max_edicts", "uint32"), M("server_flags", "uint32")};
    for (size_t i = 0; i < list.count; i++) {
      if (i == 20) for (size_t j = 0; j < 5; j++) push(&out, extra[j]);
      push(&out, list.items[i]);
    }
    free(list.items); list = out;
  }
  char name[128]; snprintf(name, sizeof name, "%s_export_t", kind);
  qa_guest_layout layout = build(name, list.items, list.count); free(list.items); return layout;
}
