/* Port of src/app/bootstrap/network/q2-layout.ts. Supersedes the
 * Q2ApplicationLayout mirror in qa/ui/hud/q2_native.h (parent
 * reconciles). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_Q2_LAYOUT_H
#define QA_APP_BOOTSTRAP_NETWORK_Q2_LAYOUT_H
#include <stdbool.h>
#include "qa/contracts/protocol.h"
typedef struct qa_app_q2_layout {
  double models, sounds, images, lights, items, player_skins;
  double max_models, max_sounds, max_images, max_config_strings;
  double map_checksum, max_clients, air_accelerate;
  bool has_n64_physics; double n64_physics;
} qa_app_q2_layout;
qa_app_q2_layout qa_app_q2_application_layout(qa_q2_protocol_identity protocol);
#endif
