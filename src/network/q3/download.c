/* Port of src/network/q3/download.ts. GPL-2.0-or-later. */
#include "qa/network/q3/download.h"
#include "qa/core/common_error.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/network/q3/pure.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char *xstrdup(const char *s) { size_t n = strlen(s); char *r = malloc(n + 1); if (r) memcpy(r, s, n + 1); return r; }
struct qa_q3_server_download {
  char *name;
  bool has_file; qa_q3_download_read_file file;
  uint8_t blocks[8][2048];
  int32_t block_sizes[8];
  int64_t size, count;
  int32_t current_block, client_block, xmit_block;
  bool eof;
  double send_time;
  qa_q3_download_server_bindings bindings;
};
bool qa_q3_server_download_init(qa_q3_server_download **out, const qa_q3_download_server_bindings *bindings) {
  qa_q3_server_download *d = calloc(1, sizeof *d);
  if (!d) return false;
  d->name = xstrdup("");
  if (!d->name) { free(d); return false; }
  d->bindings = *bindings;
  *out = d; return true;
}
void qa_q3_server_download_destroy(qa_q3_server_download *d) {
  if (!d) return;
  qa_q3_server_download_close(d);
  free(d->name); free(d);
}
const char *qa_q3_server_download_name(const qa_q3_server_download *d) { return d->name; }
void qa_q3_server_download_set_name(qa_q3_server_download *d, const char *name) { free(d->name); d->name = xstrdup(name); }
const qa_q3_download_server_bindings *qa_q3_server_download_get_bindings(const qa_q3_server_download *d) { return &d->bindings; }
void qa_q3_server_download_close(qa_q3_server_download *d) {
  bool had = d->has_file;
  qa_q3_download_read_file file = d->file;
  d->has_file = false;
  free(d->name); d->name = xstrdup("");
  if (had && file.close) file.close(file.ctx);
}
void qa_q3_server_download_begin(qa_q3_server_download *d, const char *name) {
  qa_q3_server_download_close(d);
  free(d->name);
  size_t n = strlen(name); if (n > 63) n = 63;
  d->name = malloc(n + 1);
  if (d->name) { memcpy(d->name, name, n); d->name[n] = 0; }
}
void qa_q3_server_download_acknowledge(qa_q3_server_download *d, int64_t block, double time) {
  if (block != d->client_block) { d->bindings.drop(d->bindings.ctx, "broken download"); return; }
  /* TS blockSizes[clientBlock % 8]: a negative index reads undefined, never 0. */
  int index = d->client_block % 8;
  if (index >= 0 && d->block_sizes[index] == 0) { qa_q3_server_download_close(d); return; }
  d->send_time = time;
  d->client_block = (int32_t)((uint32_t)d->client_block + 1u);
}
static bool download_open(qa_q3_server_download *d, qa_q3_message_writer *writer) {
  qa_q3_product stock_product;
  bool has_stock = qa_q3_stock_package(d->name, &stock_product);
  bool enabled = d->bindings.enabled(d->bindings.ctx);
  if (enabled && !has_stock) {
    qa_q3_check_download_name(d->name);
    d->has_file = d->bindings.open(d->bindings.ctx, d->name, &d->file);
    d->size = d->has_file ? d->file.size : -1;
    if (d->size > 0 && d->size <= 0x7fffffff) {
      d->current_block = 0; d->client_block = 0; d->xmit_block = 0; d->count = 0; d->eof = false;
      return true;
    }
  }
  char error[1200];
  if (has_stock) {
    if (stock_product == QA_Q3_PRODUCT_MISSIONPACK)
      snprintf(error, sizeof error, "Cannot autodownload Team Arena file \"%s\"\nThe Team Arena mission pack can be found in your local game store.", d->name);
    else snprintf(error, sizeof error, "Cannot autodownload id pk3 file \"%s\"", d->name);
  } else if (!enabled) {
    snprintf(error, sizeof error, "Could not download \"%s\" because autodownloading is disabled on the server.\n\n%s", d->name,
      d->bindings.pure(d->bindings.ctx)
        ? "You will need to get this file elsewhere before you can connect to this pure server.\n"
        : "The server you are connecting to is not a pure server, set autodownload to No in your settings and you might be able to join the game anyway.\n");
  } else {
    snprintf(error, sizeof error, "File \"%s\" not found on server for autodownloading.\n", d->name);
  }
  if (strlen(error) > 1023) error[1023] = 0;
  qa_q3_message_writer_write_byte(writer, QA_Q3_SVC_DOWNLOAD);
  qa_q3_message_writer_write_short(writer, 0);
  qa_q3_message_writer_write_long(writer, -1);
  qa_q3_message_writer_write_string(writer, error);
  qa_q3_server_download_close(d);
  return false;
}
void qa_q3_server_download_write(qa_q3_server_download *d, qa_q3_message_writer *writer, double time, const qa_q3_download_rate *settings) {
  if (d->name[0] == 0 || (!d->has_file && !download_open(d, writer))) return;
  if (!d->has_file) qa_raise("Error", 0, 0, "Opened Q3 download has no file");
  while ((int64_t)d->current_block - d->client_block < 8 && d->size != d->count) {
    int index = d->current_block % 8;
    if (index < 0) qa_range_error("Missing download block storage");
    int64_t bytes = d->file.read(d->file.ctx, d->blocks[index], 2048);
    if (bytes <= 0 || bytes > 2048 || d->count + bytes > d->size) {
      qa_q3_server_download_close(d);
      qa_raise("Error", 0, 0, "Source download file changed or returned an invalid read");
    }
    d->block_sizes[index] = (int32_t)bytes;
    d->count = (int32_t)(d->count + bytes);
    d->current_block = (int32_t)((uint32_t)d->current_block + 1u);
  }
  if (d->count == d->size && !d->eof && (int64_t)d->current_block - d->client_block < 8) {
    d->block_sizes[d->current_block % 8] = 0;
    d->current_block = (int32_t)((uint32_t)d->current_block + 1u);
    d->eof = true;
  }
  double rate = settings->rate;
  if (settings->max_rate != 0) {
    double cap = settings->max_rate < 1000 ? 1000 : settings->max_rate;
    rate = rate < cap ? rate : cap;
  }
  int64_t blocks;
  if (rate == 0) blocks = 1;
  else {
    /* Math.trunc(((Math.trunc(Math.imul(rate, snapshotMsec)/1000)+2048)|0)/2048) */
    int32_t imul = (int32_t)((uint32_t)qa_int32(rate) * (uint32_t)qa_int32(settings->snapshot_msec));
    int32_t inner = (int32_t)((uint32_t)(int32_t)trunc(imul / 1000.0) + 2048u);
    blocks = (int64_t)trunc(inner / 2048.0);
  }
  if (blocks < 0) blocks = 1;
  while (blocks-- > 0) {
    if (d->client_block == d->current_block) return;
    if (d->xmit_block == d->current_block) {
      if (qa_int32(time - d->send_time) > 1000) d->xmit_block = d->client_block;
      else return;
    }
    int index = d->xmit_block % 8;
    if (index < 0) qa_range_error("Missing download window slot");
    int32_t size = d->block_sizes[index];
    qa_q3_message_writer_write_byte(writer, QA_Q3_SVC_DOWNLOAD);
    qa_q3_message_writer_write_short(writer, d->xmit_block);
    if (d->xmit_block == 0) qa_q3_message_writer_write_long(writer, d->size);
    qa_q3_message_writer_write_short(writer, size);
    if (size != 0) qa_q3_message_writer_write_data(writer, d->blocks[index], (size_t)size);
    d->xmit_block = (int32_t)((uint32_t)d->xmit_block + 1u);
    d->send_time = time;
  }
}
struct qa_q3_client_download {
  bool has_file; qa_q3_download_write_file file;
  char *name, *temporary;
  int32_t block;
  int64_t count, size;
  qa_q3_download_client_bindings bindings;
};
bool qa_q3_client_download_init(qa_q3_client_download **out, const qa_q3_download_client_bindings *bindings) {
  qa_q3_client_download *d = calloc(1, sizeof *d);
  if (!d) return false;
  d->name = xstrdup(""); d->temporary = xstrdup("");
  if (!d->name || !d->temporary) { free(d->name); free(d->temporary); free(d); return false; }
  d->bindings = *bindings;
  *out = d; return true;
}
void qa_q3_client_download_destroy(qa_q3_client_download *d) {
  if (!d) return;
  qa_q3_client_download_close(d);
  free(d->name); free(d->temporary); free(d);
}
const qa_q3_download_client_bindings *qa_q3_client_download_get_bindings(const qa_q3_client_download *d) { return &d->bindings; }
void qa_q3_client_download_close(qa_q3_client_download *d) {
  bool had = d->has_file;
  qa_q3_download_write_file file = d->file;
  d->has_file = false;
  free(d->name); d->name = xstrdup("");
  free(d->temporary); d->temporary = xstrdup("");
  if (had && file.close) file.close(file.ctx);
}
void qa_q3_client_download_begin(qa_q3_client_download *d, const char *remote, const char *local) {
  qa_q3_download_client_bindings *b = &d->bindings;
  b->assert_current(b->ctx);
  qa_q3_check_download_name(remote);
  qa_q3_check_download_name(local);
  qa_q3_client_download_close(d);
  free(d->name); d->name = xstrdup(local);
  size_t n = strlen(local) + 5;
  free(d->temporary); d->temporary = malloc(n);
  if (d->temporary) snprintf(d->temporary, n, "%s.tmp", local);
  d->block = 0; d->count = 0; d->size = 0;
  b->progress(b->ctx, remote, 0, 0);
  char *cmd = malloc(strlen(remote) + 16);
  if (cmd) { snprintf(cmd, strlen(remote) + 16, "download %s", remote); b->reliable(b->ctx, cmd); free(cmd); }
}
int64_t qa_q3_client_download_publish_size(qa_q3_client_download *d, int64_t size) {
  d->size = size;
  d->bindings.progress(d->bindings.ctx, d->name, d->count, size);
  return d->size;
}
void qa_q3_client_download_receive(qa_q3_client_download *d, const qa_q3_download_block *download) {
  qa_q3_download_client_bindings *b = &d->bindings;
  b->assert_current(b->ctx);
  if (download->kind == QA_Q3_DOWNLOAD_ERROR)
    qa_common_error(QA_COMMON_ERROR_DROP, download->message ? download->message : "");
  int64_t block = download->kind == QA_Q3_DOWNLOAD_START ? 0 : download->number;
  if (block != d->block) return;
  if (!d->has_file) {
    if (d->temporary[0] == 0) { b->reliable(b->ctx, "stopdl"); return; }
    d->has_file = b->open_temporary(b->ctx, d->temporary, &d->file);
    b->assert_current(b->ctx);
    if (!d->has_file) { b->reliable(b->ctx, "stopdl"); b->completed(b->ctx); return; }
  }
  if (download->data_len != 0) {
    d->file.write_bytes(d->file.ctx, download->data, download->data_len);
    b->assert_current(b->ctx);
  }
  char cmd[32]; snprintf(cmd, sizeof cmd, "nextdl %d", d->block);
  b->reliable(b->ctx, cmd);
  d->block = (int32_t)((uint32_t)d->block + 1u);
  d->count = (int32_t)(d->count + (int64_t)download->data_len);
  b->progress(b->ctx, d->name, d->count, d->size);
  if (download->data_len == 0) {
    qa_q3_download_write_file file = d->file;
    d->has_file = false;
    if (file.close) file.close(file.ctx);
    b->assert_current(b->ctx);
    b->publish_temporary(b->ctx, d->temporary, d->name);
    b->assert_current(b->ctx);
    free(d->name); d->name = xstrdup("");
    free(d->temporary); d->temporary = xstrdup("");
    b->progress(b->ctx, "", d->count, d->size);
    b->send_packet(b->ctx); b->assert_current(b->ctx);
    b->send_packet(b->ctx); b->assert_current(b->ctx);
    b->completed(b->ctx);
  }
}
