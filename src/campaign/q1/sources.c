#include "qa/campaign_q1_sources.h"
#include <stdlib.h>
#include <string.h>

#define Q1_CAMPAIGN_SOURCE_NAME_LIST(X) \
    X(BOSS, "boss") \
    X(E1M7, "e1m7") \
    X(E2M6, "e2m6") \
    X(E3M6, "e3m6") \
    X(E4M7, "e4m7") \
    X(E5END, "e5end") \
    X(E5M6, "e5m6") \
    X(E5SM2, "e5sm2") \
    X(HIP1M4, "hip1m4") \
    X(HIP2M5, "hip2m5") \
    X(HIPEND, "hipend") \
    X(HORDE1, "horde1") \
    X(HORDE2, "horde2") \
    X(HORDE3, "horde3") \
    X(HUB, "hub") \
    X(MAP1, "map1") \
    X(MGE1M1, "mge1m1") \
    X(MGE1M3, "mge1m3") \
    X(MGEND, "mgend") \
    X(R1M7, "r1m7") \
    X(R2M8, "r2m8") \
    X(SECRET2, "secret2") \
    X(START, "start")

typedef enum q1_campaign_source_name {
#define Q1_CAMPAIGN_SOURCE_NAME_ENUM(key, text) Q1_CAMPAIGN_SOURCE_NAME_##key,
    Q1_CAMPAIGN_SOURCE_NAME_LIST(Q1_CAMPAIGN_SOURCE_NAME_ENUM)
#undef Q1_CAMPAIGN_SOURCE_NAME_ENUM
    Q1_CAMPAIGN_SOURCE_NAME_COUNT
} q1_campaign_source_name;

struct qa_q1_campaign_source {
    qa_q1_campaign_source_options options;
    qa_string_id runtime_names[Q1_CAMPAIGN_SOURCE_NAME_COUNT];
};
static bool fail(qa_error *error, const char *message) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", message);
    return false;
}
static bool addon(const qa_q1_campaign_source *source) {
    qa_q1_program program = source->options.program;
    return program == QA_Q1_DOPA || program == QA_Q1_MG1 || program == QA_Q1_MG3;
}
static bool intern(const qa_q1_campaign_source *source, const char *text, qa_string_id *out,
                   qa_error *error) {
    return qa_strings_intern_cstr(qa_session_strings(source->options.session), text, out, error);
}
static bool text_present(const qa_q1_campaign_source *source, qa_string_id id) {
    return qa_strings_text(qa_session_strings(source->options.session), id).size != 0;
}
qa_q1_campaign_source *qa_q1_campaign_source_create(const qa_q1_campaign_source_options *options,
                                                    qa_error *error) {
    if (!options || !options->session || !options->server_flags || !options->travel ||
        options->program < QA_Q1_HIPNOTIC || options->program > QA_Q1_MG3) {
        fail(error, "Invalid Q1 expansion campaign options");
        return NULL;
    }
    qa_q1_campaign_source probe = {.options = *options};
    const char *map = qa_strings_cstr(qa_session_strings(options->session), options->current_map);
    bool is_addon = addon(&probe);
    if (!map || !*map || !options->command || !options->achievement ||
        (is_addon && (!options->read_text || !options->write_text || !options->cvar ||
                      (options->program != QA_Q1_MG3 && !options->finish_horde))) ||
        (!is_addon && (!options->finale_finished || !options->schedule))) {
        fail(error, "Missing Q1 expansion campaign service");
        return NULL;
    }
    qa_q1_campaign_source *source = malloc(sizeof(*source));
    if (!source) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating expansion campaign rule");
        return NULL;
    }
    *source = probe;
    static const char *const runtime_names[] = {
#define Q1_CAMPAIGN_SOURCE_NAME_TEXT(key, text) text,
        Q1_CAMPAIGN_SOURCE_NAME_LIST(Q1_CAMPAIGN_SOURCE_NAME_TEXT)
#undef Q1_CAMPAIGN_SOURCE_NAME_TEXT
    };
    for (unsigned i = 0; i < Q1_CAMPAIGN_SOURCE_NAME_COUNT; ++i)
        if (!qa_strings_intern_cstr(qa_session_strings(options->session), runtime_names[i],
                                    &source->runtime_names[i], error)) {
            free(source);
            return NULL;
        }
    return source;
}
void qa_q1_campaign_source_destroy(qa_q1_campaign_source *source) { free(source); }
uint32_t qa_q1_mg1_last_sigil(uint32_t flags) {
    return (flags >> QA_Q1_MG1_LAST_SIGIL_SHIFT) & QA_Q1_MG1_ALL_SIGILS;
}
uint32_t qa_q1_mg1_clear_last_sigil(uint32_t flags) {
    return flags & ~((uint32_t)QA_Q1_MG1_ALL_SIGILS << QA_Q1_MG1_LAST_SIGIL_SHIFT);
}
unsigned qa_q1_mg3_rune_count(uint32_t flags) {
    unsigned count = 0;
    for (uint32_t bit = 1; bit <= 8; bit <<= 1)
        if (flags & bit)
            ++count;
    return count;
}
static bool touch(void *context, qa_q1_level *level, qa_actor_id trigger, qa_actor_id player,
                  qa_error *error) {
    (void)level;
    (void)player;
    qa_q1_campaign_source *source = context;
    qa_q1_campaign_source_options *o = &source->options;
    qa_string_id text = o->read_text(o->context, trigger, QA_Q1_CAMPAIGN_ENDTEXT);
    if (!o->write_text(o->context, o->world, QA_Q1_CAMPAIGN_INTERMISSION_TEXT, text, error))
        return false;
    if ((o->current_map == source->runtime_names[Q1_CAMPAIGN_SOURCE_NAME_MGEND]))
        *o->server_flags = 0;
    return true;
}
static bool begin(void *context, qa_q1_level *level, qa_string_id map, qa_actor_id cause,
                  double seconds, qa_error *error) {
    (void)level;
    (void)cause;
    qa_q1_campaign_source *source = context;
    qa_q1_campaign_source_options *o = &source->options;
    if (o->official_campaign) {
        const char *normal = NULL, *nightmare = NULL;
        if ((o->current_map == source->runtime_names[Q1_CAMPAIGN_SOURCE_NAME_E5END])) {
            normal = "ACH_COMPLETE_E5END";
            nightmare = "ACH_COMPLETE_E5END_NIGHTMARE";
        } else if ((o->current_map == source->runtime_names[Q1_CAMPAIGN_SOURCE_NAME_MGEND])) {
            normal = "ACH_COMPLETE_MGEND";
            nightmare = "ACH_COMPLETE_MGEND_NIGHTMARE";
        } else if (o->program == QA_Q1_MG3 && (o->current_map == source->runtime_names[Q1_CAMPAIGN_SOURCE_NAME_BOSS]) &&
                   ((map == source->runtime_names[Q1_CAMPAIGN_SOURCE_NAME_START]) || (map == source->runtime_names[Q1_CAMPAIGN_SOURCE_NAME_MAP1]))) {
            normal = "ACH_COMPLETE_MG3";
            nightmare = "ACH_COMPLETE_MG3_NIGHTMARE";
        }
        if (normal && (!o->achievement(o->context, normal, error) ||
                       (o->skill == 3 && !o->achievement(o->context, nightmare, error))))
            return false;
    }
    if ((o->current_map == source->runtime_names[Q1_CAMPAIGN_SOURCE_NAME_E5M6]) && (map == source->runtime_names[Q1_CAMPAIGN_SOURCE_NAME_E5SM2]) &&
        !o->achievement(o->context, "ACH_FIND_E5M8", error))
        return false;
    if ((o->current_map == source->runtime_names[Q1_CAMPAIGN_SOURCE_NAME_MGE1M1]) && (map == source->runtime_names[Q1_CAMPAIGN_SOURCE_NAME_MGE1M3]) &&
        !o->achievement(o->context, "ACH_FIND_MGE1M3", error))
        return false;
    return o->program == QA_Q1_MG3 || o->finish_horde(o->context, seconds, error);
}
static bool present(qa_q1_campaign_source *source, const char *key, int32_t track, bool literal,
                    qa_q1_finale_decision *decision, qa_q1_intermission_result *result,
                    qa_error *error) {
    qa_string_id text;
    if (!intern(source, literal ? key : qa_q1_finale_text(source->options.rerelease, key), &text,
                error))
        return false;
    *decision = QA_Q1_FINALE_PRESENT;
    *result = (qa_q1_intermission_result){
        .kind = QA_Q1_INTERMISSION_FINALE, .text = text, .track = track};
    return true;
}
static bool addon_finale(qa_q1_campaign_source *source, uint32_t stage,
                         qa_q1_finale_decision *decision, qa_q1_intermission_result *result,
                         qa_error *error) {
    qa_q1_campaign_source_options *o = &source->options;
    *decision = QA_Q1_FINALE_TRAVEL;
    if (stage == 2) {
        const char *key = (o->current_map == source->runtime_names[Q1_CAMPAIGN_SOURCE_NAME_E1M7])
                              ? (o->registered ? "$qc_finale_e1" : "$qc_finale_e1_shareware")
                          : (o->current_map == source->runtime_names[Q1_CAMPAIGN_SOURCE_NAME_E2M6]) ? "$qc_finale_e2"
                          : (o->current_map == source->runtime_names[Q1_CAMPAIGN_SOURCE_NAME_E3M6]) ? "$qc_finale_e3"
                          : (o->current_map == source->runtime_names[Q1_CAMPAIGN_SOURCE_NAME_E4M7]) ? "$qc_finale_e4"
                                                                  : NULL;
        qa_string_id text = 0;
        if (key) {
            if (!intern(source, key, &text, error))
                return false;
        } else {
            text = o->read_text(o->context, o->world, QA_Q1_CAMPAIGN_ENDTEXT);
            if (!text_present(source, text))
                text = o->read_text(o->context, o->world, QA_Q1_CAMPAIGN_INTERMISSION_TEXT);
        }
        if (!o->write_text(o->context, o->world, QA_Q1_CAMPAIGN_INTERMISSION_TEXT, 0, error))
            return false;
        if (text_present(source, text)) {
            *decision = QA_Q1_FINALE_PRESENT;
            *result = (qa_q1_intermission_result){
                .kind = QA_Q1_INTERMISSION_FINALE, .text = text, .track = 2};
        }
    } else if (stage == 3) {
        if (!o->registered) {
            *decision = QA_Q1_FINALE_PRESENT;
            result->kind = QA_Q1_INTERMISSION_SELL;
        } else if (o->program != QA_Q1_MG3 &&
                   (*o->server_flags & QA_Q1_MG1_ALL_SIGILS) == QA_Q1_MG1_ALL_SIGILS)
            return present(source, "$qc_mg1_endtext_all_runes", 2, true, decision, result, error);
    }
    return true;
}
static bool finale(void *context, qa_q1_level *level, uint32_t stage, qa_string_id next_map,
                   double seconds, qa_q1_finale_decision *decision,
                   qa_q1_intermission_result *result, qa_error *error) {
    (void)next_map;
    qa_q1_campaign_source *source = context;
    qa_q1_campaign_source_options *o = &source->options;
    *decision = QA_Q1_FINALE_DELEGATE;
    if (addon(source))
        return addon_finale(source, stage, decision, result, error);
    if (o->program == QA_Q1_ROGUE) {
        if (stage == 2 && (o->current_map == source->runtime_names[Q1_CAMPAIGN_SOURCE_NAME_R1M7]))
            return present(source, "$qc_finale_r1", 3, false, decision, result, error);
        if (stage == 2 && (o->current_map == source->runtime_names[Q1_CAMPAIGN_SOURCE_NAME_R2M8]) && o->coop && o->rerelease) {
            if (!o->command(o->context, "menu_credits\ndisconnect\n", error) ||
                !qa_q1_level_defer_exit(level, seconds + 10000000, error))
                return false;
            return present(source, "", 3, true, decision, result, error);
        }
        return true;
    }
    if (stage == 2) {
        if ((o->current_map == source->runtime_names[Q1_CAMPAIGN_SOURCE_NAME_HIP1M4]))
            return present(source, "$qc_finale_hip1", 6, false, decision, result, error);
        if ((o->current_map == source->runtime_names[Q1_CAMPAIGN_SOURCE_NAME_HIP2M5]))
            return present(source, "$qc_finale_hip2", 6, false, decision, result, error);
        if ((o->current_map == source->runtime_names[Q1_CAMPAIGN_SOURCE_NAME_HIPEND])) {
            if (o->rerelease && o->official_campaign &&
                (!o->achievement(o->context, "ACH_COMPLETE_HIPEND", error) ||
                 (o->skill == 3 &&
                  !o->achievement(o->context, "ACH_COMPLETE_HIPEND_NIGHTMARE", error))))
                return false;
            return present(source, "$qc_finale_hipend", 2, false, decision, result, error);
        }
    } else if (stage == 3 && o->registered && (*o->server_flags & 15u) != 15u) {
        if ((o->current_map == source->runtime_names[Q1_CAMPAIGN_SOURCE_NAME_HIP1M4]))
            return present(source, "$qc_finale_hip1m4", 6, false, decision, result, error);
        if ((o->current_map == source->runtime_names[Q1_CAMPAIGN_SOURCE_NAME_HIP2M5]))
            return present(source, "$qc_finale_hip2m5", 6, false, decision, result, error);
        if ((o->current_map == source->runtime_names[Q1_CAMPAIGN_SOURCE_NAME_HIPEND])) {
            if (!qa_q1_level_defer_exit(level, seconds + 10000000, error) ||
                (o->rerelease && !o->schedule(o->context, QA_Q1_CAMPAIGN_CHECK_FINALE, 1, error)))
                return false;
            return present(source, "$qc_finale_hipend2", 2, false, decision, result, error);
        }
    }
    return true;
}
static bool travel(void *context, qa_q1_level *level, qa_string_id map, qa_actor_id cause,
                   bool *handled, qa_error *error) {
    (void)level;
    qa_q1_campaign_source *source = context;
    qa_q1_campaign_source_options *o = &source->options;
    *handled = true;
    if (o->cvar(o->context, "samelevel") != 0)
        return o->travel(o->context, o->current_map, cause, error);
    const char *replacement = NULL;
    if (o->program != QA_Q1_MG3 && o->cvar(o->context, "horde") != 0)
        replacement = (o->current_map == source->runtime_names[Q1_CAMPAIGN_SOURCE_NAME_HORDE1])   ? "horde2"
                      : (o->current_map == source->runtime_names[Q1_CAMPAIGN_SOURCE_NAME_HORDE2]) ? "horde3"
                      : (o->current_map == source->runtime_names[Q1_CAMPAIGN_SOURCE_NAME_HORDE3]) ? "horde4"
                                                                : "horde1";
    else if (o->program == QA_Q1_MG3 && (o->current_map == source->runtime_names[Q1_CAMPAIGN_SOURCE_NAME_HUB]) &&
             (*o->server_flags &
              (QA_Q1_BLOODY_NIGHTMARE_ACTIVE | QA_Q1_BLOODY_NIGHTMARE_NEWGAME)) ==
                 (QA_Q1_BLOODY_NIGHTMARE_ACTIVE | QA_Q1_BLOODY_NIGHTMARE_NEWGAME) &&
             (map == source->runtime_names[Q1_CAMPAIGN_SOURCE_NAME_SECRET2]))
        replacement = "boss2";
    if (replacement) {
        qa_string_id next;
        return intern(source, replacement, &next, error) &&
               o->travel(o->context, next, cause, error);
    }
    if ((map == source->runtime_names[Q1_CAMPAIGN_SOURCE_NAME_START]) && !o->coop && !o->deathmatch)
        return o->command(o->context, "menu_credits\ndisconnect\n", error);
    *handled = false;
    return true;
}
bool qa_q1_campaign_source_rule(qa_q1_campaign_source *source, qa_q1_intermission_rule *out,
                                qa_error *error) {
    if (!source || !out)
        return fail(error, "Missing Q1 campaign source rule");
    static const char *names[] = {"",
                                  "q1:hipnotic:campaign",
                                  "q1:rogue:campaign",
                                  "q1:dopa:campaign",
                                  "q1:mg1:campaign",
                                  "q1:mg3:campaign"};
    qa_string_id id;
    if (!intern(source, names[source->options.program], &id, error))
        return false;
    bool is_addon = addon(source);
    *out = (qa_q1_intermission_rule){.id = id,
                                     .context = source,
                                     .touch = is_addon ? touch : NULL,
                                     .begin = is_addon ? begin : NULL,
                                     .finale = finale,
                                     .travel = is_addon ? travel : NULL};
    return true;
}
bool qa_q1_campaign_source_timer(qa_q1_campaign_source *source, qa_q1_campaign_timer timer,
                                 qa_error *error) {
    if (!source || addon(source))
        return fail(error, "Invalid mission finale timer owner");
    qa_q1_campaign_source_options *o = &source->options;
    switch (timer) {
    case QA_Q1_CAMPAIGN_CHECK_FINALE: {
        bool finished = o->finale_finished(o->context);
        return o->schedule(o->context,
                           finished ? QA_Q1_CAMPAIGN_FINISH_FINALE : QA_Q1_CAMPAIGN_CHECK_FINALE,
                           finished ? 5 : 0.1, error);
    }
    case QA_Q1_CAMPAIGN_FINISH_FINALE:
        if (!o->coop)
            return o->command(o->context, "menu_credits\n", error) &&
                   o->command(o->context, "disconnect\n", error);
        else {
            qa_string_id map;
            return intern(source, "start", &map, error) &&
                   o->travel(o->context, map, (qa_actor_id){0}, error);
        }
    default:
        return fail(error, "Unknown mission finale timer");
    }
}
bool qa_q1_campaign_source_rogue_end(qa_q1_campaign_source *source, qa_error *error) {
    if (!source || source->options.program != QA_Q1_ROGUE)
        return fail(error, "Invalid Rogue ending campaign owner");
    const qa_q1_campaign_source_options *options = &source->options;
    if (!options->rerelease)
        return true;
    if (options->official_campaign && (options->current_map == source->runtime_names[Q1_CAMPAIGN_SOURCE_NAME_R2M8])) {
        if (!options->achievement(options->context, "ACH_COMPLETE_R2M8", error) ||
            (options->skill == 3 &&
             !options->achievement(options->context, "ACH_COMPLETE_R2M8_NIGHTMARE", error)))
            return false;
    }
    return options->schedule(options->context, QA_Q1_CAMPAIGN_CHECK_FINALE, 1, error);
}
