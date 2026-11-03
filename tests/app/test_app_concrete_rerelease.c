/*
 * ApplicationRereleasePresentation (src/app/bootstrap/rerelease-presentation.ts) over real ApplicationAssets of the
 * retail Q2 rerelease baseq2 content: the scenario of tests/app/oracles/app_concrete_rerelease.ts, whose bun transcript
 * (app_concrete_rerelease.txt) this test reproduces line for line (retail loc_*.txt catalogs per seat language, player
 * name tokens, story/print/sky/fog/item-visibility routing, image-refresh skies, the language binding, drawStory). The
 * story layout/draw and the binding's failed callback check sentinel contexts. Needs QTS_TEST_CORPUS.
 * GPL-2.0-or-later.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/app/bootstrap/assets.h"
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/options.h"
#include "qa/app/bootstrap/rerelease_presentation.h"
#include "qa/content/q2/base/player/types.h"
#include "qa/content/q2/rerelease/types.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                       \
  do {                                                                    \
    checks++;                                                             \
    if (!(cond)) {                                                        \
      failures++;                                                         \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
    }                                                                     \
  } while (0)
#define N(value) QA_JS_NUMBER(value)

static char transcript[32768];
static void out(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void out(const char *format, ...) {
  char line[4096];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof line, format, args);
  va_end(args);
  strncat(transcript, line, sizeof transcript - strlen(transcript) - 2);
  strcat(transcript, "\n");
}
/* JSON.stringify of a string (ASCII escapes; UTF-8 passes through). */
static const char *json(const char *text, char *buffer, size_t capacity) {
  size_t used = 0;
  buffer[used++] = '"';
  for (const char *p = text; *p != '\0' && used + 8 < capacity; p++) {
    if (*p == '\n') { buffer[used++] = '\\'; buffer[used++] = 'n'; }
    else if (*p == '"' || *p == '\\') { buffer[used++] = '\\'; buffer[used++] = *p; }
    else buffer[used++] = *p;
  }
  buffer[used++] = '"';
  buffer[used] = '\0';
  return buffer;
}

static qa_app_rerelease_presentation *presentation;
static const char *content_id;
static qa_identity_owner ids;

static void images(const qa_q2_sky_view *sky, char *buffer, size_t capacity) {
  buffer[0] = '\0';
  for (size_t i = 0; i < sky->image_count; i++) {
    const qa_renderer_image *image = &sky->images[i];
    char entry[512];
    snprintf(entry, sizeof entry, "%s%s:%sx%s", i == 0 ? "" : " ",
        image->source_is_resource ? image->source_resource.requested_path : image->source_name, N(image->width), N(image->height));
    strncat(buffer, entry, capacity - strlen(buffer) - 1);
  }
}
static void show_view(const char *label, qa_actor_id actor, double seconds) {
  qa_app_rerelease_view view = qa_app_rerelease_view_of(presentation, actor, seconds);
  char fog[256] = "-", sky[4096] = "-";
  if (view.has_fog)
    snprintf(fog, sizeof fog, "%s %s %s %s", N(view.fog.fog.density), N(view.fog.fog.color.x), N(view.fog.fog.color.y),
        N(view.fog.fog.color.z));
  if (view.sky != NULL) {
    char list[3800];
    images(view.sky, list, sizeof list);
    snprintf(sky, sizeof sky, "%s %s %s %s", N(view.sky->rotation), view.sky->auto_rotate ? "true" : "false", N(view.sky->axis.z), list);
  }
  out("%s fog %s sky %s", label, fog, sky);
}
static void show_prints(const char *label) {
  size_t count = 0;
  const qa_app_sim_presentation_event *prints = qa_app_rerelease_drain_prints(presentation, &count);
  for (size_t i = 0; i < count; i++) {
    const qa_app_sim_presentation_event *print = &prints[i];
    if (strcmp(print->kind, "q2-player") != 0 || strcmp(print->q2_player->kind, "print") != 0) continue;
    char text[1024], recipient[32] = "-", entity[32] = "-";
    if (print->has_recipient) snprintf(recipient, sizeof recipient, "%lld", (long long)print->recipient.slot);
    if (print->has_source_entity) snprintf(entity, sizeof entity, "%s", N(print->source_entity));
    out("%s print %lld %s %s seq %s recipient %s entity %s", label, (long long)print->q2_player->target.slot, print->q2_player->level,
        json(print->q2_player->text, text, sizeof text), N(print->sequence), recipient, entity);
  }
}

typedef struct story_ctx { int sentinel; const char *label; } story_ctx;
static story_ctx *story_of(void *ctx) {
  story_ctx *story = ctx;
  if (story->sentinel != 0x5e47) { fprintf(stderr, "wrong story ctx\n"); abort(); }
  return story;
}
static void story_layout(void *ctx, const char *text, double scale, double *out_width, double *out_height) {
  (void)story_of(ctx);
  /* the oracle's layout: UTF-16 length * 8 * scale (Cyrillic is two UTF-8 bytes, one UTF-16 unit) */
  size_t units = 0;
  for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; p++) if ((*p & 0xc0) != 0x80) units++;
  *out_width = (double)units * 8 * scale;
  *out_height = 8 * scale;
}
static void story_draw(void *ctx, const char *text, double scale, double max_width, double x, double y) {
  story_ctx *story = story_of(ctx);
  char buffer[1024];
  out("%s draw %s max %s at %s %s scale %s", story->label, json(text, buffer, sizeof buffer), N(max_width), N(x), N(y), N(scale));
}
typedef struct draw_call { qa_actor_id actor; qa_seat_id seat; story_ctx *story; } draw_call;
static void draw_body(void *ctx) {
  draw_call *call = ctx;
  qa_app_rerelease_draw draw = {call->story, 320, 120, call->seat, story_layout, story_draw};
  qa_app_rerelease_draw_story(presentation, call->actor, draw, 1);
}
static void story(const char *label, qa_actor_id actor, qa_seat_id seat) {
  out("%s active %s", label, qa_app_rerelease_story_active(presentation, actor) ? "true" : "false");
  story_ctx ctx = {0x5e47, label};
  draw_call call = {actor, seat, &ctx};
  qa_caught_error caught;
  if (!qa_try(draw_body, &call, &caught)) out("%s error %s", label, caught.message);
}

static qa_q2_rerelease_event rerelease_events[16];
static qa_q2_player_event player_events[16];
static qa_app_sim_presentation_event events[16];
static size_t event_count;
static qa_app_sim_presentation_event *push_event(double sequence) {
  qa_app_sim_presentation_event *event = &events[event_count];
  memset(event, 0, sizeof *event);
  memset(&rerelease_events[event_count], 0, sizeof rerelease_events[event_count]);
  event->kind = "q2-rerelease";
  event->q2_rerelease = &rerelease_events[event_count];
  event->content = content_id;
  event->seconds = 1;
  event->sequence = sequence;
  event_count++;
  return event;
}
static qa_q2_rerelease_event *rerelease(qa_app_sim_presentation_event *event) { return (qa_q2_rerelease_event *)event->q2_rerelease; }
static void userinfo(double sequence, double slot, const char *name, const qa_actor_id *recipient) {
  qa_app_sim_presentation_event *event = push_event(sequence);
  qa_q2_player_event *player = &player_events[event_count - 1];
  memset(player, 0, sizeof *player);
  player->kind = "userinfo"; player->slot = slot; player->name = name;
  event->kind = "q2-player";
  event->q2_rerelease = NULL;
  event->q2_player = player;
  if (recipient != NULL) { event->has_recipient = true; event->recipient = *recipient; }
}
static void deliver(void) {
  qa_app_rerelease_receive(presentation, events, event_count);
  event_count = 0;
}

typedef struct failed_ctx { int sentinel; int count; } failed_ctx;
static void binding_failed(void *ctx, const char *error) {
  failed_ctx *failed = ctx;
  if (failed->sentinel != 0xfa11) { fprintf(stderr, "wrong failed ctx\n"); abort(); }
  (void)error;
  failed->count++;
}
typedef struct localize_call { qa_seat_id seat; } localize_call;
static void localize_unknown(void *ctx) {
  localize_call *call = ctx;
  free(qa_app_rerelease_localize_message(presentation, call->seat, content_id, "x", NULL, 0));
}
typedef struct write_call { qa_ui_setting_binding *binding; const char *value; } write_call;
static void write_body(void *ctx) {
  write_call *call = ctx;
  call->binding->write_str(call->binding->ctx, call->value);
}
static const void *owner_identity;

int main(void) {
  const char *root = getenv("QTS_TEST_CORPUS");
  if (root == NULL) { printf("skip: QTS_TEST_CORPUS not set\n0 checks, 0 failures\n"); return 0; }
  const char *argv[] = {"--game", "q2-rerelease-baseq2", "--map", "base1", "--dedicated", "--content-root", root};
  qa_content_root_search search = {NULL, NULL};
  qa_app_application_command command = qa_app_parse_application_command(argv, 7, search, NULL);
  CHECK(strcmp(command.kind, "run") == 0);
  qa_app_loaded_content *content = qa_app_load_application_content_native((qa_app_content_host){0}, command.options, NULL, false,
      NULL, NULL, NULL);
  memset(&command.options, 0, sizeof command.options);
  qa_app_application_command_destroy(&command);
  CHECK(qa_identity_owner_create(&ids, "rerelease-oracle"));
  qa_renderer_resource_owner owner = {&owner_identity, ids.session, 0};
  qa_app_application_assets_options asset_options;
  memset(&asset_options, 0, sizeof asset_options);
  qa_app_application_assets *assets = qa_app_application_assets_create(content, owner, NULL, &asset_options);
  content_id = qa_app_loaded_content_recipe(content)->map.entities.content;
  qa_actor_id first = qa_identity_owner_actor(&ids, 1, 0), second = qa_identity_owner_actor(&ids, 2, 0);
  qa_actor_id item = qa_identity_owner_actor(&ids, 40, 0);
  qa_seat_id seat0 = qa_identity_owner_seat(&ids, 0), seat1 = qa_identity_owner_seat(&ids, 1);
  qa_app_rerelease_seat seats[] = {{seat0, first, NULL}, {seat1, second, "russian"}};
  presentation = qa_app_rerelease_presentation_create(assets, seats, 2, NULL);
  CHECK(presentation != NULL);
  char l0[64], l1[64];
  snprintf(l0, sizeof l0, "%s", qa_app_rerelease_selected_language(presentation, seat0));
  snprintf(l1, sizeof l1, "%s", qa_app_rerelease_selected_language(presentation, seat1));
  out("language %s %s %s", l0, l1, qa_app_rerelease_selected_language(presentation, qa_identity_owner_seat(&ids, 3)));

  userinfo(1, 0, "Ranger", NULL);
  userinfo(2, 1, "Private", &first);
  rerelease(push_event(3))->kind = "story";
  rerelease(&events[2])->text = "$m_single_player";
  static const char *const exited0[] = {"##P0"}, *const exited1[] = {"##P1"};
  qa_q2_rerelease_event *print = rerelease(push_event(4));
  print->kind = "localized-print"; print->has_actor = true; print->actor = first; print->level = "chat";
  print->text = "$g_exited_level"; print->args = exited0; print->arg_count = 1;
  print = rerelease(push_event(5));
  print->kind = "localized-print"; print->level = "medium"; print->text = "$g_exited_level"; print->args = exited1; print->arg_count = 1;
  qa_q2_rerelease_event *sky = rerelease(push_event(6));
  sky->kind = "sky"; sky->sky_name = "unit1_"; sky->rotation = 90; sky->auto_rotate = false; sky->axis = (qa_vec3){0, 0, 1};
  qa_q2_rerelease_event *visibility = rerelease(push_event(7));
  visibility->kind = "item-visibility"; visibility->has_actor = true; visibility->actor = second;
  visibility->has_item = true; visibility->item = item; visibility->visible = false;
  qa_q2_rerelease_event *fog = rerelease(push_event(8));
  fog->kind = "fog"; fog->has_actor = true; fog->actor = first; fog->transition_milliseconds = 1000;
  fog->fog_value = qa_q2_create_fog();
  fog->fog_value.fog = (qa_q2_fog){1, {0.5, 1, 0}, 0.5};
  fog->fog_value.height_fog.start_color = (qa_vec3){1, 0, 0}; fog->fog_value.height_fog.start_distance = 0;
  fog->fog_value.height_fog.end_color = (qa_vec3){0, 0, 1}; fog->fog_value.height_fog.end_distance = 64;
  fog->fog_value.height_fog.falloff = 0; fog->fog_value.height_fog.density = 0;
  deliver();
  out("visible %s %s %s", qa_app_rerelease_item_visible(presentation, second, item) ? "true" : "false",
      qa_app_rerelease_item_visible(presentation, first, item) ? "true" : "false",
      qa_app_rerelease_item_visible(presentation, qa_identity_owner_actor(&ids, 9, 0), item) ? "true" : "false");
  qa_app_rerelease_prepare(presentation);
  show_prints("first");
  story("story0", first, seat0);
  story("story1", second, seat1);
  story("wrong", first, seat1);
  show_view("view0", first, 1.5);
  show_view("view1", second, 1.5);

  sky = rerelease(push_event(9));
  sky->kind = "sky"; sky->sky_name = "space1"; sky->rotation = 45; sky->auto_rotate = true; sky->axis = (qa_vec3){0, 0, 1};
  events[0].has_recipient = true; events[0].recipient = second;
  qa_app_sim_presentation_event *cleared = push_event(10);
  rerelease(cleared)->kind = "story"; rerelease(cleared)->text = "";
  cleared->has_recipient = true; cleared->recipient = first;
  qa_app_sim_presentation_event *private_print = push_event(11);
  rerelease(private_print)->kind = "localized-print"; rerelease(private_print)->level = "chat";
  rerelease(private_print)->text = "Private text";
  private_print->has_recipient = true; private_print->recipient = first;
  private_print->has_source_entity = true; private_print->source_entity = 17;
  deliver();
  qa_app_rerelease_prepare(presentation);
  show_prints("second");
  story("story0b", first, seat0);
  show_view("view0b", first, 2);
  show_view("view1b", second, 2);
  char buffer[1024];
  char *text = qa_app_rerelease_localize_message(presentation, seat1, content_id, "$g_exited_level", exited1, 1);
  out("localize %s", json(text, buffer, sizeof buffer));
  free(text);
  qa_app_rerelease_localizer *localizer = qa_app_rerelease_source_localizer(presentation, seat0, content_id);
  char *single = qa_app_rerelease_localizer_text(localizer, "$m_single_player", NULL, 0);
  char *plain = qa_app_rerelease_localizer_text(localizer, "plain", NULL, 0);
  char buffer2[1024];
  out("source %s %s", json(single, buffer, sizeof buffer), json(plain, buffer2, sizeof buffer2));
  free(single); free(plain);
  localize_call unknown = {qa_identity_owner_seat(&ids, 5)};
  qa_caught_error caught;
  if (!qa_try(localize_unknown, &unknown, &caught)) out("unknown %s", caught.message);
  failed_ctx failed = {0xfa11, 0};
  qa_ui_setting_binding binding;
  CHECK(qa_app_rerelease_language_binding(presentation, seat0, content_id, binding_failed, &failed, &binding));
  CHECK(strcmp(binding.kind, "choice") == 0);
  size_t choice_count = 0;
  const qa_ui_choice *choices = binding.choices(binding.ctx, &choice_count);
  char list[1024] = "";
  for (size_t i = 0; i < choice_count; i++) {
    char entry[128];
    snprintf(entry, sizeof entry, "%s%s/%s", i == 0 ? "" : ",", choices[i].id, choices[i].label);
    strncat(list, entry, sizeof list - strlen(list) - 1);
  }
  out("binding %s %s %s %s", binding.id, binding.label, binding.read_str(binding.ctx), list);
  binding.write_str(binding.ctx, "french");
  char read[64];
  snprintf(read, sizeof read, "%s", binding.read_str(binding.ctx));
  single = qa_app_rerelease_localizer_text(localizer, "$m_single_player", NULL, 0);
  out("written %s %s %s", read, qa_app_rerelease_selected_language(presentation, seat0), json(single, buffer, sizeof buffer));
  free(single);
  text = qa_app_rerelease_localize_message(presentation, seat0, content_id, "$m_single_player", NULL, 0);
  out("relocalize %s", json(text, buffer, sizeof buffer));
  free(text);
  write_call bad = {&binding, "klingon"};
  if (!qa_try(write_body, &bad, &caught)) out("write %s", caught.message);
  out("failures %d", failed.count);
  qa_app_rerelease_prepare(presentation);
  story("story1c", second, seat1);
  qa_scene_image_policy policy = qa_default_image_policy();
  qa_app_prepared_images *prepared = qa_app_application_assets_prepare_image_refresh(assets, &policy, NULL);
  qa_app_rerelease_refresh *refresh = qa_app_rerelease_prepare_image_refresh(presentation, prepared);
  qa_app_rerelease_refresh_publish(refresh);
  qa_app_prepared_images_commit(prepared);
  qa_app_application_assets_finish_image_refresh(assets);
  show_view("refresh0", first, 2);
  show_view("refresh1", second, 2);
  qa_app_rerelease_seat published[] = {{seat1, second, NULL}, {qa_identity_owner_seat(&ids, 2), qa_identity_owner_actor(&ids, 3, 0), NULL}};
  qa_app_rerelease_publish_seats(presentation, published, 2);
  snprintf(l1, sizeof l1, "%s", qa_app_rerelease_selected_language(presentation, seat1));
  out("published %s %s %s %s", qa_app_rerelease_story_active(presentation, first) ? "true" : "false",
      qa_app_rerelease_story_active(presentation, second) ? "true" : "false", l1, qa_app_rerelease_selected_language(presentation, seat0));
  show_view("published1", second, 2);
  qa_app_rerelease_presentation_destroy(presentation);
  qa_app_application_assets_close(assets);
  qa_app_application_assets_destroy(assets);
  qa_app_loaded_content_close(content);
  qa_app_loaded_content_destroy(content);
  qa_identity_owner_destroy(&ids);

  FILE *file = fopen("tests/app/oracles/app_concrete_rerelease.txt", "r");
  CHECK(file != NULL);
  if (file != NULL) {
    static char expected[32768];
    size_t length = fread(expected, 1, sizeof expected - 1, file);
    expected[length] = '\0';
    fclose(file);
    CHECK(strcmp(expected, transcript) == 0);
    if (strcmp(expected, transcript) != 0) fprintf(stderr, "expected:\n%s\nactual:\n%s\n", expected, transcript);
  }
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
