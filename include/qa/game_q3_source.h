#ifndef QA_GAME_Q3_SOURCE_H
#define QA_GAME_Q3_SOURCE_H

#include "qa/game_q3.h"

bool qa_q3_source_dropped_read(const qa_q3_game *, uint32_t, bool *, qa_error *);
bool qa_q3_source_physics_object_read(const qa_q3_game *, uint32_t, bool *, qa_error *);
/* Borrowed until the next mutation. Constructor-null rows have a null classname;
 * victory models borrow the current original fixed client's netname. */
bool qa_q3_source_classname_read(const qa_q3_game *, uint32_t, const char **, qa_error *);
bool qa_q3_source_memory_allocate(qa_q3_game *, uint32_t size, uint32_t *offset, qa_error *);
bool qa_q3_source_memory_rewind(qa_q3_game *, qa_error *);
bool qa_q3_source_memory_read(const qa_q3_game *, uint32_t offset, void *, uint32_t size, qa_error *);
bool qa_q3_source_memory_write(qa_q3_game *, uint32_t offset, const void *, uint32_t size, qa_error *);
/* The byte span stays at its real GAME pool address through allocation/rewind.
 * The caller must retain the source owner and obey its mutation lifetime. */
bool qa_q3_source_memory_span(qa_q3_game *, uint32_t offset, uint32_t size, uint8_t **, qa_error *);
bool qa_q3_source_memory_allocated(const qa_q3_game *, uint32_t *, qa_error *);
bool qa_q3_source_memory_status(qa_q3_game *, qa_error *);
#include "qa/game_q3_source_types.h"

/* Physical GAME rows are independent of canonical actor storage and owner.
 * Client and world admission use the actual server's source slot and actor.
 * Dynamic rows are allocated by the native G_Spawn producer. */
bool qa_q3_source_bind_client(qa_q3_game *, uint32_t slot, qa_actor_id, qa_error *);
bool qa_q3_source_bind_world(qa_q3_game *, qa_actor_id, qa_error *);
bool qa_q3_native_client_slot(const qa_q3_game *, qa_actor_id, uint32_t *, qa_error *);
bool qa_q3_source_actor_slot(const qa_q3_game *, qa_actor_id, uint32_t *, qa_error *);
bool qa_q3_source_entity_count(const qa_q3_game *, uint32_t *, qa_error *);
bool qa_q3_source_max_clients(const qa_q3_game *, uint32_t *, qa_error *);
bool qa_q3_source_binding_read(const qa_q3_game *, uint32_t,
                               qa_q3_source_binding *, qa_error *);
bool qa_q3_source_new_session_read(const qa_q3_game *, bool *, qa_error *);
bool qa_q3_source_new_session_set(qa_q3_game *, bool, qa_error *);
bool qa_q3_source_team_location_time_read(const qa_q3_game *, int32_t *, qa_error *);
bool qa_q3_source_team_location_time_set(qa_q3_game *, int32_t, qa_error *);
bool qa_q3_source_spawnflags_read(const qa_q3_game *, qa_actor_id, int32_t *, qa_error *);
bool qa_q3_source_client_counts_read(const qa_q3_game *, qa_q3_source_client_counts *, qa_error *);
bool qa_q3_source_client_counts_write(qa_q3_game *, const qa_q3_source_client_counts *, qa_error *);
bool qa_q3_source_team_state_read(const qa_q3_game *, qa_q3_source_team_state *, qa_error *);
bool qa_q3_source_team_state_write(qa_q3_game *, const qa_q3_source_team_state *, qa_error *);
bool qa_q3_source_match_state_read(const qa_q3_game *, qa_q3_source_match_state *, qa_error *);
bool qa_q3_source_match_state_write(qa_q3_game *, const qa_q3_source_match_state *, qa_error *);
/* A restored cold GAME binds its saved warmup observation to the current
 * copied Source cvar revision without entering the match or resetting clocks. */
bool qa_q3_source_warmup_rebind(qa_q3_game *, uint64_t copied_revision, bool observed, qa_error *);
bool qa_q3_source_match_context_read(const qa_q3_game *, qa_q3_product *, int32_t *, qa_error *);
bool qa_q3_source_current_origin_read(const qa_q3_game *, qa_actor_id, qa_vec3 *, qa_error *);
/* Ordinary native source actor dispatch, independent of selected execution.
 * The real source frame invokes each physical actor at most once. */
bool qa_q3_source_run_actor(qa_q3_game *, qa_actor_id,
                            const qa_source_frame *, qa_error *);
bool qa_q3_source_team_sound(qa_q3_game *, qa_vec3, int32_t event_parameter, qa_error *);
bool qa_q3_source_team_gesture(qa_q3_game *, int32_t source_team, qa_error *);
bool qa_q3_source_award_visual(qa_q3_game *, qa_actor_id, uint32_t flag, qa_error *);
bool qa_q3_source_score_plum(qa_q3_game *, qa_actor_id, qa_vec3, int32_t score, qa_error *);
/* Reset the three retained podium pointers without freeing their source rows. */
bool qa_q3_source_reset_podium_players(qa_q3_game *, qa_error *);

#endif
