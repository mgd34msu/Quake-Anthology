#ifndef QA_SETTINGS_INTERNAL_H
#define QA_SETTINGS_INTERNAL_H
#include "qa/json.h"
#include "qa/json_writer.h"
#include "qa/settings.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

bool settings_fail(qa_error *, const char *);
bool settings_object(const qa_json_document *, qa_json_id, qa_error *);
bool settings_version(const qa_json_document *, qa_json_id, qa_error *);
bool settings_string(const qa_json_document *, qa_json_id, char **, qa_error *);
bool settings_choice(const qa_json_document *, qa_json_id, const char *const *, size_t, unsigned *,
                     qa_error *);
bool settings_u32(const qa_json_document *, qa_json_id, uint32_t *, qa_error *);
bool settings_float(const qa_json_document *, qa_json_id, float *, qa_error *);
bool settings_guid(const char *, bool lowercase);
bool settings_finish(qa_json_writer *, qa_buffer *, qa_error *);
void settings_key_string(qa_json_writer *, const char *, const char *);
void settings_key_number(qa_json_writer *, const char *, double);
void settings_key_bool(qa_json_writer *, const char *, bool);
#endif
