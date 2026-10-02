#include "native_q1_composition_player.h"
#include "native_q1_composition.h"
#include "qa/game_q1_rogue.h"

static application_provider *rogue_player(qa_application *app, qa_actor_owner owner,
    qa_actor_id actor, qa_error *error) {
    bool observer;
    if (!application_native_q1_composition_player_current(app, owner, QA_MODE_ROGUE,
        actor, &observer, error)) return NULL;
    application_provider *source = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (!source || source->owner != owner || source->kind != APPLICATION_PROVIDER_Q1 ||
        !source->state.q1) {
        application_fail(error, QA_ERROR_ARGUMENT, "Rogue player lost its actual native source");
        return NULL;
    }
    return source;
}
bool application_native_q1_rogue_state(void *context, qa_actor_owner owner,
    qa_actor_id actor, qa_actor_id *out, qa_error *error) {
    qa_application *app = context;
    application_provider *source = rogue_player(app, owner, actor, error);
    qa_actor_id state;
    if (!out || !source || !qa_q1_rogue_state(source->state.q1, actor, &state, error) ||
        rogue_player(app, owner, actor, error) != source ||
        !qa_q1_rogue_state_current(source->state.q1, actor, state, error)) return false;
    *out = state;
    return true;
}
bool application_native_q1_rogue_state_current(void *context, qa_actor_owner owner,
    qa_actor_id actor, qa_actor_id state, qa_error *error) {
    application_provider *source = rogue_player(context, owner, actor, error);
    qa_actor_id actual;
    bool found;
    return source && qa_q1_rogue_state_current(source->state.q1, actor, state, error) &&
        qa_q1_rogue_state_find(source->state.q1, actor, &actual, &found, error) && found &&
        qa_actor_id_equal(actual, state);
}
bool application_native_q1_rogue_number_read(void *context, qa_actor_owner owner,
    qa_actor_id actor, qa_actor_id state, uint32_t field, double *out, qa_error *error) {
    qa_application *app = context;
    application_provider *source = rogue_player(app, owner, actor, error);
    double value;
    if (!out || field >= QA_Q1_ROGUE_FIELDS || !source ||
        !application_native_q1_rogue_state_current(app, owner, actor, state, error) ||
        !qa_q1_rogue_number_read(source->state.q1, state, (qa_q1_rogue_field)field,
            &value, error) || rogue_player(app, owner, actor, error) != source ||
        !application_native_q1_rogue_state_current(app, owner, actor, state, error)) return false;
    *out = value;
    return true;
}
bool application_native_q1_rogue_number_write(void *context, qa_actor_owner owner,
    qa_actor_id actor, qa_actor_id state, uint32_t field, double value, qa_error *error) {
    qa_application *app = context;
    application_provider *source = rogue_player(app, owner, actor, error);
    return field < QA_Q1_ROGUE_FIELDS && source &&
        application_native_q1_rogue_state_current(app, owner, actor, state, error) &&
        qa_q1_rogue_number_write(source->state.q1, state, (qa_q1_rogue_field)field, value, error) &&
        rogue_player(app, owner, actor, error) == source &&
        application_native_q1_rogue_state_current(app, owner, actor, state, error);
}
