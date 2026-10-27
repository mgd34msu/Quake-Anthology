/* Port of src/content/q2/foundation/monsters/alternate-fly-state.ts. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/monsters/alternate_fly_state.h"
#include <string.h>
qa_q2m_alternate_fly_state qa_q2m_create_alternate_fly_state(void) {
  qa_q2m_alternate_fly_state state;
  memset(&state, 0, sizeof state);
  return state;
}
