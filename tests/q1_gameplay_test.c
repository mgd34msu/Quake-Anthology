#include "gameplay_fixture.h"
#include "qa/game_q1.h"
#include "qa/game_q1_checkpoint.h"
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
    unsigned shot_sounds, impacts;
    int32_t sound_channel;
    float impact_value;
    qa_actor_id teleport_player;
    unsigned hum_sounds, particle_events, teleport_fogs, teleport_moves, retouches;
    bool hum_static;
    int32_t particle_color, particle_count;
    qa_vec3 hum_origin, particle_origin, fog_origin, player_at_fog;
    qa_builtin_motion_change teleport_motion;
} q1_fixture;

static bool emitted(void *context, const qa_builtin_event *event, qa_error *error)
{
    (void)error;
    q1_fixture *fixture = context;
    if (event->kind == QA_BUILTIN_SOUND) {
        ++fixture->shot_sounds;
        fixture->sound_channel = event->channel;
        qa_bytes sound = qa_strings_text(qa_session_strings(fixture->session), event->resource);
        const char *pain = "knight/khurt.wav";
        if (sound.size == strlen(pain) && !memcmp(sound.data, pain, sound.size))
            ++fixture->pain_sounds;
        const char *hum = "ambience/hum1.wav";
        if (sound.size == strlen(hum) && !memcmp(sound.data, hum, sound.size)) {
            ++fixture->hum_sounds;
            fixture->hum_origin = event->origin;
            fixture->hum_static = event->volume == .7f && event->attenuation == 3 && (event->flags & 1u);
        }
    }
    if (event->kind == QA_BUILTIN_IMPACT) {
        ++fixture->impacts;
        fixture->impact_value = event->value;
    }
    if (event->kind == QA_BUILTIN_PARTICLES) {
        ++fixture->particle_events;
        fixture->particle_origin = event->origin;
        fixture->particle_color = event->code;
        fixture->particle_count = event->count;
    }
    if (event->kind == QA_BUILTIN_TELEPORT && fixture->teleport_player.registry) {
        qa_body_state body;
        if (!qa_world_body_read(fixture->world, fixture->teleport_player, &body, error))
            return false;
        ++fixture->teleport_fogs;
        fixture->fog_origin = event->origin;
        fixture->player_at_fog = body.origin;
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
    q1_fixture *fixture = context;
    if (qa_actor_id_equal(actor, fixture->teleport_player) && actor.registry) {
        *traits = (qa_builtin_actor_traits){.player = true, .view_height = 22, .max_health = 100};
        return true;
    }
    return qa_q1_game_actor_traits(fixture->game, actor, traits);
}

static bool unexpected_motion(void *context, qa_actor_id actor,
    const qa_builtin_motion_change *change, qa_error *error)
{
    q1_fixture *fixture = context;
    if (qa_actor_id_equal(actor, fixture->teleport_player) && actor.registry) {
        ++fixture->teleport_moves;
        fixture->teleport_motion = *change;
        return true;
    }
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Unexpected movement in dormant trigger fixture");
    return false;
}
static bool force_retouch(void *context, uint32_t frames, qa_error *error)
{
    (void)error;
    q1_fixture *fixture = context;
    fixture->retouches += frames;
    return true;
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
    (void)session; (void)error;
    q1_fixture *fixture = context;
    if (fixture->level) qa_q1_level_actor_released(fixture->level, actor.id);
    qa_inventory_actor_released(fixture->inventory, actor);
    qa_combat_actor_released(fixture->combat, actor);
    return true;
}

static void fixture_create_rules(q1_fixture *fixture, qa_q1_edition edition, uint8_t skill,
    bool native_world, qa_q1_program program, bool coop, int32_t deathmatch, qa_ruleset_id rules)
{
    qa_error error = {0};
    *fixture = (q1_fixture){0};
    gameplay_map_create(&fixture->map);
    GAME_CHECK(qa_session_create(&(qa_session_options){.actor_capacity = 32,
        .component_capacity = 1, .actor_released = released, .release_context = fixture},
        &fixture->session, &error));
    GAME_CHECK(qa_world_create(qa_session_actor_registry(fixture->session),
        fixture->map.geometry, NULL, 0, &fixture->world, &error));
    GAME_CHECK(qa_combat_create(qa_session_actor_registry(fixture->session), qa_session_strings(fixture->session),
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
        .rules = rules,
        .skill = skill, .gravity = 800, .aim_threshold = 2, .max_clients = 1,
        .coop = coop, .deathmatch = deathmatch};
    qa_q1_host host = {.context = fixture, .check_client = check_client,
        .force_retouch = force_retouch};
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

static void fixture_create(q1_fixture *fixture, qa_q1_edition edition, uint8_t skill,
    bool native_world, qa_q1_program program)
{
    fixture_create_rules(fixture, edition, skill, native_world, program, false, 0, QA_RULESET_NETQUAKE);
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
        .set_skill = unexpected_skill,
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
    GAME_CHECK(visible.visual.models[0] == QA_STRING_NONE);
    GAME_CHECK(!visible.visual.has_inline_model);
    GAME_CHECK(qa_q1_game_physics_read(fixture.game, trigger, &physics));
    GAME_CHECK(physics.solid == QA_PHYSICS_NOT_SOLID);
    GAME_CHECK(!qa_world_get_collision(fixture.world, trigger, &collision, &error));
    GAME_CHECK(error.code == QA_OK);
    GAME_CHECK(qa_world_body_read(fixture.world, trigger, &body, &error));
    GAME_CHECK(body.angles.y == 90);
    GAME_CHECK(body.bounds.mins.x == -1025 && body.bounds.maxs.x == 1025);
    GAME_CHECK(qa_q1_game_use(fixture.game, trigger, (qa_actor_id){0}, &error));
    GAME_CHECK(qa_q1_game_presentation(fixture.game, trigger, &visible));
    GAME_CHECK(visible.visual.models[0] == QA_STRING_NONE);
    GAME_CHECK(!visible.visual.has_inline_model);
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
    GAME_CHECK(visual.visual.models[0] != QA_STRING_NONE && visual.visual.has_inline_model && visual.visual.inline_model == 0);
    GAME_CHECK(qa_q1_game_physics_read(fixture.game, actor, &physics));
    GAME_CHECK(physics.solid == QA_PHYSICS_NOT_SOLID);
    GAME_CHECK(!qa_world_get_collision(fixture.world, actor, &collision, &error));
    GAME_CHECK(error.code == QA_OK);
    GAME_CHECK(qa_q1_game_use(fixture.game, actor, (qa_actor_id){0}, &error));
    GAME_CHECK(qa_q1_game_presentation(fixture.game, actor, &visual));
    GAME_CHECK(visual.visual.models[0] != QA_STRING_NONE && visual.visual.has_inline_model && visual.visual.inline_model == 0);
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
    GAME_CHECK(visual.visual.models[0] == QA_STRING_NONE && !visual.visual.has_inline_model);
    GAME_CHECK(qa_q1_game_use(fixture.game, actor, (qa_actor_id){0}, &error));
    GAME_CHECK(qa_q1_game_presentation(fixture.game, actor, &visual));
    GAME_CHECK(visual.visual.models[0] == QA_STRING_NONE && !visual.visual.has_inline_model);
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

static void navigation_hurt_state(qa_q1_edition edition)
{
    qa_error error = {0};
    q1_fixture fixture;
    fixture_create(&fixture, edition, 1, true, QA_Q1_ID1);
    fixture_bind_maps(&fixture);
    qa_actor_id victim = spawn(&fixture, "monster_knight"), hurt, healing;
    GAME_CHECK(qa_q1_game_spawn(fixture.game, &(qa_q1_spawn){
        .classname = "trigger_hurt", .map_fields = &(qa_q1_map_fields){.model = "*0"}},
        &hurt, &error));
    qa_builtin_actor_traits traits;
    GAME_CHECK(qa_q1_game_actor_traits(fixture.game, hurt, &traits) && traits.navigation_hazard);
    GAME_CHECK(qa_q1_game_touch(fixture.game, &(qa_touch_contact){.self = hurt, .other = victim}, &error));
    GAME_CHECK(qa_q1_game_actor_traits(fixture.game, hurt, &traits) && !traits.navigation_hazard);
    advance(&fixture, 11);
    GAME_CHECK(qa_q1_game_actor_traits(fixture.game, hurt, &traits) && traits.navigation_hazard);
    GAME_CHECK(qa_q1_game_spawn(fixture.game, &(qa_q1_spawn){
        .classname = "trigger_hurt", .damage = -5,
        .map_fields = &(qa_q1_map_fields){.model = "*0"}}, &healing, &error));
    GAME_CHECK(qa_q1_game_actor_traits(fixture.game, healing, &traits) && !traits.navigation_hazard);
    GAME_CHECK(qa_session_release(fixture.session, hurt, &error));
    GAME_CHECK(!qa_q1_game_actor_traits(fixture.game, hurt, &traits));
    fixture_destroy(&fixture);
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
    qa_damage_outcome_release(&outcome);
    state.invulnerable = true;
    GAME_CHECK(qa_combat_set_traits(fixture.combat, knight, &state, &error));
    outcome = hit(&fixture, knight, 11);
    GAME_CHECK(qa_combat_read(fixture.combat, knight, &state, &error));
    /* Source consumes armor before checking godmode, and omits th_pain. */
    GAME_CHECK(state.health == 68 && state.armor.regular.points == 2);
    GAME_CHECK(outcome.result.reaction == QA_REACTION_NONE && fixture.pain_sounds == 1);
    qa_damage_outcome_release(&outcome);
    fixture_destroy(&fixture);
}

static void pain_cooldown(uint8_t skill)
{
    qa_error error = {0};
    q1_fixture fixture;
    fixture_create(&fixture, QA_Q1_CLASSIC, skill, true, QA_Q1_ID1);
    qa_actor_id knight = spawn(&fixture, "monster_knight");
    qa_damage_outcome outcome = hit(&fixture, knight, 10);
    qa_damage_outcome_release(&outcome);
    GAME_CHECK(fixture.pain_sounds == 1);
    advance(&fixture, 11);
    outcome = hit(&fixture, knight, 10);
    qa_damage_outcome_release(&outcome);
    /* knight.qc uses one second; combat.qc replaces it with five on Nightmare. */
    GAME_CHECK(fixture.pain_sounds == (skill == 3 ? 1u : 2u));
    advance(&fixture, 51);
    outcome = hit(&fixture, knight, 10);
    qa_damage_outcome_release(&outcome);
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
    qa_damage_outcome_release(&outcome);
    GAME_CHECK(qa_combat_read(fixture.combat, oldone, &state, &error));
    /* Only rerelease combat.qc has the pre-quad damage <9999 Shub gate. */
    GAME_CHECK(state.health == (edition == QA_Q1_RERELEASE ? 40000 : 39999));
    outcome = hit(&fixture, oldone, 9999);
    qa_damage_outcome_release(&outcome);
    GAME_CHECK(qa_combat_read(fixture.combat, oldone, &state, &error));
    GAME_CHECK(state.health == (edition == QA_Q1_RERELEASE ? 30001 : 30000));
    fixture_destroy(&fixture);
}


static void foreign_shambler_resistance(void)
{
    qa_error error={0};q1_fixture fixture;
    fixture_create(&fixture,QA_Q1_CLASSIC,1,true,QA_Q1_ID1);
    qa_actor_id shambler=spawn(&fixture,"monster_shambler");
    const struct { qa_cause_kind kind; int32_t mod; bool radius, half; } cases[]={
        {QA_CAUSE_Q2,8,false,true},{QA_CAUSE_Q3,6,false,true},
        {QA_CAUSE_Q2,1,true,true},{QA_CAUSE_Q3,1,true,true},
        {QA_CAUSE_Q2,1,false,false},{QA_CAUSE_Q3,1,false,false},
        {QA_CAUSE_Q1,0,true,false},{QA_CAUSE_ENVIRONMENT,0,true,false}};
    for(size_t i=0;i<sizeof cases/sizeof *cases;++i) {
        GAME_CHECK(qa_combat_set_health(fixture.combat,shambler,600,&error));
        uint64_t time_ns;double elapsed;
        GAME_CHECK(qa_q1_game_clock_read(fixture.game,&time_ns,&elapsed));
        qa_damage_request request={.target=shambler,.amount=21,.knockback=41,
            .radius=cases[i].radius,.attack={.sequence=++fixture.sequence,
                .time_ns=time_ns,.combat_provider=fixture.owner,.cause={.kind=cases[i].kind}}};
        if(cases[i].kind==QA_CAUSE_Q2)request.attack.cause.source.q2.means_of_death=cases[i].mod;
        if(cases[i].kind==QA_CAUSE_Q3)request.attack.cause.source.q3.means_of_death=cases[i].mod;
        qa_damage_outcome outcome={0};
        GAME_CHECK(qa_combat_apply(fixture.combat,&request,&outcome,&error));
        GAME_CHECK(outcome.request.amount==(cases[i].half?10.5:21));
        GAME_CHECK(outcome.request.knockback==(cases[i].half?20.5:41));
        qa_combat_state state;
        GAME_CHECK(qa_combat_read(fixture.combat,shambler,&state,&error));
        GAME_CHECK(state.health==(cases[i].half?589:579));
        qa_damage_outcome_release(&outcome);
    }
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
    qa_trace_scratch *scratch;
    qa_movement_result *output;
    qa_movement_state *borrowed;
    bool fail_after_contact;
} movement_fixture;

static bool movement_trace(void *context, const qa_trace_query *query,
    qa_trace_result *result, qa_error *error)
{
    return qa_collision_trace(((movement_fixture *)context)->geometry,
        ((movement_fixture *)context)->scratch, query, result, error);
}

static bool movement_contents(void *context, const qa_point_query *query,
    qa_point_contents *result, qa_error *error)
{
    return qa_collision_point_contents(((movement_fixture *)context)->geometry,
        ((movement_fixture *)context)->scratch, query, result, error);
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
    GAME_CHECK(qa_trace_scratch_create(map.geometry,&fixture.scratch,&error));
    qa_movement_services services = {.context = &fixture,
        .trace = movement_trace, .point_contents = movement_contents,
        .phase = movement_phase};
    for (qa_ruleset_id kind = QA_RULESET_NETQUAKE; kind <= QA_RULESET_Q3; ++kind) {
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
    qa_movement_input input = qa_movement_input_default(QA_RULESET_NETQUAKE,
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
    qa_trace_scratch_destroy(fixture.scratch);
    qa_collision_destroy(map.geometry);
}

static void exiting_body_over_world_hit(void)
{
    qa_error error = {0};
    gameplay_map map;
    gameplay_map_create(&map);
    qa_actor_registry *actors;
    qa_world *world;
    qa_actor_id actor;
    GAME_CHECK(qa_actors_create(8, NULL, NULL, &actors, &error));
    GAME_CHECK(qa_world_create(actors, map.geometry, NULL, 0, &world, &error));
    GAME_CHECK(qa_actors_allocate(actors, 1, 1, &actor, &error));
    qa_body_state body = {.origin = {0, 0, 24}, .bounds = {{-16, -16, -8}, {16, 16, 8}}};
    GAME_CHECK(qa_world_body_create(world, actor, &body, &error));
    uint8_t solid[4];
    qa_store_u32le(solid, 2);
    qa_entity_collision_fields fields = {.family = QA_GAME_Q2,
        .solid = {solid, QA_ENTITY_U32_LE}};
    qa_collision_binding binding = {.context = solid, .fields = &fields};
    GAME_CHECK(qa_world_collision_bind(world, actor, &binding, &error));
    GAME_CHECK(qa_world_link(world, actor, NULL, &error));
    qa_linked_body linked;
    GAME_CHECK(qa_world_linked(world, actor, &linked));
    uint64_t link_count = linked.link_count;
    qa_store_u32le(solid, 0);
    qa_trace_query live_query = {.start = {0, 0, 24}, .end = {0, 0, -24},
        .shape = {.kind = QA_SHAPE_POINT}, .policy = qa_collision_default_policy(QA_GAME_Q2)};
    qa_trace_result live_hit;
    GAME_CHECK(qa_world_trace(world, &live_query, &live_hit, &error));
    GAME_CHECK(live_hit.hit == QA_TRACE_HIT_WORLD && !live_hit.start_solid && live_hit.fraction < .5f);
    GAME_CHECK(qa_world_linked(world, actor, &linked) && linked.link_count == link_count);
    qa_store_u32le(solid, 2);
    for (unsigned caller = 0; caller < 4; ++caller) {
        qa_game_family family = caller == 0 ? QA_GAME_Q1 :
            caller == 3 ? QA_GAME_Q3 : QA_GAME_Q2;
        qa_trace_query query = {.start = {0, 0, 24}, .end = {0, 0, -24},
            .shape = {.kind = QA_SHAPE_POINT}, .policy = qa_collision_default_policy(family)};
        if (caller == 2) query.policy.behavior = &qa_trace_behaviors[QA_RULESET_Q2_RERELEASE];
        qa_trace_result hit;
        GAME_CHECK(qa_world_trace(world, &query, &hit, &error));
        GAME_CHECK(hit.start_solid && !hit.all_solid);
        if (family == QA_GAME_Q3) {
            GAME_CHECK(hit.hit == QA_TRACE_HIT_WORLD && hit.fraction < .5f);
        } else {
            GAME_CHECK(hit.hit == QA_TRACE_HIT_ACTOR && qa_actor_id_equal(hit.actor, actor));
            GAME_CHECK(hit.fraction == 1 && hit.end.z == -24);
        }
    }
    GAME_CHECK(qa_world_destroy(world, &error));
    GAME_CHECK(qa_actors_destroy(actors, &error));
    qa_collision_destroy(map.geometry);
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
    GAME_CHECK(qa_world_create(actors, map.geometry, NULL, 0, &world, &error));
    GAME_CHECK(qa_actors_allocate(actors, 1, 1, &actor, &error));
    qa_body_state body = {.origin = {0, 0, 128}, .bounds = {{-16, -16, -64}, {16, 16, 64}}};
    GAME_CHECK(qa_world_body_create(world, actor, &body, &error));
    uint8_t solid[4], model_index[4];
    gameplay_float(solid, 4);
    gameplay_float(model_index, 1);
    qa_entity_model_field model_entries[3] = {
        [1] = {.model = 0, .geometry = external.geometry, .present = true},
        [2] = {.model = 0, .geometry = map.geometry, .present = true}};
    qa_entity_model_fields models = {.entries = model_entries, .count = 3};
    qa_entity_collision_fields fields = {.family = QA_GAME_Q1,
        .solid = {solid, QA_ENTITY_F32_LE}, .model = {model_index, QA_ENTITY_F32_LE},
        .models = &models};
    qa_collision_binding binding = {.context = model_index, .fields = &fields};
    GAME_CHECK(qa_world_collision_bind(world, actor, &binding, &error));
    GAME_CHECK(qa_world_link(world, actor, NULL, &error));
    qa_linked_body linked;
    GAME_CHECK(qa_world_linked(world, actor, &linked));
    uint64_t link_count = linked.link_count;
    qa_trace_query live_query = {.start = {0, 0, 200}, .end = {0, 0, 160},
        .shape = {.kind = QA_SHAPE_POINT}, .policy = qa_collision_default_policy(QA_GAME_Q1)};
    qa_trace_result live_hit;
    GAME_CHECK(qa_world_trace(world, &live_query, &live_hit, &error));
    GAME_CHECK(live_hit.hit == QA_TRACE_HIT_ACTOR && qa_actor_id_equal(live_hit.actor, actor));
    GAME_CHECK(fabsf(live_hit.end.z - 192.03125f) < .0001f);
    gameplay_float(model_index, 2);
    GAME_CHECK(qa_world_trace(world, &live_query, &live_hit, &error));
    GAME_CHECK(live_hit.hit == QA_TRACE_HIT_NONE && live_hit.fraction == 1 && live_hit.end.z == 160);
    GAME_CHECK(qa_world_linked(world, actor, &linked) && linked.link_count == link_count);
    gameplay_float(model_index, 1);
    GAME_CHECK(qa_world_trace(world, &live_query, &live_hit, &error));
    GAME_CHECK(live_hit.hit == QA_TRACE_HIT_ACTOR && qa_actor_id_equal(live_hit.actor, actor));
    GAME_CHECK(fabsf(live_hit.end.z - 192.03125f) < .0001f);
    qa_world_checkpoint checkpoint = {0};
    GAME_CHECK(qa_world_checkpoint_capture(world, &checkpoint, &error));
    GAME_CHECK(checkpoint.body_count == 1 && checkpoint.spatial_count == 1);
    GAME_CHECK(checkpoint.bodies[0].collision.model_geometry == NULL &&
        checkpoint.bodies[0].stored_collision.model_geometry == NULL &&
        checkpoint.bodies[0].retained_collision.model_geometry == NULL);
    qa_collision_geometry *replacement;
    GAME_CHECK(qa_collision_create(&bsp, &replacement, &error));
    GAME_CHECK(qa_collision_load_generation(replacement) != 0 &&
        qa_collision_load_generation(replacement) != qa_collision_load_generation(external.geometry));
    GAME_CHECK(qa_collision_map_checksum(replacement) == qa_collision_map_checksum(external.geometry));
    qa_collision_portal_checkpoint portals = {0};
    GAME_CHECK(qa_collision_capture_portals(external.geometry, &portals, &error));
    GAME_CHECK(qa_collision_restore_portals(external.geometry, &portals, &error));
    GAME_CHECK(!qa_collision_restore_portals(replacement, &portals, &error));
    portals.map_generation = 0;
    GAME_CHECK(qa_collision_restore_portals(replacement, &portals, &error));
    qa_collision_portal_checkpoint_free(&portals);
    qa_collision_destroy(external.geometry);
    external.geometry = replacement;
    model_entries[1].geometry = replacement;
    GAME_CHECK(qa_world_prepare_trace_geometry(world,replacement,&error));
    GAME_CHECK(qa_world_checkpoint_restore(world, &checkpoint, NULL, NULL, &error));
    for (qa_game_family family = QA_GAME_Q1; family <= QA_GAME_Q3; ++family) {
        qa_trace_query query = {.start = {0, 0, 200}, .end = {0, 0, 160},
            .shape = {.kind = QA_SHAPE_POINT}, .policy = qa_collision_default_policy(family)};
        qa_trace_result hit;
        GAME_CHECK(qa_world_trace(world, &query, &hit, &error));
        GAME_CHECK(hit.hit == QA_TRACE_HIT_ACTOR && qa_actor_id_equal(hit.actor, actor));
        /* Stock caller clipping: Q3 SURFACE_CLIP_EPSILON is 1/8,
         * Q1/Q2 DIST_EPSILON is 1/32, independent of the restored brush format. */
        float expected_end = family == QA_GAME_Q3 ? 192.125f : 192.03125f;
        GAME_CHECK(fabsf(hit.end.z - expected_end) < .0001f);
        if (family != QA_GAME_Q1) {
            qa_point_query point = {.point = {0, 0, 180}, .policy = query.policy};
            qa_point_contents contents;
            GAME_CHECK(qa_world_point_contents(world, &point, &contents, &error));
            GAME_CHECK(qa_collision_bits_overlap(contents.contents, qa_collision_bit(QA_CONTENT_SOLID)));
        }
    }
    qa_world_checkpoint_free(&checkpoint);
    GAME_CHECK(qa_world_destroy(world, &error));
    GAME_CHECK(qa_actors_destroy(actors, &error));
    qa_collision_destroy(external.geometry);
    qa_collision_destroy(map.geometry);
}

static void draught_player(q1_fixture *fixture)
{
    qa_error error = {0};
    qa_builtin_services services = {.session = fixture->session, .world = fixture->world,
        .combat = fixture->combat, .inventory = fixture->inventory};
    qa_combat_state combat = {.health = 100, .can_take_damage = true};
    qa_string_id definition;
    GAME_CHECK(qa_strings_intern_cstr(qa_session_strings(fixture->session),
        "test:q1-ring-player", &definition, &error));
    qa_builtin_spawn spawn = {.owner = fixture->owner, .definition = definition,
        .body = {.origin = {128, 0, 1500}, .angles = {10, 20, 30}, .velocity = {11, 22, 33},
                 .bounds = {{-16, -16, -24}, {16, 16, 32}}}, .combat = &combat, .link = true};
    GAME_CHECK(qa_builtin_spawn_actor(&services, &spawn, &fixture->teleport_player, &error));
}

static void draught_ring(bool stupor, float authored_height, bool spawned)
{
    qa_error error = {0};
    q1_fixture fixture;
    fixture_create(&fixture, QA_Q1_RERELEASE, 1, false, QA_Q1_MG3);
    qa_actor_id ring;
    GAME_CHECK(qa_q1_game_spawn(fixture.game, &(qa_q1_spawn){
        .classname = stupor ? "item_draught_stupor" : "item_draught_insight",
        .origin = {0, 0, 64}, .spawnflags = spawned ? 65536u | 4u : 0,
        .map_fields = &(qa_q1_map_fields){.height = authored_height}}, &ring, &error));
    qa_body_state body;
    GAME_CHECK(qa_world_body_read(fixture.world, ring, &body, &error));
    GAME_CHECK(body.bounds.mins.x == -16 && body.bounds.mins.y == -16 && body.bounds.mins.z == -24);
    GAME_CHECK(body.bounds.maxs.x == 16 && body.bounds.maxs.y == 16 && body.bounds.maxs.z == 32);
    qa_combat_state health;
    GAME_CHECK(qa_combat_read(fixture.combat, ring, &health, &error) && health.health == 1);
    qa_target_binding binding;
    qa_target_field field;
    qa_string_id height_key;
    GAME_CHECK(qa_strings_intern_cstr(qa_session_strings(fixture.session), "height", &height_key, &error));
    GAME_CHECK(qa_q1_game_target_binding(fixture.game, ring, &binding, &error));
    GAME_CHECK(binding.field(binding.context, ring, height_key, &field));
    float height = authored_height != 0 ? authored_height : stupor ? 2048 : -2048;
    GAME_CHECK(field.kind == QA_TARGET_FIELD_NUMBER && field.value.number == height);
    GAME_CHECK(fixture.hum_sounds == (spawned ? 0u : 1u));
    if (!spawned) {
        GAME_CHECK(fixture.hum_static && fixture.hum_origin.z == 72);
    }
    advance(&fixture, 3);
    qa_q1_presentation visual;
    qa_physics_properties physics;
    GAME_CHECK(qa_q1_game_presentation(fixture.game, ring, &visual));
    GAME_CHECK(qa_q1_game_physics_read(fixture.game, ring, &physics));
    if (spawned) {
        GAME_CHECK(visual.visual.models[0] == QA_STRING_NONE && physics.solid == QA_PHYSICS_NOT_SOLID);
        GAME_CHECK(fixture.particle_events == 0);
        GAME_CHECK(qa_world_body_read(fixture.world, ring, &body, &error) && body.origin.z == 70);
        uint32_t before = qa_actors_count(qa_session_actor_registry(fixture.session));
        GAME_CHECK(qa_q1_game_use(fixture.game, ring, (qa_actor_id){0}, &error));
        GAME_CHECK(qa_actors_count(qa_session_actor_registry(fixture.session)) == before + 1);
        GAME_CHECK(qa_q1_game_use(fixture.game, ring, (qa_actor_id){0}, &error));
        GAME_CHECK(qa_actors_count(qa_session_actor_registry(fixture.session)) == before + 1);
        GAME_CHECK(fixture.hum_sounds == 0);
        advance(&fixture, 1);
    }
    GAME_CHECK(fixture.particle_events > 0);
    GAME_CHECK(fixture.particle_color == 3 && fixture.particle_count == 3);
    qa_vec3 offset = qa_vec_sub(fixture.particle_origin, qa_v3(0, 0, spawned ? 78 : 72));
    GAME_CHECK(fabsf(qa_vec_dot(offset, offset) - 48 * 48) < .02f);
    GAME_CHECK(qa_q1_game_presentation(fixture.game, ring, &visual) && visual.visual.models[0] != QA_STRING_NONE);
    draught_player(&fixture);
    GAME_CHECK(qa_q1_game_touch(fixture.game,
        &(qa_touch_contact){.self = ring, .other = fixture.teleport_player}, &error));
    GAME_CHECK(qa_world_body_read(fixture.world, fixture.teleport_player, &body, &error));
    float destination = 1500 + height;
    if (height < 0) destination += 1;
    GAME_CHECK(body.origin.x == 128 && body.origin.y == 0 && body.origin.z == destination);
    GAME_CHECK(body.angles.x == 10 && body.angles.y == 20 && body.angles.z == 30);
    GAME_CHECK(body.velocity.x == 11 && body.velocity.y == 22 && body.velocity.z == 33);
    GAME_CHECK(fixture.teleport_fogs == 1 && fixture.fog_origin.z == destination &&
        fixture.player_at_fog.z == destination);
    GAME_CHECK(fixture.teleport_moves == 1 && !fixture.teleport_motion.force_view_angles &&
        fixture.teleport_motion.hold_ns == 0);
    GAME_CHECK(fixture.retouches == 2);
    GAME_CHECK(qa_q1_game_presentation(fixture.game, ring, &visual) && visual.visual.models[0] != QA_STRING_NONE);
    GAME_CHECK(qa_q1_game_physics_read(fixture.game, ring, &physics) && physics.solid == QA_PHYSICS_TRIGGER);
    fixture_destroy(&fixture);
}

static void draught_target(void)
{
    qa_error error = {0};
    q1_fixture fixture;
    fixture_create(&fixture, QA_Q1_RERELEASE, 1, false, QA_Q1_MG3);
    fixture_bind_maps(&fixture);
    qa_actor_id destination, ring;
    GAME_CHECK(qa_q1_game_spawn(fixture.game, &(qa_q1_spawn){
        .classname = "info_teleport_destination", .targetname = "ring-destination",
        .origin = {300, 0, 64}, .angles = {0, 90, 0}}, &destination, &error));
    GAME_CHECK(qa_q1_game_spawn(fixture.game, &(qa_q1_spawn){
        .classname = "item_draught_insight", .target = "ring-destination",
        .origin = {0, 0, 64}, .spawnflags = 1}, &ring, &error));
    advance(&fixture, 3);
    draught_player(&fixture);
    GAME_CHECK(qa_q1_game_touch(fixture.game,
        &(qa_touch_contact){.self = ring, .other = fixture.teleport_player}, &error));
    qa_body_state body;
    GAME_CHECK(qa_world_body_read(fixture.world, fixture.teleport_player, &body, &error));
    GAME_CHECK(body.origin.x == 300 && body.origin.y == 0 && body.origin.z == 91);
    GAME_CHECK(body.angles.y == 90 && fabsf(body.velocity.x) < .001f && fabsf(body.velocity.y - 300) < .001f);
    GAME_CHECK(fixture.teleport_fogs == 2 && fixture.retouches == 2 && fixture.teleport_moves == 1);
    GAME_CHECK(fixture.teleport_motion.force_view_angles && fixture.teleport_motion.hold_ns == UINT64_C(700000000));
    fixture_destroy(&fixture);
}

static double hub_spawn_random(void *context)
{
    q1_fixture *fixture = context;
    ++fixture->sequence;
    return .5;
}

static void hub_spawn(qa_q1_program program)
{
    qa_error error = {0};
    q1_fixture fixture;
    fixture_create(&fixture, QA_Q1_RERELEASE, 1, false, program);
    fixture_bind_maps(&fixture);
    qa_actor_id start, returning, wrong, hub2, hub3, test;
    GAME_CHECK(qa_q1_game_spawn(fixture.game, &(qa_q1_spawn){
        .classname = "info_player_start", .origin = {0, 0, 64}}, &start, &error));
    GAME_CHECK(qa_q1_game_spawn(fixture.game, &(qa_q1_spawn){
        .classname = "info_player_start2", .origin = {10, 0, 64}}, &returning, &error));
    GAME_CHECK(qa_q1_game_spawn(fixture.game, &(qa_q1_spawn){
        .classname = "info_player_start", .origin = {20, 0, 64},
        .map_fields = &(qa_q1_map_fields){.netname = "start_2"}}, &wrong, &error));
    GAME_CHECK(qa_q1_game_spawn(fixture.game, &(qa_q1_spawn){
        .classname = "info_player_start_hub", .origin = {30, 0, 64},
        .map_fields = &(qa_q1_map_fields){.netname = "start_2"}}, &hub2, &error));
    GAME_CHECK(qa_q1_game_spawn(fixture.game, &(qa_q1_spawn){
        .classname = "info_player_start_hub", .origin = {40, 0, 64},
        .map_fields = &(qa_q1_map_fields){.netname = "start_3"}}, &hub3, &error));
    qa_q1_spawn_rule rule;
    GAME_CHECK(qa_q1_game_hub_spawn_rule(fixture.game, &rule));
    qa_q1_spawn_point points[3] = {{.actor = start, .kind = QA_Q1_SPAWN_START},
        {.actor = returning, .kind = QA_Q1_SPAWN_RETURN}};
    qa_q1_spawn_selector *selector = qa_q1_spawn_selector_create(&(qa_q1_spawn_options){
        .services = {.session = fixture.session, .world = fixture.world, .combat = fixture.combat},
        .server_flags = &fixture.server_flags, .rules = &rule, .rule_count = 1,
        .context = &fixture, .random = hub_spawn_random}, &error);
    GAME_CHECK(selector != NULL);
    qa_actor_id selected;
    const uint32_t retained = UINT32_C(1) << 20 | 3u;
    fixture.server_flags = retained | (2u | 4u) << QA_Q1_MG1_LAST_SIGIL_SHIFT;
    uint32_t flags = fixture.server_flags;
    GAME_CHECK(qa_q1_spawn_select(selector, points, 2, true, &selected, &error));
    GAME_CHECK(qa_actor_id_equal(selected, returning) && fixture.server_flags == flags);
    GAME_CHECK(qa_session_release(fixture.session, wrong, &error));
    GAME_CHECK(qa_q1_spawn_select(selector, points, 2, true, &selected, &error));
    GAME_CHECK(qa_actor_id_equal(selected, hub2) && fixture.server_flags == retained);
    fixture.server_flags = retained | 4u << QA_Q1_MG1_LAST_SIGIL_SHIFT;
    GAME_CHECK(qa_q1_spawn_select(selector, points, 2, true, &selected, &error));
    GAME_CHECK(qa_actor_id_equal(selected, hub3) && fixture.server_flags == retained);
    fixture.server_flags = retained | 1u << QA_Q1_MG1_LAST_SIGIL_SHIFT;
    flags = fixture.server_flags;
    GAME_CHECK(qa_q1_spawn_select(selector, points, 2, true, &selected, &error));
    GAME_CHECK(qa_actor_id_equal(selected, returning) && fixture.server_flags == flags);
    GAME_CHECK(qa_q1_game_spawn(fixture.game, &(qa_q1_spawn){
        .classname = "testplayerstart", .origin = {50, 0, 64}}, &test, &error));
    points[2] = (qa_q1_spawn_point){.actor = test, .kind = QA_Q1_SPAWN_TEST};
    fixture.server_flags = retained | 2u << QA_Q1_MG1_LAST_SIGIL_SHIFT;
    flags = fixture.server_flags;
    GAME_CHECK(qa_q1_spawn_select(selector, points, 3, true, &selected, &error));
    GAME_CHECK(qa_actor_id_equal(selected, test) && fixture.server_flags == flags);
    GAME_CHECK(fixture.sequence == 0);
    qa_q1_spawn_selector_destroy(selector);
    fixture_destroy(&fixture);
}

static void addon_spawn_multiplayer(qa_q1_program program, bool coop)
{
    qa_error error = {0};
    q1_fixture fixture;
    fixture_create_rules(&fixture, QA_Q1_RERELEASE, 1, false, program, coop, coop ? 0 : 1, QA_RULESET_NETQUAKE);
    fixture_bind_maps(&fixture);
    qa_actor_id hub;
    GAME_CHECK(qa_q1_game_spawn(fixture.game, &(qa_q1_spawn){
        .classname = "info_player_start", .origin = {30, 0, 64},
        .map_fields = &(qa_q1_map_fields){.netname = "start_2"}}, &hub, &error));
    qa_q1_spawn_rule rule;
    GAME_CHECK(qa_q1_game_hub_spawn_rule(fixture.game, &rule));
    const uint32_t flags = 3u | 2u << QA_Q1_MG1_LAST_SIGIL_SHIFT;
    fixture.server_flags = flags;
    qa_q1_spawn_decision decision = QA_Q1_SPAWN_SELECTED;
    qa_actor_id selected = hub;
    GAME_CHECK(rule.select(rule.context, true, &decision, &selected, &error));
    GAME_CHECK(decision == QA_Q1_SPAWN_DELEGATE && !selected.registry &&
        fixture.server_flags == flags);
    fixture_destroy(&fixture);
}

static void mg3_spawn(void)
{
    qa_error error = {0};
    q1_fixture fixture;
    fixture_create(&fixture, QA_Q1_RERELEASE, 1, false, QA_Q1_MG3);
    fixture_bind_maps(&fixture);
    qa_actor_id returning, first, second, test;
    GAME_CHECK(qa_q1_game_spawn(fixture.game, &(qa_q1_spawn){
        .classname = "info_player_start2", .origin = {0, 0, 64}}, &returning, &error));
    GAME_CHECK(qa_q1_game_spawn(fixture.game, &(qa_q1_spawn){
        .classname = "info_player_start", .origin = {10, 0, 64}}, &first, &error));
    GAME_CHECK(qa_q1_game_spawn(fixture.game, &(qa_q1_spawn){
        .classname = "info_player_start", .origin = {20, 0, 64}}, &second, &error));
    qa_q1_spawn_rule rule;
    GAME_CHECK(qa_q1_game_hub_spawn_rule(fixture.game, &rule));
    qa_q1_spawn_point points[4] = {{.actor = returning, .kind = QA_Q1_SPAWN_RETURN},
        {.actor = first, .kind = QA_Q1_SPAWN_START},
        {.actor = second, .kind = QA_Q1_SPAWN_START}};
    qa_q1_spawn_selector *selector = qa_q1_spawn_selector_create(&(qa_q1_spawn_options){
        .services = {.session = fixture.session, .world = fixture.world, .combat = fixture.combat},
        .server_flags = &fixture.server_flags, .rules = &rule, .rule_count = 1,
        .context = &fixture, .random = hub_spawn_random}, &error);
    GAME_CHECK(selector != NULL);
    const uint32_t flags = 3u | 2u << QA_Q1_MG1_LAST_SIGIL_SHIFT;
    fixture.server_flags = flags;
    qa_actor_id selected;
    GAME_CHECK(qa_q1_spawn_select(selector, points, 3, true, &selected, &error));
    GAME_CHECK(qa_actor_id_equal(selected, first) && fixture.server_flags == flags);
    fixture.server_flags = 0;
    GAME_CHECK(qa_q1_spawn_select(selector, points, 3, true, &selected, &error));
    GAME_CHECK(qa_actor_id_equal(selected, first) && fixture.server_flags == 0);
    fixture.server_flags = flags;
    GAME_CHECK(qa_session_release(fixture.session, first, &error));
    GAME_CHECK(qa_q1_spawn_select(selector, points, 3, true, &selected, &error));
    GAME_CHECK(qa_actor_id_equal(selected, second) && fixture.server_flags == flags);
    GAME_CHECK(qa_session_release(fixture.session, second, &error));
    GAME_CHECK(qa_q1_spawn_select(selector, points, 3, true, &selected, &error));
    GAME_CHECK(!selected.registry && fixture.server_flags == flags);
    GAME_CHECK(qa_q1_game_spawn(fixture.game, &(qa_q1_spawn){
        .classname = "testplayerstart", .origin = {30, 0, 64}}, &test, &error));
    points[3] = (qa_q1_spawn_point){.actor = test, .kind = QA_Q1_SPAWN_TEST};
    GAME_CHECK(qa_q1_spawn_select(selector, points, 4, true, &selected, &error));
    GAME_CHECK(qa_actor_id_equal(selected, test) && fixture.server_flags == flags);
    GAME_CHECK(fixture.sequence == 0);
    qa_q1_spawn_selector_destroy(selector);
    fixture_destroy(&fixture);
}

static qa_actor_id shooter_projectile(q1_fixture *fixture, const char *definition)
{
    uint32_t cursor = 0;
    const qa_actor_record *record;
    qa_actor_id found = {0};
    while (qa_actors_next(qa_session_actors(fixture->session), &cursor, &record)) {
        const char *name = qa_strings_cstr(qa_session_strings(fixture->session), record->definition);
        if (name && !strcmp(name, definition))
            found = record->id;
    }
    return found;
}

static void shooter_resource(q1_fixture *fixture, const char *path, bool model)
{
    qa_error error = {0};
    qa_string_id name = qa_strings_find(qa_session_strings(fixture->session),
        (qa_bytes){(const uint8_t *)path, strlen(path)});
    uint32_t index;
    GAME_CHECK(name && qa_q1_wire_emission_index(fixture->game, model, name, &index));
}

static void mission_shooter_switch(void)
{
    qa_error error = {0};
    q1_fixture fixture;
    fixture_create(&fixture, QA_Q1_RERELEASE, 1, true, QA_Q1_HIPNOTIC);
    fixture_bind_maps(&fixture);
    qa_actor_id shooter;
    GAME_CHECK(qa_q1_game_spawn(fixture.game, &(qa_q1_spawn){
        .classname = "trap_switched_shooter", .origin = {0, 0, 64}, .wait = .25f,
        .map_fields = &(qa_q1_map_fields){.next_think_seconds = 1.25f,
            .local_time_seconds = .25f}}, &shooter, &error));
    shooter_resource(&fixture, "weapons/spike2.wav", false);
    GAME_CHECK(qa_q1_wire_freeze(fixture.game, &error));
    qa_target_binding binding;
    qa_target_field field;
    qa_string_id state;
    GAME_CHECK(qa_strings_intern_cstr(qa_session_strings(fixture.session), "state", &state, &error));
    GAME_CHECK(qa_q1_game_target_binding(fixture.game, shooter, &binding, &error));
    advance(&fixture, 8);
    GAME_CHECK(fixture.shot_sounds == 0 && !shooter_projectile(&fixture, "spike").registry);
    GAME_CHECK(qa_q1_game_use(fixture.game, shooter, (qa_actor_id){0}, &error));
    GAME_CHECK(binding.field(binding.context, shooter, state, &field) &&
        field.kind == QA_TARGET_FIELD_NUMBER && field.value.number == 1);
    advance(&fixture, 3);
    GAME_CHECK(fixture.shot_sounds == 1 && shooter_projectile(&fixture, "spike").registry);
    GAME_CHECK(qa_q1_game_use(fixture.game, shooter, (qa_actor_id){0}, &error));
    GAME_CHECK(binding.field(binding.context, shooter, state, &field) &&
        field.kind == QA_TARGET_FIELD_NUMBER && field.value.number == 0);
    advance(&fixture, 3);
    GAME_CHECK(fixture.shot_sounds == 1);
    fixture_destroy(&fixture);
}

static void hipnotic_shooter(uint32_t flags)
{
    qa_error error = {0};
    q1_fixture fixture;
    fixture_create(&fixture, QA_Q1_RERELEASE, 1, true, QA_Q1_HIPNOTIC);
    fixture_bind_maps(&fixture);
    qa_actor_id shooter;
    GAME_CHECK(qa_q1_game_spawn(fixture.game, &(qa_q1_spawn){
        .classname = "trap_spikeshooter", .origin = {0, 0, 64}, .spawnflags = flags,
        .map_fields = &(qa_q1_map_fields){.ammo_rockets = 2, .current_ammo = 7,
            .punch_angle = {3, 4, 5}}}, &shooter, &error));
    shooter_resource(&fixture, flags & 2u ? "progs/laser.mdl" : flags & 4u ?
        "progs/lavarock.mdl" : "progs/missile.mdl", true);
    shooter_resource(&fixture, flags & 2u ? "enforcer/enfstop.wav" : flags & 4u ?
        "misc/spike.wav" : "weapons/sgun1.wav", false);
    GAME_CHECK(qa_q1_wire_freeze(fixture.game, &error));
    GAME_CHECK(qa_q1_game_use(fixture.game, shooter, (qa_actor_id){0}, &error));
    const char *definition = flags & 2u ? "enforcer_laser" : flags & 4u ? "chthon_lavaball" : "missile";
    qa_actor_id shot = shooter_projectile(&fixture, definition);
    GAME_CHECK(shot.registry);
    qa_body_state body;
    qa_physics_properties physics;
    qa_q1_presentation visible;
    GAME_CHECK(qa_world_body_read(fixture.world, shot, &body, &error));
    GAME_CHECK(qa_q1_game_physics_read(fixture.game, shot, &physics));
    GAME_CHECK(qa_q1_game_presentation(fixture.game, shot, &visible));
    const char *model = qa_strings_cstr(qa_session_strings(fixture.session), visible.visual.models[0]);
    if (flags & 2u) {
        GAME_CHECK(body.velocity.x == 600 && physics.motion == QA_PHYSICS_FLY);
        GAME_CHECK(model && !strcmp(model, "progs/laser.mdl"));
        unsigned sounds = fixture.shot_sounds;
        GAME_CHECK(sounds == ((flags & 16u) ? 0u : 1u));
        GAME_CHECK(qa_q1_game_touch(fixture.game, &(qa_touch_contact){.self = shot}, &error));
        GAME_CHECK(fixture.shot_sounds == sounds + ((flags & 16u) ? 0u : 1u));
        GAME_CHECK(fixture.impacts == 1 && fixture.impact_value == 1);
        GAME_CHECK(!qa_actors_get(qa_session_actors(fixture.session), shot));
    } else if (flags & 4u) {
        GAME_CHECK(body.velocity.x == 300 && physics.angular_velocity.z == 400);
        GAME_CHECK(body.bounds.mins.x == -4 && body.bounds.maxs.x == 4);
        GAME_CHECK(model && !strcmp(model, "progs/lavarock.mdl"));
        GAME_CHECK(fixture.shot_sounds == ((flags & 16u) ? 0u : 1u));
        advance(&fixture, 49);
        GAME_CHECK(qa_actors_get(qa_session_actors(fixture.session), shot));
        advance(&fixture, 2);
        GAME_CHECK(!qa_actors_get(qa_session_actors(fixture.session), shot));
    } else {
        GAME_CHECK(body.velocity.x == 1000 && body.origin.x == 8);
        GAME_CHECK(model && !strcmp(model, "progs/missile.mdl"));
        GAME_CHECK(fixture.shot_sounds == ((flags & 16u) ? 1u : 2u) && fixture.sound_channel == 1);
        qa_target_binding binding;
        qa_target_field field;
        qa_string_id rockets, current, punch;
        GAME_CHECK(qa_strings_intern_cstr(qa_session_strings(fixture.session), "ammo_rockets", &rockets, &error));
        GAME_CHECK(qa_strings_intern_cstr(qa_session_strings(fixture.session), "currentammo", &current, &error));
        GAME_CHECK(qa_strings_intern_cstr(qa_session_strings(fixture.session), "punchangle", &punch, &error));
        GAME_CHECK(qa_q1_game_target_binding(fixture.game, shooter, &binding, &error));
        GAME_CHECK(binding.field(binding.context, shooter, rockets, &field) &&
            field.kind == QA_TARGET_FIELD_NUMBER && field.value.number == 1);
        GAME_CHECK(binding.field(binding.context, shooter, current, &field) &&
            field.kind == QA_TARGET_FIELD_NUMBER && field.value.number == 1);
        GAME_CHECK(binding.field(binding.context, shooter, punch, &field) &&
            field.kind == QA_TARGET_FIELD_VECTOR && qa_vec_equal(field.value.vector, qa_v3(-2, 4, 5)));
        GAME_CHECK(qa_q1_game_use(fixture.game, shooter, (qa_actor_id){0}, &error));
        GAME_CHECK(qa_q1_game_use(fixture.game, shooter, (qa_actor_id){0}, &error));
        GAME_CHECK(binding.field(binding.context, shooter, rockets, &field) &&
            field.kind == QA_TARGET_FIELD_NUMBER && field.value.number == -1);
        GAME_CHECK(binding.field(binding.context, shooter, current, &field) &&
            field.kind == QA_TARGET_FIELD_NUMBER && field.value.number == -1);
    }
    fixture_destroy(&fixture);
}

static void rogue_shooter(uint32_t flags, uint8_t skill, bool continuous)
{
    qa_error error = {0};
    q1_fixture fixture;
    fixture_create(&fixture, QA_Q1_RERELEASE, skill, true, QA_Q1_ROGUE);
    fixture_bind_maps(&fixture);
    qa_actor_id target = {0};
    if (!(flags & (2u | 32u))) {
        GAME_CHECK(qa_q1_game_spawn(fixture.game, &(qa_q1_spawn){
            .classname = "monster_knight", .origin = {100, 0, 64}}, &target, &error));
        advance(&fixture, 6);
    }
    qa_actor_id shooter;
    GAME_CHECK(qa_q1_game_spawn(fixture.game, &(qa_q1_spawn){
        .classname = continuous ? "trap_shooter" : "trap_spikeshooter",
        .origin = {0, 0, 64}, .spawnflags = flags}, &shooter, &error));
    shooter_resource(&fixture, flags & 2u ? "progs/laser.mdl" : flags & 32u ?
        "progs/fireball.mdl" : "progs/lspike.mdl", true);
    GAME_CHECK(qa_q1_wire_freeze(fixture.game, &error));
    if (continuous)
        advance(&fixture, 11);
    else
        GAME_CHECK(qa_q1_game_use(fixture.game, shooter, (qa_actor_id){0}, &error));
    const char *definition = flags & 2u ? "enforcer_laser" : flags & 32u ? "fireball" : "lava_spike";
    qa_actor_id shot = shooter_projectile(&fixture, definition);
    GAME_CHECK(shot.registry);
    qa_body_state body;
    GAME_CHECK(qa_world_body_read(fixture.world, shot, &body, &error));
    if (flags & 2u)
        GAME_CHECK(body.velocity.x == (continuous ? 500 : 600));
    else if (flags & 32u)
        GAME_CHECK(body.velocity.x >= 900 && body.velocity.x < 1200 && fixture.shot_sounds == 0);
    else {
        GAME_CHECK(body.velocity.x == 500);
        qa_combat_state before, after;
        GAME_CHECK(qa_combat_read(fixture.combat, target, &before, &error));
        GAME_CHECK(before.can_take_damage);
        GAME_CHECK(qa_q1_game_touch(fixture.game, &(qa_touch_contact){.self = shot, .other = target}, &error));
        GAME_CHECK(qa_combat_read(fixture.combat, target, &after, &error));
        bool powered = (flags & 8u) || ((flags & 16u) && skill > 1);
        GAME_CHECK(before.health - after.health == (powered ? 30 : 15));
    }
    fixture_destroy(&fixture);
}

static void rogue_shooter_newmis(void)
{
    qa_error error = {0};
    q1_fixture fixture;
    fixture_create(&fixture, QA_Q1_RERELEASE, 1, true, QA_Q1_ROGUE);
    fixture_bind_maps(&fixture);
    qa_actor_id laser, dragon;
    GAME_CHECK(qa_q1_game_spawn(fixture.game, &(qa_q1_spawn){
        .classname = "trap_spikeshooter", .origin = {0, 0, 64}, .spawnflags = 2}, &laser, &error));
    GAME_CHECK(qa_q1_game_spawn(fixture.game, &(qa_q1_spawn){
        .classname = "trap_shooter", .origin = {0, 64, 64}, .angles = {0, 90, 0},
        .spawnflags = 32}, &dragon, &error));
    GAME_CHECK(qa_q1_wire_freeze(fixture.game, &error));
    GAME_CHECK(qa_q1_game_use(fixture.game, laser, (qa_actor_id){0}, &error));
    qa_actor_id previous = shooter_projectile(&fixture, "enforcer_laser");
    uint32_t source_slot;
    GAME_CHECK(previous.registry && qa_q1_wire_emission_slot(fixture.game, previous, &source_slot));
    advance(&fixture, 1);
    qa_body_state body;
    GAME_CHECK(qa_world_body_read(fixture.world, previous, &body, &error));
    GAME_CHECK(fabsf(body.velocity.x) < .001f && body.velocity.y == 500);
    qa_actor_id fireball = shooter_projectile(&fixture, "fireball");
    GAME_CHECK(fireball.registry && qa_world_body_read(fixture.world, fireball, &body, &error));
    GAME_CHECK(qa_vec_length(body.velocity) >= 900 && qa_vec_length(body.velocity) < 1200);
    GAME_CHECK(qa_session_release(fixture.session, previous, &error));
    qa_actor_id replacement;
    GAME_CHECK(qa_q1_game_spawn(fixture.game, &(qa_q1_spawn){
        .classname = "info_teleport_destination", .targetname = "replacement",
        .origin = {0, 0, 128}}, &replacement, &error));
    GAME_CHECK(!qa_actor_id_equal(previous, replacement));
    uint32_t replacement_slot;
    GAME_CHECK(qa_q1_wire_emission_slot(fixture.game, replacement, &replacement_slot) &&
        replacement_slot == source_slot);
    advance(&fixture, 10);
    GAME_CHECK(qa_world_body_read(fixture.world, replacement, &body, &error));
    GAME_CHECK(fabsf(body.velocity.x) < .001f && body.velocity.y == 500);
    fixture_destroy(&fixture);
}

static void qw_shooter_newmis(void)
{
    qa_error error = {0};
    q1_fixture fixture;
    fixture_create_rules(&fixture, QA_Q1_CLASSIC, 1, true, QA_Q1_ID1, false, 0, QA_RULESET_QUAKEWORLD);
    fixture_bind_maps(&fixture);
    qa_actor_id shooter;
    GAME_CHECK(qa_q1_game_spawn(fixture.game, &(qa_q1_spawn){
        .classname = "trap_spikeshooter", .origin = {0, 0, 64}}, &shooter, &error));
    GAME_CHECK(qa_q1_wire_freeze(fixture.game, &error));
    GAME_CHECK(qa_q1_game_use(fixture.game, shooter, (qa_actor_id){0}, &error));
    qa_actor_id shot = shooter_projectile(&fixture, "spike");
    GAME_CHECK(shot.registry);
    GAME_CHECK(qa_q1_game_run_newmis(fixture.game, &error));
    GAME_CHECK(qa_q1_game_run_newmis(fixture.game, &error));
    qa_body_state body;
    GAME_CHECK(qa_world_body_read(fixture.world, shot, &body, &error));
    GAME_CHECK(body.origin.x == 25);
    advance(&fixture, 1);
    GAME_CHECK(qa_world_body_read(fixture.world, shot, &body, &error));
    GAME_CHECK(body.origin.x == 75 && body.velocity.x == 500);
    uint64_t time;
    double elapsed;
    qa_clock_state clock;
    GAME_CHECK(qa_q1_game_clock_read(fixture.game, &time, &elapsed));
    GAME_CHECK(elapsed == .1 && qa_session_clock(fixture.session, fixture.owner, &clock));
    GAME_CHECK(clock.elapsed_ns == UINT64_C(100000000) && clock.frame_number == 1);
    fixture_destroy(&fixture);
}

void test_q1_gameplay(void);
void test_q1_gameplay(void)
{
    hub_spawn(QA_Q1_MG1);
    hub_spawn(QA_Q1_DOPA);
    addon_spawn_multiplayer(QA_Q1_MG1, true);
    addon_spawn_multiplayer(QA_Q1_MG1, false);
    addon_spawn_multiplayer(QA_Q1_MG3, true);
    addon_spawn_multiplayer(QA_Q1_MG3, false);
    mg3_spawn();
    mission_shooter_switch();
    hipnotic_shooter(2);
    hipnotic_shooter(2 | 16);
    hipnotic_shooter(4 | 16);
    hipnotic_shooter(8 | 16);
    rogue_shooter(4, 1, false);
    rogue_shooter(8, 1, false);
    rogue_shooter(16, 1, false);
    rogue_shooter(16, 2, false);
    rogue_shooter(2, 1, true);
    rogue_shooter(32, 1, false);
    rogue_shooter_newmis();
    qw_shooter_newmis();
    draught_ring(false, 0, false);
    draught_ring(true, 0, true);
    draught_ring(false, 128, false);
    draught_target();
    thin_brush_bounds();
    dormant_trigger_activation();
    nonsolid_inline_appearance();
    relay_template_unpublished();
    retained_external_brush();
    exiting_body_over_world_hit();
    movement_output_owner();
    donor_check_client();
    navigation_hurt_state(QA_Q1_CLASSIC);
    navigation_hurt_state(QA_Q1_RERELEASE);
    armor_and_protection();
    foreign_shambler_resistance();
    pain_cooldown(1);
    pain_cooldown(3);
    edition_damage(QA_Q1_CLASSIC);
    edition_damage(QA_Q1_RERELEASE);
}
