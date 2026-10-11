#include "internal.h"

typedef struct obituary_text {
    q1_runtime_name key, classic;
} obituary_text;
static const obituary_text messages[] = {
    {Q1_NAME_OBITUARY_QC_TELEFRAGGED, Q1_NAME_OBITUARY_TEXT_TELEFRAGGED},
    {Q1_NAME_OBITUARY_QC_SATANS_POWER, Q1_NAME_OBITUARY_TEXT_SATANS_POWER},
    {Q1_NAME_OBITUARY_QC_DISCHARGE_WATER, Q1_NAME_OBITUARY_TEXT_DISCHARGE_WATER},
    {Q1_NAME_OBITUARY_QC_DISCHARGE_SLIME, Q1_NAME_OBITUARY_TEXT_DISCHARGE_WATER},
    {Q1_NAME_OBITUARY_QC_DISCHARGE_LAVA, Q1_NAME_OBITUARY_TEXT_DISCHARGE_WATER},
    {Q1_NAME_OBITUARY_QC_SUICIDE_PIN, Q1_NAME_OBITUARY_TEXT_SUICIDE_PIN},
    {Q1_NAME_OBITUARY_QC_SUICIDE_BORED, Q1_NAME_OBITUARY_TEXT_SUICIDE_BORED},
    {Q1_NAME_OBITUARY_QC_SUICIDE_LOADED, Q1_NAME_OBITUARY_TEXT_SUICIDE_BORED},
    {Q1_NAME_OBITUARY_QC_FF_TEAMMATE, Q1_NAME_OBITUARY_TEXT_FF_TEAMMATE},
    {Q1_NAME_OBITUARY_QC_FF_GLASSES, Q1_NAME_OBITUARY_TEXT_FF_GLASSES},
    {Q1_NAME_OBITUARY_QC_FF_OTHERTEAM, Q1_NAME_OBITUARY_TEXT_FF_OTHERTEAM},
    {Q1_NAME_OBITUARY_QC_FF_FRIEND, Q1_NAME_OBITUARY_TEXT_FF_FRIEND},
    {Q1_NAME_OBITUARY_QC_DEATH_AX, Q1_NAME_OBITUARY_TEXT_DEATH_AX},
    {Q1_NAME_OBITUARY_QC_DEATH_SG, Q1_NAME_OBITUARY_TEXT_DEATH_SG},
    {Q1_NAME_OBITUARY_QC_DEATH_DBL, Q1_NAME_OBITUARY_TEXT_DEATH_DBL},
    {Q1_NAME_OBITUARY_QC_DEATH_NAIL, Q1_NAME_OBITUARY_TEXT_DEATH_NAIL},
    {Q1_NAME_OBITUARY_QC_DEATH_SNG, Q1_NAME_OBITUARY_TEXT_DEATH_SNG},
    {Q1_NAME_OBITUARY_QC_DEATH_GL1, Q1_NAME_OBITUARY_TEXT_DEATH_GL1},
    {Q1_NAME_OBITUARY_QC_DEATH_GL2, Q1_NAME_OBITUARY_TEXT_DEATH_GL2},
    {Q1_NAME_OBITUARY_QC_DEATH_RL2, Q1_NAME_OBITUARY_TEXT_DEATH_RL2},
    {Q1_NAME_OBITUARY_QC_DEATH_RL3, Q1_NAME_OBITUARY_TEXT_DEATH_RL3},
    {Q1_NAME_OBITUARY_QC_DEATH_LG1, Q1_NAME_OBITUARY_TEXT_DEATH_LG1},
    {Q1_NAME_OBITUARY_QC_DEATH_LG2, Q1_NAME_OBITUARY_TEXT_DEATH_LG2},
    {Q1_NAME_OBITUARY_QC_DEATH_DROWN1, Q1_NAME_OBITUARY_TEXT_DEATH_DROWN1},
    {Q1_NAME_OBITUARY_QC_DEATH_DROWN2, Q1_NAME_OBITUARY_TEXT_DEATH_DROWN2},
    {Q1_NAME_OBITUARY_QC_DEATH_SLIME1, Q1_NAME_OBITUARY_TEXT_DEATH_SLIME1},
    {Q1_NAME_OBITUARY_QC_DEATH_SLIME2, Q1_NAME_OBITUARY_TEXT_DEATH_SLIME2},
    {Q1_NAME_OBITUARY_QC_DEATH_LAVA1, Q1_NAME_OBITUARY_TEXT_DEATH_LAVA1},
    {Q1_NAME_OBITUARY_QC_DEATH_LAVA2, Q1_NAME_OBITUARY_TEXT_DEATH_LAVA2},
    {Q1_NAME_OBITUARY_QC_DEATH_LAVA3, Q1_NAME_OBITUARY_TEXT_DEATH_LAVA3},
    {Q1_NAME_OBITUARY_QC_DEATH_SQUISH, Q1_NAME_OBITUARY_TEXT_DEATH_SQUISH},
    {Q1_NAME_OBITUARY_QC_DEATH_FALL, Q1_NAME_OBITUARY_TEXT_DEATH_FALL},
    {Q1_NAME_OBITUARY_QC_DEATH_DIED, Q1_NAME_OBITUARY_TEXT_DEATH_DIED},
    {Q1_NAME_OBITUARY_QC_DEATH_EMPATHY1, Q1_NAME_OBITUARY_TEXT_DEATH_EMPATHY1},
    {Q1_NAME_OBITUARY_QC_DEATH_EMPATHY2, Q1_NAME_OBITUARY_TEXT_DEATH_EMPATHY2},
    {Q1_NAME_OBITUARY_QC_DEATH_BOMB1, Q1_NAME_OBITUARY_TEXT_DEATH_BOMB1},
    {Q1_NAME_OBITUARY_QC_DEATH_BOMB2, Q1_NAME_OBITUARY_TEXT_DEATH_BOMB2},
    {Q1_NAME_OBITUARY_QC_DEATH_LASER1, Q1_NAME_OBITUARY_TEXT_DEATH_LASER1},
    {Q1_NAME_OBITUARY_QC_DEATH_LASER2, Q1_NAME_OBITUARY_TEXT_DEATH_LASER2},
    {Q1_NAME_OBITUARY_QC_DEATH_HAMMER, Q1_NAME_OBITUARY_TEXT_DEATH_HAMMER},
    {Q1_NAME_OBITUARY_QC_DEATH_GRAPPLED, Q1_NAME_OBITUARY_TEXT_DEATH_GRAPPLED},
    {Q1_NAME_OBITUARY_QC_DEATH_BURNED, Q1_NAME_OBITUARY_TEXT_DEATH_BURNED},
    {Q1_NAME_OBITUARY_QC_DEATH_FUSED, Q1_NAME_OBITUARY_TEXT_DEATH_FUSED},
    {Q1_NAME_OBITUARY_QC_DEATH_BLASTED, Q1_NAME_OBITUARY_TEXT_DEATH_BLASTED},
    {Q1_NAME_OBITUARY_QC_DEATH_VENGEANCE, Q1_NAME_OBITUARY_TEXT_DEATH_VENGEANCE},
    {Q1_NAME_OBITUARY_QC_DEATH_SMASHED, Q1_NAME_OBITUARY_TEXT_DEATH_SMASHED},
    {Q1_NAME_OBITUARY_QC_CHANGED_TEAMS, Q1_NAME_OBITUARY_TEXT_CHANGED_TEAMS},
    {Q1_NAME_OBITUARY_QC_TRIED_CHANGE_TEAMS, Q1_NAME_OBITUARY_TEXT_TRIED_CHANGE_TEAMS},
    {Q1_NAME_OBITUARY_QC_KS_DRAGON1, Q1_NAME_OBITUARY_TEXT_KS_DRAGON1},
    {Q1_NAME_OBITUARY_QC_KS_DRAGON2, Q1_NAME_OBITUARY_TEXT_KS_DRAGON2},
    {Q1_NAME_OBITUARY_QC_KS_EEL, Q1_NAME_OBITUARY_TEXT_KS_EEL},
    {Q1_NAME_OBITUARY_QC_KS_WRATH, Q1_NAME_OBITUARY_TEXT_KS_WRATH},
    {Q1_NAME_OBITUARY_QC_KS_OVERLORD, Q1_NAME_OBITUARY_TEXT_KS_OVERLORD},
    {Q1_NAME_OBITUARY_QC_KS_SWORDSMAN, Q1_NAME_OBITUARY_TEXT_KS_SWORDSMAN},
    {Q1_NAME_OBITUARY_QC_KS_HEPHAESTUS, Q1_NAME_OBITUARY_TEXT_KS_HEPHAESTUS},
    {Q1_NAME_OBITUARY_QC_KS_GUARDIAN, Q1_NAME_OBITUARY_TEXT_KS_GUARDIAN},
    {Q1_NAME_OBITUARY_QC_KS_MUMMY, Q1_NAME_OBITUARY_TEXT_KS_MUMMY},
    {Q1_NAME_OBITUARY_QC_KS_GREMLIN, Q1_NAME_OBITUARY_TEXT_KS_GREMLIN},
    {Q1_NAME_OBITUARY_QC_KS_CENTROID, Q1_NAME_OBITUARY_TEXT_KS_CENTROID},
    {Q1_NAME_OBITUARY_QC_KS_ARMAGON, Q1_NAME_OBITUARY_TEXT_KS_ARMAGON},
    {Q1_NAME_OBITUARY_QC_KS_BLEW_UP, Q1_NAME_OBITUARY_TEXT_KS_BLEW_UP},
    {Q1_NAME_OBITUARY_QC_KS_SPIKED, Q1_NAME_OBITUARY_TEXT_KS_SPIKED},
    {Q1_NAME_OBITUARY_QC_KS_LAVABALL, Q1_NAME_OBITUARY_TEXT_KS_LAVABALL},
    {Q1_NAME_OBITUARY_QC_KS_TRIED_LEAVE, Q1_NAME_OBITUARY_TEXT_KS_TRIED_LEAVE},
    {Q1_NAME_OBITUARY_QC_KS_RODE_LIGHTNING, Q1_NAME_OBITUARY_TEXT_KS_RODE_LIGHTNING},
    {Q1_NAME_OBITUARY_QC_KS_CLEAVED, Q1_NAME_OBITUARY_TEXT_KS_CLEAVED},
    {Q1_NAME_OBITUARY_QC_KS_SLICED, Q1_NAME_OBITUARY_TEXT_KS_SLICED},
    {Q1_NAME_OBITUARY_QC_KS_PLASMA, Q1_NAME_OBITUARY_TEXT_KS_PLASMA},
    {Q1_NAME_OBITUARY_QC_ENTERED, Q1_NAME_OBITUARY_TEXT_ENTERED},
    {Q1_NAME_OBITUARY_QC_LEFT_GAME, Q1_NAME_OBITUARY_TEXT_LEFT_GAME},
    {Q1_NAME_OBITUARY_QC_SUICIDES, Q1_NAME_OBITUARY_TEXT_SUICIDES},
    {Q1_NAME_OBITUARY_QC_EXITED, Q1_NAME_OBITUARY_TEXT_EXITED}};
typedef struct monster_message {
    q1_runtime_name classname;
    q1_runtime_name key, classic;
} monster_message;
static const monster_message monsters[] = {
    {Q1_NAME_MONSTER_ARMY, Q1_NAME_OBITUARY_QC_KS_GRUNT, Q1_NAME_OBITUARY_TEXT_KS_GRUNT},
    {Q1_NAME_MONSTER_DEMON1, Q1_NAME_OBITUARY_QC_KS_FIEND, Q1_NAME_OBITUARY_TEXT_KS_FIEND},
    {Q1_NAME_MONSTER_DOG, Q1_NAME_OBITUARY_QC_KS_ROTTWEILER, Q1_NAME_OBITUARY_TEXT_KS_ROTTWEILER},
    {Q1_NAME_MONSTER_DRAGON, Q1_NAME_OBITUARY_QC_KS_DRAGON, Q1_NAME_OBITUARY_TEXT_KS_DRAGON},
    {Q1_NAME_MONSTER_ENFORCER, Q1_NAME_OBITUARY_QC_KS_ENFORCER, Q1_NAME_OBITUARY_TEXT_KS_ENFORCER},
    {Q1_NAME_MONSTER_FISH, Q1_NAME_OBITUARY_QC_KS_ROTFISH, Q1_NAME_OBITUARY_TEXT_KS_ROTFISH},
    {Q1_NAME_MONSTER_HELL_KNIGHT, Q1_NAME_OBITUARY_QC_KS_DEATHKNIGHT, Q1_NAME_OBITUARY_TEXT_KS_DEATHKNIGHT},
    {Q1_NAME_MONSTER_KNIGHT, Q1_NAME_OBITUARY_QC_KS_KNIGHT, Q1_NAME_OBITUARY_TEXT_KS_KNIGHT},
    {Q1_NAME_MONSTER_OGRE, Q1_NAME_OBITUARY_QC_KS_OGRE, Q1_NAME_OBITUARY_TEXT_KS_OGRE},
    {Q1_NAME_MONSTER_OLDONE, Q1_NAME_OBITUARY_QC_KS_SHUB, Q1_NAME_OBITUARY_TEXT_KS_SHUB},
    {Q1_NAME_MONSTER_SHALRATH, Q1_NAME_OBITUARY_QC_KS_VORE, Q1_NAME_OBITUARY_TEXT_KS_VORE},
    {Q1_NAME_MONSTER_SHAMBLER, Q1_NAME_OBITUARY_QC_KS_SHAMBLER, Q1_NAME_OBITUARY_TEXT_KS_SHAMBLER},
    {Q1_NAME_MONSTER_TARBABY, Q1_NAME_OBITUARY_QC_KS_SPAWN, Q1_NAME_OBITUARY_TEXT_KS_SPAWN},
    {Q1_NAME_MONSTER_VOMIT, Q1_NAME_OBITUARY_QC_KS_VOMITUS, Q1_NAME_OBITUARY_TEXT_KS_VOMITUS},
    {Q1_NAME_MONSTER_WIZARD, Q1_NAME_OBITUARY_QC_KS_SCRAG, Q1_NAME_OBITUARY_TEXT_KS_SCRAG},
    {Q1_NAME_MONSTER_ZOMBIE, Q1_NAME_OBITUARY_QC_KS_ZOMBIE, Q1_NAME_OBITUARY_TEXT_KS_ZOMBIE},
    {Q1_NAME_MONSTER_DRAGON_DEAD, Q1_NAME_OBITUARY_QC_KS_DRAGON2, Q1_NAME_COUNT},
    {Q1_NAME_MONSTER_GREMLIN, Q1_NAME_OBITUARY_QC_KS_GREMLIN, Q1_NAME_COUNT},
    {Q1_NAME_MONSTER_SCOURGE, Q1_NAME_OBITUARY_QC_KS_CENTROID, Q1_NAME_COUNT},
    {Q1_NAME_MONSTER_ARMAGON, Q1_NAME_OBITUARY_QC_KS_ARMAGON, Q1_NAME_COUNT},
    {Q1_NAME_MONSTER_EEL, Q1_NAME_OBITUARY_QC_KS_EEL, Q1_NAME_COUNT},
    {Q1_NAME_MONSTER_WRATH, Q1_NAME_OBITUARY_QC_KS_WRATH, Q1_NAME_COUNT},
    {Q1_NAME_MONSTER_SUPER_WRATH, Q1_NAME_OBITUARY_QC_KS_OVERLORD, Q1_NAME_COUNT},
    {Q1_NAME_MONSTER_SWORD, Q1_NAME_OBITUARY_QC_KS_SWORDSMAN, Q1_NAME_COUNT},
    {Q1_NAME_MONSTER_LAVA_MAN, Q1_NAME_OBITUARY_QC_KS_HEPHAESTUS, Q1_NAME_COUNT},
    {Q1_NAME_MONSTER_MORPH, Q1_NAME_OBITUARY_QC_KS_GUARDIAN, Q1_NAME_COUNT},
    {Q1_NAME_MONSTER_MUMMY, Q1_NAME_OBITUARY_QC_KS_MUMMY, Q1_NAME_COUNT}};

static const char *text(qa_q1_game *g, qa_string_id id) {
    const char *value = qa_strings_cstr(qa_session_strings(g->services.session), id);
    return value ? value : "";
}
static bool named(qa_string_id id, qa_string_id name) {
    return id == name;
}
static bool actor_named(const qa_q1_obituary_actor *actor, qa_string_id name) {
    return actor && actor->classname == name;
}
static bool message(qa_q1_game *g, qa_q1_obituary_result *out, qa_string_id key,
                    qa_string_id classic_override, qa_string_id first, qa_string_id second,
                    size_t count) {
    if (!qa_strings_text(qa_session_strings(g->services.session), key).size)
        return true;
    if (g->options.edition == QA_Q1_RERELEASE) {
        out->text = key;
        out->argument_count = count;
        out->arguments[0] = (qa_builtin_message_arg){QA_BUILTIN_MESSAGE_STRING, {.text = first}};
        out->arguments[1] = (qa_builtin_message_arg){QA_BUILTIN_MESSAGE_STRING, {.text = second}};
        return true;
    }
    qa_string_id format = classic_override;
    if (!format) {
        if ((g->options.program == QA_Q1_HIPNOTIC || g->options.program == QA_Q1_ROGUE) &&
            key == g->runtime_names[Q1_NAME_OBITUARY_QC_SUICIDE_LOADED])
            format = g->runtime_names[Q1_NAME_OBITUARY_TEXT_CHECK_LOADED];
        for (size_t i = 0; !format && i < sizeof(messages) / sizeof(messages[0]); ++i)
            if (key == g->runtime_names[messages[i].key])
                format = g->runtime_names[messages[i].classic];
        if (!format)
            format = key;
    }
    out->text = format;
    out->argument_count = count;
    out->arguments[0] = (qa_builtin_message_arg){QA_BUILTIN_MESSAGE_STRING, {.text = first}};
    out->arguments[1] = (qa_builtin_message_arg){QA_BUILTIN_MESSAGE_STRING, {.text = second}};
    return true;
}

static bool finish(qa_q1_game *g, qa_q1_obituary_result *out, qa_string_id key, qa_string_id classic,
                   const qa_q1_obituary_actor *credit, int32_t delta, qa_string_id first,
                   qa_string_id second, size_t count) {
    if (credit) {
        out->credited_actor = credit->actor;
        out->score_delta = delta;
    }
    return message(g, out, key, classic, first, second, count);
}

static bool base_obituary(qa_q1_game *g, const qa_q1_obituary_input *input, float roll,
                          qa_q1_obituary_result *out) {
    const qa_q1_obituary_actor *victim = &input->victim, *attacker = input->attacker;
    if (!victim->player)
        return true;
    qa_string_id key = 0, classic = 0;
    const qa_q1_obituary_actor *credit = victim;
    int32_t delta = -1;
    qa_string_id first = victim->name, second = 0;
    size_t count = 1;
    if (actor_named(attacker, g->runtime_names[Q1_NAME_TELEDEATH]) && input->telefrag_owner) {
        key = g->runtime_names[Q1_NAME_OBITUARY_QC_TELEFRAGGED];
        credit = input->telefrag_owner;
        delta = 1;
        second = credit->name;
        count = 2;
    } else if (actor_named(attacker, g->runtime_names[Q1_NAME_TELEDEATH2]))
        key = g->runtime_names[Q1_NAME_OBITUARY_QC_SATANS_POWER];
    else if (attacker && attacker->player) {
        if (qa_actor_id_equal(victim->actor, attacker->actor)) {
            if (victim->weapon == QA_Q1_LIGHTNING && victim->water_level > 1)
                key = victim->water_type == -4   ? g->runtime_names[Q1_NAME_OBITUARY_QC_DISCHARGE_SLIME]
                      : victim->water_type == -5 ? g->runtime_names[Q1_NAME_OBITUARY_QC_DISCHARGE_LAVA]
                                                 : g->runtime_names[Q1_NAME_OBITUARY_QC_DISCHARGE_WATER];
            else
                key = victim->weapon == QA_Q1_GRENADE ? g->runtime_names[Q1_NAME_OBITUARY_QC_SUICIDE_PIN]
                      : roll != 0                     ? g->runtime_names[Q1_NAME_OBITUARY_QC_SUICIDE_BORED]
                                                      : g->runtime_names[Q1_NAME_OBITUARY_QC_SUICIDE_LOADED];
        } else if (input->teamplay == 2 && victim->team == attacker->team &&
                   (g->options.edition == QA_Q1_RERELEASE ? attacker->team != 0 : victim->team > 0)) {
            key = roll < .25f   ? g->runtime_names[Q1_NAME_OBITUARY_QC_FF_TEAMMATE]
                  : roll < .5f  ? g->runtime_names[Q1_NAME_OBITUARY_QC_FF_GLASSES]
                  : roll < .75f ? g->runtime_names[Q1_NAME_OBITUARY_QC_FF_OTHERTEAM]
                                : g->runtime_names[Q1_NAME_OBITUARY_QC_FF_FRIEND];
            credit = attacker;
            first = attacker->name;
        } else {
            credit = attacker;
            delta = 1;
            second = attacker->name;
            count = 2;
            switch (attacker->weapon) {
            case QA_Q1_AXE:
                key = g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_AX];
                break;
            case QA_Q1_SHOTGUN:
                key = g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_SG];
                break;
            case QA_Q1_SUPER_SHOTGUN:
                key = g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_DBL];
                break;
            case QA_Q1_NAILGUN:
                key = g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_NAIL];
                break;
            case QA_Q1_SUPER_NAILGUN:
                key = g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_SNG];
                break;
            case QA_Q1_GRENADE:
                key = victim->health < -40 ? g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_GL1] : g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_GL2];
                break;
            case QA_Q1_ROCKET:
                if (g->options.edition == QA_Q1_RERELEASE && attacker->quad_expires > 0 &&
                    victim->health < -40) {
                    float choice = q1_random(g);
                    key = choice < .3f   ? g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_RL_QUAD1]
                          : choice < .6f ? g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_RL_QUAD2]
                                         : g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_RL1];
                } else
                    key = victim->health < -40 ? g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_RL2] : g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_RL3];
                break;
            case QA_Q1_LIGHTNING:
                key = attacker->water_level > 1 ? g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_LG1] : g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_LG2];
                if (g->options.edition == QA_Q1_RERELEASE && attacker->water_level > 1 &&
                    attacker->invulnerable_expires != 0) {
                    out->achievement_actor = attacker->actor;
                    out->achievement = g->runtime_names[Q1_NAME_ACH_SURVIVE_DISCHARGE];
                }
                break;
            default:
                key = attacker->kill_string;
                return finish(g, out, key, classic, credit, delta, first, second, count);
            }
        }
    } else if (g->options.edition != QA_Q1_RERELEASE && attacker) {
        if (attacker->monster) {
            classic = g->runtime_names[Q1_NAME_OBITUARY_TEXT_ACTOR];
            for (size_t i = 0; i < sizeof(monsters) / sizeof(monsters[0]); ++i)
                if (monsters[i].classic != Q1_NAME_COUNT && actor_named(attacker, g->runtime_names[monsters[i].classname])) {
                    classic = g->runtime_names[monsters[i].classic];
                    break;
                }
            key = g->runtime_names[Q1_NAME_OBITUARY_CLASSIC_MONSTER];
        } else if (actor_named(attacker, g->runtime_names[Q1_NAME_EXPLO_BOX]) ||
                   actor_named(attacker, g->runtime_names[Q1_NAME_MISC_EXPLOBOX]) ||
                   actor_named(attacker, g->runtime_names[Q1_NAME_MISC_EXPLOBOX2]))
            key = g->runtime_names[Q1_NAME_OBITUARY_QC_KS_BLEW_UP];
        else if (attacker->brush && !actor_named(attacker, g->runtime_names[Q1_NAME_WORLDSPAWN]))
            key = g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_SQUISH];
        else if (actor_named(attacker, g->runtime_names[Q1_NAME_TRAP_SHOOTER]) ||
                 actor_named(attacker, g->runtime_names[Q1_NAME_TRAP_SPIKESHOOTER]))
            key = g->runtime_names[Q1_NAME_OBITUARY_QC_KS_SPIKED];
        else if (actor_named(attacker, g->runtime_names[Q1_NAME_FIREBALL]) || actor_named(attacker, g->runtime_names[Q1_NAME_MISC_FIREBALL]))
            key = g->runtime_names[Q1_NAME_OBITUARY_QC_KS_LAVABALL];
        else if (actor_named(attacker, g->runtime_names[Q1_NAME_TRIGGER_CHANGELEVEL]))
            key = g->runtime_names[Q1_NAME_OBITUARY_QC_KS_TRIED_LEAVE];
    }
    if (!key) {
        if (victim->water_type == -3)
            key = q1_random(g) < .5f ? g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_DROWN1] : g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_DROWN2];
        else if (victim->water_type == -4)
            key = q1_random(g) < .5f ? g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_SLIME1] : g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_SLIME2];
        else if (victim->water_type == -5)
            key = victim->health < -15 ? g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_LAVA1]
                  : q1_random(g) < .5f ? g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_LAVA2]
                                       : g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_LAVA3];
        else if (attacker && attacker->brush && !actor_named(attacker, g->runtime_names[Q1_NAME_WORLDSPAWN]))
            key = g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_SQUISH];
        else if (attacker && *text(g, attacker->kill_string))
            key = attacker->kill_string;
        else
            key = named(input->death_type, g->runtime_names[Q1_NAME_FALLING]) ? g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_FALL] : g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_DIED];
    }
    return finish(g, out, key, classic, credit, delta, first, second, count);
}

static bool pack_obituary(qa_q1_game *g, const qa_q1_obituary_input *input, float roll,
                          qa_q1_obituary_result *out, qa_error *error) {
    const qa_q1_obituary_actor *victim = &input->victim, *attacker = input->attacker;
    bool hip = g->options.program == QA_Q1_HIPNOTIC;
    bool classic = g->options.edition != QA_Q1_RERELEASE;
    if (!victim->player || actor_named(attacker, g->runtime_names[Q1_NAME_TELEDEATH]) ||
        actor_named(attacker, g->runtime_names[Q1_NAME_TELEDEATH2]))
        return base_obituary(g, input, roll, out);
    qa_string_id key = 0, override = 0;
    const qa_q1_obituary_actor *credit = victim;
    int32_t delta = -1;
    qa_string_id second = 0;
    size_t count = 1;
    if (attacker && attacker->player) {
        if (qa_actor_id_equal(victim->actor, attacker->actor)) {
            if ((victim->weapon == QA_Q1_LIGHTNING && victim->water_level > 1) ||
                victim->weapon == QA_Q1_GRENADE)
                return base_obituary(g, input, roll, out);
            if (hip)
                key =
                    (classic ? roll > .4f : roll != 0) ? g->runtime_names[Q1_NAME_OBITUARY_QC_SUICIDE_BORED] : g->runtime_names[Q1_NAME_OBITUARY_QC_SUICIDE_LOADED];
            else if (!classic && roll < .5f)
                key = g->runtime_names[Q1_NAME_OBITUARY_QC_SUICIDE_BORED];
            else if (input->teamplay != 0 && victim->team != input->victim_saved_team)
                key = input->gamecfg & 16 ? g->runtime_names[Q1_NAME_OBITUARY_QC_CHANGED_TEAMS] : g->runtime_names[Q1_NAME_OBITUARY_QC_TRIED_CHANGE_TEAMS];
            else
                key = classic ? g->runtime_names[Q1_NAME_OBITUARY_QC_SUICIDE_BORED] : g->runtime_names[Q1_NAME_OBITUARY_QC_SUICIDE_LOADED];
        } else {
            if (input->teamplay == 2 && victim->team > 0 && victim->team == attacker->team)
                return base_obituary(g, input, roll, out);
            credit = attacker;
            delta = 1;
            second = attacker->name;
            count = 2;
            if (!hip && input->teamplay == 3 && input->tag_score &&
                !input->tag_score(input->tag_context, victim->actor, attacker->actor, &delta,
                                  error))
                return false;
            if (hip) {
                if (named(input->death_type, g->runtime_names[Q1_NAME_HIPNOTIC_EMPATHY]))
                    key = q1_random(g) < .5f ? g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_EMPATHY1] : g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_EMPATHY2];
                else if (named(input->inflictor_classname, g->runtime_names[Q1_NAME_PROXIMITY_GRENADE]))
                    key = q1_random(g) < .5f ? g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_BOMB1] : g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_BOMB2];
                else if (attacker->weapon == QA_Q1_LASER)
                    key = q1_random(g) < .5f ? g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_LASER1] : g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_LASER2];
                else if (attacker->weapon == QA_Q1_MJOLNIR)
                    key = g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_HAMMER];
            } else
                switch (attacker->weapon) {
                case QA_Q1_ROGUE_GRAPPLE:
                    key = g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_GRAPPLED];
                    break;
                case QA_Q1_LAVA_NAILGUN:
                case QA_Q1_LAVA_SUPER_NAILGUN:
                    key = g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_BURNED];
                    break;
                case QA_Q1_PLASMA:
                    key = g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_FUSED];
                    break;
                case QA_Q1_MULTI_GRENADE:
                case QA_Q1_MULTI_ROCKET:
                    key = g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_BLASTED];
                    break;
                default:
                    break;
                }
            if (!key) {
                if (!base_obituary(g, input, roll, out))
                    return false;
                out->credited_actor = attacker->actor;
                out->score_delta = delta;
                return true;
            }
        }
    } else {
        if (hip && *text(g, input->attacker_death_type)) {
            key = input->attacker_death_type;
            if (classic) {
                override = g->runtime_names[Q1_NAME_OBITUARY_TEXT_AUTHORED_KILL];
                second = input->attacker_death_type;
                count = 2;
            }
        } else if (hip && victim->water_type != 0 && victim->water_type != -1) {
            qa_q1_obituary_input water = *input;
            water.attacker = NULL;
            return base_obituary(g, &water, roll, out);
        } else if (attacker && attacker->monster) {
            key = 0;
            if (!hip && actor_named(attacker, g->runtime_names[Q1_NAME_MONSTER_DRAGON]))
                key = g->runtime_names[Q1_NAME_OBITUARY_QC_KS_DRAGON1];
            else
                for (size_t i = 0; i < sizeof(monsters) / sizeof(monsters[0]); ++i)
                    if (actor_named(attacker, g->runtime_names[monsters[i].classname])) {
                        key = g->runtime_names[monsters[i].key];
                        override = monsters[i].classic == Q1_NAME_COUNT ? 0 : g->runtime_names[monsters[i].classic];
                        break;
                    }
            return finish(g, out, key, override, credit, delta, victim->name, second, count);
        } else if (actor_named(attacker, g->runtime_names[Q1_NAME_EXPLO_BOX]))
            key = g->runtime_names[Q1_NAME_OBITUARY_QC_KS_BLEW_UP];
        else if (attacker && attacker->brush && !actor_named(attacker, g->runtime_names[Q1_NAME_WORLDSPAWN]))
            key = g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_SQUISH];
        else if (hip && named(input->death_type, g->runtime_names[Q1_NAME_FALLING]))
            key = g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_FALL];
        else if (actor_named(attacker, g->runtime_names[Q1_NAME_TRAP_SHOOTER]) ||
                 actor_named(attacker, g->runtime_names[Q1_NAME_TRAP_SPIKESHOOTER]))
            key = g->runtime_names[Q1_NAME_OBITUARY_QC_KS_SPIKED];
        else if (actor_named(attacker, g->runtime_names[Q1_NAME_FIREBALL]))
            key = g->runtime_names[Q1_NAME_OBITUARY_QC_KS_LAVABALL];
        else if (actor_named(attacker, g->runtime_names[Q1_NAME_TRIGGER_CHANGELEVEL]))
            key = g->runtime_names[Q1_NAME_OBITUARY_QC_KS_TRIED_LEAVE];
        else if (!hip) {
            if (actor_named(attacker, g->runtime_names[Q1_NAME_LTRAIL_START]) ||
                actor_named(attacker, g->runtime_names[Q1_NAME_LTRAIL_RELAY]))
                key = g->runtime_names[Q1_NAME_OBITUARY_QC_KS_RODE_LIGHTNING];
            else if (actor_named(attacker, g->runtime_names[Q1_NAME_PENDULUM]))
                key = g->runtime_names[Q1_NAME_OBITUARY_QC_KS_CLEAVED];
            else if (actor_named(attacker, g->runtime_names[Q1_NAME_BUZZSAW]))
                key = g->runtime_names[Q1_NAME_OBITUARY_QC_KS_SLICED];
            else if (actor_named(attacker, g->runtime_names[Q1_NAME_PLASMA]))
                key = g->runtime_names[Q1_NAME_OBITUARY_QC_KS_PLASMA];
            else if (actor_named(attacker, g->runtime_names[Q1_NAME_VENGEANCE])) {
                key = g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_VENGEANCE];
                credit = NULL;
                delta = 0;
            } else if (actor_named(attacker, g->runtime_names[Q1_NAME_POWER_SHIELD]) && input->telefrag_owner) {
                key = g->runtime_names[Q1_NAME_OBITUARY_QC_DEATH_SMASHED];
                credit = input->telefrag_owner;
                delta = 1;
                second = credit->name;
                count = 2;
            }
        }
    }
    return key ? finish(g, out, key, override, credit, delta, victim->name, second, count)
               : base_obituary(g, input, roll, out);
}

bool qa_q1_obituary(qa_q1_game *g, const qa_q1_obituary_input *input, qa_q1_obituary_result *out,
                    qa_error *error) {
    if (!g || !input || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid Q1 obituary");
        return false;
    }
    qa_q1_obituary_result result = {0};
    float roll = q1_random(g);
    bool pack = g->options.program == QA_Q1_HIPNOTIC || g->options.program == QA_Q1_ROGUE;
    bool ok = pack ? pack_obituary(g, input, roll, &result, error)
                   : base_obituary(g, input, roll, &result);
    if (ok)
        *out = result;
    return ok;
}

bool qa_q1_client_notice_result(qa_q1_game *g, qa_actor_id actor, qa_string_id name,
                                qa_q1_client_notice event, int32_t frags,
                                qa_q1_obituary_result *out, qa_error *error) {
    if (!g || !out || event < QA_Q1_CLIENT_CONNECT || event > QA_Q1_CLIENT_EXIT) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid Q1 client notice");
        return false;
    }
    static const q1_runtime_name keys[] = {Q1_NAME_OBITUARY_QC_ENTERED, Q1_NAME_OBITUARY_QC_LEFT_GAME, Q1_NAME_OBITUARY_QC_SUICIDES,
                                       Q1_NAME_OBITUARY_QC_EXITED};
    qa_q1_obituary_result result = {0};
    if (!message(g, &result, g->runtime_names[keys[event]], 0, name, 0,
                 event == QA_Q1_CLIENT_DISCONNECT ? 2 : 1))
        return false;
    if (event == QA_Q1_CLIENT_DISCONNECT)
        result.arguments[1] =
            (qa_builtin_message_arg){QA_BUILTIN_MESSAGE_NUMBER, {.number = frags}};
    if (event == QA_Q1_CLIENT_SUICIDE) {
        result.credited_actor = actor;
        result.score_delta = -2;
    }
    *out = result;
    return true;
}
