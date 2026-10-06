#ifndef QA_NETWORK_UNIFIED_CONTROL_H
#define QA_NETWORK_UNIFIED_CONTROL_H

#include "qa/network_unified_frame.h"
#include "qa/unified_frame_components.h"
#include "qa/network_unified_components.h"

typedef enum qa_unified_control_kind {
    QA_UNIFIED_CONTROL_READY, QA_UNIFIED_CONTROL_ADMITTED,
    QA_UNIFIED_CONTROL_RESOURCES, QA_UNIFIED_CONTROL_USERINFO,
    QA_UNIFIED_CONTROL_COMMAND, QA_UNIFIED_CONTROL_SOURCE_COMMAND,
    QA_UNIFIED_CONTROL_DISCONNECT, QA_UNIFIED_CONTROL_OFFER,
    QA_UNIFIED_CONTROL_COMPONENTS, QA_UNIFIED_CONTROL_COMPONENT_COMMAND,
    QA_UNIFIED_CONTROL_EVENTS, QA_UNIFIED_CONTROL_METADATA,
    QA_UNIFIED_CONTROL_INVALID
} qa_unified_control_kind;
/* Existing Source resource identities are declared once. Events reference
 * this admitted dictionary rather than sending resource metadata again. */
typedef struct qa_unified_resource_declaration {
    char *identity;
    qa_unified_resource_state resource;
} qa_unified_resource_declaration;
typedef struct qa_unified_ready_control {
    uint64_t composition;
    char *userinfo;
} qa_unified_ready_control;
typedef struct qa_unified_admitted_control {
    qa_net_client_id client;
    qa_actor_id actor;
    uint32_t source_entity;
} qa_unified_admitted_control;
typedef struct qa_unified_resources_control {
    qa_unified_resource_declaration *values;
    size_t count;
} qa_unified_resources_control;
typedef struct qa_unified_command_control {
    char *name;
    qa_unified_control_arguments arguments;
} qa_unified_command_control;
typedef struct qa_unified_source_command_control {
    char *instance;
    uint64_t publication, map_revision;
    qa_unified_control_arguments arguments;
} qa_unified_source_command_control;
typedef struct qa_unified_component_command_control {
    qa_unified_component_owner owner;
    qa_unified_control_arguments arguments;
} qa_unified_component_command_control;
typedef struct qa_unified_control {
    qa_unified_control_kind kind;
    uint32_t epoch;
    union {
        qa_unified_ready_control ready;
        qa_unified_admitted_control admitted;
        qa_unified_resources_control resources;
        char *userinfo;
        qa_unified_command_control command;
        qa_unified_source_command_control source_command;
        qa_unified_component_command_control component_command;
        qa_unified_components_control components;
        char *disconnect;
    } value;
} qa_unified_control;

/* Copies the real typed setup/command record into immutable document custody.
 * Decode uses the same fixed field layout at the external packet boundary. */
bool qa_unified_document_create_control(const qa_unified_control *, qa_unified_document **, qa_error *);
const qa_unified_control *qa_unified_document_control(const qa_unified_document *);
qa_unified_control_kind qa_unified_document_control_type(const qa_unified_document *);
bool qa_unified_document_epoch(const qa_unified_document *, uint32_t *, qa_error *);

#endif
