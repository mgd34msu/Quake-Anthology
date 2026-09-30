#include "internal.h"

static bool options(q1_save_io *io, qa_q1_options *o) {
    qa_actor_owner provider = o->provider, combat = o->combat_provider;
    qa_actor_owner movement = o->movement_provider, inventory = o->inventory_provider;
    qa_q1_program program = o->program;
    qa_q1_edition edition = o->edition;
    bool quakeworld = o->quakeworld;
    Q1_SAVE(io, string, o->provider);
    Q1_SAVE(io, string, o->combat_provider);
    Q1_SAVE(io, string, o->movement_provider);
    Q1_SAVE(io, string, o->inventory_provider);
    Q1_SAVE_ENUM(io, o->program, QA_Q1_CTF);
    Q1_SAVE_ENUM(io, o->edition, QA_Q1_QUAKE64);
    Q1_SAVE(io, bool, o->quakeworld);
    if (io->reading &&
        (o->provider != provider || o->combat_provider != combat ||
         o->movement_provider != movement || o->inventory_provider != inventory ||
         o->program != program || o->edition != edition || o->quakeworld != quakeworld))
        return q1_save_fail(io, "Q1 checkpoint belongs to a different provider composition");
    Q1_SAVE(io, bool, o->coop);
    Q1_SAVE(io, u8, o->skill);
    Q1_SAVE(io, i32, o->deathmatch);
    Q1_SAVE(io, i32, o->teamplay);
    Q1_SAVE(io, i32, o->world_type);
    Q1_SAVE(io, float, o->gravity);
    Q1_SAVE(io, float, o->aim_threshold);
    Q1_SAVE(io, u32, o->max_clients);
    Q1_SAVE(io, u32, o->random_seed);
    Q1_SAVE(io, u32, o->gamecfg);
    return o->skill <= 3 || q1_save_fail(io, "Invalid Q1 checkpoint skill");
}
bool q1_save_runtime(q1_save_io *io, qa_q1_game *g) {
    if (!options(io, &g->options))
        return false;
    Q1_SAVE(io, u32, g->total_monsters);
    Q1_SAVE(io, u32, g->killed_monsters);
    Q1_SAVE(io, u32, g->hellknight_melee);
    Q1_SAVE(io, u32, g->authored_gremlins);
    Q1_SAVE(io, u32, g->spawned_gremlins);
    for (size_t i = 0; i < 31; ++i)
        Q1_SAVE(io, u32, g->random.words[i]);
    Q1_SAVE(io, u8, g->random.front);
    Q1_SAVE(io, u8, g->random.rear);
    Q1_SAVE(io, u64, g->random.draws);
    if (g->random.front >= 31 || g->random.rear >= 31 ||
        (g->random.front + 31 - g->random.rear) % 31 != 3)
        return q1_save_fail(io, "Invalid Q1 random continuation");
    Q1_SAVE(io, actor, g->sight_actor);
    Q1_SAVE(io, actor, g->horn_charmer);
    Q1_SAVE(io, double, g->time);
    Q1_SAVE(io, double, g->elapsed);
    Q1_SAVE(io, double, g->sight_time);
    Q1_SAVE(io, u64, g->time_ns);
    Q1_SAVE(io, u64, g->attack_sequence);
    Q1_SAVE(io, vector, g->forward);
    Q1_SAVE(io, vector, g->right);
    Q1_SAVE(io, vector, g->up);
    Q1_SAVE(io, bool, g->run_straight);
    Q1_SAVE(io, u8, g->rune_knight_melee);
    Q1_SAVE(io, u8, g->enemy_range);
    Q1_SAVE(io, bool, g->enemy_visible);
    return true;
}
