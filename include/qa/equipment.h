#ifndef QA_EQUIPMENT_H
#define QA_EQUIPMENT_H

#include "qa/game_q1.h"
#include "qa/game_q2.h"
#include "qa/game_q3.h"
#include "qa/modes.h"
#include "qa/equipment_weapon_slot.h"

typedef struct qa_equipment qa_equipment;
typedef enum qa_grapple_mechanic {
    QA_GRAPPLE_DISABLED,
    QA_GRAPPLE_THREEWAVE,
    QA_GRAPPLE_ROGUE,
    QA_GRAPPLE_Q2_CTF,
    QA_GRAPPLE_LMCTF,
    QA_GRAPPLE_Q3
} qa_grapple_mechanic;
typedef enum qa_equipment_binding {
    QA_EQUIPMENT_OFFHAND,
    QA_EQUIPMENT_WEAPON_SLOT
} qa_equipment_binding;
typedef struct qa_equipment_selection {
    qa_grapple_mechanic grapple;
    qa_equipment_binding binding;
    bool retain_on_weapon_change, release_on_jump, release_on_teleport;
    qa_q2_hand_grenade_options grenades;
} qa_equipment_selection;
typedef struct qa_equipment_controls {
    qa_vec3 view_angles, previous_velocity;
    float view_height, gravity, teleport_until;
    bool grapple_held, grenade_held, jump, primary_attack, spectator, prediction;
    bool haste, no_stack_double, players_collide;
    bool teleport_known;
    uint32_t teleport_sequence;
    qa_q2_hand hand;
    uint8_t water_level;
    int32_t water_type;
    uint64_t quad_until_ns, double_until_ns, quad_fire_until_ns;
} qa_equipment_controls;
typedef struct qa_equipment_source_selection {
    qa_actor_owner grapple, grenades, items;
} qa_equipment_source_selection;
typedef struct qa_equipment_source {
    qa_actor_owner owner;
    /* A genuine external weapon-slot source supplies its canonical item;
     * admission grants its count without replacing another source's catalog. */
    qa_item_id weapon_item;
    qa_q1_game *q1;
    qa_q2_game *q2;
    qa_q3_game *q3;
    qa_q3_product q3_product;
    void *context;
    /* These callbacks belong to one retained external grapple runtime. Native
     * source pointers and an external runtime are mutually exclusive. */
    bool (*current)(void *);
    bool (*admit)(void *, qa_actor_id, qa_error *);
    bool (*frame)(void *, uint64_t now_ns, uint64_t elapsed_ns, qa_error *);
    bool (*fire)(void *, qa_actor_id, const qa_equipment_controls *, qa_error *);
    bool (*release)(void *, qa_actor_id, bool force, qa_error *);
    bool (*pull)(void *, qa_actor_id, qa_vec3 forward, qa_vec3 *, bool *, qa_error *);
    bool (*saved_actor)(void *, qa_actor_id, qa_error *);
} qa_equipment_source;
typedef struct qa_equipment_state {
    qa_actor_id actor;
    qa_equipment_source_selection sources;
    qa_equipment_source_selection pending_sources;
    qa_equipment_selection selection;
    qa_equipment_selection pending_selection;
    qa_equipment_controls controls;
    bool grapple_pressed, grapple_released, grenade_pressed, grenade_released;
    bool slot_requested, slot_active, slot_holstering, slot_lowering, configuration_pending;
    bool previous_jump, teleport_seen;
    uint32_t teleport_sequence;
    qa_actor_owner primary_request_owner;
    qa_item_id primary_request_item;
    bool weapon_slot_present;
    qa_actor_owner primary_weapon_owner;
    qa_weapon_slot_state weapon_slot;
} qa_equipment_state;
typedef struct qa_equipment_options {
    qa_builtin_services services;
    qa_q1_game *q1;
    qa_q2_game *q2;
    qa_q3_game *q3;
    qa_actor_owner q3_owner;
    qa_q3_product q3_product;
    void *context;
    /* Resolve retained publication owners without executing or admitting a
     * source. Each actor keeps separate grapple, grenade and item tuples. */
    bool (*source)(void *, qa_actor_owner, qa_equipment_source *, qa_error *);
    void *source_context;
    bool (*source_idle)(const void *);
    bool (*source_destroy)(void *, qa_error *);
    bool (*source_capture)(void *, qa_buffer *, qa_error *);
    bool (*source_restore)(void *, qa_bytes, qa_error *);
    bool (*primary_holster)(void *, qa_actor_id, qa_error *);
    bool (*primary_owner)(void *, qa_actor_id, qa_actor_owner *, qa_error *);
    bool (*primary_holstered)(void *, qa_actor_id);
    bool (*primary_holstered_read)(void *,qa_actor_id,bool *,qa_error *);
    bool (*primary_resume)(void *, qa_actor_id, qa_error *);
    bool (*primary_accepts)(void *, qa_actor_id, qa_actor_owner, qa_item_id, bool *, qa_error *);
    bool (*primary_select)(void *, qa_actor_id, qa_actor_owner, qa_item_id, bool *, qa_error *);
    bool (*select_weapon)(void *, qa_actor_id, qa_item_id, qa_error *);
    qa_q2_hand_projection_fn grenade_projection;
    bool (*grenade_interval)(void *, qa_actor_id, qa_actor_owner, uint64_t native_ns,
                             uint64_t *, bool *handled, qa_error *);
} qa_equipment_options;

bool qa_equipment_create(const qa_equipment_options *, qa_equipment **, qa_error *);
void qa_equipment_destroy(qa_equipment *);
bool qa_equipment_destroy_checked(qa_equipment *, qa_error *);
bool qa_equipment_idle(const qa_equipment *);
/* The canonical equipment codec owns its current on-disk version. */
uint32_t qa_equipment_save_version(void);
/* Borrow the source-topology prefix before constructing its runtime. Actor
 * continuation remains subject to the subsequent full equipment restore.
 * Failed calls leave both outputs unchanged. */
bool qa_equipment_saved_source(qa_session *, qa_bytes, bool *present,
    qa_bytes *source, qa_error *);
bool qa_equipment_admit(qa_equipment *, qa_actor_id, const qa_equipment_selection *, qa_error *);
bool qa_equipment_admit_sources(qa_equipment *, qa_actor_id, const qa_equipment_selection *,
    const qa_equipment_source_selection *, qa_error *);
bool qa_equipment_configure(qa_equipment *, qa_actor_id, const qa_equipment_selection *,
                            qa_error *);
bool qa_equipment_configure_sources(qa_equipment *, qa_actor_id, const qa_equipment_selection *,
    const qa_equipment_source_selection *, qa_error *);
/* After real source/body spawn: release the tether, reset slot/input state,
 * grant the selected grenade allowance and return its action to idle. */
bool qa_equipment_respawn(qa_equipment *, qa_actor_id, qa_error *);
bool qa_equipment_input(qa_equipment *, qa_actor_id, const qa_equipment_controls *, qa_error *);
bool qa_equipment_select_grapple(qa_equipment *, qa_actor_id, bool selected, qa_error *);
/* Retains the real destination through outgoing source holster. Selection is
 * delivered only after primary resume; no source animation advances here. */
bool qa_equipment_request_primary(qa_equipment *, qa_actor_id, qa_actor_owner,
    qa_item_id, bool *accepted, qa_error *);
bool qa_equipment_weapon_bind(qa_equipment *, qa_actor_id,
    const qa_equipment_weapon_binding *, qa_error *);
bool qa_equipment_weapon_unbind(qa_equipment *, qa_actor_id, qa_actor_owner,
    void *exact_context, qa_error *);
bool qa_equipment_weapon_request(qa_equipment *, qa_actor_id, qa_actor_owner,
    qa_item_id, bool *accepted, qa_error *);
bool qa_equipment_weapon_selected(qa_equipment *, qa_actor_id, qa_actor_owner);
bool qa_equipment_weapon_presented(qa_equipment *, qa_actor_id, qa_actor_owner);
bool qa_equipment_weapon_binding_is(qa_equipment *, qa_actor_id, qa_actor_owner, const void *exact_context);
bool qa_equipment_primary_selected(qa_equipment *, qa_actor_id);
size_t qa_equipment_weapon_presentation_count(qa_equipment *, qa_actor_id);
bool qa_equipment_weapon_presentation_read(qa_equipment *, qa_actor_id, size_t,
    qa_weapon_presentation *, bool *found, qa_error *);
bool qa_equipment_weapon_presentation_current(qa_equipment *, const qa_weapon_presentation *);
bool qa_equipment_reconcile(qa_equipment *,qa_actor_id,qa_error *);
bool qa_equipment_step(qa_equipment *, qa_actor_id, uint64_t now_ns, uint64_t elapsed_ns,
                       qa_q2_hand_lifecycle, qa_error *);
bool qa_equipment_after_movement(qa_equipment *, qa_actor_id, bool damage_pulse, uint64_t now_ns,
                                 uint64_t elapsed_ns, qa_error *);
bool qa_equipment_release_grapple(qa_equipment *, qa_actor_id, qa_error *);
bool qa_equipment_lmctf_command(qa_equipment *, qa_modes *, qa_mode_id, qa_actor_id,
                                bool native_slot, bool pressed, qa_error *);
bool qa_equipment_q3_pull(qa_equipment *, qa_actor_id, qa_vec3 *velocity, bool *apply, qa_error *);
float qa_equipment_gravity_scale(qa_equipment *, qa_actor_id);
bool qa_equipment_publish_q3_items(qa_equipment *, qa_actor_id, qa_error *);
bool qa_equipment_read(qa_equipment *, qa_actor_id, qa_equipment_state *);
typedef struct qa_equipment_weapon_view {
    qa_actor_id actor;
    qa_equipment_source source;
    qa_grapple_mechanic mechanic;
    qa_item_id item;
    const char *label;
    bool active;
} qa_equipment_weapon_view;
/* Observes the already admitted slot allowance even while its primary weapon
 * is visible. No source resolution, admission or execution callback runs. */
bool qa_equipment_weapon_view_read(qa_equipment *, qa_actor_id,
    qa_equipment_weapon_view *, bool *found, qa_error *);
bool qa_equipment_weapon_view_current(qa_equipment *, const qa_equipment_weapon_view *);
bool qa_equipment_restore(qa_equipment *, const qa_equipment_state *, qa_error *);
void qa_equipment_actor_released(qa_equipment *, qa_actor_record);

#endif
