#ifndef QA_APPLICATION_Q1_SAVE_H
#define QA_APPLICATION_Q1_SAVE_H
#include "qa/application.h"
#include "qa/q1_save.h"
/* Actual primary singleplayer NetQuake source, one fully admitted local human
 * in physical client row1, completed source/canonical callback boundary. */
bool qa_application_q1_save_capture(qa_application *,qa_q1_save_data **,qa_error *);
/* Pure shared header/product/capacity admission against the actual application
 * catalog and actor owner. No VM construction, source callback or file access.
 * The fresh importer uses these same guards before preparing any world. */
bool qa_application_q1_save_import_ready(const qa_application *,const qa_q1_save_data *,
    const char *product,qa_error *);
/* Construct only in a fresh isolated application. Product names an installed
 * actual catalog owner selected by qa_q1_save_select_product. This stages the
 * saved source epoch and an exact owned save copy before normal progs/map/player
 * preparation. Success can leave genuine preInit script waits pending. The
 * isolated driver must advance below before any gameplay frame or capture.
 * A failure leaves the candidate for ordinary destruction. */
bool qa_application_q1_save_import(qa_application *,const qa_q1_save_data *,
    const char *product,qa_error *);
/* Advance once at a real driver boundary. After actual startup completes,
 * applies raw globals/edict slots and body/header owners exactly once, without
 * spawning from saved records. No loop, file reread or input save borrow. */
bool qa_application_q1_save_import_advance(qa_application *,bool *complete,qa_error *);
#endif
