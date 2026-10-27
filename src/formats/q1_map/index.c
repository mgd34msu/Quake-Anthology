/* Port of src/formats/q1-map/index.ts. GPL-2.0-or-later. */
#include "qa/formats/q1_map.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static void *xmalloc(size_t size) {
  void *p = malloc(size == 0 ? 1 : size);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}

static char *xstrdup(const char *text) {
  size_t length = strlen(text);
  char *copy = xmalloc(length + 1);
  memcpy(copy, text, length + 1);
  return copy;
}

static void section_or_die(const qa_binary_reader *reader, int64_t offset, int64_t length,
                           qa_binary_reader *out) {
  if (!qa_binary_reader_section(reader, offset, length, out)) qa_range_error("Out of memory");
}

static qa_binary_reader make_reader(const uint8_t *data, size_t length, const char *source) {
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, (uint8_t *)data, length, source)) qa_range_error("Out of memory");
  return reader;
}

static const char *const lump_names[15] = {
    "entities", "planes", "textures", "vertices", "visibility", "nodes", "textureInfo",
    "faces", "lighting", "clipnodes", "leaves", "leafFaces", "edges", "surfaceEdges", "models"};

static qa_q1_bsp_format format_for(uint32_t version, const char *source) {
  switch (version) {
    case QA_Q1_BSP_VERSION: return QA_Q1_FORMAT_BSP29;
    case QA_Q1_BSP2_VERSION: return QA_Q1_FORMAT_BSP2;
    case QA_Q1_2PSB_VERSION: return QA_Q1_FORMAT_2PSB;
    case QA_Q1_QUAKE64_VERSION: return QA_Q1_FORMAT_QUAKE64;
    default: qa_binary_error(source, 0, "unsupported Quake BSP version %u", (unsigned)version);
  }
}

uint8_t *qa_q1_read_lit(const uint8_t *data, size_t data_length, bool has_sample_count,
                        double sample_count, size_t *out_length) {
  qa_binary_reader reader = make_reader(data, data_length, "Quake .lit");
  qa_binary_reader_expect_magic(&reader, "QLIT");
  uint32_t version = qa_binary_reader_u32(&reader);
  if (version != 1) qa_binary_error(reader.source, 4, "unsupported version %u", (unsigned)version);
  size_t remaining = qa_binary_reader_remaining(&reader);
  if (remaining % 3 != 0 || (has_sample_count && (double)remaining != sample_count * 3)) {
    qa_binary_error(reader.source, 8, "RGB sample count differs from BSP lighting");
  }
  uint8_t *result = qa_binary_reader_bytes(&reader, (int64_t)remaining);
  *out_length = remaining;
  qa_binary_reader_destroy(&reader);
  return result;
}

/* The TS catches any error from the external .lit; validate instead. */
static bool try_read_lit(const uint8_t *data, size_t data_length, double sample_count,
                         uint8_t **out, size_t *out_length) {
  if (data_length < 8 || memcmp(data, "QLIT", 4) != 0) return false;
  uint32_t version = (uint32_t)data[4] | ((uint32_t)data[5] << 8) | ((uint32_t)data[6] << 16) |
                     ((uint32_t)data[7] << 24);
  if (version != 1) return false;
  size_t remaining = data_length - 8;
  bool has_count = sample_count > 0;
  if (remaining % 3 != 0 || (has_count && (double)remaining != sample_count * 3)) return false;
  *out = xmalloc(remaining);
  if (remaining > 0) memcpy(*out, data + 8, remaining);
  *out_length = remaining;
  return true;
}

static qa_bsp_lighting select_lighting(const uint8_t *monochrome, size_t mono_length,
                                       const qa_q1_bspx_metadata *metadata,
                                       const qa_q1_map_options *options, bool has_packed,
                                       const uint8_t *packed, size_t packed_length) {
  if (has_packed && packed_length % 2 != 0) {
    qa_binary_error("Quake64 lighting", 0, "incomplete packed RGB sample");
  }
  double sample_count = has_packed ? (double)(packed_length / 2) : (double)mono_length;
  qa_bsp_lighting lighting;
  memset(&lighting, 0, sizeof lighting);
  if (options->has_lit) {
    uint8_t *samples = NULL;
    size_t length = 0;
    if (try_read_lit(options->lit, options->lit_length, sample_count, &samples, &length)) {
      lighting.is_rgb8 = true;
      lighting.source = "lit";
      lighting.samples.bytes = samples;
      lighting.samples.length = length;
      return lighting;
    }
  }
  if (metadata->rgb_lighting != NULL) {
    size_t length = metadata->rgb_lighting_length;
    if (length % 3 != 0 || (sample_count > 0 && (double)length != sample_count * 3)) {
      qa_binary_error("BSPX RGBLIGHTING", 0, "RGB sample count differs from BSP lighting");
    }
    uint8_t *samples = xmalloc(length);
    if (length > 0) memcpy(samples, metadata->rgb_lighting, length);
    lighting.is_rgb8 = true;
    lighting.source = "bspx";
    lighting.samples.bytes = samples;
    lighting.samples.length = length;
    return lighting;
  }
  if (has_packed) {
    size_t count = packed_length / 2;
    uint8_t *samples = xmalloc(count * 3);
    for (size_t i = 0; i < count; i++) {
      uint8_t first = packed[i * 2], second = packed[i * 2 + 1];
      samples[i * 3] = (uint8_t)(first & 0xf8);
      samples[i * 3 + 1] = (uint8_t)(((first & 7) << 5) | ((second & 0xc0) >> 5));
      samples[i * 3 + 2] = (uint8_t)((second & 0x3f) << 2);
    }
    lighting.is_rgb8 = true;
    lighting.source = "bsp";
    lighting.samples.bytes = samples;
    lighting.samples.length = count * 3;
    return lighting;
  }
  uint8_t *samples = xmalloc(mono_length);
  if (mono_length > 0) memcpy(samples, monochrome, mono_length);
  lighting.is_rgb8 = false;
  lighting.source = NULL;
  lighting.samples.bytes = samples;
  lighting.samples.length = mono_length;
  return lighting;
}

static qa_binary_reader lump_reader(const qa_q1_map *map, int index) {
  char source[1024];
  snprintf(source, sizeof source, "%s:%s", map->source, map->lumps[index].name);
  return make_reader(map->lumps[index].data, map->lumps[index].data_length, source);
}

static void validate_references(const qa_q1_map *map) {
  const qa_q1_world_geometry *world = &map->world;
  char source[1024];
#define SRC(suffix) (snprintf(source, sizeof source, "%s:%s", map->source, suffix), source)
  for (size_t i = 0; i < world->edge_count; i++)
    for (int j = 0; j < 2; j++)
      qa_q1_check_index(world->edges[i].vertices[j], (double)world->vertex_count, SRC("edge vertex"));
  for (size_t i = 0; i < world->surface_edges.count; i++)
    qa_q1_check_index(fabs(world->surface_edges.values[i]), (double)world->edge_count, SRC("surface edge"));
  for (size_t i = 0; i < world->leaf_faces.count; i++)
    qa_q1_check_index(world->leaf_faces.values[i], (double)world->face_count, SRC("leaf face"));
  for (size_t i = 0; i < world->texture_info_count; i++) {
    if (world->texture_count > 0)
      qa_q1_check_index(world->texture_info[i].texture, (double)world->texture_count, SRC("miptex"));
  }
  double lighting_count = (double)map->monochrome_lighting.length;
  double samples = world->lighting.is_rgb8 ? (double)world->lighting.samples.length / 3
                                            : (double)world->lighting.samples.length;
  if (samples > lighting_count) lighting_count = samples;
  if ((double)map->bspx_metadata.hdr_lighting_count > lighting_count)
    lighting_count = (double)map->bspx_metadata.hdr_lighting_count;
  for (size_t i = 0; i < world->face_count; i++) {
    const qa_bsp_face *face = &world->faces[i];
    qa_q1_check_index(face->plane, (double)world->plane_count, SRC("face plane"));
    qa_q1_check_index(face->texture_info, (double)world->texture_info_count, SRC("face texture info"));
    qa_q1_check_range(face->edges, (double)world->surface_edges.count, SRC("face edges"));
    if (face->has_lighting_offset && lighting_count > 0)
      qa_q1_check_index(face->lighting_offset, lighting_count, SRC("face lighting"));
  }
  for (size_t i = 0; i < world->leaf_count; i++) {
    const qa_q1_leaf *leaf = &world->leaves[i];
    qa_q1_check_range(leaf->faces, (double)world->leaf_faces.count, SRC("leaf faces"));
    if (leaf->has_visibility_offset && world->visibility.length > 0)
      qa_q1_check_index(leaf->visibility_offset, (double)world->visibility.length, SRC("leaf visibility"));
  }
  for (size_t i = 0; i < world->node_count; i++) {
    const qa_bsp_node *node = &world->nodes[i];
    qa_q1_check_index(node->plane, (double)world->plane_count, SRC("node plane"));
    qa_q1_check_range(node->faces, (double)world->face_count, SRC("node faces"));
    for (int j = 0; j < 2; j++) {
      qa_q1_check_index(node->children[j].index,
                        node->children[j].is_node ? (double)world->node_count : (double)world->leaf_count,
                        SRC("node child"));
    }
  }
  for (size_t i = 0; i < world->clipnode_count; i++) {
    const qa_q1_clip_node *node = &world->clipnodes[i];
    qa_q1_check_index(node->plane, (double)world->plane_count, SRC("clipnode plane"));
    for (int j = 0; j < 2; j++) {
      if (node->children[j].is_clipnode)
        qa_q1_check_index(node->children[j].value, (double)world->clipnode_count, SRC("clipnode child"));
    }
  }
  for (size_t i = 0; i < world->model_count; i++) {
    const qa_q1_world_model *model = &world->models[i];
    qa_q1_check_range(model->faces, (double)world->face_count, SRC("model faces"));
    qa_index_range visible = {0, model->visible_leaves};
    double max_leaves = (double)world->leaf_count - 1 > 0 ? (double)world->leaf_count - 1 : 0;
    qa_q1_check_range(visible, max_leaves, SRC("model visible leaves"));
    for (size_t hull = 0; hull < model->headnodes.count; hull++) {
      double headnode = model->headnodes.values[hull];
      if (hull == 0) {
        if (headnode >= 0) qa_q1_check_index(headnode, (double)world->node_count, SRC("model headnode"));
        else qa_q1_check_index(-1 - headnode, (double)world->leaf_count, SRC("model head leaf"));
      } else if (hull < 3 && headnode >= 0 && (world->clipnode_count > 0 || world->brush_list == NULL)) {
        qa_q1_check_index(headnode, (double)world->clipnode_count, SRC("model clip headnode"));
      }
    }
  }
#undef SRC
}

void qa_q1_read_bsp(const uint8_t *data, size_t data_length, const qa_q1_map_options *options_in,
                    qa_q1_map *out) {
  qa_q1_map_options empty_options;
  memset(&empty_options, 0, sizeof empty_options);
  const qa_q1_map_options *options = options_in != NULL ? options_in : &empty_options;
  memset(out, 0, sizeof *out);
  const char *source = options->source != NULL ? options->source : "<Quake BSP>";
  out->source = xstrdup(source);
  qa_binary_reader reader = make_reader(data, data_length, source);
  uint32_t version = qa_binary_reader_u32(&reader);
  qa_q1_bsp_format format = format_for(version, source);
  qa_q1_bsp_format layout = format == QA_Q1_FORMAT_QUAKE64 ? QA_Q1_FORMAT_BSP29 : format;
  out->version = version;
  qa_binary_reader directory;
  section_or_die(&reader, 4, 120, &directory);
  out->lumps = xmalloc(15 * sizeof *out->lumps);
  out->lump_count = 15;
  for (int i = 0; i < 15; i++) {
    int32_t offset = qa_binary_reader_i32(&directory);
    int32_t length = qa_binary_reader_i32(&directory);
    if (length > 0 && offset < 124) qa_binary_error(source, offset, "%s overlaps BSP header", lump_names[i]);
    out->lumps[i].name = lump_names[i];
    out->lumps[i].offset = offset;
    out->lumps[i].length = length;
    qa_binary_reader section;
    section_or_die(&reader, offset, length, &section);
    out->lumps[i].data = qa_binary_reader_bytes(&section, length);
    out->lumps[i].data_length = length > 0 ? (size_t)length : 0;
    qa_binary_reader_destroy(&section);
  }
  qa_binary_reader_destroy(&directory);
  out->bspx = qa_q1_read_bspx(&reader, out->lumps, 15, &out->bspx_count);
  qa_q1_world_geometry *world = &out->world;
  world->format = qa_q1_bsp_format_name(format);
  qa_binary_reader lump;
  lump = lump_reader(out, 1);
  world->planes = qa_q1_read_planes(&lump, &world->plane_count);
  qa_binary_reader_destroy(&lump);
  lump = lump_reader(out, 3);
  world->vertices = qa_q1_read_vertices(&lump, &world->vertex_count);
  qa_binary_reader_destroy(&lump);
  qa_q1_texture_lump textures;
  lump = lump_reader(out, 2);
  qa_q1_read_textures(&lump, format == QA_Q1_FORMAT_QUAKE64, &textures);
  qa_binary_reader_destroy(&lump);
  world->textures = (const qa_q1_mip_texture *const *)textures.textures;
  world->texture_count = textures.texture_count;
  out->texture_offsets = textures.offsets;
  out->texture_offset_count = textures.offset_count;
  out->mip_offsets = textures.mip_offsets;
  out->mip_offset_count = textures.mip_offset_count;
  lump = lump_reader(out, 7);
  world->faces = qa_q1_read_faces(&lump, layout, &world->face_count);
  qa_binary_reader_destroy(&lump);
  qa_bsp_face *faces = (qa_bsp_face *)world->faces;
  if (format == QA_Q1_FORMAT_QUAKE64) {
    for (size_t i = 0; i < world->face_count; i++) {
      if (!faces[i].has_lighting_offset) continue;
      if (fmod(faces[i].lighting_offset, 2) != 0) {
        qa_binary_error(source, (int64_t)faces[i].lighting_offset, "unaligned Quake64 lighting offset");
      }
      faces[i].lighting_offset /= 2;
    }
  }
  lump = lump_reader(out, 14);
  world->models = qa_q1_read_models(&lump, &world->model_count);
  qa_binary_reader_destroy(&lump);
  double corner_count = 0;
  for (size_t i = 0; i < world->face_count; i++) corner_count += world->faces[i].edges.count;
  qa_q1_read_bspx_metadata(out->bspx, out->bspx_count, (double)world->face_count,
                           (double)world->vertex_count, corner_count, &out->bspx_metadata);
  char *entities;
  if (options->has_entities) {
    char entity_source[1024];
    snprintf(entity_source, sizeof entity_source, "%s:.ent", source);
    qa_binary_reader entity_reader = make_reader(options->entities, options->entities_length, entity_source);
    entities = qa_binary_reader_fixed_byte_string(&entity_reader, (int64_t)options->entities_length);
    qa_binary_reader_destroy(&entity_reader);
  } else {
    lump = lump_reader(out, 0);
    entities = qa_binary_reader_fixed_byte_string(&lump, (int64_t)lump.length);
    qa_binary_reader_destroy(&lump);
  }
  world->entities = entities;
  out->entity_list = qa_q1_parse_entities(entities, &out->entity_count);
  lump = lump_reader(out, 8);
  size_t stored_length = qa_binary_reader_remaining(&lump);
  uint8_t *stored_lighting = qa_binary_reader_bytes(&lump, (int64_t)stored_length);
  qa_binary_reader_destroy(&lump);
  bool quake64 = format == QA_Q1_FORMAT_QUAKE64;
  if (quake64) {
    out->monochrome_lighting.bytes = NULL;
    out->monochrome_lighting.length = 0;
  } else {
    uint8_t *mono = xmalloc(stored_length);
    if (stored_length > 0) memcpy(mono, stored_lighting, stored_length);
    out->monochrome_lighting.bytes = mono;
    out->monochrome_lighting.length = stored_length;
  }
  world->lighting = select_lighting(out->monochrome_lighting.bytes, out->monochrome_lighting.length,
                                    &out->bspx_metadata, options, quake64, stored_lighting, stored_length);
  free(stored_lighting);
  lump = lump_reader(out, 6);
  world->texture_info = qa_q1_read_texture_info(&lump, &world->texture_info_count);
  qa_binary_reader_destroy(&lump);
  lump = lump_reader(out, 5);
  world->nodes = qa_q1_read_nodes(&lump, layout, &world->node_count);
  qa_binary_reader_destroy(&lump);
  lump = lump_reader(out, 10);
  world->leaves = qa_q1_read_leaves(&lump, layout, &world->leaf_count);
  qa_binary_reader_destroy(&lump);
  lump = lump_reader(out, 12);
  world->edges = qa_q1_read_edges(&lump, layout, &world->edge_count);
  qa_binary_reader_destroy(&lump);
  lump = lump_reader(out, 9);
  world->clipnodes = qa_q1_read_clipnodes(&lump, layout, &world->clipnode_count);
  qa_binary_reader_destroy(&lump);
  lump = lump_reader(out, 13);
  size_t surface_edge_count = 0;
  double *surface_edges = qa_q1_read_surface_edges(&lump, &surface_edge_count);
  qa_binary_reader_destroy(&lump);
  world->surface_edges.values = surface_edges;
  world->surface_edges.count = surface_edge_count;
  lump = lump_reader(out, 11);
  size_t leaf_face_count = 0;
  double *leaf_faces = qa_q1_read_leaf_faces(&lump, layout, &leaf_face_count);
  qa_binary_reader_destroy(&lump);
  world->leaf_faces.values = leaf_faces;
  world->leaf_faces.count = leaf_face_count;
  lump = lump_reader(out, 4);
  size_t visibility_length = qa_binary_reader_remaining(&lump);
  world->visibility.bytes = qa_binary_reader_bytes(&lump, (int64_t)visibility_length);
  world->visibility.length = visibility_length;
  qa_binary_reader_destroy(&lump);
  const uint8_t *bspx_data = NULL;
  size_t bspx_length = 0;
  bool has = qa_q1_bspx_data(out->bspx, out->bspx_count, "DECOUPLED_LM", &bspx_data, &bspx_length);
  qa_decoupled_lightmap *decoupled = NULL;
  size_t decoupled_count = 0;
  if (qa_q1_read_decoupled_lightmaps(has, bspx_data, bspx_length, (double)world->face_count,
                                     &decoupled, &decoupled_count)) {
    world->decoupled_lightmaps = decoupled;
    world->decoupled_lightmap_count = decoupled_count;
  }
  has = qa_q1_bspx_data(out->bspx, out->bspx_count, "BRUSHLIST", &bspx_data, &bspx_length);
  qa_q1_brush_list_model *brush_list = NULL;
  size_t brush_list_count = 0;
  if (qa_q1_read_brush_list(has, bspx_data, bspx_length, (double)world->model_count,
                            &brush_list, &brush_list_count)) {
    world->brush_list = brush_list;
    world->brush_list_count = brush_list_count;
  }
  qa_bsp_extension *extensions = xmalloc(out->bspx_count * sizeof *extensions);
  for (size_t i = 0; i < out->bspx_count; i++) {
    extensions[i].name = out->bspx[i].name;
    extensions[i].bytes.bytes = out->bspx[i].data;
    extensions[i].bytes.length = out->bspx[i].data_length;
  }
  world->extensions = extensions;
  world->extension_count = out->bspx_count;
  qa_binary_reader_destroy(&reader);
  validate_references(out);
}

void qa_q1_map_destroy(qa_q1_map *map) {
  if (map == NULL) return;
  qa_q1_world_geometry *world = &map->world;
  free(map->source);
  for (size_t i = 0; i < map->lump_count; i++) free(map->lumps[i].data);
  free(map->lumps);
  qa_q1_bspx_metadata_destroy(&map->bspx_metadata);
  qa_q1_entities_destroy(map->entity_list, map->entity_count);
  free((void *)world->entities);
  free((void *)world->planes);
  free((void *)world->vertices);
  free((void *)world->edges);
  free((void *)world->surface_edges.values);
  free((void *)world->nodes);
  free((void *)world->leaves);
  free((void *)world->leaf_faces.values);
  qa_q1_texture_lump lump;
  lump.textures = (qa_q1_mip_texture **)world->textures;
  lump.texture_count = world->texture_count;
  lump.offsets = map->texture_offsets;
  lump.offset_count = map->texture_offset_count;
  lump.mip_offsets = map->mip_offsets;
  lump.mip_offset_count = map->mip_offset_count;
  qa_q1_texture_lump_destroy(&lump);
  free((void *)world->texture_info);
  for (size_t i = 0; i < world->face_count; i++) free((void *)world->faces[i].styles.values);
  free((void *)world->faces);
  for (size_t i = 0; i < world->model_count; i++) free((void *)world->models[i].headnodes.values);
  free((void *)world->models);
  free((void *)world->clipnodes);
  free((void *)world->visibility.bytes);
  free((void *)world->lighting.samples.bytes);
  free((void *)map->monochrome_lighting.bytes);
  free((void *)world->decoupled_lightmaps);
  qa_q1_brush_list_destroy((qa_q1_brush_list_model *)world->brush_list, world->brush_list_count);
  free((void *)world->extensions);
  qa_q1_bspx_destroy(map->bspx, map->bspx_count);
  memset(map, 0, sizeof *map);
}
