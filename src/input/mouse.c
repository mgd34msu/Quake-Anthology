/* Port of src/input/mouse.ts. GPL-2.0-or-later. */
#include "qa/input/mouse.h"

#include <math.h>
#include <string.h>

#include "qa/foundation/support.h"

const qa_mouse_tuning qa_default_mouse_tuning = {
    3, 0, false, 0.022, 0.022, 0.8, 1, true, false, false, false};

void qa_mouse_input_init(qa_mouse_input *input, qa_mouse_tuning tuning) {
  memset(input, 0, sizeof *input);
  input->tuning_value = tuning;
}

void qa_mouse_input_init_store(qa_mouse_input *input, qa_mouse_tuning_store store) {
  memset(input, 0, sizeof *input);
  input->use_store = true;
  input->store = store;
}

void qa_mouse_input_bind_settings(qa_mouse_input *input, qa_mouse_tuning_store store) {
  input->use_store = true;
  input->store = store;
}

qa_mouse_tuning qa_mouse_input_tuning(const qa_mouse_input *input) {
  return input->use_store ? input->store.read(input->store.ctx) : input->tuning_value;
}

void qa_mouse_input_set_tuning(qa_mouse_input *input, qa_mouse_tuning value) {
  if (input->use_store) input->store.write(input->store.ctx, value);
  else input->tuning_value = value;
}

static double rounded(double value, bool binary32) {
  return binary32 ? (double)(float)value : value;
}

qa_mouse_move qa_mouse_input_sample(qa_mouse_input *input, qa_vec2 raw,
                                    double frame_milliseconds, bool strafe,
                                    bool mouse_look, double zoom_sensitivity,
                                    bool binary32) {
  if (!(frame_milliseconds > 0) || !isfinite(frame_milliseconds)) {
    qa_range_error("Mouse sample needs a positive frame duration");
  }
  qa_mouse_tuning tuning = qa_mouse_input_tuning(input);
  double x = tuning.filter ? rounded((raw.x + input->previous.x) * 0.5, binary32)
                           : rounded(raw.x, binary32);
  double y = tuning.filter ? rounded((raw.y + input->previous.y) * 0.5, binary32)
                           : rounded(raw.y, binary32);
  input->previous = raw;
  double rate = rounded(sqrt(rounded(rounded(x * x, binary32) +
                                      rounded(y * y, binary32), binary32)) /
                            rounded(frame_milliseconds, binary32),
                        binary32);
  double gain = rounded(rounded(tuning.sensitivity +
                                    rounded(rate * tuning.acceleration, binary32),
                                binary32) *
                            zoom_sensitivity,
                        binary32);
  x = rounded(x * gain, binary32);
  y = rounded(y * gain, binary32);
  bool horizontal_strafe = strafe || (tuning.look_strafe && mouse_look);
  qa_mouse_move move;
  move.yaw = horizontal_strafe ? 0 : rounded(-tuning.yaw * x, binary32);
  move.side = horizontal_strafe ? rounded(tuning.side * x, binary32) : 0;
  move.pitch = !strafe && (mouse_look || tuning.free_look)
                   ? rounded(tuning.pitch * y * (tuning.invert_pitch ? -1 : 1), binary32)
                   : 0;
  move.forward = strafe || !(mouse_look || tuning.free_look)
                     ? rounded(-tuning.forward * y, binary32)
                     : 0;
  return move;
}

void qa_mouse_input_clear(qa_mouse_input *input) {
  input->previous.x = 0;
  input->previous.y = 0;
}
