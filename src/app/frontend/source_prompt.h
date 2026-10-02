#ifndef QA_FRONTEND_SOURCE_PROMPT_H
#define QA_FRONTEND_SOURCE_PROMPT_H
#include "internal.h"
typedef struct frontend_source_prompt frontend_source_prompt;
bool frontend_source_prompt_create(frontend_seat *, qa_ui_id, frontend_source_prompt **, qa_error *);
bool frontend_source_prompt_idle(const frontend_source_prompt *);
bool frontend_source_prompt_destroy(frontend_source_prompt **, qa_error *);
bool frontend_source_prompt_supported(const frontend_source_prompt *, qa_actor_id);
bool frontend_source_prompt_receive(frontend_source_prompt *, const qa_builtin_event *, qa_error *);
bool frontend_source_prompt_prepare(frontend_source_prompt *, qa_error *);
bool frontend_source_prompt_input(frontend_source_prompt *, const qa_input_event *, bool *, qa_error *);
bool frontend_source_prompt_checkpoint(const frontend_source_prompt *, qa_buffer *, qa_error *);
bool frontend_source_prompt_restore(frontend_source_prompt *, qa_bytes, qa_error *);
bool frontend_source_prompt_restore_finish(frontend_source_prompt *, qa_error *);
#endif
