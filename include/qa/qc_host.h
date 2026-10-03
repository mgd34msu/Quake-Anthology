#ifndef QA_QC_HOST_H
#define QA_QC_HOST_H
#include "qa/bsp.h"
#include "qa/builtin.h"
#include "qa/console.h"
#include "qa/qc.h"

typedef struct qa_qc_game qa_qc_game;
typedef enum qa_qc_game_value_kind {
    QA_QC_GAME_FLOAT, QA_QC_GAME_VECTOR, QA_QC_GAME_STRING,
    QA_QC_GAME_ACTOR, QA_QC_GAME_INTEGER, QA_QC_GAME_FUNCTION, QA_QC_GAME_FIELD
} qa_qc_game_value_kind;
typedef struct qa_qc_game_value {
    qa_qc_game_value_kind kind;
    union { float number; qa_vec3 vector; const char *string; qa_actor_id actor;
            int32_t integer; } value;
} qa_qc_game_value;
typedef struct qa_qc_game_global { const char *name; qa_qc_game_value value; } qa_qc_game_global;
typedef enum qa_qc_resource_kind { QA_QC_RESOURCE_MODEL, QA_QC_RESOURCE_SOUND } qa_qc_resource_kind;
typedef struct qa_qc_game_resource { uint32_t index; qa_bounds bounds; } qa_qc_game_resource;
typedef struct qa_qc_game_options {
    qa_qc_options vm;
    qa_builtin_services services;
    qa_cvars *cvars;
    qa_console *console;
    qa_command_context command_context;
    uint32_t max_clients;
    uint32_t map_exclusion_flags;
    void *context;
    /* Shared per-provider source precache ordering; lookup must reject absent
     * resources. Text is borrowed only for the callback. */
    bool (*resource)(void *, qa_qc_resource_kind, const char *, bool precache,
                       qa_qc_game_resource *, qa_error *);
    /* Captures/restores all coupled engine state: source precaches, cvars,
     * message routing buffers, client/spawn parms and extension continuations.
     * Mandatory; host never silently omits application-owned state. */
    bool (*checkpoint)(void *, qa_buffer *, qa_error *);
    bool (*restore)(void *, qa_bytes, qa_error *);
} qa_qc_game_options;
/* Program/shared authorities are borrowed. Remaining vm.host.builtins are real
 * required engine capabilities; creation validates the full selected profile.
 * Explicit bindings override defaults, enabling selected cross-game owners.
 * prepare_entity and observers retain their supplied contexts. */
bool qa_qc_game_create(const qa_qc_program *, const qa_qc_game_options *, qa_qc_game **,
                        qa_error *);
bool qa_qc_game_destroy(qa_qc_game *, qa_error *);
bool qa_qc_game_idle(const qa_qc_game *);
qa_qc_instance *qa_qc_game_instance(qa_qc_game *);
bool qa_qc_game_bind_client(qa_qc_game *, uint32_t client, qa_actor_id, qa_error *);
void qa_qc_game_actor_released(qa_qc_game *, qa_actor_record);
bool qa_qc_game_set_time(qa_qc_game *, double seconds, double frame_seconds, qa_error *);
bool qa_qc_game_loading(qa_qc_game *, bool loading, qa_error *);
/* Called only after retiring all old actor bindings. Retains engine-owned
 * continuations, recreates level-private globals/entities and VM strings. */
bool qa_qc_game_reset_level(qa_qc_game *, qa_error *);
/* A call saves reserved ABI and supplied globals, stages typed arguments, and
 * restores every staged word on success or guest failure, including reentry.
 * Result is three raw words; integer bits preserve entity/string/function ABI. */
bool qa_qc_game_call(qa_qc_game *, const char *function, const qa_qc_game_value *, size_t,
                      const qa_qc_game_global *, size_t, uint32_t result[3], qa_error *);
bool qa_qc_game_call_index(qa_qc_game *, uint32_t function, const qa_qc_game_value *, size_t,
                            const qa_qc_game_global *, size_t, uint32_t result[3], qa_error *);
bool qa_qc_game_callback(qa_qc_game *, qa_actor_id self, qa_actor_id other,
                          const char *field, qa_error *);
/* Source ED_ParseEdict conventions: angle/light aliases, escaped newlines,
 * declared types, unknown/editor keys ignored. World uses edict0; dynamic rows
 * start after reserved clients. Unknown class/function fails explicitly. */
bool qa_qc_game_spawn_entity(qa_qc_game *, bool world, const qa_entity_property *, size_t,
                              int32_t *reference, qa_error *);
/* Application-selected physics provider reads/writes the private QC fields;
 * shared body storage is supplied by the existing VM body binding. */
bool qa_qc_game_read_physics(qa_qc_game *, qa_actor_id, qa_physics_properties *, qa_error *);
bool qa_qc_game_write_physics(qa_qc_game *, qa_actor_id, const qa_physics_properties *, qa_error *);
/* Private VM state and host blob remain under the existing QC checkpoint API. */
bool qa_qc_game_capture(qa_qc_game *, qa_qc_checkpoint **, qa_error *);
bool qa_qc_game_restore(qa_qc_game *, const qa_qc_checkpoint *, qa_error *);
#endif
