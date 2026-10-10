#include "internal.h"

typedef struct q2_obituary {
    int means;
    const char *classic, *suffix, *localized;
} q2_obituary;
static const q2_obituary environments[] = {{23, "suicides", "", "suicide"},
                                           {22, "cratered", "", "falling"},
                                           {20, "was squished", "", "crush"},
                                           {17, "sank like a rock", "", "water"},
                                           {18, "melted", "", "slime"},
                                           {19, "does a back flip into the lava", "", "lava"},
                                           {25, "blew up", "", "explosive"},
                                           {26, "blew up", "", "explosive"},
                                           {28, "found a way out", "", "exit"},
                                           {30, "saw the light", "", "laser"},
                                           {33, "got blasted", "", "blaster"},
                                           {27, "was in the wrong place", "", "hurt"},
                                           {29, "was in the wrong place", "", "hurt"},
                                           {31, "was in the wrong place", "", "hurt"},
                                           {38, NULL, NULL, "gekk"},
                                           {36, NULL, NULL, "gekk"}};
static const q2_obituary kills[] = {{1, "was blasted by", "", "blaster"},
                                    {2, "was gunned down by", "", "shotgun"},
                                    {3, "was blown away by", "'s super shotgun", "sshotgun"},
                                    {4, "was machinegunned by", "", "machinegun"},
                                    {5, "was cut in half by", "'s chaingun", "chaingun"},
                                    {6, "was popped by", "'s grenade", "grenade"},
                                    {7, "was shredded by", "'s shrapnel", "grenade_splash"},
                                    {8, "ate", "'s rocket", "rocket"},
                                    {9, "almost dodged", "'s rocket", "rocket_splash"},
                                    {10, "was melted by", "'s hyperblaster", "hyperblaster"},
                                    {11, "was railed by", "", "railgun"},
                                    {12, "saw the pretty lights from", "'s BFG", "bfg_laser"},
                                    {13, "was disintegrated by", "'s BFG blast", "bfg_blast"},
                                    {14, "couldn't hide from", "'s BFG", "bfg_effect"},
                                    {15, "caught", "'s handgrenade", "handgrenade"},
                                    {16, "didn't see", "'s handgrenade", "handgrenade_splash"},
                                    {24, "feels", "'s pain", "held_grenade"},
                                    {21, "tried to invade", "'s personal space", "telefrag"},
                                    {57, NULL, NULL, "telefrag"},
                                    {34, NULL, NULL, "ripper"},
                                    {35, NULL, NULL, "phalanx"},
                                    {39, NULL, NULL, "trap"},
                                    {40, NULL, NULL, "chainfist"},
                                    {41, NULL, NULL, "disintegrator"},
                                    {42, NULL, NULL, "etf_rifle"},
                                    {44, NULL, NULL, "heatbeam"},
                                    {45, NULL, NULL, "tesla"},
                                    {46, NULL, NULL, "prox"},
                                    {47, NULL, NULL, "nuke"},
                                    {48, NULL, NULL, "vengeance_sphere"},
                                    {49, NULL, NULL, "hunter_sphere"},
                                    {50, NULL, NULL, "defender_sphere"},
                                    {51, NULL, NULL, "tracker"},
                                    {53, NULL, NULL, "dopple_explode"},
                                    {54, NULL, NULL, "dopple_vengeance"},
                                    {55, NULL, NULL, "dopple_hunter"},
                                    {56, NULL, NULL, "grapple"}};
static bool score(qa_q2_game *g, q2_actor *victim, qa_actor_id attacker, qa_actor_id recipient,
                  int change, int means, qa_error *e) {
    if (!q2_actor_live(g, recipient))
        return true;
    qa_q2_player_services *s = &g->player_runtime->services;
    if (s->shared_score_owned)
        return true;
    if (s->score)
        return s->score(s->context, victim->id, attacker, recipient, change, means, e);
    q2_actor *native = q2_client(g, recipient, e);
    if (!native)
        return false;
    int current = native->client->info.score;
    native->client->info.score = change > 0 ? (current == INT_MAX ? INT_MAX : current + 1)
                                               : (current == INT_MIN ? INT_MIN : current - 1);
    return true;
}
bool q2_player_obituary(qa_q2_game *g, q2_actor *a, const qa_damage_outcome *outcome, qa_error *e) {
    int raw = outcome->request.attack.cause.kind == QA_CAUSE_Q2
                  ? outcome->request.attack.cause.source.q2.means_of_death
                  : 0;
    int means = raw & ~0x8000000;
    qa_actor_id attacker = outcome->request.attack.attacker;
    qa_builtin_player_info attacker_info;
    bool player_attacker = q2_player_info(g, attacker, &attacker_info);
    bool self = player_attacker && qa_actor_id_equal(attacker, a->id);
    bool friendly = (raw & 0x8000000) || (g->options.cooperative && player_attacker);
    bool rr = g->options.edition == QA_Q2_RERELEASE,
         no_loss = outcome->request.attack.cause.kind == QA_CAUSE_Q2 &&
                   outcome->request.attack.cause.source.q2.no_point_loss;
    const q2_obituary *environment = NULL, *kill = NULL;
    for (size_t i = 0; i < sizeof(environments) / sizeof(*environments); i++)
        if (environments[i].means == means) {
            environment = &environments[i];
            break;
        }
    for (size_t i = 0; i < sizeof(kills) / sizeof(*kills); i++)
        if (kills[i].means == means) {
            kill = &kills[i];
            break;
        }
    char text[256];
    const char *name = q2_player_source_name(g, a->client);
    if (rr) {
        if (self) {
            const char *key = means == 24                 ? "held_grenade"
                              : means == 16 || means == 7 ? "grenade_splash"
                              : means == 9                ? "rocket_splash"
                              : means == 13               ? "bfg_blast"
                              : means == 39               ? "trap"
                              : means == 53               ? "dopple_explode"
                                                          : "default";
            snprintf(text, sizeof(text), "$g_mod_self_%s", key);
        } else if (environment)
            snprintf(text, sizeof(text), "$g_mod_generic_%s", environment->localized);
        else if (player_attacker)
            snprintf(text, sizeof(text), "$g_mod_kill_%s", kill ? kill->localized : "generic");
        else
            snprintf(text, sizeof(text), "$g_mod_generic_died");
        qa_string_id key, victim_name, attacker_name = 0;
        size_t count = player_attacker && !self && !environment ? 2 : 1;
        if (!qa_builtin_resource(&g->services, text, &key, e) ||
            !qa_builtin_resource(&g->services, name, &victim_name, e) ||
            (count == 2 &&
             !qa_builtin_resource(&g->services, attacker_info.name, &attacker_name, e)))
            return false;
        qa_actor_id recipient = self || environment || !player_attacker ? a->id : attacker;
        int change = qa_actor_id_equal(recipient, a->id) ? -1 : friendly ? -1 : 1;
        if (g->options.deathmatch && (change > 0 || !no_loss) &&
            !score(g, a, outcome->request.attack.attacker, recipient, change, raw, e))
            return false;
        else if (player_attacker && !self && !environment && !g->options.deathmatch &&
                 !g->options.cooperative && !score(g, a, attacker, a->id, -1, raw, e))
            return false;
        if (!q2_actor_live(g, a->id))
            return true;
        qa_builtin_message_arg arguments[] = {
            {.kind = QA_BUILTIN_MESSAGE_STRING, .value.text = victim_name},
            {.kind = QA_BUILTIN_MESSAGE_STRING, .value.text = attacker_name}};
        return qa_builtin_emit(&g->services,
                               &(qa_builtin_event){.kind = QA_BUILTIN_MESSAGE,
                                                   .family = QA_GAME_Q2,
                                                   .provider = g->options.owner,
                                                   .time_ns = g->now_ns,
                                                   .text = key,
                                                   .code = 1,
                                                   .arguments = arguments,
                                                   .argument_count = count},
                               e);
    }
    char message[128];
    const char *cause = environment ? environment->classic : NULL;
    if (self) {
        const char *possessive = a->client->rule.gender == 1   ? "her"
                                 : a->client->rule.gender == 2 ? "its"
                                                          : "his";
        const char *reflexive = a->client->rule.gender == 1   ? "herself"
                                : a->client->rule.gender == 2 ? "itself"
                                                         : "himself";
        if (means == 24)
            snprintf(message, sizeof(message), "tried to put the pin back in");
        else if (means == 7 || means == 16)
            snprintf(message, sizeof(message), "tripped on %s own grenade", possessive);
        else if (means == 9)
            snprintf(message, sizeof(message), "blew %s up", reflexive);
        else if (means == 13)
            snprintf(message, sizeof(message), "should have used a smaller gun");
        else
            snprintf(message, sizeof(message), "killed %s", reflexive);
        cause = message;
    }
    qa_actor_id recipient = a->id;
    int change = -1;
    if ((g->options.deathmatch || g->options.cooperative) && cause)
        snprintf(text, sizeof(text), "%s %s.\n", name, cause);
    else if ((g->options.deathmatch || g->options.cooperative) && player_attacker && kill &&
             kill->classic) {
        snprintf(text, sizeof(text), "%s %s %s%s\n", name, kill->classic,
                 attacker_info.name, kill->suffix);
        recipient = attacker;
        change = friendly ? -1 : 1;
    } else
        snprintf(text, sizeof(text), "%s died.\n", name);
    if (g->options.deathmatch &&
        !score(g, a, outcome->request.attack.attacker, recipient, change, raw, e))
        return false;
    return !q2_actor_live(g, a->id) || q2_player_print(g, (qa_actor_id){0}, 1, text, e);
}
