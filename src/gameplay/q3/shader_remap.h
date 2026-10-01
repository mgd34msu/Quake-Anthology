#ifndef QA_Q3_SHADER_REMAP_INTERNAL_H
#define QA_Q3_SHADER_REMAP_INTERNAL_H

#include "qa/game_q3_shader_remap.h"
#include "qa/source_save.h"

void q3_shader_remaps_clear(qa_q3_game *);
bool q3_shader_remaps_capture(const qa_q3_game *, qa_q3_shader_remap_state *, qa_error *);
bool q3_shader_remaps_prepare(const qa_q3_shader_remap_state *,
                               qa_q3_shader_remap_state *, qa_error *);
void q3_shader_remaps_commit(qa_q3_game *, const qa_q3_shader_remap_state *);
bool q3_shader_remaps_codec(qa_source_save_io *, qa_q3_shader_remap_state *);
/* Reconstructed with the genuine Q3 map target binding, never serialized. */
bool q3_shader_remap_target(void *, qa_actor_id source, qa_string_id old_name,
                             qa_string_id new_name, uint64_t time_ns, qa_error *);

#endif
