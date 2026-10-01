#ifndef QA_FRONTEND_SELECTED_CHARACTER_SAVE_H
#define QA_FRONTEND_SELECTED_CHARACTER_SAVE_H
#include "selected_character.h"

/* Prepare actual empty registries over imported visual heaps before the Q3
 * registry dictionary. Late private import attaches retained resources and
 * declaration/pose continuation after the real actor roster is installed. */
bool frontend_selected_character_topology_checkpoint(const qa_frontend *, qa_buffer *, qa_error *);
bool frontend_selected_character_prepare_restored(qa_frontend *, qa_bytes, qa_error *);
bool frontend_selected_character_checkpoint(const qa_frontend *, size_t, qa_buffer *, qa_error *);
bool frontend_selected_character_restore(qa_frontend *, size_t, qa_bytes, qa_error *);
bool frontend_selected_character_topology_ready(const qa_frontend *, qa_error *);
bool frontend_selected_character_rebind_ready(const qa_frontend *, const qa_frontend *, qa_error *);
void frontend_selected_character_rebind(qa_frontend *, qa_frontend *);

#endif
