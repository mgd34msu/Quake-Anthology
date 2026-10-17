#include "gameplay_fixture.h"
#include "../src/gameplay/q2/entities/internal.h"

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

