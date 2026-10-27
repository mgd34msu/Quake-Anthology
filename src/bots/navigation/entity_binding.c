/* See header. GPL-2.0-or-later. */
#include "qa/bots/navigation/entity_binding.h"
bool qa_source_mover_bounds_match(qa_bounds a, qa_bounds l, qa_vec3 o, qa_vec3 e) {
  return a.min.x == l.min.x + e.x - o.x && a.max.x == l.max.x + e.x - o.x
    && a.min.y == l.min.y + e.y - o.y && a.max.y == l.max.y + e.y - o.y
    && a.min.z == l.min.z + e.z - o.z && a.max.z == l.max.z + e.z - o.z;
}
