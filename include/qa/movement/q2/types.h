/*
 * Port of src/movement/q2/types.ts: Quake II q_shared.h and rerelease
 * game.h movement representations. id Software / ZeniMax.
 * GPL-2.0-or-later.
 * C accommodations:
 *  - TS Vec3/Vec4 tuples become qa_q2_vec3/qa_q2_vec4 arrays; trace
 *    surfaces/entities are embedded values with has_* flags (TS null),
 *    so traces returned by value own their copies. Entity identity
 *    (TS ===) becomes qa_q2_same_hit value equality, which matches
 *    the canonicalization in q2/index.ts.
 *  - Callbacks become function pointer + ctx (the pmove ctx field).
 *  - Touch lists are fixed MAXTOUCH arrays (the TS push sites are
 *    all guarded by the same bound).
 */
#ifndef QA_MOVEMENT_Q2_TYPES_H
#define QA_MOVEMENT_Q2_TYPES_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"
#include "qa/core/numeric.h"

/** TS Vec3 / Vec4 tuples. */
typedef double qa_q2_vec3[3];
typedef double qa_q2_vec4[4];

/** TS axes: readonly [0, 1, 2]. */
extern const int qa_q2_axes[3];

/** TS MovementEntity (a TraceHit that is never "none"). */
typedef qa_trace_hit qa_q2_movement_entity;

/** TS CplaneT. */
typedef struct qa_q2_cplane {
  qa_q2_vec3 normal;
  double dist;
  double type;
  double signbits;
} qa_q2_cplane;

/** TS CsurfaceT (the contracts Q2SurfaceInfo shape). */
typedef qa_q2_surface_info qa_q2_csurface;
typedef qa_q2_csurface qa_q2_kex_csurface;

/** TS TraceT. has_surface/has_ent/has_surface2 false is the TS null.
 * surface_ref/surface2_ref are C-only: the scene-owned surface the
 * embedded copy came from (q2/index.ts contact traces point there). */
typedef struct qa_q2_trace {
  bool allsolid;
  bool startsolid;
  double fraction;
  qa_q2_vec3 endpos;
  qa_q2_cplane plane;
  bool has_surface;
  qa_q2_csurface surface;
  const qa_q2_csurface *surface_ref;
  double contents;
  bool has_ent;
  qa_q2_movement_entity ent;
  qa_q2_cplane plane2;
  bool has_surface2;
  qa_q2_csurface surface2;
  const qa_q2_csurface *surface2_ref;
  qa_trace_result source;
} qa_q2_trace;
typedef qa_q2_trace qa_q2_kex_trace;

/** TS PmTraceFn / StuckObjectTraceFn. */
typedef qa_q2_trace (*qa_q2_pm_trace_fn)(void *ctx, qa_q2_vec3 start, qa_q2_vec3 mins,
                                         qa_q2_vec3 maxs, qa_q2_vec3 end);
typedef qa_q2_pm_trace_fn qa_q2_stuck_object_trace_fn;
typedef double (*qa_q2_point_contents_fn)(void *ctx, qa_q2_vec3 point);

/** TS KexPmoveT trace/clip callbacks. */
typedef qa_q2_trace (*qa_q2_kex_trace_fn)(void *ctx, qa_q2_vec3 start, qa_q2_vec3 mins,
                                          qa_q2_vec3 maxs, qa_q2_vec3 end, bool has_pass,
                                          qa_q2_movement_entity pass, double mask);
typedef qa_q2_trace (*qa_q2_kex_clip_fn)(void *ctx, qa_q2_vec3 start, qa_q2_vec3 mins,
                                         qa_q2_vec3 maxs, qa_q2_vec3 end, double mask);

#define QA_Q2_PITCH 0
#define QA_Q2_YAW 1
#define QA_Q2_ROLL 2
#define QA_Q2_STEPSIZE 18
#define QA_Q2_MAXTOUCH 32

/* TS PmTypeT. */
/* network/q2/state.h spells the same TS PmTypeT values; one shared definition. */
#ifndef QA_Q2_PM_TYPE_DEFINED
#define QA_Q2_PM_TYPE_DEFINED
typedef enum qa_q2_pm_type {
  QA_Q2_PM_NORMAL, QA_Q2_PM_SPECTATOR, QA_Q2_PM_DEAD, QA_Q2_PM_GIB,
  QA_Q2_PM_FREEZE, QA_Q2_PM_GRAPPLE = 5, QA_Q2_PM_NOCLIP = 6
} qa_q2_pm_type;
#endif
/* TS KexPmTypeT. */
#define QA_Q2_KEX_PM_NORMAL 0
#define QA_Q2_KEX_PM_GRAPPLE 1
#define QA_Q2_KEX_PM_NOCLIP 2
#define QA_Q2_KEX_PM_SPECTATOR 3
#define QA_Q2_KEX_PM_DEAD 4
#define QA_Q2_KEX_PM_GIB 5
#define QA_Q2_KEX_PM_FREEZE 6
/* TS StuckResultT. */
#define QA_Q2_STUCK_GOOD_POSITION 0
#define QA_Q2_STUCK_FIXED 1
#define QA_Q2_STUCK_NO_GOOD_POSITION 2
/* TS WaterLevelT. */
#define QA_Q2_WATER_NONE 0
#define QA_Q2_WATER_FEET 1
#define QA_Q2_WATER_WAIST 2
#define QA_Q2_WATER_UNDER 3

/* TS PmflagsT (and the individually exported PMF_* consts). */
#define QA_Q2_PMF_NONE 0
#define QA_Q2_PMF_DUCKED 1
#define QA_Q2_PMF_JUMP_HELD 2
#define QA_Q2_PMF_ON_GROUND 4
#define QA_Q2_PMF_TIME_WATERJUMP 8
#define QA_Q2_PMF_TIME_LAND 16
#define QA_Q2_PMF_TIME_TELEPORT 32
#define QA_Q2_PMF_NO_POSITIONAL_PREDICTION 64
#define QA_Q2_PMF_ON_LADDER 128
#define QA_Q2_PMF_NO_ANGULAR_PREDICTION 256
#define QA_Q2_PMF_IGNORE_PLAYER_COLLISION 512
#define QA_Q2_PMF_TIME_TRICK 1024

/* TS ButtonT. */
#define QA_Q2_BUTTON_NONE 0
#define QA_Q2_BUTTON_ATTACK 1
#define QA_Q2_BUTTON_USE 2
#define QA_Q2_BUTTON_HOLSTER 4
#define QA_Q2_BUTTON_JUMP 8
#define QA_Q2_BUTTON_CROUCH 16
#define QA_Q2_BUTTON_ANY 128

/* TS RefdefFlagsT. */
#define QA_Q2_RDF_NONE 0
#define QA_Q2_RDF_UNDERWATER 1

/* TS ContentsT (and the individually exported CONTENTS_* consts). */
#define QA_Q2_CONTENTS_NONE 0
#define QA_Q2_CONTENTS_SOLID 1
#define QA_Q2_CONTENTS_WINDOW 2
#define QA_Q2_CONTENTS_LAVA 8
#define QA_Q2_CONTENTS_SLIME 16
#define QA_Q2_CONTENTS_WATER 32
#define QA_Q2_CONTENTS_CURRENT_0 (1 << 18)
#define QA_Q2_CONTENTS_CURRENT_90 (1 << 19)
#define QA_Q2_CONTENTS_CURRENT_180 (1 << 20)
#define QA_Q2_CONTENTS_CURRENT_270 (1 << 21)
#define QA_Q2_CONTENTS_CURRENT_UP (1 << 22)
#define QA_Q2_CONTENTS_CURRENT_DOWN (1 << 23)
#define QA_Q2_CONTENTS_LADDER (1 << 29)
#define QA_Q2_CONTENTS_NO_WATERJUMP (1 << 13)
#define QA_Q2_CONTENTS_PLAYERCLIP (1 << 16)
#define QA_Q2_CONTENTS_MONSTER (1 << 25)
#define QA_Q2_CONTENTS_PLAYER (1 << 30)

/* TS SurfflagsT. */
#define QA_Q2_SURF_SLICK 2

#define QA_Q2_MASK_SOLID (QA_Q2_CONTENTS_SOLID | QA_Q2_CONTENTS_WINDOW)
#define QA_Q2_MASK_DEADSOLID (QA_Q2_MASK_SOLID | QA_Q2_CONTENTS_PLAYERCLIP)
#define QA_Q2_MASK_PLAYERSOLID \
  (QA_Q2_MASK_DEADSOLID | QA_Q2_CONTENTS_MONSTER | QA_Q2_CONTENTS_PLAYER)
#define QA_Q2_MASK_CLASSIC_PLAYERSOLID (QA_Q2_MASK_DEADSOLID | QA_Q2_CONTENTS_MONSTER)
#define QA_Q2_MASK_WATER (QA_Q2_CONTENTS_WATER | QA_Q2_CONTENTS_LAVA | QA_Q2_CONTENTS_SLIME)
#define QA_Q2_MASK_CURRENT                                                                 \
  (QA_Q2_CONTENTS_CURRENT_0 | QA_Q2_CONTENTS_CURRENT_90 | QA_Q2_CONTENTS_CURRENT_180 |     \
   QA_Q2_CONTENTS_CURRENT_270 | QA_Q2_CONTENTS_CURRENT_UP | QA_Q2_CONTENTS_CURRENT_DOWN)

/** TS ClassicPmove. ctx serves trace/pointcontents. */
typedef struct qa_q2_classic_pmove {
  void *ctx;
  struct {
    double pm_type;
    qa_q2_vec3 origin;
    qa_q2_vec3 velocity;
    double pm_flags;
    double pm_time;
    double gravity;
    qa_q2_vec3 delta_angles;
  } s;
  struct {
    double msec;
    qa_q2_vec3 angles;
    double forwardmove;
    double sidemove;
    double upmove;
    double buttons;
    double impulse;
    double lightlevel;
  } cmd;
  bool snapinitial;
  double numtouch;
  qa_q2_movement_entity touchents[QA_Q2_MAXTOUCH];
  qa_q2_trace touchtraces[QA_Q2_MAXTOUCH];
  qa_q2_vec3 viewangles;
  double viewheight;
  qa_q2_vec3 mins;
  qa_q2_vec3 maxs;
  bool has_groundentity;
  qa_q2_movement_entity groundentity;
  double watertype;
  double waterlevel;
  qa_q2_pm_trace_fn trace;
  qa_q2_point_contents_fn pointcontents;
  qa_bounds character_bounds;
  bool has_body_bounds;
  qa_bounds body_bounds;
  bool has_previous_bounds;
  qa_bounds previous_bounds;
} qa_q2_classic_pmove;

/** TS KexTouchListT. */
typedef struct qa_q2_kex_touch_list {
  double num;
  qa_q2_trace traces[QA_Q2_MAXTOUCH];
} qa_q2_kex_touch_list;

/** TS KexPmoveT. ctx serves trace/clip/pointcontents. */
typedef struct qa_q2_kex_pmove {
  void *ctx;
  struct {
    double pm_type;
    qa_q2_vec3 origin;
    qa_q2_vec3 velocity;
    double pm_flags;
    double pm_time;
    double gravity;
    qa_q2_vec3 delta_angles;
    double viewheight;
  } s;
  struct {
    double msec;
    qa_q2_vec3 angles;
    double forwardmove;
    double sidemove;
    double buttons;
    double server_frame;
  } cmd;
  bool snapinitial;
  qa_q2_kex_touch_list touch;
  qa_q2_vec3 viewangles;
  qa_q2_vec3 mins;
  qa_q2_vec3 maxs;
  bool has_groundentity;
  qa_q2_movement_entity groundentity;
  qa_q2_cplane groundplane;
  double watertype;
  double waterlevel;
  bool has_player;
  qa_q2_movement_entity player;
  qa_q2_kex_trace_fn trace;
  qa_q2_kex_clip_fn clip;
  qa_q2_point_contents_fn pointcontents;
  qa_q2_vec3 viewoffset;
  qa_q2_vec4 screen_blend;
  double rdflags;
  bool jump_sound;
  bool step_clip;
  double impact_delta;
  qa_bounds character_bounds;
  bool has_body_bounds;
  qa_bounds body_bounds;
  bool has_previous_bounds;
  qa_bounds previous_bounds;
} qa_q2_kex_pmove;

/** TS PmConfigT and PM_CONFIG_DEFAULT. */
typedef struct qa_q2_pm_config {
  double airaccel;
  bool n64_physics;
} qa_q2_pm_config;
extern const qa_q2_pm_config qa_q2_pm_config_default;

/*
 * TS element(): the double-array specialization. An out-of-range or
 * non-integer index aborts with the TS RangeError message.
 */
double qa_q2_element(const double *values, size_t length, double index);

/** TS plane(). */
qa_q2_cplane qa_q2_plane(void);

/* TS ToInt32 flag edits on number fields, as in q1/types. */
bool qa_q2_flag_test(double flags, int mask);
double qa_q2_flag_set(double flags, int mask);
double qa_q2_flag_clear(double flags, int mask);

/** MovementEntity identity (TS === after index.ts canonicalization). */
bool qa_q2_same_hit(qa_trace_hit first, qa_trace_hit second);

#endif
