#include "internal.h"

static bool select_rank(const float *ranks, size_t count, const qa_bot_random_source *random,
                        uint32_t *out) {
    float sum = 0;
    for (size_t i = 0; i < count; ++i)
        if (!(ranks[i] < 0))
            sum += ranks[i];
    if (sum > 0) {
        (void)bot_random(random);
        for (size_t i = 0; i < count; ++i)
            if (!(ranks[i] < 0)) {
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
static bool read_rank(const qa_bot_genetic_source *source, int32_t index, float *out, qa_error *e) {
    if (source->read)
        return source->read(source->context, index, out, e);
    *out = source->ranks[index];
    return true;
}
static bool write_rank(const qa_bot_genetic_source *source, qa_bot_genetic_target target,
                       uint32_t value, qa_error *e) {
    return !source->write || source->write(source->context, target, (int32_t)value, e);
}
static bool unavailable(const qa_bot_genetic_source *source, const char *message, qa_error *e) {
    if (source->warning)
        source->warning(source->context, message);
    return write_rank(source, QA_BOT_GENETIC_CHILD, 0, e) &&
           write_rank(source, QA_BOT_GENETIC_PARENT2, 0, e) &&
           write_rank(source, QA_BOT_GENETIC_PARENT1, 0, e);
}
bool qa_bot_genetic_select_from(int32_t count, const qa_bot_genetic_source *source,
                                const qa_bot_random_source *random, qa_bot_genetic_selection *out,
                                qa_error *e) {
    if (out == NULL || source == NULL || random == NULL || random->next == NULL ||
        (count > 0 && count <= 256 && source->read == NULL && source->ranks == NULL)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid bot genetic selection request");
        return false;
    }
    *out = (qa_bot_genetic_selection){.status = QA_BOT_GENETIC_TOO_MANY};
    if (count > 256)
        return unavailable(source, "GeneticParentsAndChildSelection: too many bots\n", e);
    float ranks[256];
    size_t valid = 0;
    for (int32_t i = 0; i < count; ++i) {
        float rank;
        if (!read_rank(source, i, &rank, e))
            return false;
        if (!(rank < 0))
            ++valid;
    }
    out->status = QA_BOT_GENETIC_TOO_FEW;
    if (valid < 3)
        return unavailable(source, "GeneticParentsAndChildSelection: too few valid bots\n", e);
    for (int32_t i = 0; i < count; ++i)
        if (!read_rank(source, i, &ranks[i], e))
            return false;
    out->status = QA_BOT_GENETIC_RANDOM_ENDPOINT;
    if (!select_rank(ranks, (size_t)count, random, &out->parent1))
        return true;
    if (!write_rank(source, QA_BOT_GENETIC_PARENT1, out->parent1, e))
        return false;
    ranks[out->parent1] = -1;
    if (!select_rank(ranks, (size_t)count, random, &out->parent2))
        return true;
    if (!write_rank(source, QA_BOT_GENETIC_PARENT2, out->parent2, e))
        return false;
    ranks[out->parent2] = -1;
    float maximum = 0;
    for (int32_t i = 0; i < count; ++i)
        if (ranks[i] > maximum)
            maximum = ranks[i];
    for (int32_t i = 0; i < count; ++i)
        if (!(ranks[i] < 0))
            ranks[i] = maximum - ranks[i];
    if (!select_rank(ranks, (size_t)count, random, &out->child))
        return true;
    if (!write_rank(source, QA_BOT_GENETIC_CHILD, out->child, e))
        return false;
    out->status = QA_BOT_GENETIC_SELECTED;
    return true;
}
bool qa_bot_genetic_select(const float *rank, size_t count, const qa_bot_random_source *random,
                           qa_bot_genetic_selection *out, qa_error *e) {
    qa_bot_genetic_source source = {.ranks = rank};
    return qa_bot_genetic_select_from(count > INT32_MAX ? INT32_MAX : (int32_t)count, &source,
                                      random, out, e);
}
