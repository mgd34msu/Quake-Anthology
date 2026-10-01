#ifndef QA_APPLICATION_Q1_SAVE_H
#define QA_APPLICATION_Q1_SAVE_H
#include "qa/application.h"
#include "qa/q1_save.h"
/* Actual primary singleplayer NetQuake source, one fully admitted local human
 * in physical client row1, completed source/canonical callback boundary. */
bool qa_application_q1_save_capture(qa_application *,uint32_t version,
    const char *comment,qa_q1_save_data **,qa_error *);
/* Construct only in a fresh isolated application. Product names an installed
 * actual catalog owner selected by qa_q1_save_select_product. This stages the
 * saved source epoch before normal progs/map/player construction, then applies
 * raw globals/edict slots and real body/header owners without source spawning
 * from saved records. A failure leaves the candidate for ordinary destruction. */
bool qa_application_q1_save_import(qa_application *,const qa_q1_save_data *,
    const char *product,qa_error *);
#endif
