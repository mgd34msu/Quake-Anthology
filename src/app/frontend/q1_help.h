#ifndef QA_FRONTEND_Q1_HELP_H
#define QA_FRONTEND_Q1_HELP_H
#include "internal.h"

typedef struct frontend_q1_help frontend_q1_help;
bool frontend_q1_help_create(frontend_seat *,qa_error *);
bool frontend_q1_help_destroy(frontend_seat *,qa_error *);
bool frontend_q1_help_open(frontend_seat *,qa_error *);
void frontend_q1_help_bind_source(frontend_seat *,qa_scene_resources *);
void frontend_q1_help_forget_source(frontend_seat *,const qa_scene_resources *);
#endif
