/* Port of src/compat/q2/rerelease/navigation.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/rerelease/navigation.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct qa_compat_q2_rerelease_navigation_imports {
  qa_compat_q2_memory *memory; qa_compat_q2_rerelease_navigation_services services;
  void *actor_ctx; bool (*actor)(void *, qa_guest_address, qa_actor_id *);
};
qa_compat_q2_rerelease_navigation_imports *qa_compat_q2_rerelease_navigation_imports_create(
    qa_compat_q2_memory *memory, qa_compat_q2_rerelease_navigation_services services,
    void *actor_ctx, bool (*actor)(void *, qa_guest_address, qa_actor_id *)) {
  qa_compat_q2_rerelease_navigation_imports *n = calloc(1, sizeof *n); if (!n) return NULL;
  n->memory = memory; n->services = services; n->actor_ctx = actor_ctx; n->actor = actor; return n;
}
void qa_compat_q2_rerelease_navigation_imports_destroy(qa_compat_q2_rerelease_navigation_imports *n) { free(n); }
qa_compat_q2_memory *qa_compat_q2_rerelease_navigation_imports_get_memory(const qa_compat_q2_rerelease_navigation_imports *n) { return n->memory; }
static qa_vec3 read_vector(const qa_compat_q2_memory *memory, qa_guest_address address) {
  qa_vec3 v = qa_compat_q2_mem_read_f32_vector(memory, address);
  if (!isfinite(v.x) || !isfinite(v.y) || !isfinite(v.z)) qa_range_error("Nonfinite Q2 navigation point");
  return v;
}
static double read_finite(const qa_compat_q2_memory *memory, qa_guest_address request, int64_t off) {
  double v = qa_compat_q2_mem_read_f32(memory, qa_compat_q2_mem_offset(memory, request, off));
  if (!isfinite(v)) qa_range_error("Nonfinite Q2 navigation parameter");
  return v;
}
static void write_vector(const qa_compat_q2_memory *memory, qa_guest_address address, qa_vec3 p) {
  qa_compat_q2_mem_write_f32(memory, address, p.x);
  qa_compat_q2_mem_write_f32(memory, qa_compat_q2_mem_offset(memory, address, 4), p.y);
  qa_compat_q2_mem_write_f32(memory, qa_compat_q2_mem_offset(memory, address, 8), p.z);
}
qa_guest_call_result qa_compat_q2_rerelease_navigation_imports_invoke(qa_compat_q2_rerelease_navigation_imports *n, const char *name,
                                                                      const qa_guest_call_value *args, size_t count) {
  qa_compat_q2_memory *memory = n->memory;
  if (strcmp(name, "GetPathToGoal") != 0) {
    qa_actor_id actor; bool has_actor = n->actor(n->actor_ctx, qa_compat_q2_guest_required_pointer(args, count, 0), &actor);
    if (strcmp(name, "Bot_FollowActor") == 0) {
      qa_actor_id target; bool has_target = n->actor(n->actor_ctx, qa_compat_q2_guest_required_pointer(args, count, 1), &target);
      return qa_compat_q2_call_int32(!has_actor || !has_target ? 0 : n->services.follow_actor(n->services.ctx, actor, target));
    }
    qa_vec3 point = read_vector(memory, qa_compat_q2_guest_required_pointer(args, count, 1));
    const qa_guest_call_value *tolerance = qa_compat_q2_guest_argument(args, count, 2);
    if (tolerance->kind != QA_GUEST_VALUE_FLOAT32 || !isfinite(tolerance->number_value) || tolerance->number_value < 0)
      qa_range_error("Invalid Q2 bot movement tolerance");
    return qa_compat_q2_call_int32(!has_actor ? 0 : n->services.move_to_point(n->services.ctx, actor, point, tolerance->number_value));
  }
  qa_guest_address request = qa_compat_q2_guest_required_pointer(args, count, 0), output = qa_compat_q2_guest_required_pointer(args, count, 1);
  memory->check(memory->ctx, request, 80, "read"); memory->check(memory->ctx, output, 40, "write");
  qa_guest_address buffer; bool has_buffer = qa_compat_q2_mem_read_pointer(memory, qa_compat_q2_mem_offset(memory, request, 64), &buffer);
  int64_t point_count = qa_compat_q2_mem_read_i64(memory, qa_compat_q2_mem_offset(memory, request, 72));
  if (point_count < 0 || point_count > 0x7fffffff || (point_count > 0 && !has_buffer)) qa_range_error("Invalid Q2 navigation point buffer");
  if (has_buffer) memory->check(memory->ctx, buffer, (double)point_count * 12, "write");
  qa_compat_q2_rerelease_path_request req;
  req.start = read_vector(memory, request); req.goal = read_vector(memory, qa_compat_q2_mem_offset(memory, request, 12));
  req.flags = qa_compat_q2_mem_read_u32(memory, qa_compat_q2_mem_offset(memory, request, 24));
  req.move_distance = read_finite(memory, request, 28);
  req.ignore_node_flags = qa_compat_q2_mem_read_u8(memory, qa_compat_q2_mem_offset(memory, request, 36)) != 0;
  req.min_height = read_finite(memory, request, 40); req.max_height = read_finite(memory, request, 44);
  req.radius = read_finite(memory, request, 48); req.drop_height = read_finite(memory, request, 52);
  req.jump_height = read_finite(memory, request, 56);
  qa_compat_q2_rerelease_path_result result = n->services.path_to_goal(n->services.ctx, n->services.runtime(n->services.ctx), req);
  size_t points_written = result.point_count < (size_t)point_count ? result.point_count : (size_t)point_count;
  if (has_buffer) for (size_t i = 0; i < points_written; i++)
    write_vector(memory, qa_compat_q2_mem_offset(memory, buffer, (int64_t)(i * 12)), result.points[i]);
  qa_compat_q2_mem_write_i32(memory, output, (int32_t)points_written);
  qa_compat_q2_mem_write_f32(memory, qa_compat_q2_mem_offset(memory, output, 4), result.distance_squared);
  write_vector(memory, qa_compat_q2_mem_offset(memory, output, 8), result.first);
  write_vector(memory, qa_compat_q2_mem_offset(memory, output, 20), result.second);
  qa_compat_q2_mem_write_i32(memory, qa_compat_q2_mem_offset(memory, output, 32), (int32_t)result.link_type);
  qa_compat_q2_mem_write_i32(memory, qa_compat_q2_mem_offset(memory, output, 36), (int32_t)result.code);
  return qa_compat_q2_call_uint32(result.code < 5 ? 1 : 0);
}
