/* Port of src/compat/q2/rerelease/messages.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/rerelease/messages.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"

struct qa_compat_q2_rerelease_message_imports {
  qa_compat_q2_memory *memory; qa_compat_q2_rerelease_message_services services;
  void *slot_ctx; double (*source_slot)(void *, qa_guest_address);
};
qa_compat_q2_rerelease_message_imports *qa_compat_q2_rerelease_message_imports_create(
    qa_compat_q2_memory *memory, qa_compat_q2_rerelease_message_services services,
    void *slot_ctx, double (*source_slot)(void *, qa_guest_address)) {
  qa_compat_q2_rerelease_message_imports *m = calloc(1, sizeof *m); if (!m) return NULL;
  m->memory = memory; m->services = services; m->slot_ctx = slot_ctx; m->source_slot = source_slot; return m;
}
void qa_compat_q2_rerelease_message_imports_destroy(qa_compat_q2_rerelease_message_imports *m) { free(m); }
qa_compat_q2_memory *qa_compat_q2_rerelease_message_imports_get_memory(const qa_compat_q2_rerelease_message_imports *m) { return m->memory; }
bool qa_compat_q2_rerelease_message_imports_invoke(qa_compat_q2_rerelease_message_imports *m, qa_compat_q2_rerelease_import_call call, qa_guest_call_result *out) {
  if (strcmp(call.api, "game") != 0) return false;
  const qa_guest_call_value *args = call.arguments; size_t count = call.argument_count;
  qa_q2_sizebuf *buffer = m->services.buffer;
  const char *name = call.name;
  if (strcmp(name, "WriteChar") == 0) qa_q2_msg_write_char(buffer, (double)qa_compat_q2_guest_integer(args, count, 0));
  else if (strcmp(name, "WriteByte") == 0) qa_q2_msg_write_byte(buffer, (double)qa_compat_q2_guest_integer(args, count, 0));
  else if (strcmp(name, "WriteShort") == 0) qa_q2_msg_write_short(buffer, (double)qa_compat_q2_guest_integer(args, count, 0));
  else if (strcmp(name, "WriteLong") == 0) qa_q2_msg_write_long(buffer, (double)qa_compat_q2_guest_integer(args, count, 0));
  else if (strcmp(name, "WriteFloat") == 0 || strcmp(name, "WriteAngle") == 0) {
    const qa_guest_call_value *v = qa_compat_q2_guest_argument(args, count, 0);
    if (v->kind != QA_GUEST_VALUE_FLOAT32) qa_range_error("Q2 message writer requires a source float");
    if (strcmp(name, "WriteFloat") == 0) qa_q2_msg_write_float(buffer, v->number_value);
    else qa_q2_msg_write_byte(buffer, (double)(qa_js_to_int32(trunc((float)((float)(v->number_value * 256) / 360))) & 255)); /* guest NaN/huge: ToInt32 */
  }
  else if (strcmp(name, "WritePosition") == 0) {
    qa_vec3 p = qa_compat_q2_mem_read_f32_vector(m->memory, qa_compat_q2_guest_required_pointer(args, count, 0));
    qa_q2_msg_write_float(buffer, p.x); qa_q2_msg_write_float(buffer, p.y); qa_q2_msg_write_float(buffer, p.z);
  }
  else if (strcmp(name, "WriteDir") == 0) {
    qa_guest_address address; bool has = qa_compat_q2_guest_pointer(args, count, 0, &address);
    if (!has) qa_q2_msg_write_byte(buffer, 0);
    else { qa_vec3 d = qa_compat_q2_mem_read_f32_vector(m->memory, address); qa_q2_wire_vec3 w = {(float)d.x, (float)d.y, (float)d.z}; qa_q2_msg_write_dir(buffer, w); }
  }
  else if (strcmp(name, "WriteString") == 0) {
    qa_guest_address address; bool has = qa_compat_q2_guest_pointer(args, count, 0, &address);
    if (!has) qa_q2_msg_write_byte(buffer, 0);
    else {
      int64_t length = 0;
      while (qa_compat_q2_mem_read_u8(m->memory, qa_compat_q2_mem_offset(m->memory, address, length)) != 0) {
        if (++length >= buffer->maxsize) qa_range_error("Q2 message string exceeds message capacity");
      }
      uint8_t *bytes = malloc((size_t)length + 1);
      m->memory->copy(m->memory->ctx, address, bytes, (double)length + 1);
      qa_q2_sz_write(buffer, bytes, (size_t)length + 1, (int64_t)length + 1);
      free(bytes);
    }
  }
  else if (strcmp(name, "WriteEntity") == 0) qa_q2_msg_write_short(buffer, m->source_slot(m->slot_ctx, qa_compat_q2_guest_required_pointer(args, count, 0)));
  else if (strcmp(name, "unicast") == 0) {
    qa_guest_address address; bool has = qa_compat_q2_guest_pointer(args, count, 0, &address);
    if (!has) qa_q2_sz_clear(buffer);
    else {
      if (buffer->overflowed) qa_range_error("Q2 unicast message buffer overflowed");
      double slot = m->source_slot(m->slot_ctx, address);
      if (m->services.accepts_client(m->services.ctx, slot) && buffer->cursize != 0)
        m->services.unicast(m->services.ctx, (qa_compat_q2_rerelease_unicast){slot, qa_compat_q2_guest_integer(args, count, 1) != 0,
            (double)qa_compat_q2_guest_integer(args, count, 2), buffer->data, (size_t)buffer->cursize});
      qa_q2_sz_clear(buffer);
    }
  }
  else if (strcmp(name, "multicast") == 0) {
    if (buffer->overflowed) qa_range_error("Q2 multicast message buffer overflowed");
    int64_t target = qa_compat_q2_guest_integer(args, count, 1);
    qa_guest_address address; bool has = qa_compat_q2_guest_pointer(args, count, 0, &address);
    if (target != 0 && target != 1 && target != 2) qa_range_error("Unknown Q2 multicast destination");
    if (target != 0 && !has) qa_range_error("Q2 spatial multicast requires an origin");
    if (buffer->cursize != 0) {
      qa_compat_q2_rerelease_multicast msg; memset(&msg, 0, sizeof msg);
      msg.has_origin = has; if (has) msg.origin = qa_compat_q2_mem_read_f32_vector(m->memory, address);
      msg.destination = target == 0 ? "all" : target == 1 ? "phs" : "pvs";
      msg.reliable = qa_compat_q2_guest_integer(args, count, 2) != 0;
      msg.bytes = buffer->data; msg.byte_length = (size_t)buffer->cursize;
      m->services.multicast(m->services.ctx, msg);
    }
    qa_q2_sz_clear(buffer);
  }
  else return false;
  if (out) *out = qa_compat_q2_call_void();
  return true;
}
