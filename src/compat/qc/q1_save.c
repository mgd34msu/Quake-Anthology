/* Port of src/compat/qc/q1-save.ts (moved from src/persistence/q1-quakec.ts). GPL-2.0-or-later. */
#include "qa/compat/qc/q1_save.h"

#include <stdlib.h>
#include <string.h>

#include "qa/persistence/value.h"

static void record_clear(qa_q1_text_record *record) {
  for (size_t i = 0; i < record->pair_count; i++) {
    free(record->pairs[i].key);
    free(record->pairs[i].value);
  }
  free(record->pairs);
  record->pairs = NULL;
  record->pair_count = 0;
}

void qa_q1_unknown_save_fields_free(qa_q1_unknown_save_fields *fields) {
  if (fields == NULL) return;
  record_clear(&fields->globals);
  for (size_t i = 0; i < fields->entity_count; i++)
    record_clear(&fields->entities[i].fields);
  free(fields->entities);
  memset(fields, 0, sizeof *fields);
}

bool qa_capture_q1_quakec_save(qa_qc_machine *machine, qa_q1_save_header header,
                               qa_q1_quakec_is_free is_free,
                               qa_q1_save_data *out) {
  memset(out, 0, sizeof *out);
  /* The VM checks both interpreted and host-builtin reentry, which
   * depth alone cannot do. */
  machine->snapshot(machine->ctx);
  out->format_version = header.format_version;
  out->game_directories = header.game_directories;
  out->comment = header.comment;
  out->spawn_parameters = header.spawn_parameters;
  out->spawn_parameter_count = header.spawn_parameter_count;
  out->skill = header.skill;
  out->map = header.map;
  out->time = header.time;
  out->light_styles = header.light_styles;
  out->light_style_count = header.light_style_count;
  out->extension_text = header.extension_text;
  out->extension_length = header.extension_length;
  qa_q1_text_record globals = machine->save_global_pairs(machine->ctx);
  out->globals = globals.pairs;
  out->global_count = globals.pair_count;
  size_t count = machine->entity_count(machine->ctx);
  if (count > 0) {
    out->entities = calloc(count, sizeof *out->entities);
    if (out->entities == NULL) {
      /* The header stays with the caller on failure: release only
       * what capture itself allocated. */
      record_clear(&globals);
      out->globals = NULL;
      out->global_count = 0;
      out->game_directories = NULL;
      out->comment = NULL;
      out->spawn_parameters = NULL;
      out->spawn_parameter_count = 0;
      out->map = NULL;
      out->light_styles = NULL;
      out->light_style_count = 0;
      out->extension_text = NULL;
      out->extension_length = 0;
      return false;
    }
  }
  out->entity_count = count;
  for (size_t slot = 0; slot < count; slot++)
    out->entities[slot] = machine->save_entity_pairs(
        machine->ctx, slot, is_free.fn(is_free.ctx, slot));
  return true;
}

bool qa_restore_q1_quakec_save(qa_qc_machine *machine,
                               const qa_q1_save_data *save,
                               qa_q1_quakec_save_host host,
                               qa_q1_unknown_save_fields *out) {
  memset(out, 0, sizeof *out);
  machine->snapshot(machine->ctx);
  if (save->entity_count < 1 ||
      save->entity_count > machine->entity_capacity(machine->ctx))
    qa_save_format_error("q1.entities",
                         "saved edict count exceeds the selected guest "
                         "capacity");
  host.begin(host.ctx, save->entity_count);
  machine->set_entity_count(machine->ctx, save->entity_count);
  /* Saved entity references may point forward, so install the
   * complete source count first. */
  qa_q1_text_record saved_globals = {save->globals, save->global_count};
  out->globals = machine->apply_global_pairs(machine->ctx, &saved_globals);
  for (size_t slot = 0; slot < save->entity_count; slot++) {
    size_t length = 0;
    uint8_t *bytes = machine->entity_bytes(machine->ctx, slot, &length);
    memset(bytes, 0, length);
    qa_qc_entity_parse_result parsed =
        machine->apply_entity_pairs(machine->ctx, slot, &save->entities[slot]);
    if (parsed.unknown.pair_count != 0) {
      qa_q1_unknown_entity_fields *grown = realloc(
          out->entities, (out->entity_count + 1) * sizeof *out->entities);
      if (grown == NULL) {
        record_clear(&parsed.unknown);
        record_clear(&parsed.retained);
        qa_q1_unknown_save_fields_free(out);
        return false;
      }
      out->entities = grown;
      out->entities[out->entity_count].slot = slot;
      out->entities[out->entity_count].fields = parsed.unknown;
      out->entity_count++;
    }
    record_clear(&parsed.retained);
    host.entity(host.ctx, slot, parsed.empty);
  }
  qa_q1_save_header header;
  header.format_version = save->format_version;
  header.game_directories = save->game_directories;
  header.comment = save->comment;
  header.spawn_parameters = save->spawn_parameters;
  header.spawn_parameter_count = save->spawn_parameter_count;
  header.skill = save->skill;
  header.map = save->map;
  header.time = save->time;
  header.light_styles = save->light_styles;
  header.light_style_count = save->light_style_count;
  header.extension_text = save->extension_text;
  header.extension_length = save->extension_length;
  /* Keep the header separate from source record arrays for
   * host-owned restoration. */
  host.finish(host.ctx, header);
  return true;
}
