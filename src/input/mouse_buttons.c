/* Port of src/input/mouse-buttons.ts. GPL-2.0-or-later. */
#include "qa/input/mouse_buttons.h"

double qa_quake_mouse_button(double physical_button) {
  return physical_button == 2 ? 3 : physical_button == 3 ? 2 : physical_button;
}

double qa_physical_mouse_button(double quake_button) {
  return qa_quake_mouse_button(quake_button);
}
