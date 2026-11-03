/* Port of src/compat/qvm/cgame-body.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_QVM_CGAME_BODY_H
#define QA_COMPAT_QVM_CGAME_BODY_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/compat/qvm/body_scope.h"
#include "qa/compat/qvm/module.h"
#include "qa/compat/qvm/render_record.h"
#include "qa/compat/qvm/syscalls.h"
#include "qa/contracts/qvm_mod_presentation.h"

typedef struct qa_qvm_body_submission {
  double entry, actor_argument, entity_number_offset;
  bool reference_is_argument; /* false is {kind:"locals"} */
  double reference_index;
  bool has_when;
  double when_argument, when_equals;
  bool has_mesh;
  qa_qvm_scene_body_mesh mesh;
} qa_qvm_body_submission;

typedef struct qa_qvm_body_capture {
  void *ctx;
  bool (*selected)(void *ctx, double entity);
  bool (*submit)(void *ctx, double entity, qa_qvm_body_part part,
                 const qa_q3_pres_source_ref_entity_record *source, bool base);
} qa_qvm_body_capture;

typedef struct qa_qvm_body_hidden_fn {
  void *ctx;
  bool (*call)(void *ctx, double entity);
} qa_qvm_body_hidden_fn;

typedef struct qa_qvm_body_submissions qa_qvm_body_submissions;

/* `image` is the resolved bytecode artifact's image; role/kind are
 * checked by the caller flags (TS artifact.kind/role). NULL only on OOM. */
qa_qvm_body_submissions *qa_qvm_body_submissions_create(
    qa_qvm_module *module, const qa_qvm_image *image, bool artifact_is_bytecode_cgame,
    const qa_qvm_body_submission *declarations, size_t declaration_count,
    qa_qvm_body_hidden_fn hidden, const qa_qvm_body_capture *capture);
void qa_qvm_body_submissions_destroy(qa_qvm_body_submissions *submissions);
bool qa_qvm_body_submissions_get_captures_player_meshes(
    const qa_qvm_body_submissions *submissions);
void qa_qvm_body_submissions_enable(qa_qvm_body_submissions *submissions, bool active);
bool qa_qvm_body_submissions_suppress(qa_qvm_body_submissions *submissions,
                                      const qa_qvm_host_call *call);
void qa_qvm_body_submissions_close(qa_qvm_body_submissions *submissions);

#endif
