/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/prediction.h"
#include "qa/foundation/support.h"
#include <stdlib.h>
#include <string.h>
qa_bot_travel_prediction_result_bots_a
qa_project_bot_movement(qa_bot_movement_prediction q, qa_bot_movement_projection_bots_a proj) {
  qa_bot_travel_prediction_result_bots_a r;
  memset(&r, 0, sizeof r);
  qa_vec3 end = q.origin, velocity = q.velocity;
  double seconds = 0;
  int64_t frames = 0, stop_event = 0;
  bool grounded = q.on_ground;
  int64_t water = 0;
  bool has_area = false;
  int64_t end_area = 0;
  size_t cap = (size_t)(q.max_frames > 0 ? q.max_frames + 1 : 1);
  r.trajectory = malloc(cap * sizeof *r.trajectory);
  r.trajectory_count = 0;
  if (r.trajectory)
    r.trajectory[r.trajectory_count++] = end;
  qa_vec3 none = {0, 0, 0};
  for (int64_t frame = 0; frame < q.max_frames; frame++) {
    double elapsed = 0;
    qa_bot_movement_step_result res;
    memset(&res, 0, sizeof res);
    bool ok = proj.step(proj.ctx, frame, frame < q.command_frames ? q.command_move : none, &elapsed,
                        &res);
    if (!ok)
      qa_range_error("Bot projection movement input and provider disagree");
    if (!(elapsed > 0))
      qa_range_error("Bot movement projection must advance detached source time");
    if (res.actor_removed)
      qa_range_error("Movement projection attempted to remove the authoritative actor");
    end = res.origin;
    velocity = res.velocity;
    grounded = res.grounded;
    water = res.water_level;
    r.has_bounds = res.has_bounds;
    r.bounds = res.bounds;
    qa_bot_movement_stop stop = proj.stop(proj.ctx, frame, &res);
    has_area = stop.has_area;
    end_area = stop.area;
    stop_event = stop.events & q.stop_events;
    if (stop_event != 0)
      end = stop.origin;
    seconds += elapsed;
    frames++;
    if (r.trajectory && r.trajectory_count < cap)
      r.trajectory[r.trajectory_count++] = end;
    if (stop_event != 0)
      break;
  }
  r.end = end;
  r.has_end_area = has_area;
  r.end_area = end_area;
  r.velocity = velocity;
  r.frames = frames;
  r.stop_event = stop_event;
  r.seconds = seconds;
  r.grounded = grounded;
  r.water_level = water;
  return r;
}
void qa_bot_travel_prediction_result_bots_a_destroy(qa_bot_travel_prediction_result_bots_a *r) {
  free(r->trajectory);
  r->trajectory = NULL;
}
