/* Port of src/content/archive/index.ts. GPL-2.0-or-later. */
#include "qa/content/archive.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static void free_entries(qa_archive_entry *entries, size_t count) {
  if (entries == NULL) return;
  for (size_t i = 0; i < count; i++) {
    free(entries[i].path);
    free(entries[i].raw_path);
  }
  free(entries);
}

static const qa_archive_entry *select_entry(const qa_archive_handle *archive,
                                            int64_t ordinal,
                                            const qa_archive_entry *requested) {
  const qa_archive_entry *entry = NULL;
  if (ordinal >= 0 && (uint64_t)ordinal < archive->entry_count)
    entry = &archive->entries[ordinal];
  if (ordinal < -QA_MAX_SAFE_INTEGER || ordinal > QA_MAX_SAFE_INTEGER ||
      entry == NULL || (requested != NULL && requested != entry)) {
    qa_archive_error(archive->source, 0,
                     "entry %lld does not belong to this archive",
                     (long long)ordinal);
  }
  if (entry->is_directory)
    qa_archive_error(archive->source, 0, "entry %lld is a directory",
                     (long long)ordinal);
  return entry;
}

size_t qa_archive_find_entries(const qa_archive_handle *archive,
                               const char *path,
                               qa_archive_path_comparison comparison,
                               const qa_archive_entry **out, size_t capacity) {
  char *key = qa_compare_entry_path(path, comparison);
  if (key == NULL) qa_range_error("out of memory");
  size_t matches = 0;
  for (size_t i = 0; i < archive->entry_count; i++) {
    char *name = qa_compare_entry_path(archive->entries[i].path, comparison);
    if (name == NULL) { free(key); qa_range_error("out of memory"); }
    bool equal = strcmp(name, key) == 0;
    free(name);
    if (!equal) continue;
    if (out != NULL && matches < capacity) out[matches] = &archive->entries[i];
    matches++;
  }
  free(key);
  return matches;
}

static uint8_t *read_selected(qa_archive_handle *archive,
                              const qa_archive_entry *entry,
                              size_t *out_length) {
  if (entry->format == QA_ARCHIVE_FORMAT_PAK)
    return qa_archive_source_read(&archive->storage, entry->data_offset,
                                  entry->byte_length, out_length);
  size_t header_length = 0, variable_length = 0, compressed_length = 0;
  uint8_t *header_bytes = qa_archive_source_read(
      &archive->storage, entry->local_header_offset, 30, &header_length);
  if (header_bytes == NULL && header_length != 0) return NULL;
  qa_zip_local_header header = qa_read_zip_local_header(
      header_bytes, header_length, archive->source, entry);
  free(header_bytes);
  uint8_t *variable = qa_archive_source_read(
      &archive->storage, entry->local_header_offset + 30, header.byte_length,
      &variable_length);
  if (variable == NULL && variable_length != 0) return NULL;
  int64_t offset = qa_zip_data_offset(variable, variable_length,
                                      header.name_length, archive->source,
                                      archive->central_offset, entry);
  free(variable);
  uint8_t *compressed = qa_archive_source_read(
      &archive->storage, offset, entry->compressed_size, &compressed_length);
  if (compressed == NULL && compressed_length != 0) return NULL;
  uint8_t *decoded = qa_decode_zip_entry(compressed, compressed_length,
                                         archive->source, offset, entry,
                                         out_length);
  free(compressed);
  return decoded;
}

uint8_t *qa_archive_read_entry_ordinal(qa_archive_handle *archive,
                                       int64_t ordinal, size_t *out_length) {
  return read_selected(archive, select_entry(archive, ordinal, NULL),
                       out_length);
}
uint8_t *qa_archive_read_entry_entry(qa_archive_handle *archive,
                                     const qa_archive_entry *entry,
                                     size_t *out_length) {
  int64_t ordinal = entry != NULL ? entry->ordinal : -1;
  return read_selected(archive, select_entry(archive, ordinal, entry),
                       out_length);
}
uint8_t *qa_archive_read_entry_sync_ordinal(qa_archive_handle *archive,
                                            int64_t ordinal, size_t *out_length) {
  return qa_archive_read_entry_ordinal(archive, ordinal, out_length);
}
uint8_t *qa_archive_read_entry_sync_entry(qa_archive_handle *archive,
                                          const qa_archive_entry *entry,
                                          size_t *out_length) {
  return qa_archive_read_entry_entry(archive, entry, out_length);
}
uint8_t *qa_read_entry(qa_archive_handle *archive, int64_t ordinal,
                       size_t *out_length) {
  return qa_archive_read_entry_ordinal(archive, ordinal, out_length);
}

void qa_archive_close(qa_archive_handle *archive) {
  if (archive == NULL) return;
  qa_archive_source_close(&archive->storage);
}
void qa_archive_destroy(qa_archive_handle *archive) {
  if (archive == NULL) return;
  qa_archive_source_destroy(&archive->storage);
  free_entries(archive->entries, archive->entry_count);
  free(archive);
}

/* node path extname(source).toLowerCase(), approximated over bytes. */
static qa_archive_format detect_format(const uint8_t *header, size_t header_length,
                                       const char *source) {
  if (header_length >= 4 && header[0] == 80 && header[1] == 65 &&
      header[2] == 67 && header[3] == 75)
    return QA_ARCHIVE_FORMAT_PAK;
  const char *slash = strrchr(source, '/');
  const char *base = slash != NULL ? slash + 1 : source;
  const char *dot = strrchr(base, '.');
  char extension[8] = "";
  if (dot != NULL && dot != base) {
    size_t length = strlen(dot);
    if (length < sizeof extension) {
      for (size_t i = 0; i < length; i++) {
        char c = dot[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        extension[i] = c;
      }
      extension[length] = '\0';
    }
  }
  if (strcmp(extension, ".pk3") == 0) return QA_ARCHIVE_FORMAT_PK3;
  if (strcmp(extension, ".kpf") == 0) return QA_ARCHIVE_FORMAT_KPF;
  if (strcmp(extension, ".pak") == 0) return QA_ARCHIVE_FORMAT_PAK;
  return QA_ARCHIVE_FORMAT_ZIP;
}

/* openArchiveSource's catch closes the retained source before rethrowing a
 * parse error; decodeArchive's memory source is released the same way. */
static void discard_partial_archive(void *arg) {
  qa_archive_handle *archive = (qa_archive_handle *)arg;
  free_entries(archive->entries, archive->entry_count);
  qa_archive_source_destroy(&archive->storage);
  free(archive);
}

static qa_archive_handle *open_from_storage(qa_archive_source *storage,
                                            bool has_format,
                                            qa_archive_format format) {
  qa_archive_handle *archive =
      (qa_archive_handle *)calloc(1, sizeof *archive);
  if (archive == NULL) {
    qa_archive_source_destroy(storage);
    return NULL;
  }
  archive->storage = *storage;
  archive->source = archive->storage.source;
  archive->byte_length = archive->storage.byte_length;
  const char *path = archive->source;
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, discard_partial_archive, archive);

  size_t header_length = 0;
  int64_t wanted = archive->byte_length < 12 ? archive->byte_length : 12;
  uint8_t *header = qa_archive_source_read(&archive->storage, 0, wanted,
                                           &header_length);
  if (header == NULL && header_length != 0) goto oom;
  /* Each buffer is released if a parser below raises (malformed input). */
  qa_cleanup header_node;
  qa_cleanup_push(&header_node, free, header);
  qa_archive_format selected =
      has_format ? format : detect_format(header, header_length, path);
  archive->format = selected;
  if (selected == QA_ARCHIVE_FORMAT_PAK) {
    qa_pak_header directory = qa_read_pak_header(
        header, header_length, path, archive->byte_length);
    qa_cleanup_pop(&header_node);
    free(header);
    size_t directory_length = 0;
    uint8_t *directory_bytes = qa_archive_source_read(
        &archive->storage, directory.offset, directory.byte_length,
        &directory_length);
    if (directory_bytes == NULL && directory_length != 0) goto oom;
    qa_cleanup directory_node;
    qa_cleanup_push(&directory_node, free, directory_bytes);
    bool ok = qa_read_pak_directory(directory_bytes, directory_length, path,
                                    archive->byte_length, &archive->entries,
                                    &archive->entry_count);
    qa_cleanup_pop(&directory_node);
    free(directory_bytes);
    if (!ok) goto oom;
    archive->central_offset = directory.offset;
    qa_cleanup_pop(&cleanup);
    return archive;
  }
  qa_cleanup_pop(&header_node);
  free(header);
  int64_t tail_length = archive->byte_length < QA_ZIP_TAIL_LENGTH
                            ? archive->byte_length
                            : QA_ZIP_TAIL_LENGTH;
  size_t tail_read = 0, directory_read = 0;
  uint8_t *tail = qa_archive_source_read(
      &archive->storage, archive->byte_length - tail_length, tail_length,
      &tail_read);
  if (tail == NULL && tail_read != 0) goto oom;
  qa_cleanup tail_node;
  qa_cleanup_push(&tail_node, free, tail);
  qa_zip_directory directory =
      qa_read_zip_tail(tail, tail_read, path, archive->byte_length);
  qa_cleanup_pop(&tail_node);
  free(tail);
  uint8_t *directory_bytes = qa_archive_source_read(
      &archive->storage, directory.offset, directory.byte_length,
      &directory_read);
  if (directory_bytes == NULL && directory_read != 0) goto oom;
  qa_cleanup zip_directory_node;
  qa_cleanup_push(&zip_directory_node, free, directory_bytes);
  bool ok = qa_read_zip_directory(directory_bytes, directory_read, path,
                                  directory, selected, &archive->entries,
                                  &archive->entry_count);
  qa_cleanup_pop(&zip_directory_node);
  free(directory_bytes);
  if (!ok) goto oom;
  archive->central_offset = directory.offset;
  qa_cleanup_pop(&cleanup);
  return archive;
oom:
  /* Parse errors raise through the cleanup above; only OOM reaches here. */
  qa_cleanup_pop(&cleanup);
  discard_partial_archive(archive);
  return NULL;
}

qa_archive_handle *qa_decode_archive(const uint8_t *bytes, size_t length,
                                     bool has_format, qa_archive_format format,
                                     const char *source) {
  qa_archive_source storage;
  if (!qa_memory_source_init(&storage, bytes, length, source)) return NULL;
  return open_from_storage(&storage, has_format, format);
}

qa_archive_handle *qa_open_archive(const char *path, bool has_format,
                                   qa_archive_format format) {
  qa_archive_source storage;
  if (!qa_file_source_init(&storage, path)) return NULL;
  return qa_open_archive_source(&storage, has_format, format);
}

qa_archive_handle *qa_open_archive_source(qa_archive_source *storage,
                                          bool has_format,
                                          qa_archive_format format) {
  qa_archive_source owned = *storage;
  memset(storage, 0, sizeof *storage);
  storage->descriptor = -1;
  storage->closed = true;
  return open_from_storage(&owned, has_format, format);
}
