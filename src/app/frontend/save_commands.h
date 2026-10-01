#ifndef QA_FRONTEND_SAVE_COMMANDS_H
#define QA_FRONTEND_SAVE_COMMANDS_H
#include "qa/frontend.h"
#include "qa/source_save.h"

bool frontend_save_commands_create(qa_frontend *, qa_error *);
bool frontend_save_commands_destroy(qa_frontend *, qa_error *);
bool frontend_save_commands_idle(const qa_frontend *);
bool frontend_save_commands_capture_ready(const qa_frontend *);
bool frontend_save_commands_queue(qa_frontend *, const qa_command_invocation *, qa_error *);
/* Runs only after step returns; publishes through the driver's actual slot. */
bool frontend_save_commands_drain(qa_frontend **slot, qa_error *);
bool frontend_save_commands_checkpoint(qa_frontend *, qa_buffer *, qa_error *);
/* The candidate's real tools root and core command generation must exist.
 * Import creates its own owner; no active manager pointer transfers. */
bool frontend_save_commands_restore(qa_frontend *, qa_bytes, qa_error *);
#endif
