/* Port of src/content/q2/foundation/movers.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_FOUNDATION_MOVERS_H
#define QA_CONTENT_Q2_FOUNDATION_MOVERS_H
#include "qa/content/q2/foundation/angular_motion.h"
#include "qa/content/q2/foundation/motion.h"
typedef struct qa_q2_train_stop { qa_actor_id actor; qa_vec3 origin; bool has_next; qa_actor_id next; double wait; bool teleport; } qa_q2_train_stop;
typedef struct qa_q2_train_route { bool running; bool has_destination; qa_actor_id destination; const qa_q2_train_stop *stops; size_t stop_count; } qa_q2_train_route;
typedef struct qa_q2_door_checkpoint_state { qa_vec3 start, end, safe_direction; double distance, water_divisor, debounce; bool button, angular, water, reversed, activated; const char *phase; } qa_q2_door_checkpoint_state;
typedef struct qa_q2_door_checkpoint { qa_saved_actor_id actor; qa_q2_door_checkpoint_state state; qa_saved_actor_id master; const qa_saved_actor_id *team; size_t team_count; } qa_q2_door_checkpoint;
/* TS train entry; has_wait false = TS wait undefined (older saves). */
typedef struct qa_q2_train_checkpoint { qa_saved_actor_id actor; bool has_destination; qa_saved_actor_id destination; double debounce; bool has_wait; double wait; bool ship; } qa_q2_train_checkpoint;
typedef struct qa_q2_movers_checkpoint { const qa_q2_door_checkpoint *doors; size_t door_count; const qa_q2_train_checkpoint *trains; size_t train_count; qa_q2_linear_motion_checkpoint linear; qa_q2_angular_motion_checkpoint angular; } qa_q2_movers_checkpoint;
typedef struct qa_q2_mover_hooks { void *ctx; void (*path_corner)(void *ctx, qa_q2_entity *c, qa_q2_game_services *g, qa_actor_id other); void (*combat_point)(void *ctx, qa_q2_entity *p, qa_q2_game_services *g, qa_actor_id other); } qa_q2_mover_hooks;
typedef struct qa_q2_mover_module qa_q2_mover_module;
qa_q2_mover_module *qa_q2_mover_module_create(qa_q2_mover_hooks hooks);
qa_q2_mover_module *qa_q2_create_mover_module(qa_q2_mover_hooks hooks);
void qa_q2_mover_module_destroy(qa_q2_mover_module *m);
qa_q2_spawn_module qa_q2_mover_module_as_spawn(qa_q2_mover_module *m);
qa_q2_linear_motion *qa_q2_mover_module_linear(qa_q2_mover_module *m);
qa_q2_angular_motion *qa_q2_mover_module_angular(qa_q2_mover_module *m);
bool qa_q2_mover_module_train_route(qa_q2_mover_module *m, qa_q2_entity *e, qa_q2_game_services *g, qa_q2_train_route *out);
typedef struct qa_q2_traversal { bool locked; bool has_destination; qa_vec3 destination; } qa_q2_traversal;
qa_q2_traversal qa_q2_mover_module_traversal(qa_q2_mover_module *m, qa_q2_entity *e);
qa_q2_callback_definitions qa_q2_mover_module_callbacks(qa_q2_mover_module *m);
qa_q2_movers_checkpoint qa_q2_mover_module_capture(qa_q2_mover_module *m, qa_q2_game_services *g);
void qa_q2_mover_module_restore(qa_q2_mover_module *m, qa_q2_game_services *g, qa_q2_movers_checkpoint cp);
bool qa_q2_mover_module_spawn(qa_q2_mover_module *m, qa_q2_entity *e, qa_q2_game_services *g);
void qa_q2_mover_module_resume_train_at(qa_q2_mover_module *m, qa_q2_entity *e, qa_q2_game_services *g, qa_q2_entity *corner);
qa_vec3 qa_q2_mover_module_train_direction(qa_q2_mover_module *m, qa_q2_entity *e, qa_q2_game_services *g);
void qa_q2_mover_module_spawn_train(qa_q2_mover_module *m, qa_q2_entity *e, qa_q2_game_services *g);
#endif
