#ifndef QA_NATIVE_HOST_H
#define QA_NATIVE_HOST_H

#include "qa/console.h"
#include "qa/gameplay.h"
#include "qa/inventory.h"
#include "qa/movement.h"
#include "qa/native.h"
#include "qa/physics.h"
#include "qa/qvm.h"
#include "qa/session.h"
#include "qa/targets.h"
#include "qa/vfs.h"
#include "qa/network_q2.h"
#include "qa/network_q2_kex_game.h"

typedef struct qa_native_host qa_native_host;

typedef enum qa_native_host_resource_kind {
    QA_NATIVE_HOST_MODEL,
    QA_NATIVE_HOST_SOUND,
    QA_NATIVE_HOST_IMAGE
} qa_native_host_resource_kind;

typedef enum qa_native_host_print_kind {
    QA_NATIVE_HOST_PRINT_DEBUG,
    QA_NATIVE_HOST_PRINT_BROADCAST,
    QA_NATIVE_HOST_PRINT_CLIENT,
    QA_NATIVE_HOST_PRINT_CENTER
} qa_native_host_print_kind;

typedef struct qa_native_host_print {
    qa_native_host_print_kind kind;
    qa_actor_id client;
    int32_t level;
    const char *text;
} qa_native_host_print;

typedef struct qa_native_host_localized_print {
    int32_t level;
    const char *base;
    const char *const *arguments;
    size_t argument_count;
} qa_native_host_localized_print;

typedef struct qa_native_host_sound {
    qa_actor_id actor;
    qa_actor_id client;
    qa_vec3 origin;
    int32_t index;
    uint32_t flags;
    uint8_t channel;
    float volume, attenuation, time_offset;
    bool positioned, local, reliable;
    const char *name;
    const qa_actor_id *recipients;
    size_t recipient_count;
    bool audience_captured;
} qa_native_host_sound;

typedef enum qa_native_host_message_target {
    QA_NATIVE_HOST_MULTICAST,
    QA_NATIVE_HOST_UNICAST
} qa_native_host_message_target;

typedef struct qa_native_host_message_reference {
    size_t offset;
    qa_actor_id actor;
} qa_native_host_message_reference;

typedef struct qa_native_host_message {
    qa_native_host_message_target target;
    qa_bytes payload;
    qa_vec3 origin;
    qa_actor_id client;
    int32_t destination;
    uint32_t flags;
    bool reliable, positioned;
    const qa_native_host_message_reference *references;
    size_t reference_count;
} qa_native_host_message;

typedef struct qa_native_host_link_metadata {
    int32_t area, secondary_area;
    int32_t headnode;
    int32_t clusters[16];
    int32_t cluster_count; /* -1 means the source cluster list overflowed. */
    uint32_t network_solid;
} qa_native_host_link_metadata;

typedef struct qa_native_host_command_view {
    size_t count;
    const char *const *arguments;
    const char *tail;
    /* Reached CG parser view; console entry snapshots leave this false. */
    bool canonical_configstrings;
} qa_native_host_command_view;

typedef struct qa_native_host_q2_hud_view {
    int32_t x, y, width, height;
    int32_t safe_x, safe_y, safe_width, safe_height;
    int32_t scale;
} qa_native_host_q2_hud_view;

typedef struct qa_native_host_q2_path_request {
    qa_vec3 start, goal;
    uint32_t flags;
    float move_distance, draw_time, min_height, max_height, radius, drop_height, jump_height;
    bool ignore_node_flags;
    int64_t point_capacity;
    void *point_context;
    bool (*point)(void *, qa_vec3, qa_error *);
} qa_native_host_q2_path_request;
typedef struct qa_native_host_q2_path_info {
    int32_t point_count;
    float distance_squared;
    qa_vec3 first, second;
    int32_t link_type, return_code;
} qa_native_host_q2_path_info;

typedef enum qa_native_host_restore_phase {
    QA_NATIVE_HOST_RESTORE_CVARS,
    QA_NATIVE_HOST_RESTORE_PRECACHES,
    QA_NATIVE_HOST_RESTORE_COMPLETE
} qa_native_host_restore_phase;

typedef struct qa_native_host_engine_services {
    void *context;
    qa_strings *strings;
    void (*print)(void *, const qa_native_host_print *);
    bool (*localized_print)(void *, qa_actor_id, const qa_native_host_localized_print *, qa_error *);
    bool (*debug_graph)(void *, float, int32_t, qa_error *);
    bool (*bot_move_to)(void *, qa_actor_id, qa_vec3, float, int32_t *, qa_error *);
    bool (*bot_follow)(void *, qa_actor_id, qa_actor_id, int32_t *, qa_error *);
    bool (*path_to_goal)(void *, const qa_native_host_q2_path_request *,
                         qa_native_host_q2_path_info *, qa_error *);
    bool (*configstring_get)(void *, int32_t, qa_string_id *, qa_error *);
    bool (*configstring_set)(void *, int32_t, const char *, qa_error *);
    bool (*resource_index)(void *, qa_native_host_resource_kind, const char *, int32_t *,
                           qa_error *);
    bool (*command)(void *, qa_native_host_command_view *, qa_error *);
    bool (*message)(void *, const qa_native_host_message *, qa_error *);
    bool (*sound)(void *, const qa_native_host_sound *, qa_error *);
    bool (*link_metadata)(void *, qa_actor_id, qa_native_host_link_metadata *, qa_error *);
    uint32_t (*server_frame)(void *);
    uint64_t (*source_frame)(void *);
    bool (*extension)(void *, qa_native_profile, const char *, qa_native_address *, qa_error *);
    bool (*checkpoint)(void *, qa_buffer *, qa_error *);
    bool (*restore)(void *, qa_bytes, qa_native_host_restore_phase, qa_error *);
    void *frontend_lifetime;
    void (*release_frontend)(void *);
    qa_vfs *content_files;
    qa_cvars *cvars;
    bool (*hud_view)(void *, uint32_t seat, qa_native_host_q2_hud_view *, qa_error *);
    /* Pure application-owner lifetime predicate for detached frontend adoption. */
    void *owner_context;
    bool (*owner_idle)(void *);
    bool (*entity_number)(void *, qa_actor_id, uint32_t *, qa_error *);
    /* Exact borrowed source-import transfer. Only declaration-backed owners
     * install this pair; before commits reached source writes and after reads
     * the canonical effects of the actual import. */
    bool (*source_before)(void *, qa_error *);
    bool (*source_after)(void *, qa_error *);
    /* Real Source precache selection. Transfers its retained file and owned
     * opening; an absent selection leaves all owned outputs empty. */
    bool (*resource_precache)(void *, qa_native_host_resource_kind, const char *,
        const qa_vfs **actual_view, qa_resource **retained, qa_vfs_acquisition *owned_opening,
        bool *found, qa_error *);
    bool (*source_import)(void *,const qa_native_import_call *,qa_native_value *,bool *handled,qa_error *);
} qa_native_host_engine_services;

typedef struct qa_native_host_instance_options {
    const qa_native_declaration *declaration;
    const qa_native_dependency *dependencies;
    size_t dependency_count;
    const qa_native_runtime_config *runtime;
    const struct qa_native_process_options *process;
    uint32_t tick_rate;
    float frame_seconds;
    uint32_t frame_milliseconds;
    bool observe;
} qa_native_host_instance_options;

typedef struct qa_native_host_world_services {
    qa_session *session;
    qa_world *world;
    qa_physics *physics;
    qa_combat *combat;
    qa_inventory *inventory;
    qa_targets *targets;
    qa_actor_owner owner;
    qa_actor_definition definition;
    qa_actor_id world_actor;
    /* Called once after a source slot is allocated and before it is linked.
     * Artifact-specific private fields may bind the shared stores here. */
    void *binding_context;
    bool (*project_actor)(void *, qa_native_host *, uint32_t, qa_native_address,
                          qa_actor_id *, bool *present, qa_error *);
    bool (*address_for_actor)(void *, qa_native_host *, qa_actor_id,
                              qa_native_address *, bool *present, qa_error *);
    bool (*bind_actor)(void *, qa_native_host *, uint32_t, qa_actor_id, qa_error *);
    void (*release_actor)(void *, qa_native_host *, uint32_t, qa_actor_id);
    bool (*reserved_source_slot)(void *, uint32_t, bool *, qa_error *);
} qa_native_host_world_services;

typedef struct qa_native_host_movement_services {
    void *context;
    qa_movement_services kernel;
    /* Supplies ownership, selected-provider policy, authored posture and time.
     * The adapter fills source state and command fields before this callback. */
    bool (*prepare)(void *, qa_native_host *, qa_native_address, qa_movement_input *,
                    uint64_t *prepared, qa_error *);
    /* Optional selected movement execution at the actual SDK Pmove call.
     * The result owns its contacts and must use the physical Source dialect. */
    bool (*execute)(void *, qa_native_host *, qa_native_address,
                    const qa_movement_input *, const qa_movement_services *,
                    qa_movement_result *, qa_error *);
    bool (*commit)(void *, qa_native_host *, qa_native_address,
                   const qa_movement_result *, uint64_t prepared, qa_error *);
} qa_native_host_movement_services;

typedef struct qa_native_host_q2_application_call {
    qa_native_host *host;
    qa_native_instance *instance;
    const qa_native_import_call *import;
    uint32_t seat;
    bool seat_bound;
} qa_native_host_q2_application_call;
bool qa_native_host_q2_call_string(const qa_native_host_q2_application_call *,
    size_t index, const char **text, qa_error *);
bool qa_native_host_string_span(qa_native_host *, qa_native_address, const char **, qa_error *);
/* Immutable source return storage remains valid through this host's lifetime. */
bool qa_native_host_retain_string(qa_native_host *, qa_string_id, qa_native_address *, qa_error *);

/* These callbacks own services whose state is outside gameplay authority:
 * localization, clipboard, debug presentation, bot navigation and cgame UI. */
typedef bool (*qa_native_host_q2_application_fn)(
    void *, const qa_native_host_q2_application_call *, qa_native_value *, qa_error *);

typedef struct qa_native_host_q2_services {
    qa_native_host_engine_services engine;
    qa_native_host_movement_services movement;
    void *application_context;
    qa_native_host_q2_application_fn application;
    size_t maximum_message_bytes;
    size_t maximum_string_bytes;
} qa_native_host_q2_services;

typedef struct qa_native_host_q2_game_options {
    qa_native_host_instance_options instance;
    qa_native_host_world_services world;
    qa_native_host_q2_services services;
    qa_cvars *cvars;
    qa_console *console;
    qa_command_context command_context;
} qa_native_host_q2_game_options;

typedef struct qa_native_host_q2_cgame_options {
    qa_native_host_instance_options instance;
    qa_native_host_engine_services engine;
    qa_native_host_q2_application_fn application;
    void *application_context;
    qa_cvars *cvars;
    qa_console *console;
    qa_command_context command_context;
    size_t maximum_string_bytes;
    uint32_t seat;
    bool seat_bound;
} qa_native_host_q2_cgame_options;

typedef struct qa_native_host_guest_memory {
    void *context;
    uint8_t pointer_bytes;
    bool (*read)(void *, uint64_t, void *, size_t, qa_error *);
    bool (*write)(void *, uint64_t, qa_bytes, qa_error *);
    bool (*read_string)(void *, uint64_t, size_t, qa_buffer *, qa_error *);
} qa_native_host_guest_memory;

typedef struct qa_native_host_q3_call {
    qa_native_host *host;
    qa_native_instance *instance;
    qa_native_profile profile;
    qa_qvm_role role;
    qa_qvm_abi abi;
    int32_t source_service;
    int32_t canonical_service;
    bool engine_service;
    const qa_native_value *arguments;
    size_t argument_count;
    const qa_native_signature *fixed_signature;
    qa_native_host_guest_memory memory;
} qa_native_host_q3_call;

typedef struct qa_native_host_q3_bridge {
    void *context;
    /* Native Q3 variadic calls require a description before arguments can be
     * consumed. The QVM adapter uses the same dispatch callback after its own
     * qa_qvm_classify_syscall call. */
    bool (*describe_native)(void *, qa_qvm_role, qa_qvm_abi, int32_t,
                            const qa_native_value_type **, size_t *, qa_error *);
    bool (*dispatch)(void *, const qa_native_host_q3_call *, qa_native_value *, qa_error *);
    bool (*checkpoint)(void *, qa_buffer *, qa_error *);
    bool (*restore)(void *, qa_bytes, qa_error *);
    void (*entity_changed)(void *, const qa_native_entity_event *);
} qa_native_host_q3_bridge;

typedef struct qa_native_host_q3_options {
    qa_native_host_instance_options instance;
    qa_native_host_world_services world;
    qa_native_host_engine_services engine;
    qa_native_host_q3_bridge bridge;
    qa_cvars *cvars;
    qa_console *console;
    qa_command_context command_context;
    qa_qvm_role role;
    qa_qvm_abi abi;
    size_t maximum_string_bytes;
} qa_native_host_q3_options;

/* Creation outputs start empty. A failed factory with retained source or
 * process resources returns its host in *out; keep borrowed service contexts
 * alive until destroy_owned clears it. */
bool qa_native_host_create_q2_game(qa_native_module *, const qa_native_host_q2_game_options *,
                                   qa_native_host **, qa_error *);
bool qa_native_host_create_q2_cgame(qa_native_module *, const qa_native_host_q2_cgame_options *,
                                    qa_native_host **, qa_error *);
bool qa_native_host_create_q3(qa_native_module *, const qa_native_host_q3_options *,
                              qa_native_host **, qa_error *);
bool qa_native_host_create_q2_game_begin(qa_native_module *, const qa_native_host_q2_game_options *,
                                         qa_native_host **, qa_error *);
bool qa_native_host_create_q2_cgame_begin(qa_native_module *, const qa_native_host_q2_cgame_options *,
                                          qa_native_host **, qa_error *);
bool qa_native_host_create_q3_begin(qa_native_module *, const qa_native_host_q3_options *,
                                    qa_native_host **, qa_error *);
bool qa_native_host_create_advance(qa_native_host *, size_t quantum,
                                   bool *complete, qa_error *);
bool qa_native_host_startup_paused(const qa_native_host *);
bool qa_native_host_startup_cancel(qa_native_host *, qa_error *);
/* Clears only a consumed host. Failed OS unload retains the original host and
 * every borrowed source service until a later cleanup attempt can consume it. */
bool qa_native_host_destroy_owned(qa_native_host **, qa_error *);
/* Readonly initial admission. Actual checked OS unload can still reject. */
bool qa_native_host_destroy_ready(const qa_native_host *);
/* Terminal cleanup must retain every still-live source or borrowed actor. */
bool qa_native_host_terminal_retired(const qa_native_host *);

qa_native_instance *qa_native_host_instance(qa_native_host *);
qa_native_profile qa_native_host_profile(const qa_native_host *);
typedef struct qa_native_host_entity_layout {
    size_t bytes, inuse, linkcount, area, area2, flags;
    size_t mins, maxs, absmin, absmax, size, solid, clipmask, owner;
    size_t cluster_count, clusters, headnode;
} qa_native_host_entity_layout;
const qa_native_host_entity_layout *qa_native_host_source_layout(const qa_native_host *);
/* Load-reserved result storage shared by SDK imports and selected foreign
 * movement. Nested imports retain distinct slots until their call completes. */
qa_movement_result *qa_native_host_movement_acquire(qa_native_host *, size_t *slot,
    qa_error *);
void qa_native_host_movement_release(qa_native_host *, size_t slot);
bool qa_native_host_source_public_bytes(qa_native_host *, uint32_t source_slot,
    size_t *, qa_error *);
bool qa_native_host_q3_memory(qa_native_host *, qa_qvm_role, qa_qvm_abi,
                               qa_native_host_guest_memory *, qa_error *);

/* Dispatch after the shared registry invalidates the released ID. Clears only
 * matching owned/borrowed bindings; repeated notifications are harmless. */
bool qa_native_host_actor_released(qa_native_host *, qa_actor_record, qa_error *);
bool qa_native_host_slot_for_actor(const qa_native_host *, qa_actor_id, uint32_t *);
bool qa_native_host_detach_actor(qa_native_host *, uint32_t source_slot,
                                  qa_actor_id, qa_error *);
bool qa_native_host_world_actor_bind(qa_native_host *, qa_actor_id, qa_error *);
/* Original Q2 entity address admission and retirement reconciliation. These
 * use actual source inuse/slot state and preserve full canonical generations;
 * callers must hold their source owner through synchronous callbacks. */
bool qa_native_host_source_actor(qa_native_host *, qa_native_address, bool observe,
                                  qa_actor_id *, qa_error *);
bool qa_native_host_source_address(qa_native_host *, qa_actor_id,
    qa_native_address *, qa_error *);
bool qa_native_host_source_reconcile(qa_native_host *, qa_error *);
bool qa_native_host_source_active(qa_native_host *, uint32_t source_slot, bool *, qa_error *);
/* Source clocks are published by the actual application source owner. These
 * boundaries read the original table and events without calling RunFrame. */
bool qa_native_host_source_frame_begin(qa_native_host *, qa_error *);
bool qa_native_host_source_frame_end(qa_native_host *, qa_error *);
bool qa_native_host_source_birth(qa_native_host *, qa_native_address, qa_actor_id *, qa_error *);
bool qa_native_host_source_body_bind(qa_native_host *, uint32_t source_slot,
    uint32_t velocity_offset, uint32_t ground_offset,
    void *write_context, bool (*write)(void *, const qa_body_state *, qa_error *),
    qa_error *);
bool qa_native_host_source_body_write(qa_native_host *, uint32_t source_slot,
    uint32_t velocity_offset, uint32_t ground_offset, const qa_body_state *, qa_error *);
typedef struct qa_native_host_source_touch qa_native_host_source_touch;
/* The caller retains even a partially prepared ticket until checked disposal.
 * Arguments borrow its native scratch for one synchronous source call. */
bool qa_native_host_source_touch_prepare(qa_native_host *, bool rerelease,
    const qa_touch_contact *, qa_native_host_source_touch **, qa_error *);
bool qa_native_host_source_touch_arguments(const qa_native_host_source_touch *,
    qa_native_value arguments[4], qa_error *);
bool qa_native_host_source_touch_close(qa_native_host_source_touch **, qa_error *);
/* Encode the actual GAME ABI trace, retaining physical entity and surface
 * pointers owned by this host. The output span is borrowed; ownership stays
 * with the caller. No Source call or movement is executed. */
bool qa_native_host_q2_trace_encode(qa_native_host *,const qa_trace_result *,qa_buffer,qa_error *);

/* Q2 refreshes its real cvars and executes SDK Init. The GAME constructor then
 * activates its source producers before qa_native_host_source_reconcile admits
 * canonical actors. Cold reconstruction imports its saved HOST bindings before
 * resuming reconciliation; CGAME has no GAME actor table to reconcile. */
bool qa_native_host_initialize(qa_native_host *, int32_t level_time, int32_t random_seed,
                               bool restart, qa_error *);
bool qa_native_host_initialize_begin(qa_native_host *, int32_t level_time,
                                      int32_t random_seed, bool restart, qa_error *);
bool qa_native_host_initialize_advance(qa_native_host *, size_t quantum,
                                        bool *complete, qa_error *);
bool qa_native_host_shutdown(qa_native_host *, bool restart, qa_error *);
bool qa_native_host_spawn_entities(qa_native_host *, const char *map, const char *entities,
                                   const char *spawn_point, qa_error *);
bool qa_native_host_run_frame(qa_native_host *, bool main_loop, qa_error *);
bool qa_native_host_prep_frame(qa_native_host *, qa_error *);
/* Read the original public player prefix into 296 caller-owned writable bytes.
 * The output borrows 184 classic or 296 rerelease bytes from that storage. */
bool qa_native_host_q2_player_state(qa_native_host *, uint32_t source_slot,
    uint8_t storage[296], qa_bytes *out, qa_error *);
/* Source data is the original 1024-byte layout plus 256 signed short counts;
 * player_state is the public 296-byte KEX state. Copies live only during DrawHUD. */
typedef struct qa_native_host_q2_hud_state {
    int16_t layout_flags, hit_marker_damage;
} qa_native_host_q2_hud_state;
bool qa_native_host_q2_draw_hud(qa_native_host *, uint32_t seat,
    const qa_native_host_q2_hud_view *, int32_t player_number,
    qa_bytes server_data, qa_bytes player_state, bool shared_inventory,
    qa_native_host_q2_hud_state *observe, qa_error *);
typedef enum qa_native_q2_hud_text_kind {
    QA_NATIVE_Q2_HUD_CENTER,
    QA_NATIVE_Q2_HUD_NOTIFY,
    QA_NATIVE_Q2_HUD_CLEAR_NOTIFY,
    QA_NATIVE_Q2_HUD_CLEAR_CENTER
} qa_native_q2_hud_text_kind;
/* The flag is instant for centerprint and is_chat for notify. */
bool qa_native_host_q2_hud_text(qa_native_host *, uint32_t seat,
    qa_native_q2_hud_text_kind, const char *text, bool flag, qa_error *);
bool qa_native_host_q2_parse_config(qa_native_host *, int32_t index,
    const char *text, qa_error *);
bool qa_native_host_q2_monster_flash_offset(qa_native_host *, uint16_t flash,
    qa_vec3 *, qa_error *);
bool qa_native_host_server_command(qa_native_host *, qa_error *);

typedef struct qa_native_host_client_request {
    uint32_t slot;
    const char *userinfo;
    const char *social_id;
    bool bot;
} qa_native_host_client_request;

bool qa_native_host_client_choose_slot(qa_native_host *, const char *userinfo,
                                       const char *social_id, bool bot,
                                       char *client_info, size_t client_info_capacity,
                                       bool spectator, uint32_t *slot, qa_error *);
bool qa_native_host_client_connect(qa_native_host *, const qa_native_host_client_request *,
                                   bool *accepted, qa_error *);
/* Declared native client calls retain the real source slot independently of
 * the conventional ClientConnect entry. This performs no original callback. */
bool qa_native_host_client_retained_set(qa_native_host *, uint32_t slot, bool, qa_error *);
bool qa_native_host_client_connect_userinfo(qa_native_host *, const qa_native_host_client_request *,
    bool *accepted, qa_buffer *returned_userinfo, qa_error *);
bool qa_native_host_client_begin(qa_native_host *, uint32_t slot, qa_error *);
bool qa_native_host_client_userinfo(qa_native_host *, uint32_t slot, const char *userinfo,
                                    qa_error *);
bool qa_native_host_client_userinfo_result(qa_native_host *, uint32_t slot,
    const char *userinfo, qa_buffer *returned_userinfo, qa_error *);
/* Executes the original callback while retaining the actual client slot.
 * After reading and detaching final Source leases, the caller detaches that
 * actor and reconciles the completed source changes. */
bool qa_native_host_client_disconnect(qa_native_host *, uint32_t slot, qa_error *);
bool qa_native_host_client_command(qa_native_host *, uint32_t slot, qa_error *);
/* Synchronous continuation of an actual scanner region in this same instance.
 * The event is borrowed only until its region callback returns. */
bool qa_native_host_client_command_region(qa_native_host *, uint32_t slot,
    const qa_native_region_event *, qa_error *);
bool qa_native_host_client_think(qa_native_host *, uint32_t slot, qa_bytes source_usercmd,
                                 qa_error *);

/* Native vmMain uses the same command words as QVM. arguments excludes the
 * command word and may contain at most twelve words. */
bool qa_native_host_q3_vm_call(qa_native_host *, int32_t command,
                               const int32_t *arguments, size_t argument_count,
                               int32_t *result, qa_error *);
bool qa_native_host_q3_cgame_initialize(qa_native_host *, int32_t server_message_number,
                                       int32_t server_command_sequence, int32_t client_number,
                                       qa_error *);
bool qa_native_host_q3_ui_initialize(qa_native_host *, bool connecting, qa_error *);
/* Game-role convenience calls preserve the Q3/QL source entry contracts.
 * A nonempty denial buffer is owned by the caller and must be freed. */
bool qa_native_host_q3_client_connect(qa_native_host *, uint32_t slot, bool first_time,
                                      bool bot, qa_buffer *denial, qa_error *);
bool qa_native_host_q3_client_begin(qa_native_host *, uint32_t slot, qa_error *);
bool qa_native_host_q3_client_userinfo(qa_native_host *, uint32_t slot, qa_error *);
bool qa_native_host_q3_client_disconnect(qa_native_host *, uint32_t slot, qa_error *);
bool qa_native_host_q3_client_command(qa_native_host *, uint32_t slot, qa_error *);
bool qa_native_host_q3_client_think(qa_native_host *, uint32_t slot, qa_error *);
bool qa_native_host_q3_console_command(qa_native_host *, bool *handled, qa_error *);
bool qa_native_host_q3_bot_frame(qa_native_host *, int32_t level_time, qa_error *);

/* Explicit host continuation used by qa_native_options checkpoint callbacks. */
bool qa_native_host_checkpoint(qa_native_host *, qa_buffer *, qa_error *);
/* Q2 imports cvars before Init and saved precache indices before map
 * reconstruction. COMPLETE imports source slots and engine continuation after
 * ReadLevel. A LEVEL revisit keeps current GAME cvars and saved LEVEL precaches. */
bool qa_native_host_restore(qa_native_host *, qa_bytes, qa_native_host_restore_phase,
    bool restore_cvars, qa_error *);
typedef struct qa_native_host_reconstruction qa_native_host_reconstruction;
/* Borrow a complete, distinct baseline host's real services while executing
 * original source map reconstruction. Both owners must outlive the phase.
 * End clears temporary source-slot identities and restores the original
 * service graph. A rejected end retains the phase and its borrowed owner. */
bool qa_native_host_reconstruction_begin(qa_native_host *, qa_native_host *,
    qa_native_host_reconstruction **, qa_error *);
bool qa_native_host_reconstruction_end(qa_native_host_reconstruction *, qa_error *);
/* After source Shutdown, destroy consumes the target once its normal destroy
 * admission passes, including backend destruction faults. Rejection retains
 * the phase. The baseline host itself is never destroyed by this operation. */
bool qa_native_host_reconstruction_destroy(qa_native_host_reconstruction *, bool *consumed, qa_error *);

/* Application-side content adapter. Portable helpers link qa_native_module_load
 * and do not need the VFS implementation. */
bool qa_native_host_module_open(qa_vfs *, const char *, qa_native_profile,
                                qa_native_module **, qa_error *);

#endif
