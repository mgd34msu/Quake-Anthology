#ifndef QA_APPLICATION_QC_PRESENTATION_H
#define QA_APPLICATION_QC_PRESENTATION_H
#include "qa/application.h"
#include "qa/qc.h"
#include "qa/network_q1_nq.h"
#include "qa/inventory.h"

typedef struct qa_application_qc_animation {
    qa_actor_id actor;
    qa_actor_owner provider;
    const qa_launch_instance *descriptor;
    const qa_qc_program *program;
    const qa_qc_instance *instance;
    qa_launch_role role;
    double frame, next_frame_seconds, attack_finished_seconds, source_weapon;
} qa_application_qc_animation;
/* Required continuation fields come from the actual selected program. Custom
 * profiles must declare each field; absent declarations are unsupported. */
bool qa_application_qc_animation_read(qa_application *,qa_actor_id,qa_launch_role,
    qa_application_qc_animation *,qa_error *);
bool qa_application_qc_animation_current(qa_application *,const qa_application_qc_animation *);

typedef struct qa_application_qc_message_source {
    qa_actor_owner provider;
    const qa_launch_instance *descriptor;
    const qa_qc_program *program;
    const qa_qc_instance *instance;
    qa_net_protocol_id protocol;
    qa_nq_options options;
    uint32_t client_slots;
    uint64_t map_revision;
} qa_application_qc_message_source;
/* found=false identifies a different real source family. A reached QC owner
 * must be complete; this reads no source callback or body projection. */
bool qa_application_qc_message_source_read(qa_application *,qa_actor_owner,
    qa_application_qc_message_source *,bool *found,qa_error *);
bool qa_application_qc_message_source_current(qa_application *,const qa_application_qc_message_source *);
bool qa_application_qc_message_client(qa_application *,const qa_application_qc_message_source *,
    qa_actor_id,uint32_t *slot,qa_error *);
/* Observe one physical client slot. Disconnected slots return found=false;
 * connected slots must retain their actual full-generation entity binding. */
bool qa_application_qc_message_client_at(qa_application *,const qa_application_qc_message_source *,
    uint32_t slot,qa_actor_id *,bool *found,qa_error *);
bool qa_application_qc_message_entity(qa_application *,const qa_application_qc_message_source *,
    uint32_t slot,qa_actor_id *,qa_error *);
bool qa_application_qc_message_signon_count(qa_application *,const qa_application_qc_message_source *,
    size_t *,qa_error *);
bool qa_application_qc_message_signon_at(qa_application *,const qa_application_qc_message_source *,
    size_t,qa_application_protocol_event *,qa_error *);
size_t qa_application_qc_message_source_count(const qa_application *);
bool qa_application_qc_message_source_at(qa_application *,size_t,
    qa_application_qc_message_source *,bool *,qa_error *);
/* Apply one reached SETANGLE to the recipient's existing selected control.
 * This never writes source edicts or admits a new player. */
bool qa_application_qc_message_angles(qa_application *,const qa_application_qc_message_source *,
    qa_actor_id,qa_vec3,qa_error *);
bool qa_application_qc_message_view_offset(qa_application *,const qa_application_qc_message_source *,
    qa_actor_id,qa_vec3 *,qa_error *);
typedef struct qa_application_qc_weapon_ui_binding {
    qa_item_id item;
    const char *label;
    uint32_t bit;
    int32_t impulse;
} qa_application_qc_weapon_ui_binding;
typedef struct qa_application_qc_power_timer {
    const char *item, *label;
    double expires_seconds;
} qa_application_qc_power_timer;
typedef struct qa_application_qc_player_ui {
    qa_application_qc_message_source source;
    qa_actor_id recipient;
    uint32_t source_slot, items;
    double weapon, current_ammo, now_seconds;
    size_t binding_count, timer_count;
    qa_application_qc_power_timer timers[4];
    bool selected_arsenal;
} qa_application_qc_player_ui;
/* Pure reads of the returned, already allocated Source client. UI bindings
 * come from its retained declaration or the original program's SDK table. */
bool qa_application_qc_message_player_ui_read(qa_application *,const qa_application_qc_message_source *,
    qa_actor_id,qa_application_qc_player_ui *,qa_error *);
/* A separate selected ARSENAL observation may run during another Source's
 * genuine application advance. Its own QC instance/projection must be returned. */
bool qa_application_qc_selected_player_ui_read(qa_application *,qa_actor_id,qa_launch_role,
    qa_application_qc_player_ui *,qa_error *);
bool qa_application_qc_message_player_ui_current(qa_application *,const qa_application_qc_player_ui *);
bool qa_application_qc_message_player_ui_binding(qa_application *,const qa_application_qc_player_ui *,
    size_t,qa_application_qc_weapon_ui_binding *,qa_error *);
/* Full weapon catalog metadata belongs to this actual selected/source owner.
 * Custom profiles require its admitted item definition; SDK programs use their
 * genuine weapon profile. No foreign inventory definition supplies a fallback. */
bool qa_application_qc_message_player_ui_definition(qa_application *,const qa_application_qc_player_ui *,
    size_t,qa_item_definition *,qa_error *);
/* Writes the actual selected QC client's declared impulse. An unavailable or
 * unowned weapon returns accepted=false without staging a Source command. */
bool qa_application_qc_weapon_request(qa_application *,qa_actor_id,qa_item_id,bool *accepted,qa_error *);
bool qa_application_qc_weapon_settled(qa_application *,qa_actor_id,bool *,qa_error *);
#endif
