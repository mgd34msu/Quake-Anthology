#ifndef QA_FRONTEND_NETWORK_SESSION_H
#define QA_FRONTEND_NETWORK_SESSION_H
#include "internal.h"
#include "qa/network_q2_session.h"
typedef struct frontend_remote_unified_domain frontend_remote_unified_domain;

/* A retained initial UI or completed decoded session owns this Draw. The
 * physical ordinal admits its sole presentation pass and actual listener. */
bool frontend_network_client_draw(qa_frontend *,uint32_t physical_seat,uint32_t stereo,
    bool *rendered,qa_audio_listener *,bool *has_listener,qa_error *);
bool frontend_network_q2_configs(qa_frontend *,const qa_q2_config_entry **,size_t *,qa_error *);
bool frontend_network_unified_local_domain(const qa_frontend *, const frontend_remote_unified_domain *);
#endif
