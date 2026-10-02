#ifndef QA_BOT_LIBRARY_H
#define QA_BOT_LIBRARY_H

#include "qa/math.h"
#include "qa/script.h"

#define QA_BOT_NO_INDEX UINT32_MAX
#define QA_BOT_CHARACTERISTICS 81
typedef struct qa_bot_random_source {
    void *context;
    uint32_t (*next)(void *);
} qa_bot_random_source;
typedef struct qa_bot_library_options {
    qa_script_services scripts;
    qa_script_options preprocessor;
    bool reload_characters;
} qa_bot_library_options;
typedef struct qa_bot_library qa_bot_library;
typedef struct qa_bot_log qa_bot_log;
/* This owner is confined to the source session thread. File/diagnostic/RNG
 * callbacks must keep their owners alive until the enclosing operation returns.
 * Retained resource references survive library destruction. */
bool qa_bot_library_create(const qa_bot_library_options *, qa_bot_library **, qa_error *);
void qa_bot_library_destroy(qa_bot_library *);
bool qa_bot_library_idle(const qa_bot_library *);
bool qa_bot_library_weights_shutdown(qa_bot_library *,qa_error *);
void qa_bot_library_reload(qa_bot_library *, bool);
bool qa_bot_library_pc_bind(qa_bot_library *, void *, bool (*)(void *),
    bool (*)(void *, bool source, qa_error *), qa_error *);
bool qa_bot_library_pc_close(qa_bot_library *, bool source, qa_error *);
void qa_bot_library_pc_unbind(qa_bot_library *, const void *);
const qa_script_defines *qa_bot_library_global_defines(const qa_bot_library *);
bool qa_bot_library_global_define(qa_bot_library *, const char *definition, qa_error *);
/* The runtime owns this logger and disposes it after the library. Binding and
 * importing resource data perform no file operations. */
bool qa_bot_library_log_bind(qa_bot_library *, qa_bot_log *, qa_error *);
qa_bot_log *qa_bot_library_log(const qa_bot_library *);
typedef struct qa_bot_variable {
    const char *name, *string;
    int32_t flags;
    bool modified;
    float value;
} qa_bot_variable;
/* Variable records are stable until library destruction/clear. A variable's
 * string is borrowed until its next successful set. Names compare in ASCII
 * source order without locale; a set marks modified even when text is equal. */
const qa_bot_variable *qa_bot_library_variable(const qa_bot_library *, const char *name);
/* The optional byte reader models a borrowed name; NULL is a null source name.
 * Only compared bytes are consumed, in reverse variable insertion order. */
bool qa_bot_library_variable_find_from(const qa_bot_library *, void *context,
                                       bool (*byte)(void *, size_t, uint8_t *, qa_error *),
                                       const qa_bot_variable **, qa_error *);
bool qa_bot_library_variable_default(qa_bot_library *, const char *name, const char *value,
                                     const qa_bot_variable **, qa_error *);
bool qa_bot_library_variable_set(qa_bot_library *, const char *name, const char *value, qa_error *);
void qa_bot_library_variable_unmodify(qa_bot_library *, const char *name);
const qa_bot_variable *qa_bot_library_variable_at(const qa_bot_library *, size_t source_index);
void qa_bot_library_variables_clear(qa_bot_library *);
bool qa_bot_library_variable_restore(qa_bot_library *, const qa_bot_variable *, qa_error *);
bool qa_bot_variable_number(const char *, float *, qa_error *);
typedef struct qa_bot_weight_node {
    int32_t inventory, threshold;
    uint32_t child, next;
    bool balanced;
} qa_bot_weight_node;
typedef struct qa_bot_weight_value {
    float weight, minimum, maximum;
} qa_bot_weight_value;
typedef struct qa_bot_weight_definition {
    const char *name;
    uint32_t root, end;
} qa_bot_weight_definition;
typedef struct qa_bot_weights_view {
    const char *path;
    const qa_bot_weight_definition *weights;
    const qa_bot_weight_node *nodes;
    const qa_bot_weight_value *values;
    size_t weight_count, node_count;
    int32_t maximum_inventory_index;
} qa_bot_weights_view;
typedef struct qa_bot_weights qa_bot_weights;
typedef struct qa_bot_weight_workspace qa_bot_weight_workspace;
bool qa_bot_weights_load(qa_bot_library *, const char *, qa_bot_weights **, qa_error *);
bool qa_bot_weights_load_result(qa_bot_library *,const char *,qa_bot_weights **,bool *source_failure,qa_error *);
void qa_bot_weights_retain(qa_bot_weights *);
void qa_bot_weights_release(qa_bot_weights *);
/* Explicit source FreeWeightConfig; native reference release is pure. */
bool qa_bot_weights_free(qa_bot_weights *,qa_error *);
/* A metadata projection of the current source bytes. The borrowed projection
 * lasts until the next read/release; an invalid source configuration returns
 * NULL. Evaluation and mutation use the source allocation directly. */
const qa_bot_weights_view *qa_bot_weights_read(const qa_bot_weights *);
bool qa_bot_weights_clone(const qa_bot_weights *, qa_bot_weights **, qa_error *);
bool qa_bot_weights_restore(const qa_bot_weights_view *, qa_bot_weights **, qa_error *);
int32_t qa_bot_weights_find(const qa_bot_weights *, const char *);
bool qa_bot_weights_find_value(const qa_bot_weights *,const char *,int32_t *,qa_error *);
bool qa_bot_weight_workspace_create(qa_bot_weight_workspace **, qa_error *);
void qa_bot_weight_workspace_destroy(qa_bot_weight_workspace *);
typedef struct qa_bot_inventory_view {
    const int32_t *data;
    size_t count;
    void *context;
    bool (*read)(void *, int32_t index, int32_t *, qa_error *);
} qa_bot_inventory_view;
bool qa_bot_weights_evaluate_view(const qa_bot_weights *, uint32_t, const qa_bot_inventory_view *,
                                  const qa_bot_random_source *, qa_bot_weight_workspace *, float *,
                                  qa_error *);
bool qa_bot_weights_evaluate(const qa_bot_weights *, uint32_t, const int32_t *inventory, size_t,
                             const qa_bot_random_source *undecided, qa_bot_weight_workspace *,
                             float *, qa_error *);
bool qa_bot_weights_scale(qa_bot_weights *, const char *, float, qa_error *);
bool qa_bot_weights_scale_range(qa_bot_weights *, float, qa_error *);
bool qa_bot_weights_evolve(qa_bot_weights *, const qa_bot_random_source *, qa_error *);
/* Source-order mutations survive a topology mismatch. matched reports that
 * mismatch independently of allocation/argument errors. */
bool qa_bot_weights_interbreed(qa_bot_weights *, const qa_bot_weights *, const qa_bot_weights *,
                               bool *matched, qa_error *);
bool qa_bot_weights_interbreed_report(qa_bot_weights *, const qa_bot_weights *,
                                       const qa_bot_weights *, void *context,
                                       bool (*report)(void *, const char *,qa_error *), bool *matched, qa_error *);
typedef enum qa_bot_character_value_kind {
    QA_BOT_CHARACTER_UNSET,
    QA_BOT_CHARACTER_INTEGER,
    QA_BOT_CHARACTER_FLOAT,
    QA_BOT_CHARACTER_STRING
} qa_bot_character_value_kind;
typedef struct qa_bot_character_value {
    qa_bot_character_value_kind kind;
    union {
        int32_t integer;
        float number;
        const char *string;
    } data;
} qa_bot_character_value;
typedef struct qa_bot_character_view {
    const char *path;
    float skill;
    qa_bot_character_value values[QA_BOT_CHARACTERISTICS];
} qa_bot_character_view;
typedef struct qa_bot_character qa_bot_character;
bool qa_bot_character_load(qa_bot_library *, const char *, float, qa_bot_character **, qa_error *);
void qa_bot_character_retain(qa_bot_character *);
void qa_bot_character_release(qa_bot_character *);
const qa_bot_character_view *qa_bot_character_read(const qa_bot_character *);
bool qa_bot_character_view_read(const qa_bot_character *, const qa_bot_character_view **, qa_error *);
bool qa_bot_character_restore(const qa_bot_character_view *, qa_bot_character **, qa_error *);
bool qa_bot_character_free(qa_bot_character *, qa_error *);
/* Saved float values include IEEE results produced by skill interpolation. */
bool qa_bot_character_float(const qa_bot_character *, uint32_t, float *, qa_error *);
bool qa_bot_character_integer(const qa_bot_character *, uint32_t, int32_t *, qa_error *);
bool qa_bot_character_string(const qa_bot_character *, uint32_t, const char **, qa_error *);
bool qa_bot_character_bounded_float(const qa_bot_character *, uint32_t, float, float, float *,
                                    qa_error *);
bool qa_bot_character_bounded_integer(const qa_bot_character *, uint32_t, int32_t, int32_t,
                                      int32_t *, qa_error *);
typedef enum qa_bot_genetic_status {
    QA_BOT_GENETIC_SELECTED,
    QA_BOT_GENETIC_TOO_MANY,
    QA_BOT_GENETIC_TOO_FEW,
    QA_BOT_GENETIC_RANDOM_ENDPOINT
} qa_bot_genetic_status;
typedef struct qa_bot_genetic_selection {
    qa_bot_genetic_status status;
    uint32_t parent1, parent2, child;
} qa_bot_genetic_selection;
typedef enum qa_bot_genetic_target {
    QA_BOT_GENETIC_PARENT1,
    QA_BOT_GENETIC_PARENT2,
    QA_BOT_GENETIC_CHILD
} qa_bot_genetic_target;
typedef struct qa_bot_genetic_source {
    const float *ranks;
    void *context;
    bool (*read)(void *, int32_t, float *, qa_error *);
    bool (*write)(void *, qa_bot_genetic_target, int32_t, qa_error *);
    void (*warning)(void *, const char *);
} qa_bot_genetic_source;
bool qa_bot_genetic_select_from(int32_t count, const qa_bot_genetic_source *,
                                const qa_bot_random_source *, qa_bot_genetic_selection *,
                                qa_error *);
bool qa_bot_genetic_select(const float *, size_t, const qa_bot_random_source *,
                           qa_bot_genetic_selection *, qa_error *);

typedef struct qa_bot_projectile_info {
    char name[80], model[80];
    int32_t flags, damage, visible_damage, damage_type, health_increase;
    float gravity, radius, push, detonation, bounce, bounce_friction, bounce_stop;
} qa_bot_projectile_info;
typedef struct qa_bot_weapon_info {
    bool valid;
    int32_t number, level, weapon_inventory, flags, projectile_count, ammo_amount, ammo_inventory;
    char name[80], model[80], projectile[80];
    float horizontal_spread, vertical_spread, speed, acceleration, extra_z_velocity, activate,
        reload, spin_up, spin_down;
    qa_vec3 recoil, offset, angle_offset;
    uint32_t projectile_index;
} qa_bot_weapon_info;
typedef struct qa_bot_weapons_view {
    const char *path;
    const qa_bot_weapon_info *weapons;
    const qa_bot_projectile_info *projectiles;
    size_t weapon_capacity, projectile_capacity, weapon_count, projectile_count;
} qa_bot_weapons_view;
typedef struct qa_bot_weapons qa_bot_weapons;
bool qa_bot_weapons_load(qa_bot_library *, const char *, size_t weapon_capacity,
                         size_t projectile_capacity, qa_bot_weapons **, qa_error *);
void qa_bot_weapons_retain(qa_bot_weapons *);
void qa_bot_weapons_release(qa_bot_weapons *);
/* Metadata is projected from the retained raw source allocation on each
 * read. Its array/path spans last until the next read or release. Native
 * source algorithms and ABI copies use the actual byte getters. */
const qa_bot_weapons_view *qa_bot_weapons_read(const qa_bot_weapons *);
bool qa_bot_weapons_restore(const qa_bot_weapons_view *, qa_bot_weapons **, qa_error *);
typedef struct qa_bot_weapon_selector qa_bot_weapon_selector;
bool qa_bot_weapon_selector_create(qa_bot_weapons *, qa_bot_weights *, qa_bot_weapon_selector **,
                                   qa_error *);
void qa_bot_weapon_selector_destroy(qa_bot_weapon_selector *);
bool qa_bot_weapon_weight(qa_bot_weapon_selector *, uint32_t, const int32_t *, size_t, float *,
                          bool *found, qa_error *);
bool qa_bot_weapon_choose(qa_bot_weapon_selector *, const int32_t *, size_t, uint32_t *,
                          qa_error *);
bool qa_bot_weapon_weight_view(qa_bot_weapon_selector *, uint32_t, const qa_bot_inventory_view *,
                               float *, bool *, qa_error *);
bool qa_bot_weapon_choose_view(qa_bot_weapon_selector *, const qa_bot_inventory_view *, uint32_t *,
                               qa_error *);
typedef struct qa_bot_item_info {
    char classname[32], name[80], model[80];
    int32_t model_index, type, inventory, number;
    float respawn_seconds;
    qa_vec3 mins, maxs;
} qa_bot_item_info;
typedef struct qa_bot_items_view {
    const char *path;
    const qa_bot_item_info *items;
    size_t count, capacity;
} qa_bot_items_view;
typedef struct qa_bot_items qa_bot_items;
bool qa_bot_items_load(qa_bot_library *, const char *, size_t maximum, qa_bot_items **, qa_error *);
void qa_bot_items_retain(qa_bot_items *);
void qa_bot_items_release(qa_bot_items *);
const qa_bot_items_view *qa_bot_items_read(const qa_bot_items *);
bool qa_bot_items_view_read(const qa_bot_items *, const qa_bot_items_view **, qa_error *);
bool qa_bot_items_restore(const qa_bot_items_view *, qa_bot_items **, qa_error *);
bool qa_bot_items_free(qa_bot_items *, qa_error *);

#endif
