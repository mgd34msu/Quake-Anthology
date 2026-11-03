/* See include/qa/compat/qvm/server_game_syscalls.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/server_game_syscalls.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/abi.h"
#include "qa/core/common_error.h"
#include "qa/foundation/support.h"

/* Q3 MAX_GENTITIES: area queries return entity slots, each below it. */
#define AREA_ENTITY_SLOTS 1024

/* Landed QvmGameImport values (TS abi.ts). */
enum {
  G_DROP_CLIENT = QA_QVM_GAME_IMPORT_G_DROP_CLIENT,
  G_SEND_SERVER_COMMAND = QA_QVM_GAME_IMPORT_G_SEND_SERVER_COMMAND,
  G_SET_CONFIGSTRING = QA_QVM_GAME_IMPORT_G_SET_CONFIGSTRING,
  G_GET_CONFIGSTRING = QA_QVM_GAME_IMPORT_G_GET_CONFIGSTRING,
  G_GET_USERINFO = QA_QVM_GAME_IMPORT_G_GET_USERINFO,
  G_SET_USERINFO = QA_QVM_GAME_IMPORT_G_SET_USERINFO,
  G_GET_SERVERINFO = QA_QVM_GAME_IMPORT_G_GET_SERVERINFO,
  G_SET_BRUSH_MODEL = QA_QVM_GAME_IMPORT_G_SET_BRUSH_MODEL,
  G_TRACE = QA_QVM_GAME_IMPORT_G_TRACE,
  G_POINT_CONTENTS = QA_QVM_GAME_IMPORT_G_POINT_CONTENTS,
  G_IN_PVS = QA_QVM_GAME_IMPORT_G_IN_PVS,
  G_IN_PVS_IGNORE_PORTALS = QA_QVM_GAME_IMPORT_G_IN_PVS_IGNORE_PORTALS,
  G_ADJUST_AREA_PORTAL_STATE = QA_QVM_GAME_IMPORT_G_ADJUST_AREA_PORTAL_STATE,
  G_AREAS_CONNECTED = QA_QVM_GAME_IMPORT_G_AREAS_CONNECTED,
  G_LINKENTITY = QA_QVM_GAME_IMPORT_G_LINKENTITY,
  G_UNLINKENTITY = QA_QVM_GAME_IMPORT_G_UNLINKENTITY,
  G_ENTITIES_IN_BOX = QA_QVM_GAME_IMPORT_G_ENTITIES_IN_BOX,
  G_ENTITY_CONTACT = QA_QVM_GAME_IMPORT_G_ENTITY_CONTACT,
  G_GET_USERCMD = QA_QVM_GAME_IMPORT_G_GET_USERCMD,
  G_GET_ENTITY_TOKEN = QA_QVM_GAME_IMPORT_G_GET_ENTITY_TOKEN,
  G_TRACECAPSULE = QA_QVM_GAME_IMPORT_G_TRACECAPSULE,
  G_ENTITY_CONTACTCAPSULE = QA_QVM_GAME_IMPORT_G_ENTITY_CONTACTCAPSULE,
};

static qa_vec3 vector(const qa_qvm_host_call *call, int32_t pointer) {
  qa_qvm_writable_view view;
  qa_qvm_game_memory_view(QA_SHARED_GUEST(call), pointer, 12, 0, &view);
  qa_vec3 result = {(double)qa_qvm_writable_view_get_float32(&view, 0, true),
                    (double)qa_qvm_writable_view_get_float32(&view, 4, true),
                    (double)qa_qvm_writable_view_get_float32(&view, 8, true)};
  return result;
}

static int32_t slot(const qa_qvm_host_call *call, const qa_qvm_server_game_services *services, size_t index) {
  return (int32_t)qa_qvm_game_data_number_from_pointer(services->data,
                                                        (double)qa_qvm_host_call_word(call, index));
}

bool qa_qvm_server_game_syscall(const qa_qvm_host_call *call,
                                const qa_qvm_server_game_services *services, int32_t *out) {
  if (strcmp(call->kind, "engine") != 0 || strcmp(call->role, "qagame") != 0) return false;
  const qa_qvm_server_spatial_operations *spatial = services->spatial;
  switch (call->code) {
    case G_DROP_CLIENT:
    case G_SEND_SERVER_COMMAND:
    case G_GET_USERINFO:
    case G_SET_USERINFO:
    case G_GET_USERCMD: {
      qa_qvm_client_game_services bundle;
      bundle.abi_profile = services->data->abi_profile;
      bundle.max_clients = services->max_clients;
      bundle.ctx = services->ctx;
      bundle.get_userinfo = services->get_userinfo;
      bundle.set_userinfo = services->set_userinfo;
      bundle.get_user_command = services->get_user_command;
      bundle.drop_client = services->drop_client;
      bundle.send_server_command = services->send_server_command;
      return qa_qvm_client_game_syscall(call, bundle, out);
    }
    case G_SET_CONFIGSTRING:
    case G_GET_CONFIGSTRING:
    case G_GET_SERVERINFO: {
      qa_qvm_server_information_services bundle;
      bundle.abi_profile = services->data->abi_profile;
      bundle.cvars = services->cvars;
      bundle.configstrings_ctx = services->configstrings_ctx;
      bundle.configstring_get = services->configstring_get;
      bundle.configstring_set = services->configstring_set;
      return qa_qvm_server_information_syscall(call, &bundle, out);
    }
    case G_SET_BRUSH_MODEL: {
      if (qa_qvm_host_call_word(call, 2) == 0)
        qa_common_error(QA_COMMON_ERROR_DROP, "SV_SetBrushModel: NULL");
      char *name = qa_qvm_game_memory_read_string(QA_SHARED_GUEST(call), qa_qvm_host_call_word(call, 2));
      if (name[0] != '*') {
        char message[1200];
        snprintf(message, sizeof message, "SV_SetBrushModel: %s isn't a brush model", name);
        free(name);
        qa_common_error(QA_COMMON_ERROR_DROP, message);
      }
      spatial->set_brush_model(spatial->ctx, slot(call, services, 1), name);
      free(name);
      *out = 0;
      return true;
    }
    case G_TRACE:
    case G_TRACECAPSULE: {
      qa_vec3 zero = {0, 0, 0};
      qa_qvm_server_trace_query query;
      query.start = vector(call, qa_qvm_host_call_word(call, 2));
      query.end = vector(call, qa_qvm_host_call_word(call, 5));
      query.shape_kind = call->code == G_TRACECAPSULE ? "capsule" : "box";
      query.shape_bounds.min = qa_qvm_host_call_word(call, 3) == 0 ? zero : vector(call, qa_qvm_host_call_word(call, 3));
      query.shape_bounds.max = qa_qvm_host_call_word(call, 4) == 0 ? zero : vector(call, qa_qvm_host_call_word(call, 4));
      query.pass_entity_num = qa_qvm_host_call_word(call, 6);
      query.mask = qa_qvm_host_call_word(call, 7);
      qa_qvm_trace_record result = spatial->trace(spatial->ctx, &query);
      qa_qvm_writable_view view;
      qa_qvm_game_memory_view(QA_SHARED_GUEST(call), qa_qvm_host_call_word(call, 1), QA_QVM_TRACE_BYTES, 0, &view);
      qa_qvm_write_trace_view(&view, &result);
      *out = 0;
      return true;
    }
    case G_POINT_CONTENTS:
      *out = spatial->point_contents(spatial->ctx, vector(call, qa_qvm_host_call_word(call, 1)),
                                     qa_qvm_host_call_word(call, 2));
      return true;
    case G_IN_PVS:
    case G_IN_PVS_IGNORE_PORTALS: {
      qa_vec3 first = vector(call, qa_qvm_host_call_word(call, 1));
      qa_vec3 second = vector(call, qa_qvm_host_call_word(call, 2));
      *out = spatial->in_pvs(spatial->ctx, first, second, call->code == G_IN_PVS_IGNORE_PORTALS) ? 1 : 0;
      return true;
    }
    case G_ADJUST_AREA_PORTAL_STATE:
      spatial->adjust_area_portal_state(spatial->ctx, slot(call, services, 1),
                                        qa_qvm_host_call_word(call, 2) != 0);
      *out = 0;
      return true;
    case G_AREAS_CONNECTED:
      *out = spatial->areas_connected(spatial->ctx, qa_qvm_host_call_word(call, 1),
                                      qa_qvm_host_call_word(call, 2)) ? 1 : 0;
      return true;
    case G_LINKENTITY:
      spatial->link(spatial->ctx, slot(call, services, 1));
      *out = 0;
      return true;
    case G_UNLINKENTITY:
      spatial->unlink(spatial->ctx, slot(call, services, 1));
      *out = 0;
      return true;
    case G_ENTITIES_IN_BOX: {
      qa_bounds input;
      input.min = vector(call, qa_qvm_host_call_word(call, 1));
      input.max = vector(call, qa_qvm_host_call_word(call, 2));
      int32_t maximum = qa_qvm_host_call_word(call, 4);
      /* TS queries once. A negative maximum returns the whole list, so size the
       * buffer for every entity slot (no more fit the guest's entity table) and
       * never past the guest output span (a longer list fails at its view). */
      size_t capacity = maximum >= 0 ? (size_t)maximum : AREA_ENTITY_SLOTS;
      if (maximum < 0) {
        qa_qvm_game_region span;
        if (qa_qvm_game_memory_pointer(QA_SHARED_GUEST(call), qa_qvm_host_call_word(call, 3), &span) &&
            span.length / 4 < capacity)
          capacity = span.length / 4;
      }
      int32_t *result = malloc((capacity == 0 ? 1 : capacity) * sizeof *result);
      if (result == NULL) qa_range_error("QVM memory allocation failed");
      size_t length = spatial->area_entities(spatial->ctx, input, maximum, result, capacity);
      if (maximum >= 0 && length > (size_t)maximum) {
        free(result);
        qa_range_error("Area entity owner exceeded output capacity");
      }
      /* Only a list longer than the entity table or the guest span gets here;
       * the view below rejects the latter as TS does. */
      if (maximum < 0 && length > capacity) {
        qa_cleanup cleanup;
        qa_cleanup_push(&cleanup, free, result);
        qa_qvm_writable_view output;
        qa_qvm_game_memory_view(QA_SHARED_GUEST(call), qa_qvm_host_call_word(call, 3), (int64_t)(length * 4), 0, &output);
        qa_cleanup_pop(&cleanup);
      }
      while (length > capacity) {
        capacity = length;
        int32_t *grown = realloc(result, capacity * sizeof *grown);
        if (grown == NULL) {
          free(result);
          qa_range_error("QVM memory allocation failed");
        }
        result = grown;
        length = spatial->area_entities(spatial->ctx, input, maximum, result, capacity);
      }
      if (length > 0) {
        qa_qvm_writable_view output;
        qa_qvm_game_memory_view(QA_SHARED_GUEST(call), qa_qvm_host_call_word(call, 3), (int64_t)(length * 4), 0, &output);
        for (size_t i = 0; i < length; i++)
          qa_qvm_writable_view_set_int32(&output, (int64_t)(i * 4), result[i], true);
      }
      free(result);
      *out = (int32_t)length;
      return true;
    }
    case G_ENTITY_CONTACT:
    case G_ENTITY_CONTACTCAPSULE: {
      qa_bounds input;
      input.min = vector(call, qa_qvm_host_call_word(call, 1));
      input.max = vector(call, qa_qvm_host_call_word(call, 2));
      *out = spatial->entity_contact(spatial->ctx, input, slot(call, services, 3),
                                     call->code == G_ENTITY_CONTACTCAPSULE) ? 1 : 0;
      return true;
    }
    case G_GET_ENTITY_TOKEN:
      return qa_qvm_entity_token_syscall(call, services->entity_tokens, out);
    default: return false;
  }
}
