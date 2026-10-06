#ifndef QA_APPLICATION_GUEST_Q3_WEAPON_PROFILE_H
#define QA_APPLICATION_GUEST_Q3_WEAPON_PROFILE_H

#include "qa/gameplay.h"
#include "qa/qvm.h"
#include "qa/strings.h"

struct q3g_role;
typedef struct application_q3_weapon_catalog_entry {
    int32_t weapon;
    qa_item_id item, ammo;
} application_q3_weapon_catalog_entry;
typedef struct application_q3_weapon_test {
    uint32_t offset, mask;
    int32_t value;
    bool masked, at_most;
} application_q3_weapon_test;
typedef struct application_q3_weapon_predicate { uint32_t instruction; bool unselected; } application_q3_weapon_predicate;
typedef struct application_q3_weapon_region { uint32_t entry, join; } application_q3_weapon_region;
typedef struct application_q3_weapon_context { qa_string_id provider; qa_item_id item; } application_q3_weapon_context;
typedef struct application_q3_weapon_team_command {
    qa_string_id source;
    qa_team_id team;
    qa_string_id *arguments;
    size_t argument_count;
} application_q3_weapon_team_command;
typedef struct application_q3_weapon_profile {
    const qa_qvm_image *image;
    qa_qvm_abi abi;
    uint32_t entity_stride, client_stride, client_pointer;
    uint32_t dispatcher, request, request_argument, selection_offset;
    bool pointer_global;
    uint32_t pointer_base, pointer_offset, *indirections;
    size_t indirection_count;
    application_q3_weapon_predicate *predicates;
    size_t predicate_count;
    application_q3_weapon_test *settled, *accepted;
    size_t settled_count, accepted_count;
    application_q3_weapon_catalog_entry *catalog;
    size_t catalog_count;
    uint32_t movement_move, movement_slice, movement_duck, movement_global;
    uint32_t movement_mins, movement_maxs, movement_trace_callback, movement_trace_mask;
    application_q3_weapon_region locomotion;
    bool body_trace;
    uint32_t max_health, persistent_max_health;
    struct {
        uint32_t movement_type, health, team, flags;
        int32_t spectator_team, respawn_flag, *excluded;
        size_t excluded_count;
    } availability;
    uint32_t powerups[3];
    struct { uint32_t entry; int32_t attack, melee; } animation;
    uint32_t water_entity, water_movement;
    struct { uint32_t entry, result; application_q3_weapon_region stop; } damage;
    application_q3_weapon_context *contexts;
    size_t context_count;
    qa_qvm_region_evaluation delay, teleport, objectives;
    uint32_t *delay_inputs, *teleport_inputs, *objective_inputs;
    uint32_t delay_global, delay_player;
    uint32_t teleport_entry, spawn, view;
    struct { uint32_t entry, argument, weapon, ammo; bool inventory; application_q3_weapon_region region; } drop;
    struct {
        uint32_t entry, argument, weapons, ammo, name, item;
        application_q3_weapon_region named;
    } give;
    bool has_match, present, catalog_qualified;
    uint32_t score;
    application_q3_weapon_team_command *teams;
    size_t team_count;
} application_q3_weapon_profile;

/* catalog is the actual source item-table roster, not a fixed family table.
 * All owned arrays preserve its physical order and the admitted declaration.
 * Unknown undeclared artifacts return a genuinely absent profile. */
bool application_q3_weapon_profile_read(struct q3g_role *, qa_bytes primary,
    const application_q3_weapon_catalog_entry *, size_t, application_q3_weapon_profile *, qa_error *);
void application_q3_weapon_profile_free(application_q3_weapon_profile *);
/* A declared live table is genuinely absent before its source Init. Validate
 * its actual roster once populated, or after detached source RAM restoration.
 * This does not execute or manufacture the table. */
bool application_q3_weapon_profile_catalog(application_q3_weapon_profile *,
    const application_q3_weapon_catalog_entry *, size_t, qa_error *);

#endif
