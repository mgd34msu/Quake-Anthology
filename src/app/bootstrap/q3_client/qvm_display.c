/* Port of src/app/bootstrap/q3-client/qvm-display.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/q3_client/qvm_display.h"
#include <stdlib.h>
#include <string.h>
#include "qa/compat/qvm/abi.h"
#include "qa/compat/qvm/memory.h"
#include "qa/render/q3_hardware.h"
#include "qa/foundation/js.h"
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
static int32_t word_i32(const qa_qvm_host_call *call, size_t offset) {
  int32_t v;
  memcpy(&v, call->base.words + offset, sizeof v);
  return v;
}
static void record_set_i32(qa_qvm_memory *guest, int64_t pointer, int64_t offset, int32_t value) {
  qa_qvm_writable_view view;
  qa_qvm_memory_view(guest, pointer + offset, 4, 0, &view);
  qa_qvm_writable_view_set_int32(&view, 0, value, true);
}
static void record_set_f32(qa_qvm_memory *guest, int64_t pointer, int64_t offset, double value) {
  qa_qvm_writable_view view;
  qa_qvm_memory_view(guest, pointer + offset, 4, 0, &view);
  qa_qvm_writable_view_set_float32(&view, 0, value, true); /* DataView.setFloat32 */
}
typedef struct font_destination_ctx {
  qa_app_qvm_display_options *options;
  qa_qvm_memory *guest;
  int32_t pointer;
} font_destination_ctx;
static qa_q3_font_record_view font_destination(void *ctx) {
  font_destination_ctx *d = ctx;
  d->options->assert_current(d->options->assert_ctx);
  qa_qvm_memory_span span = qa_qvm_memory_get_span(d->guest, d->pointer, 20548, 0);
  qa_q3_font_record_view view = {d->guest->bytes + span.byte_offset, (size_t)span.byte_length};
  return view;
}
static void font_print_bridge(void *ctx, const char *text) {
  qa_app_application_q3_assets *media = ctx;
  qa_app_application_q3_assets_print(media, text);
}
bool qa_app_qvm_display_syscall(const qa_qvm_host_call *call, qa_app_qvm_display_options options,
                                qa_qvm_host_result *out) {
  options.assert_current(options.assert_ctx);
  if (!is(call->kind, "engine") || (!is(call->role, "ui") && !is(call->role, "cgame"))) return false;
  bool ui = is(call->role, "ui");
  qa_qvm_memory *guest = call->guest;
  int32_t get_gl = ui ? QA_QVM_UI_IMPORT_UI_GETGLCONFIG : QA_QVM_CGAME_IMPORT_CG_GETGLCONFIG;
  if (call->code == get_gl) {
    const char *profile = call->has_abi_profile ? call->abi_profile : "q3-modern";
    bool legacy = !is(profile, "q3-modern");
    int64_t bytes = legacy ? 4164 : 11332;
    int64_t shift = legacy ? 7168 : 0;
    int32_t pointer = word_i32(call, 4);
    double width = 0, height = 0;
    options.viewport(options.viewport_ctx, &width, &height);
    qa_qvm_memory_span span = qa_qvm_memory_get_span(guest, pointer, bytes, 0);
    qa_qvm_memory_fill_bytes(guest, span.byte_offset, span.byte_length, 0);
    const qa_app_gl_config *gl = options.gl_config;
    const qa_app_gl_driver *driver = options.driver;
    qa_qvm_memory_write_string(guest, pointer, driver != NULL && driver->renderer != NULL ? driver->renderer : "Quake Anthology software renderer", 1024);
    qa_qvm_memory_write_string(guest, pointer + 1024, driver != NULL && driver->vendor != NULL ? driver->vendor : "Quake Anthology", 1024);
    qa_qvm_memory_write_string(guest, pointer + 2048, driver != NULL && driver->version != NULL ? driver->version : "software", 1024);
    record_set_i32(guest, pointer, 11264 - shift, gl != NULL ? qa_js_to_int32(gl->max_texture_size) : 0);
    record_set_i32(guest, pointer, 11268 - shift, gl != NULL ? qa_js_to_int32(gl->texture_units) : 0);
    record_set_i32(guest, pointer, 11272 - shift, gl != NULL ? qa_js_to_int32(gl->color_bits) : 24);
    record_set_i32(guest, pointer, 11276 - shift, gl != NULL ? qa_js_to_int32(gl->depth_bits) : 64);
    record_set_i32(guest, pointer, 11280 - shift, qa_js_to_int32(options.backend_stencil_bits));
    record_set_i32(guest, pointer, 11288 - shift,
                   qa_js_to_int32(qa_q3_hardware_number(qa_q3_hardware_classify(driver != NULL && driver->renderer != NULL ? driver->renderer : ""))));
    record_set_i32(guest, pointer, 11304 - shift, qa_js_to_int32(width));
    record_set_i32(guest, pointer, 11308 - shift, qa_js_to_int32(height));
    record_set_f32(guest, pointer, 11312 - shift, width / height);
    record_set_i32(guest, pointer, 11324 - shift, gl != NULL && gl->stereo_enabled ? 1 : 0);
    *out = 0;
    return true;
  }
  int32_t register_font = ui ? QA_QVM_UI_IMPORT_UI_R_REGISTERFONT : QA_QVM_CGAME_IMPORT_CG_R_REGISTERFONT;
  if (call->code == register_font) {
    int32_t name_word = word_i32(call, 4);
    int32_t size = word_i32(call, 8);
    int32_t pointer = word_i32(call, 12);
    char *name = name_word == 0 ? NULL : qa_qvm_memory_read_string(guest, name_word);
    font_destination_ctx destination = {&options, guest, pointer};
    const qa_q3_registered_font *font = qa_q3_renderer_font_registry_register_font(
        qa_app_application_q3_assets_fonts(options.media), name, size,
        font_print_bridge, options.media, font_destination, &destination);
    free(name);
    options.assert_current(options.assert_ctx);
    if (font == NULL) { *out = 0; return true; }
    for (int index = 0; index < 255; index++) {
      char *path = qa_qvm_memory_read_string(guest, pointer + index * 80 + 48);
      const qa_q3_pres_scene_shader *shader = NULL;
      qa_q3_pres_q3_renderer_resources_register_shader_no_mip(options.resources, path, &shader);
      options.assert_current(options.assert_ctx);
      double handle = qa_q3_pres_q3_renderer_resources_shader_handle(options.resources, shader);
      qa_qvm_writable_view view;
      qa_qvm_memory_view(guest, pointer + index * 80 + 44, 4, 0, &view);
      qa_qvm_writable_view_set_int32(&view, 0, qa_js_to_int32(handle), true);
      free(path);
    }
    *out = 0;
    return true;
  }
  return false;
}
