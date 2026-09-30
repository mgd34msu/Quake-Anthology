#ifndef QA_BOT_NAVIGATION_SOURCE_H
#define QA_BOT_NAVIGATION_SOURCE_H
#include "qa/bot_navigation.h"
#include "qa/bot_source.h"

/* Navigation selection and first-trace exclusion are independent. Module
 * callers supply base navigation plus the canonical passed actor. */
bool qa_bot_navigation_reachable_from(qa_bot_navigation *, const qa_bot_vector_source *,
                                      qa_actor_id pass, uint32_t *, qa_error *);
bool qa_bot_navigation_fuzzy_from(qa_bot_navigation *, const qa_bot_vector_source *,
                                  uint32_t *, qa_error *);
typedef struct qa_bot_route_source_prediction {
    qa_bot_route_prediction value;
    /* When true, publication reads the original origin, component by component,
     * after admitting its output record. value.end_position is then unused. */
    bool end_is_origin;
} qa_bot_route_source_prediction;
/* query.route.origin is supplied by the reader instead. */
bool qa_bot_navigation_predict_route_from(qa_bot_navigation *,
                                          const qa_bot_route_prediction_query *,
                                          const qa_bot_vector_source *,
                                          qa_bot_route_source_prediction *, qa_error *);
#endif
