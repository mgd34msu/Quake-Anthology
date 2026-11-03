/* Port of src/compat/q2/rerelease/layouts.ts. GPL-2.0-or-later.
 * Layouts are built once on first use (the TS structure() builder);
 * call qa_compat_q2_rerelease_layouts_ensure() or field_offset
 * before reading byte_length directly. */
#ifndef QA_COMPAT_Q2_RERELEASE_LAYOUTS_H
#define QA_COMPAT_Q2_RERELEASE_LAYOUTS_H
#include <stddef.h>
#include "qa/compat/q2/classic/layout.h"

void qa_compat_q2_rerelease_layouts_ensure(void);
double qa_compat_q2_rerelease_field_offset(const qa_guest_layout *layout, const char *name);
extern qa_guest_layout qa_compat_q2_rerelease_vec2_layout;
extern qa_guest_layout qa_compat_q2_rerelease_vec3_layout;
extern qa_guest_layout qa_compat_q2_rerelease_plane_layout;
extern qa_guest_layout qa_compat_q2_rerelease_surface_layout;
extern qa_guest_layout qa_compat_q2_rerelease_trace_layout;
extern qa_guest_layout qa_compat_q2_rerelease_cvar_layout;
extern qa_guest_layout qa_compat_q2_rerelease_pmove_state_layout;
extern qa_guest_layout qa_compat_q2_rerelease_usercmd_layout;
extern qa_guest_layout qa_compat_q2_rerelease_touch_list_layout;
extern qa_guest_layout qa_compat_q2_rerelease_pmove_layout;
extern qa_guest_layout qa_compat_q2_rerelease_entity_state_layout;
extern qa_guest_layout qa_compat_q2_rerelease_player_state_layout;
extern qa_guest_layout qa_compat_q2_rerelease_client_layout;
extern qa_guest_layout qa_compat_q2_rerelease_height_fog_layout;
extern qa_guest_layout qa_compat_q2_rerelease_persistent_client_layout;
extern qa_guest_layout qa_compat_q2_rerelease_private_client_prefix_layout;
extern qa_guest_layout qa_compat_q2_rerelease_respawn_client_layout;
extern qa_guest_layout qa_compat_q2_rerelease_private_client_layout;
extern qa_guest_layout qa_compat_q2_rerelease_server_entity_layout;
extern qa_guest_layout qa_compat_q2_rerelease_edict_layout;
extern qa_guest_layout qa_compat_q2_rerelease_private_edict_prefix_layout;
extern qa_guest_layout qa_compat_q2_rerelease_rectangle_layout;
/* TS importTableLayout/exportTableLayout: fields are malloc'd and
 * retained for the process (the TS layouts are immortal consts). */
qa_guest_layout qa_compat_q2_rerelease_import_table_layout(const char *kind, const char *const *names, size_t count);
qa_guest_layout qa_compat_q2_rerelease_export_table_layout(const char *kind, const char *const *names, size_t count);
#endif
