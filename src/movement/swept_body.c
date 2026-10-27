/* Port of src/movement/swept-body.ts. GPL-2.0-or-later. */
#include "qa/movement/swept_body.h"

#include <stdlib.h>

#include "qa/foundation/support.h"

static const qa_vec3 swept_zero = {0, 0, 0};

qa_sweep_stop qa_sweep_body(const qa_swept_body_services *services, double elapsed) {
  void *ctx = services->ctx;
  const qa_swept_body_math *math = &services->math;
  qa_swept_body_state initial;
  if (!services->read(ctx, &initial)) return QA_SWEEP_REMOVED;
  const qa_vec3 primal = initial.velocity;
  const bool paired = services->has_plane_response;
  const size_t seed_count = paired ? services->plane_response.seed_count : 0;
  qa_vec3 *planes = malloc((seed_count + 8) * sizeof *planes);
  if (planes == NULL) qa_range_error("Out of memory");
  size_t plane_count = 0;
  for (size_t i = 0; i < seed_count; i++) planes[plane_count++] = services->plane_response.seeds[i];
  qa_vec3 original = initial.velocity;
  double remaining = elapsed;
  qa_sweep_stop stop = QA_SWEEP_COMPLETE;
  bool done = false;
  for (int bump = 0; bump < 4 && !done; bump++) {
    qa_swept_body_state state;
    if (!services->read(ctx, &state)) {
      stop = QA_SWEEP_REMOVED;
      break;
    }
    if (services->stop_when_still && state.velocity.x == 0 && state.velocity.y == 0 && state.velocity.z == 0) break;
    qa_swept_body_trace trace;
    services->trace(ctx, state.origin, math->advance(math->ctx, state.origin, remaining, state.velocity), &trace);
    if (trace.all_solid || (services->has_collision_policy && services->stop_on_start_solid && trace.start_solid)) {
      if (services->has_collision_policy && services->all_solid_velocity == QA_SWEPT_ALL_SOLID_ZERO_Z) {
        qa_vec3 zero_z = state.velocity;
        zero_z.z = 0;
        services->write_velocity(ctx, zero_z, QA_SWEPT_VELOCITY_VERTICAL);
      } else {
        services->write_velocity(ctx, swept_zero, QA_SWEPT_VELOCITY_ALL);
      }
      if (services->solid != NULL) services->solid(ctx, &trace);
      stop = QA_SWEEP_SOLID;
      break;
    }
    if (services->prepare_trace != NULL) services->prepare_trace(ctx, &trace);
    if (trace.fraction > 0) {
      services->write_origin(ctx, trace.end);
      if (!services->has_collision_policy || services->original_velocity != QA_SWEPT_ORIGINAL_INITIAL)
        original = state.velocity;
      if (!paired) plane_count = 0;
    }
    if (trace.fraction == 1) break;
    if (services->touch != NULL) services->touch(ctx, &trace);
    if (paired) {
      remaining = math->remaining(math->ctx, remaining, trace.fraction);
      if (plane_count >= 5) {
        services->write_velocity(ctx, swept_zero, QA_SWEPT_VELOCITY_ALL);
        stop = QA_SWEEP_PLANE_LIMIT;
        break;
      }
    }
    const qa_vec3 normal = services->normal(ctx, &trace);
    services->impact(ctx, &trace, normal);
    qa_swept_body_state current;
    if (!services->read(ctx, &current)) {
      stop = QA_SWEEP_REMOVED;
      break;
    }
    if (!paired) {
      remaining = math->remaining(math->ctx, remaining, trace.fraction);
      if (plane_count >= 5) {
        services->write_velocity(ctx, swept_zero, QA_SWEPT_VELOCITY_ALL);
        stop = QA_SWEEP_PLANE_LIMIT;
        break;
      }
    }
    if (services->has_duplicate_plane) {
      bool duplicate = false;
      for (size_t i = 0; i < plane_count; i++) {
        if (math->dot(math->ctx, normal, planes[i]) > services->duplicate_threshold) {
          duplicate = true;
          break;
        }
      }
      if (duplicate) {
        services->duplicate_recover(ctx, normal);
        continue;
      }
    }
    planes[plane_count++] = normal;
    bool has_velocity = false;
    qa_vec3 velocity = swept_zero;
    qa_vec3 last_candidate = current.velocity;
    for (size_t index = 0; index < plane_count && !done; index++) {
      const qa_vec3 plane = planes[index];
      if (paired) {
        const qa_swept_plane_response *response = &services->plane_response;
        const double into = math->dot(math->ctx, current.velocity, plane);
        if (into >= response->enter_threshold) continue;
        response->impact_speed(response->ctx, -into);
        qa_vec3 clipped = math->clip(math->ctx, current.velocity, plane);
        qa_vec3 end_clipped = math->clip(math->ctx, response->end_velocity(response->ctx), plane);
        for (size_t second_index = 0; second_index < plane_count; second_index++) {
          const qa_vec3 second = planes[second_index];
          if (second_index == index || math->dot(math->ctx, clipped, second) >= response->enter_threshold) continue;
          clipped = math->clip(math->ctx, clipped, second);
          end_clipped = math->clip(math->ctx, end_clipped, second);
          if (math->dot(math->ctx, clipped, plane) >= 0) continue;
          const qa_vec3 direction = response->normalize(response->ctx, math->cross(math->ctx, plane, second));
          clipped = math->scale(math->ctx, direction, math->dot(math->ctx, direction, current.velocity));
          end_clipped = math->scale(math->ctx, direction,
                                    math->dot(math->ctx, direction, response->end_velocity(response->ctx)));
          bool blocked = false;
          for (size_t third = 0; third < plane_count; third++) {
            if (third != index && third != second_index &&
                math->dot(math->ctx, clipped, planes[third]) < response->enter_threshold) {
              blocked = true;
              break;
            }
          }
          if (blocked) {
            services->write_velocity(ctx, swept_zero, QA_SWEPT_VELOCITY_ALL);
            stop = QA_SWEEP_CREASE_BLOCKED;
            done = true;
            break;
          }
        }
        if (done) break;
        velocity = clipped;
        has_velocity = true;
        response->write_end_velocity(response->ctx, end_clipped);
        break;
      }
      const qa_vec3 candidate = math->clip(math->ctx,
                                           services->has_collision_policy &&
                                                   services->candidate_velocity == QA_SWEPT_CANDIDATE_SEQUENTIAL
                                               ? last_candidate
                                               : original,
                                           plane);
      last_candidate = candidate;
      bool accepted = true;
      for (size_t other = 0; other < plane_count; other++) {
        if (!services->same_plane(ctx, planes[other], plane, other == index) &&
            math->dot(math->ctx, candidate, planes[other]) < 0) {
          accepted = false;
          break;
        }
      }
      if (accepted) {
        velocity = candidate;
        has_velocity = true;
        break;
      }
    }
    if (done) break;
    if (paired) {
      if (has_velocity) services->write_velocity(ctx, velocity, QA_SWEPT_VELOCITY_ALL);
      continue;
    }
    if (!has_velocity) {
      if (plane_count != 2) {
        services->write_velocity(ctx, swept_zero, QA_SWEPT_VELOCITY_ALL);
        stop = QA_SWEEP_CREASE_BLOCKED;
        break;
      }
      const qa_vec3 direction = math->cross(math->ctx, planes[0], planes[1]);
      velocity = math->scale(math->ctx, direction,
                             math->dot(math->ctx, direction,
                                       services->has_collision_policy &&
                                               services->crease_velocity == QA_SWEPT_CREASE_LAST_CANDIDATE
                                           ? last_candidate
                                           : current.velocity));
    }
    if (math->dot(math->ctx, velocity, primal) <= 0) {
      services->write_velocity(ctx, swept_zero, QA_SWEPT_VELOCITY_ALL);
      stop = QA_SWEEP_REVERSED;
      break;
    }
    services->write_velocity(ctx, velocity, QA_SWEPT_VELOCITY_ALL);
  }
  free(planes);
  return stop;
}
