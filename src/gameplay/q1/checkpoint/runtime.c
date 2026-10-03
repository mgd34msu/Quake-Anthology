#include "internal.h"

static bool options(q1_save_io *io, qa_q1_options *o) {
    qa_actor_owner provider = o->provider, combat = o->combat_provider;
    qa_actor_owner movement = o->movement_provider, inventory = o->inventory_provider;
    qa_q1_program program = o->program;
    qa_q1_edition edition = o->edition;
    bool quakeworld = o->quakeworld;
    uint32_t max_clients = o->max_clients;
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
    if (io->reading && o->max_clients != max_clients)
        return q1_save_fail(io, "Q1 checkpoint changes prepared source client capacity");
    Q1_SAVE(io, u32, o->random_seed);
    Q1_SAVE(io, u32, o->gamecfg);
    return o->skill <= 3 || q1_save_fail(io, "Invalid Q1 checkpoint skill");
}
bool q1_save_runtime(q1_save_io *io, qa_q1_game *g) {
    if (!options(io, &g->options))
        return false;
    for (size_t i = 0; i < 2; ++i) {
        Q1_SAVE(io, double, g->source_captures[i]);
        double total = g->source_captures[i];
        if (total < 0 || total > 9007199254740991.0 || floor(total) != total ||
            (g->options.program != QA_Q1_CTF && total != 0))
            return q1_save_fail(io, "Invalid Q1 source-client capture total");
    }
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
    Q1_SAVE(io, actor, g->rogue_runes_world);
    Q1_SAVE(io, bool, g->rogue_runes_started);
    if (g->rogue_runes_started != (g->rogue_runes_world.registry != 0) ||
        (g->rogue_runes_world.registry && g->options.program != QA_Q1_ROGUE))
        return q1_save_fail(io, "Invalid Rogue rune source-world continuation");
    Q1_SAVE(io, double, g->time);
    Q1_SAVE(io, double, g->elapsed);
    Q1_SAVE(io, double, g->sight_time);
    Q1_SAVE(io, double, g->finale_last_poll);
    Q1_SAVE(io, bool, g->finale_polled);
    Q1_SAVE(io, bool, g->finale_acknowledged);
    if (!g->finale_polled && (g->finale_last_poll != 0 || g->finale_acknowledged))
        return q1_save_fail(io, "Q1 finale acknowledgement has no source poll");
    Q1_SAVE(io, u64, g->time_ns);
    Q1_SAVE(io, u64, g->attack_sequence);
    Q1_SAVE(io, u32, g->force_retouch);
    Q1_SAVE(io, vector, g->forward);
    Q1_SAVE(io, vector, g->right);
    Q1_SAVE(io, vector, g->up);
    if (g->options.quakeworld) {
        /* Q_atof of an accepted 63-byte info value can overflow binary32. */
        uint32_t rj;
        memcpy(&rj, &g->qw_rj, sizeof(rj));
        Q1_SAVE(io, u32, rj);
        if (io->reading) memcpy(&g->qw_rj, &rj, sizeof(rj));
        if (isnan(g->qw_rj)) return q1_save_fail(io, "Invalid QW rj Source global");
        Q1_SAVE(io, actor, g->qw_multi_entity);
        Q1_SAVE(io, float, g->qw_multi_damage);
        Q1_SAVE(io, float, g->qw_blood_count);
        Q1_SAVE(io, float, g->qw_puff_count);
        Q1_SAVE(io, vector, g->qw_blood_origin);
        Q1_SAVE(io, vector, g->qw_puff_origin);
        if (!isfinite(g->qw_multi_damage) || !isfinite(g->qw_blood_count) || !isfinite(g->qw_puff_count) ||
            g->qw_blood_count<0 || g->qw_puff_count<0 ||
            truncf(g->qw_blood_count)!=g->qw_blood_count || truncf(g->qw_puff_count)!=g->qw_puff_count)
            return q1_save_fail(io,"Invalid QW multi-damage Source globals");
    }
    Q1_SAVE(io, bool, g->run_straight);
    Q1_SAVE(io, u8, g->rune_knight_melee);
    Q1_SAVE(io, u8, g->enemy_range);
    Q1_SAVE(io, bool, g->enemy_visible);
    return true;
}
