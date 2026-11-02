/* Port of src/app/bootstrap/simulation/q3/guest-bots.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/q3/guest_bots.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/qvm/entity_record.h"
#include "qa/content/q3/base/shared/entity_shared.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/persistence/shared.h"

typedef struct recorded_entity {
  double slot;
  qa_app_q3_bot_entity_snapshot snapshot;
  double interval, updated, visible;
  qa_vec3 seen;
} recorded_entity;
struct qa_app_q3_guest_bots {
  qa_app_q3_guest_bot_options options;
  bool closed, map_loaded, traced;
  double last_time;
  char map_name[128];
  bool has_map_name;
  recorded_entity entities[1024];
  size_t entity_count;
  int32_t epair;
  /* predictive pass scratch */
  double visible_enemies, weapon;
  int has_weapon;
};

static recorded_entity *find_record(qa_app_q3_guest_bots *bots, double slot) {
  for (size_t i = 0; i < bots->entity_count; i++) if (bots->entities[i].slot == slot) return &bots->entities[i];
  return NULL;
}
static int32_t word(const qa_qvm_host_call *call, size_t index) { return qa_qvm_host_call_word(call, index); }
static void initialized(qa_app_q3_guest_bots *bots) {
  if (!bots->options.library.is_initialized(bots->options.library.ctx))
    qa_range_error("bot library used before being setup");
}
static bool navigation_ready(qa_app_q3_guest_bots *bots) {
  qa_source_bot_navigation_bots_a *navigation = bots->options.library.source_navigation(bots->options.library.ctx);
  return navigation && navigation->ready;
}
static qa_source_bot_navigation_bots_a *navigation(qa_app_q3_guest_bots *bots) {
  if (!navigation_ready(bots)) qa_range_error("Source bot navigation is unavailable");
  return bots->options.library.source_navigation(bots->options.library.ctx);
}

qa_app_q3_guest_bots *qa_app_q3_guest_bots_create(qa_app_q3_guest_bot_options options) {
  qa_app_q3_guest_bots *bots = calloc(1, sizeof *bots);
  if (!bots) return NULL;
  bots->options = options;
  return bots;
}
void qa_app_q3_guest_bots_destroy(qa_app_q3_guest_bots *bots) { free(bots); }
void qa_app_q3_guest_bots_close(qa_app_q3_guest_bots *bots) {
  if (bots->closed) return;
  bots->options.library.close(bots->options.library.ctx);
  bots->closed = true;
  bots->entity_count = 0;
}

bool qa_app_q3_guest_bots_syscall(qa_app_q3_guest_bots *bots, const qa_qvm_host_call *call, qa_qvm_host_result *out) {
  if (!call->kind || strcmp(call->kind, "engine") != 0 || !call->role || strcmp(call->role, "qagame") != 0) return false;
  if (bots->closed) qa_range_error("Q3 guest bot services are retired");
  qa_app_q3_bot_library *library = &bots->options.library;
  void *ctx = bots->options.host_ctx;
  switch (call->code) {
    case QA_QVM_GAME_IMPORT_BOTLIB_SETUP: *out = (int32_t)library->setup(library->ctx); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_SHUTDOWN: *out = (int32_t)library->shutdown(library->ctx); return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_START_FRAME: {
      double time = bots->options.word_float(ctx, call, 0);
      *out = (int32_t)library->start_frame(library->ctx, (float)time);
      bots->last_time = time; bots->traced = false;
      return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_LOAD_MAP: {
      const char *name = bots->options.required_string(ctx, call, 0, "map name");
      if (bots->map_loaded) {
        if (bots->has_map_name && strcmp(bots->map_name, name) == 0) {
          if (!library->is_initialized(library->ctx)) qa_range_error("bot library used before being setup");
          *out = 0; return true;
        }
        qa_range_error("Q3 bot map cannot change inside one guest game");
      }
      double result = library->load_map(library->ctx, name);
      bots->map_loaded = true; bots->has_map_name = true;
      strncpy(bots->map_name, name, 127);
      *out = (int32_t)result; return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_UPDATENTITY: {
      initialized(bots);
      qa_qvm_bot_entity_update state;
      bots->options.structure(ctx, call, 1, QA_QVM_BOT_ENTITY_STATE_BYTES, &state, "bot entity state");
      double entity = word(call, 0);
      qa_qvm_shared_entity *shared = qa_app_q3_guest_records_entity(bots->options.records, entity);
      qa_qvmx_view view = shared->s.view;
      qa_qvm_entity_state record_state = qa_qvm_borrow_entity_state(view, shared->profile);
      qa_app_q3_bot_entity_snapshot snapshot; memset(&snapshot, 0, sizeof snapshot);
      snapshot.state = state; snapshot.slot = entity;
      snapshot.other_entity = qa_qvm_entity_state_get_other_entity_num(&record_state);
      snapshot.frame = record_state.source ? 0 : 0;
      snapshot.time = bots->last_time;
      library->update_entity(library->ctx, entity, snapshot);
      recorded_entity *record = find_record(bots, entity);
      if (!record && bots->entity_count < 1024) { record = &bots->entities[bots->entity_count++]; memset(record, 0, sizeof *record); record->slot = entity; }
      if (record) {
        record->snapshot = snapshot;
        double interval = state.type == 1 ? 0.125 : state.type == 3 ? 0.5 : 0.25;
        record->interval = interval; record->updated = bots->last_time;
        record->visible = bots->last_time; record->seen = state.origin;
      }
      *out = 0; return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_TEST: {
      qa_vec3 origin = {0, 0, 0};
      if (qa_qvm_game_data_get_num_entities(qa_app_q3_guest_records_data(bots->options.records)) > 0) {
        qa_qvm_shared_entity *entity = qa_app_q3_guest_records_entity(bots->options.records, 0);
        origin = qa_qvm_shared_entity_get_current_origin(entity);
      }
      *out = (int32_t)qa_source_bot_navigation_point_area(navigation(bots), origin);
      return true;
    }
    case 400: /* BOTLIB_ALLOC_CLIENT */
      initialized(bots); *out = (int32_t)bots->options.clients.allocate_client(bots->options.clients.ctx); return true;
    case 401: /* BOTLIB_FREE_CLIENT */
      initialized(bots); bots->options.clients.free_client(bots->options.clients.ctx, word(call, 0)); *out = 0; return true;
    case QA_QVM_GAME_IMPORT_BOTLIB_GET_SNAPSHOT_ENTITY: {
      initialized(bots);
      double client = word(call, 0), entity = word(call, 1);
      double current = 0;
      while (bots->options.clients.snapshot_entity(bots->options.clients.ctx, client, current) <= -1 + 0 && false) current++;
      double result = -1;
      for (double sequence = 0;; sequence++) {
        double candidate = bots->options.clients.snapshot_entity(bots->options.clients.ctx, client, sequence);
        if (candidate == -1) break;
        if (candidate > entity) { result = candidate; break; }
      }
      *out = (int32_t)result; return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_GET_CONSOLE_MESSAGE: {
      initialized(bots);
      const char *message = bots->options.clients.console_message(bots->options.clients.ctx, word(call, 0));
      bots->options.output_string(ctx, call, 1, message ? message : "");
      *out = message != NULL ? 1 : 0; return true;
    }
    case QA_QVM_GAME_IMPORT_BOTLIB_USER_COMMAND: {
      initialized(bots);
      qa_q3_base_user_command command;
      bots->options.structure(ctx, call, 1, sizeof command, &command, "bot user command");
      bots->options.clients.user_command(bots->options.clients.ctx, word(call, 0), command);
      *out = 0; return true;
    }
    default: break;
  }
  /* AAS / char / goal / weapon ranges are delegated generically. */
  if (call->code >= 200 && call->code <= 650) {
    /* Navigation-backed AAS reads. */
    switch (call->code) {
      case QA_QVM_GAME_IMPORT_BOTLIB_AAS_INITIALIZED:
        *out = navigation_ready(bots) ? 1 : 0; return true;
      case QA_QVM_GAME_IMPORT_BOTLIB_AAS_POINT_AREA_NUM: {
        qa_vec3 origin;
        bots->options.structure(ctx, call, 0, sizeof origin, &origin, "AAS point");
        *out = (int32_t)qa_source_bot_navigation_point_area(navigation(bots), origin); return true;
      }
      case QA_QVM_GAME_IMPORT_BOTLIB_AAS_TIME: {
        float time = (float)(bots->last_time / 1000);
        memcpy(out, &time, sizeof time); return true;
      }
      case QA_QVM_GAME_IMPORT_BOTLIB_AAS_SWIMMING: {
        qa_vec3 origin;
        bots->options.structure(ctx, call, 0, sizeof origin, &origin, "AAS point");
        qa_source_bot_navigation_bots_a *nav = navigation(bots);
        *out = nav->swimming(nav->ctx, origin) ? 1 : 0; return true;
      }
      case QA_QVM_GAME_IMPORT_BOTLIB_AAS_ENTITY_INFO: {
        double entity = word(call, 0);
        recorded_entity *record = find_record(bots, entity);
        qa_qvm_aas_entity_info info; memset(&info, 0, sizeof info);
        info.type = 0;
        if (record) {
          info.type = record->snapshot.state.type; info.flags = record->snapshot.state.flags;
          info.origin = record->snapshot.state.origin; info.angles = record->snapshot.state.angles;
          info.old_origin = record->snapshot.state.old_origin; info.mins = record->snapshot.state.mins;
          info.maxs = record->snapshot.state.maxs; info.ground_entity = record->snapshot.state.ground_entity;
          info.solid = record->snapshot.state.solid; info.model_index = record->snapshot.state.model_index;
          info.model_index2 = record->snapshot.state.model_index2; info.frame = record->snapshot.state.frame;
          info.event = record->snapshot.state.event; info.event_parameter = record->snapshot.state.event_parameter;
          info.powerups = record->snapshot.state.powerups; info.weapon = record->snapshot.state.weapon;
          info.legs_animation = record->snapshot.state.legs_animation; info.torso_animation = record->snapshot.state.torso_animation;
          info.valid = true; info.number = entity; info.last_visible_origin = record->seen;
          info.last_update_time = record->updated; info.update_interval = record->interval;
        }
        bots->options.output_struct(ctx, call, 1, QA_QVM_AAS_ENTITY_INFO_BYTES, &info, "AAS entity info");
        *out = 0; return true;
      }
      default: break;
    }
    return false;
  }
  return false;
}

qa_save_value *qa_app_q3_guest_bots_checkpoint(qa_app_q3_guest_bots *bots) {
  qa_save_value *root = qa_save_value_object(), *list = qa_save_value_array();
  qa_save_value *library_value = bots->options.library.capture(bots->options.library.ctx);
  if (!root || !list || !library_value) { qa_save_value_free(root); qa_save_value_free(list); qa_save_value_free(library_value); return NULL; }
  bool ok = qa_save_value_set(root, "version", qa_save_value_number(1))
      && qa_save_value_set(root, "library", library_value)
      && qa_save_value_set(root, "lastTimeMilliseconds", qa_save_value_number(bots->last_time));
  for (size_t i = 0; ok && i < bots->entity_count; i++) {
    recorded_entity *record = &bots->entities[i];
    qa_save_value *entry = qa_save_value_object(), *seen = qa_save_value_array();
    ok = entry && seen
        && qa_save_value_push(seen, qa_save_value_number(record->seen.x))
        && qa_save_value_push(seen, qa_save_value_number(record->seen.y))
        && qa_save_value_push(seen, qa_save_value_number(record->seen.z))
        && qa_save_value_set(entry, "slot", qa_save_value_number(record->slot))
        && qa_save_value_set(entry, "otherEntity", qa_save_value_number(record->snapshot.other_entity))
        && qa_save_value_set(entry, "frame", qa_save_value_number(record->snapshot.frame))
        && qa_save_value_set(entry, "time", qa_save_value_number(record->snapshot.time))
        && qa_save_value_set(entry, "updateInterval", qa_save_value_number(record->interval))
        && qa_save_value_set(entry, "lastUpdateTime", qa_save_value_number(record->updated))
        && qa_save_value_set(entry, "lastVisibleTime", qa_save_value_number(record->visible))
        && qa_save_value_set(entry, "lastVisibleOrigin", seen)
        && qa_save_value_push(list, entry);
    if (!ok) qa_save_value_free(entry);
  }
  ok = ok && qa_save_value_set(root, "entities", list);
  if (!ok) { qa_save_value_free(root); return NULL; }
  return root;
}
static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out)) qa_range_error("Out of memory");
  return out;
}
void qa_app_q3_guest_bots_restore(qa_app_q3_guest_bots *bots, const qa_save_value *value) {
  if (bots->closed || bots->options.library.is_initialized(bots->options.library.ctx) || bots->entity_count != 0)
    qa_range_error("Q3 guest bot checkpoint restore requires a fresh library");
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "q3.guest.bots")) qa_range_error("Out of memory");
  qa_save_reader version = field(&reader, "version");
  qa_save_reader_literal_number(&version, 1); qa_save_reader_destroy(&version);
  qa_save_reader library_reader = field(&reader, "library");
  qa_save_reader last = field(&reader, "lastTimeMilliseconds");
  double time = qa_save_reader_finite(&last);
  qa_save_reader_destroy(&last);
  qa_save_reader list = field(&reader, "entities");
  size_t count = qa_save_reader_list_count(&list);
  recorded_entity records[1024]; size_t record_count = 0;
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry;
    if (!qa_save_reader_item(&list, i, &entry)) qa_range_error("Out of memory");
    qa_save_reader r = field(&entry, "slot");
    double slot = qa_save_reader_integer_min(&r, 0);
    qa_save_reader_destroy(&r);
    (void)qa_app_q3_guest_records_entity(bots->options.records, slot);
    double max_time = 0;
    recorded_entity record; memset(&record, 0, sizeof record); record.slot = slot;
    r = field(&entry, "otherEntity"); record.snapshot.other_entity = qa_save_reader_integer(&r); qa_save_reader_destroy(&r);
    r = field(&entry, "frame"); record.snapshot.frame = qa_save_reader_integer(&r); qa_save_reader_destroy(&r);
    r = field(&entry, "time"); record.snapshot.time = qa_save_reader_integer_min(&r, 0); qa_save_reader_destroy(&r);
    r = field(&entry, "updateInterval"); record.interval = qa_save_reader_finite(&r); qa_save_reader_destroy(&r);
    r = field(&entry, "lastUpdateTime"); record.updated = qa_save_reader_finite(&r); qa_save_reader_destroy(&r);
    r = field(&entry, "lastVisibleTime"); record.visible = qa_save_reader_finite(&r); qa_save_reader_destroy(&r);
    r = field(&entry, "lastVisibleOrigin");
    record.seen = qa_read_vector(&r); qa_save_reader_destroy(&r);
    max_time = record.snapshot.time;
    if (record.updated > max_time) max_time = record.updated;
    if (record.visible > max_time) max_time = record.visible;
    if (max_time > time || record.interval <= 0) qa_save_reader_fail(&reader, "invalid bot entity continuation");
    bool duplicate = false;
    for (size_t j = 0; j < record_count; j++) if (records[j].slot == slot) duplicate = true;
    if (duplicate) qa_save_reader_fail(&reader, "duplicate bot entity continuation");
    records[record_count++] = record;
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
  bots->options.library.restore(bots->options.library.ctx, library_reader.value);
  qa_save_reader_destroy(&library_reader);
  qa_save_reader_destroy(&reader);
  memcpy(bots->entities, records, record_count * sizeof *records);
  bots->entity_count = record_count;
  bots->last_time = time;
}
