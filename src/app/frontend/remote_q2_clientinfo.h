#ifndef QA_FRONTEND_REMOTE_Q2_CLIENTINFO_H
#define QA_FRONTEND_REMOTE_Q2_CLIENTINFO_H
#include "remote_q2_client.h"
#include "qa/material.h"
#include "qa/player_names.h"
struct remote_q2_model;
typedef struct remote_q2_clientinfo {
    const char *model;
    const char *weapon;
    qa_q2_player_media media;
    struct remote_q2_model *model_resource, *weapon_resource;
    const qa_material *skin_material;
    bool valid;
} remote_q2_clientinfo;
bool remote_q2_clientinfo_read(frontend_remote_q2 *, uint32_t player, uint32_t weapon,
    remote_q2_clientinfo *, qa_error *);
bool remote_q2_clientinfo_prepare(frontend_remote_q2 *, qa_error *);
bool remote_q2_clientinfo_config(frontend_remote_q2 *, uint32_t, bool weapon_changed, qa_error *);
bool remote_q2_clientinfo_settings(frontend_remote_q2 *, qa_error *);
#endif
