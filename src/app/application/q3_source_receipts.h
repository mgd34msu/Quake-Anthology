#ifndef QA_APPLICATION_Q3_SOURCE_RECEIPTS_H
#define QA_APPLICATION_Q3_SOURCE_RECEIPTS_H
#include "qa/network_q3.h"
#include "qa/source_save.h"

typedef struct application_q3_snapshot_receipt {
    int32_t message, time;
    qa_actor_id player;
    uint32_t player_number;
    size_t count;
    struct { uint32_t number; qa_actor_id actor; } entities[QA_Q3_SNAPSHOT_ENTITIES];
    bool valid, delivered;
} application_q3_snapshot_receipt;
typedef struct application_q3_source_receipts {
    application_q3_snapshot_receipt *snapshots;
} application_q3_source_receipts;
typedef bool (*application_q3_receipt_actor)(void *, uint32_t, qa_actor_id *, qa_error *);

bool application_q3_source_receipts_prepare(application_q3_source_receipts *, qa_error *);
void application_q3_source_receipts_clear(application_q3_source_receipts *);
void application_q3_source_receipts_destroy(application_q3_source_receipts *);
bool application_q3_source_receipts_capture(application_q3_source_receipts *, const qa_q3_snapshot *,
    application_q3_receipt_actor, void *, qa_error *);
void application_q3_source_receipts_delivered(application_q3_source_receipts *, int32_t);
bool application_q3_source_receipts_read(const application_q3_source_receipts *, uint32_t,
    int32_t time, bool timed, qa_actor_id *);
void application_q3_source_receipts_visit(const application_q3_source_receipts *,
    void (*)(void *, qa_actor_id), void *);
void application_q3_source_receipts_latest(const application_q3_source_receipts *, qa_actor_id [QA_Q3_ENTITIES]);
bool application_q3_source_receipts_legacy(application_q3_source_receipts *, const qa_q3_snapshot *,
    const qa_actor_id [QA_Q3_ENTITIES], qa_error *);
bool application_q3_source_receipts_fields(qa_source_save_io *, application_q3_source_receipts *,
    bool (*)(qa_source_save_io *,qa_actor_id *,bool), bool checkpoint);
#endif
