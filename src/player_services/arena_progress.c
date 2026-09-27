#include "qa/arena_progress.h"
#include "qa/network_q3.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static const char *const score_names[] = {"g_spScores1", "g_spScores2", "g_spScores3",
                                          "g_spScores4", "g_spScores5"};
static bool fail(qa_error *error, const char *message) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", message);
    return false;
}
static int32_t signed_word(uint32_t bits) {
    int32_t result;
    memcpy(&result, &bits, sizeof(result));
    return result;
}
static int32_t game_integer(const char *text) {
    const unsigned char *p = (const unsigned char *)text;
    while (*p && (*p <= 32 || *p >= 128))
        ++p;
    bool negative = *p == '-';
    if (*p == '+' || *p == '-')
        ++p;
    uint32_t value = 0;
    while (*p >= '0' && *p <= '9')
        value = value * 10u + (uint32_t)(*p++ - '0');
    return signed_word(negative ? 0u - value : value);
}
static bool read_value(const qa_arena_progress *progress, const char *name, const char *prefix,
                       int32_t index, int32_t *out, qa_error *error) {
    if (!progress || !progress->cvars || !out)
        return fail(error, "Missing arena progression owner/output");
    const qa_cvar_view *variable = qa_cvars_find(progress->cvars, name);
    char key[32], value[8192];
    (void)snprintf(key, sizeof(key), "%s%" PRId32, prefix, index);
    if (!qa_q3_info_value(variable ? variable->value : "", key, value, sizeof(value), error))
        return false;
    *out = game_integer(value);
    return true;
}
static bool write_value(qa_arena_progress *progress, const char *name, const char *prefix,
                        int32_t index, int32_t value, qa_error *error) {
    const qa_cvar_view *variable = qa_cvars_find(progress->cvars, name);
    const char *text = variable ? variable->value : "";
    char info[1024], key[32], number[16];
    size_t size = strlen(text);
    if (size >= sizeof(info))
        return fail(error, "Arena progression info string exceeds source bound");
    memcpy(info, text, size + 1);
    (void)snprintf(key, sizeof(key), "%s%" PRId32, prefix, index);
    (void)snprintf(number, sizeof(number), "%" PRId32, value);
    return qa_q3_info_set(info, sizeof(info), key, number, error) &&
           qa_cvars_set(progress->cvars, name, info, true, error);
}
bool qa_arena_progress_init(qa_arena_progress *out, qa_cvars *cvars, qa_arena_catalog catalog,
                            uint64_t owner, qa_error *error) {
    if (!out || !cvars || catalog.regular_levels < 0 || catalog.regular_levels % 4 ||
        catalog.total_levels < catalog.regular_levels || catalog.training < -1 ||
        catalog.final < -1 || catalog.training >= catalog.total_levels ||
        catalog.final >= catalog.total_levels)
        return fail(error, "Invalid arena progression catalog");
    for (size_t i = 0; i < 5; ++i)
        if (!qa_cvars_register(cvars, score_names[i], "", QA_CVAR_ARCHIVE, owner, NULL, error))
            return false;
    if (!qa_cvars_register(cvars, "g_spAwards", "", QA_CVAR_ARCHIVE, owner, NULL, error) ||
        !qa_cvars_register(cvars, "g_spVideos", "", QA_CVAR_ARCHIVE, owner, NULL, error))
        return false;
    *out = (qa_arena_progress){cvars, catalog};
    return true;
}
bool qa_arena_progress_best(const qa_arena_progress *progress, int32_t level, qa_arena_best *out,
                            qa_error *error) {
    if (!out)
        return fail(error, "Missing arena best-score output");
    qa_arena_best best = {0};
    for (int32_t skill = 1; skill <= 5; ++skill) {
        int32_t rank;
        if (!read_value(progress, score_names[skill - 1], "l", level, &rank, error))
            return false;
        if (rank >= 1 && rank <= 8 && (best.rank == 0 || rank <= best.rank))
            best = (qa_arena_best){rank, skill};
    }
    *out = best;
    return true;
}
bool qa_arena_progress_award(const qa_arena_progress *progress, int32_t medal, int32_t *out,
                             qa_error *error) {
    return read_value(progress, "g_spAwards", "a", medal, out, error);
}
bool qa_arena_progress_movie(const qa_arena_progress *progress, int32_t tier, bool *out,
                             qa_error *error) {
    if (!out)
        return fail(error, "Missing arena movie output");
    int32_t value = 0;
    if (tier > 0 && !read_value(progress, "g_spVideos", "tier", tier, &value, error))
        return false;
    *out = value != 0;
    return true;
}
bool qa_arena_progress_current(const qa_arena_progress *progress, int32_t *out, qa_error *error) {
    if (!progress || !progress->cvars || !out)
        return fail(error, "Missing arena progression owner/output");
    qa_arena_best best;
    int32_t training = progress->catalog.training;
    if (training >= 0) {
        if (!qa_arena_progress_best(progress, training, &best, error))
            return false;
        if (best.rank != 1) {
            *out = training;
            return true;
        }
    }
    for (int32_t level = 0; level < progress->catalog.regular_levels; ++level) {
        if (!qa_arena_progress_best(progress, level, &best, error))
            return false;
        if (best.rank != 1) {
            *out = level;
            return true;
        }
    }
    *out = progress->catalog.final;
    return true;
}
bool qa_arena_progress_available(const qa_arena_progress *progress, int32_t level, bool *out,
                                 qa_error *error) {
    if (!progress || !out)
        return fail(error, "Missing arena availability owner/output");
    const qa_arena_catalog *catalog = &progress->catalog;
    if (catalog->training >= 0 && level == catalog->training) {
        *out = true;
        return true;
    }
    if (level < 0 || level >= catalog->total_levels) {
        *out = false;
        return true;
    }
    int32_t current;
    if (!qa_arena_progress_current(progress, &current, error))
        return false;
    if (catalog->training >= 0 && current == catalog->training) {
        *out = false;
        return true;
    }
    if (current < 0) {
        *out = true;
        return true;
    }
    int32_t current_tier = current == catalog->final ? catalog->regular_levels / 4 : current / 4;
    int32_t tier = level == catalog->final ? catalog->regular_levels / 4 : level / 4;
    *out = tier <= current_tier;
    return true;
}
bool qa_arena_progress_tier(const qa_arena_progress *progress, int32_t level, int32_t *out,
                            qa_error *error) {
    if (!progress || !out)
        return fail(error, "Missing arena tier owner/output");
    const qa_arena_catalog *catalog = &progress->catalog;
    if (catalog->training >= 0 && level == catalog->training) {
        *out = 0;
        return true;
    }
    if (catalog->final >= 0 && level == catalog->final) {
        *out = catalog->regular_levels / 4 + 1;
        return true;
    }
    if (level < 0 || level >= catalog->regular_levels) {
        *out = -1;
        return true;
    }
    int32_t tier = level / 4;
    for (int32_t index = tier * 4; index < tier * 4 + 4; ++index) {
        qa_arena_best best;
        if (!qa_arena_progress_best(progress, index, &best, error))
            return false;
        if (best.rank != 1) {
            *out = -1;
            return true;
        }
    }
    *out = tier + 1;
    return true;
}
static bool grant(qa_arena_progress *progress, qa_arena_postgame *out, int32_t medal, int32_t count,
                  int32_t display, qa_error *error) {
    if (!count)
        return true;
    int32_t previous;
    if (!qa_arena_progress_award(progress, medal, &previous, error) ||
        !write_value(progress, "g_spAwards", "a", medal,
                     signed_word((uint32_t)previous + (uint32_t)count), error))
        return false;
    if (display)
        out->awards[out->award_count++] = (qa_arena_award){medal, display};
    return true;
}
bool qa_arena_progress_record(qa_arena_progress *progress, const qa_arena_result *result,
                              qa_arena_postgame *out, qa_error *error) {
    if (!progress || !progress->cvars || !result || !out || result->level < 0 ||
        result->level >= progress->catalog.total_levels || result->rank < 1 || result->rank > 8 ||
        result->skill < 1 || result->skill > 5)
        return fail(error, "Invalid arena result");
    const char *name = score_names[result->skill - 1];
    int32_t previous;
    if (!read_value(progress, name, "l", result->level, &previous, error))
        return false;
    if ((!previous || previous > result->rank) &&
        !write_value(progress, name, "l", result->level, result->rank, error))
        return false;
    qa_arena_postgame postgame = {.rank = result->rank, .completed_tier = -1, .unlocked_movie = -1};
    if (result->accuracy >= 50 && !grant(progress, &postgame, 0, 1, result->accuracy, error))
        return false;
    if (!grant(progress, &postgame, 1, result->impressive, result->impressive, error) ||
        !grant(progress, &postgame, 2, result->excellent, result->excellent, error) ||
        !grant(progress, &postgame, 3, result->gauntlet, result->gauntlet, error) ||
        !qa_arena_progress_award(progress, 4, &previous, error))
        return false;
    int32_t old_hundreds = previous / 100;
    if (!grant(progress, &postgame, 4, result->frags, 0, error) ||
        !qa_arena_progress_award(progress, 4, &previous, error))
        return false;
    int32_t hundreds = previous / 100;
    if (hundreds > old_hundreds)
        postgame.awards[postgame.award_count++] = (qa_arena_award){4, hundreds * 100};
    if (result->perfect && !grant(progress, &postgame, 5, 1, 1, error))
        return false;
    if (result->rank == 1 &&
        !qa_arena_progress_tier(progress, result->level, &postgame.completed_tier, error))
        return false;
    if (postgame.completed_tier >= 0) {
        int32_t movie = postgame.completed_tier + 1;
        bool unlocked;
        if (!qa_arena_progress_movie(progress, movie, &unlocked, error))
            return false;
        if (!unlocked) {
            if (!write_value(progress, "g_spVideos", "tier", movie, 1, error))
                return false;
            postgame.unlocked_movie = movie;
        }
    }
    if (!qa_arena_progress_current(progress, &postgame.next_level, error))
        return false;
    *out = postgame;
    return true;
}
bool qa_arena_progress_reset(qa_arena_progress *progress, qa_error *error) {
    if (!progress || !progress->cvars)
        return fail(error, "Missing arena progression owner");
    for (size_t i = 0; i < 5; ++i)
        if (!qa_cvars_set(progress->cvars, score_names[i], "", true, error))
            return false;
    return qa_cvars_set(progress->cvars, "g_spAwards", "", true, error) &&
           qa_cvars_set(progress->cvars, "g_spVideos", "", true, error);
}
bool qa_arena_progress_unlock_levels(qa_arena_progress *progress, qa_error *error) {
    if (!progress || !progress->cvars)
        return fail(error, "Missing arena progression owner");
    for (int32_t level = 0; level < progress->catalog.total_levels; ++level)
        if (!write_value(progress, score_names[0], "l", level, 1, error))
            return false;
    for (int32_t tier = 1; tier <= 8; ++tier)
        if (!write_value(progress, "g_spVideos", "tier", tier, 1, error))
            return false;
    return true;
}
bool qa_arena_progress_unlock_medals(qa_arena_progress *progress, qa_error *error) {
    if (!progress || !progress->cvars)
        return fail(error, "Missing arena progression owner");
    for (int32_t medal = 0; medal < 6; ++medal)
        if (!write_value(progress, "g_spAwards", "a", medal, 100, error))
            return false;
    return true;
}
