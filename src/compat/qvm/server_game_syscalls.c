/* See include/qa/compat/qvm/server_game_syscalls.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/server_game_syscalls.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/common_error.h"
#include "qa/foundation/support.h"

/* Mirrored QvmGameImport values (TS abi.ts). */
enum {
  G_DROP_CLIENT = 16, G_SEND_SERVER_COMMAND = 17, G_SET_CONFIGSTRING = 18,
  G_GET_CONFIGSTRING = 19, G_GET_USERINFO = 20, G_SET_USERINFO = 21,
  G_GET_SERVERINFO = 22, G_SET_BRUSH_MODEL = 23, G_TRACE = 24,
  G_POINT_CONTENTS = 25, G_IN_PVS = 26, G_IN_PVS_IGNORE_PORTALS = 27,
  G_ADJUST_AREA_PORTAL_STATE = 28, G_AREAS_CONNECTED = 29, G_LINKENTITY = 30,
  G_UNLINKENTITY = 31, G_ENTITIES_IN_BOX = 32, G_ENTITY_CONTACT = 33,
  G_GET_USERCMD = 36, G_GET_ENTITY_TOKEN = 37, G_TRACECAPSULE = 43,
  G_ENTITY_CONTACTCAPSULE = 44,
};

static qa_vec3 vector(const qa_qvm_host_call *call, int32_t pointer) {
  qa_qvm_writable_view view;
  qa_qvm_memory_view(call->guest, pointer, 12, 0, &view);
  qa_vec3 result = {(double)qa_qvm_writable_view_get_float32(&view, 0, true),
                    (double)qa_qvm_writable_view_get_float32(&view, 4, true),
                    (double)qa_qvm_writable_view_get_float32(&view, 8, true)};
  return result;
}

static int32_t slot(const qa_qvm_host_call *call, const qa_qvm_server_game_services *services, size_t index) {
  return services->data->number_from_pointer(services->data->ctx, qa_qvm_host_call_word(call, index));
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
      qa_qvmy_client_game_services bundle;
      bundle.abi_profile = services->data->abi_profile;
      bundle.max_clients = services->max_clients;
      bundle.ctx = services->ctx;
      bundle.get_userinfo = services->get_userinfo;
      bundle.set_userinfo = services->set_userinfo;
      bundle.get_user_command = services->get_user_command;
      bundle.drop_client = services->drop_client;
      bundle.send_server_command = services->send_server_command;
      return services->client_game_syscall(services->client_game_ctx, call, &bundle, out);
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
      char *name = qa_qvm_memory_read_string(call->guest, qa_qvm_host_call_word(call, 2));
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
      qa_qvm_memory_view(call->guest, qa_qvm_host_call_word(call, 1), QA_QVM_TRACE_BYTES, 0, &view);
      qa_qvm_write_trace_view(&view, &result);
      *out = 0;
      return true;
    }
    case G_POINT_CONTENTS:
      *out = spatial->point_contents(spatial->ctx, vector(call, qa_qvm_host_call_word(call, 1)),
                                     qa_qvm_host_call_word(call, 2));
      return true;
    case G_IN_PVS:
    case G_IN_PVS_IGNORE_PORTALS:
      *out = spatial->in_pvs(spatial->ctx, vector(call, qa_qvm_host_call_word(call, 1)),
                             vector(call, qa_qvm_host_call_word(call, 2)),
                             call->code == G_IN_PVS_IGNORE_PORTALS) ? 1 : 0;
      return true;
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
      qa_bounds input = {vector(call, qa_qvm_host_call_word(call, 1)),
                         vector(call, qa_qvm_host_call_word(call, 2))};
      int32_t maximum = qa_qvm_host_call_word(call, 4);
      size_t capacity = maximum > 0 ? (size_t)maximum : 0;
      int32_t *result = malloc((capacity == 0 ? 1 : capacity) * sizeof *result);
      if (result == NULL) qa_range_error("QVM memory allocation failed");
      size_t length = spatial->area_entities(spatial->ctx, input, maximum, result, capacity);
      if (maximum >= 0 && length > (size_t)maximum) {
        free(result);
        qa_range_error("Area entity owner exceeded output capacity");
      }
      if (length > 0) {
        qa_qvm_writable_view output;
        qa_qvm_memory_view(call->guest, qa_qvm_host_call_word(call, 3), (int64_t)(length * 4), 0, &output);
        for (size_t i = 0; i < length; i++)
          qa_qvm_writable_view_set_int32(&output, (int64_t)(i * 4), result[i], true);
      }
      free(result);
      *out = (int32_t)length;
      return true;
    }
    case G_ENTITY_CONTACT:
    case G_ENTITY_CONTACTCAPSULE: {
      qa_bounds input = {vector(call, qa_qvm_host_call_word(call, 1)),
                         vector(call, qa_qvm_host_call_word(call, 2))};
      *out = spatial->entity_contact(spatial->ctx, input, slot(call, services, 3),
                                     call->code == G_ENTITY_CONTACTCAPSULE) ? 1 : 0;
      return true;
    }
    case G_GET_ENTITY_TOKEN: {
      const char *token = NULL;
      bool ended = false;
      services->entity_token(services->ctx, &token, &ended);
      return services->entity_token_syscall(services->entity_token_ctx, call, token, ended, out);
    }
    default: return false;
  }
}
