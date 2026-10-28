/* Port of src/content/q3/presentation/client.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/client.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/presentation/info.h"
#include "qa/foundation/support.h"
struct qa_q3_pres_client_presentation {
  qa_q3_pres_client_game_state state;
  qa_q3_pres_client_game_static_state static_state;
  qa_q3_pres_presentation_session session;
  qa_q3_pres_client_sound sound;
  char *strings[1024];
  qa_q3_pres_presentation_frame_runtime frames;
  qa_q3_pres_client_console_runtime console;
  bool has_console, scene_scope;
  qa_q3_pres_client_presentation_options options; /* primary callbacks */
  qa_q3_pres_scene_presentation_options scene_options;
  bool closed;
};
static char *duplicate(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  if (copy) memcpy(copy, text, length);
  return copy;
}
static bool init_strings(qa_q3_pres_client_presentation *presentation) {
  for (int i = 0; i < 1024; i++) {
    presentation->strings[i] = duplicate("");
    if (!presentation->strings[i]) return false;
  }
  return true;
}
static bool register_media_sequence(qa_q3_pres_client_media *media, qa_q3_pres_client_media_host *host) {
  if (!media || !host) return true;
  if (!qa_q3_pres_register_client_loading_graphics(media)) return false;
  if (!qa_q3_pres_register_client_sounds(media, host)) return false;
  qa_q3_pres_registered_client_graphics graphics;
  if (!qa_q3_pres_register_client_graphics(media, host, &graphics)) return false;
  return qa_q3_pres_register_clients(media, host);
}
static qa_q3_pres_client_presentation *create_common(qa_q3_pres_presentation_session session,
                                                       qa_q3_pres_client_sound sound, bool scene_scope) {
  qa_q3_pres_client_presentation *presentation = calloc(1, sizeof *presentation);
  if (!presentation) return NULL;
  presentation->session = session;
  presentation->sound = sound;
  presentation->scene_scope = scene_scope;
  session.assert_current(session.ctx);
  if (!qa_q3_pres_client_game_state_init(&presentation->state, session.product, session.client_number,
                                         session.server_message_sequence)) {
    free(presentation);
    return NULL;
  }
  qa_q3_pres_client_game_static_state_init(&presentation->static_state, session.product);
  presentation->static_state.server_command_sequence = session.last_executed_server_command;
  if (!init_strings(presentation)) {
    qa_q3_pres_client_presentation_destroy(presentation);
    return NULL;
  }
  return presentation;
}
qa_q3_pres_client_presentation *qa_q3_pres_create_client_presentation(
    qa_q3_pres_client_presentation_options options) {
  qa_q3_pres_client_presentation *presentation = create_common(options.session, options.sound, false);
  if (!presentation) return NULL;
  presentation->options = options;
  if (!options.frame_host.hud || !options.frame_host.status) qa_range_error("Primary cgame lost its HUD owner");
  if (!register_media_sequence(options.media, options.media_host)) {
    qa_q3_pres_client_presentation_destroy(presentation);
    return NULL;
  }
  if (options.pool) qa_q3_pres_local_entity_pool_initialize(options.pool);
  if (options.marks) qa_q3_pres_impact_mark_system_reset(options.marks);
  if (!qa_q3_pres_set_string(&presentation->state.info_screen_text, "")) {
    qa_q3_pres_client_presentation_destroy(presentation);
    return NULL;
  }
  if (options.set_config_values && !options.set_config_values(options.ctx)) {
    qa_q3_pres_client_presentation_destroy(presentation);
    return NULL;
  }
  if (options.start_music && !options.start_music(options.ctx)) {
    qa_q3_pres_client_presentation_destroy(presentation);
    return NULL;
  }
  if (options.loading_string_done) options.loading_string_done(options.ctx);
  if (options.init_team_chat) options.init_team_chat(options.ctx);
  if (options.shader_state_changed && !options.shader_state_changed(options.ctx)) {
    qa_q3_pres_client_presentation_destroy(presentation);
    return NULL;
  }
  options.sound.clear_looping_sounds(options.sound.ctx, true);
  qa_q3_pres_presentation_frame_runtime_init(&presentation->frames, &presentation->state, options.frame_host);
  qa_q3_pres_client_console_runtime_init(&presentation->console, &presentation->state,
      &presentation->static_state, options.console_host,
      options.frame_host.view ? options.frame_host.view->state : &presentation->state,
      options.frame_host.weapons ? options.frame_host.weapons->selection.state : &presentation->state);
  presentation->has_console = true;
  options.session.assert_current(options.session.ctx);
  return presentation;
}
qa_q3_pres_client_presentation *qa_q3_pres_create_scene_presentation(
    qa_q3_pres_scene_presentation_options options) {
  qa_q3_pres_client_presentation *presentation = create_common(options.session, options.sound, true);
  if (!presentation) return NULL;
  presentation->scene_options = options;
  if (!register_media_sequence(options.media, options.media_host)) {
    qa_q3_pres_client_presentation_destroy(presentation);
    return NULL;
  }
  if (options.pool) qa_q3_pres_local_entity_pool_initialize(options.pool);
  if (options.marks) qa_q3_pres_impact_mark_system_reset(options.marks);
  if (!qa_q3_pres_set_string(&presentation->state.info_screen_text, "")) {
    qa_q3_pres_client_presentation_destroy(presentation);
    return NULL;
  }
  if (options.set_config_values && !options.set_config_values(options.ctx)) {
    qa_q3_pres_client_presentation_destroy(presentation);
    return NULL;
  }
  options.sound.clear_looping_sounds(options.sound.ctx, true);
  qa_q3_pres_presentation_frame_runtime_init_scene(&presentation->frames, &presentation->state, options.scene_host);
  options.session.assert_current(options.session.ctx);
  return presentation;
}
void qa_q3_pres_client_presentation_destroy(qa_q3_pres_client_presentation *presentation) {
  if (!presentation) return;
  for (int i = 0; i < 1024; i++) free(presentation->strings[i]);
  qa_q3_pres_client_game_static_state_destroy(&presentation->static_state);
  qa_q3_pres_client_game_state_destroy(&presentation->state);
  free(presentation);
}
qa_q3_pres_client_game_state *qa_q3_pres_client_presentation_state(qa_q3_pres_client_presentation *p) {
  return &p->state;
}
qa_q3_pres_client_game_static_state *qa_q3_pres_client_presentation_static_state(qa_q3_pres_client_presentation *p) {
  return &p->static_state;
}
qa_q3_pres_presentation_frame_runtime *qa_q3_pres_client_presentation_frames(qa_q3_pres_client_presentation *p) {
  return &p->frames;
}
qa_q3_pres_client_console_runtime *qa_q3_pres_client_presentation_console(qa_q3_pres_client_presentation *p) {
  return p->has_console ? &p->console : NULL;
}
const char *qa_q3_pres_client_presentation_config_string(qa_q3_pres_client_presentation *presentation, double index) {
  if (index == trunc(index) && (index < 0 || index >= 1024)) {
    if (index >= -9007199254740991.0 && index <= 9007199254740991.0)
      qa_range_error("CG_ConfigString: bad index: %lld", (long long)index);
    qa_range_error("CG_ConfigString: bad index: %g", index);
  }
  if (index != trunc(index)) qa_range_error("CG_ConfigString: bad index %g", index);
  return presentation->strings[(size_t)index];
}
bool qa_q3_pres_client_presentation_set_config_strings(qa_q3_pres_client_presentation *presentation,
    const char *const *strings, size_t count) {
  if (count > 1024) count = 1024;
  for (size_t i = 0; i < count; i++) {
    if (!qa_q3_pres_set_string(&presentation->strings[i], strings[i] ? strings[i] : "")) return false;
  }
  return true;
}
void qa_q3_pres_client_presentation_close(qa_q3_pres_client_presentation *presentation) {
  if (presentation->closed) return;
  presentation->closed = true;
  qa_q3_pres_presentation_frame_runtime_close(&presentation->frames);
  if (!presentation->scene_scope) {
    qa_q3_pres_client_presentation_options *options = &presentation->options;
    if (options->dispose_menus) (void)options->dispose_menus(options->ctx);
    if (presentation->has_console) qa_q3_pres_client_console_runtime_dispose(&presentation->console);
    if (options->dispose_server_commands) (void)options->dispose_server_commands(options->ctx);
    if (options->pool) qa_q3_pres_local_entity_pool_initialize(options->pool);
    if (options->marks) qa_q3_pres_impact_mark_system_reset(options->marks);
    if (options->clear_particles) options->clear_particles(options->ctx);
  } else {
    qa_q3_pres_scene_presentation_options *options = &presentation->scene_options;
    if (options->pool) qa_q3_pres_local_entity_pool_initialize(options->pool);
    if (options->marks) qa_q3_pres_impact_mark_system_reset(options->marks);
    if (options->clear_particles) options->clear_particles(options->ctx);
  }
  presentation->sound.clear_looping_sounds(presentation->sound.ctx, true);
}
bool qa_q3_pres_client_presentation_key_event(qa_q3_pres_client_presentation *presentation, double key, bool down) {
  presentation->session.assert_current(presentation->session.ctx);
  if (presentation->scene_scope || !presentation->options.key_event) return true;
  return presentation->options.key_event(presentation->options.ctx, key, down);
}
bool qa_q3_pres_client_presentation_mouse_event(qa_q3_pres_client_presentation *presentation, double x, double y) {
  presentation->session.assert_current(presentation->session.ctx);
  if (presentation->scene_scope || !presentation->options.mouse_event) return true;
  return presentation->options.mouse_event(presentation->options.ctx, x, y);
}
bool qa_q3_pres_client_presentation_event_handling(qa_q3_pres_client_presentation *presentation, double type) {
  presentation->session.assert_current(presentation->session.ctx);
  if (presentation->scene_scope || !presentation->options.event_handling) return true;
  return presentation->options.event_handling(presentation->options.ctx, type);
}
