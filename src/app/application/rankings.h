#ifndef QA_APPLICATION_RANKINGS_PRIVATE_H
#define QA_APPLICATION_RANKINGS_PRIVATE_H

#include "internal.h"
#include "qa/application_rankings.h"
#include "qa/persistence_application.h"

typedef struct application_rankings application_rankings;
typedef enum application_ranking_source_effect {
    APPLICATION_RANKING_SPECTATOR,
    APPLICATION_RANKING_ACTIVATE,
    APPLICATION_RANKING_SCOREBOARD,
    APPLICATION_RANKING_DROP_BOT
} application_ranking_source_effect;
/* These effects are implemented by the authoritative player/source owner.
 * They use ordinary source spawn/team/scoreboard/retirement paths. */
bool application_rankings_source_effect(qa_application *, application_provider *,
    qa_actor_id, application_ranking_source_effect, qa_error *);

/* Initial map publication and each ordinary source frame call frame after
 * players and source output are published. The real fast restart closes the
 * old match before source retirement and begins again only after settlement. */
bool application_rankings_frame(qa_application *, qa_error *);
bool application_rankings_frame_ordinary(qa_application *, qa_error *);
bool application_rankings_round_close(qa_application *, application_provider *, qa_error *);
bool application_rankings_close(qa_application *, qa_error *);
bool application_rankings_disconnect(qa_application *, qa_actor_id, qa_error *);
bool application_rankings_report(void *, const qa_ranking_source_report *, qa_error *);
bool application_rankings_warmup(void *);
bool application_rankings_idle(const qa_application *);
/* Private cleanup never closes the shared rankings backend. Ordinary map and
 * round close must end that backend before disposing this source state. */
void application_rankings_dispose(qa_application *);
bool application_rankings_prepare(const qa_application_options *, const qa_application_persistence_ops *,
    qa_bytes, qa_error *);
bool application_rankings_capture(qa_application *, const qa_application_persistence_ops *,
    qa_buffer *empty, qa_error *);
bool application_rankings_restore(qa_application *, const qa_application_persistence_ops *,
    qa_bytes, qa_error *);
bool application_rankings_restore_ready(qa_application *, qa_error *);
void application_rankings_publish_restored(qa_application *);
void application_rankings_relinquish(qa_application *);

#endif
