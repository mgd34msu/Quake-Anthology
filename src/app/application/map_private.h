#ifndef QA_APPLICATION_MAP_PRIVATE_H
#define QA_APPLICATION_MAP_PRIVATE_H

#include "internal.h"
#include "qa/game_q1_wire.h"
#include "qa/game_q2_wire.h"

typedef struct application_player_travel application_player_travel;
bool application_map_prepare_content(qa_application *, application_publication *, qa_error *);
bool application_map_prepare_points(qa_application *, application_publication *, qa_error *);
bool application_map_restore_points(qa_application *, application_player_travel *, qa_error *);
bool application_map_restore_bind(qa_application *, const qa_launch_snapshot *, qa_error *);
bool application_map_restore_identity(qa_application *, const qa_launch_snapshot *, qa_error *);
bool application_players_restore_prepare(qa_application *, const qa_launch_choices *, qa_error *);

bool application_players_prepare(qa_application *, application_publication *,
                                  bool, bool, const qa_q2_landmark *,
                                  application_player_travel **, qa_error *);
bool application_players_publish(qa_application *, const qa_launch_choices *,
                                  application_player_travel *, qa_error *);
void application_players_dispose(application_player_travel *);
bool application_players_point(application_player_travel *, qa_mode_spawnpoint,
                               qa_string_id, uint32_t, qa_error *);
bool application_players_q1_points(application_player_travel *, uint32_t,
    const qa_q1_wire_binding *, size_t, qa_error *);
bool application_players_q2_points(application_player_travel *, uint32_t,
    const qa_q2_wire_binding *, size_t, qa_error *);
void application_players_world_type(application_player_travel *, int32_t);
void application_players_close(qa_application *);
bool application_map_spawn_point(qa_application *, const qa_launch_choices *,
                                 qa_string_id *, qa_error *);
bool application_players_advance(qa_application *, qa_error *);
void application_map_travel_options(const qa_application *, bool *, bool *,
                                    const qa_q2_landmark **);

#endif
