#ifndef QA_CVAR_CATALOG_GENERATED_H
#define QA_CVAR_CATALOG_GENERATED_H
#include <stddef.h>
#include <stdint.h>
/* Generated dialect order matches QA_RULESET_NETQUAKE..QA_RULESET_Q3. */
#define QA_CVAR_CATALOG_DIALECTS 5
#define QA_CVAR_CATALOG_NO_ROW UINT16_MAX
/* Strings are offsets into one immutable UTF-8 pool; offset zero is empty. */
typedef uint32_t qa_cvar_catalog_text;
typedef enum qa_cvar_catalog_type {
    QA_CATALOG_FLOAT, QA_CATALOG_INT, QA_CATALOG_BOOL, QA_CATALOG_ENUM,
    QA_CATALOG_STRING, QA_CATALOG_BITMASK
} qa_cvar_catalog_type;
typedef enum qa_cvar_catalog_side {
    QA_CATALOG_ANY_SIDE, QA_CATALOG_CLIENT, QA_CATALOG_SERVER
} qa_cvar_catalog_side;
typedef enum qa_cvar_catalog_default_kind {
    QA_CATALOG_NATIVE_DEFAULT, QA_CATALOG_UNSET_DEFAULT,
    QA_CATALOG_STORED_ONLY, QA_CATALOG_ANTHOLOGY_DEFAULT,
    QA_CATALOG_CONDITIONAL_DEFAULT
} qa_cvar_catalog_default_kind;
enum qa_cvar_catalog_issue {
    QA_CATALOG_TYPE_HINT = 1u << 0, QA_CATALOG_DEFAULT_POLICY = 1u << 1,
    QA_CATALOG_FLAG_POLICY = 1u << 2, QA_CATALOG_CONVERSION_POLICY = 1u << 3,
    QA_CATALOG_HOME_POLICY = 1u << 4, QA_CATALOG_PORT_DISAGREEMENT = 1u << 5
};
typedef enum qa_cvar_catalog_condition {
    QA_CATALOG_CONDITION_ALWAYS,
    QA_CATALOG_CONDITION_MAC,
    QA_CATALOG_CONDITION_NOT_MAC,
    QA_CATALOG_CONDITION_LINUX,
    QA_CATALOG_CONDITION_NOT_LINUX,
    QA_CATALOG_CONDITION_DEDICATED,
    QA_CATALOG_CONDITION_CLIENT,
    QA_CATALOG_CONDITION_ENGINE,
    QA_CATALOG_CONDITION_GAME,
    QA_CATALOG_CONDITION_CGAME,
    QA_CATALOG_CONDITION_UNRESOLVED,
    QA_CATALOG_CONDITION_WINDOWS,
    QA_CATALOG_CONDITION_NOT_WINDOWS,
} qa_cvar_catalog_condition;
typedef struct qa_cvar_catalog_default {
    qa_cvar_catalog_text member, value, condition, raw;
    uint32_t issues;
    uint8_t kind, condition_kind;
} qa_cvar_catalog_default;
enum qa_cvar_catalog_auxiliary {
    QA_CATALOG_PRIVATE = 1, QA_CATALOG_NO_ARCHIVE = 2,
    QA_CATALOG_GAME = 4, QA_CATALOG_FILES = 8, QA_CATALOG_REFRESH = 16,
    QA_CATALOG_SOUND = 32, QA_CATALOG_SERVER_NOTIFY = 64
};
typedef struct qa_cvar_catalog_flags {
    qa_cvar_catalog_text member, raw;
    uint32_t flags, auxiliary, issues;
} qa_cvar_catalog_flags;
typedef struct qa_cvar_catalog_dialect {
    qa_cvar_catalog_text raw_default, raw_flags, effect;
    uint32_t default_first, flags_first;
    uint16_t default_count, flags_count;
} qa_cvar_catalog_dialect;
typedef enum qa_cvar_catalog_conversion_kind {
    QA_CATALOG_IDENTITY, QA_CATALOG_RECIPROCAL, QA_CATALOG_BOOL_INVERT,
    QA_CATALOG_LINEAR, QA_CATALOG_ENUM_DETAIL, QA_CATALOG_BIT_VIEW,
    QA_CATALOG_COMPOSITE, QA_CATALOG_RESOLUTION, QA_CATALOG_CONSUMER_UNITS,
    QA_CATALOG_SIDE_SCOPE, QA_CATALOG_POLICY
} qa_cvar_catalog_conversion_kind;
typedef struct qa_cvar_catalog_operand {
    uint16_t row_index;
    uint8_t inverted, predicate;
    uint32_t unified_bit;
    uint32_t bits[QA_CVAR_CATALOG_DIALECTS];
} qa_cvar_catalog_operand;
typedef enum qa_cvar_catalog_map_direction {
    QA_CATALOG_ALIAS_TO_CANONICAL, QA_CATALOG_CANONICAL_TO_ALIAS
} qa_cvar_catalog_map_direction;
typedef struct qa_cvar_catalog_map {
    double alias_value, canonical_value;
    uint8_t direction;
} qa_cvar_catalog_map;
typedef enum qa_cvar_catalog_role {
    QA_CATALOG_ANY_ROLE, QA_CATALOG_CGAME
} qa_cvar_catalog_role;
typedef enum qa_cvar_catalog_operation {
    QA_CATALOG_OP_NONE,
    QA_CATALOG_OP_KHZ_HZ,
    QA_CATALOG_OP_SKILL,
    QA_CATALOG_OP_VIEW_SIZE,
    QA_CATALOG_OP_BOOL_DETAIL,
    QA_CATALOG_OP_AUTOSWITCH,
    QA_CATALOG_OP_GUN,
    QA_CATALOG_OP_FOOTSTEPS,
    QA_CATALOG_OP_LAGOMETER,
    QA_CATALOG_OP_DRAW_2D,
    QA_CATALOG_OP_SHADOWS,
    QA_CATALOG_OP_OLD_RAIL,
    QA_CATALOG_OP_INPUT_GRAB,
    QA_CATALOG_OP_SOUND_BACKEND,
    QA_CATALOG_OP_NO_SKINS,
    QA_CATALOG_OP_FORCE_RESPAWN,
    QA_CATALOG_OP_DEATHMATCH,
    QA_CATALOG_OP_COOP,
    QA_CATALOG_OP_TEAMPLAY,
    QA_CATALOG_OP_CTF,
    QA_CATALOG_OP_QW_SKIN,
    QA_CATALOG_OP_SEX,
    QA_CATALOG_OP_COLOR,
    QA_CATALOG_OP_PLAYER_COLORS,
    QA_CATALOG_OP_NEEDPASS,
    QA_CATALOG_OP_SAME_LEVEL,
    QA_CATALOG_OP_NO_EXIT,
    QA_CATALOG_OP_DOWNLOAD,
    QA_CATALOG_OP_CLEAR_COLOR,
    QA_CATALOG_OP_FULLSCREEN,
    QA_CATALOG_OP_VIDEO_MODE,
    QA_CATALOG_OP_MUSIC_MUTE,
    QA_CATALOG_OP_SPECTATOR,
} qa_cvar_catalog_operation;
typedef struct qa_cvar_catalog_conversion {
    double scale, offset, lower, upper;
    qa_cvar_catalog_text raw;
    uint32_t operand_first, map_first, issues;
    uint16_t operand_count, map_count;
    uint8_t kind, detail_required, role_scope, operation;
} qa_cvar_catalog_conversion;
typedef struct qa_cvar_catalog_binding {
    qa_cvar_catalog_text name;
    uint16_t row_index, conversion[QA_CVAR_CATALOG_DIALECTS];
    uint8_t side_scope, canonical, native_dialects, seat;
} qa_cvar_catalog_binding;
enum qa_cvar_catalog_policy {
    QA_CATALOG_POLICY_LATCH_ALL = 1u << 0,
    QA_CATALOG_POLICY_PRIVATE = 1u << 1
};
typedef struct qa_cvar_catalog_row {
    qa_cvar_catalog_text name, aliases, range_hint, owner, conversion, sources, raw_flags;
    qa_cvar_catalog_text status[3];
    qa_cvar_catalog_dialect dialect[QA_CVAR_CATALOG_DIALECTS];
    uint32_t issues, archive_flags, policies;
    uint16_t rule_conversion;
    uint8_t type_hint, home_dialect, family_count, not_stored;
} qa_cvar_catalog_row;
extern const char *const qa_cvar_catalog_strings;
extern const qa_cvar_catalog_row qa_cvar_catalog_rows[];
extern const qa_cvar_catalog_binding qa_cvar_catalog_bindings[];
extern const qa_cvar_catalog_default qa_cvar_catalog_defaults[];
extern const qa_cvar_catalog_flags qa_cvar_catalog_flag_clauses[];
extern const qa_cvar_catalog_conversion qa_cvar_catalog_conversions[];
extern const qa_cvar_catalog_operand qa_cvar_catalog_operands[];
extern const qa_cvar_catalog_map qa_cvar_catalog_maps[];
extern const size_t qa_cvar_catalog_row_count, qa_cvar_catalog_binding_count;
extern const size_t qa_cvar_catalog_default_count, qa_cvar_catalog_flag_clause_count;
extern const size_t qa_cvar_catalog_conversion_count, qa_cvar_catalog_operand_count;
extern const size_t qa_cvar_catalog_map_count;
static inline const char *qa_cvar_catalog_string(qa_cvar_catalog_text offset) {
    return qa_cvar_catalog_strings + offset;
}
#endif
