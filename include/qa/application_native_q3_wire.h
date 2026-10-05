#ifndef QA_APPLICATION_NATIVE_Q3_WIRE_H
#define QA_APPLICATION_NATIVE_Q3_WIRE_H

#include "qa/application_q3_client.h"
#include "qa/network_q3.h"

/* Only the actual synchronous native GAME wire service constructor owns this
 * exact delegate pointer. It creates no original Q3 host or CLIENT namespace. */
bool qa_application_native_q3_wire_preconstruction_current(const qa_application *,
    qa_actor_owner, uint32_t seat, const qa_q3_host_options *);

/* This is the genuine local GAME wire lease, independent of a guest host. */
typedef struct application_native_q3_wire_client_lease qa_native_q3_wire_reader;
typedef struct qa_native_q3_wire_basis {
    qa_application *application;
    qa_session *session;
    const qa_q3_game *source_game;
    qa_cvars *source_cvars;
    qa_actor_owner source_owner, receiver;
    qa_actor_id actor;
    qa_q3_product product;
    uint32_t seat, physical_client;
    uint64_t publication_generation, map_revision;
} qa_native_q3_wire_basis;
typedef struct qa_native_q3_wire_publication {
    int32_t initial_command_sequence, latest_command_sequence, reached_command_sequence;
    int32_t snapshot_number, snapshot_time, user_command_number;
    bool has_gamestate, has_snapshot;
} qa_native_q3_wire_publication;
typedef struct qa_native_q3_wire_receipt {
    const qa_native_q3_wire_reader *reader;
    qa_actor_id actor;
    uint64_t publication_generation, map_revision;
    int32_t sequence;
    bool present;
    const qa_command_tokens *arguments;
} qa_native_q3_wire_receipt;

/* Pure installed-source admission also supports an isolated PERSISTING import.
 * The real wire graph and fixed client must already be restored. The lease
 * prevents source GAME/console retirement; release never destroys those owners. */
bool qa_native_q3_wire_reader_acquire(qa_application *, qa_actor_owner receiver,
    uint32_t seat, uint32_t physical_client, qa_actor_id, qa_native_q3_wire_reader **empty, qa_error *);
bool qa_native_q3_wire_reader_destroy(qa_native_q3_wire_reader **owned, qa_error *);
bool qa_native_q3_wire_reader_current(const qa_native_q3_wire_reader *);
bool qa_native_q3_wire_reader_idle(const qa_native_q3_wire_reader *);
bool qa_native_q3_wire_reader_basis(const qa_native_q3_wire_reader *, qa_native_q3_wire_basis *, qa_error *);
bool qa_native_q3_wire_reader_publication(const qa_native_q3_wire_reader *, qa_native_q3_wire_publication *, qa_error *);
/* Claims the next reliable command through the real CL_GetServerCommand owner:
 * BCS assembly, reached gamestate, SystemInfo/effects and acknowledgment happen
 * before receipt publication. Text/tokens borrow until the next claim. */
bool qa_native_q3_wire_reader_command(qa_native_q3_wire_reader *, int32_t sequence,
    qa_native_q3_wire_receipt *, qa_error *);
bool qa_native_q3_wire_receipt_current(const qa_native_q3_wire_receipt *);
/* Reads the reached client gamestate, never latest authoritative GAME text.
 * Revision is the actual command sequence that reached this row, including the
 * initial gamestate baseline. Borrowed text survives until the next mutation. */
bool qa_native_q3_wire_reader_configstring(const qa_native_q3_wire_reader *, uint32_t index,
    const char **text, uint64_t *revision, qa_error *);
bool qa_native_q3_wire_reader_snapshot(qa_native_q3_wire_reader *, int32_t number,
    const qa_q3_snapshot **, int32_t *ping, qa_error *);
bool qa_native_q3_wire_reader_user_command(qa_native_q3_wire_reader *, int32_t number,
    qa_q3_usercmd *, bool *present, qa_error *);
bool qa_native_q3_wire_reader_command_values(qa_native_q3_wire_reader *, int32_t weapon,
    float sensitivity, qa_error *);
/* Current local CGAME command selection, held by its actual GAME wire client. */
bool qa_application_native_q3_input_values_read(qa_application *, uint32_t seat, qa_actor_id,
    uint8_t *weapon, float *sensitivity, bool *present, qa_error *);
bool qa_native_q3_wire_reader_actor(qa_native_q3_wire_reader *, uint32_t source_number,
    qa_actor_id *, bool *present, qa_error *);
bool qa_native_q3_wire_reader_reliable(qa_native_q3_wire_reader *, const char *, qa_error *);
bool qa_native_q3_wire_reader_effect(qa_native_q3_wire_reader *, qa_application_q3_client_effect,
    const char *, qa_error *);
/* The actual GAME/wire graph is imported first. These pure codecs retain the
 * reader's owned arguments and receipt continuation against its exact installed
 * binding. Restore runs no tokenizer, reliable claim or frontend effect. */
bool qa_native_q3_wire_reader_checkpoint(const qa_native_q3_wire_reader *, qa_buffer *, qa_error *);
bool qa_native_q3_wire_reader_restore(qa_native_q3_wire_reader *, qa_bytes, qa_error *);

#endif
