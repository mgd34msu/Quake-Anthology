/* Port of src/render/scene/textures.ts. GPL-2.0-or-later.
 * Note: landed C decoders abort on undecodable bytes (qa_binary_error)
 * instead of throwing, so the TS try/catch candidate skipping cannot be
 * reproduced; the first present candidate's decode result stands. */
#include "qa/render/scene/textures.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/math.h"
#include "qa/formats/images.h"
#include "qa/foundation/support.h"
#include "qa/render/scene/q2_image.h"
#include "qa/render/scene/skin.h"
static void *xalloc(size_t n) { void *p = malloc(n ? n : 1); if (!p) qa_range_error("Allocation failed"); return p; }
static void *xg(void *p, size_t n) { void *q = realloc(p, n ? n : 1); if (!q) qa_range_error("Allocation failed"); return q; }
static char *xdup(const char *s) { size_t n = strlen(s) + 1; char *p = xalloc(n); memcpy(p, s, n); return p; }
static int str_eq(const char *a, const char *b) { return a && b && !strcmp(a, b); }
static int ends_ci(const char *s, const char *suffix) {
  size_t ls = strlen(s), lf = strlen(suffix); if (lf > ls) return 0;
  for (size_t i = 0; i < lf; i++) { char a = s[ls-lf+i], b = suffix[i]; if (a >= 'A' && a <= 'Z') a = (char)(a+32); if (a != b) return 0; }
  return 1;
}
static int starts(const char *s, const char *prefix) { return !strncmp(s, prefix, strlen(prefix)); }
typedef struct tex_rec { qa_scene_texture tex; char *name_owned; void *decoded; int decoded_kind; qa_image_level *levels; qa_q2_mipmapped_image *q2; qa_mip_chain *chain; } tex_rec;
typedef struct cache_entry { char *key; const qa_scene_texture *tex; int is_null; } cache_entry;
typedef struct req_entry { char *key; char *name; qa_scene_texture_load_options options; char *wrap_owned, *family_owned, *usage_owned; } req_entry;
typedef struct anim_frames { double ordinal; qa_render_image *frames; size_t count; qa_scene_texture *texture; double previous; qa_scene_texture_loader *loader; } anim_frames;
typedef struct impl {
  qa_scene_image_policy_owned policy; int has_policy;
  tex_rec **textures; size_t ntex, ctex;
  cache_entry *cache; size_t ncache, ccache;
  req_entry *requests; size_t nreq, creq;
  qa_renderer_image *owned; size_t nowned, cowned;
  anim_frames *anims; size_t nanim, canim;
  qa_scene_animation_token *stops; size_t nstop, cstop;
  void **scratch; size_t nscratch, cscratch;
} impl;
static impl *im(qa_scene_texture_loader *l) { return (impl *)l->_loaded; }
static void scratch_add(impl *o, void *p) { if (o->nscratch == o->cscratch) { o->cscratch = o->cscratch ? o->cscratch * 2 : 16; o->scratch = xg(o->scratch, o->cscratch * sizeof(void *)); } o->scratch[o->nscratch++] = p; }
static void require_open(qa_scene_texture_loader *l) { if (l->closed) qa_range_error("Scene texture loader is closed"); }
static const qa_texture_sampling REPEAT_SAMP = { "repeat", "linear-mipmap-nearest" };
static tex_rec *new_rec(qa_scene_texture_loader *l) {
  impl *o = im(l);
  if (o->ntex == o->ctex) { o->ctex = o->ctex ? o->ctex * 2 : 8; o->textures = xg(o->textures, o->ctex * sizeof *o->textures); }
  tex_rec *r = xalloc(sizeof *r); memset(r, 0, sizeof *r);
  o->textures[o->ntex++] = r; return r;
}
static void owned_add(impl *o, qa_renderer_image image) {
  if (o->nowned == o->cowned) { o->cowned = o->cowned ? o->cowned * 2 : 8; o->owned = xg(o->owned, o->cowned * sizeof *o->owned); }
  o->owned[o->nowned++] = image;
}
const qa_scene_texture *qa_scene_texture_loader_register(qa_scene_texture_loader *l, const char *name, qa_render_image content,
    const qa_texture_sampling *sampling, const qa_renderer_image *source, bool has_logical, double logical_width, double logical_height) {
  require_open(l);
  impl *o = im(l);
  qa_texture_sampling samp = sampling ? *sampling : REPEAT_SAMP;
  const qa_scene_image_handle *handle = source
    ? qa_scene_image_registry_register(l->images, name, content, samp, *source)
    : qa_scene_image_registry_register_generated(l->images, name, content, samp);
  qa_renderer_image image = handle->image;
  owned_add(o, image);
  tex_rec *rec = new_rec(l);
  rec->name_owned = xdup(name);
  rec->tex.name = rec->name_owned; rec->tex.image = image; rec->tex.content = content;
  double lw = has_logical ? logical_width : (content.kind == QA_RENDER_IMAGE_DEPTH32F ? content.depth_levels[0].width : content.levels[0].width);
  double lh = has_logical ? logical_height : (content.kind == QA_RENDER_IMAGE_DEPTH32F ? content.depth_levels[0].height : content.levels[0].height);
  rec->tex.width = lw; rec->tex.height = lh; rec->tex.fullbright = NULL;
  if (content.kind == QA_RENDER_IMAGE_INDEXED8 && content.fullbright) {
    qa_fullbright_range range = *content.fullbright;
    qa_image_level *levels = xalloc(content.level_count * sizeof *levels);
    for (size_t li = 0; li < content.level_count; li++) {
      qa_image_level level = content.levels[li];
      size_t npix = (size_t)(level.width * level.height);
      uint8_t *pixels = xalloc(npix ? npix * 4 : 1); memset(pixels, 0, npix * 4);
      for (size_t offset = 0; offset < level.pixels.length; offset++) {
        uint8_t index = level.pixels.bytes[offset];
        int transparent = !str_eq(content.transparency.kind, "opaque") && index == content.transparency.index;
        if (index < range.first || index > range.last || transparent) continue;
        uint8_t translated = content.translation && index < content.translation->length ? content.translation->bytes[index] : index;
        size_t co = (size_t)translated * 3;
        pixels[offset*4] = co < content.palette.colors.length ? content.palette.colors.bytes[co] : 0;
        pixels[offset*4+1] = co+1 < content.palette.colors.length ? content.palette.colors.bytes[co+1] : 0;
        pixels[offset*4+2] = co+2 < content.palette.colors.length ? content.palette.colors.bytes[co+2] : 0;
        pixels[offset*4+3] = 255;
      }
      levels[li].width = level.width; levels[li].height = level.height;
      levels[li].pixels.bytes = pixels; levels[li].pixels.length = npix * 4;
      scratch_add(o, pixels);
    }
    scratch_add(o, levels);
    qa_render_image fb; memset(&fb, 0, sizeof fb); fb.kind = QA_RENDER_IMAGE_RGBA8; fb.levels = levels; fb.level_count = content.level_count;
    fb.border_color = qa_math_vec4(0, 0, 0, 0);
    char *fbname = xalloc(strlen(name) + 16); snprintf(fbname, strlen(name) + 16, "%s:fullbright", name); scratch_add(o, fbname);
    const qa_scene_image_handle *fbh = source
      ? qa_scene_image_registry_register(l->images, fbname, fb, samp, *source)
      : qa_scene_image_registry_register_generated(l->images, fbname, fb, samp);
    rec->tex.fullbright_value = fbh->image; rec->tex.fullbright = &rec->tex.fullbright_value;
    owned_add(o, fbh->image);
  }
  return &rec->tex;
}
static qa_render_image rgba_wrap(qa_scene_texture_loader *l, qa_image_level level_owned, int mipmap, tex_rec *rec) {
  impl *o = im(l); (void)o;
  qa_render_image image; memset(&image, 0, sizeof image);
  image.kind = QA_RENDER_IMAGE_RGBA8; image.border_color = qa_math_vec4(0, 0, 0, 0);
  if (!mipmap) {
    qa_image_level *levels = xalloc(sizeof *levels); *levels = level_owned;
    if (rec) rec->levels = levels;
    scratch_add(o, levels);
    image.levels = levels; image.level_count = 1;
  } else {
    qa_mip_chain *chain = xalloc(sizeof *chain); memset(chain, 0, sizeof *chain);
    if (!qa_generate_mip_chain(&level_owned, QA_MIP_BOX, chain)) qa_range_error("Allocation failed");
    if (rec) rec->chain = chain;
    scratch_add(o, chain);
    image.levels = chain->levels; image.level_count = chain->count;
  }
  return image;
}
bool qa_scene_texture_loader_init(qa_scene_texture_loader *l, qa_scene_image_registry *images, qa_scene_asset_reader reader, const qa_palette *palette, const qa_scene_texture_loader_options *options) {
  memset(l, 0, sizeof *l);
  impl *o = xalloc(sizeof *o); memset(o, 0, sizeof *o);
  l->_loaded = o; l->_policy = o; l->_requests = o; l->_owned = o; l->_animations = o;
  l->images = images; l->reader = reader;
  if (palette) { l->palette_value = *palette; l->palette = &l->palette_value; }
  if (options && options->has_policy) { o->has_policy = qa_snapshot_image_policy(options->policy, &o->policy); if (!o->has_policy) return false; }
  l->strict = options && options->strict;
  l->fullbright_first = options && options->has_fullbright_first ? options->fullbright_first : 224;
  static const uint8_t white_px[4] = { 255, 255, 255, 255 };
  qa_image_level white_level = { 1, 1, { white_px, 4 } };
  qa_image_level *white_levels = xalloc(sizeof *white_levels); *white_levels = white_level; scratch_add(o, white_levels);
  qa_render_image white_content; memset(&white_content, 0, sizeof white_content);
  white_content.kind = QA_RENDER_IMAGE_RGBA8; white_content.levels = white_levels; white_content.level_count = 1;
  white_content.border_color = qa_math_vec4(1, 1, 1, 1);
  qa_texture_sampling white_samp = { "repeat", "nearest" };
  l->white = qa_scene_texture_loader_register(l, "*white", white_content, &white_samp, NULL, false, 0, 0);
  uint8_t *pixels = xalloc(16 * 16 * 4);
  for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
    int edge = x == 0 || y == 0 || x == 15 || y == 15;
    uint8_t *dst = pixels + (y * 16 + x) * 4;
    dst[0] = edge ? 255 : 32; dst[1] = edge ? 255 : 32; dst[2] = edge ? 255 : 32; dst[3] = 255;
  }
  qa_image_level miss_level = { 16, 16, { pixels, 16 * 16 * 4 } };
  qa_mip_chain *chain = xalloc(sizeof *chain); memset(chain, 0, sizeof *chain);
  if (!qa_generate_mip_chain(&miss_level, QA_MIP_BOX, chain)) return false;
  scratch_add(o, pixels); scratch_add(o, chain);
  qa_render_image miss_content; memset(&miss_content, 0, sizeof miss_content);
  miss_content.kind = QA_RENDER_IMAGE_RGBA8; miss_content.levels = chain->levels; miss_content.level_count = chain->count;
  miss_content.border_color = qa_math_vec4(0, 0, 0, 1);
  l->missing = qa_scene_texture_loader_register(l, "*default", miss_content, &REPEAT_SAMP, NULL, false, 0, 0);
  return true;
}
void qa_scene_texture_loader_destroy(qa_scene_texture_loader *l) {
  if (!l || !l->_loaded) return;
  impl *o = im(l);
  for (size_t i = 0; i < o->ntex; i++) {
    free(o->textures[i]->name_owned);
    if (o->textures[i]->q2) { qa_q2_mipmapped_image_destroy(o->textures[i]->q2); free(o->textures[i]->q2); }
    free(o->textures[i]);
  }
  free(o->textures);
  for (size_t i = 0; i < o->ncache; i++) free(o->cache[i].key);
  free(o->cache);
  for (size_t i = 0; i < o->nreq; i++) { free(o->requests[i].key); free(o->requests[i].name); free(o->requests[i].wrap_owned); free(o->requests[i].family_owned); free(o->requests[i].usage_owned); }
  free(o->requests); free(o->owned); free(o->anims); free(o->stops);
  for (size_t i = 0; i < o->nscratch; i++) free(o->scratch[i]);
  free(o->scratch);
  if (o->has_policy) qa_scene_image_policy_owned_destroy(&o->policy);
  free(o); memset(l, 0, sizeof *l);
}
const qa_scene_texture *qa_scene_texture_loader_q1_embedded(qa_scene_texture_loader *l, qa_q1_mip_texture texture) {
  if (!texture.is_embedded) return NULL;
  if (!l->palette) qa_range_error("Embedded Quake textures require their content palette");
  qa_image_level levels[4];
  for (int mip = 0; mip < 4; mip++) {
    int w = (int)texture.width >> mip; if (w < 1) w = 1;
    int h = (int)texture.height >> mip; if (h < 1) h = 1;
    levels[mip].width = w; levels[mip].height = h; levels[mip].pixels = texture.levels[mip];
  }
  qa_palette_transparency transparency; memset(&transparency, 0, sizeof transparency);
  transparency.kind = starts(texture.name, "{") ? "q1-fence" : "opaque"; transparency.index = 255;
  qa_fullbright_range range = { l->fullbright_first, 255 };
  const qa_fullbright_range *fb = (starts(texture.name, "sky") || starts(texture.name, "*")) ? NULL : &range;
  qa_render_image content = qa_indexed_render_image_make(levels, 4, *l->palette, transparency, fb, NULL);
  return qa_scene_texture_loader_register(l, texture.name, content, NULL, NULL, false, 0, 0);
}
static const qa_scene_texture *cache_get(impl *o, const char *key, int *present, int *is_null) {
  for (size_t i = 0; i < o->ncache; i++) if (!strcmp(o->cache[i].key, key)) { *present = 1; *is_null = o->cache[i].is_null; return o->cache[i].tex; }
  *present = 0; return NULL;
}
static void cache_set(impl *o, const char *key, const qa_scene_texture *tex) {
  if (o->ncache == o->ccache) { o->ccache = o->ccache ? o->ccache * 2 : 16; o->cache = xg(o->cache, o->ccache * sizeof *o->cache); }
  o->cache[o->ncache++] = (cache_entry){ xdup(key), tex, tex == NULL };
}
static anim_frames *anim_find(impl *o, double ordinal) {
  for (size_t i = 0; i < o->nanim; i++) if (o->anims[i].ordinal == ordinal) return &o->anims[i];
  return NULL;
}
typedef struct anim_ctx { anim_frames *frames; } anim_ctx;
static void anim_update(void *ctx, double milliseconds) {
  anim_frames *f = ((anim_ctx *)ctx)->frames;
  double beat = floor(milliseconds / 100);
  long n = (long)f->count;
  long index = (long)fmod(fmod(beat, (double)n) + n, (double)n);
  if ((double)index == f->previous) return;
  const qa_render_image *frame = &f->frames[index];
  for (size_t level = 0; level < frame->level_count; level++)
    qa_scene_image_registry_update(f->loader->images, f->texture->image, (double)level, frame->levels[level]);
  f->previous = (double)index;
}
static void animate(qa_scene_texture_loader *l, const qa_scene_texture *texture, qa_render_image *frames, size_t count) {
  if (count < 2) return;
  impl *o = im(l);
  if (o->nanim == o->canim) { o->canim = o->canim ? o->canim * 2 : 4; o->anims = xg(o->anims, o->canim * sizeof *o->anims); }
  anim_frames *f = &o->anims[o->nanim++];
  f->ordinal = texture->image.ordinal; f->frames = frames; f->count = count; f->texture = (qa_scene_texture *)texture; f->previous = 0; f->loader = l;
  anim_ctx *ctx = xalloc(sizeof *ctx); ctx->frames = f; scratch_add(o, ctx); scratch_add(o, frames);
  qa_scene_animation_token token = qa_scene_image_registry_track_animation(l->images, texture->image, anim_update, ctx, NULL, NULL);
  if (o->nstop == o->cstop) { o->cstop = o->cstop ? o->cstop * 2 : 4; o->stops = xg(o->stops, o->cstop * sizeof *o->stops); }
  o->stops[o->nstop++] = token;
}
const qa_scene_texture *qa_scene_texture_loader_sample_surface(qa_scene_texture_loader *l, const qa_scene_texture *texture, qa_scene_surface_options options) {
  require_open(l);
  if (options.mipmap && str_eq(options.wrap, "repeat")) return texture;
  impl *o = im(l);
  char key[256]; snprintf(key, sizeof key, "%csurface:%g%c%d%c%s", 0, texture->image.ordinal, 0, options.mipmap ? 1 : 0, 0, options.wrap);
  /* C key: embedded NULs collapse; use printable key */
  snprintf(key, sizeof key, "surface:%g:%d:%s", texture->image.ordinal, options.mipmap ? 1 : 0, options.wrap);
  int present = 0, is_null = 0;
  const qa_scene_texture *existing = cache_get(o, key, &present, &is_null);
  if (present) {
    if (is_null) qa_range_error("Surface sampling cache lost its texture");
    return existing;
  }
  qa_render_image original = texture->content;
  qa_render_image content = original;
  if (!options.mipmap) { content.level_count = 1; content.depth_level_count = original.kind == QA_RENDER_IMAGE_DEPTH32F ? 1 : original.depth_level_count; }
  qa_texture_sampling samp = { options.wrap, options.mipmap ? "linear-mipmap-nearest" : "linear" };
  const qa_scene_texture *sampled = qa_scene_texture_loader_register(l, texture->name, content, &samp, &texture->image, true, texture->width, texture->height);
  anim_frames *frames = anim_find(o, texture->image.ordinal);
  if (frames) {
    qa_render_image *surface_frames = xalloc(frames->count * sizeof *surface_frames);
    for (size_t i = 0; i < frames->count; i++) { surface_frames[i] = frames->frames[i]; if (!options.mipmap) surface_frames[i].level_count = 1; }
    animate(l, sampled, surface_frames, frames->count);
  }
  cache_set(o, key, sampled);
  return sampled;
}
static qa_render_image indexed_content(qa_scene_texture_loader *l, qa_image_level *levels, size_t n, qa_palette palette, qa_palette_transparency transparency, const qa_fullbright_range *fb) {
  (void)l;
  return qa_indexed_render_image_make(levels, n, palette, transparency, fb, NULL);
}
static const char *suffix_of(const char *path) { const char *d = strrchr(path, '.'); return d ? d : ""; }
static int suffix_is(const char *path, const char *suffix) { return ends_ci(path, suffix); }
static qa_image_level level_from(qa_image_level level, uint8_t *pixels, size_t n) { level.pixels.bytes = pixels; level.pixels.length = n; return level; }
static const qa_scene_texture *load_uncached(qa_scene_texture_loader *l, const char *name, qa_scene_texture_load_options options) {
  impl *o = im(l);
  const char *dot = strrchr(name, '.'), *slash = strrchr(name, '/');
  int explicit_ext = dot && (!slash || dot > slash);
  char *base = explicit_ext ? xalloc((size_t)(dot - name) + 1) : xdup(name);
  if (explicit_ext) { memcpy(base, name, (size_t)(dot - name)); base[dot - name] = '\0'; }
  int wall = str_eq(options.usage, "wall") || (!options.usage && (starts(name, "textures/") || ends_ci(name, ".wal")));
  const qa_scene_image_policy *policy = NULL; qa_scene_image_policy def_policy;
  if (o->has_policy) policy = &o->policy.policy;
  else if (str_eq(options.family, "q2")) { def_policy = qa_default_image_policy(); policy = &def_policy; }
  const char *src_exts[8]; int nsrc = 0;
  if (str_eq(options.family, "q2")) { const char *e[] = { ".png",".jpg",".tga",".jpeg",".bmp",".gif", wall ? ".wal" : ".pcx" }; memcpy(src_exts, e, sizeof e); nsrc = 7; }
  else if (str_eq(options.family, "q1")) { const char *e[] = { ".lmp",".tga",".jpg",".png",".jpeg",".pcx",".bmp",".gif" }; memcpy(src_exts, e, sizeof e); nsrc = 8; }
  else { const char *e[] = { ".tga",".jpg",".png",".jpeg",".pcx",".bmp",".gif" }; memcpy(src_exts, e, sizeof e); nsrc = 7; }
  char override_exts[8][8]; int nover = 0;
  if (policy && policy->has_formats) for (size_t i = 0; i < policy->format_count && nover < 8; i++) snprintf(override_exts[nover++], 8, ".%s", policy->formats[i]);
  else for (int i = 0; i < nsrc; i++) if (strcmp(src_exts[i], ".lmp") && strcmp(src_exts[i], ".wal") && strcmp(src_exts[i], ".pcx")) { snprintf(override_exts[nover++], 8, "%s", src_exts[i]); }
  const char *exts[16]; int next = 0;
  if (!policy || !policy->has_formats) for (int i = 0; i < nsrc; i++) exts[next++] = src_exts[i];
  else {
    for (int i = 0; i < nover; i++) exts[next++] = override_exts[i];
    if (str_eq(options.family, "q1")) { exts[next++] = ".lmp"; exts[next++] = ".pcx"; }
    else exts[next++] = (str_eq(options.family, "q2") && wall) ? ".wal" : ".pcx";
  }
  char *requested_name = xdup(name);
  if (!explicit_ext && str_eq(options.family, "q2") && wall) { free(requested_name); requested_name = xalloc(strlen(name) + 5); sprintf(requested_name, "%s.wal", name); }
  char requested[32] = "";
  if (explicit_ext) { size_t rl = strlen(dot + 1); if (rl > 30) rl = 30; for (size_t i = 0; i < rl; i++) { char c = dot[1+i]; requested[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; } }
  else if (strcmp(requested_name, name)) strcpy(requested, "wal");
  int native = !strcmp(requested, "pcx") || !strcmp(requested, "wal") || (str_eq(options.family, "q1") && !strcmp(requested, "lmp"));
  int truecolor = !strcmp(requested,"png")||!strcmp(requested,"jpg")||!strcmp(requested,"tga")||!strcmp(requested,"jpeg")||!strcmp(requested,"bmp")||!strcmp(requested,"gif");
  const char *usage = options.usage ? options.usage : (wall ? "wall" : "picture");
  int usage_ok = 0;
  if (policy) for (size_t i = 0; i < policy->override_usage_count; i++) if (!strcmp(policy->override_usages[i], usage)) usage_ok = 1;
  int override_native = policy && policy->override_level >= 1 && usage_ok && (native || (policy->override_level > 1 && truecolor));
  /* candidates: Set semantics with dedupe */
  char *candidates[40]; int ncand = 0;
  #define ADD_CAND(p) do { int dup = 0; for (int i = 0; i < ncand; i++) if (!strcmp(candidates[i], (p))) dup = 1; if (!dup && ncand < 40) candidates[ncand++] = (p); else free(p); } while (0)
  if (override_native) for (int i = 0; i < nover; i++) { char *p = xalloc(strlen(base) + 9); sprintf(p, "%s%s", base, override_exts[i]); ADD_CAND(p); }
  if (explicit_ext || strcmp(requested_name, name)) ADD_CAND(xdup(requested_name));
  for (int i = 0; i < next; i++) { const char *e = (policy && policy->has_formats && i < nover) ? override_exts[i] : exts[i]; char *p = xalloc(strlen(base) + 9); sprintf(p, "%s%s", base, e); ADD_CAND(p); }
  const qa_scene_texture *result = NULL;
  for (int ci = 0; ci < ncand && !result; ci++) {
    const char *path = candidates[ci];
    qa_scene_asset asset; uint8_t *owned = NULL;
    if (!l->reader.read(l->reader.ctx, path, &asset, &owned)) continue;
    require_open(l);
    const char *suffix = suffix_of(path);
    qa_render_image content; memset(&content, 0, sizeof content);
    qa_render_image *anim = NULL; size_t nanim = 0;
    int mipmap = !options.has_mipmap || options.mipmap;
    tex_rec *pending_rec = NULL; /* decoded storage is scratch-owned */
    if (ends_ci(path, ".gif")) {
      qa_gif_image *decoded = xalloc(sizeof *decoded); memset(decoded, 0, sizeof *decoded);
      if (!qa_decode_gif(asset.bytes, asset.length, path, decoded)) qa_range_error("Allocation failed");
      scratch_add(o, decoded);
      anim = xalloc((decoded->frame_count ? decoded->frame_count : 1) * sizeof *anim); nanim = decoded->frame_count;
      for (size_t fi = 0; fi < decoded->frame_count; fi++) {
        qa_render_image rgba = rgba_wrap(l, decoded->frames[fi].image, mipmap, NULL);
        qa_render_image converted = (str_eq(options.family, "q2") && mipmap) ? rgba : rgba;
        if (str_eq(options.family, "q2") && mipmap) {
          qa_q2_mipmapped_image *q2 = xalloc(sizeof *q2);
          converted = qa_q2_mipmapped_image_new(rgba, q2); scratch_add(o, q2);
          if (converted.kind == QA_RENDER_IMAGE_DEPTH32F) qa_range_error("GIF mip processing produced depth pixels");
        }
        anim[fi] = converted;
      }
      content = anim[0];
    } else if (ends_ci(path, ".lmp")) {
      if (!l->palette) qa_range_error("Quake picture textures require their content palette");
      qa_indexed_image *pic = xalloc(sizeof *pic); memset(pic, 0, sizeof *pic);
      if (!qa_decode_qpic(asset.bytes, asset.length, path, pic)) qa_range_error("Allocation failed");
      scratch_add(o, pic);
      qa_image_level *lvl = xalloc(sizeof *lvl); lvl->width = (double)pic->width; lvl->height = (double)pic->height; lvl->pixels.bytes = pic->indices; lvl->pixels.length = pic->indices_length; scratch_add(o, lvl);
      qa_palette_transparency tr = { "opaque", 0 }; qa_fullbright_range fb = { l->fullbright_first, 255 };
      qa_fullbright_range *fbs = xalloc(sizeof *fbs); *fbs = fb; scratch_add(o, fbs);
      content = indexed_content(l, lvl, 1, *l->palette, tr, fbs);
    } else if (ends_ci(path, ".wal")) {
      if (!l->palette) qa_range_error("WAL textures require their content palette");
      qa_wal_image *wal = xalloc(sizeof *wal); memset(wal, 0, sizeof *wal);
      if (!qa_decode_wal(asset.bytes, asset.length, path, wal)) qa_range_error("Allocation failed");
      scratch_add(o, wal);
      qa_palette_transparency tr = { "opaque", 0 };
      content = indexed_content(l, wal->levels, 4, *l->palette, tr, NULL);
    } else if (ends_ci(path, ".pcx")) {
      qa_pcx_image *pcx = xalloc(sizeof *pcx); memset(pcx, 0, sizeof *pcx);
      if (!qa_decode_pcx(asset.bytes, asset.length, path, pcx)) qa_range_error("Allocation failed");
      scratch_add(o, pcx);
      if (!l->palette && !pcx->palette) qa_range_error("PCX has no palette: %s", path);
      const uint8_t *palette_bytes = pcx->palette ? pcx->palette : l->palette->colors.bytes;
      size_t palette_len = pcx->palette ? pcx->palette_length : l->palette->colors.length;
      if (!palette_bytes) qa_range_error("PCX palette was lost: %s", path);
      if (str_eq(options.family, "q2") && (str_eq(options.usage, "skin") || str_eq(options.usage, "sprite")) && l->palette) {
        uint8_t *pixels = pcx->indices;
        if (str_eq(options.usage, "skin")) {
          size_t fo = 0; pixels = qa_flood_skin(pcx->indices, pcx->indices_length, (double)pcx->width, (double)pcx->height, *l->palette, &fo);
          if (!pixels) qa_range_error("Allocation failed");
          scratch_add(o, pixels);
        }
        qa_image_level *lvl = xalloc(sizeof *lvl); lvl->width = (double)pcx->width; lvl->height = (double)pcx->height; lvl->pixels.bytes = pixels; lvl->pixels.length = pcx->indices_length; scratch_add(o, lvl);
        qa_palette_transparency tr = { "index", 255 };
        content = indexed_content(l, lvl, 1, *l->palette, tr, NULL);
      } else {
        size_t npix = (size_t)(pcx->width * pcx->height);
        uint8_t *pixels = xalloc(npix ? npix * 4 : 1);
        size_t count = pcx->indices_length;
        for (size_t offset = 0; offset < count; offset++) {
          uint8_t index = pcx->indices[offset]; uint8_t color = index;
          if (str_eq(options.family, "q2") && index == 255) {
            int found = -1;
            if (offset > (size_t)pcx->width && pcx->indices[offset - (size_t)pcx->width] != 255) found = pcx->indices[offset - (size_t)pcx->width];
            else if (offset < count - (size_t)pcx->width && pcx->indices[offset + (size_t)pcx->width] != 255) found = pcx->indices[offset + (size_t)pcx->width];
            else if (offset > 0 && pcx->indices[offset - 1] != 255) found = pcx->indices[offset - 1];
            else if (offset < count - 1 && pcx->indices[offset + 1] != 255) found = pcx->indices[offset + 1];
            color = found >= 0 ? (uint8_t)found : 0;
          }
          size_t co = (size_t)color * 3;
          pixels[offset*4] = co < palette_len ? palette_bytes[co] : 0;
          pixels[offset*4+1] = co+1 < palette_len ? palette_bytes[co+1] : 0;
          pixels[offset*4+2] = co+2 < palette_len ? palette_bytes[co+2] : 0;
          pixels[offset*4+3] = (str_eq(options.family, "q2") && index == 255) ? 0 : 255;
        }
        qa_image_level level = { (double)pcx->width, (double)pcx->height, { pixels, npix * 4 } };
        scratch_add(o, pixels);
        content = rgba_wrap(l, level, mipmap, NULL);
        (void)pending_rec;
      }
    } else {
      if (!(ends_ci(path,".png")||ends_ci(path,".tga")||ends_ci(path,".bmp")||ends_ci(path,".jpg")||ends_ci(path,".jpeg")))
        qa_range_error("Unsupported scene image format: %s", path);
      qa_image_level level; memset(&level, 0, sizeof level);
      if (ends_ci(path, ".png")) { qa_png_image *d = xalloc(sizeof *d); memset(d, 0, sizeof *d); if (!qa_decode_png(asset.bytes, asset.length, path, d)) qa_range_error("Allocation failed"); scratch_add(o, d); level = d->level; }
      else if (ends_ci(path, ".tga")) {
        if (!options.family || str_eq(options.family, "q3")) { qa_image_data *d = xalloc(sizeof *d); memset(d, 0, sizeof *d); if (!qa_decode_q3_tga(asset.bytes, asset.length, path, NULL, NULL, d)) qa_range_error("Allocation failed"); scratch_add(o, d); level = level_from((qa_image_level){ (double)d->width, (double)d->height, {0,0} }, d->pixels, d->pixels_length); }
        else { qa_tga_image *d = xalloc(sizeof *d); memset(d, 0, sizeof *d); if (!qa_decode_tga(asset.bytes, asset.length, path, d)) qa_range_error("Allocation failed"); scratch_add(o, d); level = d->level; }
      }
      else if (ends_ci(path, ".bmp")) { qa_bmp_image *d = xalloc(sizeof *d); memset(d, 0, sizeof *d); if (!qa_decode_bmp(asset.bytes, asset.length, path, d)) qa_range_error("Allocation failed"); scratch_add(o, d); level = d->level; }
      else { qa_jpeg_image *d = xalloc(sizeof *d); memset(d, 0, sizeof *d); if (!qa_decode_jpeg(asset.bytes, asset.length, path, NULL, NULL, d)) qa_range_error("Allocation failed"); scratch_add(o, d); level = *d; }
      content = rgba_wrap(l, level, mipmap, NULL);
    }
    /* logical size from the original beneath a replacement */
    double logical_w = content.kind == QA_RENDER_IMAGE_DEPTH32F ? content.depth_levels[0].width : content.levels[0].width;
    double logical_h = content.kind == QA_RENDER_IMAGE_DEPTH32F ? content.depth_levels[0].height : content.levels[0].height;
    int logical_set = 0;
    /* Original-size adoption attempts; decode failures abort in C (see header note). */
    if (str_eq(options.family, "q2") && suffix_is(path, ".wal") == 0 && (ends_ci(name, ".wal") || (!explicit_ext && wall))) {
      char *walpath = xalloc(strlen(base) + 5); sprintf(walpath, "%s.wal", base);
      qa_scene_asset oa; uint8_t *oo = NULL;
      int got = (l->reader.read_original && l->reader.read_original(l->reader.ctx, walpath, &oa, &oo)) || l->reader.read(l->reader.ctx, walpath, &oa, &oo);
      if (got) { qa_wal_image d; memset(&d, 0, sizeof d); if (qa_decode_wal(oa.bytes, oa.length, walpath, &d)) { logical_w = (double)d.width; logical_h = (double)d.height; logical_set = 1; qa_wal_image_destroy(&d); } }
      free(walpath); free(oo);
    }
    (void)logical_set; (void)suffix;
    if (!anim && str_eq(options.family, "q2") && mipmap) {
      qa_q2_mipmapped_image *q2 = xalloc(sizeof *q2);
      content = qa_q2_mipmapped_image_new(content, q2); scratch_add(o, q2);
      /* logical size tracks the pre-mipmap content unless adopted */
    }
    qa_texture_sampling samp = { options.wrap ? options.wrap : "repeat", (options.has_mipmap && !options.mipmap) ? "linear" : "linear-mipmap-nearest" };
    qa_renderer_image source_image = asset.source_sample;
    const qa_scene_texture *texture = qa_scene_texture_loader_register(l, name, content, &samp, &source_image, true, logical_w, logical_h);
    if (anim) animate(l, texture, anim, nanim);
    free(owned);
    result = texture;
  }
  for (int i = 0; i < ncand; i++) free(candidates[i]);
  free(base); free(requested_name);
  return result;
}
const qa_scene_texture *qa_scene_texture_loader_load(qa_scene_texture_loader *l, const char *name, const qa_scene_texture_load_options *options_in) {
  require_open(l);
  qa_scene_texture_load_options options; memset(&options, 0, sizeof options);
  if (options_in) options = *options_in;
  if (!options.family || str_eq(options.family, "q3")) {
    if (!strcmp(name, l->white->name)) return l->white;
    if (!strcmp(name, l->missing->name)) return l->missing;
  }
  char key[1024];
  snprintf(key, sizeof key, "%s|%d|%s|%s|%s", name, (!options.has_mipmap || options.mipmap) ? 1 : 0,
           options.wrap ? options.wrap : "repeat", options.family ? options.family : "q3", options.usage ? options.usage : "");
  impl *o = im(l);
  int present = 0, is_null = 0;
  const qa_scene_texture *existing = cache_get(o, key, &present, &is_null);
  if (present) return is_null ? NULL : existing;
  if (o->nreq == o->creq) { o->creq = o->creq ? o->creq * 2 : 16; o->requests = xg(o->requests, o->creq * sizeof *o->requests); }
  req_entry *req = &o->requests[o->nreq++];
  req->key = xdup(key); req->name = xdup(name); req->options = options;
  req->wrap_owned = options.wrap ? xdup(options.wrap) : NULL; req->options.wrap = req->wrap_owned;
  req->family_owned = options.family ? xdup(options.family) : NULL; req->options.family = req->family_owned;
  req->usage_owned = options.usage ? xdup(options.usage) : NULL; req->options.usage = req->usage_owned;
  const qa_scene_texture *loaded = load_uncached(l, name, options);
  /* re-find impl pointers after possible realloc of textures array */
  cache_set(o, key, loaded);
  return loaded;
}
void qa_scene_texture_loader_prepare_replacement(qa_scene_texture_loader *l, qa_scene_texture_loader *replacement) {
  require_open(l);
  impl *o = im(l);
  for (size_t i = 0; i < o->nreq; i++) (void)qa_scene_texture_loader_load(replacement, o->requests[i].name, &o->requests[i].options);
  require_open(l);
}
void qa_scene_texture_loader_close(qa_scene_texture_loader *l) {
  l->closed = true;
  impl *o = im(l);
  for (size_t i = 0; i < o->nstop; i++) qa_scene_animation_token_stop(o->stops[i]);
  o->nstop = 0; o->nanim = 0; o->ncache = 0; o->nreq = 0;
}
void qa_scene_texture_loader_dispose_images(qa_scene_texture_loader *l) {
  qa_scene_texture_loader_close(l);
  impl *o = im(l);
  for (size_t i = 0; i < o->nowned; i++) if (qa_scene_image_registry_is_resident(l->images, o->owned[i])) qa_scene_image_registry_release(l->images, o->owned[i]);
  o->nowned = 0;
}
