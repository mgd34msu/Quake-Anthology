/* Port of src/app/bootstrap/simulation/equipment-checkpoint.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/equipment_checkpoint.h"
#include <stdlib.h>
#include "qa/foundation/support.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/shared.h"
static qa_save_reader field_of(const qa_save_reader *r, const char *name) {
  qa_save_reader out; if (!qa_save_reader_field(r, name, &out)) qa_range_error("Out of memory"); return out;
}
qa_app_sim_hand_grenade_runtime_checkpoint qa_app_sim_read_hand_grenade_runtime_checkpoint(const qa_save_reader *reader) {
  qa_app_sim_hand_grenade_runtime_checkpoint out = {0};
  qa_save_reader source = field_of(reader, "source");
  qa_save_reader random_r = field_of(&source, "random");
  out.random = qa_read_random(&random_r);
  if (out.random.kind != QA_RANDOM_STATE_GLIBC_RANDOM && out.random.kind != QA_RANDOM_STATE_Q2_RERELEASE_MT19937) {
    qa_save_reader kind_r = field_of(&random_r, "kind");
    qa_save_reader_fail(&kind_r, "expected glibc-random or q2-rerelease-mt19937");
  }
  qa_save_reader version_r = field_of(reader, "version");
  out.version = qa_save_reader_literal_number(&version_r, 1); qa_save_reader_destroy(&version_r);
  qa_save_reader controller_r = field_of(reader, "controller");
  out.controller = qa_read_hand_grenades_checkpoint(&controller_r); qa_save_reader_destroy(&controller_r);
  qa_save_reader controls_r = field_of(reader, "controls");
  size_t n = qa_save_reader_list_count(&controls_r);
  out.controls = calloc(n != 0 ? n : 1, sizeof *out.controls); if (out.controls == NULL) qa_range_error("Out of memory");
  out.control_count = n;
  for (size_t i = 0; i < n; i++) {
    qa_save_reader entry; if (!qa_save_reader_item(&controls_r, i, &entry)) qa_range_error("Out of memory");
    qa_save_reader af = field_of(&entry, "actor"); out.controls[i].actor = qa_read_saved_actor(&af); qa_save_reader_destroy(&af);
    qa_save_reader hf = field_of(&entry, "held"); out.controls[i].held = qa_save_reader_boolean(&hf); qa_save_reader_destroy(&hf);
    qa_save_reader pf = field_of(&entry, "pressed"); out.controls[i].pressed = qa_save_reader_boolean(&pf); qa_save_reader_destroy(&pf);
    qa_save_reader rf = field_of(&entry, "released"); out.controls[i].released = qa_save_reader_boolean(&rf); qa_save_reader_destroy(&rf);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&controls_r);
  qa_save_reader entities_r = field_of(&source, "entities");
  out.entities = qa_read_q2_foundation_checkpoint(&entities_r); qa_save_reader_destroy(&entities_r);
  qa_save_reader_destroy(&random_r); qa_save_reader_destroy(&source);
  return out;
}
