#include "internal.h"

static bool select_rank(const float *ranks, size_t count, const qa_bot_random_source *random,
                        uint32_t *out) {
    float sum = 0;
    for (size_t i = 0; i < count; ++i)
        if (ranks[i] >= 0)
            sum += ranks[i];
    if (sum > 0) {
        (void)bot_random(random);
        for (size_t i = 0; i < count; ++i)
            if (ranks[i] >= 0) {
                sum -= ranks[i];
                if (sum <= 0) {
                    *out = (uint32_t)i;
                    return true;
                }
            }
    }
    uint32_t index = (uint32_t)(bot_random(random) * (float)count);
    if (index >= count)
        return false;
    for (size_t i = 0; i < count; ++i) {
        if (ranks[index] >= 0) {
            *out = index;
            return true;
        }
        index = (index + 1) % (uint32_t)count;
    }
    *out = 0;
    return true;
}
bool qa_bot_genetic_select(const float *rank, size_t count, const qa_bot_random_source *random,
                           qa_bot_genetic_selection *out, qa_error *e) {
    if (out == NULL || random == NULL || random->next == NULL || (count != 0 && rank == NULL)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid bot genetic selection request");
        return false;
    }
    *out = (qa_bot_genetic_selection){.status = QA_BOT_GENETIC_TOO_MANY};
    if (count > 256)
        return true;
    float ranks[256];
    size_t valid = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!isfinite(rank[i])) {
            qa_error_set(e, QA_ERROR_ARGUMENT, i, "Genetic rank must be finite");
            return false;
        }
        ranks[i] = rank[i];
        if (ranks[i] >= 0)
            ++valid;
    }
    out->status = QA_BOT_GENETIC_TOO_FEW;
    if (valid < 3)
        return true;
    out->status = QA_BOT_GENETIC_RANDOM_ENDPOINT;
    if (!select_rank(ranks, count, random, &out->parent1))
        return true;
    ranks[out->parent1] = -1;
    if (!select_rank(ranks, count, random, &out->parent2))
        return true;
    ranks[out->parent2] = -1;
    float maximum = 0;
    for (size_t i = 0; i < count; ++i)
        if (ranks[i] > maximum)
            maximum = ranks[i];
    for (size_t i = 0; i < count; ++i)
        if (ranks[i] >= 0)
            ranks[i] = maximum - ranks[i];
    if (!select_rank(ranks, count, random, &out->child))
        return true;
    out->status = QA_BOT_GENETIC_SELECTED;
    return true;
}
