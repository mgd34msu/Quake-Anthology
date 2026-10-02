#ifndef QA_FRONTEND_NETWORK_SESSION_H
#define QA_FRONTEND_NETWORK_SESSION_H
#include "internal.h"

/* A retained initial UI or completed decoded session owns this Draw. The
 * physical ordinal admits its sole presentation pass and actual listener. */
bool frontend_network_client_draw(qa_frontend *,uint32_t physical_seat,uint32_t stereo,
    bool *rendered,qa_audio_listener *,bool *has_listener,qa_error *);
#endif
