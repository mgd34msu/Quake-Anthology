#include "gameplay_fixture.h"
#include "qa/game_q1.h"
#include "qa/game_q1_wire.h"

typedef struct q1_fixture {
    gameplay_map map;
    qa_session *session;
    qa_world *world;
    qa_combat *combat;
    qa_inventory *inventory;
    qa_physics physics;
    qa_q1_game *game;
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

static bool check_client(void *context, qa_actor_id observer, qa_actor_id *target)
{
    qa_error error = {0};
    q1_fixture *fixture = context;
    GAME_CHECK(qa_q1_game_check_client(fixture->game, observer, client_eye, fixture,
        target, &error));
    return true;
}

static bool released(void *context, qa_session *session, qa_actor_record actor, qa_error *error)
{
    (void)session;
    q1_fixture *fixture = context;
    qa_inventory_actor_released(fixture->inventory, actor);
    qa_combat_actor_released(fixture->combat, actor);
    return qa_world_actor_released(fixture->world, actor, error);
}

static void fixture_create(q1_fixture *fixture, qa_q1_edition edition, uint8_t skill)
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
        .physics = &fixture->physics, .emit = emitted, .context = fixture};
    qa_q1_options options = {.provider = fixture->owner, .combat_provider = fixture->owner,
        .inventory_provider = fixture->owner, .program = QA_Q1_ID1, .edition = edition,
        .skill = skill, .gravity = 800, .aim_threshold = 2, .max_clients = 1};
    qa_q1_host host = {.context = fixture, .check_client = check_client};
    GAME_CHECK(qa_q1_game_create(&services, &options, &host, &fixture->game, &error));
    qa_component component;
    GAME_CHECK(qa_q1_game_component(fixture->game, &component, &error));
    GAME_CHECK(qa_session_add(fixture->session, &component, &error));
    qa_combat_policy policy;
    GAME_CHECK(qa_q1_game_combat_policy(fixture->game, &policy, &error));
    GAME_CHECK(qa_combat_register_policy(fixture->combat, &policy, &error));
    GAME_CHECK(qa_q1_wire_begin_world(fixture->game, "maps/test.bsp", 0, 1, &error));
}

static void fixture_destroy(q1_fixture *fixture)
{
    qa_error error = {0};
    GAME_CHECK(qa_session_retire_actors(fixture->session, &error));
    GAME_CHECK(qa_combat_unregister_policy(fixture->combat, fixture->owner, &error));
    GAME_CHECK(qa_session_remove(fixture->session, fixture->owner, &error));
    qa_q1_game_destroy(fixture->game);
    GAME_CHECK(qa_inventory_destroy(fixture->inventory, &error));
    GAME_CHECK(qa_combat_destroy(fixture->combat, &error));
    GAME_CHECK(qa_world_destroy(fixture->world, &error));
    GAME_CHECK(qa_session_destroy(fixture->session, &error));
    qa_collision_destroy(fixture->map.geometry);
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
    fixture_create(&fixture, QA_Q1_CLASSIC, 1);
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
    fixture_create(&fixture, QA_Q1_CLASSIC, skill);
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
    fixture_create(&fixture, edition, 1);
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

void test_q1_gameplay(void);
void test_q1_gameplay(void)
{
    armor_and_protection();
    pain_cooldown(1);
    pain_cooldown(3);
    edition_damage(QA_Q1_CLASSIC);
    edition_damage(QA_Q1_RERELEASE);
}
