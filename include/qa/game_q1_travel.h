#ifndef QA_GAME_Q1_TRAVEL_H
#define QA_GAME_Q1_TRAVEL_H
#include "qa/game_q1.h"

typedef struct qa_q1_travel_state qa_q1_travel_state;
/* The originating session string table must outlive this retained state.
 * Decode remaps saved string bytes into its explicitly supplied session. */
typedef struct qa_q1_travel_ctf {
    int32_t last_team;
    float status, access;
    bool start_map, pregame_over, observer, grapple_enabled, grapple_disabled;
} qa_q1_travel_ctf;
/* These callbacks borrow the actual source ThreeWave controller and selected
 * grapple admission. They must reject a retired or differently bound owner. */
typedef struct qa_q1_travel_services {
    void *context;
    bool (*ctf_read)(void *, qa_actor_id, qa_q1_travel_ctf *, qa_error *);
    bool (*ctf_restore)(void *, qa_actor_id, int32_t last_team,
                        float status, float access, qa_error *);
} qa_q1_travel_services;

bool qa_q1_travel_new(qa_q1_game *, qa_actor_id, const qa_q1_travel_services *,
                       qa_q1_travel_state **, qa_error *);
bool qa_q1_travel_capture(qa_q1_game *, qa_actor_id, qa_item_id selected_weapon,
                           const qa_q1_travel_services *, qa_q1_travel_state **, qa_error *);
bool qa_q1_travel_admit(qa_q1_game *, qa_actor_id, qa_q1_travel_state *,
                         const qa_q1_travel_services *, qa_error *);
bool qa_q1_travel_retain(qa_q1_travel_state *, qa_error *);
void qa_q1_travel_destroy(qa_q1_travel_state *);
bool qa_q1_travel_encode(qa_session *, const qa_q1_travel_state *, qa_buffer *, qa_error *);
bool qa_q1_travel_decode(qa_session *, qa_bytes, qa_q1_travel_state **, qa_error *);
#endif
