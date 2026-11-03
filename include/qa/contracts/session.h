/*
 * Port of src/contracts/session.ts (types only): simulation inputs,
 * events, snapshots, checkpoints, and seat presentation interfaces.
 * The optional ModSessionCheckpoint in SaveImage is forward declared
 * (mods.h) to avoid the session<->mods include cycle. TS Promise
 * returns (stepAsync) are synchronous in C; its progress callback is
 * fn ptr + ctx. GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_SESSION_H
#define QA_CONTRACTS_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/content.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/numeric.h"
#include "qa/contracts/protocol.h"
#include "qa/contracts/render.h"
#include "qa/contracts/scene.h"
#include "qa/contracts/time.h"
#include "qa/contracts/ui.h"
#include "qa/contracts/world.h"

typedef struct qa_mod_session_checkpoint qa_mod_session_checkpoint;

/* TS ActorConfiguration extends Pick<ExecutableRecipe,
 * "movement"|"character"|"weapons"|"inventory">. */
typedef struct qa_actor_configuration {
  qa_provider_reference movement;
  qa_character_selection character;
  qa_provider_reference_list weapons;
  qa_provider_reference inventory;
  qa_actor_id actor;
} qa_actor_configuration;

/* TS CommandSource union on kind: "local-seat" | "remote-client" | "bot". */
typedef enum qa_command_source_kind {
  QA_COMMAND_SOURCE_LOCAL_SEAT, QA_COMMAND_SOURCE_REMOTE_CLIENT, QA_COMMAND_SOURCE_BOT
} qa_command_source_kind;
typedef struct qa_command_source {
  qa_command_source_kind kind;
  qa_seat_id seat;
  qa_client_id client;
  qa_provider_id provider; /* bot */
} qa_command_source;

typedef struct qa_actor_command {
  qa_actor_id actor;
  qa_command_source source;
  double sequence;
  qa_user_command command;
  const char *angle_space;             /* optional: "absolute"|"source-relative" */
  const qa_arsenal_intent *arsenal;    /* optional */
} qa_actor_command;

typedef struct qa_input_batch {
  double elapsed_milliseconds;
  const qa_actor_command *commands; size_t command_count;
} qa_input_batch;

/* TS EventAudience union on kind: "world" | "seat" | "client". */
typedef struct qa_event_audience {
  const char *kind;
  qa_seat_id seat;
  qa_client_id client;
} qa_event_audience;

/* TS SimulationEventPayload union on kind: "sound" | "damage" |
 * "transition" | "message"(optional sourcePresentationSequence). */
typedef enum qa_simulation_event_payload_kind {
  QA_SIM_EVENT_SOUND, QA_SIM_EVENT_DAMAGE, QA_SIM_EVENT_TRANSITION, QA_SIM_EVENT_MESSAGE
} qa_simulation_event_payload_kind;
typedef struct qa_simulation_event_payload {
  qa_simulation_event_payload_kind kind;
  qa_resource_id resource;               /* sound */
  const qa_actor_id *actor;              /* sound, nullable */
  qa_vec3 origin;
  double channel, volume, attenuation;
  qa_damage_outcome outcome;             /* damage */
  qa_transition_decision decision;       /* transition */
  qa_network_event event;                /* message */
  bool has_source_presentation_sequence;
  double source_presentation_sequence;
} qa_simulation_event_payload;

typedef struct qa_simulation_event {
  double sequence;
  qa_source_time time;
  qa_event_audience audience;
  qa_simulation_event_payload payload;
} qa_simulation_event;

typedef struct qa_body_snapshot { qa_actor_id actor; qa_body_state body; } qa_body_snapshot;
typedef struct qa_inventory_snapshot {
  qa_actor_id actor;
  const qa_inventory_entry *entries; size_t entry_count;
} qa_inventory_snapshot;

typedef struct qa_world_snapshot {
  qa_session_id session;
  qa_frame_context frame;
  const qa_actor_observation *actors; size_t actor_count;
  const qa_body_snapshot *bodies; size_t body_count;
  const qa_inventory_snapshot *inventories; size_t inventory_count;
  const qa_actor_configuration *configurations; size_t configuration_count;
  qa_scene_snapshot scene;
} qa_world_snapshot;

typedef struct qa_simulation_output qa_simulation_output;
typedef struct qa_simulation_progress {
  const qa_simulation_output *output;
  double elapsed_milliseconds, pending_milliseconds;
} qa_simulation_progress;
struct qa_simulation_output {
  qa_world_snapshot snapshot;
  const qa_simulation_event *events; size_t event_count;
};

typedef struct qa_saved_actor_id { double slot, generation; } qa_saved_actor_id;

/* ActorSlotCheckpoint.lifetime: {kind:"active",owner,definition} |
 * {kind:"free",freedAt: SourceTime | null}. */
typedef struct qa_actor_slot_checkpoint {
  qa_saved_actor_id id;
  bool lifetime_is_active;
  qa_provider_id owner;
  const char *definition; /* `${string}:${string}` */
  const qa_source_time *freed_at; /* nullable */
} qa_actor_slot_checkpoint;

/* TS SavedBodyState = Omit<BodyState,"ground"> + ground SavedActorId|null. */
typedef struct qa_saved_body_state {
  qa_vec3 origin, angles, velocity;
  qa_bounds bounds;
  const qa_saved_actor_id *ground;
} qa_saved_body_state;
typedef struct qa_saved_body_attachment {
  qa_saved_actor_id anchor;
  qa_body_follow_kind follow_kind;
  qa_vec3 follow_offset;
} qa_saved_body_attachment;

typedef struct qa_body_checkpoint_linked {
  qa_saved_body_state state;
  qa_bounds absolute_bounds;
} qa_body_checkpoint_linked;
typedef struct qa_body_checkpoint {
  qa_saved_actor_id actor;
  qa_saved_body_state body;
  const qa_saved_body_attachment *attachment; /* nullable */
  double link_count;
  const qa_body_checkpoint_linked *linked;    /* nullable */
} qa_body_checkpoint;

typedef struct qa_combat_checkpoint { qa_saved_actor_id actor; qa_combat_state state; } qa_combat_checkpoint;
typedef struct qa_inventory_checkpoint {
  qa_saved_actor_id actor;
  const qa_inventory_entry *entries; size_t entry_count;
} qa_inventory_checkpoint;
/* TS ActorConfigurationCheckpoint = Omit<ActorConfiguration,"actor"> + saved actor. */
typedef struct qa_actor_configuration_checkpoint {
  qa_provider_reference movement;
  qa_character_selection character;
  qa_provider_reference_list weapons;
  qa_provider_reference inventory;
  qa_saved_actor_id actor;
} qa_actor_configuration_checkpoint;

typedef struct qa_think_checkpoint {
  bool has_execution_provider;
  qa_provider_id execution_provider;
  qa_saved_actor_id actor;
  qa_callback_id callback;
  qa_source_time due;
  qa_think_boundary boundary;
  qa_provider_id provider;
  double sequence;
} qa_think_checkpoint;

typedef struct qa_provider_checkpoint {
  qa_provider_id provider;
  const char *schema; /* `${string}:${string}` */
  double version;
  qa_byte_view bytes;
} qa_provider_checkpoint;

/* Omit<ProviderCheckpoint, "bytes"> (used by mods.h ModIdentity). */
typedef struct qa_provider_checkpoint_ref {
  qa_provider_id provider;
  const char *schema;
  double version;
} qa_provider_checkpoint_ref;

typedef struct qa_save_clock { qa_provider_id provider; qa_source_time time; } qa_save_clock;
typedef struct qa_save_random { qa_provider_id provider; qa_random_state state; } qa_save_random;

/* TS SelectionConsumptionEntry: what a native-guest session did with one
 * selection axis. axis "enemies" | "movement" | "character"; outcome
 * "honored" | "partial" | "guest-native". Metadata only; restore never
 * reads it back as state. */
typedef struct qa_selection_consumption_entry {
  const char *axis, *requested, *outcome, *detail;
} qa_selection_consumption_entry;

/* C-only: the JS insertion order of the SaveImage record's keys, which
 * encodeCheckpointValue (JSON.stringify) preserves. A checkpoint built by
 * SharedSimulation.checkpointFromSource orders its keys schemaVersion,
 * recipe, frame, nextEventSequence, mods?, consumption, clocks, ...,
 * guests; parseSaveImage builds schemaVersion, legacyArmorLayout?, recipe,
 * frame, nextEventSequence, clocks, ..., guests, mods?, consumption. */
typedef enum qa_save_image_key_order {
  QA_SAVE_IMAGE_ORDER_CHECKPOINT = 0,
  QA_SAVE_IMAGE_ORDER_PARSED
} qa_save_image_key_order;

typedef struct qa_save_image {
  const qa_mod_session_checkpoint *mods; /* optional */
  int64_t schema_version;                /* TS literal 4 */
  bool legacy_armor_layout;              /* TS optional `true` */
  qa_executable_recipe recipe;
  qa_frame_context frame;
  double next_event_sequence;
  const qa_save_clock *clocks; size_t clock_count;
  const qa_save_random *random; size_t random_count;
  const qa_actor_slot_checkpoint *actors; size_t actor_count;
  const qa_body_checkpoint *bodies; size_t body_count;
  const qa_combat_checkpoint *combat; size_t combat_count;
  const qa_inventory_checkpoint *inventories; size_t inventory_count;
  const qa_actor_configuration_checkpoint *configurations; size_t configuration_count;
  const qa_think_checkpoint *thinks; size_t think_count;
  const qa_provider_checkpoint *providers; size_t provider_count;
  const qa_guest_checkpoint *guests; size_t guest_count;
  /* TS consumption (empty when nothing was narrowed). */
  const qa_selection_consumption_entry *consumption; size_t consumption_count;
  qa_save_image_key_order key_order; /* C-only, see above */
} qa_save_image;

/** TS Simulation. step_async is the optional stepAsync (nullable);
 * its progress callback may be NULL. */
typedef struct qa_simulation {
  void *ctx;
  qa_session_id session;
  qa_executable_recipe recipe;
  qa_simulation_output (*step)(void *ctx, qa_input_batch input);
  qa_simulation_output (*step_async)(void *ctx, qa_input_batch input,
      void (*progress)(void *progress_ctx, qa_simulation_progress frame),
      void *progress_ctx);
  qa_save_image (*checkpoint)(void *ctx);
  void (*close)(void *ctx);
} qa_simulation;

/* TS SeatActorBinding: {kind:"spectator"} | {kind:"player",actor}. */
typedef struct qa_seat_actor_binding {
  bool is_player;
  qa_actor_id actor;
} qa_seat_actor_binding;

typedef struct qa_seat_client_state {
  qa_seat_id seat;
  qa_client_id client;
  qa_seat_actor_binding actor;
  qa_seat_ui_state ui;
  qa_seat_presentation_binding presentation;
} qa_seat_client_state;

/** TS SeatPresentation. */
typedef struct qa_seat_presentation {
  void *ctx;
  qa_seat_client_state state;
  void (*receive)(void *ctx, const qa_simulation_event *events, size_t event_count);
  qa_render_frame (*frame)(void *ctx, qa_world_snapshot snapshot);
  void (*render)(void *ctx, qa_render_frame frame, qa_renderer_backend backend);
} qa_seat_presentation;

#endif
