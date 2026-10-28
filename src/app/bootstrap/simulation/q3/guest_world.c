/* Port of src/app/bootstrap/simulation/q3/guest-world.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/q3/guest_world.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/world/collision/q3/clip_models.h"
#include "qa/world/collision/q3/world.h"

typedef struct reference { double first, second, count; } reference;
struct qa_app_q3_guest_world {
  qa_shared_scene_queries *scene;
  reference references[256];
  size_t reference_count;
};
static const char *geometry_kind(qa_decoded_world_kind kind) {
  return kind == QA_DECODED_WORLD_Q1 ? "q1-bsp" : kind == QA_DECODED_WORLD_Q2 ? "q2-bsp" : "q3-bsp";
}
static reference *find_reference(qa_app_q3_guest_world *world, double first, double second) {
  for (size_t i = 0; i < world->reference_count; i++)
    if (world->references[i].first == first && world->references[i].second == second) return &world->references[i];
  return NULL;
}
static void set_reference(qa_app_q3_guest_world *world, double first, double second, double count) {
  reference *entry = find_reference(world, first, second);
  if (!entry) {
    if (world->reference_count >= 256) qa_range_error("Out of memory");
    entry = &world->references[world->reference_count++];
    entry->first = first; entry->second = second;
  }
  entry->count = count;
}
qa_app_q3_guest_world *qa_app_q3_guest_world_create(qa_shared_scene_queries *scene) {
  qa_app_q3_guest_world *world = calloc(1, sizeof *world);
  if (!world) return NULL;
  world->scene = scene;
  return world;
}
void qa_app_q3_guest_world_destroy(qa_app_q3_guest_world *world) { free(world); }
qa_shared_scene_queries *qa_app_q3_guest_world_scene(qa_app_q3_guest_world *world) { return world->scene; }

void qa_app_q3_guest_world_adjust_area_portal_state(qa_app_q3_guest_world *world, double first, double second, bool open) {
  if (first < 0 || second < 0 || first == second) return;
  qa_decoded_world geometry = world->scene->geometry;
  if (geometry.kind == QA_DECODED_WORLD_Q1) return;
  double lo = first < second ? first : second, hi = first < second ? second : first;
  reference *existing = find_reference(world, lo, hi);
  double previous = existing ? existing->count : 0;
  double count = previous + (open ? 1 : -1);
  if (count < 0) qa_range_error("Guest area portal reference underflow");
  if (geometry.kind == QA_DECODED_WORLD_Q3) {
    qa_shared_adjust_area_portal_pair(world->scene, first, second, open);
    set_reference(world, lo, hi, count);
    return;
  }
  const qa_q2_world_geometry *q2 = geometry.as.q2;
  if (!q2 || (size_t)first >= q2->area_count || (size_t)second >= q2->area_count)
    qa_range_error("Guest area pair is outside the selected map");
  const qa_q2_area *area = &q2->areas[(size_t)first];
  set_reference(world, lo, hi, count);
  for (double index = area->portals.first; index < area->portals.first + area->portals.count; index++) {
    if ((size_t)index >= q2->area_portal_count) continue;
    const qa_q2_area_portal *portal = &q2->area_portals[(size_t)index];
    if (portal->other_area == second)
      qa_shared_adjust_area_portal_contribution(world->scene, portal->portal, open ? 1 : -1);
  }
}
qa_save_value *qa_app_q3_guest_world_capture_portal_checkpoint(qa_app_q3_guest_world *world) {
  qa_source_clip_models *native = qa_shared_native_q3_clip_models(world->scene);
  if (native) return qa_collision_world_capture_portal_checkpoint(native->world);
  qa_save_value *root = qa_save_value_object(), *list = qa_save_value_array();
  if (!root || !list) { qa_save_value_free(root); qa_save_value_free(list); return NULL; }
  bool ok = qa_save_value_set(root, "kind", qa_save_value_string(geometry_kind(world->scene->geometry.kind)));
  for (size_t i = 0; ok && i < world->reference_count; i++) {
    char pair[64];
    snprintf(pair, sizeof pair, "%lld:%lld", (long long)world->references[i].first, (long long)world->references[i].second);
    qa_save_value *entry = qa_save_value_object();
    ok = entry && qa_save_value_set(entry, "pair", qa_save_value_string(pair))
        && qa_save_value_set(entry, "count", qa_save_value_number(world->references[i].count))
        && qa_save_value_push(list, entry);
    if (!ok) qa_save_value_free(entry);
  }
  ok = ok && qa_save_value_set(root, "references", list);
  if (!ok) { qa_save_value_free(root); return NULL; }
  return root;
}
void qa_app_q3_guest_world_close(qa_app_q3_guest_world *world) {
  size_t total = world->reference_count;
  reference copy[256];
  memcpy(copy, world->references, total * sizeof *copy);
  for (size_t i = 0; i < total; i++)
    for (double index = 0; index < copy[i].count; index++)
      qa_app_q3_guest_world_adjust_area_portal_state(world, copy[i].first, copy[i].second, false);
  world->reference_count = 0;
}
static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out)) qa_range_error("Out of memory");
  return out;
}
void qa_app_q3_guest_world_restore_portal_checkpoint(qa_app_q3_guest_world *world, const qa_save_value *value) {
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "q3.guest.foreign-portals")) qa_range_error("Out of memory");
  qa_source_clip_models *native = qa_shared_native_q3_clip_models(world->scene);
  qa_save_reader kind = field(&reader, "kind");
  bool has_kind = !qa_save_reader_is_undefined(&kind);
  qa_save_reader_destroy(&kind);
  if (!has_kind && native) {
    qa_app_q3_guest_world_close(world);
    qa_collision_world_restore_portal_checkpoint(native->world, value);
    qa_save_reader areas = field(&reader, "areas");
    size_t area_count = qa_save_reader_list_count(&areas);
    qa_save_reader_destroy(&areas);
    qa_save_reader portals = field(&reader, "portals");
    size_t portal_count = qa_save_reader_list_count(&portals);
    double *values = calloc(portal_count ? portal_count : 1, sizeof *values);
    if (!values) qa_range_error("Out of memory");
    for (size_t i = 0; i < portal_count; i++) {
      qa_save_reader item;
      if (!qa_save_reader_item(&portals, i, &item)) qa_range_error("Out of memory");
      values[i] = qa_save_reader_integer_min(&item, 0);
      qa_save_reader_destroy(&item);
    }
    qa_save_reader_destroy(&portals);
    for (size_t first = 0; first < area_count; first++) {
      for (size_t second = first + 1; second < area_count; second++) {
        size_t index = first * area_count + second;
        double count = index < portal_count ? values[index] : 0;
        if (count > 0) set_reference(world, (double)first, (double)second, count);
      }
    }
    free(values);
    qa_save_reader_destroy(&reader);
    return;
  }
  kind = field(&reader, "kind");
  qa_save_reader_literal_string(&kind, geometry_kind(world->scene->geometry.kind));
  qa_save_reader_destroy(&kind);
  typedef struct saved_pair { double first, second, count; char text[64]; } saved_pair;
  saved_pair entries[256]; size_t entry_count = 0;
  qa_save_reader references = field(&reader, "references");
  size_t count = qa_save_reader_list_count(&references);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader cell;
    if (!qa_save_reader_item(&references, i, &cell)) qa_range_error("Out of memory");
    qa_save_reader pair_reader = field(&cell, "pair");
    const char *pair = qa_save_reader_string(&pair_reader);
    qa_save_reader count_reader = field(&cell, "count");
    double amount = qa_save_reader_integer_min(&count_reader, 0);
    qa_save_reader_destroy(&count_reader);
    long long first = 0, second = 0;
    char trailing = 0;
    bool valid = pair && sscanf(pair, "%lld:%lld%c", &first, &second, &trailing) == 2 && first >= 0 && second >= 0;
    for (size_t j = 0; valid && j < entry_count; j++) if (strcmp(entries[j].text, pair) == 0) valid = false;
    if (!valid) qa_range_error("Invalid guest portal reference");
    if (first >= second) qa_range_error("Invalid guest portal pair");
    qa_decoded_world geometry = world->scene->geometry;
    if (geometry.kind == QA_DECODED_WORLD_Q1
        || (geometry.kind == QA_DECODED_WORLD_Q2 && geometry.as.q2
            && ((size_t)first >= geometry.as.q2->area_count || (size_t)second >= geometry.as.q2->area_count)))
      qa_range_error("Guest portal checkpoint belongs to another area table");
    entries[entry_count].first = (double)first;
    entries[entry_count].second = (double)second;
    entries[entry_count].count = amount;
    snprintf(entries[entry_count].text, 64, "%s", pair);
    entry_count++;
    qa_save_reader_destroy(&pair_reader);
    qa_save_reader_destroy(&cell);
  }
  qa_save_reader_destroy(&references);
  qa_save_reader_destroy(&reader);
  qa_app_q3_guest_world_close(world);
  for (size_t i = 0; i < entry_count; i++) {
    for (double index = 0; index < entries[i].count; index++)
      qa_app_q3_guest_world_adjust_area_portal_state(world, entries[i].first, entries[i].second, true);
    if (entries[i].count == 0) set_reference(world, entries[i].first, entries[i].second, 0);
  }
}
