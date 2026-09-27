#include "internal.h"

typedef struct obituary_text {
    const char *key, *classic;
} obituary_text;
static const obituary_text messages[] = {
    {"$qc_telefragged", "{0} was telefragged by {1}\n"},
    {"$qc_satans_power", "Satan's power deflects {0}'s telefrag\n"},
    {"$qc_discharge_water", "{0} discharges into the water.\n"},
    {"$qc_discharge_slime", "{0} discharges into the water.\n"},
    {"$qc_discharge_lava", "{0} discharges into the water.\n"},
    {"$qc_suicide_pin", "{0} tries to put the pin back in\n"},
    {"$qc_suicide_bored", "{0} becomes bored with life\n"},
    {"$qc_suicide_loaded", "{0} becomes bored with life\n"},
    {"$qc_ff_teammate", "{0} mows down a teammate\n"},
    {"$qc_ff_glasses", "{0} checks his glasses\n"},
    {"$qc_ff_otherteam", "{0} gets a frag for the other team\n"},
    {"$qc_ff_friend", "{0} loses another friend\n"},
    {"$qc_death_ax", "{0} was ax-murdered by {1}\n"},
    {"$qc_death_sg", "{0} chewed on {1}'s boomstick\n"},
    {"$qc_death_dbl", "{0} ate 2 loads of {1}'s buckshot\n"},
    {"$qc_death_nail", "{0} was nailed by {1}\n"},
    {"$qc_death_sng", "{0} was punctured by {1}\n"},
    {"$qc_death_gl1", "{0} was gibbed by {1}'s grenade\n"},
    {"$qc_death_gl2", "{0} eats {1}'s pineapple\n"},
    {"$qc_death_rl2", "{0} was gibbed by {1}'s rocket\n"},
    {"$qc_death_rl3", "{0} rides {1}'s rocket\n"},
    {"$qc_death_lg1", "{0} accepts {1}'s discharge\n"},
    {"$qc_death_lg2", "{0} accepts {1}'s shaft\n"},
    {"$qc_death_drown1", "{0} sleeps with the fishes\n"},
    {"$qc_death_drown2", "{0} sucks it down\n"},
    {"$qc_death_slime1", "{0} gulped a load of slime\n"},
    {"$qc_death_slime2", "{0} can't exist on slime alone\n"},
    {"$qc_death_lava1", "{0} burst into flames\n"},
    {"$qc_death_lava2", "{0} turned into hot slag\n"},
    {"$qc_death_lava3", "{0} visits the Volcano God\n"},
    {"$qc_death_squish", "{0} was squished\n"},
    {"$qc_death_fall", "{0} fell to his death\n"},
    {"$qc_death_died", "{0} died\n"},
    {"$qc_death_empathy1", "{0} shares {1}'s pain\n"},
    {"$qc_death_empathy2", "{0} feels {1}'s pain\n"},
    {"$qc_death_bomb1", "{0} got too friendly with {1}'s bomb\n"},
    {"$qc_death_bomb2", "{0} did the rhumba with {1}'s bomb\n"},
    {"$qc_death_laser1", "{0} was toasted by {1}'s laser\n"},
    {"$qc_death_laser2", "{0} was radiated by {1}'s laser\n"},
    {"$qc_death_hammer", "{0} was slammed by {1}'s hammer\n"},
    {"$qc_death_grappled", "{0} was grappled by {1}\n"},
    {"$qc_death_burned", "{0} was burned by {1}\n"},
    {"$qc_death_fused", "{0} was fused by {1}\n"},
    {"$qc_death_blasted", "{0} was blasted to bits by {1}\n"},
    {"$qc_death_vengeance", "{0} was purged by the Vengeance Sphere\n"},
    {"$qc_death_smashed", "{0} was smashed by {1}\n"},
    {"$qc_changed_teams", "{0} changed teams\n"},
    {"$qc_tried_change_teams", "{0} tried to change teams\n"},
    {"$qc_ks_dragon1", "{0} was annihilated by the Dragon\n"},
    {"$qc_ks_dragon2", "{0} was squashed by the Dragon\n"},
    {"$qc_ks_eel", "{0} was electrified by an Eel\n"},
    {"$qc_ks_wrath", "{0} was disintegrated by a Wrath\n"},
    {"$qc_ks_overlord", "{0} was obliterated by an Overlord\n"},
    {"$qc_ks_swordsman", "{0} was slit open by a Phantom Swordsman\n"},
    {"$qc_ks_hephaestus", "{0} fries in Hephaestus' fury\n"},
    {"$qc_ks_guardian", "{0} was crushed by a Guardian\n"},
    {"$qc_ks_mummy", "{0} was Mummified\n"},
    {"$qc_ks_gremlin", "{0} was outsmarted by a Gremlin\n"},
    {"$qc_ks_centroid", "{0} was stung by a Centroid\n"},
    {"$qc_ks_armagon", "{0} was outgunned by Armagon\n"},
    {"$qc_ks_blew_up", "{0} blew up\n"},
    {"$qc_ks_spiked", "{0} was spiked\n"},
    {"$qc_ks_lavaball", "{0} ate a lavaball\n"},
    {"$qc_ks_tried_leave", "{0} tried to leave\n"},
    {"$qc_ks_rode_lightning", "{0} rode the lightning\n"},
    {"$qc_ks_cleaved", "{0} was cleaved in two\n"},
    {"$qc_ks_sliced", "{0} was sliced to pieces\n"},
    {"$qc_ks_plasma", "{0} was turned to plasma\n"},
    {"$qc_entered", "{0} entered the game\n"},
    {"$qc_left_game", "{0} left the game with {1} frags\n"},
    {"$qc_suicides", "{0} suicides\n"},
    {"$qc_exited", "{0} exited the level\n"}};
typedef struct monster_message {
    const char *classname, *key, *classic;
} monster_message;
static const monster_message monsters[] = {
    {"monster_army", "$qc_ks_grunt", "{0} was shot by a Grunt\n"},
    {"monster_demon1", "$qc_ks_fiend", "{0} was eviscerated by a Fiend\n"},
    {"monster_dog", "$qc_ks_rottweiler", "{0} was mauled by a Rottweiler\n"},
    {"monster_dragon", "$qc_ks_dragon", "{0} was fried by a Dragon\n"},
    {"monster_enforcer", "$qc_ks_enforcer", "{0} was blasted by an Enforcer\n"},
    {"monster_fish", "$qc_ks_rotfish", "{0} was fed to the Rotfish\n"},
    {"monster_hell_knight", "$qc_ks_deathknight", "{0} was slain by a Death Knight\n"},
    {"monster_knight", "$qc_ks_knight", "{0} was slashed by a Knight\n"},
    {"monster_ogre", "$qc_ks_ogre", "{0} was destroyed by an Ogre\n"},
    {"monster_oldone", "$qc_ks_shub", "{0} became one with Shub-Niggurath\n"},
    {"monster_shalrath", "$qc_ks_vore", "{0} was exploded by a Vore\n"},
    {"monster_shambler", "$qc_ks_shambler", "{0} was smashed by a Shambler\n"},
    {"monster_tarbaby", "$qc_ks_spawn", "{0} was slimed by a Spawn\n"},
    {"monster_vomit", "$qc_ks_vomitus", "{0} was vomited on by a Vomitus\n"},
    {"monster_wizard", "$qc_ks_scrag", "{0} was scragged by a Scrag\n"},
    {"monster_zombie", "$qc_ks_zombie", "{0} joins the Zombies\n"},
    {"monster_dragon_dead", "$qc_ks_dragon2", NULL},
    {"monster_gremlin", "$qc_ks_gremlin", NULL},
    {"monster_scourge", "$qc_ks_centroid", NULL},
    {"monster_armagon", "$qc_ks_armagon", NULL},
    {"monster_eel", "$qc_ks_eel", NULL},
    {"monster_wrath", "$qc_ks_wrath", NULL},
    {"monster_super_wrath", "$qc_ks_overlord", NULL},
    {"monster_sword", "$qc_ks_swordsman", NULL},
    {"monster_lava_man", "$qc_ks_hephaestus", NULL},
    {"monster_morph", "$qc_ks_guardian", NULL},
    {"monster_mummy", "$qc_ks_mummy", NULL}};

static const char *text(qa_q1_game *g, qa_string_id id) {
    const char *value = qa_strings_cstr(qa_session_strings(g->services.session), id);
    return value ? value : "";
}
static bool named(qa_q1_game *g, qa_string_id id, const char *name) {
    return !strcmp(text(g, id), name);
}
static bool actor_named(qa_q1_game *g, const qa_q1_obituary_actor *actor, const char *name) {
    return actor && named(g, actor->classname, name);
}
static bool message(qa_q1_game *g, qa_q1_obituary_result *out, const char *key,
                    const char *classic_override, qa_string_id first, qa_string_id second,
                    size_t count, qa_error *error) {
    if (!*key)
        return true;
    if (g->options.edition == QA_Q1_RERELEASE) {
        if (!qa_builtin_resource(&g->services, key, &out->text, error))
            return false;
        out->argument_count = count;
        out->arguments[0] = (qa_builtin_message_arg){QA_BUILTIN_MESSAGE_STRING, {.text = first}};
        out->arguments[1] = (qa_builtin_message_arg){QA_BUILTIN_MESSAGE_STRING, {.text = second}};
        return true;
    }
    const char *format = classic_override;
    if (!format) {
        if ((g->options.program == QA_Q1_HIPNOTIC || g->options.program == QA_Q1_ROGUE) &&
            !strcmp(key, "$qc_suicide_loaded"))
            format = "{0} checks if his weapon is loaded\n";
        for (size_t i = 0; !format && i < sizeof(messages) / sizeof(messages[0]); ++i)
            if (!strcmp(key, messages[i].key))
                format = messages[i].classic;
        if (!format)
            format = key;
    }
    if (!qa_builtin_resource(&g->services, format, &out->text, error))
        return false;
    out->argument_count = count;
    out->arguments[0] = (qa_builtin_message_arg){QA_BUILTIN_MESSAGE_STRING, {.text = first}};
    out->arguments[1] = (qa_builtin_message_arg){QA_BUILTIN_MESSAGE_STRING, {.text = second}};
    return true;
}

static bool finish(qa_q1_game *g, qa_q1_obituary_result *out, const char *key, const char *classic,
                   const qa_q1_obituary_actor *credit, int32_t delta, qa_string_id first,
                   qa_string_id second, size_t count, qa_error *error) {
    if (credit) {
        out->credited_actor = credit->actor;
        out->score_delta = delta;
    }
    return message(g, out, key, classic, first, second, count, error);
}

static bool base_obituary(qa_q1_game *g, const qa_q1_obituary_input *input, float roll,
                          qa_q1_obituary_result *out, qa_error *error) {
    const qa_q1_obituary_actor *victim = &input->victim, *attacker = input->attacker;
    if (!victim->player)
        return true;
    const char *key = NULL, *classic = NULL;
    const qa_q1_obituary_actor *credit = victim;
    int32_t delta = -1;
    qa_string_id first = victim->name, second = 0;
    size_t count = 1;
    if (actor_named(g, attacker, "teledeath") && input->telefrag_owner) {
        key = "$qc_telefragged";
        credit = input->telefrag_owner;
        delta = 1;
        second = credit->name;
        count = 2;
    } else if (actor_named(g, attacker, "teledeath2"))
        key = "$qc_satans_power";
    else if (attacker && attacker->player) {
        if (qa_actor_id_equal(victim->actor, attacker->actor)) {
            if (victim->weapon == QA_Q1_LIGHTNING && victim->water_level > 1)
                key = victim->water_type == -4   ? "$qc_discharge_slime"
                      : victim->water_type == -5 ? "$qc_discharge_lava"
                                                 : "$qc_discharge_water";
            else
                key = victim->weapon == QA_Q1_GRENADE ? "$qc_suicide_pin"
                      : roll != 0                     ? "$qc_suicide_bored"
                                                      : "$qc_suicide_loaded";
        } else if (input->teamplay == 2 && victim->team && victim->team == attacker->team) {
            key = roll < .25f   ? "$qc_ff_teammate"
                  : roll < .5f  ? "$qc_ff_glasses"
                  : roll < .75f ? "$qc_ff_otherteam"
                                : "$qc_ff_friend";
            credit = attacker;
            first = attacker->name;
        } else {
            credit = attacker;
            delta = 1;
            second = attacker->name;
            count = 2;
            switch (attacker->weapon) {
            case QA_Q1_AXE:
                key = "$qc_death_ax";
                break;
            case QA_Q1_SHOTGUN:
                key = "$qc_death_sg";
                break;
            case QA_Q1_SUPER_SHOTGUN:
                key = "$qc_death_dbl";
                break;
            case QA_Q1_NAILGUN:
                key = "$qc_death_nail";
                break;
            case QA_Q1_SUPER_NAILGUN:
                key = "$qc_death_sng";
                break;
            case QA_Q1_GRENADE:
                key = victim->health < -40 ? "$qc_death_gl1" : "$qc_death_gl2";
                break;
            case QA_Q1_ROCKET:
                if (g->options.edition == QA_Q1_RERELEASE && attacker->quad_expires > 0 &&
                    victim->health < -40) {
                    float choice = q1_random(g);
                    key = choice < .3f   ? "$qc_death_rl_quad1"
                          : choice < .6f ? "$qc_death_rl_quad2"
                                         : "$qc_death_rl1";
                } else
                    key = victim->health < -40 ? "$qc_death_rl2" : "$qc_death_rl3";
                break;
            case QA_Q1_LIGHTNING:
                key = attacker->water_level > 1 ? "$qc_death_lg1" : "$qc_death_lg2";
                if (g->options.edition == QA_Q1_RERELEASE && attacker->water_level > 1 &&
                    attacker->invulnerable_expires != 0) {
                    out->achievement_actor = attacker->actor;
                    if (!qa_builtin_resource(&g->services, "ACH_SURVIVE_DISCHARGE",
                                             &out->achievement, error))
                        return false;
                }
                break;
            default:
                key = text(g, attacker->kill_string);
                break;
            }
        }
    } else if (g->options.edition != QA_Q1_RERELEASE && attacker) {
        if (attacker->monster) {
            classic = "{0}";
            for (size_t i = 0; i < sizeof(monsters) / sizeof(monsters[0]); ++i)
                if (monsters[i].classic && actor_named(g, attacker, monsters[i].classname)) {
                    classic = monsters[i].classic;
                    break;
                }
            key = "classic-monster";
        } else if (actor_named(g, attacker, "explo_box") ||
                   actor_named(g, attacker, "misc_explobox") ||
                   actor_named(g, attacker, "misc_explobox2"))
            key = "$qc_ks_blew_up";
        else if (attacker->brush && !actor_named(g, attacker, "worldspawn"))
            key = "$qc_death_squish";
        else if (actor_named(g, attacker, "trap_shooter") ||
                 actor_named(g, attacker, "trap_spikeshooter"))
            key = "$qc_ks_spiked";
        else if (actor_named(g, attacker, "fireball") || actor_named(g, attacker, "misc_fireball"))
            key = "$qc_ks_lavaball";
        else if (actor_named(g, attacker, "trigger_changelevel"))
            key = "$qc_ks_tried_leave";
    }
    if (!key) {
        if (victim->water_type == -3)
            key = q1_random(g) < .5f ? "$qc_death_drown1" : "$qc_death_drown2";
        else if (victim->water_type == -4)
            key = q1_random(g) < .5f ? "$qc_death_slime1" : "$qc_death_slime2";
        else if (victim->water_type == -5)
            key = victim->health < -15 ? "$qc_death_lava1"
                  : q1_random(g) < .5f ? "$qc_death_lava2"
                                       : "$qc_death_lava3";
        else if (attacker && attacker->brush && !actor_named(g, attacker, "worldspawn"))
            key = "$qc_death_squish";
        else if (attacker && *text(g, attacker->kill_string))
            key = text(g, attacker->kill_string);
        else
            key = named(g, input->death_type, "falling") ? "$qc_death_fall" : "$qc_death_died";
    }
    return finish(g, out, key, classic, credit, delta, first, second, count, error);
}

static bool pack_obituary(qa_q1_game *g, const qa_q1_obituary_input *input, float roll,
                          qa_q1_obituary_result *out, qa_error *error) {
    const qa_q1_obituary_actor *victim = &input->victim, *attacker = input->attacker;
    bool hip = g->options.program == QA_Q1_HIPNOTIC;
    bool classic = g->options.edition != QA_Q1_RERELEASE;
    if (!victim->player || actor_named(g, attacker, "teledeath") ||
        actor_named(g, attacker, "teledeath2"))
        return base_obituary(g, input, roll, out, error);
    const char *key = NULL, *override = NULL;
    const qa_q1_obituary_actor *credit = victim;
    int32_t delta = -1;
    qa_string_id second = 0;
    size_t count = 1;
    if (attacker && attacker->player) {
        if (qa_actor_id_equal(victim->actor, attacker->actor)) {
            if ((victim->weapon == QA_Q1_LIGHTNING && victim->water_level > 1) ||
                victim->weapon == QA_Q1_GRENADE)
                return base_obituary(g, input, roll, out, error);
            if (hip)
                key =
                    (classic ? roll > .4f : roll != 0) ? "$qc_suicide_bored" : "$qc_suicide_loaded";
            else if (!classic && roll < .5f)
                key = "$qc_suicide_bored";
            else if (input->teamplay && victim->team != input->victim_saved_team)
                key = input->gamecfg & 16 ? "$qc_changed_teams" : "$qc_tried_change_teams";
            else
                key = classic ? "$qc_suicide_bored" : "$qc_suicide_loaded";
        } else {
            if (input->teamplay == 2 && victim->team && victim->team == attacker->team)
                return base_obituary(g, input, roll, out, error);
            credit = attacker;
            delta = 1;
            second = attacker->name;
            count = 2;
            if (!hip && input->teamplay == 3 && input->tag_score &&
                !input->tag_score(input->tag_context, victim->actor, attacker->actor, &delta,
                                  error))
                return false;
            if (hip) {
                if (named(g, input->death_type, "hipnotic:empathy"))
                    key = q1_random(g) < .5f ? "$qc_death_empathy1" : "$qc_death_empathy2";
                else if (named(g, input->inflictor_classname, "proximity_grenade"))
                    key = q1_random(g) < .5f ? "$qc_death_bomb1" : "$qc_death_bomb2";
                else if (attacker->weapon == QA_Q1_LASER)
                    key = q1_random(g) < .5f ? "$qc_death_laser1" : "$qc_death_laser2";
                else if (attacker->weapon == QA_Q1_MJOLNIR)
                    key = "$qc_death_hammer";
            } else
                switch (attacker->weapon) {
                case QA_Q1_ROGUE_GRAPPLE:
                    key = "$qc_death_grappled";
                    break;
                case QA_Q1_LAVA_NAILGUN:
                case QA_Q1_LAVA_SUPER_NAILGUN:
                    key = "$qc_death_burned";
                    break;
                case QA_Q1_PLASMA:
                    key = "$qc_death_fused";
                    break;
                case QA_Q1_MULTI_GRENADE:
                case QA_Q1_MULTI_ROCKET:
                    key = "$qc_death_blasted";
                    break;
                default:
                    break;
                }
            if (!key) {
                if (!base_obituary(g, input, roll, out, error))
                    return false;
                out->credited_actor = attacker->actor;
                out->score_delta = delta;
                return true;
            }
        }
    } else {
        if (hip && *text(g, input->attacker_death_type)) {
            key = text(g, input->attacker_death_type);
            if (classic) {
                override = "{0} {1}\n";
                second = input->attacker_death_type;
                count = 2;
            }
        } else if (hip && victim->water_type != 0 && victim->water_type != -1) {
            qa_q1_obituary_input water = *input;
            water.attacker = NULL;
            return base_obituary(g, &water, roll, out, error);
        } else if (attacker && attacker->monster) {
            key = "";
            if (!hip && actor_named(g, attacker, "monster_dragon"))
                key = "$qc_ks_dragon1";
            else
                for (size_t i = 0; i < sizeof(monsters) / sizeof(monsters[0]); ++i)
                    if (actor_named(g, attacker, monsters[i].classname)) {
                        key = monsters[i].key;
                        override = monsters[i].classic;
                        break;
                    }
        } else if (actor_named(g, attacker, "explo_box"))
            key = "$qc_ks_blew_up";
        else if (attacker && attacker->brush && !actor_named(g, attacker, "worldspawn"))
            key = "$qc_death_squish";
        else if (hip && named(g, input->death_type, "falling"))
            key = "$qc_death_fall";
        else if (actor_named(g, attacker, "trap_shooter") ||
                 actor_named(g, attacker, "trap_spikeshooter"))
            key = "$qc_ks_spiked";
        else if (actor_named(g, attacker, "fireball"))
            key = "$qc_ks_lavaball";
        else if (actor_named(g, attacker, "trigger_changelevel"))
            key = "$qc_ks_tried_leave";
        else if (!hip) {
            if (actor_named(g, attacker, "ltrail_start") ||
                actor_named(g, attacker, "ltrail_relay"))
                key = "$qc_ks_rode_lightning";
            else if (actor_named(g, attacker, "pendulum"))
                key = "$qc_ks_cleaved";
            else if (actor_named(g, attacker, "buzzsaw"))
                key = "$qc_ks_sliced";
            else if (actor_named(g, attacker, "plasma"))
                key = "$qc_ks_plasma";
            else if (actor_named(g, attacker, "Vengeance")) {
                key = "$qc_death_vengeance";
                credit = NULL;
                delta = 0;
            } else if (actor_named(g, attacker, "power_shield") && input->telefrag_owner) {
                key = "$qc_death_smashed";
                credit = input->telefrag_owner;
                delta = 1;
                second = credit->name;
                count = 2;
            }
        }
    }
    return key ? finish(g, out, key, override, credit, delta, victim->name, second, count, error)
               : base_obituary(g, input, roll, out, error);
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
                   : base_obituary(g, input, roll, &result, error);
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
    static const char *const keys[] = {"$qc_entered", "$qc_left_game", "$qc_suicides",
                                       "$qc_exited"};
    qa_q1_obituary_result result = {0};
    if (!message(g, &result, keys[event], NULL, name, 0, event == QA_Q1_CLIENT_DISCONNECT ? 2 : 1,
                 error))
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
