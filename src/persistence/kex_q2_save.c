/* Port of src/persistence/kex-q2-save.ts. GPL-2.0-or-later. */
#include "qa/persistence/kex_q2_save.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "qa/core/binary/index.h"
#include "qa/persistence/source_bytes.h"

void qa_kex_q2_save_info_free(qa_kex_q2_save_info *info) {
  free(info->comment);
  free(info->map_command);
  free(info->game_directory);
  for (size_t i = 0; i < info->cvar_count; i++) {
    free(info->cvars[i].name);
    free(info->cvars[i].value);
  }
  free(info->cvars);
  memset(info, 0, sizeof *info);
}
void qa_kex_q2_save_archive_free(qa_kex_q2_save_archive *archive) {
  qa_kex_q2_save_info_free(&archive->info);
  for (size_t i = 0; i < archive->file_count; i++) free(archive->files[i]);
  free(archive->files);
  qa_save_value_free(archive->game);
  memset(archive, 0, sizeof *archive);
}

static char *read_length_string(qa_binary_reader *reader) {
  uint32_t length = qa_binary_reader_u32(reader);
  if (length > 65536)
    qa_save_format_error("q2-kex", "save string is implausibly long");
  uint8_t *bytes = qa_binary_reader_bytes(reader, (int64_t)length);
  if (length > 0 && bytes == NULL) return NULL;
  char *text = qa_byte_text(bytes, length);
  free(bytes);
  return text;
}
static char *dup_text(const char *text) {
  size_t length = strlen(text);
  char *out = (char *)malloc(length + 1);
  if (out != NULL) memcpy(out, text, length + 1);
  return out;
}

bool qa_decode_kex_q2_server_save(const uint8_t *bytes, size_t length,
                                  qa_kex_q2_save_info *out) {
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, (uint8_t *)bytes, length, "q2-kex/server.ssv"))
    return false;
  memset(out, 0, sizeof *out);
  char *signature = qa_read_fixed(&reader, 4);
  bool matches = signature != NULL && strcmp(signature, "EVAS") == 0;
  free(signature);
  if (!matches) {
    qa_binary_reader_destroy(&reader);
    qa_save_format_error("q2-kex", "missing EVAS save signature");
  }
  int kind = qa_binary_reader_u8(&reader);
  if (kind != 0 && kind != 1 && kind != 2) {
    qa_binary_reader_destroy(&reader);
    qa_save_format_error("q2-kex", "unknown save type");
  }
  out->comment = read_length_string(&reader);
  uint32_t low = qa_binary_reader_u32(&reader);
  uint32_t high = qa_binary_reader_u32(&reader);
  out->timestamp = ((uint64_t)high << 32) | (uint64_t)low;
  out->map_command = read_length_string(&reader);
  if (out->comment == NULL || out->map_command == NULL) goto oom;
  while (qa_binary_reader_remaining(&reader) > 0) {
    char *name = read_length_string(&reader);
    if (name == NULL) goto oom;
    if (name[0] == '\0') { free(name); break; }
    char *value = read_length_string(&reader);
    if (value == NULL) { free(name); goto oom; }
    /* TS Map.set: replace an existing name in place. */
    bool replaced = false;
    for (size_t i = 0; i < out->cvar_count; i++) {
      if (strcmp(out->cvars[i].name, name) == 0) {
        free(out->cvars[i].value);
        out->cvars[i].value = value;
        free(name);
        replaced = true;
        break;
      }
    }
    if (!replaced) {
      qa_kex_cvar *grown = (qa_kex_cvar *)realloc(
          out->cvars, (out->cvar_count + 1) * sizeof *grown);
      if (grown == NULL) { free(name); free(value); goto oom; }
      out->cvars = grown;
      out->cvars[out->cvar_count].name = name;
      out->cvars[out->cvar_count].value = value;
      out->cvar_count++;
    }
  }
  for (size_t i = 0; i < out->cvar_count; i++) {
    if (strcmp(out->cvars[i].name, "gamename") == 0) {
      free(out->game_directory);
      out->game_directory = dup_text(out->cvars[i].value);
      if (out->game_directory == NULL) goto oom;
    }
  }
  if (qa_binary_reader_remaining(&reader) != 0) {
    qa_binary_reader_destroy(&reader);
    qa_save_format_error("q2-kex", "trailing server metadata");
  }
  qa_binary_reader_destroy(&reader);
  return true;
oom:
  qa_binary_reader_destroy(&reader);
  qa_kex_q2_save_info_free(out);
  return false;
}

/* Synchronous replacement for the TS async file read + gunzipSync;
 * C has no async runtime (see docs/PORTING.md). */
static uint8_t *read_file_gunzipped(const char *path, size_t *out_length) {
  FILE *file = fopen(path, "rb");
  if (file == NULL)
    qa_save_format_error("q2-kex", "cannot decompress save archive: %s", strerror(errno));
  size_t capacity = 65536;
  size_t length = 0;
  uint8_t *compressed = (uint8_t *)malloc(capacity);
  if (compressed == NULL) { fclose(file); return NULL; }
  for (;;) {
    if (length == capacity) {
      capacity *= 2;
      uint8_t *grown = (uint8_t *)realloc(compressed, capacity);
      if (grown == NULL) { free(compressed); fclose(file); return NULL; }
      compressed = grown;
    }
    size_t read = fread(compressed + length, 1, capacity - length, file);
    length += read;
    if (read == 0) break;
  }
  fclose(file);
  z_stream stream;
  memset(&stream, 0, sizeof stream);
  if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) {
    free(compressed);
    qa_save_format_error("q2-kex", "cannot decompress save archive: %s",
                         stream.msg != NULL ? stream.msg : "zlib error");
  }
  stream.next_in = compressed;
  stream.avail_in = (uInt)length;
  size_t out_capacity = 65536;
  size_t produced = 0;
  uint8_t *out = (uint8_t *)malloc(out_capacity);
  if (out == NULL) { inflateEnd(&stream); free(compressed); return NULL; }
  for (;;) {
    if (produced == out_capacity) {
      out_capacity *= 2;
      uint8_t *grown = (uint8_t *)realloc(out, out_capacity);
      if (grown == NULL) {
        inflateEnd(&stream);
        free(compressed);
        free(out);
        return NULL;
      }
      out = grown;
    }
    stream.next_out = out + produced;
    stream.avail_out = (uInt)(out_capacity - produced);
    int status = inflate(&stream, Z_NO_FLUSH);
    produced = (size_t)stream.total_out;
    if (status == Z_STREAM_END) break;
    if (status != Z_OK && status != Z_BUF_ERROR) {
      const char *message = stream.msg != NULL ? stream.msg : "zlib error";
      inflateEnd(&stream);
      free(compressed);
      free(out);
      qa_save_format_error("q2-kex", "cannot decompress save archive: %s", message);
    }
    if (status == Z_BUF_ERROR && stream.avail_in == 0) {
      inflateEnd(&stream);
      free(compressed);
      free(out);
      qa_save_format_error("q2-kex", "cannot decompress save archive: %s",
                           "unexpected end of file");
    }
  }
  inflateEnd(&stream);
  free(compressed);
  *out_length = produced;
  return out;
}

bool qa_read_kex_q2_save_archive(const char *path, qa_kex_q2_save_archive *out) {
  size_t pak_length = 0;
  uint8_t *pak = read_file_gunzipped(path, &pak_length);
  if (pak == NULL) return false;
  memset(out, 0, sizeof *out);
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, pak, pak_length, "q2-kex/save.pak")) {
    free(pak);
    return false;
  }
  char *signature = qa_read_fixed(&reader, 4);
  bool matches = signature != NULL && strcmp(signature, "PACK") == 0;
  free(signature);
  if (!matches) {
    qa_binary_reader_destroy(&reader);
    free(pak);
    qa_save_format_error("q2-kex", "save archive is not a PAK");
  }
  uint32_t directory_offset = qa_binary_reader_u32(&reader);
  uint32_t directory_length = qa_binary_reader_u32(&reader);
  if (directory_length % 64 != 0 ||
      (uint64_t)directory_offset + (uint64_t)directory_length > (uint64_t)pak_length) {
    qa_binary_reader_destroy(&reader);
    free(pak);
    qa_save_format_error("q2-kex", "save archive directory is out of bounds");
  }
  qa_binary_reader_seek(&reader, (int64_t)directory_offset);
  size_t entry_count = directory_length / 64;
  typedef struct entry { uint32_t offset; uint32_t length; } entry;
  entry *entries = entry_count > 0 ? (entry *)malloc(entry_count * sizeof *entries) : NULL;
  out->files = entry_count > 0 ? (char **)calloc(entry_count, sizeof *out->files) : NULL;
  if (entry_count > 0 && (entries == NULL || out->files == NULL)) {
    free(entries);
    qa_binary_reader_destroy(&reader);
    free(pak);
    qa_kex_q2_save_archive_free(out);
    return false;
  }
  out->file_count = entry_count;
  for (size_t i = 0; i < entry_count; i++) {
    char *name = qa_read_fixed(&reader, 56);
    if (name == NULL) {
      free(entries);
      qa_binary_reader_destroy(&reader);
      free(pak);
      qa_kex_q2_save_archive_free(out);
      return false;
    }
    for (size_t c = 0; name[c] != '\0'; c++) {
      unsigned char ch = (unsigned char)name[c];
      if (ch >= 'A' && ch <= 'Z') name[c] = (char)(ch + 32);
      else if (ch >= 0xc0 && ch <= 0xde && ch != 0xd7) name[c] = (char)(ch + 0x20);
    }
    uint32_t offset = qa_binary_reader_u32(&reader);
    uint32_t entry_length = qa_binary_reader_u32(&reader);
    if ((uint64_t)offset + (uint64_t)entry_length > (uint64_t)pak_length) {
      qa_binary_reader_destroy(&reader);
      free(entries);
      free(pak);
      qa_save_format_error("q2-kex", "save entry %s is out of bounds", name);
    }
    out->files[i] = name;
    entries[i].offset = offset;
    entries[i].length = entry_length;
  }
  qa_binary_reader_destroy(&reader);
  const uint8_t *server = NULL;
  size_t server_length = 0;
  const uint8_t *game_bytes = NULL;
  size_t game_length = 0;
  for (size_t i = 0; i < entry_count; i++) {
    if (strcmp(out->files[i], "server.ssv") == 0) {
      server = pak + entries[i].offset;
      server_length = entries[i].length;
    }
    if (strcmp(out->files[i], "game.ssv") == 0) {
      game_bytes = pak + entries[i].offset;
      game_length = entries[i].length;
    }
  }
  free(entries);
  if (server == NULL) {
    free(pak);
    qa_kex_q2_save_archive_free(out);
    qa_save_format_error("q2-kex", "save archive has no server.ssv");
  }
  qa_save_value *game = NULL;
  if (game_bytes != NULL) {
    game = qa_parse_json_value(game_bytes, game_length, true);
    if (game == NULL) {
      free(pak);
      qa_kex_q2_save_archive_free(out);
      qa_save_format_error("q2-kex", "game.ssv is not JSON");
    }
  }
  if (!qa_decode_kex_q2_server_save(server, server_length, &out->info)) {
    qa_save_value_free(game);
    free(pak);
    qa_kex_q2_save_archive_free(out);
    return false;
  }
  out->game = game;
  free(pak);
  return true;
}
bool qa_read_kex_q2_save_info(const char *path, qa_kex_q2_save_info *out) {
  qa_kex_q2_save_archive archive;
  if (!qa_read_kex_q2_save_archive(path, &archive)) return false;
  *out = archive.info;
  memset(&archive.info, 0, sizeof archive.info);
  qa_kex_q2_save_archive_free(&archive);
  return true;
}
bool qa_kex_q2_save_restorable(const qa_kex_q2_save_archive *save) {
  for (size_t i = 0; i < save->file_count; i++)
    if (strcmp(save->files[i], "server.ssv") != 0 &&
        strcmp(save->files[i], "game.ssv") != 0)
      return false;
  const qa_save_value *game = save->game;
  if (game == NULL || game->kind != QA_SAVE_OBJECT) return false;
  const qa_save_value *clients = qa_save_value_get(game, "clients");
  if (clients == NULL) return true;
  if (clients->kind != QA_SAVE_ARRAY) return false;
  for (size_t i = 0; i < clients->item_count; i++) {
    const qa_save_value *client = clients->items[i];
    if (client->kind != QA_SAVE_OBJECT || client->entry_count != 0) return false;
  }
  return true;
}
