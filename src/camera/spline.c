/* Port of src/camera/spline.ts. GPL-2.0-or-later. */
#include "qa/camera/spline.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/math.h"
#include "qa/foundation/support.h"

/* TS throws plain Error in this module; abort with identical message. */
static noreturn void camera_error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  fprintf(stderr, "Error: ");
  vfprintf(stderr, format, args);
  fprintf(stderr, "\n");
  va_end(args);
  abort();
}

static void *xmalloc(size_t size) {
  void *p = malloc(size ? size : 1);
  if (p == NULL) abort();
  return p;
}
static void *xrealloc(void *p, size_t size) {
  void *q = realloc(p, size ? size : 1);
  if (q == NULL) abort();
  return q;
}
static char *xstrdup(const char *s) {
  size_t n = strlen(s) + 1;
  char *p = xmalloc(n);
  memcpy(p, s, n);
  return p;
}
static char *xdup_range(const char *first, size_t length) {
  char *p = xmalloc(length + 1);
  memcpy(p, first, length);
  p[length] = '\0';
  return p;
}
static float fr(double v) { return (float)v; }

/* JS Number(string), restricted to ASCII whitespace trimming. */
static double js_number(const char *s) {
  while (isspace((unsigned char)*s)) s++;
  size_t len = strlen(s);
  while (len > 0 && isspace((unsigned char)s[len - 1])) len--;
  if (len == 0) return 0;
  char *tmp = xdup_range(s, len);
  double result;
  if (strcmp(tmp, "Infinity") == 0 || strcmp(tmp, "+Infinity") == 0) {
    result = INFINITY;
  } else if (strcmp(tmp, "-Infinity") == 0) {
    result = -INFINITY;
  } else if (len > 2 && tmp[0] == '0' &&
             (tmp[1] == 'x' || tmp[1] == 'X' || tmp[1] == 'b' || tmp[1] == 'B' ||
              tmp[1] == 'o' || tmp[1] == 'O')) {
    int base = (tmp[1] == 'x' || tmp[1] == 'X') ? 16
               : (tmp[1] == 'b' || tmp[1] == 'B') ? 2 : 8;
    result = 0;
    bool ok = true;
    for (size_t i = 2; i < len; i++) {
      int d;
      if (tmp[i] >= '0' && tmp[i] <= '9') d = tmp[i] - '0';
      else if (tmp[i] >= 'a' && tmp[i] <= 'f') d = tmp[i] - 'a' + 10;
      else if (tmp[i] >= 'A' && tmp[i] <= 'F') d = tmp[i] - 'A' + 10;
      else { ok = false; break; }
      if (d >= base) { ok = false; break; }
      result = result * base + d;
    }
    if (!ok) result = NAN;
  } else if (isalpha((unsigned char)tmp[0]) ||
             ((tmp[0] == '+' || tmp[0] == '-') &&
              isalpha((unsigned char)tmp[1]))) {
    result = NAN; /* JS rejects inf/nan spellings strtod would accept */
  } else {
    char *end = NULL;
    result = strtod(tmp, &end);
    if (end == tmp || *end != '\0') result = NAN;
  }
  free(tmp);
  return result;
}

static void format_number(double value, char *buf, size_t size) {
  if (isnan(value)) { snprintf(buf, size, "NaN"); return; }
  if (isinf(value)) { snprintf(buf, size, value < 0 ? "-Infinity" : "Infinity"); return; }
  if (value == 0) { snprintf(buf, size, "0"); return; } /* JS String(-0)=="0" */
  if (value == trunc(value) && fabs(value) < 1e21) {
    snprintf(buf, size, "%.0f", value);
    return;
  }
  for (int prec = 1; prec <= 17; prec++) {
    snprintf(buf, size, "%.*g", prec, value);
    if (strtod(buf, NULL) == value) return;
  }
  snprintf(buf, size, "%.17g", value);
}

/* ---- Tokens ---- */
typedef struct tokens {
  char **items;
  size_t count;
  size_t index;
  char *scratch; /* next() result */
  char *lower;   /* next().toLowerCase() result */
} tokens;

static void tokens_push(tokens *t, char ***arr, size_t *count, size_t *cap,
                        const char *first, size_t length) {
  (void)t;
  if (*count == *cap) {
    *cap = *cap ? *cap * 2 : 16;
    *arr = xrealloc(*arr, *cap * sizeof **arr);
  }
  (*arr)[(*count)++] = xdup_range(first, length);
}

static void tokens_init(tokens *t, const char *text) {
  char **raw = NULL;
  size_t count = 0, cap = 0;
  const char *p = text;
  while (*p != '\0') {
    if (isspace((unsigned char)*p)) { p++; continue; }
    if (p[0] == '/' && p[1] == '*') {
      const char *e = strstr(p + 2, "*/");
      if (e != NULL) { tokens_push(t, &raw, &count, &cap, p, (size_t)(e + 2 - p)); p = e + 2; continue; }
    }
    if (p[0] == '/' && p[1] == '/') {
      const char *e = p;
      while (*e != '\0' && *e != '\n') e++;
      tokens_push(t, &raw, &count, &cap, p, (size_t)(e - p));
      p = e;
      continue;
    }
    if (*p == '"') {
      const char *e = p + 1;
      while (*e != '\0' && *e != '"' && *e != '\r' && *e != '\n') e++;
      if (*e == '"') { tokens_push(t, &raw, &count, &cap, p, (size_t)(e + 1 - p)); p = e + 1; continue; }
    }
    if (strchr("{}()", *p) != NULL) {
      tokens_push(t, &raw, &count, &cap, p, 1);
      p++;
      continue;
    }
    const char *e = p;
    while (*e != '\0' && !isspace((unsigned char)*e) &&
           strchr("{}()", *e) == NULL)
      e++;
    tokens_push(t, &raw, &count, &cap, p, (size_t)(e - p));
    p = e;
  }
  /* TS filter: drop comment tokens (line and block) */
  size_t kept = 0;
  for (size_t i = 0; i < count; i++) {
    if (strncmp(raw[i], "//", 2) == 0 || strncmp(raw[i], "/*", 2) == 0)
      free(raw[i]);
    else
      raw[kept++] = raw[i];
  }
  t->items = raw;
  t->count = kept;
  t->index = 0;
  t->scratch = NULL;
  t->lower = NULL;
}

static void tokens_destroy(tokens *t) {
  for (size_t i = 0; i < t->count; i++) free(t->items[i]);
  free(t->items);
  free(t->scratch);
  free(t->lower);
}

static const char *tokens_peek(const tokens *t) {
  return t->index < t->count ? t->items[t->index] : NULL;
}

static const char *tokens_next(tokens *t) {
  if (t->index >= t->count) camera_error("Unexpected end of camera file");
  const char *value = t->items[t->index++];
  free(t->scratch);
  if (value[0] == '"') {
    size_t len = strlen(value);
    /* value.slice(1, -1) */
    t->scratch = xdup_range(value + 1, len >= 2 ? len - 2 : 0);
  } else {
    t->scratch = xstrdup(value);
  }
  return t->scratch;
}

static const char *tokens_next_lower(tokens *t) {
  const char *value = tokens_next(t);
  free(t->lower);
  t->lower = xstrdup(value);
  for (char *c = t->lower; *c != '\0'; c++)
    *c = (char)tolower((unsigned char)*c);
  return t->lower;
}

static void tokens_expect(tokens *t, const char *value) {
  if (strcmp(tokens_next_lower(t), value) != 0)
    camera_error("Expected camera token %s", value);
}

static double tokens_number(tokens *t) {
  const char *token = tokens_next(t);
  double value = js_number(token);
  bool blank = true;
  for (const char *c = token; *c != '\0'; c++)
    if (!isspace((unsigned char)*c)) blank = false;
  if (blank || !isfinite(value))
    camera_error("Invalid camera number %s", token);
  return value;
}

static qa_vec3 tokens_vector(tokens *t) {
  tokens_expect(t, "(");
  qa_vec3 point;
  point.x = tokens_number(t);
  point.y = tokens_number(t);
  point.z = tokens_number(t);
  tokens_expect(t, ")");
  return point;
}

static void destroy_position(qa_camera_position *position) {
  free(position->name);
  free(position->velocities);
  free(position->points);
  position->name = NULL;
  position->velocities = NULL;
  position->points = NULL;
  position->velocity_count = 0;
  position->point_count = 0;
}

void qa_camera_definition_destroy(qa_camera_definition *definition) {
  destroy_position(&definition->position);
  for (size_t i = 0; i < definition->target_count; i++)
    destroy_position(&definition->targets[i]);
  free(definition->targets);
  for (size_t i = 0; i < definition->event_count; i++)
    free(definition->events[i].param);
  free(definition->events);
  definition->targets = NULL;
  definition->events = NULL;
  definition->target_count = 0;
  definition->event_count = 0;
}

static void parse_position(tokens *t, qa_camera_position_kind kind,
                           qa_camera_position *out) {
  static const qa_vec3 zero = {0, 0, 0};
  tokens_expect(t, "{");
  char *name = xstrdup("position");
  double time = 0, base_velocity = 0, granularity = 0.025;
  qa_vec3 point = zero, start = zero, end = zero;
  qa_camera_velocity *velocities = NULL;
  size_t velocity_count = 0, velocity_cap = 0;
  qa_vec3 *points = NULL;
  size_t point_count = 0, point_cap = 0;
  const char *peek;
  while ((peek = tokens_peek(t)) == NULL || strcmp(peek, "}") != 0) {
    const char *key = tokens_next_lower(t);
    if (strcmp(key, "name") == 0) {
      char *next_name = xstrdup(tokens_next(t));
      free(name);
      name = next_name;
    } else if (strcmp(key, "type") == 0) {
      tokens_number(t);
    } else if (strcmp(key, "time") == 0) {
      time = tokens_number(t);
    } else if (strcmp(key, "basevelocity") == 0) {
      base_velocity = tokens_number(t);
    } else if (strcmp(key, "velocity") == 0) {
      qa_camera_velocity v;
      v.start = tokens_number(t);
      v.duration = tokens_number(t);
      v.speed = tokens_number(t);
      if (velocity_count == velocity_cap) {
        velocity_cap = velocity_cap ? velocity_cap * 2 : 4;
        velocities = xrealloc(velocities, velocity_cap * sizeof *velocities);
      }
      velocities[velocity_count++] = v;
    } else if (strcmp(key, "pos") == 0) {
      point = tokens_vector(t);
    } else if (strcmp(key, "startpos") == 0) {
      start = tokens_vector(t);
    } else if (strcmp(key, "endpos") == 0) {
      end = tokens_vector(t);
    } else if (strcmp(key, "target") == 0) {
      tokens_expect(t, "{");
      const char *inner;
      while ((inner = tokens_peek(t)) == NULL || strcmp(inner, "}") != 0) {
        if (inner != NULL && strcmp(inner, "(") == 0) {
          qa_vec3 p = tokens_vector(t);
          if (point_count == point_cap) {
            point_cap = point_cap ? point_cap * 2 : 4;
            points = xrealloc(points, point_cap * sizeof *points);
          }
          points[point_count++] = p;
        } else {
          const char *property = tokens_next_lower(t);
          if (strcmp(property, "granularity") == 0)
            granularity = tokens_number(t);
          else if (strcmp(property, "name") == 0)
            tokens_next(t);
          else
            camera_error("Unknown spline property %s", property);
        }
      }
      tokens_expect(t, "}");
    } else {
      camera_error("Unknown camera position property %s", key);
    }
  }
  tokens_expect(t, "}");
  bool negative = time < 0;
  for (size_t i = 0; i < velocity_count; i++)
    if (velocities[i].start < 0 || velocities[i].duration < 0 ||
        velocities[i].speed < 0)
      negative = true;
  if (negative) camera_error("Negative camera timing or speed");
  out->kind = kind;
  out->name = name;
  out->time = time;
  out->base_velocity = base_velocity;
  out->velocities = velocities;
  out->velocity_count = velocity_count;
  out->point = point;
  out->start = start;
  out->end = end;
  out->granularity = granularity;
  out->points = points;
  out->point_count = point_count;
  if (kind == QA_CAMERA_POSITION_SPLINE &&
      (granularity < 0.0001 || granularity > 1 || point_count < 4))
    camera_error(
        "Spline requires four points and granularity between 0.0001 and 1");
}

static bool ascii_iequals(const char *a, const char *b) {
  while (*a != '\0' && *b != '\0') {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    a++;
    b++;
  }
  return *a == *b;
}

void qa_parse_camera(const char *text, qa_camera_definition *out) {
  tokens t;
  tokens_init(&t, text);
  const char *first = tokens_peek(&t);
  if (first != NULL && (ascii_iequals(first, "camera") ||
                        ascii_iequals(first, "camerapathdef")))
    tokens_next(&t);
  tokens_expect(&t, "{");
  double seconds = 30;
  bool has_camera = false;
  qa_camera_position camera;
  memset(&camera, 0, sizeof camera);
  double value = 90, start = 90, end = 90, time = 0;
  qa_camera_position *targets = NULL;
  size_t target_count = 0, target_cap = 0;
  qa_camera_event *events = NULL;
  size_t event_count = 0, event_cap = 0;
  const char *peek;
  while ((peek = tokens_peek(&t)) == NULL || strcmp(peek, "}") != 0) {
    const char *key = tokens_next_lower(&t);
    if (strcmp(key, "time") == 0) {
      seconds = tokens_number(&t);
    } else if (strcmp(key, "camera_fixed") == 0 ||
               strcmp(key, "camera_interpolated") == 0 ||
               strcmp(key, "camera_spline") == 0) {
      qa_camera_position_kind kind =
          strcmp(key, "camera_fixed") == 0 ? QA_CAMERA_POSITION_FIXED
          : strcmp(key, "camera_interpolated") == 0
              ? QA_CAMERA_POSITION_INTERPOLATED
              : QA_CAMERA_POSITION_SPLINE;
      qa_camera_position parsed;
      parse_position(&t, kind, &parsed);
      if (has_camera) destroy_position(&camera);
      camera = parsed;
      has_camera = true;
    } else if (strcmp(key, "target_fixed") == 0 ||
               strcmp(key, "target_interpolated") == 0 ||
               strcmp(key, "target_spline") == 0) {
      qa_camera_position_kind kind =
          strcmp(key, "target_fixed") == 0 ? QA_CAMERA_POSITION_FIXED
          : strcmp(key, "target_interpolated") == 0
              ? QA_CAMERA_POSITION_INTERPOLATED
              : QA_CAMERA_POSITION_SPLINE;
      if (target_count == target_cap) {
        target_cap = target_cap ? target_cap * 2 : 4;
        targets = xrealloc(targets, target_cap * sizeof *targets);
      }
      parse_position(&t, kind, &targets[target_count++]);
    } else if (strcmp(key, "event") == 0) {
      double type = 0, event_time = 0;
      char *param = xstrdup("");
      tokens_expect(&t, "{");
      const char *inner;
      while ((inner = tokens_peek(&t)) == NULL || strcmp(inner, "}") != 0) {
        const char *field = tokens_next_lower(&t);
        if (strcmp(field, "type") == 0) type = tokens_number(&t);
        else if (strcmp(field, "param") == 0) {
          char *next_param = xstrdup(tokens_next(&t));
          free(param);
          param = next_param;
        } else if (strcmp(field, "time") == 0) event_time = tokens_number(&t);
        else camera_error("Unknown camera event property %s", field);
      }
      tokens_expect(&t, "}");
      if (type != trunc(type) || type < 0 || type > 9 || event_time < 0)
        camera_error("Invalid camera event");
      if (type == 1 && (!isfinite(js_number(param)) || js_number(param) < 0))
        camera_error("Invalid camera wait duration");
      if (event_count == event_cap) {
        event_cap = event_cap ? event_cap * 2 : 4;
        events = xrealloc(events, event_cap * sizeof *events);
      }
      events[event_count].type = type;
      events[event_count].param = param;
      events[event_count].time = event_time;
      event_count++;
    } else if (strcmp(key, "fov") == 0) {
      tokens_expect(&t, "{");
      const char *inner;
      while ((inner = tokens_peek(&t)) == NULL || strcmp(inner, "}") != 0) {
        const char *field = tokens_next_lower(&t);
        if (strcmp(field, "fov") == 0) value = tokens_number(&t);
        else if (strcmp(field, "startfov") == 0) start = tokens_number(&t);
        else if (strcmp(field, "endfov") == 0) end = tokens_number(&t);
        else if (strcmp(field, "time") == 0) time = tokens_number(&t);
        else camera_error("Unknown camera FOV property %s", field);
      }
      tokens_expect(&t, "}");
    } else {
      camera_error("Unknown camera property %s", key);
    }
  }
  tokens_expect(&t, "}");
  bool bad_fov = value <= 0 || value >= 180 || start <= 0 || start >= 180 ||
                 end <= 0 || end >= 180;
  if (tokens_peek(&t) != NULL || !has_camera || seconds <= 0 || time < 0 ||
      bad_fov)
    camera_error("Invalid camera definition");
  for (size_t i = 0; i < event_count; i++) {
    if (events[i].type != 4) continue;
    bool found = false;
    for (size_t j = 0; j < target_count; j++)
      if (strcmp(targets[j].name, events[i].param) == 0) found = true;
    if (!found) camera_error("Unknown camera target %s", events[i].param);
  }
  tokens_destroy(&t);
  out->seconds = seconds;
  out->position = camera;
  out->targets = targets;
  out->target_count = target_count;
  out->events = events;
  out->event_count = event_count;
  out->fov.value = value;
  out->fov.start = start;
  out->fov.end = end;
  out->fov.time = time;
}

/* ---- serialize ---- */
typedef struct strbuf {
  char *data;
  size_t length;
  size_t capacity;
  bool oom;
} strbuf;

static void sb_append(strbuf *sb, const char *text) {
  if (sb->oom) return;
  size_t n = strlen(text);
  if (sb->length + n + 1 > sb->capacity) {
    size_t cap = sb->capacity ? sb->capacity : 256;
    while (cap < sb->length + n + 1) cap *= 2;
    char *grown = realloc(sb->data, cap);
    if (grown == NULL) { sb->oom = true; return; }
    sb->data = grown;
    sb->capacity = cap;
  }
  memcpy(sb->data + sb->length, text, n + 1);
  sb->length += n;
}

static void sb_appendf(strbuf *sb, const char *format, ...) {
  char buf[512];
  va_list args;
  va_start(args, format);
  vsnprintf(buf, sizeof buf, format, args);
  va_end(args);
  sb_append(sb, buf);
}

static void sb_number(strbuf *sb, double value) {
  char buf[64];
  format_number(value, buf, sizeof buf);
  sb_append(sb, buf);
}

static void sb_quote(strbuf *sb, const char *value) {
  if (strpbrk(value, "\"\r\n") != NULL)
    camera_error("Camera names cannot contain quotes or newlines");
  sb_append(sb, "\"");
  sb_append(sb, value);
  sb_append(sb, "\"");
}

static void sb_vector(strbuf *sb, qa_vec3 value) {
  char x[64], y[64], z[64];
  format_number(value.x, x, sizeof x);
  format_number(value.y, y, sizeof y);
  format_number(value.z, z, sizeof z);
  sb_appendf(sb, "( %s %s %s )", x, y, z);
}

static const char *kind_name(qa_camera_position_kind kind) {
  switch (kind) {
    case QA_CAMERA_POSITION_FIXED: return "fixed";
    case QA_CAMERA_POSITION_INTERPOLATED: return "interpolated";
    case QA_CAMERA_POSITION_SPLINE: return "spline";
  }
  return "fixed"; /* unreachable for valid enums */
}

static void sb_write_position(strbuf *sb, const qa_camera_position *path,
                              const char *prefix) {
  sb_appendf(sb, "%s_%s {\nname ", prefix, kind_name(path->kind));
  sb_quote(sb, path->name ? path->name : "");
  sb_append(sb, "\ntime ");
  sb_number(sb, path->time);
  sb_append(sb, "\nbaseVelocity ");
  sb_number(sb, path->base_velocity);
  sb_append(sb, "\n");
  for (size_t i = 0; i < path->velocity_count; i++) {
    char a[64], b[64], c[64];
    format_number(path->velocities[i].start, a, sizeof a);
    format_number(path->velocities[i].duration, b, sizeof b);
    format_number(path->velocities[i].speed, c, sizeof c);
    sb_appendf(sb, "velocity %s %s %s\n", a, b, c);
  }
  if (path->kind == QA_CAMERA_POSITION_FIXED) {
    sb_append(sb, "pos ");
    sb_vector(sb, path->point);
    sb_append(sb, "\n");
  } else if (path->kind == QA_CAMERA_POSITION_INTERPOLATED) {
    sb_append(sb, "startPos ");
    sb_vector(sb, path->start);
    sb_append(sb, "\nendPos ");
    sb_vector(sb, path->end);
    sb_append(sb, "\n");
  } else {
    sb_append(sb, "target {\ngranularity ");
    sb_number(sb, path->granularity);
    sb_append(sb, "\n");
    for (size_t i = 0; i < path->point_count; i++) {
      if (i > 0) sb_append(sb, "\n");
      sb_vector(sb, path->points[i]);
    }
    sb_append(sb, "\n}\n");
  }
  sb_append(sb, "}\n");
}

char *qa_serialize_camera(const qa_camera_definition *camera) {
  strbuf sb = {NULL, 0, 0, false};
  sb_append(&sb, "cameraPathDef {\ntime ");
  sb_number(&sb, camera->seconds);
  sb_append(&sb, "\n");
  sb_write_position(&sb, &camera->position, "camera");
  for (size_t i = 0; i < camera->target_count; i++)
    sb_write_position(&sb, &camera->targets[i], "target");
  for (size_t i = 0; i < camera->event_count; i++) {
    sb_append(&sb, "event {\ntype ");
    sb_number(&sb, camera->events[i].type);
    sb_append(&sb, "\nparam ");
    sb_quote(&sb, camera->events[i].param ? camera->events[i].param : "");
    sb_append(&sb, "\ntime ");
    sb_number(&sb, camera->events[i].time);
    sb_append(&sb, "\n}\n");
  }
  sb_append(&sb, "fov {\nfov ");
  sb_number(&sb, camera->fov.value);
  sb_append(&sb, "\nstartFOV ");
  sb_number(&sb, camera->fov.start);
  sb_append(&sb, "\nendFOV ");
  sb_number(&sb, camera->fov.end);
  sb_append(&sb, "\ntime ");
  sb_number(&sb, camera->fov.time);
  sb_append(&sb, "\n}\n}\n");
  if (sb.oom) { free(sb.data); return NULL; }
  return sb.data;
}

/* ---- playback ---- */
static bool spline_points(const qa_camera_position *path, qa_vec3 **out,
                          size_t *out_count) {
  qa_vec3 *result = NULL;
  size_t count = 0, cap = 0;
  for (size_t i = 3; i < path->point_count; i++) {
    for (double t = 0; t < 1.001; t = (double)fr(t + path->granularity)) {
      double weights[4] = {
          (double)fr(pow(1 - t, 3) / 6),
          (double)fr((3 * pow(t, 3) - 6 * pow(t, 2) + 4) / 6),
          (double)fr((-3 * pow(t, 3) + 3 * pow(t, 2) + 3 * t + 1) / 6),
          (double)fr(pow(t, 3) / 6)};
      qa_vec3 point = {0, 0, 0};
      for (int j = 0; j < 4; j++) {
        size_t index = i - 3 + (size_t)j;
        if (index >= path->point_count)
          camera_error("Invalid spline control span");
        qa_vec3 source = path->points[index];
        double weight = weights[j];
        point.x = (double)fr(point.x + (double)fr(source.x * weight));
        point.y = (double)fr(point.y + (double)fr(source.y * weight));
        point.z = (double)fr(point.z + (double)fr(source.z * weight));
      }
      if (count == cap) {
        cap = cap ? cap * 2 : 16;
        qa_vec3 *grown = realloc(result, cap * sizeof *grown);
        if (grown == NULL) { free(result); return false; }
        result = grown;
      }
      result[count++] = point;
    }
  }
  *out = result;
  *out_count = count;
  return true;
}

static bool position_playback_init(qa_camera_position_playback *pb,
                                   const qa_camera_position *path,
                                   double duration,
                                   const qa_camera_velocity *velocities,
                                   size_t velocity_count) {
  memset(pb, 0, sizeof *pb);
  pb->path = path;
  pb->duration = duration;
  if (path->kind == QA_CAMERA_POSITION_SPLINE) {
    if (!spline_points(path, &pb->points, &pb->point_count)) return false;
  } else if (path->kind == QA_CAMERA_POSITION_FIXED) {
    pb->points = malloc(sizeof *pb->points);
    if (pb->points == NULL) return false;
    pb->points[0] = path->point;
    pb->point_count = 1;
  } else {
    pb->points = malloc(2 * sizeof *pb->points);
    if (pb->points == NULL) return false;
    pb->points[0] = path->start;
    pb->points[1] = path->end;
    pb->point_count = 2;
  }
  size_t distance_count = pb->point_count ? pb->point_count : 1;
  pb->distances = malloc(distance_count * sizeof *pb->distances);
  if (pb->distances == NULL) { free(pb->points); pb->points = NULL; return false; }
  pb->distance_count = distance_count;
  double distance = 0;
  pb->distances[0] = 0;
  for (size_t i = 1; i < pb->point_count; i++) {
    distance += qa_length3(qa_sub3(pb->points[i], pb->points[i - 1]));
    pb->distances[i] = distance;
  }
  pb->distance = distance;
  if (velocity_count > 0) {
    pb->velocities = malloc(velocity_count * sizeof *pb->velocities);
    if (pb->velocities == NULL) {
      free(pb->points);
      free(pb->distances);
      pb->points = NULL;
      pb->distances = NULL;
      return false;
    }
    memcpy(pb->velocities, velocities,
           velocity_count * sizeof *pb->velocities);
    pb->velocity_count = velocity_count;
  }
  return true;
}

static void position_playback_destroy(qa_camera_position_playback *pb) {
  free(pb->points);
  free(pb->distances);
  free(pb->velocities);
  pb->points = NULL;
  pb->distances = NULL;
  pb->velocities = NULL;
}

static void position_playback_start(qa_camera_position_playback *pb,
                                    double time, bool has_duration,
                                    double duration) {
  pb->start_time = time;
  pb->last_time = time;
  pb->traveled = 0;
  if (has_duration) pb->duration = duration;
}

static qa_vec3 position_playback_sample(qa_camera_position_playback *pb,
                                        double time) {
  const qa_camera_position *path = pb->path;
  if (path->kind == QA_CAMERA_POSITION_FIXED) return path->point;
  if (path->kind == QA_CAMERA_POSITION_INTERPOLATED) {
    double elapsed = time - pb->start_time;
    double velocity = pb->distance / (pb->duration / 1000);
    for (size_t i = 0; i < pb->velocity_count; i++) {
      if (elapsed >= pb->velocities[i].start &&
          elapsed <= pb->velocities[i].start + pb->velocities[i].duration) {
        velocity = pb->velocities[i].speed;
        break;
      }
    }
    pb->traveled += fmax(0, time - pb->last_time) / 1000 * velocity;
    pb->last_time = time;
    double fraction = pb->distance == 0
                          ? 0
                          : fmax(0, fmin(1, pb->traveled / pb->distance));
    return qa_add3(qa_scale3(path->start, 1 - fraction),
                   qa_scale3(path->end, fraction));
  }
  double desired = pb->duration == 0
                       ? pb->distance
                       : (time - pb->start_time) / pb->duration * pb->distance;
  long index = -1;
  for (size_t i = 0; i < pb->distance_count; i++) {
    if (pb->distances[i] >= desired) { index = (long)i; break; }
  }
  if (index < 0) index = (long)pb->point_count - 1;
  bool have_low = index - 1 >= 0 && index - 1 < (long)pb->point_count;
  bool have_high = index + 1 >= 0 && index + 1 < (long)pb->point_count;
  bool have_lo = index - 1 >= 0 && index - 1 < (long)pb->distance_count;
  bool have_hi = index + 1 >= 0 && index + 1 < (long)pb->distance_count;
  if (have_low && have_high && have_lo && have_hi) {
    double lo = pb->distances[index - 1], hi = pb->distances[index + 1];
    if (hi > lo) {
      double fraction = (desired - lo) / (hi - lo);
      return qa_add3(qa_scale3(pb->points[index - 1], 1 - fraction),
                     qa_scale3(pb->points[index + 1], fraction));
    }
  }
  if (index >= 0 && index < (long)pb->point_count)
    return pb->points[index];
  qa_vec3 zero = {0, 0, 0};
  return zero;
}

static long find_target(const qa_camera_definition *definition,
                        const char *param) {
  for (size_t i = 0; i < definition->target_count; i++)
    if (definition->targets[i].name != NULL &&
        strcmp(definition->targets[i].name, param) == 0)
      return (long)i;
  return -1;
}

bool qa_camera_playback_init(qa_camera_playback *playback,
                             const qa_camera_definition *definition,
                             double start_time) {
  memset(playback, 0, sizeof *playback);
  playback->definition = definition;
  playback->start_time = start_time;
  playback->active_target = 0;
  double total = definition->seconds * 1000;
  size_t wait_count = 0;
  for (size_t i = 0; i < definition->event_count; i++) {
    if (definition->events[i].type != 1) continue;
    total += js_number(definition->events[i].param) * 1000;
    wait_count++;
  }
  playback->total_milliseconds = total;
  size_t velocity_count = definition->position.velocity_count + wait_count;
  qa_camera_velocity *velocities = NULL;
  if (velocity_count > 0) {
    velocities = malloc(velocity_count * sizeof *velocities);
    if (velocities == NULL) return false;
    size_t n = 0;
    for (size_t i = 0; i < definition->position.velocity_count; i++)
      velocities[n++] = definition->position.velocities[i];
    for (size_t i = 0; i < definition->event_count; i++) {
      if (definition->events[i].type != 1) continue;
      velocities[n].start = definition->events[i].time;
      velocities[n].duration = js_number(definition->events[i].param) * 1000;
      velocities[n].speed = 0;
      n++;
    }
  }
  bool ok = position_playback_init(&playback->camera, &definition->position,
                                   definition->seconds * 1000, velocities,
                                   velocity_count);
  free(velocities);
  if (!ok) return false;
  position_playback_start(&playback->camera, start_time, false, 0);
  playback->last_time = start_time;
  playback->target_count = definition->target_count;
  if (definition->target_count > 0) {
    playback->targets =
        calloc(definition->target_count, sizeof *playback->targets);
    if (playback->targets == NULL) goto oom;
    for (size_t i = 0; i < definition->target_count; i++) {
      const qa_camera_position *target = &definition->targets[i];
      /* target.time || totalMilliseconds */
      double duration =
          (target->time == 0 || isnan(target->time)) ? total : target->time;
      if (!position_playback_init(&playback->targets[i], target, duration,
                                  target->velocities, target->velocity_count))
        goto oom;
    }
    for (size_t i = 0; i < definition->target_count; i++)
      position_playback_start(&playback->targets[i], start_time, false, 0);
  }
  if (definition->event_count > 0) {
    playback->triggered = calloc(definition->event_count, sizeof(bool));
    playback->sample_events =
        malloc(definition->event_count * sizeof *playback->sample_events);
    if (playback->triggered == NULL || playback->sample_events == NULL)
      goto oom;
    playback->sample_event_capacity = definition->event_count;
  }
  {
    double time_so_far = 0;
    size_t changes_seen = 0;
    size_t total_changes = 0;
    for (size_t i = 0; i < definition->event_count; i++)
      if (definition->events[i].type == 4) total_changes++;
    for (size_t i = 0; i < definition->event_count; i++) {
      const qa_camera_event *event = &definition->events[i];
      if (event->type != 4) continue;
      changes_seen++;
      double duration;
      if (changes_seen < total_changes) {
        duration = 0;
        size_t seen = 0;
        for (size_t j = 0; j < definition->event_count; j++) {
          if (definition->events[j].type != 4) continue;
          if (++seen == changes_seen + 1) {
            duration = definition->events[j].time; /* changes[i+1]?.time */
            break;
          }
        }
      } else {
        duration = total - time_so_far;
      }
      long target_index = find_target(definition, event->param);
      if (target_index >= 0 && playback->targets != NULL) {
        position_playback_start(&playback->targets[target_index], start_time,
                                true, duration);
        playback->active_target = target_index;
      }
      time_so_far += duration;
    }
  }
  return true;
oom:
  qa_camera_playback_destroy(playback);
  return false;
}

void qa_camera_playback_destroy(qa_camera_playback *playback) {
  position_playback_destroy(&playback->camera);
  for (size_t i = 0; i < playback->target_count; i++)
    if (playback->targets != NULL)
      position_playback_destroy(&playback->targets[i]);
  free(playback->targets);
  free(playback->triggered);
  free(playback->sample_events);
  playback->targets = NULL;
  playback->triggered = NULL;
  playback->sample_events = NULL;
  playback->target_count = 0;
}

bool qa_camera_playback_sample(qa_camera_playback *playback, double time,
                               qa_camera_sample *out) {
  if (!isfinite(time) || time < playback->last_time)
    qa_range_error("Camera playback requires a monotonic clock");
  playback->last_time = time;
  if (playback->stopped ||
      trunc((time - playback->start_time) / 1000) >
          playback->total_milliseconds / 1000)
    return false;
  size_t event_count = 0;
  for (size_t i = 0; i < playback->definition->event_count; i++) {
    const qa_camera_event *event = &playback->definition->events[i];
    if (playback->triggered[i] || !(time >= playback->start_time + event->time))
      continue;
    playback->triggered[i] = true;
    playback->sample_events[event_count++] = event;
    if (event->type == 9) {
      playback->stopped = true;
      return false;
    }
    if (event->type == 4) {
      playback->active_target =
          find_target(playback->definition, event->param);
      if (playback->active_target >= 0 && playback->targets != NULL &&
          playback->active_target < (long)playback->target_count)
        position_playback_start(
            &playback->targets[playback->active_target],
            playback->start_time + event->time, false, 0);
    }
  }
  qa_vec3 origin = position_playback_sample(&playback->camera, time);
  qa_vec3 target = origin;
  if (playback->targets != NULL && playback->active_target >= 0 &&
      playback->active_target < (long)playback->target_count)
    target = position_playback_sample(
        &playback->targets[playback->active_target], time);
  qa_camera_fov fov = playback->definition->fov;
  double fraction =
      fov.time == 0
          ? 0
          : fmax(0, fmin(1, (time - playback->start_time) / fov.time));
  out->origin = origin;
  out->direction = qa_normalize3(qa_sub3(target, origin));
  out->fov = fov.time == 0 ? fov.value
                           : fov.start + (fov.end - fov.start) * fraction;
  out->events = playback->sample_events;
  out->event_count = event_count;
  return true;
}



