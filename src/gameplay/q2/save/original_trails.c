#include "original_trails.h"
#include "original_edicts.h"

bool q2_original_trail_record(qa_q2_game *game, q2_original_record_io *io,
                              q2_actor *actor, bool *handled) {
    qa_string_id classname = actor->entity ? actor->entity->classname : 0;
    if (io->reading && !q2_original_string(game, io, "classname", 280, &classname))
        return false;
    const char *name = qa_strings_cstr(qa_session_strings(game->services.session), classname);
    *handled = name && !strcmp(name, "player_trail");
    if (!*handled) return true;
    if (io->reading && !actor->entity) {
        actor->entity = q2_entity_state_take(game, io->error);
        if (!actor->entity) {
            return false;
        }
        actor->entity_game = game;
        actor->entity->kind = Q2E_POINT;
        actor->entity->classname = classname;
        actor->entity->ordinal = UINT32_MAX;
    }
    q2_entity_state *node = actor->entity;
    if (io->edition == QA_Q2_RERELEASE && !node->trail) {
        q2_entity_trail_prepare(node);
    }
    if (!io->references_only && !q2_original_scalar(io, "timestamp", Q2_ORIGINAL_TIME,
        288, 288, 288, &node->timestamp_ns)) return false;
    if (io->edition != QA_Q2_RERELEASE) return true;
    return q2_original_source_reference(game, io, "owner", 256, &node->trail->owner) &&
        q2_original_source_reference(game, io, "enemy", 540, &node->trail->older) &&
        q2_original_source_reference(game, io, "chain", 536, &node->trail->newer);
}

bool q2_original_trail_client(qa_q2_game *game, q2_original_record_io *io,
                              qa_actor_id player) {
    if (io->edition != QA_Q2_RERELEASE) return true;
    qa_actor_reference head = {0}, tail = {0};
    if (!io->reading && player.registry &&
        !q2_player_trail_client(game, player, false, &head, &tail, io->error)) return false;
    if (!q2_original_source_reference(game, io, "trail_head", 0, &head) ||
        !q2_original_source_reference(game, io, "trail_tail", 0, &tail)) return false;
    return !io->reading || q2_player_trail_client(game, player, true, &head, &tail, io->error);
}
