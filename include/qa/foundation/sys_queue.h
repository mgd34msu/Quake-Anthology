/*
 * Port of src/foundation/sys-queue.ts (SysEventQueue): fixed system
 * intake queue. One timestamped tagged record for every physical input;
 * payload bytes live in the queue's load-sized arena until consumed.
 * Numeric fields are doubles, exactly the TypeScript `number` values.
 * GPL-2.0-or-later.
 */
#ifndef QA_FOUNDATION_SYS_QUEUE_H
#define QA_FOUNDATION_SYS_QUEUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/foundation/arena.h"

enum {
  QA_SYS_TIME = 0,
  QA_SYS_KEY = 1,
  QA_SYS_CHAR = 2,
  QA_SYS_MOUSE = 3,
  QA_SYS_CONTROLLER_AXIS = 4,
  QA_SYS_CONTROLLER_BUTTON = 5,
  QA_SYS_CONSOLE_LINE = 6,
  QA_SYS_PACKET = 7,
  QA_SYS_WINDOW = 8,
  QA_SYS_QUIT = 9,
  QA_SYS_DEVICE = 10,
  QA_SYS_INPUT_FRAME = 11,
  QA_SYS_USERCMD = 12,
  QA_SYS_CONSOLE_COMMAND = 13,
  QA_SYS_KIND_COUNT = 14
};

typedef int qa_sys_event_kind;

typedef enum qa_sys_admit_result {
  QA_SYS_ACCEPTED = 0,
  QA_SYS_FULL = 1,
  QA_SYS_OVERSIZE = 2
} qa_sys_admit_result;

/* One reusable intake slot. Borrowed until its own consume/reset/destroy. */
typedef struct qa_sys_event_slot {
  qa_sys_event_kind kind;
  double time_ns;
  double a;
  double b;
  double c;
  double d;
  int64_t payload_offset;
  size_t payload_length;
} qa_sys_event_slot;

typedef struct qa_sys_event_cursor {
  int64_t next;
} qa_sys_event_cursor;

typedef struct qa_sys_queue_stats {
  int64_t accepted;
  int64_t consumed;
  int64_t full_records;
  int64_t full_bytes;
  int64_t records;
  size_t bytes;
  int64_t peak_records;
  size_t peak_bytes;
} qa_sys_queue_stats;

typedef struct qa_sys_event_queue {
  qa_sys_event_slot *slots;
  size_t slot_count;
  size_t mask;
  qa_byte_arena payload;
  qa_sys_event_cursor **cursors;
  size_t cursor_count;
  size_t cursor_capacity;
  int64_t first_id;
  int64_t next_id;
  bool quit;
  int64_t accepted_count;
  int64_t consumed_count;
  int64_t full_record_count;
  int64_t full_byte_count;
  int64_t peak_record_count;
  size_t peak_byte_count;
  uint32_t oversize_by_kind[QA_SYS_KIND_COUNT];
  uint32_t deferred_by_kind[QA_SYS_KIND_COUNT];
} qa_sys_event_queue;

/* Defaults match the TypeScript: 1024 records, 1 MiB payload. */
bool qa_sys_event_queue_init(qa_sys_event_queue *queue, size_t record_capacity,
                             size_t byte_capacity);
bool qa_sys_event_queue_init_default(qa_sys_event_queue *queue);
void qa_sys_event_queue_destroy(qa_sys_event_queue *queue);

int64_t qa_sys_event_queue_live(const qa_sys_event_queue *queue);
qa_sys_queue_stats qa_sys_event_queue_stats_of(const qa_sys_event_queue *queue);
uint32_t qa_sys_event_queue_oversize(const qa_sys_event_queue *queue,
                                     qa_sys_event_kind kind);
uint32_t qa_sys_event_queue_deferred(const qa_sys_event_queue *queue,
                                     qa_sys_event_kind kind);

qa_sys_admit_result qa_sys_event_queue_admit(qa_sys_event_queue *queue,
                                             qa_sys_event_kind kind,
                                             size_t maximum_payload);
qa_sys_admit_result qa_sys_event_queue_push(qa_sys_event_queue *queue,
                                            qa_sys_event_kind kind, double time_ns,
                                            double a, double b, double c, double d,
                                            const uint8_t *payload, size_t payload_length,
                                            const uint8_t *tail, size_t tail_length);
/* Admits one frame-time record; the frontend's only frame-duration input. */
qa_sys_admit_result qa_sys_event_queue_frame(qa_sys_event_queue *queue, double time_ns);

bool qa_sys_event_queue_open_cursor(qa_sys_event_queue *queue,
                                    qa_sys_event_cursor *cursor);
void qa_sys_event_queue_close_cursor(qa_sys_event_queue *queue,
                                     qa_sys_event_cursor *cursor);
/* Returns the cursor's next borrowed slot without consuming it. */
qa_sys_event_slot *qa_sys_event_queue_read(qa_sys_event_queue *queue,
                                           qa_sys_event_cursor *cursor);
/* Payload bytes stay valid until this record's consume/reset/destroy. */
const uint8_t *qa_sys_event_queue_payload_of(qa_sys_event_queue *queue,
                                             const qa_sys_event_slot *slot,
                                             size_t *out_length);
void qa_sys_event_queue_consume(qa_sys_event_queue *queue,
                                qa_sys_event_cursor *cursor);
bool qa_sys_event_queue_pending(const qa_sys_event_queue *queue,
                                qa_sys_event_kind kind);
bool qa_sys_event_queue_quit_requested(const qa_sys_event_queue *queue);
void qa_sys_event_queue_latch_quit(qa_sys_event_queue *queue);
void qa_sys_event_queue_reset(qa_sys_event_queue *queue);

#endif
