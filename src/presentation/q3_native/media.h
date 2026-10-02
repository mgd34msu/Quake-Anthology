#ifndef QA_Q3_NATIVE_MEDIA_H
#define QA_Q3_NATIVE_MEDIA_H

#include "qa/application_native_q3_presentation.h"
#include "qa/q3_presentation.h"
#include "qa/application_native_q3_wire.h"
#include "qa/application_selected_effects.h"

typedef struct q3n_media q3n_media;
typedef struct q3n_remote_source_view q3n_remote_source_view;
typedef struct q3n_remote_source q3n_remote_source;
typedef struct q3n_compiled_source q3n_compiled_source;
typedef struct q3n_compiled_source_view q3n_compiled_source_view;
/* A normalized remote CLIENT effect owns no GAME or retail snapshot. The
 * entered producer qualifies its actual provider, actor, clock and dictionary. */
typedef struct q3n_unified_effect_source {
    void *context;
    qa_actor_owner provider;
    qa_actor_id actor;
    qa_vfs *content;
    qa_q3_presentation_assets *assets;
    qa_q3_product product;
    int32_t time;
    bool (*current)(const struct q3n_unified_effect_source *);
} q3n_unified_effect_source;
bool q3n_media_unified_effects_current(const q3n_media *, const q3n_unified_effect_source *, qa_error *);
bool q3n_media_load_unified_effects(q3n_media *, const q3n_unified_effect_source *, qa_error *);
typedef enum q3n_missile_trail {
    Q3N_TRAIL_NONE, Q3N_TRAIL_ROCKET, Q3N_TRAIL_GRENADE,
    Q3N_TRAIL_GRAPPLE, Q3N_TRAIL_PLASMA, Q3N_TRAIL_NAIL
} q3n_missile_trail;
typedef enum q3n_brass { Q3N_BRASS_NONE, Q3N_BRASS_MACHINEGUN, Q3N_BRASS_SHOTGUN, Q3N_BRASS_NAILGUN } q3n_brass;
typedef struct q3n_item_media {
    int32_t models[2], icon, pickup_sound;
    bool registered;
} q3n_item_media;
typedef struct q3n_weapon_media {
    int32_t item_index;
    int32_t weapon_model, barrel_model, hands_model, flash_model, ammo_model;
    int32_t weapon_icon, ammo_icon;
    qa_vec3 weapon_midpoint, flash_light_color, missile_light_color;
    int32_t missile_model, missile_render_flags, missile_sound;
    int32_t trail_time;
    float missile_light, trail_radius;
    q3n_missile_trail trail;
    q3n_brass eject_brass;
    int32_t flash_sounds[4], ready_sound, firing_sound;
    bool registered, ready, loop_fire_sound;
} q3n_weapon_media;

/* These indices name authored cgs.media fields, independently of GAME's
 * modelindex and sound indexes. Numeric handles belong to the backend. */
typedef enum q3n_graphic {
    Q3N_G_CHARSET, Q3N_G_WHITE, Q3N_G_CHARSET_PROP, Q3N_G_CHARSET_PROP_GLOW, Q3N_G_CHARSET_PROP_B,
    Q3N_G_VIEW_BLOOD, Q3N_G_DEFER, Q3N_G_SCOREBOARD_NAME, Q3N_G_SCOREBOARD_PING,
    Q3N_G_SCOREBOARD_SCORE, Q3N_G_SCOREBOARD_TIME,
    Q3N_G_SMOKE_PUFF, Q3N_G_SMOKE_RAGEPRO, Q3N_G_SHOTGUN_SMOKE, Q3N_G_NAIL_PUFF, Q3N_G_BLUE_PROX_MINE,
    Q3N_G_PLASMA_BALL, Q3N_G_BLOOD_TRAIL, Q3N_G_LAGOMETER, Q3N_G_CONNECTION,
    Q3N_G_WATER_BUBBLE, Q3N_G_TRACER, Q3N_G_SELECT, Q3N_G_BACK_TILE, Q3N_G_NOAMMO,
    Q3N_G_QUAD, Q3N_G_QUAD_WEAPON, Q3N_G_BATTLE_SUIT, Q3N_G_BATTLE_WEAPON,
    Q3N_G_INVIS, Q3N_G_REGEN, Q3N_G_HASTE_PUFF,
    Q3N_G_RED_CUBE, Q3N_G_BLUE_CUBE, Q3N_G_RED_CUBE_ICON, Q3N_G_BLUE_CUBE_ICON,
    Q3N_G_RED_FLAG, Q3N_G_BLUE_FLAG, Q3N_G_FLAG_POLE, Q3N_G_FLAG_FLAP,
    Q3N_G_RED_FLAG_SKIN, Q3N_G_BLUE_FLAG_SKIN, Q3N_G_NEUTRAL_FLAG_SKIN,
    Q3N_G_RED_FLAG_BASE, Q3N_G_BLUE_FLAG_BASE, Q3N_G_NEUTRAL_FLAG_BASE, Q3N_G_NEUTRAL_FLAG,
    Q3N_G_OVERLOAD_BASE, Q3N_G_OVERLOAD_TARGET, Q3N_G_OVERLOAD_LIGHTS, Q3N_G_OVERLOAD_ENERGY,
    Q3N_G_HARVESTER, Q3N_G_HARVESTER_RED_SKIN, Q3N_G_HARVESTER_BLUE_SKIN, Q3N_G_HARVESTER_NEUTRAL,
    Q3N_G_RED_KAMIKAZE, Q3N_G_DUST_PUFF, Q3N_G_FRIEND, Q3N_G_RED_QUAD, Q3N_G_TEAM_STATUS_BAR,
    Q3N_G_BLUE_KAMIKAZE, Q3N_G_ARMOR, Q3N_G_ARMOR_ICON, Q3N_G_MACHINEGUN_BRASS, Q3N_G_SHOTGUN_BRASS,
    Q3N_G_GIB_ABDOMEN, Q3N_G_GIB_ARM, Q3N_G_GIB_CHEST, Q3N_G_GIB_FIST, Q3N_G_GIB_FOOT,
    Q3N_G_GIB_FOREARM, Q3N_G_GIB_INTESTINE, Q3N_G_GIB_LEG, Q3N_G_GIB_SKULL, Q3N_G_GIB_BRAIN,
    Q3N_G_SMOKE2, Q3N_G_BALLOON, Q3N_G_BLOOD_EXPLOSION, Q3N_G_BULLET_FLASH, Q3N_G_RING_FLASH,
    Q3N_G_DISH_FLASH, Q3N_G_TELEPORT_MODEL, Q3N_G_TELEPORT_SHADER, Q3N_G_KAMIKAZE_EFFECT,
    Q3N_G_KAMIKAZE_SHOCKWAVE, Q3N_G_KAMIKAZE_HEAD, Q3N_G_KAMIKAZE_TRAIL,
    Q3N_G_GUARD_PLAYER, Q3N_G_SCOUT_PLAYER, Q3N_G_DOUBLER_PLAYER, Q3N_G_AMMOREGEN_PLAYER,
    Q3N_G_INVULNERABILITY_IMPACT, Q3N_G_INVULNERABILITY_JUICED, Q3N_G_MEDKIT_USAGE,
    Q3N_G_HEART, Q3N_G_INVULNERABILITY_PLAYER, Q3N_G_MEDAL_IMPRESSIVE, Q3N_G_MEDAL_EXCELLENT,
    Q3N_G_MEDAL_GAUNTLET, Q3N_G_MEDAL_DEFEND, Q3N_G_MEDAL_ASSIST, Q3N_G_MEDAL_CAPTURE,
    Q3N_G_BULLET_MARK, Q3N_G_BURN_MARK, Q3N_G_HOLE_MARK, Q3N_G_ENERGY_MARK,
    Q3N_G_SHADOW_MARK, Q3N_G_WAKE_MARK, Q3N_G_BLOOD_MARK,
    Q3N_G_PATROL, Q3N_G_ASSAULT, Q3N_G_CAMP, Q3N_G_FOLLOW, Q3N_G_DEFEND, Q3N_G_TEAM_LEADER,
    Q3N_G_RETRIEVE, Q3N_G_ESCORT, Q3N_G_CURSOR, Q3N_G_SIZE_CURSOR, Q3N_G_SELECT_CURSOR,
    Q3N_G_LIGHTNING_SHADER, Q3N_G_LIGHTNING_EXPLOSION,
    Q3N_G_BULLET_EXPLOSION, Q3N_G_ROCKET_EXPLOSION, Q3N_G_GRENADE_EXPLOSION,
    Q3N_G_PLASMA_EXPLOSION, Q3N_G_RAIL_EXPLOSION, Q3N_G_BFG_EXPLOSION, Q3N_G_RAIL_RINGS, Q3N_G_RAIL_CORE,
    Q3N_GRAPHIC_COUNT
} q3n_graphic;
typedef enum q3n_sound {
    Q3N_S_ONE_MINUTE, Q3N_S_FIVE_MINUTES, Q3N_S_SUDDEN_DEATH, Q3N_S_ONE_FRAG, Q3N_S_TWO_FRAGS, Q3N_S_THREE_FRAGS,
    Q3N_S_COUNT3, Q3N_S_COUNT2, Q3N_S_COUNT1, Q3N_S_FIGHT, Q3N_S_PREPARE, Q3N_S_PREPARE_TEAM,
    Q3N_S_CAPTURE_AWARD, Q3N_S_RED_LEADS, Q3N_S_BLUE_LEADS, Q3N_S_TEAMS_TIED, Q3N_S_HIT_TEAM,
    Q3N_S_RED_SCORED, Q3N_S_BLUE_SCORED, Q3N_S_CAPTURE_YOUR_TEAM, Q3N_S_CAPTURE_OPPONENT,
    Q3N_S_RETURN_YOUR_TEAM, Q3N_S_RETURN_OPPONENT, Q3N_S_TAKEN_YOUR_TEAM, Q3N_S_TAKEN_OPPONENT,
    Q3N_S_RED_FLAG_RETURNED, Q3N_S_BLUE_FLAG_RETURNED, Q3N_S_ENEMY_TOOK_YOUR_FLAG, Q3N_S_YOUR_TEAM_TOOK_ENEMY_FLAG,
    Q3N_S_NEUTRAL_FLAG_RETURNED, Q3N_S_YOUR_TEAM_TOOK_FLAG, Q3N_S_ENEMY_TOOK_FLAG, Q3N_S_YOU_HAVE_FLAG,
    Q3N_S_HOLY_SHIT, Q3N_S_BASE_UNDER_ATTACK, Q3N_S_TRACER, Q3N_S_SELECT, Q3N_S_WEAR_OFF, Q3N_S_USE_NOTHING,
    Q3N_S_GIB, Q3N_S_GIB_BOUNCE1, Q3N_S_GIB_BOUNCE2, Q3N_S_GIB_BOUNCE3,
    Q3N_S_USE_INVULNERABILITY, Q3N_S_INVULNERABILITY_IMPACT1, Q3N_S_INVULNERABILITY_IMPACT2,
    Q3N_S_INVULNERABILITY_IMPACT3, Q3N_S_INVULNERABILITY_JUICED,
    Q3N_S_OBELISK_HIT1, Q3N_S_OBELISK_HIT2, Q3N_S_OBELISK_HIT3, Q3N_S_OBELISK_RESPAWN,
    Q3N_S_AMMOREGEN, Q3N_S_DOUBLER, Q3N_S_GUARD, Q3N_S_SCOUT,
    Q3N_S_TELE_IN, Q3N_S_TELE_OUT, Q3N_S_RESPAWN, Q3N_S_NOAMMO, Q3N_S_TALK, Q3N_S_LAND,
    Q3N_S_HIT, Q3N_S_HIT_HIGH_ARMOR, Q3N_S_HIT_LOW_ARMOR,
    Q3N_S_IMPRESSIVE, Q3N_S_EXCELLENT, Q3N_S_DENIED, Q3N_S_HUMILIATION, Q3N_S_ASSIST, Q3N_S_DEFEND,
    Q3N_S_FIRST_IMPRESSIVE, Q3N_S_FIRST_EXCELLENT, Q3N_S_FIRST_HUMILIATION,
    Q3N_S_TAKEN_LEAD, Q3N_S_TIED_LEAD, Q3N_S_LOST_LEAD, Q3N_S_VOTE_NOW, Q3N_S_VOTE_PASSED, Q3N_S_VOTE_FAILED,
    Q3N_S_WATER_IN, Q3N_S_WATER_OUT, Q3N_S_WATER_UNDER, Q3N_S_JUMP_PAD,
    Q3N_S_FLIGHT, Q3N_S_MEDKIT, Q3N_S_QUAD, Q3N_S_RICOCHET1, Q3N_S_RICOCHET2, Q3N_S_RICOCHET3,
    Q3N_S_RAIL_FIRE, Q3N_S_ROCKET_EXPLOSION, Q3N_S_PLASMA_EXPLOSION, Q3N_S_PROX_EXPLOSION,
    Q3N_S_NAIL_HIT, Q3N_S_NAIL_FLESH, Q3N_S_NAIL_METAL, Q3N_S_CHAINGUN_HIT, Q3N_S_CHAINGUN_FLESH, Q3N_S_CHAINGUN_METAL,
    Q3N_S_WEAPON_HOVER, Q3N_S_KAMIKAZE_EXPLODE, Q3N_S_KAMIKAZE_IMPLODE, Q3N_S_KAMIKAZE_FAR,
    Q3N_S_WINNER, Q3N_S_LOSER, Q3N_S_YOU_SUCK, Q3N_S_PROX_FLESH, Q3N_S_PROX_METAL, Q3N_S_PROX_HIT, Q3N_S_PROX_ACTIVATE,
    Q3N_S_REGEN, Q3N_S_PROTECT, Q3N_S_NORMAL_HEALTH, Q3N_S_GRENADE_BOUNCE1, Q3N_S_GRENADE_BOUNCE2,
    Q3N_S_LIGHTNING_HIT1, Q3N_S_LIGHTNING_HIT2, Q3N_S_LIGHTNING_HIT3, Q3N_S_CHAINGUN_WIND,
    Q3N_SOUND_COUNT
} q3n_sound;
typedef struct q3n_inline_media { int32_t model; qa_vec3 midpoint; } q3n_inline_media;
typedef struct q3n_media_view {
    qa_q3_product product;
    size_t item_count, inline_count;
    q3n_item_media items[256];
    q3n_weapon_media weapons[16];
    int32_t graphics[Q3N_GRAPHIC_COUNT], sounds[Q3N_SOUND_COUNT];
    int32_t number_shaders[11], bot_skill_shaders[5], crosshairs[10];
    int32_t red_flag_shaders[3], blue_flag_shaders[3], neutral_flag_shaders[4], flag_status_shaders[3];
    int32_t footsteps[7][4];
    int32_t game_models[256], game_sounds[256];
    const q3n_inline_media *inline_models;
} q3n_media_view;
typedef struct q3n_media_options {
    qa_q3_product product;
    qa_q3_presentation_assets *assets;
    q3n_remote_source *remote_source;
    q3n_compiled_source *compiled_source;
} q3n_media_options;
typedef struct q3n_loading_media {
    qa_q3_product product;
    qa_q3_presentation_assets *assets;
    const q3n_remote_source *remote_source;
    const q3n_compiled_source *compiled_source;
    int32_t proportional;
    bool initialized;
} q3n_loading_media;
/* The GAME cut qualifies physical source ownership; the reader supplies reached
 * client configstrings. World binding belongs to the outer owner. */
typedef struct q3n_media_load {
    qa_application *application;
    const qa_application_native_q3_presentation *source;
    qa_native_q3_wire_reader *reader;
    /* Actual remote CLIENT receipt, mutually exclusive with GAME/reader. */
    const q3n_remote_source_view *remote;
    const q3n_compiled_source_view *compiled;
    void *context;
    bool (*loading)(void *, const char *, int32_t item_or_minus_one, qa_error *);
    int32_t game_type;
    uint32_t inline_models;
    bool build_script;
} q3n_media_load;
bool q3n_media_create(const q3n_media_options *, q3n_media **, qa_error *);
void q3n_media_destroy(q3n_media *);
bool q3n_media_idle(const q3n_media *);
const q3n_media_view *q3n_media_read(const q3n_media *);
qa_q3_presentation_assets *q3n_media_assets(const q3n_media *);
bool q3n_media_remote_current(const q3n_media *, const q3n_remote_source_view *, qa_error *);
bool q3n_media_compiled_current(const q3n_media *, const q3n_compiled_source_view *, qa_error *);
bool q3n_media_compiled_configstring_changed(q3n_media *, const q3n_compiled_source_view *, uint32_t, qa_error *);
/* Pure observation is valid inside an authored loading callback while the
 * media owner is busy. It performs no registration or renderer work. */
bool q3n_media_loading_read(const q3n_media *, q3n_loading_media *, qa_error *);
bool q3n_media_loading_graphics(q3n_media *, qa_error *);
bool q3n_media_load_sounds(q3n_media *, const q3n_media_load *, qa_error *);
bool q3n_media_load_graphics(q3n_media *, const q3n_media_load *, qa_error *);
/* The standalone effect/local-entity bundle follows the actual selected
 * source product and content. It never initializes GAME/configstring media. */
bool q3n_media_load_effects(q3n_media *, qa_application *,
    const qa_application_selected_effects *, const qa_application_effect_event *, qa_error *);
bool q3n_media_effects_ready(const q3n_media *);
bool q3n_media_effects_current(const q3n_media *, qa_application *,
    const qa_application_selected_effects *, const qa_application_effect_event *, qa_error *);
bool q3n_media_register_item(q3n_media *, uint32_t, qa_error *);
bool q3n_media_register_weapon(q3n_media *, uint32_t, qa_error *);
/* CL_GetServerCommand has already reached this exact cs row. Registration is
 * performed for this command even when its text repeats the previous value. */
bool q3n_media_configstring_changed(q3n_media *, qa_native_q3_wire_reader *,
    uint32_t index, qa_error *);
bool q3n_media_remote_configstring_changed(q3n_media *, const q3n_remote_source_view *,
    uint32_t index, qa_error *);
/* Aggregate imports the actual backend registry first. These pure codecs
 * require its capture lease and retain only real numeric holder references. */
bool q3n_media_checkpoint(const q3n_media *, qa_buffer *, qa_error *);
bool q3n_media_restore(q3n_media *, qa_bytes, qa_error *);

#endif
