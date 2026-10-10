#ifndef QA_NETWORK_UNIFIED_COMPONENTS_H
#define QA_NETWORK_UNIFIED_COMPONENTS_H

#include "qa/unified_frame_components.h"
#include "qa/unified_frame_events.h"
#include "qa/launch.h"

/* The admitted declaration, selected executable and Source namespace form
 * one identity. QVM and native modules retain their genuine revision rules. */
typedef struct qa_unified_component_identity {
    qa_program_kind runtime;
    char *product, *id, *provider, *content;
    qa_unified_mod_identity module;
} qa_unified_component_identity;
typedef struct qa_unified_control_arguments {
    char **values;
    size_t count;
} qa_unified_control_arguments;
typedef struct qa_module_server_command {
    int32_t sequence;
    qa_unified_control_arguments arguments;
} qa_module_server_command;
typedef struct qa_unified_component_q3 {
    qa_source_owner owner;
    qa_unified_component_identity identity;
    uint64_t generation;
    qa_qvm_abi abi;
    bool scene;
    int64_t game_state_revision;
    qa_q3_gamestate *game_state;
    int32_t command_base;
    qa_module_server_command *commands;
    size_t command_count;
} qa_unified_component_q3;
typedef enum qa_unified_component_hud {
    QA_UNIFIED_COMPONENT_HUD_NONE, QA_UNIFIED_COMPONENT_HUD_OVERLAY,
    QA_UNIFIED_COMPONENT_HUD_REPLACE
} qa_unified_component_hud;
typedef struct qa_unified_component_configstring {
    uint32_t index;
    char *value;
} qa_unified_component_configstring;
typedef struct qa_unified_component_q2 {
    qa_source_owner owner;
    qa_unified_component_identity identity;
    uint64_t generation;
    qa_unified_component_hud hud;
    qa_net_protocol_id protocol;
    bool replace_configstrings;
    qa_unified_component_configstring *configstrings;
    size_t configstring_count;
    char *layout;
    int16_t inventory[256];
    int32_t player_number;
} qa_unified_component_q2;
typedef struct qa_unified_components_control {
    uint64_t revision;
    qa_unified_component_q3 *sources;
    size_t source_count;
    qa_unified_component_q2 *native;
    size_t native_count;
} qa_unified_components_control;

/* Native holder operations share the same record table as the wire codec. */
bool qa_unified_component_identity_clone(const qa_unified_component_identity *, qa_unified_component_identity *, qa_error *);
bool qa_unified_component_identity_equal(const qa_unified_component_identity *, const qa_unified_component_identity *);
void qa_unified_component_identity_dispose(qa_unified_component_identity *);
bool qa_unified_component_identity_write(const qa_unified_component_identity *, qa_buffer *, qa_error *);
bool qa_unified_component_identity_read(qa_bytes, qa_unified_component_identity *, qa_error *);

#endif
