#include "gameplay_fixture.h"
#include "../src/gameplay/q2/entities/internal.h"
#include "../src/gameplay/q2/monsters/internal.h"

typedef struct brush_animation_output { int32_t frame; unsigned count; } brush_animation_output;
static bool brush_animation_visual(void *context, qa_actor_id actor,
    const qa_entity_visual *visual, qa_error *error)
{
    (void)actor;(void)error;
    brush_animation_output *output=context;
    output->frame=visual->frame;
    ++output->count;
    return true;
}
void test_q2_brush_animation(void)
{
    qa_error error={0};qa_session *session=NULL;
    GAME_CHECK(qa_session_create(&(qa_session_options){.actor_capacity=1,.component_capacity=1},&session,&error));
    qa_string_id name;qa_actor_id actor;
    GAME_CHECK(qa_strings_intern_cstr(qa_session_strings(session),"animated-brush",&name,&error));
    GAME_CHECK(qa_actors_allocate(qa_session_actor_registry(session),name,name,&actor,&error));
    brush_animation_output output={0};
    q2_entity_state state={.visual.frame=2,
        .brush_animation={.start=2,.end=4,.speed=50,.enabled=true,
            .alt_start=10,.alt_end=12,.alt_speed=25}};
    q2_actor row={.id=actor,.entity=&state},*rows[]={&row};
    q2_entities entities={.services={.context=&output,.visual=brush_animation_visual}};
    qa_q2_game game={.services.session=session,.actors=rows,.capacity=1,
        .entity_runtime=&entities,.now_ns=100*Q2_MS};
    GAME_CHECK(q2_entity_prethink(&game,&row,&error));
    GAME_CHECK(output.count==1 && output.frame==3 && state.brush_animation.next_tick_ns==150*Q2_MS);
    game.now_ns=125*Q2_MS;
    GAME_CHECK(q2_entity_prethink(&game,&row,&error) && output.count==1);
    game.now_ns=150*Q2_MS;
    GAME_CHECK(q2_entity_prethink(&game,&row,&error) && output.frame==4);
    game.now_ns=200*Q2_MS;
    GAME_CHECK(q2_entity_prethink(&game,&row,&error) && output.frame==2);
    state.brush_animation.alternate=true;
    GAME_CHECK(q2_entity_prethink(&game,&row,&error) && output.frame==12);
    GAME_CHECK(state.brush_animation.currently_alternate && state.brush_animation.next_tick_ns==225*Q2_MS);
    game.now_ns=225*Q2_MS;
    GAME_CHECK(q2_entity_prethink(&game,&row,&error) && output.frame==10);
    state.visual.frame=12;state.brush_animation.alt_nowrap=true;
    game.now_ns=250*Q2_MS;
    GAME_CHECK(q2_entity_prethink(&game,&row,&error) && output.frame==12);
    state.brush_animation.alt_nowrap=false;state.brush_animation.alt_style=1;
    state.visual.frame=10;game.now_ns=275*Q2_MS;
    GAME_CHECK(q2_entity_prethink(&game,&row,&error) && output.frame==12);
    state.brush_animation.alt_start=5;state.brush_animation.alt_end=9;
    state.brush_animation.alt_style=2;state.brush_animation.alt_speed=0;
    game.now_ns=300*Q2_MS;q2_rerelease_seed(&game,123);
    for(unsigned i=0;i<16;++i) {
        GAME_CHECK(q2_entity_prethink(&game,&row,&error));
        GAME_CHECK(output.frame>=5 && output.frame<=9);
    }
    unsigned delivered=output.count;state.brush_animation.enabled=false;
    GAME_CHECK(q2_entity_prethink(&game,&row,&error) && output.count==delivered);
    GAME_CHECK(qa_session_destroy(session,&error));
}

static bool anger_fields(void *context, qa_actor_id actor, qa_authored_target *out)
{
    (void)actor;
    *out = *(qa_authored_target *)context;
    return true;
}

static bool anger_foreign(void *context, qa_actor_id actor, qa_actor_id target, qa_error *error)
{
    (void)actor; (void)target; (void)error;
    ++*(unsigned *)context;
    return true;
}

void test_q2_target_anger_editions(void)
{
    qa_error error = {0};
    qa_session *session = NULL;
    qa_combat *combat = NULL;
    GAME_CHECK(qa_session_create(&(qa_session_options){.actor_capacity = 3, .component_capacity = 1},
        &session, &error));
    qa_strings *strings = qa_session_strings(session);
    qa_string_id owner, victim_name, angry_name;
    GAME_CHECK(qa_strings_intern_cstr(strings, "anger-owner", &owner, &error));
    GAME_CHECK(qa_strings_intern_cstr(strings, "victim", &victim_name, &error));
    GAME_CHECK(qa_strings_intern_cstr(strings, "angry", &angry_name, &error));
    qa_actor_registry *actors = qa_session_actor_registry(session);
    qa_actor_id source, victim, angry;
    GAME_CHECK(qa_actors_allocate(actors, owner, owner, &source, &error));
    GAME_CHECK(qa_actors_allocate(actors, owner, owner, &victim, &error));
    GAME_CHECK(qa_actors_allocate(actors, owner, owner, &angry, &error));
    GAME_CHECK(qa_combat_create(actors, strings, NULL, &combat, &error));
    GAME_CHECK(qa_combat_create_actor(combat, victim, &(qa_combat_state){.health = 37}, &error));
    GAME_CHECK(qa_combat_create_actor(combat, angry, &(qa_combat_state){.health = 1}, &error));
    qa_targets *targets = qa_targets_create(&(qa_target_options){.session = session}, &error);
    GAME_CHECK(targets != NULL);
    qa_authored_target victim_fields = {.targetname = victim_name};
    qa_authored_target angry_fields = {.targetname = angry_name};
    GAME_CHECK(qa_targets_bind(targets, &(qa_target_binding){.actor = victim,
        .source = QA_RULESET_Q2_CLASSIC, .context = &victim_fields, .read = anger_fields}, &error));
    GAME_CHECK(qa_targets_bind(targets, &(qa_target_binding){.actor = angry,
        .source = QA_RULESET_NETQUAKE, .context = &angry_fields, .read = anger_fields}, &error));
    q2_entity_state state = {.kind = Q2E_ANGER, .target = angry_name, .killtarget = victim_name};
    q2_actor source_row = {.id = source, .entity = &state};
    struct qa_q2_monster victim_ai = {0};
    q2_actor victim_row = {.id = victim, .monster = &victim_ai};
    q2_actor *rows[3] = {0};
    rows[source.slot] = &source_row; rows[victim.slot] = &victim_row;
    unsigned acquisitions = 0;
    q2_entities entities = {.services = {.context = &acquisitions, .targets = targets,
        .target_anger = anger_foreign}};
    qa_q2_game game = {.services = {.session = session, .combat = combat},
        .options.owner = owner, .actors = rows, .capacity = 3, .entity_runtime = &entities};
    const struct {
        qa_ruleset_id rules;
        bool already_monster;
        double attacker_health, victim_health;
        unsigned acquisitions;
        bool good_guy, not_counted;
    } cases[] = {
        {QA_RULESET_Q2_RERELEASE, true, 1, 37, 1, false, false},
        {QA_RULESET_Q2_RERELEASE, true, 0, 37, 0, false, false},
        {QA_RULESET_Q2_CLASSIC, true, 0, 300, 1, true, false},
        {QA_RULESET_Q2_RERELEASE, false, 1, 300, 1, true, true}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); ++i) {
        game.options.rules = cases[i].rules;
        victim_row.physics.flags = cases[i].already_monster ? QA_PHYSICS_MONSTER : 0;
        victim_ai.good_guy = victim_ai.do_not_count = false;
        GAME_CHECK(qa_combat_set_health(combat, victim, 37, &error));
        GAME_CHECK(qa_combat_set_health(combat, angry, cases[i].attacker_health, &error));
        acquisitions = 0;
        bool handled;
        GAME_CHECK(q2_target_extra_use(&game, &source_row, (qa_actor_id){0},
            (qa_actor_id){0}, &handled, &error) && handled);
        qa_combat_state result;
        GAME_CHECK(qa_combat_read(combat, victim, &result, &error));
        GAME_CHECK(result.health == cases[i].victim_health && acquisitions == cases[i].acquisitions);
        GAME_CHECK(victim_ai.good_guy == cases[i].good_guy && victim_ai.do_not_count == cases[i].not_counted);
        GAME_CHECK(victim_row.physics.flags & QA_PHYSICS_MONSTER);
    }
    qa_targets_destroy(targets);
    GAME_CHECK(qa_combat_destroy(combat, &error));
    GAME_CHECK(qa_session_destroy(session, &error));
}

