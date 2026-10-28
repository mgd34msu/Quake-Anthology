/* Port of src/app/bootstrap/simulation/bot-knowledge-checkpoint.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/bot_knowledge_checkpoint.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/persistence/save_image.h"
struct qa_app_sim_bot_knowledge_persistence {
  const char *source; qa_app_sim_bot_knowledge_entry *entries; size_t count, cap;
};
qa_app_sim_bot_knowledge_persistence *qa_app_sim_bot_knowledge_persistence_create(const char *source,
    qa_app_sim_bot_knowledge_entry *entries, size_t entry_count) {
  qa_app_sim_bot_knowledge_persistence *p = calloc(1, sizeof *p);
  if (p == NULL) qa_range_error("Out of memory");
  p->source = source; p->entries = entries; p->count = entry_count; p->cap = entry_count;
  return p;
}
void qa_app_sim_bot_knowledge_persistence_destroy(qa_app_sim_bot_knowledge_persistence *p) { if (p != NULL) { free(p->entries); free(p); } }
qa_app_sim_bot_knowledge_checkpoint qa_app_sim_bot_knowledge_checkpoint_of(qa_app_sim_bot_knowledge_persistence *p) {
  qa_app_sim_bot_knowledge_checkpoint out = {0};
  out.version = 1; out.source = p->source; out.actor_count = p->count;
  out.actors = calloc(p->count != 0 ? p->count : 1, sizeof *out.actors);
  if (out.actors == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < p->count; i++) { out.actors[i].handle = p->entries[i].handle; out.actors[i].actor = qa_saved_actor_id_of(p->entries[i].actor); }
  return out;
}
void qa_app_sim_bot_knowledge_checkpoint_destroy(qa_app_sim_bot_knowledge_checkpoint *c) { free(c->actors); c->actors = NULL; c->actor_count = 0; }
size_t qa_app_sim_bot_knowledge_entry_count(const qa_app_sim_bot_knowledge_persistence *p) { return p->count; }
const qa_app_sim_bot_knowledge_entry *qa_app_sim_bot_knowledge_entries(const qa_app_sim_bot_knowledge_persistence *p) { return p->entries; }
void qa_app_sim_bot_knowledge_restore(qa_app_sim_bot_knowledge_persistence *p, const qa_save_value *value,
    qa_app_sim_saved_actor_fn actor_fn, void *actor_ctx, qa_app_sim_weapon_handle_fn weapon_fn, void *weapon_ctx) {
  qa_save_reader reader; if (!qa_save_reader_init(&reader, value, "botKnowledge")) qa_range_error("Out of memory");
  qa_save_reader f;
  if (!qa_save_reader_field(&reader, "version", &f)) qa_range_error("Out of memory");
  qa_save_reader_literal_number(&f, 1); qa_save_reader_destroy(&f);
  if (!qa_save_reader_field(&reader, "source", &f)) qa_range_error("Out of memory");
  qa_save_reader_literal_string(&f, p->source); qa_save_reader_destroy(&f);
  qa_save_reader actors_r; if (!qa_save_reader_field(&reader, "actors", &actors_r)) qa_range_error("Out of memory");
  size_t n = qa_save_reader_list_count(&actors_r);
  qa_app_sim_bot_knowledge_entry *restored = calloc(n != 0 ? n : 1, sizeof *restored);
  double *seen = calloc(n != 0 ? n : 1, sizeof *seen);
  if (restored == NULL || seen == NULL) qa_range_error("Out of memory");
  size_t rn = 0;
  for (size_t i = 0; i < n; i++) {
    qa_save_reader entry; if (!qa_save_reader_item(&actors_r, i, &entry)) qa_range_error("Out of memory");
    qa_save_reader hf; if (!qa_save_reader_field(&entry, "handle", &hf)) qa_range_error("Out of memory");
    double saved = qa_save_reader_integer_min(&hf, 0); qa_save_reader_destroy(&hf);
    double handle = weapon_fn(weapon_ctx, saved);
    bool dup_seen = false, dup_handle = false;
    for (size_t j = 0; j < rn; j++) { if (seen[j] == saved) dup_seen = true; if (restored[j].handle == handle) dup_handle = true; }
    if (dup_seen || !(handle == trunc(handle) && fabs(handle) <= 9007199254740991.0) || handle < 0 || dup_handle)
      qa_save_reader_fail(&entry, "invalid or duplicate weapon handle mapping");
    qa_save_reader af; if (!qa_save_reader_field(&entry, "actor", &af)) qa_range_error("Out of memory");
    qa_saved_actor_id sv = qa_read_saved_actor(&af); qa_save_reader_destroy(&af);
    seen[rn] = saved; restored[rn].handle = handle; restored[rn].actor = actor_fn(actor_ctx, sv); rn++;
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&actors_r); qa_save_reader_destroy(&reader);
  free(seen); free(p->entries); p->entries = restored; p->count = rn; p->cap = rn;
}
