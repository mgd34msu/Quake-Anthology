#ifndef QA_Q3_HOST_H
#define QA_Q3_HOST_H

#include "qa/native_host.h"
#include "qa/q3_abi.h"
#include "qa/network_q3.h"

typedef struct qa_q3_host qa_q3_host;
typedef struct qa_scene_resources qa_scene_resources;
typedef struct qa_scene_world qa_scene_world;
typedef struct qa_scene_frame qa_scene_frame;
typedef struct qa_audio_bank qa_audio_bank;
typedef struct qa_audio_mixer qa_audio_mixer;
typedef struct qa_input_seat qa_input_seat;
typedef struct qa_bot_runtime qa_bot_runtime;
typedef struct qa_text_field qa_text_field;
typedef struct qa_script_defines qa_script_defines;
typedef struct qa_q3_key qa_q3_key;
typedef struct qa_q3_presentation qa_q3_presentation;
typedef struct qa_font_library qa_font_library;
typedef struct qa_scene_image qa_scene_image;
typedef struct qa_q3_ref_entity qa_q3_ref_entity;
typedef struct qa_q3_refdef qa_q3_refdef;

typedef struct qa_q3_host_calendar {
    int32_t second, minute, hour, day, month, year, weekday, year_day, is_dst;
} qa_q3_host_calendar;

typedef struct qa_q3_host_common_services {
    void *context;
    void (*print)(void *, const char *);
    uint32_t (*milliseconds)(void *);
    int32_t (*calendar)(void *, qa_q3_host_calendar *);
    bool (*arguments)(void *, qa_native_host_command_view *, qa_error *);
    bool (*client_command)(void *, const char *, qa_error *);
    /* Owned alternating directory/description entries; release with listing_free. */
    bool (*installed_mods)(void *, qa_vfs_listing *, qa_error *);
    /* Owned text bytes; size excludes an optional NUL terminator. */
    bool (*clipboard)(void *, qa_buffer *, qa_error *);
} qa_q3_host_common_services;

typedef struct qa_q3_host_server_services {
    void *context;
    uint32_t maximum_clients;
    bool (*configstring)(void *, uint32_t, const char **, qa_error *);
    bool (*set_configstring)(void *, uint32_t, const char *, qa_error *);
    bool (*userinfo)(void *, uint32_t, const char **, qa_error *);
    bool (*set_userinfo)(void *, uint32_t, const char *, qa_error *);
    bool (*user_command)(void *, uint32_t, qa_q3_usercmd *, qa_error *);
    bool (*drop_client)(void *, uint32_t, const char *, qa_error *);
    bool (*send_command)(void *, int32_t, const char *, qa_error *);
    bool (*allocate_bot)(void *, int32_t *client, qa_error *);
    bool (*free_bot)(void *, int32_t client, qa_error *);
    bool (*bot_snapshot_entity)(void *, int32_t client, int32_t sequence, int32_t *, qa_error *);
    bool (*bot_console_message)(void *, int32_t client, const char **, qa_error *);
    bool (*bot_user_command)(void *, int32_t client, const qa_q3_usercmd *, qa_error *);
    bool (*admit_actor)(void *, qa_actor_id, qa_error *);
    /* Notification after source player velocity/entity/ground stores. Updates
     * shared continuations only; writing this actor's world body would recurse. */
    bool (*player_velocity)(void *, qa_actor_id, qa_vec3, qa_error *);
    qa_actor_id (*world_actor)(void *);
} qa_q3_host_server_services;

/* Views borrow the connected client. Snapshot callbacks retain their source
 * ordering and limits; the adapter copies records before guest writes reenter. */
typedef struct qa_q3_host_client_services {
    void *context;
    const qa_q3_gamestate *(*gamestate)(void *);
    bool (*current_snapshot)(void *, int32_t *number, int32_t *time, qa_error *);
    bool (*snapshot)(void *, int32_t, const qa_q3_snapshot **, int32_t *ping, qa_error *);
    bool (*server_command)(void *, int32_t, bool *present, qa_error *);
    int32_t (*current_command)(void *);
    bool (*user_command)(void *, int32_t, qa_q3_usercmd *, bool *present, qa_error *);
    bool (*command_values)(void *, int32_t weapon, float sensitivity, qa_error *);
    bool (*source_actor)(void *, uint32_t source_number, qa_actor_id *, bool *present, qa_error *);
} qa_q3_host_client_services;

typedef struct qa_q3_host_collision_services {
    void *context;
    qa_collision_geometry *(*geometry)(void *);
    bool (*load_map)(void *, const char *, qa_error *);
} qa_q3_host_collision_services;

typedef struct qa_q3_host_presentation_services {
    void *context;
    qa_q3_presentation *seat;
    qa_font_library *fonts;
    /* The display owner supplies the fixed-width source glconfig record. */
    bool (*configuration)(void *, uint8_t out[11332], qa_error *);
    bool (*update_screen)(void *, qa_error *);
} qa_q3_host_presentation_services;

typedef enum qa_q3_host_cvar_namespace {
    QA_Q3_HOST_CVAR_ENGINE=1,
    QA_Q3_HOST_CVAR_GAME,
    QA_Q3_HOST_CVAR_CLIENT,
    QA_Q3_HOST_CVAR_MOUSE,
    QA_Q3_HOST_CVAR_MOVEMENT,
    QA_Q3_HOST_CVAR_FALLBACK,
    QA_Q3_HOST_CVAR_Q3_VIEW,
    QA_Q3_HOST_CVAR_SELECTED_VIEW
} qa_q3_host_cvar_namespace;
/* Stable roles name actual namespaces relative to this factory's retained
 * source/receiver tuple and authored seat. Reference qualifies pointer aliases
 * against that inventory; resolve follows the same physical owner through
 * publication and reconstruction. Neither infers ownership from a cvar name.
 * Both callbacks are pure, and their parent outlives the host. */
typedef struct qa_q3_host_cvar_services {
    void *context;
    bool (*reference)(void *,const qa_cvars *,qa_q3_host_cvar_namespace *,qa_error *);
    bool (*resolve)(void *,qa_q3_host_cvar_namespace,qa_cvars **,qa_error *);
} qa_q3_host_cvar_services;

/* Binding traps follow the actual retained configuration dictionary through
 * preparation and publication. Physical keys, focus and catcher ownership
 * stay on options.seat. The returned dictionary borrows the same physical
 * ordinal; the callback's retained factory context outlives the host. */
typedef struct qa_q3_host_input_services {
    void *context;
    bool (*bindings)(void *,const qa_input_seat *,qa_input_seat **,qa_error *);
} qa_q3_host_input_services;

/* The actual RenderScene syscall supplies its host and optional original QVM
 * call before the backend enters. Leave runs once after every enter invocation,
 * including a failed enter that returned a partial token. It must close that
 * scope without dispatching source code or destroying the calling host. */
typedef struct qa_q3_host_render_services {
    void *context;
    bool (*enter)(void *, const qa_q3_host *, const qa_qvm_call *,
        const qa_q3_refdef *, void **token, qa_error *);
    void (*leave)(void *, void *token, bool rendered);
} qa_q3_host_render_services;

/* Pure identity proof available only inside this host's actual RenderScene
 * enter/backend/leave bracket. A NULL source call denotes a native syscall. */
bool qa_q3_host_render_scope_current(const qa_q3_host *, const qa_qvm_call *,
    const void *frontend_lifetime, uint64_t service_owner, qa_qvm_role,
    const qa_q3_presentation *);

/* Pure decoding of the original fixed-width refEntity record. */
bool qa_q3_host_ref_entity_decode(qa_bytes, qa_q3_ref_entity *, qa_error *);
typedef bool (*qa_q3_host_source_entity_fn)(void *, const qa_qvm_call *,
    int32_t original_pointer, const qa_q3_ref_entity *, bool *suppress, qa_error *);

typedef struct qa_q3_host_options {
    qa_qvm_role role;
    qa_qvm_abi abi;
    qa_session *session;
    qa_world *world;
    qa_actor_owner owner;
    /* Console registrations and a private catcher contribution have their own
     * lifetime; zero retains the standalone actor-owner convention. */
    uint64_t service_owner;
    qa_cvars *cvars;
    /* GAME alone may borrow its shared engine sv_cheats owner. Other names
     * and ordinary handles remain in the actual private GAME registry. */
    qa_cvars *engine_cvars;
    /* Optional client timing authority supplied by the actual constructor.
     * A local CGAME factory may request its real GAME registry before the
     * client binding is installed. The application resolves that request
     * before host creation; a standalone host requires concrete ownership. */
    qa_cvars *client_time_cvars;
    qa_actor_owner client_time_owner;
    bool client_time_from_game;
    qa_console *console;
    qa_command_context command_context;
    qa_vfs *mounts;
    qa_mount_id writable_mount;
    qa_scene_resources *scene_resources;
    qa_scene_world *scene_world;
    qa_scene_frame *scene_frame;
    qa_audio_bank *sound_bank;
    qa_audio_mixer *sound_mixer;
    qa_input_seat *seat;
    qa_text_field *console_field;
    /* UI and cgame in one product may share this contribution. Its lifetime is
     * managed by the application; zero selects this host's private owner. */
    uint64_t input_owner;
    qa_q3_key *keys;
    const char *game_directory;
    qa_bot_runtime *bots;
    uint32_t bot_client_base, bot_entity_base;
    bool remapped_bot_namespace;
    bool shared_bot_lifetime;
    qa_script_defines *script_globals;
    const char *script_date, *script_time;
    qa_q3_host_common_services common;
    qa_q3_host_server_services server;
    qa_q3_host_client_services client;
    qa_q3_host_collision_services collision;
    qa_q3_host_presentation_services presentation;
    qa_q3_host_cvar_services cvar_namespaces;
    qa_q3_host_input_services input;
    qa_q3_host_render_services render;
    /* Original CGAME QVM submissions retain their current syscall token and
     * unmasked signed pointer. The source role owns this callback/context. */
    qa_q3_host_source_entity_fn source_entity;
    void *source_entity_context;
    /* Successful create consumes this lease. A failed create leaves it with
     * the caller. Release runs once after all host users and source teardown. */
    void *frontend_lifetime;
    void (*release_frontend)(void *);
    qa_bytes entity_text; /* Immutable map text borrowed for this host lifetime. */
    size_t maximum_string_bytes;
} qa_q3_host_options;

/* Owners passed in options outlive this module host. File slots and other
 * source handles belong to this host; it never destroys borrowed services. */
bool qa_q3_host_create(const qa_q3_host_options *, qa_q3_host **, qa_error *);
/* Map preparation can create the application library after host construction.
 * Attach at the idle boundary before source initialization, never by mutating
 * a copied options struct after the host was created. */
bool qa_q3_host_attach_bots(qa_q3_host *, qa_bot_runtime *, uint32_t client_base,
                           uint32_t entity_base, bool remapped_namespace,
                           bool shared_lifetime, qa_error *);
/* Pointer identity only; the borrowed runtime need not be entered or read. */
bool qa_q3_host_borrows_bots(const qa_q3_host *, const qa_bot_runtime *);
/* Admission only: portal/registry cleanup can still fail without consuming.
 * Executor ownership is checked by the module owner before calling destroy. */
bool qa_q3_host_destroy_ready(const qa_q3_host *);
/* Qualify both old and candidate hosts before exchanging frontend containers.
 * Applying the already-qualified frame binding performs no source callbacks. */
bool qa_q3_host_frontend_rebind_ready(const qa_q3_host *, const qa_scene_frame *current, const void *current_context, qa_error *);
void qa_q3_host_frontend_rebind(qa_q3_host *, qa_scene_frame *destination, const void *current_context, void *destination_context);
/* Detached restored hosts may receive their actual renderer world before
 * private continuation activation. The caller qualifies the destination's
 * saved content and owner graph; these functions qualify and move the borrow. */
const qa_scene_world *qa_q3_host_scene_world(const qa_q3_host *);
bool qa_q3_host_scene_world_rebind_ready(const qa_q3_host *, const qa_scene_world *current,
    const qa_scene_world *destination, qa_error *);
void qa_q3_host_scene_world_rebind(qa_q3_host *, qa_scene_world *destination);
/* Close this source's portal contributions before replacing map geometry.
 * Source records/body bindings remain alive until their actors retire. */
bool qa_q3_host_close_map(qa_q3_host *, qa_error *);
/* Same-map GAME restart keeps the host and its real service bindings. Admission
 * is readonly; reset additionally requires every source actor to be retired. */
bool qa_q3_host_round_ready(const qa_q3_host *, qa_error *);
bool qa_q3_host_round_reset(qa_q3_host *, qa_bytes entity_text, qa_error *);
bool qa_q3_host_destroy(qa_q3_host *, qa_error *);
/* Attach before restoring host state or querying source records. Ordinary
 * syscall entry also attaches the same executor; replacing one is rejected. */
bool qa_q3_host_attach_qvm(qa_q3_host *, qa_qvm *, qa_error *);
bool qa_q3_host_attach_native(qa_q3_host *, qa_native_host *, qa_error *);
/* After the owned executor pointer clears, retire its borrowed aliases
 * before fallible host cleanup. Prior destruction admission must be complete. */
void qa_q3_host_native_consumed(qa_q3_host *);
void qa_q3_host_qvm_consumed(qa_q3_host *);
qa_qvm_options qa_q3_host_qvm_options(qa_q3_host *, qa_qvm_semantics);
qa_native_host_q3_bridge qa_q3_host_native_bridge(qa_q3_host *);
typedef struct qa_q3_host_game_data {
    uint32_t entity_count, entity_stride, client_count, client_stride;
    /* Source QVM offsets or native addresses, without the host pointer tag. */
    uint64_t entities_address, clients_address;
} qa_q3_host_game_data;
bool qa_q3_host_game_data_read(const qa_q3_host *, qa_q3_host_game_data *);
bool qa_q3_host_entity(qa_q3_host *, uint32_t, qa_q3_entity *, qa_qvm_entity_shared *, qa_error *);
bool qa_q3_host_player(qa_q3_host *, uint32_t, qa_q3_player *, qa_error *);
/* Qualified guest adapters read original enum/flag words. Presentation
 * translation must not reject mod-private words needed by gameplay views. */
bool qa_q3_host_source_entity(qa_q3_host *, uint32_t, qa_q3_entity *, qa_qvm_entity_shared *, qa_error *);
bool qa_q3_host_source_player(qa_q3_host *, uint32_t, qa_q3_player *, qa_error *);
bool qa_q3_host_write_player(qa_q3_host *, uint32_t, const qa_q3_player *, qa_error *);
/* Mutates only public playerState motion/view fields. The application retains
 * cutscene lifetime, suppresses input callbacks, and updates canonical body,
 * combat and links. The latest source usercmd supplies forced-view deltas. */
bool qa_q3_host_player_cutscene(qa_q3_host *, qa_actor_id, qa_vec3 origin,
                                qa_vec3 view_angles, int32_t view_height, qa_error *);
/* The application captures each guest's prior pmType through player() before
 * entering the cutscene. Release retains the final pose and restores that mode. */
bool qa_q3_host_player_cutscene_clear(qa_q3_host *, qa_actor_id, int32_t prior_pm_type, qa_error *);
/* Source slot ownership is checked against the canonical registry. Borrowed
 * projections never replace a foreign actor's body or collision authority. */
bool qa_q3_host_bind_actor(qa_q3_host *, uint32_t, qa_actor_id, bool borrowed, qa_error *);
bool qa_q3_host_actor(qa_q3_host *, uint32_t, bool create, qa_actor_id *, qa_error *);
bool qa_q3_host_actor_slot(const qa_q3_host *, qa_actor_id, uint32_t *, qa_error *);
bool qa_q3_host_actor_released(qa_q3_host *, qa_actor_record, qa_error *);
/* Remove an idle borrowed source projection without releasing its shared actor. */
bool qa_q3_host_detach_actor(qa_q3_host *, uint32_t source_slot, qa_actor_id, qa_error *);
bool qa_q3_host_player_motion(qa_q3_host *, uint32_t, bool begin, qa_error *);
/* A retired client cannot release/reuse its canonical actor until every
 * admitted source and outer completion motion scope has drained. */
bool qa_q3_host_input_idle(const qa_q3_host *, uint32_t);
bool qa_q3_host_source_input(const qa_q3_host *, qa_input_seat **, uint64_t *owner);
qa_console *qa_q3_host_console(const qa_q3_host *, qa_cvars **, qa_command_context *);
typedef struct qa_q3_host_cvar_cache {
    qa_qvm *vm;
    qa_native_instance *native;
    int32_t source_pointer,handle,modification;
    uint64_t address;
    float number;
    int32_t integer;
    char value[256];
} qa_q3_host_cvar_cache;
/* Actual CGAME cg_drawstatus vmCvar registrations/updates, keyed by original
 * QVM pointer and executor or native address and memory owner. Read only the
 * real guest cache; a changed handle reports absent, never inferred status. */
size_t qa_q3_host_cvar_cache_count(const qa_q3_host *);
bool qa_q3_host_cvar_cache_read(const qa_q3_host *,size_t,qa_q3_host_cvar_cache *,bool *found,qa_error *);
typedef struct qa_q3_host_client_context {
    qa_session *session;
    qa_qvm_role role;
    qa_actor_owner owner;
    uint64_t service_owner;
    qa_console *console;
    qa_cvars *cvars;
    qa_cvars *client_time_cvars;
    qa_actor_owner client_time_owner;
    qa_command_context command_context;
    void *frontend_lifetime;
} qa_q3_host_client_context;
/* Pure borrowed identity, valid during the host's synchronous source callbacks.
 * frontend_lifetime stays owned by the host and must not be released here. */
bool qa_q3_host_client_context_read(const qa_q3_host *, qa_q3_host_client_context *);
bool qa_q3_host_retire_input(qa_q3_host *, uint32_t, bool retired, qa_error *);
typedef struct qa_q3_host_visibility {
    int32_t area, area2, last_cluster, clusters[16];
    uint32_t cluster_count;
} qa_q3_host_visibility;
bool qa_q3_host_link(qa_q3_host *, uint32_t slot, qa_error *);
bool qa_q3_host_unlink(qa_q3_host *, uint32_t slot, qa_error *);
bool qa_q3_host_visibility_read(qa_q3_host *, uint32_t slot,
                                qa_q3_host_visibility *, bool *present, qa_error *);
/* Called by the map transaction after attaching its immutable source text. */
bool qa_q3_host_set_entity_text(qa_q3_host *, qa_bytes, qa_error *);
typedef struct qa_q3_host_portal_claim {
    uint64_t map_identity, contributions;
    qa_collision_family family;
    uint32_t first, second, portal;
} qa_q3_host_portal_claim;
/* Candidate admission sums claims from every provider before publishing the
 * restored shared geometry. Q2 claims name real portals; Q3 names area pairs. */
size_t qa_q3_host_portal_claim_count(const qa_q3_host *);
bool qa_q3_host_portal_claim_at(const qa_q3_host *, size_t, qa_q3_host_portal_claim *);
bool qa_q3_host_checkpoint(qa_q3_host *, qa_buffer *, qa_error *);
/* Restore requires an attached executor, restored actor registry and qualified
 * immutable world geometry. It reconstructs source body/collision callbacks;
 * the WORLD owner must then restore and validate body fields, links and portal
 * counts before finish. READ descriptors own the exact saved immutable
 * path/digest/bytes and cursor without filesystem acquisition or cache mutation.
 * Failed binding installation retains its contexts until candidate actors retire. */
bool qa_q3_host_restore(qa_q3_host *, qa_bytes, qa_error *);
/* After aggregate portal-claim and whole-candidate validation, qualify saved
 * cvar namespace references and original VM cache addresses, then
 * publish this host's restored continuation. No allocation or source dispatch
 * occurs. */
bool qa_q3_host_finish_restore(qa_q3_host *, qa_error *);

#endif
