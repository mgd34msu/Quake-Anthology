/* Port of src/app/bootstrap/simulation/native-q2-map.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NATIVE_Q2_MAP_H
#define QA_APP_BOOTSTRAP_SIMULATION_NATIVE_Q2_MAP_H
typedef struct qa_app_sim_native_q2_map_geometry { const char *kind; const char *entities; double model_count; } qa_app_sim_native_q2_map_geometry;
/* Malloc'd entity text. edition "classic"|"rerelease"; mode "singleplayer"|"coop"|"deathmatch". */
char *qa_app_sim_prepare_native_q2_map(qa_app_sim_native_q2_map_geometry world, const char *edition, const char *mode);
#endif
