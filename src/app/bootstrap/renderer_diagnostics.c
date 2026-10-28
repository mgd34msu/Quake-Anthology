/* Port of src/app/bootstrap/renderer-diagnostics.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/renderer_diagnostics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/input/support.h"
#include "qa/materials/shader_types.h"
typedef struct handler_ctx handler_ctx;
struct qa_app_renderer_diagnostics_registration {
  qa_app_renderer_diagnostic_services services;
  const char *names[6];
  size_t count;
  handler_ctx *ctxs;
};
struct handler_ctx { qa_app_renderer_diagnostics_registration *reg; int which; };
static char *xdup(const char *s) { char *r = qa_input_dup(s); if (!r) qa_range_error("Out of memory"); return r; }
static void append_str(char **buf, const char *add) {
  size_t a = strlen(*buf), b = strlen(add);
  char *n = realloc(*buf, a + b + 1);
  if (!n) qa_range_error("Out of memory");
  memcpy(n + a, add, b + 1); *buf = n;
}
static char *num(double v) { char *r = qa_input_js_number(v); if (!r) qa_range_error("Out of memory"); return r; }
static void append_num(char **buf, double v) { char *n = num(v); append_str(buf, n); free(n); }
enum { DIAG_IMAGE_LIST, DIAG_SHADER_LIST, DIAG_MODEL_LIST, DIAG_SKIN_LIST, DIAG_MODE_LIST, DIAG_GFX_INFO };
static const char *decoded_kind(qa_decoded_model_kind kind) {
  switch (kind) {
    case QA_DECODED_MODEL_Q1_ALIAS: return "q1-mdl";
    case QA_DECODED_MODEL_Q2_ALIAS: return "q2-md2";
    case QA_DECODED_MODEL_Q1_SPRITE: return "q1-spr";
    case QA_DECODED_MODEL_Q2_SPRITE: return "q2-sp2";
    case QA_DECODED_MODEL_Q3_MESH: return "q3-md3";
    case QA_DECODED_MODEL_MD4: return "q3-md4";
    case QA_DECODED_MODEL_MD5: return "md5";
    case QA_DECODED_MODEL_BRUSH_MODEL: return "brush-model";
  }
  return "";
}
static void diagnostic_handler(void *ctx, qa_command_invocation *invocation) {
  handler_ctx *h = ctx;
  qa_app_renderer_diagnostic_services *services = &h->reg->services;
  /* TS invocation.args excludes the command name (landed argv excludes it too). */
  size_t arg_count = invocation->argc;
  char *text = xdup("");
  char line[4096];
  switch (h->which) {
    case DIAG_IMAGE_LIST: {
      qa_app_renderer_diagnostics d = services->renderer(services->ctx);
      append_str(&text, "ordinal width height encoding mipLevels name\n");
      for (size_t i = 0; i < d.image_count; i++) {
        if (i) append_str(&text, "\n");
        append_num(&text, d.images[i].ordinal); append_str(&text, " ");
        append_num(&text, d.images[i].width); append_str(&text, " ");
        append_num(&text, d.images[i].height); append_str(&text, " ");
        append_str(&text, d.images[i].encoding); append_str(&text, " ");
        append_num(&text, d.images[i].mip_levels); append_str(&text, " ");
        append_str(&text, d.images[i].name);
      }
      snprintf(line, sizeof line, "\n%zu resident images\n", d.image_count);
      append_str(&text, line);
      services->print(services->ctx, text, invocation->source);
      qa_app_renderer_diagnostics_free(&d);
      break;
    }
    case DIAG_SHADER_LIST: {
      size_t count = 0;
      const qa_registered_scene_material *shaders = services->shaders(services->ctx, arg_count != 0, &count);
      append_str(&text, "passes lightmap iterator sort name\n");
      for (size_t i = 0; i < count; i++) {
        const qa_mat_finished_shader *finished = qa_registered_scene_material_finished(shaders[i]);
        const qa_render_material *material = qa_registered_scene_material_material(shaders[i]);
        if (i) append_str(&text, "\n");
        append_num(&text, finished->num_unfogged_passes); append_str(&text, " ");
        append_num(&text, finished->lightmap_index); append_str(&text, " ");
        append_str(&text, finished->iterator.kind); append_str(&text, " ");
        append_num(&text, finished->sort); append_str(&text, " ");
        append_str(&text, material->name);
      }
      snprintf(line, sizeof line, "\n%zu registered shaders\n", count);
      append_str(&text, line);
      services->print(services->ctx, text, invocation->source);
      break;
    }
    case DIAG_MODEL_LIST: {
      qa_q3_pres_q3_renderer_resources *resources = services->resources(services->ctx, invocation->source);
      if (resources == NULL) qa_range_error("No active source client model registry");
      qa_q3_pres_registered_model *models = NULL; size_t count = 0;
      if (!qa_q3_pres_q3_renderer_resources_registered_models(resources, &models, &count)) qa_range_error("Out of memory");
      append_str(&text, "handle kind name\n");
      for (size_t i = 0; i < count; i++) {
        if (i) append_str(&text, "\n");
        append_num(&text, models[i].handle); append_str(&text, " ");
        const char *kind = models[i].model.kind == QA_Q3_PRES_SCENE_MODEL_LOADED
                               ? decoded_kind(models[i].model.model.kind)
                               : models[i].model.kind == QA_Q3_PRES_SCENE_MODEL_INLINE ? "inline" : "default";
        append_str(&text, kind); append_str(&text, " ");
        append_str(&text, models[i].path);
      }
      snprintf(line, sizeof line, "\n%zu registered models\n", count);
      append_str(&text, line);
      free(models);
      services->print(services->ctx, text, invocation->source);
      break;
    }
    case DIAG_SKIN_LIST: {
      qa_q3_pres_q3_renderer_resources *resources = services->resources(services->ctx, invocation->source);
      if (resources == NULL) qa_range_error("No active source client skin registry");
      qa_q3_pres_registered_skin *skins = NULL; size_t count = 0;
      if (!qa_q3_pres_q3_renderer_resources_registered_skins(resources, &skins, &count)) qa_range_error("Out of memory");
      for (size_t i = 0; i < count; i++) {
        if (i) append_str(&text, "\n");
        append_num(&text, skins[i].handle); append_str(&text, " ");
        append_str(&text, skins[i].path); append_str(&text, "\n");
        for (size_t j = 0; j < skins[i].skin->surface_count; j++) {
          if (j) append_str(&text, "\n");
          append_str(&text, "  "); append_str(&text, skins[i].skin->surfaces[j].name);
          append_str(&text, " = "); append_str(&text, skins[i].skin->surfaces[j].shader);
        }
      }
      snprintf(line, sizeof line, "\n%zu registered skins\n", count);
      append_str(&text, line);
      free(skins);
      services->print(services->ctx, text, invocation->source);
      break;
    }
    case DIAG_MODE_LIST: {
      qa_app_renderer_diagnostics d = services->renderer(services->ctx);
      for (size_t i = 0; i < d.display_mode_count; i++) {
        if (i) append_str(&text, "\n");
        snprintf(line, sizeof line, "%zu: %dx%d %d bit %d Hz", i, d.display_modes[i].width, d.display_modes[i].height,
                 d.display_modes[i].color_bits, d.display_modes[i].refresh_rate);
        append_str(&text, line);
      }
      snprintf(line, sizeof line, "\n%zu display modes\n", d.display_mode_count);
      append_str(&text, line);
      services->print(services->ctx, text, invocation->source);
      qa_app_renderer_diagnostics_free(&d);
      break;
    }
    case DIAG_GFX_INFO: {
      qa_app_renderer_diagnostics d = services->renderer(services->ctx);
      append_str(&text, "backend: "); append_str(&text, d.backend); append_str(&text, "\ndrawable: ");
      append_num(&text, d.width); append_str(&text, "x"); append_num(&text, d.height); append_str(&text, "\n");
      if (d.driver == NULL) append_str(&text, "driver: software renderer\n");
      else {
        append_str(&text, "vendor: "); append_str(&text, d.driver->vendor); append_str(&text, "\nrenderer: ");
        append_str(&text, d.driver->renderer); append_str(&text, "\nversion: "); append_str(&text, d.driver->version);
        append_str(&text, "\nshadingLanguage: "); append_str(&text, d.driver->shading_language); append_str(&text, "\n");
      }
      services->print(services->ctx, text, invocation->source);
      qa_app_renderer_diagnostics_free(&d);
      break;
    }
  }
  free(text);
}
qa_app_renderer_diagnostics_registration *qa_app_register_renderer_diagnostics(qa_app_renderer_diagnostic_services services) {
  qa_app_renderer_diagnostics_registration *reg = calloc(1, sizeof *reg);
  if (!reg) return NULL;
  reg->services = services;
  reg->ctxs = calloc(6, sizeof *reg->ctxs);
  if (!reg->ctxs) { free(reg); return NULL; }
  static const struct { const char *name, *summary, *usage; int which; } defs[6] = {
      {"imagelist", "List actual resident renderer images.", "imagelist", DIAG_IMAGE_LIST},
      {"shaderlist", "List registered shaders, optionally in source sorted order.", "shaderlist [sorted]", DIAG_SHADER_LIST},
      {"modellist", "List model handles registered by the invoking source client.", "modellist", DIAG_MODEL_LIST},
      {"skinlist", "List source skin handles and their registered surface shaders.", "skinlist", DIAG_SKIN_LIST},
      {"modelist", "List display modes returned by the active video device.", "modelist", DIAG_MODE_LIST},
      {"gfxinfo", "Report the actual backend, drawable size and GL driver when present.", "gfxinfo", DIAG_GFX_INFO}};
  for (size_t i = 0; i < 6; i++) {
    if (qa_command_buffer_exists(services.commands, defs[i].name))
      qa_range_error("Renderer diagnostic command already registered: %s", defs[i].name);
    reg->ctxs[i].reg = reg; reg->ctxs[i].which = defs[i].which;
    qa_command_documentation doc = {defs[i].summary, defs[i].usage, NULL, 0, NULL, 0};
    if (!qa_command_buffer_register_engine(services.commands, defs[i].name, diagnostic_handler, &reg->ctxs[i], &doc))
      qa_range_error("Cannot register renderer diagnostic %s", defs[i].name);
    reg->names[reg->count++] = defs[i].name;
  }
  return reg;
}
void qa_app_renderer_diagnostics_unregister(qa_app_renderer_diagnostics_registration *r) {
  if (!r) return;
  for (size_t i = 0; i < r->count; i++)
    qa_command_buffer_unregister(r->services.commands, r->names[i], diagnostic_handler, &r->ctxs[i], true);
  r->count = 0;
  free(r->ctxs);
  free(r);
}
