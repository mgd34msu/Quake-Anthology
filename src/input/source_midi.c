/* Port of src/input/source-midi.ts. GPL-2.0-or-later. */
#include "qa/input/source_midi.h"

#include <string.h>

#include "qa/input/key_codes.h"

void qa_source_midi_decoder_init(qa_source_midi_decoder *decoder) {
  memset(decoder, 0, sizeof *decoder);
}

void qa_source_midi_decoder_reset(qa_source_midi_decoder *decoder) {
  decoder->status = 0;
  decoder->has_first = false;
}

void qa_source_midi_decoder_feed(qa_source_midi_decoder *decoder,
                                 const uint8_t *bytes, size_t length,
                                 double channel, double time,
                                 qa_queue_key_fn queue_key, void *queue_ctx) {
  for (size_t i = 0; i < length; i++) {
    int byte = bytes[i];
    /* System realtime messages can interrupt either a message or
     * running status. */
    if (byte >= 0xf8) continue;
    if (byte >= 0x80) {
      decoder->status = byte < 0xf0 ? byte : 0;
      decoder->has_first = false;
      continue;
    }
    if (decoder->status == 0) continue;
    int command = decoder->status & 0xf0;
    /* Program change and channel pressure each have one data byte. */
    if (command == 0xc0 || command == 0xd0) continue;
    if (!decoder->has_first) {
      decoder->first = byte;
      decoder->has_first = true;
      continue;
    }
    int note = decoder->first;
    decoder->has_first = false;
    if ((double)((decoder->status & 0x0f) + 1) != channel) continue;
    if (command != 0x80 && command != 0x90) continue;
    int key = note - 60 + QA_KEY_AUX1;
    if (key < QA_KEY_AUX1 || key > 255) continue;
    /* The source has no return after velocity-zero NoteOff: retain
     * both events. */
    if (command == 0x80 || byte == 0) queue_key(queue_ctx, key, false, time);
    if (command == 0x90) queue_key(queue_ctx, key, true, time);
  }
}
