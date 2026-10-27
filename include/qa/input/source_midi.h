/*
 * Port of src/input/source-midi.ts: win32/win_input.c MidiInProc,
 * MIDI_NoteOn and MIDI_NoteOff over MIDI 1.0 byte framing.
 * GPL-2.0-or-later.
 */
#ifndef QA_INPUT_SOURCE_MIDI_H
#define QA_INPUT_SOURCE_MIDI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/input/support.h"

typedef struct qa_source_midi_decoder {
  int status;
  bool has_first;
  int first;
} qa_source_midi_decoder;

void qa_source_midi_decoder_init(qa_source_midi_decoder *decoder);
void qa_source_midi_decoder_reset(qa_source_midi_decoder *decoder);
/* Time is the host message time, not the MIDI driver's relative
 * timestamp. */
void qa_source_midi_decoder_feed(qa_source_midi_decoder *decoder,
                                 const uint8_t *bytes, size_t length,
                                 double channel, double time,
                                 qa_queue_key_fn queue_key, void *queue_ctx);

#endif
