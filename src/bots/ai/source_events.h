#ifndef QA_BOT_SOURCE_EVENTS_H
#define QA_BOT_SOURCE_EVENTS_H

#include "qa/network_q3.h"
#include "qa/math.h"

enum { BOT_SOURCE_EVENT_ENTITIES=1024, BOT_SOURCE_PROX_MINES=64 };
typedef struct bot_source_events_globals {
    qa_vec3 last_teleport_origin;
    float last_teleport_time;
} bot_source_events_globals;

struct qa_bots;
struct bot_ai_state;
/* These consume current GAME rows through genuine source snapshot indices.
 * The PS sample was retained before console intake by the ordinary AI caller. */
bool bot_ai_source_check_snapshot(struct qa_bots *,struct bot_ai_state *,qa_error *);
bool bot_ai_source_check_event(struct qa_bots *,struct bot_ai_state *,const qa_q3_entity *,qa_error *);
bool bot_ai_source_current_entity(struct qa_bots *,int32_t,qa_q3_entity *,qa_error *);
bool bot_ai_source_set_teleport_time(struct qa_bots *,struct bot_ai_state *,qa_error *);

#endif
