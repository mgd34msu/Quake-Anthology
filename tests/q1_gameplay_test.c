#include "gameplay_fixture.h"
#include "qa/game_q1.h"
#include "qa/game_q1_wire.h"
#include "qa/game_q1_maps.h"
#include "qa/game_q1_bots.h"
#include "qa/movement.h"

typedef struct q1_fixture {
    gameplay_map map;
    qa_session *session;
    qa_world *world;
    qa_combat *combat;
    qa_inventory *inventory;
    qa_physics physics;
    qa_q1_game *game;
    qa_targets *targets;
    qa_q1_level *level;
    uint32_t server_flags;
    qa_actor_owner owner;
    uint64_t sequence;
    unsigned pain_sounds;
} q1_fixture;

static bool emitted(void *context, const qa_builtin_event *event, qa_error *error)
{
    (void)error;
    q1_fixture *fixture = context;
    if (event->kind == QA_BUILTIN_SOUND) {
        qa_bytes sound = qa_strings_text(qa_session_strings(fixture->session), event->resource);
        const char *pain = "knight/khurt.wav";
        if (sound.size == strlen(pain) && !memcmp(sound.data, pain, sound.size))
            ++fixture->pain_sounds;
    }
    return true;
}

static bool reacted(void *context, const qa_damage_outcome *outcome, qa_error *error)
{
    return qa_q1_game_reaction(((q1_fixture *)context)->game, outcome, error);
}

static bool damage_effect(void *context, qa_combat *combat, qa_damage_effect_stage stage,
    const qa_damage_request *request, qa_damage_effect *effect, qa_error *error)
{
    (void)combat;
    return qa_q1_game_damage_effect(((q1_fixture *)context)->game, stage, request, effect, error);
}

static bool physics_read(void *context, qa_actor_id actor, qa_physics_properties *properties)
{
    return qa_q1_game_physics_read(((q1_fixture *)context)->game, actor, properties);
}

static bool physics_write(void *context, qa_actor_id actor,
    const qa_physics_properties *properties, qa_error *error)
{
    return qa_q1_game_physics_write(((q1_fixture *)context)->game, actor, properties, error);
}

static bool actor_traits(void *context, qa_actor_id actor, qa_builtin_actor_traits *traits)
{
    return qa_q1_game_actor_traits(((q1_fixture *)context)->game, actor, traits);
}

static bool unexpected_motion(void *context, qa_actor_id actor,
    const qa_builtin_motion_change *change, qa_error *error)
{
    (void)context; (void)actor; (void)change;
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Unexpected movement in dormant trigger fixture");
    return false;
}

static bool client_eye(void *context, qa_actor_id actor, qa_vec3 *eye, qa_error *error)
{
    q1_fixture *fixture = context;
    qa_body_state body;
    qa_builtin_actor_traits traits;
    if (!qa_world_body_read(fixture->world, actor, &body, error) ||
        !qa_q1_game_actor_traits(fixture->game, actor, &traits)) return false;
    *eye = qa_vec_add(body.origin, qa_v3(0, 0, traits.view_height));
    return true;
}

static bool client_player(void *context, uint32_t slot, bool selection,
    qa_actor_id *actor, qa_builtin_check_client_row *row, qa_error *error)
{
    q1_fixture *fixture = context;
    *row = (qa_builtin_check_client_row){0};
    *actor = (qa_actor_id){0};
    if (!qa_q1_source_client_actor(fixture->game, slot, actor)) return true;
    qa_combat_state combat;
    if (!qa_combat_read(fixture->combat, *actor, &combat, error)) return false;
    *row = (qa_builtin_check_client_row){.present = true, .health = combat.health};
    if (selection && !(row->health <= 0)) {
        qa_q1_source_client_view client;
        if (!qa_q1_source_client_read(fixture->game, *actor, &client)) return false;
        row->no_target = client.no_target;
    }
    return true;
}

static bool check_client(void *context, qa_actor_id observer, qa_actor_id *target, qa_error *error)
{
    q1_fixture *fixture = context;
    qa_q1_check_client_source source = {.context = fixture, .player = client_player,
                                      .eye = client_eye};
    return qa_q1_game_check_client(fixture->game, observer, &source, target, error);
}

static bool released(void *context, qa_session *session, qa_actor_record actor, qa_error *error)
{
    (void)session;
    q1_fixture *fixture = context;
    if (fixture->level) qa_q1_level_actor_released(fixture->level, actor.id);
    qa_inventory_actor_released(fixture->inventory, actor);
    qa_combat_actor_released(fixture->combat, actor);
    return qa_world_actor_released(fixture->world, actor, error);
}

static void fixture_create(q1_fixture *fixture, qa_q1_edition edition, uint8_t skill,
    bool native_world, qa_q1_program program)
{
    qa_error error = {0};
    *fixture = (q1_fixture){0};
    gameplay_map_create(&fixture->map);
    GAME_CHECK(qa_session_create(&(qa_session_options){.actor_capacity = 32,
        .component_capacity = 1, .actor_released = released, .release_context = fixture},
        &fixture->session, &error));
    GAME_CHECK(qa_world_create(qa_session_actor_registry(fixture->session),
        fixture->map.geometry, NULL, &fixture->world, &error));
    GAME_CHECK(qa_combat_create(qa_session_actor_registry(fixture->session),
        &(qa_combat_hooks){.context = fixture, .reaction = reacted, .effect = damage_effect},
        &fixture->combat, &error));
    GAME_CHECK(qa_inventory_create(qa_session_actor_registry(fixture->session),
        &fixture->inventory, &error));
    GAME_CHECK(qa_strings_intern_cstr(qa_session_strings(fixture->session),
        "test:q1", &fixture->owner, &error));
    GAME_CHECK(qa_physics_init(&fixture->physics, fixture->world, (qa_actor_id){0},
        &(qa_physics_services){.context = fixture, .read = physics_read, .write = physics_write},
        &error));
    qa_builtin_services services = {.session = fixture->session, .world = fixture->world,
        .combat = fixture->combat, .inventory = fixture->inventory,
        .physics = &fixture->physics, .emit = emitted, .context = fixture,
        .actor_traits = actor_traits, .motion_changed = unexpected_motion};
    qa_q1_options options = {.provider = fixture->owner, .combat_provider = fixture->owner,
        .inventory_provider = fixture->owner, .program = program, .edition = edition,
        .skill = skill, .gravity = 800, .aim_threshold = 2, .max_clients = 1};
    qa_q1_host host = {.context = fixture, .check_client = check_client};
    GAME_CHECK(qa_q1_game_create(&services, &options, &host, &fixture->game, &error));
    qa_component component;
    GAME_CHECK(qa_q1_game_component(fixture->game, &component, &error));
    GAME_CHECK(qa_session_add(fixture->session, &component, &error));
    qa_combat_policy policy;
    GAME_CHECK(qa_q1_game_combat_policy(fixture->game, &policy, &error));
    GAME_CHECK(qa_combat_register_policy(fixture->combat, &policy, &error));
    if (native_world)
        GAME_CHECK(qa_q1_wire_begin_world(fixture->game, "maps/test.bsp", 0, 1, &error));
}

static void fixture_destroy(q1_fixture *fixture)
{
    qa_error error = {0};
    GAME_CHECK(qa_session_retire_actors(fixture->session, &error));
    GAME_CHECK(qa_combat_unregister_policy(fixture->combat, fixture->owner, &error));
    GAME_CHECK(qa_session_remove(fixture->session, fixture->owner, &error));
    qa_q1_game_destroy(fixture->game);
    qa_targets_destroy(fixture->targets);
    qa_q1_level_destroy(fixture->level);
    GAME_CHECK(qa_inventory_destroy(fixture->inventory, &error));
    GAME_CHECK(qa_combat_destroy(fixture->combat, &error));
    GAME_CHECK(qa_physics_dispose(&fixture->physics, &error));
    GAME_CHECK(qa_world_destroy(fixture->world, &error));
    GAME_CHECK(qa_session_destroy(fixture->session, &error));
    qa_collision_destroy(fixture->map.geometry);
}

static bool unexpected_static(void *context, const qa_q1_static_model *model, qa_error *error)
{
    (void)context; (void)model; (void)error;
    return false;
}
static bool unexpected_ambient(void *context, qa_vec3 origin, qa_string_id sound,
    float volume, float attenuation, qa_error *error)
{
    (void)context; (void)origin; (void)sound; (void)volume; (void)attenuation; (void)error;
    return false;
}
static bool unexpected_lightstyle(void *context, int32_t style, qa_string_id pattern,
    qa_error *error)
{
    (void)context; (void)style; (void)pattern; (void)error;
    return false;
}
static bool unexpected_skill(void *context, int32_t skill, qa_error *error)
{
    (void)context; (void)skill; (void)error;
    return false;
}
static bool unexpected_secret(void *context, qa_actor_id source, qa_actor_id player,
    uint32_t total, uint32_t found, qa_error *error)
{
    (void)context; (void)source; (void)player; (void)total; (void)found; (void)error;
    return false;
}
static bool unexpected_begin(void *context, qa_string_id map, qa_actor_id cause,
    double seconds, qa_error *error)
{
    (void)context; (void)map; (void)cause; (void)seconds; (void)error;
    return false;
}
static bool unexpected_travel(void *context, qa_string_id map, qa_actor_id cause, qa_error *error)
{
    (void)context; (void)map; (void)cause; (void)error;
    return false;
}
static bool unexpected_defer(void *context, double seconds, qa_error *error)
{
    (void)context; (void)seconds; (void)error;
    return false;
}
static bool unexpected_achievement(void *context, qa_actor_id player, const char *id,
    qa_error *error)
{
    (void)context; (void)player; (void)id; (void)error;
    return false;
}

static void fixture_bind_maps(q1_fixture *fixture)
{
    qa_error error = {0};
    fixture->targets = qa_targets_create(&(qa_target_options){.session = fixture->session}, &error);
    GAME_CHECK(fixture->targets != NULL);
    qa_string_id map;
    GAME_CHECK(qa_strings_intern_cstr(qa_session_strings(fixture->session), "test", &map, &error));
    fixture->level = qa_q1_level_create(&(qa_q1_level_options){
        .services = {.session = fixture->session}, .current_map = map,
        .server_flags = &fixture->server_flags, .rerelease = true,
        .begin = unexpected_begin, .travel = unexpected_travel,
        .defer_begin = unexpected_defer, .achievement = unexpected_achievement}, &error);
    GAME_CHECK(fixture->level != NULL);
    GAME_CHECK(qa_q1_game_maps_bind(fixture->game, &(qa_q1_map_options){
        .targets = fixture->targets, .level = fixture->level,
        .server_flags = &fixture->server_flags, .current_map = map,
        .static_model = unexpected_static, .ambient = unexpected_ambient,
        .lightstyle = unexpected_lightstyle, .set_skill = unexpected_skill,
        .secret_found = unexpected_secret}, &error));
}

static void thin_brush_bounds(void)
{
    qa_error error = {0};
    gameplay_map map;
    gameplay_map_create(&map);
    qa_collision_destroy(map.geometry);
    uint32_t model = qa_load_u32le(map.bytes + 4 + 14 * 8);
    gameplay_float(map.bytes + model + 4, 409);
    gameplay_float(map.bytes + model + 16, 408);
    qa_bsp_view bsp;
    GAME_CHECK(qa_bsp_open((qa_bytes){map.bytes, sizeof(map.bytes)}, &bsp, &error));
    GAME_CHECK(qa_collision_create(&bsp, &map.geometry, &error));
    qa_bounds bounds;
    GAME_CHECK(qa_collision_model_bounds(map.geometry, 0, &bounds, &error));
    GAME_CHECK(bounds.mins.y == 408 && bounds.maxs.y == 409);
    qa_collision_destroy(map.geometry);
}

static void dormant_trigger_activation(void)
{
    qa_error error = {0};
    q1_fixture fixture;
    fixture_create(&fixture, QA_Q1_RERELEASE, 1, false, QA_Q1_MG1);
    fixture_bind_maps(&fixture);
    qa_actor_id trigger;
    GAME_CHECK(qa_q1_game_spawn(fixture.game, &(qa_q1_spawn){
        .classname = "trigger_once", .targetname = "gold", .spawnflags = 2,
        .angles = {0, 90, 0}, .map_fields = &(qa_q1_map_fields){.model = "*0"}},
        &trigger, &error));
    qa_q1_presentation visible;
    qa_physics_properties physics;
    qa_body_state body;
    qa_actor_collision collision;
    GAME_CHECK(qa_q1_game_presentation(fixture.game, trigger, &visible));
    GAME_CHECK(visible.model == QA_STRING_NONE);
    GAME_CHECK(!visible.has_inline_model);
    GAME_CHECK(qa_q1_game_physics_read(fixture.game, trigger, &physics));
    GAME_CHECK(physics.solid == QA_PHYSICS_NOT_SOLID);
    GAME_CHECK(!qa_world_get_collision(fixture.world, trigger, &collision, &error));
    GAME_CHECK(error.code == QA_OK);
    GAME_CHECK(qa_world_body_read(fixture.world, trigger, &body, &error));
    GAME_CHECK(body.angles.y == 90);
    GAME_CHECK(body.bounds.mins.x == -1025 && body.bounds.maxs.x == 1025);
    GAME_CHECK(qa_q1_game_use(fixture.game, trigger, (qa_actor_id){0}, &error));
    GAME_CHECK(qa_q1_game_presentation(fixture.game, trigger, &visible));
    GAME_CHECK(visible.model == QA_STRING_NONE);
    GAME_CHECK(!visible.has_inline_model);
    GAME_CHECK(qa_q1_game_physics_read(fixture.game, trigger, &physics));
    GAME_CHECK(physics.solid == QA_PHYSICS_TRIGGER && physics.motion == QA_PHYSICS_STATIONARY);
    GAME_CHECK(qa_world_get_collision(fixture.world, trigger, &collision, &error));
    GAME_CHECK(collision.role == QA_COLLISION_TRIGGER);
    GAME_CHECK(qa_world_body_read(fixture.world, trigger, &body, &error));
    GAME_CHECK(body.angles.y == 0);
    GAME_CHECK(body.bounds.mins.x == -1025 && body.bounds.maxs.x == 1025);
    fixture_destroy(&fixture);
}

static void nonsolid_inline_appearance(void)
{
    qa_error error = {0};
    q1_fixture fixture;
    fixture_create(&fixture, QA_Q1_RERELEASE, 1, false, QA_Q1_MG1);
    fixture_bind_maps(&fixture);
    qa_actor_id actor;
    GAME_CHECK(qa_q1_game_spawn(fixture.game, &(qa_q1_spawn){
        .classname = "rotate_object_continuously", .spawnflags = 1,
        .map_fields = &(qa_q1_map_fields){.model = "*0"}}, &actor, &error));
    qa_q1_presentation visual;
    qa_physics_properties physics;
    qa_actor_collision collision;
    GAME_CHECK(qa_q1_game_presentation(fixture.game, actor, &visual));
    GAME_CHECK(visual.model != QA_STRING_NONE && visual.has_inline_model && visual.inline_model == 0);
    GAME_CHECK(qa_q1_game_physics_read(fixture.game, actor, &physics));
    GAME_CHECK(physics.solid == QA_PHYSICS_NOT_SOLID);
    GAME_CHECK(!qa_world_get_collision(fixture.world, actor, &collision, &error));
    GAME_CHECK(error.code == QA_OK);
    GAME_CHECK(qa_q1_game_use(fixture.game, actor, (qa_actor_id){0}, &error));
    GAME_CHECK(qa_q1_game_presentation(fixture.game, actor, &visual));
    GAME_CHECK(visual.model != QA_STRING_NONE && visual.has_inline_model && visual.inline_model == 0);
    GAME_CHECK(!qa_world_get_collision(fixture.world, actor, &collision, &error));
    GAME_CHECK(error.code == QA_OK);
    fixture_destroy(&fixture);
}

static void relay_template_unpublished(void)
{
    qa_error error = {0};
    q1_fixture fixture;
    fixture_create(&fixture, QA_Q1_CLASSIC, 1, false, QA_Q1_ID1);
    fixture_bind_maps(&fixture);
    qa_actor_id actor;
    GAME_CHECK(qa_q1_game_spawn(fixture.game, &(qa_q1_spawn){
        .classname = "trigger_relay", .targetname = "relay",
        .map_fields = &(qa_q1_map_fields){.model = "*0"}}, &actor, &error));
    qa_q1_presentation visual;
    GAME_CHECK(qa_q1_game_presentation(fixture.game, actor, &visual));
    GAME_CHECK(visual.model == QA_STRING_NONE && !visual.has_inline_model);
    GAME_CHECK(qa_q1_game_use(fixture.game, actor, (qa_actor_id){0}, &error));
    GAME_CHECK(qa_q1_game_presentation(fixture.game, actor, &visual));
    GAME_CHECK(visual.model == QA_STRING_NONE && !visual.has_inline_model);
    fixture_destroy(&fixture);
}

static qa_actor_id spawn(q1_fixture *fixture, const char *classname)
{
    qa_error error = {0};
    qa_actor_id actor;
    GAME_CHECK(qa_q1_game_spawn(fixture->game,
        &(qa_q1_spawn){.classname = classname, .origin = {0, 0, 64}}, &actor, &error));
    GAME_CHECK(actor.registry != 0);
    for (unsigned i = 0; i < 6; ++i)
        GAME_CHECK(qa_session_advance(fixture->session, UINT64_C(100000000), &error));
    return actor;
}

static void advance(q1_fixture *fixture, unsigned frames)
{
    qa_error error = {0};
    for (unsigned i = 0; i < frames; ++i)
        GAME_CHECK(qa_session_advance(fixture->session, UINT64_C(100000000), &error));
}

static qa_damage_outcome hit(q1_fixture *fixture, qa_actor_id target, float amount)
{
    qa_error error = {0};
    uint64_t time_ns;
    double elapsed;
    GAME_CHECK(qa_q1_game_clock_read(fixture->game, &time_ns, &elapsed));
    qa_damage_request request = {.target = target, .amount = amount,
        .attack = {.sequence = ++fixture->sequence, .time_ns = time_ns,
            .combat_provider = fixture->owner, .cause = {.kind = QA_CAUSE_Q1}}};
    qa_damage_outcome outcome = {0};
    GAME_CHECK(qa_combat_apply(fixture->combat, &request, &outcome, &error));
    return outcome;
}

static void armor_and_protection(void)
{
    qa_error error = {0};
    q1_fixture fixture;
    fixture_create(&fixture, QA_Q1_CLASSIC, 1, true, QA_Q1_ID1);
    qa_actor_id knight = spawn(&fixture, "monster_knight");
    qa_armor armor = {.regular = {.kind = QA_ARMOR_Q1, .points = 10,
        .protection.q1_absorption = 0.3f}};
    GAME_CHECK(qa_combat_set_armor(fixture.combat, knight, &armor, &error));
    qa_damage_outcome outcome = hit(&fixture, knight, 11);
    qa_combat_state state;
    GAME_CHECK(qa_combat_read(fixture.combat, knight, &state, &error));
    /* progs106/combat.qc: ceil(11*.3)=4 saved, ceil(11-4)=7 taken. */
    GAME_CHECK(state.health == 68 && state.armor.regular.points == 6);
    GAME_CHECK(outcome.result.applied_damage == 7 && fixture.pain_sounds == 1);
    qa_damage_outcome_free(&outcome);
    state.invulnerable = true;
    GAME_CHECK(qa_combat_set_traits(fixture.combat, knight, &state, &error));
    outcome = hit(&fixture, knight, 11);
    GAME_CHECK(qa_combat_read(fixture.combat, knight, &state, &error));
    /* Source consumes armor before checking godmode, and omits th_pain. */
    GAME_CHECK(state.health == 68 && state.armor.regular.points == 2);
    GAME_CHECK(outcome.result.reaction == QA_REACTION_NONE && fixture.pain_sounds == 1);
    qa_damage_outcome_free(&outcome);
    fixture_destroy(&fixture);
}

static void pain_cooldown(uint8_t skill)
{
    qa_error error = {0};
    q1_fixture fixture;
    fixture_create(&fixture, QA_Q1_CLASSIC, skill, true, QA_Q1_ID1);
    qa_actor_id knight = spawn(&fixture, "monster_knight");
    qa_damage_outcome outcome = hit(&fixture, knight, 10);
    qa_damage_outcome_free(&outcome);
    GAME_CHECK(fixture.pain_sounds == 1);
    advance(&fixture, 11);
    outcome = hit(&fixture, knight, 10);
    qa_damage_outcome_free(&outcome);
    /* knight.qc uses one second; combat.qc replaces it with five on Nightmare. */
    GAME_CHECK(fixture.pain_sounds == (skill == 3 ? 1u : 2u));
    advance(&fixture, 51);
    outcome = hit(&fixture, knight, 10);
    qa_damage_outcome_free(&outcome);
    GAME_CHECK(fixture.pain_sounds == (skill == 3 ? 2u : 3u));
    fixture_destroy(&fixture);
}

static void edition_damage(qa_q1_edition edition)
{
    qa_error error = {0};
    q1_fixture fixture;
    fixture_create(&fixture, edition, 1, true, QA_Q1_ID1);
    qa_actor_id oldone = spawn(&fixture, "monster_oldone");
    qa_combat_state state;
    GAME_CHECK(qa_combat_read(fixture.combat, oldone, &state, &error));
    GAME_CHECK(state.health == 40000);
    qa_damage_outcome outcome = hit(&fixture, oldone, 1);
    qa_damage_outcome_free(&outcome);
    GAME_CHECK(qa_combat_read(fixture.combat, oldone, &state, &error));
    /* Only rerelease combat.qc has the pre-quad damage <9999 Shub gate. */
    GAME_CHECK(state.health == (edition == QA_Q1_RERELEASE ? 40000 : 39999));
    outcome = hit(&fixture, oldone, 9999);
    qa_damage_outcome_free(&outcome);
    GAME_CHECK(qa_combat_read(fixture.combat, oldone, &state, &error));
    GAME_CHECK(state.health == (edition == QA_Q1_RERELEASE ? 30001 : 30000));
    fixture_destroy(&fixture);
}


typedef struct mixed_check_client {
    q1_fixture *fixture;
    qa_actor_id player;
    bool no_target;
} mixed_check_client;

static bool mixed_client_player(void *context, uint32_t slot, bool selection,
    qa_actor_id *actor, qa_builtin_check_client_row *row, qa_error *error)
{
    mixed_check_client *mixed = context;
    *actor = (qa_actor_id){0};
    *row = (qa_builtin_check_client_row){0};
    if (slot != 0 || !qa_actors_get(qa_session_actors(mixed->fixture->session), mixed->player))
        return true;
    qa_combat_state combat;
    if (!qa_combat_read(mixed->fixture->combat, mixed->player, &combat, error)) return false;
    *actor = mixed->player;
    *row = (qa_builtin_check_client_row){.present = true, .health = combat.health,
                                       .no_target = selection && mixed->no_target};
    return true;
}

static bool mixed_client_eye(void *context, qa_actor_id actor, qa_vec3 *out, qa_error *error)
{
    mixed_check_client *mixed = context;
    qa_body_state body;
    if (!qa_world_body_read(mixed->fixture->world, actor, &body, error)) return false;
    *out = qa_vec_add(body.origin, qa_v3(0, 0, 22));
    return true;
}

static void donor_check_client(void)
{
    qa_error error = {0};
    q1_fixture fixture;
    fixture_create(&fixture, QA_Q1_CLASSIC, 1, false, QA_Q1_ID1);
    qa_actor_id observer = spawn(&fixture, "monster_knight");
    qa_builtin_services services = {.session = fixture.session, .world = fixture.world,
        .combat = fixture.combat, .inventory = fixture.inventory,
        .physics = &fixture.physics, .emit = emitted, .context = &fixture};
    qa_combat_state combat = {.health = 100, .can_take_damage = true};
    qa_string_id definition;
    GAME_CHECK(qa_strings_intern_cstr(qa_session_strings(fixture.session),
        "test:foreign-player", &definition, &error));
    qa_builtin_spawn native = {.owner = fixture.owner, .definition = definition,
        .body = {.origin = {32, 0, 64}}, .combat = &combat};
    mixed_check_client mixed = {.fixture = &fixture};
    GAME_CHECK(qa_builtin_spawn_actor(&services, &native, &mixed.player, &error));
    uint32_t slot;
    GAME_CHECK(!qa_q1_native_client_slot(fixture.game, mixed.player, &slot, NULL));
    qa_q1_check_client_source source = {.context = &mixed, .player = mixed_client_player,
                                      .eye = mixed_client_eye};
    qa_actor_id target;
    GAME_CHECK(qa_q1_game_check_client(fixture.game, observer, &source, &target, &error));
    GAME_CHECK(qa_actor_id_equal(target, mixed.player));
    GAME_CHECK(qa_combat_set_health(fixture.combat, mixed.player, 0, &error));
    GAME_CHECK(qa_q1_game_check_client(fixture.game, observer, &source, &target, &error));
    GAME_CHECK(!target.registry);
    GAME_CHECK(qa_session_release(fixture.session, mixed.player, &error));
    advance(&fixture, 1);
    GAME_CHECK(qa_q1_game_check_client(fixture.game, observer, &source, &target, &error));
    GAME_CHECK(!target.registry);
    fixture_destroy(&fixture);
}

typedef struct movement_fixture {
    qa_collision_geometry *geometry;
    qa_movement_result *output;
    qa_movement_state *borrowed;
    bool fail_after_contact;
} movement_fixture;

static bool movement_trace(void *context, const qa_trace_query *query,
    qa_trace_result *result, qa_error *error)
{
    return qa_collision_trace(((movement_fixture *)context)->geometry,
        query, result, error);
}

static bool movement_contents(void *context, const qa_point_query *query,
    qa_point_contents *result, qa_error *error)
{
    return qa_collision_point_contents(((movement_fixture *)context)->geometry,
        query, result, error);
}

static qa_movement_control movement_phase(void *context, qa_movement_phase phase,
    qa_movement_call *call, qa_error *error)
{
    movement_fixture *fixture = context;
    /* This loan is read during publication after the kernel returns. */
    if (call->state != &fixture->output->state) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Movement callback has temporary state storage");
        return QA_MOVEMENT_ERROR;
    }
    fixture->borrowed = call->state;
    return fixture->fail_after_contact && phase == QA_MOVE_POSTTHINK
        ? QA_MOVEMENT_ERROR : QA_MOVEMENT_CONTINUE;
}

static void movement_output_owner(void)
{
    qa_error error = {0};
    gameplay_map map;
    gameplay_map_create(&map);
    movement_fixture fixture = {.geometry = map.geometry};
    qa_movement_services services = {.context = &fixture,
        .trace = movement_trace, .point_contents = movement_contents,
        .phase = movement_phase};
    for (qa_movement_kind kind = QA_MOVEMENT_NETQUAKE; kind <= QA_MOVEMENT_Q3; ++kind) {
        qa_movement_input input = qa_movement_input_default(kind, (qa_actor_id){0});
        input.elapsed_ns = UINT64_C(14000000);
        input.command.milliseconds = 14;
        input.command.server_time_ms = 14;
        GAME_CHECK(qa_movement_set_origin(&input.state, qa_v3(0, 0, 64), &error));
        qa_movement_result output = {0};
        fixture.output = &output;
        fixture.borrowed = NULL;
        GAME_CHECK(qa_movement_move(&input, &services, &output, &error));
        GAME_CHECK(fixture.borrowed == &output.state);
        GAME_CHECK(output.status == QA_MOVEMENT_ACTIVE);
        GAME_CHECK(fixture.borrowed->kind == kind);
        qa_movement_result_free(&output);
    }
    qa_movement_input input = qa_movement_input_default(QA_MOVEMENT_NETQUAKE,
        (qa_actor_id){0});
    input.elapsed_ns = UINT64_C(14000000);
    input.command.milliseconds = 14;
    input.command.sequence = 77;
    qa_movement_result output = {.status = QA_MOVEMENT_ACTOR_REMOVED,
        .state = input.state, .command_sequence = 900, .view_height = 19,
        .effect_count = 38, .render_flags = 987};
    output.state.data.nq.flags = 123;
    output.state.data.nq.origin = qa_v3(11, 12, 13);
    fixture.output = &output;
    fixture.fail_after_contact = true;
    GAME_CHECK(!qa_movement_physics_netquake(&input, &services, &output, &error));
    GAME_CHECK(fixture.borrowed == &output.state);
    GAME_CHECK(output.status == QA_MOVEMENT_ACTOR_REMOVED);
    GAME_CHECK(output.command_sequence == 900 && output.view_height == 19);
    GAME_CHECK(output.state.data.nq.flags == 123);
    GAME_CHECK(output.state.data.nq.origin.x == 11 &&
        output.state.data.nq.origin.y == 12 && output.state.data.nq.origin.z == 13);
    GAME_CHECK(output.effect_count == 38 && output.render_flags == 987);
    GAME_CHECK(output.contacts != NULL && output.contact_capacity > 0);
    GAME_CHECK(output.contact_count == 0);
    qa_movement_result_free(&output);
    qa_collision_destroy(map.geometry);
}

static bool external_brush_collision(void *context, qa_actor_collision *out, qa_error *error)
{
    (void)error;
    *out = (qa_actor_collision){.family = QA_COLLISION_Q1, .inline_model = true,
        .model_geometry = *(qa_collision_geometry **)context, .contents = -2};
    return true;
}

static void retained_external_brush(void)
{
    qa_error error = {0};
    gameplay_map map, external;
    gameplay_map_create(&map);
    gameplay_map_create(&external);
    qa_collision_destroy(external.geometry);
    uint32_t plane = qa_load_u32le(external.bytes + 4 + QA_BSP_PLANES * 8);
    uint32_t model = qa_load_u32le(external.bytes + 4 + QA_BSP_MODELS * 8);
    gameplay_float(external.bytes + plane + 12, 64);
    for (unsigned axis = 0; axis < 3; ++axis) {
        gameplay_float(external.bytes + model + axis * 4, axis == 2 ? -64 : -16);
        gameplay_float(external.bytes + model + 12 + axis * 4, axis == 2 ? 64 : 16);
    }
    qa_bsp_view bsp;
    GAME_CHECK(qa_bsp_open((qa_bytes){external.bytes, sizeof(external.bytes)}, &bsp, &error));
    GAME_CHECK(qa_collision_create(&bsp, &external.geometry, &error));
    qa_actor_registry *actors;
    qa_world *world;
    qa_actor_id actor;
    GAME_CHECK(qa_actors_create(8, NULL, NULL, &actors, &error));
    GAME_CHECK(qa_world_create(actors, map.geometry, NULL, &world, &error));
    GAME_CHECK(qa_actors_allocate(actors, 1, 1, &actor, &error));
    qa_body_state body = {.origin = {0, 0, 128}, .bounds = {{-16, -16, -64}, {16, 16, 64}}};
    GAME_CHECK(qa_world_body_create(world, actor, &body, &error));
    qa_collision_binding binding = {.context = &external.geometry, .read = external_brush_collision};
    GAME_CHECK(qa_world_collision_bind(world, actor, &binding, &error));
    GAME_CHECK(qa_world_link(world, actor, NULL, &error));
    qa_world_checkpoint checkpoint = {0};
    GAME_CHECK(qa_world_checkpoint_capture(world, &checkpoint, &error));
    GAME_CHECK(checkpoint.body_count == 1 && checkpoint.spatial_count == 1);
    GAME_CHECK(checkpoint.bodies[0].collision.model_geometry == NULL &&
        checkpoint.bodies[0].stored_collision.model_geometry == NULL &&
        checkpoint.bodies[0].retained_collision.model_geometry == NULL);
    qa_collision_geometry *replacement;
    GAME_CHECK(qa_collision_create(&bsp, &replacement, &error));
    qa_collision_destroy(external.geometry);
    external.geometry = replacement;
    GAME_CHECK(qa_world_checkpoint_restore(world, &checkpoint, NULL, NULL, &error));
    for (qa_collision_family family = QA_COLLISION_Q1; family <= QA_COLLISION_Q3; ++family) {
        qa_trace_query query = {.start = {0, 0, 200}, .end = {0, 0, 160},
            .shape = {.kind = QA_SHAPE_POINT}, .policy = qa_collision_default_policy(family)};
        qa_trace_result hit;
        GAME_CHECK(qa_world_trace(world, &query, &hit, &error));
        GAME_CHECK(hit.hit == QA_TRACE_HIT_ACTOR && qa_actor_id_equal(hit.actor, actor));
        /* Stock caller clipping: Q3 SURFACE_CLIP_EPSILON is 1/8,
         * Q1/Q2 DIST_EPSILON is 1/32, independent of the restored brush format. */
        float expected_end = family == QA_COLLISION_Q3 ? 192.125f : 192.03125f;
        GAME_CHECK(fabsf(hit.end.z - expected_end) < .0001f);
        if (family != QA_COLLISION_Q1) {
            qa_point_query point = {.point = {0, 0, 180}, .policy = query.policy};
            qa_point_contents contents;
            GAME_CHECK(qa_world_point_contents(world, &point, &contents, &error));
            GAME_CHECK((contents.contents & 1) != 0);
        }
    }
    qa_world_checkpoint_free(&checkpoint);
    GAME_CHECK(qa_world_destroy(world, &error));
    GAME_CHECK(qa_actors_destroy(actors, &error));
    qa_collision_destroy(external.geometry);
    qa_collision_destroy(map.geometry);
}

void test_q1_gameplay(void);
void test_q1_gameplay(void)
{
    thin_brush_bounds();
    dormant_trigger_activation();
    nonsolid_inline_appearance();
    relay_template_unpublished();
    retained_external_brush();
    movement_output_owner();
    donor_check_client();
    armor_and_protection();
    pain_cooldown(1);
    pain_cooldown(3);
    edition_damage(QA_Q1_CLASSIC);
    edition_damage(QA_Q1_RERELEASE);
}
