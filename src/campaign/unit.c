#include "qa/campaign.h"
#include <stdlib.h>
#include <string.h>

struct qa_campaign_world {
    size_t references;
    qa_campaign_location location;
    qa_campaign_world_kind kind;
    qa_buffer bytes;
    qa_q2_save_level *q2;
    struct qa_campaign_world *source;
};
struct qa_campaign_unit {
    qa_strings *strings;
    qa_campaign_unit_checkpoint state;
    uint64_t revision;
};
struct qa_campaign_visit {
    qa_campaign_unit *unit;
    qa_campaign_unit_checkpoint candidate;
    qa_campaign_world *restore;
    uint64_t revision;
    bool committed;
};
static bool fail(qa_error *error, qa_status code, const char *message) {
    qa_error_set(error, code, 0, "%s", message);
    return false;
}
static bool valid_id(const qa_strings *strings, qa_string_id id) {
    qa_bytes text = qa_strings_text(strings, id);
    return text.size && !memchr(text.data, 0, text.size);
}
static bool valid_location(const qa_strings *strings, qa_campaign_location location) {
    return valid_id(strings, location.content) && valid_id(strings, location.map);
}
bool qa_campaign_location_equal(qa_campaign_location a, qa_campaign_location b) {
    return a.content == b.content && a.map == b.map;
}
bool qa_campaign_location_make(qa_strings *strings, qa_string_id content, qa_bytes map,
                               qa_campaign_location *out, qa_error *error) {
    if (!strings || !out || !valid_id(strings, content) || !map.data || !map.size ||
        memchr(map.data, 0, map.size))
        return fail(error, QA_ERROR_ARGUMENT, "Invalid campaign location");
    if (map.size >= 5 && !memcmp(map.data, "maps/", 5)) {
        map.data += 5;
        map.size -= 5;
    }
    if (map.size >= 4 && !memcmp(map.data + map.size - 4, ".bsp", 4))
        map.size -= 4;
    if (!map.size)
        return fail(error, QA_ERROR_ARGUMENT, "Empty campaign map name");
    qa_string_id name;
    if (!qa_strings_intern(strings, map, &name, error))
        return false;
    *out = (qa_campaign_location){content, name};
    return true;
}
bool qa_campaign_world_create(qa_campaign_location location, qa_bytes bytes,
                              qa_campaign_world **out, qa_error *error) {
    if (!bytes.data || !bytes.size)
        return fail(error, QA_ERROR_ARGUMENT, "Invalid departed campaign world");
    qa_buffer copy = {.data = malloc(bytes.size), .size = bytes.size};
    if (!copy.data)
        return fail(error, QA_ERROR_MEMORY, "Retaining campaign snapshot");
    memcpy(copy.data, bytes.data, bytes.size);
    bool ok = qa_campaign_world_take(location, &copy, out, error);
    qa_buffer_free(&copy);
    return ok;
}
void qa_campaign_world_retain(qa_campaign_world *world) {
    if (world)
        ++world->references;
}
bool qa_campaign_world_take(qa_campaign_location location, qa_buffer *bytes,
                            qa_campaign_world **out, qa_error *error) {
    if (!out || !location.content || !location.map || !bytes || !bytes->data ||
        !bytes->size)
        return fail(error, QA_ERROR_ARGUMENT, "Invalid departed campaign world");
    qa_campaign_world *world = calloc(1, sizeof(*world));
    if (!world)
        return fail(error, QA_ERROR_MEMORY, "Allocating campaign world");
    world->bytes = *bytes;
    *bytes = (qa_buffer){0};
    world->references = 1;
    world->location = location;
    *out = world;
    return true;
}
void qa_campaign_world_release(qa_campaign_world *world) {
    if (world && !--world->references) {
        if (world->source) qa_campaign_world_release(world->source);
        else {
            qa_buffer_free(&world->bytes);
            if (world->q2) { qa_q2_save_level_dispose(world->q2); free(world->q2); }
        }
        free(world);
    }
}
bool qa_campaign_world_relocate(const qa_campaign_world *source, qa_campaign_location location,
                                qa_campaign_world **out, qa_error *error) {
    if (!source || !location.content || !location.map || !out)
        return fail(error, QA_ERROR_ARGUMENT, "Invalid relocated campaign world");
    qa_campaign_world *world=calloc(1,sizeof(*world));
    if (!world) return fail(error,QA_ERROR_MEMORY,"Retaining relocated campaign world");
    world->references=1; world->location=location; world->bytes=source->bytes;
    world->kind=source->kind; world->q2=source->q2;
    world->source=source->source?source->source:(qa_campaign_world *)source;
    qa_campaign_world_retain(world->source); *out=world;
    return true;
}
qa_campaign_location qa_campaign_world_location(const qa_campaign_world *world) {
    return world->location;
}
qa_bytes qa_campaign_world_bytes(const qa_campaign_world *world) {
    return (qa_bytes){world->bytes.data, world->bytes.size};
}
bool qa_campaign_world_q2_take(qa_campaign_location location, qa_q2_save_level **level,
    qa_campaign_world **out, qa_error *error) {
    if (!level || !*level || !out || !location.content || !location.map ||
        !(*level)->game.data || !(*level)->game.size || !(*level)->name[0] ||
        !memchr((*level)->name, 0, sizeof((*level)->name)))
        return fail(error, QA_ERROR_ARGUMENT, "Invalid original Q2 campaign level");
    qa_campaign_world *world=calloc(1,sizeof(*world));
    if (!world) return fail(error,QA_ERROR_MEMORY,"Retaining original Q2 campaign level");
    world->references=1; world->location=location; world->kind=QA_CAMPAIGN_Q2_ORIGINAL_LEVEL;
    world->q2=*level; *level=NULL; *out=world;
    return true;
}
qa_campaign_world_kind qa_campaign_world_type(const qa_campaign_world *world) {
    return world->kind;
}
const qa_q2_save_level *qa_campaign_world_q2(const qa_campaign_world *world) {
    return world && world->kind==QA_CAMPAIGN_Q2_ORIGINAL_LEVEL?world->q2:NULL;
}
qa_campaign_unit *qa_campaign_unit_create(qa_strings *strings, qa_error *error) {
    if (!strings) {
        fail(error, QA_ERROR_ARGUMENT, "Campaign unit requires session strings");
        return NULL;
    }
    qa_campaign_unit *unit = calloc(1, sizeof(*unit));
    if (!unit) {
        fail(error, QA_ERROR_MEMORY, "Allocating campaign unit");
        return NULL;
    }
    unit->strings = strings;
    return unit;
}
void qa_campaign_unit_checkpoint_free(qa_campaign_unit_checkpoint *state) {
    if (!state)
        return;
    for (size_t i = 0; i < state->count; ++i)
        qa_campaign_world_release(state->worlds[i]);
    free(state->worlds);
    *state = (qa_campaign_unit_checkpoint){0};
}
void qa_campaign_unit_destroy(qa_campaign_unit *unit) {
    if (!unit)
        return;
    qa_campaign_unit_checkpoint_free(&unit->state);
    free(unit);
}
bool qa_campaign_unit_current(const qa_campaign_unit *unit, qa_campaign_location *out) {
    if (!unit->state.has_current)
        return false;
    *out = unit->state.current;
    return true;
}
static bool reserve(qa_campaign_unit_checkpoint *out, size_t capacity, qa_error *error) {
    if (capacity > SIZE_MAX / sizeof(*out->worlds))
        return fail(error, QA_ERROR_MEMORY, "Campaign world count overflow");
    out->worlds = capacity ? malloc(capacity * sizeof(*out->worlds)) : NULL;
    return !capacity || out->worlds ||
           fail(error, QA_ERROR_MEMORY, "Allocating campaign world handles");
}
static bool checkpoint_retain(const qa_campaign_unit_checkpoint *state,
                              qa_campaign_unit_checkpoint *out, qa_error *error) {
    qa_campaign_unit_checkpoint copy = {.has_current = state->has_current,
                                        .current = state->current};
    if (!reserve(&copy, state->count, error))
        return false;
    for (size_t i = 0; i < state->count; ++i) {
        qa_campaign_world *world = state->worlds[i];
        qa_campaign_world_retain(world);
        copy.worlds[copy.count++] = world;
    }
    *out = copy;
    return true;
}
bool qa_campaign_unit_capture(const qa_campaign_unit *unit, qa_campaign_unit_checkpoint *out,
                              qa_error *error) {
    return checkpoint_retain(&unit->state,out,error);
}
bool qa_campaign_unit_stage(qa_campaign_unit *unit, qa_campaign_location destination, bool new_unit,
                            bool load_cached_world, qa_campaign_world *departure, qa_campaign_visit **out,
                            qa_error *error) {
    if (!unit || !out || !valid_location(unit->strings, destination) ||
        (departure &&
         (!unit->state.has_current || !valid_location(unit->strings, departure->location) ||
          !qa_campaign_location_equal(unit->state.current, departure->location))))
        return fail(error, QA_ERROR_ARGUMENT, "Campaign departure does not match the active world");
    if (unit->revision == UINT64_MAX)
        return fail(error, QA_ERROR_ARGUMENT, "Campaign revision exhausted");
    qa_campaign_visit *visit = calloc(1, sizeof(*visit));
    if (!visit)
        return fail(error, QA_ERROR_MEMORY, "Allocating campaign visit");
    visit->unit = unit;
    visit->revision = unit->revision;
    visit->candidate.has_current = true;
    visit->candidate.current = destination;
    bool reset =
        new_unit || (unit->state.has_current && unit->state.current.content != destination.content);
    size_t capacity = reset ? 0 : unit->state.count + (departure ? 1u : 0u);
    if (capacity < unit->state.count && !reset) {
        free(visit);
        return fail(error, QA_ERROR_MEMORY, "Campaign visit count overflow");
    }
    if (!reserve(&visit->candidate, capacity, error)) {
        free(visit);
        return false;
    }
    if (!reset) {
        for (size_t i = 0; i < unit->state.count; ++i) {
            qa_campaign_world *world = unit->state.worlds[i];
            if (departure && qa_campaign_location_equal(world->location, departure->location))
                continue;
            if (qa_campaign_location_equal(world->location, destination)) {
                if (load_cached_world) {
                    qa_campaign_world_retain(world);
                    visit->restore = world;
                }
            } else {
                qa_campaign_world_retain(world);
                visit->candidate.worlds[visit->candidate.count++] = world;
            }
        }
        if (departure) {
            if (qa_campaign_location_equal(departure->location, destination)) {
                if (load_cached_world) {
                    qa_campaign_world_retain(departure);
                    qa_campaign_world_release(visit->restore);
                    visit->restore = departure;
                }
            } else {
                qa_campaign_world_retain(departure);
                visit->candidate.worlds[visit->candidate.count++] = departure;
            }
        }
    }
    *out = visit;
    return true;
}
const qa_campaign_world *qa_campaign_visit_restore(const qa_campaign_visit *visit) {
    return visit->restore;
}
bool qa_campaign_visit_capture(const qa_campaign_visit *visit, qa_campaign_unit_checkpoint *out,
                               qa_error *error) {
    if (!visit || visit->committed || !out)
        return fail(error,QA_ERROR_ARGUMENT,"Campaign visit is not pending");
    return checkpoint_retain(&visit->candidate,out,error);
}
bool qa_campaign_visit_commit(qa_campaign_visit *visit, qa_error *error) {
    if (!visit || visit->committed || visit->revision != visit->unit->revision)
        return fail(error, QA_ERROR_ARGUMENT, "Campaign visit was superseded or already published");
    qa_campaign_unit_checkpoint previous = visit->unit->state;
    visit->unit->state = visit->candidate;
    visit->candidate = (qa_campaign_unit_checkpoint){0};
    ++visit->unit->revision;
    visit->committed = true;
    qa_campaign_unit_checkpoint_free(&previous);
    return true;
}
void qa_campaign_visit_destroy(qa_campaign_visit *visit) {
    if (!visit)
        return;
    qa_campaign_unit_checkpoint_free(&visit->candidate);
    qa_campaign_world_release(visit->restore);
    free(visit);
}
bool qa_campaign_unit_restore(qa_campaign_unit *unit, const qa_campaign_unit_checkpoint *state,
                              qa_error *error) {
    if (!unit || !state || unit->revision == UINT64_MAX ||
        (state->has_current ? !valid_location(unit->strings, state->current) : state->count != 0) ||
        (state->count && !state->worlds))
        return fail(error, QA_ERROR_ARGUMENT, "Invalid campaign unit checkpoint");
    for (size_t i = 0; i < state->count; ++i) {
        const qa_campaign_world *world = state->worlds[i];
        if (!world || !valid_location(unit->strings, world->location) ||
            world->location.content != state->current.content ||
            qa_campaign_location_equal(world->location, state->current))
            return fail(error, QA_ERROR_FORMAT, "Invalid visited campaign world");
        for (size_t j = 0; j < i; ++j)
            if (qa_campaign_location_equal(world->location, state->worlds[j]->location))
                return fail(error, QA_ERROR_FORMAT, "Duplicate visited campaign world");
    }
    qa_campaign_unit_checkpoint candidate = {.has_current = state->has_current,
                                             .current = state->current};
    if (!reserve(&candidate, state->count, error))
        return false;
    for (size_t i = 0; i < state->count; ++i) {
        qa_campaign_world_retain(state->worlds[i]);
        candidate.worlds[candidate.count++] = state->worlds[i];
    }
    qa_campaign_unit_checkpoint_free(&unit->state);
    unit->state = candidate;
    ++unit->revision;
    return true;
}
