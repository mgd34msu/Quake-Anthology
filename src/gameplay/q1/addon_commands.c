#include "boss_internal.h"
#include "maps/internal.h"

static bool source_class(qa_q1_game *g, qa_string_id classname, const char *name) {
    qa_bytes text = qa_strings_text(qa_session_strings(g->services.session), classname);
    return text.size == strlen(name) && !memcmp(text.data, name, text.size);
}
static bool oldnew_credits(qa_q1_game *g, qa_error *error) {
    qa_builtin_event music = {.kind = QA_BUILTIN_EFFECT,
                              .family = QA_GAME_Q1,
                              .provider = g->options.provider,
                              .code = 3, .count = 3, .time_ns = g->time_ns};
    qa_string_id pattern;
    if (!qa_builtin_resource(&g->services, "music", &music.resource, error) ||
        !qa_builtin_emit(&g->services, &music, error))
        return false;
    if (g->destroy_pending)
        return true;
    if (!g->maps || !g->maps->options.lightstyle)
        return q1_map_fail(error, "Q1 omnicide credits require authored lighting owner");
    if (!qa_builtin_resource(&g->services, "m", &pattern, error) ||
        !g->maps->options.lightstyle(g->maps->options.context, 0, pattern, error))
        return false;
    return g->destroy_pending || qa_q1_game_map_finish_addon(g, QA_Q1_MAP_END_DOPA, error);
}
bool q1_addon_omnicide(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    if (!g->maps || !g->maps->options.targets)
        return q1_map_fail(error, "Q1 omnicide requires authored target ownership");
    qa_builtin_snapshot_frame *snapshot;
    if (!q1_snapshot_actors(g, &snapshot, error))
        return false;
    bool ok = true;
    for (size_t i = 0; ok && !g->destroy_pending && i < snapshot->snapshot.count; ++i) {
        qa_actor_id id = snapshot->snapshot.ids[i];
        q1_actor *native = q1_entity(g, id);
        bool monster = native && native->native && native->kind == Q1_MONSTER &&
                       ((native->physics.flags & QA_PHYSICS_MONSTER) ||
                        (native->state.monster.addon.enabled && !native->state.monster.addon.started));
        qa_builtin_actor_traits traits = {0};
        if (!monster && g->services.actor_traits &&
            g->services.actor_traits(g->services.context, id, &traits))
            monster = traits.monster;
        if (!monster || !q1_alive(g, id))
            continue;
        qa_authored_target fields = {0};
        bool authored = qa_targets_read(g->maps->options.targets, id, &fields);
        if (g->destroy_pending)
            break;
        if (!q1_alive(g, id))
            continue;
        native = q1_entity(g, id);
        qa_string_id classname = authored ? fields.classname
                                  : native ? native->classname : traits.classname;
        if (authored && (fields.target || fields.killtarget) &&
            !qa_targets_use(g->maps->options.targets, id, actor, g->time_ns, error)) {
            ok = false;
            break;
        }
        if (g->destroy_pending)
            break;
        if (g->options.program == QA_Q1_MG3) {
            if (source_class(g, classname, "monster_oldone_new"))
                ok = oldnew_credits(g, error);
            else if (source_class(g, classname, "monster_boss"))
                ok = q1_final_end(g, error);
        }
        if (!ok || !q1_alive(g, id))
            continue;
        native = q1_entity(g, id);
        if (native && native->native)
            ok = q1_remove(g, native, error);
        else if (g->maps->options.retire_actor)
            ok = g->maps->options.retire_actor(g->maps->options.context, id, error);
        else
            ok = q1_map_fail(error, "Q1 omnicide requires selected monster retirement owner");
    }
    qa_builtin_snapshot_release(snapshot);
    if (!ok || g->destroy_pending)
        return ok;
    g->killed_monsters = g->total_monsters;
    qa_builtin_event event = {.kind = QA_BUILTIN_EFFECT,
                              .family = QA_GAME_Q1,
                              .provider = g->options.provider,
                              .count = (int32_t)g->killed_monsters,
                              .time_ns = g->time_ns};
    return qa_builtin_resource(&g->services, "monster-count", &event.resource, error) &&
           qa_builtin_emit(&g->services, &event, error);
}
