#ifndef QA_FRONTEND_REMOTE_UNIFIED_COMPONENTS_PRIVATE_H
#define QA_FRONTEND_REMOTE_UNIFIED_COMPONENTS_PRIVATE_H
#include "remote_unified_components.h"
#include "internal.h"
#include "../application/guest_q3_component_scene_factory.h"
#include "qa/q3_abi.h"
#include "remote_unified_events.h"
#include "component_scene.h"
#include "qa/unified_frame_components.h"

typedef struct remote_component_state {
    qa_unified_document *identity,*presentation_owner;
    char *provider;
    qa_unified_mod_identity module;
    uint64_t owner_generation,generation,game_state_revision;
    int32_t command_sequence;
    qa_qvm_abi abi;
    bool player_events;
    const qa_recipe_provider *provider_row;
    const qa_catalog_mod *mod;
    qa_q3_gamestate game_state;
    application_q3_scene_command commands[64];
    qa_command_tokens arguments[64];
    size_t command_count;
} remote_component_state;
typedef struct remote_component_frame {
    qa_unified_document *packet;
    application_q3_scene_context context;
    qa_actor_id viewer;
    qa_q3_snapshot snapshot;
    application_q3_scene_actor *actors;
    remote_component_state source;
    bool has_scene;
} remote_component_frame;
typedef struct remote_component_event {
    application_q3_scene_player_event value;
    uint64_t sequence;
    struct remote_component_event *next;
} remote_component_event;
typedef struct remote_component_polygon_admission {
    uint32_t ordinal;
    bool reached,admitted,emitted;
} remote_component_polygon_admission;
typedef struct remote_component_light_admission {
    uint32_t ordinal;
    bool reached,admitted;
} remote_component_light_admission;
typedef struct remote_component_packet_admission {
    int32_t source_time;
    uint32_t first_entity;
    size_t entity_scanned,entity_count,entity_emitted;
    bool entity_started;
    remote_component_polygon_admission *polygons;
    size_t polygon_count;
    remote_component_light_admission *lights;
    size_t light_count;
} remote_component_packet_admission;
typedef struct remote_component {
    struct remote_component *retired_next;
    struct frontend_unified_components *parent;
    remote_component_state state;
    remote_component_frame *frame,*baseline;
    qa_resource *artifact,*gameplay;
    qa_vfs_acquisition acquisition,gameplay_acquisition;
    qa_qvm_image *image,*gameplay_image;
    application_q3_scene_profile *profile;
    application_q3_scene *scene;
    application_q3_component_scene_frontend frontend;
    qa_q3_presentation_assets *assets;
    qa_cvars *cvars;
    qa_console *console;
    qa_q3_host_options host;
    application_q3_scene_context entered;
    int32_t renderer_time;
    double scene_time_offset;
    int32_t previous_frame_time;
    bool scene_time_present,previous_frame_present;
    qa_actor_owner owner,services;
    uint64_t draw_sequence;
    uint64_t picture_sequence;
    size_t pictures_submitted;
    bool pictures_present;
    remote_component_event *events;
    uint64_t event_sequence;
    bool event_present;
    qa_q3_source_scene_bank *submission_bank;
    uint64_t submission_cycle;
    remote_component_packet_admission *admissions;
    size_t admission_count;
    bool admissions_ready;
    uint64_t frontend_identity;
    qa_buffer saved_scene,saved_cvars,saved_console;
    bool restore_pending,restore_imported,restore_frontend,restore_consoles;
    bool acquired,host_entered,initialized,advanced,submitted,retired;
} remote_component;
struct frontend_unified_components {
    qa_frontend *frontend;
    frontend_remote_unified *replica;
    frontend_unified_media *media;
    qa_executable_recipe *recipe;
    frontend_unified_events *events;
    remote_component **rows;
    size_t count;
    remote_component *retired;
    uint64_t revision;
    qa_scene_light *lights;
    size_t light_count;
    struct frontend_unified_component_frame *prepared;
    bool busy,closing,restoring,failed;
};
struct frontend_unified_component_frame {
    frontend_unified_components *owner;
    const qa_unified_document *input;
    qa_unified_document *owned_input;
    remote_component_frame **rows;
    size_t count;
};
bool q3remote_component_fail(qa_error *,qa_status,const char *);
bool q3remote_component_player_finite(const qa_q3_player *);
void q3remote_component_state_free(remote_component_state *);
void q3remote_component_frame_free(remote_component_frame *);
bool q3remote_component_state_read(frontend_unified_components *,const qa_unified_document *,qa_json_id,
    const remote_component *,remote_component_state *,qa_error *);
bool q3remote_component_frame_read(frontend_unified_components *,remote_component *,const qa_unified_document *,const qa_unified_component_source *,
    remote_component_frame **,qa_error *);
bool q3remote_component_close(remote_component **,qa_error *);
bool q3remote_component_retire(remote_component *,qa_error *);
bool q3remote_component_open(remote_component *,qa_error *);
bool q3remote_component_state_qualify(frontend_unified_components *,remote_component_state *,qa_error *);
bool q3remote_component_packet_read(remote_component *,size_t,frontend_component_scene_packet *,qa_error *);
void q3remote_component_admissions_clear(remote_component *);
size_t q3remote_component_physical_count(const frontend_unified_components *);
remote_component *q3remote_component_physical_at(const frontend_unified_components *,size_t);
#endif
