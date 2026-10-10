#ifndef QA_GAME_Q3_WIRE_H
#define QA_GAME_Q3_WIRE_H

#include "qa/game_q3.h"

typedef struct qa_q3_wire_visibility {
    bool linked, present;
    uint32_t server_flags;
    int32_t single_client, area, area2, last_cluster, clusters[16];
    size_t cluster_count;
} qa_q3_wire_visibility;

/* Native network observation uses the complete unique 128-leaf membership of
 * the current published body. The source engine link owner above retains its
 * original 16-cluster overflow policy independently. */
typedef struct qa_q3_wire_native_visibility {
    bool linked, present;
    uint32_t server_flags;
    int32_t single_client, area, area2, last_cluster, clusters[128];
    size_t cluster_count;
} qa_q3_wire_native_visibility;
bool qa_q3_wire_native_visibility_read(const qa_q3_game *, uint32_t physical_slot,
                                        qa_q3_wire_native_visibility *, qa_error *);

typedef struct qa_q3_wire_mode {
    int32_t score;
} qa_q3_wire_mode;

typedef struct qa_q3_wire_policy {
    int32_t pm_type, bob_cycle, pm_flags, pm_time, gravity, speed, movement_dir;
} qa_q3_wire_policy;

typedef enum qa_q3_wire_policy_fields {
    QA_Q3_WIRE_PM_TYPE = 1,
    QA_Q3_WIRE_PM_BOB_CYCLE = 2,
    QA_Q3_WIRE_PM_FLAGS = 4,
    QA_Q3_WIRE_PM_TIME = 8,
    QA_Q3_WIRE_PM_GRAVITY = 16,
    QA_Q3_WIRE_PM_SPEED = 32,
    QA_Q3_WIRE_PM_DIRECTION = 64,
    QA_Q3_WIRE_PM_ALL = 127
} qa_q3_wire_policy_fields;

typedef struct qa_q3_wire_services {
    void *context;
    bool (*movement_flags)(void *, qa_actor_id, uint32_t clear, uint32_t set,
                           qa_error *);
    bool (*movement_policy)(void *, qa_actor_id, uint8_t fields,
                            const qa_q3_wire_policy *, qa_error *);
    bool (*mode)(void *, qa_actor_id, qa_q3_wire_mode *, qa_error *);
} qa_q3_wire_services;

/* Bind the real selected movement and match owners. These capabilities are
 * borrowed and are rebound after candidate restore, never serialized. */
bool qa_q3_wire_bind_services(qa_q3_game *, const qa_q3_wire_services *, qa_error *);
bool qa_q3_wire_player_read(const qa_q3_game *, uint32_t source_client,
                            qa_q3_player *, qa_error *);
bool qa_q3_wire_entity_read(const qa_q3_game *, uint32_t physical_slot,
                            qa_q3_entity *, qa_q3_wire_visibility *, qa_error *);
typedef struct qa_q3_source_model {
    int32_t type, model;
} qa_q3_source_model;
/* Pure initialized physical Source fields before model matching. */
bool qa_q3_source_model_read(const qa_q3_game *, uint32_t physical_slot,
                              qa_q3_source_model *, qa_error *);
/* Actual obelisk trigger activator's initialized Source frame, without a
 * visibility or body observation. An absent relationship leaves frame alone. */
bool qa_q3_source_activator_frame_read(const qa_q3_game *, uint32_t physical_slot,
                                        int32_t *frame, bool *present, qa_error *);
/* Literal current r.contents from its actual collision owner, only at the
 * requested contents stage of the Source scan. */
bool qa_q3_source_contents_read(const qa_q3_game *, uint32_t physical_slot,
                                 int32_t *, qa_error *);
/* BotModelMinsMaxs observes only requested r.currentOrigin + r.mins/maxs. */
bool qa_q3_source_model_bounds_read(const qa_q3_game *, uint32_t physical_slot,
                                     qa_vec3 *mins, qa_vec3 *maxs, qa_error *);
/* Actual source S trajectories/ground only; never changes r.currentOrigin. */
bool qa_q3_wire_entity_motion_write(qa_q3_game *, qa_actor_id, const qa_trajectory *,
                                     const qa_trajectory *, int32_t ground_number, qa_error *);
typedef struct qa_q3_wire_body {
    qa_actor_id actor;
    qa_body_state current;
    qa_body_link_state last_link;
    qa_actor_collision collision;
    bool colliding, proximity_trigger;
} qa_q3_wire_body;
bool qa_q3_wire_body_read(const qa_q3_game *, uint32_t physical_slot,
                          qa_q3_wire_body *, qa_error *);
typedef struct qa_q3_wire_client_view {
    qa_vec3 origin;
    bool present, bot;
} qa_q3_wire_client_view;
/* The fixed record's absent-actor body is the source constructor ZERO_BODY.
 * A present actor always reads its real body and propagates read failures. */
bool qa_q3_wire_client_read(const qa_q3_game *, uint32_t source_client,
                            qa_q3_wire_client_view *, qa_error *);
typedef struct qa_q3_wire_client_body {
    qa_body_state current;
    qa_shape_kind model_shape;
} qa_q3_wire_client_body;
/* Fixed source clients retain their PS and r.model independently of an actor.
 * Absent records expose their actual ZERO_BODY, with their real source model. */
bool qa_q3_wire_client_source_body_read(const qa_q3_game *, uint32_t source_client,
                                        qa_q3_wire_client_body *, qa_error *);
bool qa_q3_wire_client_source_pm_read(const qa_q3_game *, uint32_t source_client,
                                      int32_t *, qa_error *);
bool qa_q3_wire_client_source_score_read(const qa_q3_game *, uint32_t source_client,
                                         int32_t *, qa_error *);
typedef struct qa_q3_source_client_motion {
    qa_vec3 origin;
    int32_t delta_yaw_word;
} qa_q3_source_client_motion;
/* Resolves the physical entity's genuine client pointer, including a victory
 * model borrowing an inactive original fixed client. Writes do not set inuse. */
bool qa_q3_wire_borrowed_client_motion_read(const qa_q3_game *, qa_actor_id,
                                            qa_q3_source_client_motion *, qa_error *);
bool qa_q3_wire_borrowed_client_motion_write(qa_q3_game *, qa_actor_id,
                                             const qa_q3_source_client_motion *, qa_error *);
bool qa_q3_wire_entity_event_time(const qa_q3_game *, uint32_t physical_slot,
                                   int32_t *, qa_error *);
/* Genuine GAME link: authored solid/visibility, exact source bounds, and the
 * optional ClientThink snapped origin without changing the shared body. */
bool qa_q3_wire_link(qa_q3_game *, qa_actor_id, const qa_vec3 *origin_override, qa_error *);
/* Observe the genuine shared World publication for an already bound Source row.
 * Unbound actors have no Q3 projection; this never relinks the physical body. */
bool qa_q3_wire_linked(qa_q3_game *, const qa_linked_body *, qa_error *);

/* These fields belong to native GAME's PM policy when another movement family
 * is selected. Native Q3 movement keeps its existing control owner. */
bool qa_q3_wire_player_policy(qa_q3_game *, qa_actor_id,
                              const qa_q3_wire_policy *, qa_error *);
/* Read the actual selected Q3 PM owner or this source's authored foreign
 * policy. The observation never retains a movement copy. */
bool qa_q3_wire_player_policy_read(const qa_q3_game *, qa_actor_id,
                                    qa_q3_wire_policy *, qa_error *);
/* Actual source assignments replace only the named PM fields. */
bool qa_q3_wire_player_policy_update(qa_q3_game *, qa_actor_id, uint8_t fields,
                                      const qa_q3_wire_policy *, qa_error *);
/* Native ClientThink applies PM_UpdateViewAngles before projecting its raw
 * command into another movement family. Selected Q3 uses its real kernel. */
bool qa_q3_wire_player_view_command(qa_q3_game *, qa_actor_id,
                                     const qa_q3_usercmd *, qa_vec3 *, qa_error *);
/* Source StopFollowing clears only PMF_FOLLOW. A selected Q3 movement writer
 * changes its real owner; inactive fixed rows retain their own native policy. */
bool qa_q3_wire_client_stop_following(qa_q3_game *, uint32_t source_slot, qa_error *);
/* ClientDisconnect transfers the retiring movement owner's native PM scalars
 * into the fixed source client before that canonical control is destroyed. */
bool qa_q3_wire_client_detach(qa_q3_game *, uint32_t source_slot, qa_error *);
/* CheckIntermissionExit writes each CONNECTED client's source PS field. */
bool qa_q3_wire_client_ready(qa_q3_game *, uint32_t source_slot,
                             int32_t ready_mask, qa_error *);
/* Only PS ammo slots without a shared inventory item have source backing. */
bool qa_q3_wire_player_special_ammo(qa_q3_game *, qa_actor_id, uint32_t ammo_slot,
                                     int32_t value, qa_error *);
bool qa_q3_wire_player_loop_sound(qa_q3_game *, qa_actor_id,
                                   int32_t sound_index, qa_error *);

/* BG_PlayerStateToEntityState is an actual source mutation. Observation never
 * advances entityEventSequence. Call at the source ClientThink/EndFrame sites. */
bool qa_q3_wire_player_publish(qa_q3_game *, qa_actor_id, bool snap,
                               bool extrapolate, int32_t time_ms, qa_error *);
typedef struct qa_q3_wire_player_publication {
    qa_vec3 position;
    int32_t weapon, client_number;
} qa_q3_wire_player_publication;
/* ClientThink and FireWeapon read the completed BG publication before
 * trap_LinkEntity produces spatial visibility. */
bool qa_q3_wire_player_publication_read(const qa_q3_game *, qa_actor_id,
                                         qa_q3_wire_player_publication *, qa_error *);
/* SendPendingPredictableEvents authors a real temporary row and consumes the
 * next native ring entry after ordinary BG conversion. */
bool qa_q3_wire_player_pending(qa_q3_game *, qa_actor_id, qa_error *);

#endif
