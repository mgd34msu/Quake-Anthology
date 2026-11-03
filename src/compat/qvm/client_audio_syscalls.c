/* Port of src/compat/qvm/client-audio-syscalls.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/client_audio_syscalls.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/abi.h"
#include "qa/core/common_error.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

static qa_vec3 read_vector(const qa_qvm_host_call *call, int32_t word) {
  qa_qvm_writable_view view;
  qa_qvm_memory_view(call->guest, word, 12, 0, &view);
  qa_vec3 v = {qa_qvm_writable_view_get_float32(&view, 0, true),
               qa_qvm_writable_view_get_float32(&view, 4, true),
               qa_qvm_writable_view_get_float32(&view, 8, true)};
  return v;
}

/* TS pcm(index): false is TS undefined (invalid handle); *out NULL is a
 * null PcmSound. */
static bool pcm(const qa_qvm_client_audio_services *services, double index, qa_pcm_sound **out) {
  bool has = false;
  qa_q3_pres_presentation_sound_bank_sound_for_index(services->sound.bank, index, out, &has);
  /* snd_dma.c supplies only S_COLOR_YELLOW as the printf format on invalid handles. */
  if (!has) services->print(services->ctx, "^3");
  return has;
}

static void out_of_memory(bool ok) {
  if (!ok) qa_range_error("out of memory");
}

static bool modern(const qa_qvm_host_call *call) {
  return !call->has_abi_profile || strcmp(call->abi_profile, "q3-modern") == 0;
}

bool qa_qvm_client_audio_syscall(const qa_qvm_host_call *call,
                                 qa_qvm_client_audio_services services, qa_qvm_host_result *out) {
  if (strcmp(call->kind, "engine") != 0 || strcmp(call->role, services.role) != 0) return false;
  const qa_q3_pres_client_sound *sound = &services.sound;
  bool ui = strcmp(services.role, "ui") == 0;
  if (call->code == (ui ? QA_QVM_UI_IMPORT_UI_S_REGISTERSOUND
                        : QA_QVM_CGAME_IMPORT_CG_S_REGISTERSOUND)) {
    int32_t name = qa_qvm_host_call_word(call, 1);
    bool compressed = modern(call) && qa_qvm_host_call_word(call, 2) != 0;
    char *path = NULL;
    if (name != 0) {
      path = qa_qvm_memory_read_string(call->guest, name);
      if (path == NULL) qa_range_error("out of memory");
    }
    qa_pcm_sound *value = NULL;
    out_of_memory(qa_q3_pres_presentation_sound_bank_register_sound(sound->bank, path, compressed, &value));
    free(path);
    *out = qa_int32(qa_q3_pres_presentation_sound_bank_index_for_sound(sound->bank, value));
    return true;
  }
  if (call->code == (ui ? QA_QVM_UI_IMPORT_UI_S_STARTLOCALSOUND
                        : QA_QVM_CGAME_IMPORT_CG_S_STARTLOCALSOUND)) {
    qa_pcm_sound *value = NULL;
    if (pcm(&services, qa_qvm_host_call_word(call, 1), &value))
      sound->start_local_sound(sound->ctx, value, qa_qvm_host_call_word(call, 2));
    *out = 0;
    return true;
  }
  if (call->code == (ui ? QA_QVM_UI_IMPORT_UI_S_STARTBACKGROUNDTRACK
                        : QA_QVM_CGAME_IMPORT_CG_S_STARTBACKGROUNDTRACK)) {
    int32_t intro_word = qa_qvm_host_call_word(call, 1);
    int32_t loop_word = qa_qvm_host_call_word(call, 2);
    char *intro = intro_word == 0 ? NULL : qa_qvm_memory_read_string(call->guest, intro_word);
    char *loop = loop_word == 0 ? NULL : qa_qvm_memory_read_string(call->guest, loop_word);
    if ((intro_word != 0 && intro == NULL) || (loop_word != 0 && loop == NULL))
      qa_range_error("out of memory");
    out_of_memory(sound->start_background_track(sound->ctx, intro == NULL ? "" : intro, loop == NULL ? "" : loop));
    free(intro);
    free(loop);
    *out = 0;
    return true;
  }
  if (call->code == (ui ? QA_QVM_UI_IMPORT_UI_S_STOPBACKGROUNDTRACK
                        : QA_QVM_CGAME_IMPORT_CG_S_STOPBACKGROUNDTRACK)) {
    out_of_memory(sound->start_background_track(sound->ctx, "", ""));
    *out = 0;
    return true;
  }
  if (strcmp(call->role, "cgame") != 0) return false;
  switch (call->code) {
    case QA_QVM_CGAME_IMPORT_CG_S_STARTSOUND: {
      int32_t origin = qa_qvm_host_call_word(call, 1);
      int32_t entity = qa_qvm_host_call_word(call, 2);
      int32_t channel = qa_qvm_host_call_word(call, 3);
      if (origin == 0 && (entity < 0 || entity > 1024)) {
        char message[96];
        snprintf(message, sizeof message, "S_StartSound: bad entitynum %d", (int)entity);
        qa_common_error(QA_COMMON_ERROR_DROP, message);
      }
      qa_pcm_sound *value = NULL;
      if (pcm(&services, qa_qvm_host_call_word(call, 4), &value)) {
        qa_vec3 origin_vector = {0, 0, 0};
        if (origin != 0) origin_vector = read_vector(call, origin);
        sound->start_sound(sound->ctx, origin == 0 ? NULL : &origin_vector, entity, channel, value);
      }
      *out = 0;
      return true;
    }
    case QA_QVM_CGAME_IMPORT_CG_S_CLEARLOOPINGSOUNDS:
      sound->clear_looping_sounds(sound->ctx, !modern(call) || qa_qvm_host_call_word(call, 1) != 0);
      *out = 0;
      return true;
    case QA_QVM_CGAME_IMPORT_CG_S_ADDLOOPINGSOUND:
    case QA_QVM_CGAME_IMPORT_CG_S_ADDREALLOOPINGSOUND: {
      qa_pcm_sound *value = NULL;
      if (pcm(&services, qa_qvm_host_call_word(call, 4), &value)) {
        qa_vec3 origin = read_vector(call, qa_qvm_host_call_word(call, 2));
        qa_vec3 velocity = read_vector(call, qa_qvm_host_call_word(call, 3));
        sound->add_loop_sound(sound->ctx, qa_qvm_host_call_word(call, 1), origin, velocity, value,
                              call->code == QA_QVM_CGAME_IMPORT_CG_S_ADDREALLOOPINGSOUND);
      }
      *out = 0;
      return true;
    }
    case QA_QVM_CGAME_IMPORT_CG_S_UPDATEENTITYPOSITION:
      sound->update_sound_position(sound->ctx, qa_qvm_host_call_word(call, 1),
                                   read_vector(call, qa_qvm_host_call_word(call, 2)));
      *out = 0;
      return true;
    case QA_QVM_CGAME_IMPORT_CG_S_STOPLOOPINGSOUND:
      sound->stop_looping_sound(sound->ctx, qa_qvm_host_call_word(call, 1));
      *out = 0;
      return true;
    case QA_QVM_CGAME_IMPORT_CG_S_RESPATIALIZE: {
      int32_t entity = qa_qvm_host_call_word(call, 1);
      qa_vec3 origin = read_vector(call, qa_qvm_host_call_word(call, 2));
      qa_qvm_writable_view axes;
      qa_qvm_memory_view(call->guest, qa_qvm_host_call_word(call, 3), 36, 0, &axes);
      qa_axis axis;
      for (int vector = 0; vector < 3; vector++) {
        axis.vectors[vector].x = qa_qvm_writable_view_get_float32(&axes, vector * 12, true);
        axis.vectors[vector].y = qa_qvm_writable_view_get_float32(&axes, vector * 12 + 4, true);
        axis.vectors[vector].z = qa_qvm_writable_view_get_float32(&axes, vector * 12 + 8, true);
      }
      (void)qa_qvm_host_call_word(call, 4);
      sound->set_listener(sound->ctx, entity, origin, axis);
      *out = 0;
      return true;
    }
    default: return false;
  }
}
