#ifndef QA_FRONTEND_REMOTE_Q1_HUD_H
#define QA_FRONTEND_REMOTE_Q1_HUD_H
#include "remote_q1_client.h"
#include "qa/hud.h"
typedef struct frontend_remote_q1_hud_storage frontend_remote_q1_hud_storage;
bool frontend_remote_q1_hud_read(frontend_remote_q1 *,const qa_hud_frame *,qa_hud_data *,qa_error *);
void remote_q1_hud_clear(frontend_remote_q1 *);
#endif
