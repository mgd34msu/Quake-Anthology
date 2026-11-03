/* Port of src/network/q1/session.ts. SignonHost -> fn+ctx; returned
 * message byte blocks are malloc'd. GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q1_SESSION_H
#define QA_NETWORK_Q1_SESSION_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/network/q1/message.h"
#include "qa/network/q1/profile.h"
#include "qa/network/q1/quakeworld.h"
#include "qa/network/services/downloads.h"
void qa_write_client_string_command(qa_size_buf *s, const char *text);
typedef struct qa_netquake_seat_identity {
  const char *name; double color; const char *spawn_parameters;
  bool has_extension_flags; double extension_flags;
} qa_netquake_seat_identity;
typedef struct qa_netquake_signon { double stage; qa_netquake_seat_identity seat; } qa_netquake_signon;
void qa_netquake_signon_init(qa_netquake_signon *s, qa_netquake_seat_identity seat);
uint8_t *qa_netquake_signon_receive(qa_netquake_signon *s, double stage, size_t *out_len);
void qa_netquake_signon_first_entity(qa_netquake_signon *s);
bool qa_netquake_signon_active(const qa_netquake_signon *s);

typedef struct qa_qw_signon_host {
  void *ctx;
  void (*server_data)(void *ctx, qa_quakeworld_message *out);
  const char *const *(*models)(void *ctx, size_t *out_count);
  const char *const *(*sounds)(void *ctx, size_t *out_count);
  /* Borrowed buffers valid for the call. */
  void (*signon_buffers)(void *ctx, const uint8_t *const **out, const size_t **lens, size_t *out_count);
  bool (*accepts_map_checksum)(void *ctx, uint32_t checksum);
  uint8_t **(*spawn)(void *ctx, double start_client, size_t **lens, size_t *out_count);
  void (*begin)(void *ctx);
  void (*disconnect)(void *ctx, const char *reason);
  qa_download_source *(*open_download)(void *ctx, const char *path);
} qa_qw_signon_host;
typedef struct qa_qw_command_result {
  bool handled; char *text; uint8_t **messages; size_t *lens; size_t count;
} qa_qw_command_result;
void qa_qw_command_result_destroy(qa_qw_command_result *r);
typedef struct qa_quakeworld_signon_server qa_quakeworld_signon_server;
void qa_quakeworld_signon_server_init(qa_quakeworld_signon_server *s, qa_qw_signon_host host, bool donor_wide);
void qa_quakeworld_signon_server_destroy(qa_quakeworld_signon_server *s);
/* prepared_download NULL + has_prepared false mirrors TS undefined. */
qa_qw_command_result qa_quakeworld_signon_server_command(qa_quakeworld_signon_server *s, const char *text,
                                                        qa_download_source *prepared_download, bool has_prepared);
void qa_quakeworld_signon_server_close(qa_quakeworld_signon_server *s);
/* Heap instance for callers that cannot see the struct size; free closes it. */
qa_quakeworld_signon_server *qa_quakeworld_signon_server_create(qa_qw_signon_host host, bool donor_wide);
void qa_quakeworld_signon_server_free(qa_quakeworld_signon_server *s);

typedef struct qa_quakeworld_precache_client {
  double server_count; char **models; size_t model_count, model_cap;
  char **sounds; size_t sound_count, sound_cap;
} qa_quakeworld_precache_client;
void qa_quakeworld_precache_client_init(qa_quakeworld_precache_client *c);
void qa_quakeworld_precache_client_destroy(qa_quakeworld_precache_client *c);
/* Malloc'd command strings. */
char **qa_quakeworld_precache_client_receive(qa_quakeworld_precache_client *c, const qa_quakeworld_message *message, size_t *out_count);
char *qa_quakeworld_precache_client_sounds_ready(const qa_quakeworld_precache_client *c);
char *qa_quakeworld_precache_client_models_ready(const qa_quakeworld_precache_client *c, uint32_t checksum);
char *qa_quakeworld_precache_client_skins_ready(const qa_quakeworld_precache_client *c);
/* mode: 28, 29, or 0 = "auto". */
qa_quakeworld_profile qa_choose_quakeworld_protocol(int mode, bool needs_wide);
#endif
