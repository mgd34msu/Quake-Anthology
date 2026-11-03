/* Port of src/app/bootstrap/application.ts createBots (TS 1212-1233 in the
 * layout's numbering) and the LoadedApplicationContent views the bot modules
 * read (bot-assets.ts loadApplicationBotAssets, navigation.ts
 * createApplicationBotNavigation). Layout: docs/application_layout.md.
 *
 * C ownership: TS keeps the bot service, its assets, the navigation it
 * created and the closures alive by reference. Here one record per
 * createBots result owns them; qa_application_bots_release frees the record
 * of a closed service and qa_application_bots_free frees every record when
 * the application is destroyed. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "application_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/bot_assets.h"
#include "qa/app/bootstrap/simulation/bot_rerelease.h"
#include "qa/app/bootstrap/simulation/navigation.h"
#include "qa/app/bootstrap/simulation/network_q3.h"
#include "qa/app/bootstrap/simulation/q3/guest_runtime.h"
#include "qa/bots/behavior/assets.h"
#include "qa/content/catalog/index.h"
#include "qa/content/mounts.h"
#include "qa/formats/q1_map/types.h"
#include "qa/formats/q2_map.h"

static void *checked(void *p) {
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}

/* ---- LoadedApplicationContent views ---- */
static qa_content_mounted *content_for(qa_app_loaded_content *content, qa_content_id id) {
  return (qa_content_mounted *)qa_app_loaded_content_for_content(content, id);
}
/* loadMountedBotAssetFiles(await content.forContent(id), content.catalog). */
static bool asset_mounted(void *ctx, qa_content_id id, qa_bot_asset_files *out) {
  qa_app_loaded_content *content = ctx;
  return qa_load_mounted_bot_asset_files(out, content_for(content, id), qa_app_loaded_content_catalog(content), NULL, 0);
}
/* content.catalog.products.find(product => product.expectation.id === id && product.availability.kind === "installed")?.id */
static qa_content_id asset_installed(void *ctx, const char *expectation_id) {
  const qa_content_catalog *catalog = qa_app_loaded_content_catalog(ctx);
  for (size_t i = 0; i < catalog->product_count; i++) {
    const qa_catalog_discovered_product *product = &catalog->products[i];
    if (strcmp(product->expectation.id, expectation_id) == 0 && product->availability.kind == QA_CATALOG_INSTALLED) return product->id;
  }
  return NULL;
}
static qa_content_id asset_product(void *ctx, const char *id) {
  return qa_content_catalog_product(qa_app_loaded_content_catalog(ctx), id)->id;
}
/* (await mounts.open(path))?.bytes over the mounts the loader selected for that content. */
static uint8_t *asset_open(void *ctx, qa_content_id id, const char *path, size_t *out_length) {
  qa_content_opened_resource opened;
  memset(&opened, 0, sizeof opened);
  if (!qa_content_mounted_open(content_for(ctx, id), path, NULL, NULL, &opened)) return NULL;
  *out_length = opened.byte_length;
  return opened.bytes;
}
/* The LoadedApplicationContent members loadApplicationBotAssets reads. ctx is the content. */
qa_app_sim_bot_asset_content qa_application_bot_asset_content(qa_app_loaded_content *content) {
  qa_app_sim_bot_asset_content view;
  memset(&view, 0, sizeof view);
  view.ctx = content;
  view.recipe = qa_app_loaded_content_recipe(content);
  view.mounted = asset_mounted;
  view.installed = asset_installed;
  view.product = asset_product;
  view.open = asset_open;
  return view;
}
static qa_content_mounted *navigation_for_content(void *ctx, qa_content_id id) { return content_for(ctx, id); }
/* The LoadedApplicationContent members createApplicationBotNavigation reads. ctx is the content. */
qa_app_sim_navigation_content qa_application_navigation_content(qa_app_loaded_content *content) {
  qa_app_sim_navigation_content view;
  memset(&view, 0, sizeof view);
  view.ctx = content;
  view.recipe = qa_app_loaded_content_recipe(content);
  view.mounts = (qa_content_mounted *)qa_app_loaded_content_mounts(content);
  view.for_content = navigation_for_content;
  return view;
}
/* TS simulation.recipe identity: the C simulation keeps a value copy of the
 * recipe it was built from, so the copy of this content's recipe is the
 * content's recipe; any other recipe keeps its own address. */
static const qa_executable_recipe *simulation_recipe(qa_app_shared_simulation *simulation, qa_app_loaded_content *content) {
  const qa_executable_recipe *recipe = qa_app_loaded_content_recipe(content);
  return memcmp(recipe, &simulation->recipe, sizeof *recipe) == 0 ? recipe : &simulation->recipe;
}
/* TS content.world.leaves.length. */
static double world_leaf_count(qa_app_loaded_content *content) {
  qa_app_application_world world = qa_app_loaded_content_world(content);
  if (world.value == NULL) qa_raise("TypeError", 0, 0, "Loaded content has no world");
  if (strcmp(world.kind, "q1-bsp") == 0) return (double)((const qa_q1_map *)world.value)->world.leaf_count;
  if (strcmp(world.kind, "q2-bsp") == 0) return (double)((const qa_q2_decoded_map *)world.value)->world.leaf_count;
  return (double)((const qa_q3_world_geometry *)world.value)->leaf_count;
}

/* ---- createBots(content, simulation, setup = {}) ---- */
typedef struct application_bots {
  qa_application *app;
  qa_command_buffer *commands; /* TS commands (null = NULL) */
  qa_session_client *(*resolve_client)(void *ctx, double slot, double generation); void *resolve_ctx;
  qa_app_sim_bot_simulation simulation;
  qa_app_sim_application_bot_assets *assets;
  qa_app_sim_navigation_content navigation_content;
  qa_app_sim_navigation_simulation navigation_simulation;
  qa_app_sim_application_bot_navigation *created_navigation; /* NULL: the simulation's rereleaseNavigation() */
  qa_app_sim_bot_navigation navigation;
  qa_app_sim_bots_options options;
  qa_app_sim_rerelease_bots *rerelease;
  qa_app_sim_application_bots *q3;
  const qa_app_sim_bot_service *service;
} application_bots;

static void bots_print(void *ctx, const char *text) {
  application_bots *bots = ctx;
  qa_application_print(bots->app, text);
}
static void bots_insert_console_command(void *ctx, const char *text) {
  application_bots *bots = ctx;
  if (bots->commands == NULL) qa_raise("Error", 0, 0, "Bot console has no source command buffer");
  qa_command_buffer_insert(bots->commands, text, NULL, -1);
}
static qa_session_client *bots_resolve_client(void *ctx, double slot, double generation) {
  application_bots *bots = ctx;
  return bots->resolve_client(bots->resolve_ctx, slot, generation);
}
static qa_bot_log_open_result bots_open_log(void *ctx, const char *filename) {
  (void)ctx; /* TS openApplicationBotLog takes no application state */
  return qa_app_sim_open_application_bot_log(NULL, filename);
}
static qa_bot_log_open_result bots_resume_log(void *ctx, const char *filename, int64_t position) {
  (void)ctx; /* TS resumeApplicationBotLog takes no application state */
  return qa_app_sim_resume_application_bot_log(NULL, filename, (double)position);
}
/* The ApplicationBotNavigation createApplicationBotNavigation returned, as the bot shells take it. */
static qa_save_value *navigation_checkpoint(void *ctx) {
  qa_app_sim_bot_navigation_checkpoint checkpoint;
  if (!qa_app_sim_application_bot_navigation_checkpoint(ctx, &checkpoint)) qa_range_error("Out of memory");
  qa_save_value *value = qa_app_sim_bot_navigation_checkpoint_value(&checkpoint);
  qa_app_sim_bot_navigation_checkpoint_destroy(&checkpoint);
  return value;
}
static void navigation_restore(void *ctx, const qa_save_value *value) {
  qa_app_sim_application_bot_navigation_restore_checkpoint(ctx, value, NULL, NULL);
}
static void navigation_restart(void *ctx) { qa_app_sim_application_bot_navigation_restart_round(ctx); }

static void application_bots_destroy(application_bots *bots) {
  if (bots->rerelease != NULL) qa_app_sim_rerelease_bots_destroy(bots->rerelease);
  if (bots->q3 != NULL) qa_app_sim_application_bots_destroy(bots->q3);
  if (bots->created_navigation != NULL) qa_app_sim_application_bot_navigation_destroy(bots->created_navigation);
  qa_app_sim_application_bot_assets_free(bots->assets);
  free(bots);
}
typedef struct create_bots_attempt {
  application_bots *bots;
  qa_app_loaded_content *content;
  qa_app_shared_simulation *simulation;
  const qa_application_create_bots_setup *setup;
} create_bots_attempt;
static void create_bots_body(void *ctx) {
  create_bots_attempt *attempt = ctx;
  application_bots *bots = attempt->bots;
  qa_app_loaded_content *content = attempt->content;
  qa_app_shared_simulation *simulation = attempt->simulation;
  const qa_application_create_bots_setup *setup = attempt->setup;
  qa_app_sim_bot_asset_content asset_content = qa_application_bot_asset_content(content);
  bots->assets = checked(qa_app_sim_load_application_bot_assets_with(&asset_content, &bots->simulation, setup->restore != NULL));
  if (!qa_app_shared_sim_bot_navigation(simulation, &bots->navigation)) {
    bots->navigation_content = qa_application_navigation_content(content);
    bots->navigation_simulation = qa_app_shared_sim_navigation_simulation(simulation, simulation_recipe(simulation, content));
    bots->created_navigation = checked(qa_app_sim_create_application_bot_navigation(bots->navigation_content, bots->navigation_simulation));
    memset(&bots->navigation, 0, sizeof bots->navigation);
    bots->navigation.selected = *qa_app_sim_application_bot_navigation_selected(bots->created_navigation);
    bots->navigation.ctx = bots->created_navigation;
    bots->navigation.checkpoint = navigation_checkpoint;
    bots->navigation.restore_checkpoint = navigation_restore;
    bots->navigation.restart_round = navigation_restart;
  }
  const qa_ss_source_runtime *q1 = qa_app_shared_sim_q1_source(simulation);
  qa_app_sim_bots_options *options = &bots->options;
  memset(options, 0, sizeof *options);
  options->session = attempt->bots->app->session;
  options->simulation = &bots->simulation;
  options->files = bots->assets->files;
  options->navigation = &bots->navigation;
  if (setup->restore == NULL) {
    options->clients = setup->clients; options->client_count = setup->client_count;
    options->restart = setup->restart;
  } else options->restore = setup->restore;
  options->automatic_frame = true;
  options->configuration = q1 != NULL ? q1->q1_cvars : qa_app_shared_sim_q2_server_cvars(simulation);
  options->leaf_count = world_leaf_count(content);
  options->ctx = bots;
  options->print = bots_print;
  options->insert_console_command = bots_insert_console_command;
  options->resolve_client = setup->resolve_client == NULL ? NULL : bots_resolve_client;
  options->open_log = bots_open_log;
  options->resume_log = bots_resume_log;
  bots->service = qa_app_sim_create_application_bots(options, bots->assets, &bots->rerelease, &bots->q3);
}
qa_app_sim_bot_service *qa_application_create_bots(qa_application *app, qa_app_loaded_content *content,
    qa_app_shared_simulation *simulation, const qa_application_create_bots_setup *setup) {
  qa_application_create_bots_setup defaults;
  if (setup == NULL) { memset(&defaults, 0, sizeof defaults); setup = &defaults; }
  bool requested = setup->requested;
  if (qa_app_shared_sim_q3_source(simulation) == NULL && setup->client_count == 0 && !requested && setup->restore == NULL) return NULL;
  application_bots *bots = checked(calloc(1, sizeof *bots));
  bots->app = app;
  bots->commands = setup->has_commands ? setup->commands : app->source_commands;
  bots->resolve_client = setup->resolve_client;
  bots->resolve_ctx = setup->resolve_ctx;
  bots->simulation = qa_app_shared_sim_bot_simulation(simulation);
  const char *unsupported = qa_app_sim_bot_admission_error(&bots->simulation);
  if (unsupported != NULL) {
    free(bots);
    if (setup->client_count != 0 || requested || setup->restore != NULL) qa_raise("Error", 0, 0, "%s", unsupported);
    return NULL;
  }
  create_bots_attempt attempt = { bots, content, simulation, setup };
  qa_caught_error caught;
  if (!qa_try(create_bots_body, &attempt, &caught)) {
    application_bots_destroy(bots);
    qa_rethrow(&caught);
  }
  if (app->bot_record_count == app->bot_record_capacity) {
    size_t next = app->bot_record_capacity == 0 ? 4 : app->bot_record_capacity * 2;
    app->bot_records = checked(realloc(app->bot_records, next * sizeof *app->bot_records));
    app->bot_record_capacity = next;
  }
  app->bot_records[app->bot_record_count++] = bots;
  /* The service is the TS ApplicationBotService; its storage is the record's. */
  return (qa_app_sim_bot_service *)bots->service;
}
/* C-only: frees the record of a bot service the application dropped (after its close). */
void qa_application_bots_release(qa_application *app, const qa_app_sim_bot_service *service) {
  for (size_t i = 0; i < app->bot_record_count; i++) {
    application_bots *bots = app->bot_records[i];
    if (bots->service != service) continue;
    application_bots_destroy(bots);
    app->bot_records[i] = app->bot_records[--app->bot_record_count];
    return;
  }
}
/* ---- prepareGuestBots(content, simulation) (TS 1820-1859) ---- */
/* C ownership: the guest bots borrow the files, the navigation and these closures for
 * the guest game's life; the record keeps them until the application is destroyed. */
typedef struct guest_bot_record {
  qa_application *app;
  qa_app_shared_simulation *simulation;
  qa_app_sim_bot_simulation bot_simulation;
  qa_app_sim_application_bot_assets *assets;
  qa_app_sim_navigation_content navigation_content;
  qa_app_sim_navigation_simulation navigation_simulation;
  qa_app_sim_application_bot_navigation *navigation;
  qa_app_sim_bot_navigation selected;
  qa_app_sim_network_simulation authority_simulation;
  qa_app_sim_network_content authority_content;
  qa_app_sim_q3_server_authority authority;
  bool has_authority;
} guest_bot_record;
static void guest_bot_record_destroy(guest_bot_record *record) {
  if (record->has_authority) qa_app_sim_q3_application_server_authority_destroy(record->authority);
  if (record->navigation != NULL) qa_app_sim_application_bot_navigation_destroy(record->navigation);
  qa_app_sim_application_bot_assets_free(record->assets);
  free(record);
}
/* random: { nextInt: () => simulation.random.nextInteger() } */
static int64_t guest_random(void *ctx) {
  guest_bot_record *record = ctx;
  return (int64_t)qa_app_sim_source_random_next_integer(record->simulation->random);
}
/* milliseconds: () => Math.trunc(simulation.timeSeconds * 1000) */
static double guest_milliseconds(void *ctx) {
  guest_bot_record *record = ctx;
  return trunc(qa_app_shared_sim_time_seconds(record->simulation) * 1000);
}
/* print: (_severity, text) => this.host.print(text) */
static void guest_print(void *ctx, int severity, const char *text) {
  (void)severity; /* TS ignores the severity argument */
  guest_bot_record *record = ctx;
  qa_application_print(record->app, text);
}
static qa_bot_log_open_result guest_open_log(void *ctx, const char *filename) {
  (void)ctx; /* TS openApplicationBotLog takes no application state */
  return qa_app_sim_open_application_bot_log(NULL, filename);
}
static qa_bot_log_open_result guest_resume_log(void *ctx, const char *filename, int64_t position) {
  (void)ctx; /* TS resumeApplicationBotLog takes no application state */
  return qa_app_sim_resume_application_bot_log(NULL, filename, (double)position);
}
/* createClient: slot => session.clientAt(slot) !== null ? null : createClient(slot), connect("loopback"), client.id */
static bool guest_create_client(void *ctx, double slot, qa_client_id *out) {
  guest_bot_record *record = ctx;
  qa_engine_session *session = record->app->session;
  if (qa_engine_session_client_at(session, (int64_t)slot) != NULL) return false;
  qa_session_client *client = qa_engine_session_create_client(session, (int64_t)slot);
  (void)qa_session_client_connect(client, "loopback");
  *out = qa_session_client_id(client);
  return true;
}
static void guest_free_client(void *ctx, qa_client_id client) {
  guest_bot_record *record = ctx;
  qa_engine_session_close_client(record->app->session, client);
}
/* snapshotEntities: player => authority.snapshot(player).entities.map(entity => entity.number) */
static double guest_snapshot_entity(void *ctx, qa_app_q3_player player, double sequence) {
  guest_bot_record *record = ctx;
  qa_app_q3_snapshot snapshot = record->authority.host.snapshot(record->authority.host.ctx, player);
  if (!(sequence >= 0) || sequence >= (double)snapshot.entity_count) return -1;
  const qa_q3_entity_state_record *entity = snapshot.entities[(size_t)sequence];
  return entity->number;
}
static void guest_bot_print(void *ctx, const char *text) {
  guest_bot_record *record = ctx;
  qa_application_print(record->app, text);
}
typedef struct prepare_guest_attempt {
  guest_bot_record *record;
  qa_app_loaded_content *content;
  qa_app_q3_qvm_server_game *guest;
} prepare_guest_attempt;
static void prepare_guest_body(void *ctx) {
  prepare_guest_attempt *attempt = ctx;
  guest_bot_record *record = attempt->record;
  qa_app_shared_simulation *simulation = record->simulation;
  qa_app_sim_bot_asset_content asset_content = qa_application_bot_asset_content(attempt->content);
  record->assets = checked(qa_app_sim_load_application_bot_assets_with(&asset_content, &record->bot_simulation, true));
  record->navigation_content = qa_application_navigation_content(attempt->content);
  record->navigation_simulation = qa_app_shared_sim_navigation_simulation(simulation, simulation_recipe(simulation, attempt->content));
  record->navigation = checked(qa_app_sim_create_application_bot_navigation(record->navigation_content, record->navigation_simulation));
  memset(&record->selected, 0, sizeof record->selected);
  record->selected.selected = *qa_app_sim_application_bot_navigation_selected(record->navigation);
  record->selected.ctx = record->navigation;
  record->selected.checkpoint = navigation_checkpoint;
  record->selected.restore_checkpoint = navigation_restore;
  record->selected.restart_round = navigation_restart;
  record->authority_simulation = qa_app_shared_sim_network_simulation(simulation);
  record->authority_content = qa_app_shared_sim_network_content(simulation, attempt->content);
  qa_app_sim_q3_server_binding_options binding;
  memset(&binding, 0, sizeof binding);
  binding.session = record->app->session;
  binding.simulation = &record->authority_simulation;
  binding.content = &record->authority_content;
  binding.mode = "restore";
  binding.print_ctx = record;
  binding.print = guest_bot_print;
  record->authority = qa_app_sim_create_q3_application_server_host(binding);
  record->has_authority = true;
  qa_app_q3_guest_bot_preparation preparation;
  memset(&preparation, 0, sizeof preparation);
  preparation.selected = &record->selected;
  preparation.library.files = record->assets->files;
  preparation.library.random.next_int = guest_random;
  preparation.library.random.ctx = record;
  preparation.library.debug = false;
  preparation.library.ctx = record;
  preparation.library.milliseconds = guest_milliseconds;
  preparation.library.print = guest_print;
  preparation.library.open_log = guest_open_log;
  preparation.library.resume_log = guest_resume_log;
  preparation.ctx = record;
  preparation.create_client = guest_create_client;
  preparation.free_client = guest_free_client;
  preparation.snapshot_entity = guest_snapshot_entity;
  qa_app_q3_qvm_server_game_attach_bots(attempt->guest, preparation);
}
void qa_application_prepare_guest_bots(qa_application *app, qa_app_loaded_content *content, qa_app_shared_simulation *simulation) {
  qa_app_q3_qvm_server_game *guest = qa_app_shared_sim_q3_guest(simulation);
  if (guest == NULL) return;
  guest_bot_record *record = checked(calloc(1, sizeof *record));
  record->app = app;
  record->simulation = simulation;
  record->bot_simulation = qa_app_shared_sim_bot_simulation(simulation);
  prepare_guest_attempt attempt = {record, content, guest};
  qa_caught_error caught;
  if (!qa_try(prepare_guest_body, &attempt, &caught)) {
    guest_bot_record_destroy(record);
    qa_rethrow(&caught);
  }
  if (app->guest_bot_record_count == app->guest_bot_record_capacity) {
    size_t next = app->guest_bot_record_capacity == 0 ? 4 : app->guest_bot_record_capacity * 2;
    app->guest_bot_records = checked(realloc(app->guest_bot_records, next * sizeof *app->guest_bot_records));
    app->guest_bot_record_capacity = next;
  }
  app->guest_bot_records[app->guest_bot_record_count++] = record;
}
void qa_application_bots_free(qa_application *app) {
  for (size_t i = 0; i < app->bot_record_count; i++) application_bots_destroy(app->bot_records[i]);
  free(app->bot_records);
  app->bot_records = NULL;
  app->bot_record_count = app->bot_record_capacity = 0;
  for (size_t i = 0; i < app->guest_bot_record_count; i++) guest_bot_record_destroy(app->guest_bot_records[i]);
  free(app->guest_bot_records);
  app->guest_bot_records = NULL;
  app->guest_bot_record_count = app->guest_bot_record_capacity = 0;
}
