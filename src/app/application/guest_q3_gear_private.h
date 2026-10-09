#ifndef QA_APPLICATION_GUEST_Q3_GEAR_PRIVATE_H
#define QA_APPLICATION_GUEST_Q3_GEAR_PRIVATE_H

#include "guest_q3_gear.h"
#include "qa/binary.h"
#include "qa/qvm_save.h"
#include "qa/cvars_save.h"
#include "qa/source_save.h"
#include "qa/bsp.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct q3gear_binding {
    qa_actor_id actor;
    uint32_t pointer;
    qa_vec3 origin;
    bool player, retired, connected, begun;
} q3gear_binding;
typedef struct q3gear_tether {
    qa_actor_id owner, actor;
    uint32_t hook;
    bool tracked, pulling, orphaned;
} q3gear_tether;
struct application_q3_gear {
    application_q3_gear_options options;
    const application_q3_grapple_definition *definition;
    qa_qvm_image *image;
    qa_qvm *vm;
    qa_q3_host *host;
    qa_qvm_options lower;
    qa_cvars *cvars;
    qa_cvar_handle no_curves, player_curve_clip, cheats;
    qa_console *console;
    qa_buffer entities;
    char *path, *configstrings[1024], *userinfo[64];
    const qa_command_invocation *arguments;
    q3gear_binding *bindings;
    q3gear_tether *tethers;
    uint32_t capacity;
    qa_qvm_binding same_team, damage, pull, mover;
    int32_t milliseconds, frame;
    qa_vec3 forward, translation;
    bool busy, synchronizing, initialized, restoring, pull_pending, mover_pending, draining;
    uint32_t pull_client;
};
bool q3gear_fail(qa_error *, qa_status, const char *);
bool q3gear_current(application_q3_gear *, qa_error *);
bool q3gear_word(application_q3_gear *, uint32_t, int32_t *, qa_error *);
bool q3gear_store(application_q3_gear *, uint32_t, int32_t, qa_error *);
bool q3gear_vector(application_q3_gear *, uint32_t, qa_vec3 *, qa_error *);
bool q3gear_vector_store(application_q3_gear *, uint32_t, qa_vec3, qa_error *);
bool q3gear_slot(application_q3_gear *, uint32_t pointer, uint32_t *, qa_error *);
bool q3gear_layout(application_q3_gear *, qa_q3_host_game_data *, qa_error *);
bool q3gear_filter_entities(qa_bytes, qa_buffer *, qa_error *);
bool q3gear_services(application_q3_gear *, qa_error *);
bool q3gear_syscall(void *, const qa_qvm_call *, int32_t, int32_t *, qa_error *);
bool q3gear_host_checkpoint(void *, qa_buffer *, qa_error *);
bool q3gear_host_restore(void *, qa_bytes, qa_error *);
bool q3gear_replace_text(char **, const char *, qa_error *);
bool q3gear_target(application_q3_gear *, qa_actor_id, application_q3_gear_target *, qa_error *);
bool q3gear_mirror(application_q3_gear *, const application_q3_gear_target *, qa_error *);
bool q3gear_reserve(application_q3_gear *, uint32_t slot, qa_error *);
bool q3gear_forget(application_q3_gear *, qa_actor_id, qa_error *);
bool q3gear_release(application_q3_gear *, qa_actor_id, bool, qa_error *);
bool q3gear_hook(application_q3_gear *, qa_actor_id, uint32_t *, uint32_t *, qa_error *);
bool q3gear_sound(application_q3_gear *, qa_actor_id, const char *, bool, qa_error *);
bool q3gear_flush_releases(application_q3_gear *, qa_error *);
bool q3gear_leave(application_q3_gear *, bool success, qa_error *);
bool q3gear_bind_hooks(application_q3_gear *, qa_error *);
size_t q3gear_descriptors(application_q3_gear *, qa_qvm_saved_function out[4]);
bool q3gear_sync(application_q3_gear *, qa_error *);
bool q3gear_publish(application_q3_gear *, q3gear_tether *, qa_error *);
qa_actor_id q3gear_actor(application_q3_gear *, int32_t pointer);
bool q3gear_call(application_q3_gear *, uint32_t instruction,
    const int32_t *, size_t, int32_t *, qa_error *);

#endif
