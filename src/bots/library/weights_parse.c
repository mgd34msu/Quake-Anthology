#include "internal.h"

typedef struct switch_frame {
    int32_t inventory;
    uint32_t first, last;
    bool has_default, braced;
} switch_frame;
static bool node(qa_bot_weights *weights, int32_t inventory, int32_t threshold, uint32_t *out,
                 qa_error *e) {
    bot_weight_topology *t = weights->topology;
    if (t->node_count == UINT32_MAX) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Too many fuzzy separators");
        return false;
    }
    if (!bot_grow((void **)&t->nodes, &t->node_capacity, t->node_count + 1, sizeof(*t->nodes), e) ||
        !bot_grow((void **)&weights->values, &weights->value_capacity, t->node_count + 1,
                  sizeof(*weights->values), e))
        return false;
    *out = (uint32_t)t->node_count;
    t->nodes[t->node_count] =
        (qa_bot_weight_node){inventory, threshold, QA_BOT_NO_INDEX, QA_BOT_NO_INDEX, false};
    weights->values[t->node_count++] = (qa_bot_weight_value){0};
    if (inventory > t->maximum_inventory_index)
        t->maximum_inventory_index = inventory;
    return true;
}
static bool value(qa_bot_library *library, qa_script *s, float *out, qa_error *e) {
    qa_script_token token;
    if (!bot_token(s, &token, e))
        return false;
    if (qa_script_token_is(&token, "-")) {
        bot_warning(library, s, "Negative fuzzy value uses source magnitude");
        if (!bot_token(s, &token, e))
            return false;
    }
    if (token.kind != QA_SCRIPT_NUMBER || !isfinite(token.number) || !isfinite((float)token.number))
        return bot_fail(s, "Expected finite fuzzy return value", e);
    *out = (float)token.number;
    return true;
}
static bool result(qa_bot_library *library, qa_script *s, qa_bot_weights *weights, uint32_t index,
                   qa_error *e) {
    bool balance;
    if (!qa_script_check(s, "balance", &balance, e))
        return false;
    qa_bot_weight_value *v = weights->values + index;
    weights->topology->nodes[index].balanced = balance;
    if (balance) {
        if (!qa_script_expect(s, "(", e) || !value(library, s, &v->weight, e) ||
            !qa_script_expect(s, ",", e) || !value(library, s, &v->minimum, e) ||
            !qa_script_expect(s, ",", e) || !value(library, s, &v->maximum, e) ||
            !qa_script_expect(s, ")", e))
            return false;
    } else {
        if (!value(library, s, &v->weight, e))
            return false;
        v->minimum = v->maximum = v->weight;
    }
    return qa_script_expect(s, ";", e);
}
static bool push_switch(qa_script *s, switch_frame **stack, size_t *count, size_t *capacity,
                        bool braced, qa_error *e) {
    int32_t inventory;
    if (!qa_script_expect(s, "(", e) || !bot_integer(s, &inventory, e) ||
        !qa_script_expect(s, ")", e) || !qa_script_expect(s, "{", e))
        return false;
    if (!bot_grow((void **)stack, capacity, *count + 1, sizeof(**stack), e))
        return false;
    (*stack)[(*count)++] =
        (switch_frame){inventory, QA_BOT_NO_INDEX, QA_BOT_NO_INDEX, false, braced};
    return true;
}
static bool parse_switch(qa_bot_library *library, qa_script *s, qa_bot_weights *weights,
                         uint32_t *root, qa_error *e) {
    switch_frame *stack = NULL;
    size_t count = 0, capacity = 0;
    if (!push_switch(s, &stack, &count, &capacity, false, e)) {
        free(stack);
        return false;
    }
    while (count != 0) {
        switch_frame *f = stack + count - 1;
        qa_script_token token;
        if (!bot_token(s, &token, e))
            goto fail;
        if (qa_script_token_is(&token, "}")) {
            if (f->first == QA_BOT_NO_INDEX) {
                bot_fail(s, "Empty fuzzy switch", e);
                goto fail;
            }
            if (!f->has_default) {
                bot_warning(library, s, "Fuzzy switch has no default");
                uint32_t extra;
                if (!node(weights, f->inventory, 999999, &extra, e))
                    goto fail;
                weights->topology->nodes[f->last].next = extra;
            }
            uint32_t first = f->first;
            bool braced = f->braced;
            --count;
            if (count == 0)
                *root = first;
            else
                weights->topology->nodes[stack[count - 1].last].child = first;
            if (braced && !qa_script_expect(s, "}", e))
                goto fail;
            continue;
        }
        int32_t threshold;
        if (qa_script_token_is(&token, "default")) {
            if (f->has_default) {
                bot_fail(s, "Duplicate fuzzy switch default", e);
                goto fail;
            }
            f->has_default = true;
            threshold = 999999;
        } else if (qa_script_token_is(&token, "case")) {
            if (!bot_integer(s, &threshold, e))
                goto fail;
        } else {
            bot_fail(s, "Expected fuzzy switch case/default", e);
            goto fail;
        }
        uint32_t index;
        if (!node(weights, f->inventory, threshold, &index, e) || !qa_script_expect(s, ":", e))
            goto fail;
        if (f->first == QA_BOT_NO_INDEX)
            f->first = index;
        else
            weights->topology->nodes[f->last].next = index;
        f->last = index;
        if (!bot_token(s, &token, e))
            goto fail;
        bool braced = qa_script_token_is(&token, "{");
        if (braced && !bot_token(s, &token, e))
            goto fail;
        if (qa_script_token_is(&token, "switch")) {
            if (!push_switch(s, &stack, &count, &capacity, braced, e))
                goto fail;
        } else if (qa_script_token_is(&token, "return")) {
            if (!result(library, s, weights, index, e) || (braced && !qa_script_expect(s, "}", e)))
                goto fail;
        } else {
            bot_fail(s, "Expected fuzzy return or switch", e);
            goto fail;
        }
    }
    free(stack);
    return true;
fail:
    free(stack);
    return false;
}
bool bot_weights_parse(qa_bot_library *library, const char *path, qa_bot_weights **out,
                       qa_error *e) {
    qa_script *s;
    if (!qa_script_open(path, &library->options.scripts, &library->options.preprocessor, &s, e))
        return false;
    qa_bot_weights *weights = calloc(1, sizeof(*weights));
    bot_weight_topology *topology = calloc(1, sizeof(*topology));
    if (weights == NULL || topology == NULL) {
        free(weights);
        free(topology);
        qa_script_close(s);
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating fuzzy weights");
        return false;
    }
    atomic_init(&weights->references, 1);
    atomic_init(&topology->references, 1);
    weights->topology = topology;
    topology->maximum_inventory_index = -1;
    topology->path =
        bot_string(&topology->arena, (qa_bytes){(const uint8_t *)path, strlen(path)}, e);
    if (topology->path == NULL)
        goto fail;
    for (;;) {
        qa_script_token token;
        bool found;
        if (!qa_script_next(s, &token, &found, e))
            goto fail;
        if (!found)
            break;
        if (!qa_script_token_is(&token, "weight")) {
            bot_fail(s, "Expected weight definition", e);
            goto fail;
        }
        if (topology->weight_count == 128) {
            bot_warning(library, s, "Too many fuzzy weights");
            break;
        }
        if (!bot_token(s, &token, e))
            goto fail;
        if (token.kind != QA_SCRIPT_STRING) {
            bot_fail(s, "Fuzzy weight name must be quoted", e);
            goto fail;
        }
        const char *name = bot_string(&topology->arena, qa_script_token_value(&token), e);
        if (name == NULL)
            goto fail;
        if (!bot_token(s, &token, e))
            goto fail;
        bool braced = qa_script_token_is(&token, "{");
        if (braced && !bot_token(s, &token, e))
            goto fail;
        uint32_t root;
        if (qa_script_token_is(&token, "switch")) {
            if (!parse_switch(library, s, weights, &root, e))
                goto fail;
        } else if (qa_script_token_is(&token, "return")) {
            if (!node(weights, 0, 999999, &root, e) || !result(library, s, weights, root, e))
                goto fail;
        } else {
            bot_fail(s, "Expected fuzzy return or switch", e);
            goto fail;
        }
        if (braced && !qa_script_expect(s, "}", e))
            goto fail;
        topology->weights[topology->weight_count++] =
            (qa_bot_weight_definition){name, root, (uint32_t)topology->node_count};
    }
    bot_weights_view(weights);
    qa_script_close(s);
    *out = weights;
    return true;
fail:
    qa_script_close(s);
    qa_bot_weights_release(weights);
    return false;
}
