/* Port of src/app/bootstrap/remote-components.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/remote_components.h"
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/assets.h"
#include "qa/app/bootstrap/audio.h"
#include "qa/app/bootstrap/component_media.h"
#include "qa/app/bootstrap/presentation_state.h"
#include "qa/foundation/support.h"

/* component-media.ts's (events, audio, assets) arguments over one PresentationState. */
typedef struct media_ref {
  qa_app_presentation_state *state;
  qa_app_application_audio *audio;
  qa_app_application_assets *assets;
} media_ref;
static qa_app_presentation_state *state_of(void *ctx) { return ((media_ref *)ctx)->state; }
static const qa_app_retained_presentation *const *media_pending_local_media(void *ctx, size_t *out_count) {
  return qa_app_presentation_state_pending_local_media(state_of(ctx), out_count);
}
static double media_applied_media_sequence(void *ctx) { return qa_app_presentation_state_applied_media_sequence(state_of(ctx)); }
static bool media_local_media_current(void *ctx, const qa_app_retained_presentation *request) {
  return qa_app_presentation_state_local_media_current(state_of(ctx), request);
}
static void media_acknowledge_local_media(void *ctx, const qa_app_retained_presentation *request, bool committed) {
  qa_app_presentation_state_acknowledge_local_media(state_of(ctx), request, committed);
}
static void media_applied_media(void *ctx, double sequence) { qa_app_presentation_state_applied_media(state_of(ctx), sequence); }
static void media_applied_shader(void *ctx, const char *original, double sequence) {
  qa_app_presentation_state_applied_shader(state_of(ctx), original, sequence);
}
static const qa_app_retained_presentation *media_resolve_shader_replay(void *ctx, const qa_app_retained_presentation *request) {
  return qa_app_presentation_state_resolve_shader_replay(state_of(ctx), request);
}
static void media_enable_local_media(void *ctx) { qa_app_presentation_state_enable_local_media(state_of(ctx)); }
static void media_publish_local_media(void *ctx, const qa_presentation_owner *owner, qa_content_id content,
    const qa_component_presentation_media_request *event, bool initializing, qa_app_media_current_fn current,
    void *current_ctx, void (*release_current)(void *current_ctx)) {
  qa_app_presentation_state_publish_local_media(state_of(ctx), owner, content, event, initializing, current, current_ctx,
      release_current);
}
static const char *media_play_component_media(void *ctx, const qa_app_local_presentation_media *source,
    qa_app_media_current_fn active, void *active_ctx) {
  qa_app_application_audio_play_component_media(((media_ref *)ctx)->audio, source->has_owner ? &source->owner : NULL,
      source->content, &source->event, active, active_ctx);
  return NULL;
}
static const char *media_audio_receive(void *ctx, const qa_app_sim_presentation_event *events, size_t count,
    qa_audio_audience audience, bool music) {
  qa_app_application_audio_receive(((media_ref *)ctx)->audio, events, count, audience, music);
  return NULL;
}
static qa_world_scene *media_world(void *ctx) { return qa_app_application_assets_world(((media_ref *)ctx)->assets); }
static qa_scene_shader_registry *media_provider_shaders(void *ctx, qa_content_id content, const char **out_error) {
  *out_error = NULL;
  return qa_app_application_assets_provider(((media_ref *)ctx)->assets, content).shaders;
}
static qa_app_component_media_host media_host(media_ref *ref) {
  qa_app_component_media_host host;
  memset(&host, 0, sizeof host);
  host.ctx = ref;
  host.pending_local_media = media_pending_local_media;
  host.applied_media_sequence = media_applied_media_sequence;
  host.local_media_current = media_local_media_current;
  host.acknowledge_local_media = media_acknowledge_local_media;
  host.applied_media = media_applied_media;
  host.applied_shader = media_applied_shader;
  host.resolve_shader_replay = media_resolve_shader_replay;
  host.enable_local_media = media_enable_local_media;
  host.publish_local_media = media_publish_local_media;
  host.play_component_media = media_play_component_media;
  host.audio_receive = media_audio_receive;
  host.world = media_world;
  host.provider_shaders = media_provider_shaders;
  return host;
}

typedef struct queued_command { qa_app_component_client_command_request request; char *name; char **arguments; } queued_command;
struct qa_app_remote_component_view {
  qa_app_unified_remote *remote;
  qa_app_mod_presentations_options options; /* the caller's (TS this.options) */
  media_ref control;                        /* componentMediaControl(remote.media, ...) at construction */
  media_ref current;                        /* this.remote.media at each prepareMedia */
  qa_app_application_mod_presentations *clients;
  queued_command *commands; size_t command_count, command_capacity;
  bool closed;
};
static void queued_command_free(queued_command *command) {
  free(command->name);
  for (size_t i = 0; i < command->request.argument_count; i++) free(command->arguments[i]);
  free(command->arguments);
  qa_command_context_release(&command->request.source);
  memset(command, 0, sizeof *command);
}
static void commands_free(queued_command *commands, size_t count) {
  for (size_t i = 0; i < count; i++) queued_command_free(&commands[i]);
  free(commands);
}
static char *dup_text(const char *text) {
  size_t length = strlen(text != NULL ? text : "") + 1;
  char *copy = malloc(length);
  if (copy == NULL) qa_range_error("Out of memory");
  memcpy(copy, text != NULL ? text : "", length);
  return copy;
}

/* The options the view hands ApplicationModPresentations: its own presentationMedia and queueCommand, the
 * caller's other members forwarded with the caller's ctx. */
static void view_print(void *ctx, const char *text) {
  qa_app_remote_component_view *view = ctx;
  view->options.print(view->options.ctx, text);
}
static void view_next_frame(void *ctx) {
  qa_app_remote_component_view *view = ctx;
  view->options.next_frame(view->options.ctx);
}
static const qa_app_system_cinematic_host *view_system_cinematics(void *ctx, const qa_active_mod_presentation *source,
    const qa_app_viewing_seat *presentation, const qa_app_component_cinematic_scope *scope) {
  qa_app_remote_component_view *view = ctx;
  return view->options.system_cinematics(view->options.ctx, source, presentation, scope);
}
/* presentationMedia: componentMediaControl(remote.media, options.audio, options.assets). */
static void view_presentation_media(void *ctx, const qa_active_mod_presentation *source,
    const qa_component_presentation_media_request *request, bool initializing, bool (*current)(void *current_ctx),
    void *current_ctx) {
  qa_app_remote_component_view *view = ctx;
  const char *error = qa_app_component_media_control(media_host(&view->control), source, request, initializing, current,
      current_ctx);
  if (error != NULL) qa_raise("Error", 0, 0, "%s", error);
}
/* queueCommand: request => { if (this.closed) throw ...; this.commands.push(request); } */
static void view_queue_command(void *ctx, const qa_app_component_client_command_request *request) {
  qa_app_remote_component_view *view = ctx;
  if (view->closed) qa_raise("Error", 0, 0, "Remote component view is retired");
  if (view->command_count == view->command_capacity) {
    size_t capacity = view->command_capacity ? view->command_capacity * 2 : 8;
    queued_command *grown = realloc(view->commands, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    view->commands = grown;
    view->command_capacity = capacity;
  }
  queued_command *command = &view->commands[view->command_count];
  memset(command, 0, sizeof *command);
  command->request = *request;
  command->request.argument_count = 0;
  command->request.arguments = NULL;
  command->request.source = qa_command_context_copy(&request->source);
  view->command_count++;
  command->name = dup_text(request->name);
  command->request.name = command->name;
  command->arguments = calloc(request->argument_count + 1, sizeof *command->arguments);
  if (command->arguments == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < request->argument_count; i++) {
    command->arguments[i] = dup_text(request->arguments[i]);
    command->request.argument_count = i + 1;
  }
  command->request.arguments = (const char *const *)command->arguments;
}

qa_app_remote_component_view *qa_app_remote_component_view_create(qa_app_unified_remote *remote,
    const qa_app_mod_presentations_options *options) {
  qa_app_remote_component_view *view = calloc(1, sizeof *view);
  if (view == NULL) return NULL;
  view->remote = remote;
  view->options = *options;
  view->control = (media_ref){qa_app_unified_remote_media(remote), options->audio, options->assets};
  /* componentMediaControl's eager events.enableLocalMedia(). */
  qa_app_component_media_control_enable(media_host(&view->control));
  qa_app_mod_presentations_options clients = *options;
  clients.ctx = view;
  clients.print = options->print != NULL ? view_print : NULL;
  clients.next_frame = options->next_frame != NULL ? view_next_frame : NULL;
  clients.system_cinematics = options->system_cinematics != NULL ? view_system_cinematics : NULL;
  clients.presentation_media = view_presentation_media;
  clients.queue_command = view_queue_command;
  view->clients = qa_app_application_mod_presentations_create(&clients);
  if (view->clients == NULL) { free(view); return NULL; }
  return view;
}
void qa_app_remote_component_view_destroy(qa_app_remote_component_view *view) {
  if (view == NULL) return;
  qa_app_remote_component_view_close(view);
  qa_app_application_mod_presentations_destroy(view->clients);
  free(view);
}
bool qa_app_remote_component_view_pending_commands(qa_app_remote_component_view *view) {
  return view->command_count != 0 || qa_app_application_mod_presentations_pending_commands(view->clients);
}
typedef struct taken_commands { queued_command *commands; size_t count; } taken_commands;
static void taken_commands_free(void *arg) {
  taken_commands *taken = arg;
  commands_free(taken->commands, taken->count);
  taken->commands = NULL; taken->count = 0;
}
typedef struct owner_events { qa_app_sim_presentation_event *events; } owner_events;
static void owner_events_free(void *arg) { free(((owner_events *)arg)->events); }
void qa_app_remote_component_view_prepare_media(qa_app_remote_component_view *view,
    const qa_app_sim_presentation_event *events, size_t event_count, bool music) {
  if (view->closed) qa_raise("Error", 0, 0, "Remote component view is retired");
  taken_commands taken = {view->commands, view->command_count};
  view->commands = NULL; view->command_count = 0; view->command_capacity = 0;
  qa_cleanup guard;
  qa_cleanup_push(&guard, taken_commands_free, &taken);
  for (size_t i = 0; i < taken.count; i++)
    (void)qa_app_application_mod_presentations_dispatch_command(view->clients, &taken.commands[i].request);
  qa_cleanup_pop(&guard);
  taken_commands_free(&taken);
  view->current = (media_ref){qa_app_unified_remote_media(view->remote), view->options.audio, view->options.assets};
  qa_app_component_media_host host = media_host(&view->current);
  owner_events filtered = {NULL};
  qa_cleanup filtered_guard;
  qa_cleanup_push(&filtered_guard, owner_events_free, &filtered);
  const qa_app_sim_presentation_event *source = events;
  size_t source_count = event_count;
  if (!music) {
    /* events.filter(event => event.kind === "presentation-owner") */
    filtered.events = calloc(event_count ? event_count : 1, sizeof *filtered.events);
    if (filtered.events == NULL) qa_range_error("Out of memory");
    source_count = 0;
    for (size_t i = 0; i < event_count; i++)
      if (strcmp(events[i].kind, "presentation-owner") == 0) filtered.events[source_count++] = events[i];
    source = filtered.events;
  }
  const char *error = qa_app_prepare_presentation_audio(host, source, source_count, NULL, 0);
  if (error != NULL) qa_raise("Error", 0, 0, "%s", error);
  qa_cleanup_pop(&filtered_guard);
  owner_events_free(&filtered);
  error = qa_app_prepare_presentation_shaders(host);
  if (error != NULL) qa_raise("Error", 0, 0, "%s", error);
}
typedef struct source_pointers { const qa_active_mod_presentation **items; } source_pointers;
static void source_pointers_free(void *arg) { free(((source_pointers *)arg)->items); }
void qa_app_remote_component_view_prepare(qa_app_remote_component_view *view, qa_app_world_seat_presentation *presentation,
    const qa_app_sim_presentation_event *events, size_t event_count, double frame) {
  if (view->closed) qa_raise("Error", 0, 0, "Remote component view is retired");
  qa_app_viewing_seat seat = qa_app_world_seat_presentation_viewing_seat(presentation);
  size_t count = 0;
  const qa_active_mod_presentation *sources = qa_app_unified_remote_mod_presentation_sources(view->remote, &count);
  source_pointers pointers = {calloc(count ? count : 1, sizeof *pointers.items)};
  if (pointers.items == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) pointers.items[i] = &sources[i];
  qa_cleanup guard;
  qa_cleanup_push(&guard, source_pointers_free, &pointers);
  qa_app_application_mod_presentations_prepare(view->clients, &seat, 1, pointers.items, count, events, event_count, frame);
  qa_cleanup_pop(&guard);
  source_pointers_free(&pointers);
}
void qa_app_remote_component_view_close(qa_app_remote_component_view *view) {
  if (view->closed) return;
  view->closed = true;
  commands_free(view->commands, view->command_count);
  view->commands = NULL; view->command_count = 0; view->command_capacity = 0;
  qa_app_application_mod_presentations_close(view->clients);
}
