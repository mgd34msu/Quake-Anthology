/* Port of src/app/bootstrap/selected-q3-presentation.ts: one original
 * selected-source cgame scene per viewing seat, without primary view, body,
 * weapon, input or HUD ownership (ApplicationSelectedQ3Presentation), and
 * the per-seat collection (ApplicationSelectedQ3Presentations).
 * Async is synchronous; errors raise through qa_raise with the TS class and
 * message. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SELECTED_Q3_PRESENTATION_H
#define QA_APP_BOOTSTRAP_SELECTED_Q3_PRESENTATION_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/mod_presentations.h"
#include "qa/app/bootstrap/presentation.h"
#include "qa/app/bootstrap/simulation/arsenal/q3_source.h"
#include "qa/app/bootstrap/simulation/q3/presentation.h"
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/contracts/content.h"

/* TS SelectedQ3Presentation { content, source, state }. Borrowed. */
typedef struct qa_app_selected_q3_source {
  qa_content_id content;
  qa_app_q3_selected_source *source;
  const qa_app_q3_source_presentation_state *state;
} qa_app_selected_q3_source;

/* TS SelectedQ3PresentationOptions: the ApplicationModPresentationsOptions
 * plus hardware() (true is "ragepro", false "generic"), called with base.ctx. */
typedef struct qa_app_selected_q3_presentation_options {
  qa_app_mod_presentations_options base;
  bool (*hardware_ragepro)(void *ctx);
} qa_app_selected_q3_presentation_options;

typedef struct qa_app_selected_q3_presentation qa_app_selected_q3_presentation;
/* TS ApplicationSelectedQ3Presentation.create. options, source and seat are
 * borrowed and must outlive the result. */
qa_app_selected_q3_presentation *qa_app_selected_q3_presentation_create(
    const qa_app_selected_q3_presentation_options *options, qa_app_selected_q3_source source,
    const qa_app_viewing_seat *presentation);
/* Closes (TS close), then frees; a close failure raises after the free. */
void qa_app_selected_q3_presentation_destroy(qa_app_selected_q3_presentation *presentation);
bool qa_app_selected_q3_presentation_owns(const qa_app_selected_q3_presentation *presentation,
    const qa_app_selected_q3_source *source, const qa_app_viewing_seat *seat);
void qa_app_selected_q3_presentation_prepare(qa_app_selected_q3_presentation *presentation,
    const qa_app_selected_q3_source *source, const qa_app_sim_presentation_event *events, size_t event_count,
    double frame);
void qa_app_selected_q3_presentation_close(qa_app_selected_q3_presentation *presentation);

typedef struct qa_app_selected_q3_presentations qa_app_selected_q3_presentations;
/* options is copied; its pointers are borrowed. */
qa_app_selected_q3_presentations *qa_app_selected_q3_presentations_create(
    const qa_app_selected_q3_presentation_options *options);
/* Closes, then frees; a close failure raises after the free. */
void qa_app_selected_q3_presentations_destroy(qa_app_selected_q3_presentations *presentations);
/* Seats are identified by their ctx. */
void qa_app_selected_q3_presentations_retain(qa_app_selected_q3_presentations *presentations,
    const qa_app_viewing_seat *seats, size_t seat_count);
/* source NULL is TS null. */
void qa_app_selected_q3_presentations_prepare(qa_app_selected_q3_presentations *presentations,
    const qa_app_viewing_seat *seats, size_t seat_count, const qa_app_selected_q3_source *source,
    const qa_app_sim_presentation_event *events, size_t event_count, double frame);
void qa_app_selected_q3_presentations_close(qa_app_selected_q3_presentations *presentations);
#endif
