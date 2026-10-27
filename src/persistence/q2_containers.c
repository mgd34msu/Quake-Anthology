/* Port of src/persistence/q2-containers.ts. GPL-2.0-or-later. */
#include "qa/persistence/q2_containers.h"

#include <stdlib.h>
#include <string.h>

#include "qa/core/binary/index.h"
#include "qa/persistence/source_bytes.h"
#include "qa/persistence/value.h"

#define QA_SSV2 0x32565353u
#define QA_SAV2 0x32564153u

static bool push_cvar(qa_saved_cvar **cvars, size_t *count, char *name, char *value) {
  qa_saved_cvar *grown =
      (qa_saved_cvar *)realloc(*cvars, (*count + 1) * sizeof *grown);
  if (grown == NULL) return false;
  *cvars = grown;
  (*cvars)[*count].name = name;
  (*cvars)[*count].value = value;
  (*count)++;
  return true;
}
static void free_cvars(qa_saved_cvar *cvars, size_t count) {
  for (size_t i = 0; i < count; i++) {
    free(cvars[i].name);
    free(cvars[i].value);
  }
  free(cvars);
}
void qa_q2_classic_server_save_free(qa_q2_classic_server_save *save) {
  free(save->comment);
  free(save->map_command);
  free_cvars(save->cvars, save->cvar_count);
  memset(save, 0, sizeof *save);
}
void qa_q2_rerelease_server_save_free(qa_q2_rerelease_server_save *save) {
  free(save->comment);
  free(save->map_command);
  free_cvars(save->cvars, save->cvar_count);
  memset(save, 0, sizeof *save);
}
void qa_q2_level_metadata_free(qa_q2_level_metadata *save) {
  for (size_t i = 0; i < save->configstring_count; i++)
    free(save->configstrings[i].value);
  free(save->configstrings);
  free(save->portal_bytes);
  memset(save, 0, sizeof *save);
}

bool qa_decode_q2_classic_server(const uint8_t *bytes, size_t length,
                                 qa_q2_classic_server_save *out) {
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, (uint8_t *)bytes, length, "q2/server.ssv"))
    return false;
  memset(out, 0, sizeof *out);
  out->comment = qa_read_fixed(&reader, 32);
  out->map_command = qa_read_fixed(&reader, 128);
  if (out->comment == NULL || out->map_command == NULL) goto oom;
  while (qa_binary_reader_remaining(&reader) > 0) {
    char *name = qa_read_fixed(&reader, 128);
    char *value = qa_read_fixed(&reader, 128);
    if (name == NULL || value == NULL || !push_cvar(&out->cvars, &out->cvar_count, name, value)) {
      free(name);
      free(value);
      goto oom;
    }
  }
  qa_binary_reader_destroy(&reader);
  return true;
oom:
  qa_binary_reader_destroy(&reader);
  qa_q2_classic_server_save_free(out);
  return false;
}
uint8_t *qa_encode_q2_classic_server(const qa_q2_classic_server_save *save,
                                     size_t *out_length) {
  qa_binary_writer writer;
  if (!qa_binary_writer_init(&writer, (int64_t)(160 + save->cvar_count * 256)))
    return NULL;
  qa_write_fixed(&writer, save->comment, strlen(save->comment), 32);
  qa_write_fixed(&writer, save->map_command, strlen(save->map_command), 128);
  for (size_t i = 0; i < save->cvar_count; i++) {
    qa_write_fixed(&writer, save->cvars[i].name, strlen(save->cvars[i].name), 128);
    qa_write_fixed(&writer, save->cvars[i].value, strlen(save->cvars[i].value), 128);
  }
  uint8_t *result = qa_binary_writer_finish(&writer, out_length);
  qa_binary_writer_destroy(&writer);
  return result;
}
bool qa_decode_q2_classic_level_metadata(const uint8_t *bytes, size_t length,
                                         int64_t configstring_count, int64_t width,
                                         qa_q2_level_metadata *out) {
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, (uint8_t *)bytes, length, "q2/level.sv2"))
    return false;
  memset(out, 0, sizeof *out);
  for (int64_t index = 0; index < configstring_count; index++) {
    char *value = qa_read_fixed(&reader, width);
    if (value == NULL) goto oom;
    if (value[0] != '\0') {
      qa_q2_configstring *grown = (qa_q2_configstring *)realloc(
          out->configstrings, (out->configstring_count + 1) * sizeof *grown);
      if (grown == NULL) { free(value); goto oom; }
      out->configstrings = grown;
      out->configstrings[out->configstring_count].index = index;
      out->configstrings[out->configstring_count].value = value;
      out->configstring_count++;
    } else free(value);
  }
  out->portal_length = qa_binary_reader_remaining(&reader);
  out->portal_bytes = qa_binary_reader_bytes(&reader, (int64_t)out->portal_length);
  if (out->portal_length > 0 && out->portal_bytes == NULL) goto oom;
  qa_binary_reader_destroy(&reader);
  return true;
oom:
  qa_binary_reader_destroy(&reader);
  qa_q2_level_metadata_free(out);
  return false;
}
uint8_t *qa_encode_q2_classic_level_metadata(const qa_q2_level_metadata *save,
                                             int64_t configstring_count, int64_t width,
                                             size_t *out_length) {
  qa_binary_writer writer;
  if (!qa_binary_writer_init(&writer,
          configstring_count * width + (int64_t)save->portal_length))
    return NULL;
  for (int64_t index = 0; index < configstring_count; index++) {
    const char *value = "";
    for (size_t i = 0; i < save->configstring_count; i++)
      if (save->configstrings[i].index == index) value = save->configstrings[i].value;
    qa_write_fixed(&writer, value, strlen(value), width);
  }
  qa_binary_writer_bytes(&writer, save->portal_bytes, save->portal_length);
  uint8_t *result = qa_binary_writer_finish(&writer, out_length);
  qa_binary_writer_destroy(&writer);
  return result;
}
bool qa_decode_q2_rerelease_server(const uint8_t *bytes, size_t length,
                                   qa_q2_rerelease_server_save *out) {
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, (uint8_t *)bytes, length, "q2-rerelease/server.ssv"))
    return false;
  memset(out, 0, sizeof *out);
  if (qa_binary_reader_u32(&reader) != QA_SSV2 || qa_binary_reader_u32(&reader) != 1)
    qa_save_format_error("q2-rerelease", "unsupported server save version");
  uint32_t low = qa_binary_reader_u32(&reader);
  uint32_t high = qa_binary_reader_u32(&reader);
  out->timestamp = ((uint64_t)high << 32) | (uint64_t)low;
  int kind = qa_binary_reader_u8(&reader);
  if (kind != 0 && kind != 1 && kind != 2)
    qa_save_format_error("q2-rerelease", "unknown save type");
  out->kind = kind;
  out->comment = qa_read_cstring(&reader);
  out->map_command = qa_read_cstring(&reader);
  if (out->comment == NULL || out->map_command == NULL) goto oom;
  for (;;) {
    char *name = qa_read_cstring(&reader);
    if (name == NULL) goto oom;
    if (name[0] == '\0') { free(name); break; }
    char *value = qa_read_cstring(&reader);
    if (value == NULL || !push_cvar(&out->cvars, &out->cvar_count, name, value)) {
      free(name);
      free(value);
      goto oom;
    }
  }
  if (qa_binary_reader_remaining(&reader) != 0) {
    qa_binary_reader_destroy(&reader);
    qa_save_format_error("q2-rerelease", "trailing server metadata");
  }
  qa_binary_reader_destroy(&reader);
  return true;
oom:
  qa_binary_reader_destroy(&reader);
  qa_q2_rerelease_server_save_free(out);
  return false;
}
uint8_t *qa_encode_q2_rerelease_server(const qa_q2_rerelease_server_save *save,
                                       size_t *out_length) {
  size_t size = 20 + strlen(save->comment) + strlen(save->map_command);
  for (size_t i = 0; i < save->cvar_count; i++)
    size += strlen(save->cvars[i].name) + strlen(save->cvars[i].value) + 2;
  qa_binary_writer writer;
  if (!qa_binary_writer_init(&writer, (int64_t)size)) return NULL;
  qa_binary_writer_u32(&writer, (int64_t)QA_SSV2);
  qa_binary_writer_u32(&writer, 1);
  qa_binary_writer_u32(&writer, (int64_t)(uint32_t)save->timestamp);
  qa_binary_writer_u32(&writer, (int64_t)(uint32_t)(save->timestamp >> 32));
  qa_binary_writer_u8(&writer, save->kind);
  qa_write_cstring(&writer, save->comment, strlen(save->comment));
  qa_write_cstring(&writer, save->map_command, strlen(save->map_command));
  for (size_t i = 0; i < save->cvar_count; i++) {
    qa_write_cstring(&writer, save->cvars[i].name, strlen(save->cvars[i].name));
    qa_write_cstring(&writer, save->cvars[i].value, strlen(save->cvars[i].value));
  }
  qa_write_cstring(&writer, "", 0);
  uint8_t *result = qa_binary_writer_finish(&writer, out_length);
  qa_binary_writer_destroy(&writer);
  return result;
}
bool qa_decode_q2_rerelease_level_metadata(const uint8_t *bytes, size_t length,
                                           int64_t configstring_end,
                                           qa_q2_level_metadata *out) {
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, (uint8_t *)bytes, length, "q2-rerelease/level.sv2"))
    return false;
  memset(out, 0, sizeof *out);
  if (qa_binary_reader_u32(&reader) != QA_SAV2 || qa_binary_reader_u32(&reader) != 1)
    qa_save_format_error("q2-rerelease", "unsupported level save version");
  for (;;) {
    int64_t index = qa_binary_reader_u16(&reader);
    if (index == configstring_end) break;
    if (index > configstring_end)
      qa_save_format_error("q2-rerelease", "configstring outside source range");
    char *value = qa_read_cstring(&reader);
    if (value == NULL) goto oom;
    qa_q2_configstring *grown = (qa_q2_configstring *)realloc(
        out->configstrings, (out->configstring_count + 1) * sizeof *grown);
    if (grown == NULL) { free(value); goto oom; }
    out->configstrings = grown;
    out->configstrings[out->configstring_count].index = index;
    out->configstrings[out->configstring_count].value = value;
    out->configstring_count++;
  }
  out->portal_length = qa_binary_reader_u8(&reader);
  out->portal_bytes = qa_binary_reader_bytes(&reader, (int64_t)out->portal_length);
  if (out->portal_length > 0 && out->portal_bytes == NULL) goto oom;
  if (qa_binary_reader_remaining(&reader) != 0) {
    qa_binary_reader_destroy(&reader);
    qa_save_format_error("q2-rerelease", "trailing level metadata");
  }
  qa_binary_reader_destroy(&reader);
  return true;
oom:
  qa_binary_reader_destroy(&reader);
  qa_q2_level_metadata_free(out);
  return false;
}
uint8_t *qa_encode_q2_rerelease_level_metadata(const qa_q2_level_metadata *save,
                                               int64_t configstring_end,
                                               size_t *out_length) {
  size_t size = 11 + save->portal_length;
  for (size_t i = 0; i < save->configstring_count; i++)
    size += strlen(save->configstrings[i].value) + 3;
  qa_binary_writer writer;
  if (!qa_binary_writer_init(&writer, (int64_t)size)) return NULL;
  qa_binary_writer_u32(&writer, (int64_t)QA_SAV2);
  qa_binary_writer_u32(&writer, 1);
  for (size_t i = 0; i < save->configstring_count; i++) {
    if (save->configstrings[i].index < 0 ||
        save->configstrings[i].index >= configstring_end)
      qa_save_format_error("q2-rerelease", "configstring outside source range");
    qa_binary_writer_u16(&writer, save->configstrings[i].index);
    qa_write_cstring(&writer, save->configstrings[i].value,
                     strlen(save->configstrings[i].value));
  }
  qa_binary_writer_u16(&writer, configstring_end);
  qa_binary_writer_u8(&writer, (int64_t)save->portal_length);
  qa_binary_writer_bytes(&writer, save->portal_bytes, save->portal_length);
  uint8_t *result = qa_binary_writer_finish(&writer, out_length);
  qa_binary_writer_destroy(&writer);
  return result;
}
