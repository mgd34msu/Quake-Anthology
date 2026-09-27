#include "qa/campaign.h"
#include <stdlib.h>
#include <string.h>

static bool invalid(qa_error *error, const char *message) {
    qa_error_set(error, QA_ERROR_FORMAT, 0, "%s", message);
    return false;
}
static bool word(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '-';
}
static bool classify(const char *name, qa_travel_kind *kind) {
    if (!*name || *name == '/' || strstr(name, "//"))
        return false;
    const char *at = name;
    while (*at && (word((unsigned char)*at) || *at == '/'))
        ++at;
    if (at == name)
        return false;
    if (!*at)
        *kind = QA_TRAVEL_MAP;
    else if (!strcmp(at, ".cin"))
        *kind = QA_TRAVEL_CINEMATIC;
    else if (!strcmp(at, ".pcx"))
        *kind = QA_TRAVEL_PICTURE;
    else if (!strcmp(at, ".dm2"))
        *kind = QA_TRAVEL_DEMO;
    else
        return false;
    return true;
}
void qa_travel_route_free(qa_travel_route *route) {
    if (!route)
        return;
    free(route->targets);
    free(route->storage);
    *route = (qa_travel_route){0};
}
bool qa_q2_travel_parse(const char *expression, qa_travel_route *out, qa_error *error) {
    if (!expression || !out || !*expression)
        return invalid(error, "Q2 travel has no destination");
    size_t length = strlen(expression), count = 1;
    for (size_t i = 0; i < length; ++i)
        if (expression[i] == '+')
            ++count;
    if (count > SIZE_MAX / sizeof(qa_travel_target))
        return invalid(error, "Q2 travel chain overflow");
    qa_travel_route route = {.count = count};
    route.storage = malloc(length + 1);
    route.targets = calloc(count, sizeof(*route.targets));
    if (!route.storage || !route.targets) {
        qa_travel_route_free(&route);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q2 travel route");
        return false;
    }
    memcpy(route.storage, expression, length + 1);
    char *part = route.storage;
    for (size_t i = 0; i < count; ++i) {
        char *next = strchr(part, '+');
        if (next)
            *next++ = 0;
        if (!*part) {
            invalid(error, "Q2 travel has an empty destination");
            goto fail;
        }
        char *spawn = strchr(part, '$');
        if (spawn)
            *spawn++ = 0;
        qa_travel_target *target = &route.targets[i];
        target->new_unit = *part == '*';
        target->name = part + (target->new_unit ? 1 : 0);
        target->spawn_point = spawn ? spawn : "";
        if (!classify(target->name, &target->kind)) {
            invalid(error, "Invalid Q2 travel destination");
            goto fail;
        }
        for (const unsigned char *at = (const unsigned char *)target->spawn_point; *at; ++at)
            if (!word(*at)) {
                invalid(error, "Invalid Q2 travel spawn point");
                goto fail;
            }
        part = next;
    }
    *out = route;
    return true;
fail:
    qa_travel_route_free(&route);
    return false;
}
bool qa_q2_nextserver(const qa_travel_route *route, size_t current, qa_buffer *out,
                      qa_error *error) {
    if (!route || current >= route->count || !out)
        return invalid(error, "Invalid next-server route");
    size_t size = current + 1 == route->count ? 0 : 10;
    for (size_t i = current + 1; i < route->count; ++i) {
        const qa_travel_target *target = &route->targets[i];
        qa_travel_kind kind;
        if (!target->name || !target->spawn_point || !classify(target->name, &kind) ||
            kind != target->kind)
            return invalid(error, "Invalid next-server target");
        for (const unsigned char *at = (const unsigned char *)target->spawn_point; *at; ++at)
            if (!word(*at))
                return invalid(error, "Invalid next-server spawn point");
        size_t name = strlen(target->name), spawn = strlen(target->spawn_point);
        if (size > SIZE_MAX - 5 || name > SIZE_MAX - size - 5 || spawn > SIZE_MAX - size - name - 5)
            return invalid(error, "Next-server text overflow");
        size += name + spawn + (target->new_unit ? 1u : 0u) + (spawn ? 1u : 0u) +
                (i > current + 1 ? 1u : 0u);
    }
    char *text = malloc(size + 1);
    if (!text) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating next-server command");
        return false;
    }
    size_t at = 0;
    if (size) {
        memcpy(text, "gamemap \"", 9);
        at = 9;
        for (size_t i = current + 1; i < route->count; ++i) {
            const qa_travel_target *target = &route->targets[i];
            if (i > current + 1)
                text[at++] = '+';
            if (target->new_unit)
                text[at++] = '*';
            size_t count = strlen(target->name);
            memcpy(text + at, target->name, count);
            at += count;
            if (*target->spawn_point) {
                text[at++] = '$';
                count = strlen(target->spawn_point);
                memcpy(text + at, target->spawn_point, count);
                at += count;
            }
        }
        text[at++] = '"';
    }
    text[at] = 0;
    *out = (qa_buffer){(uint8_t *)text, at};
    return true;
}
