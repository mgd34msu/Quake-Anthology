#include "library_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct material_token {
    char text[1024];
    size_t start, end, line;
} material_token;

typedef struct material_lexer {
    qa_bytes source;
    size_t at, line;
    bool pending, failed;
    material_token saved;
    qa_error *error;
} material_lexer;

typedef struct material_parser {
    material_lexer lexer;
    qa_material_library *library;
    qa_material *material;
    qa_scene_image_options options;
    const char *base_name;
    const qa_scene_image *base_image;
    bool rejected;
} material_parser;

static unsigned char byte_at(const material_lexer *lexer, size_t at)
{
    return at < lexer->source.size ? lexer->source.data[at] : 0;
}

static bool token_next(material_lexer *lexer, bool lines, material_token *out)
{
    if (lexer->failed) return false;
    if (lexer->pending) {
        lexer->pending = false;
        *out = lexer->saved;
        return true;
    }
    bool newline = false;
    for (;;) {
        unsigned char c;
        while ((c = byte_at(lexer, lexer->at)) && c <= ' ') {
            if (c == '\n' || c == '\r') newline = true;
            if (c == '\n') ++lexer->line;
            ++lexer->at;
        }
        if (byte_at(lexer, lexer->at) == '/' && byte_at(lexer, lexer->at + 1) == '/') {
            lexer->at += 2;
            while ((c = byte_at(lexer, lexer->at)) && c != '\n' && c != '\r') ++lexer->at;
            continue;
        }
        if (byte_at(lexer, lexer->at) == '/' && byte_at(lexer, lexer->at + 1) == '*') {
            lexer->at += 2;
            while ((c = byte_at(lexer, lexer->at)) &&
                   !(c == '*' && byte_at(lexer, lexer->at + 1) == '/')) {
                if (c == '\n' || c == '\r') newline = true;
                if (c == '\n') ++lexer->line;
                ++lexer->at;
            }
            if (c) lexer->at += 2;
            continue;
        }
        break;
    }
    if ((!lines && newline) || !byte_at(lexer, lexer->at)) return false;
    out->start = lexer->at;
    out->line = lexer->line;
    bool quoted = byte_at(lexer, lexer->at) == '"';
    if (quoted) ++lexer->at;
    size_t count = 0;
    unsigned char c;
    while ((c = byte_at(lexer, lexer->at)) && (quoted ? c != '"' : c > ' ')) {
        if (count + 1 >= sizeof(out->text)) {
            qa_error_set(lexer->error, QA_ERROR_FORMAT, out->start,
                         "Shader token exceeds 1023 bytes at line %zu", out->line);
            lexer->failed = true;
            return false;
        }
        out->text[count++] = (char)c;
        if (c == '\n') ++lexer->line;
        ++lexer->at;
    }
    if (quoted && c == '"') ++lexer->at;
    out->text[count] = 0;
    out->end = lexer->at;
    return true;
}

static void token_put(material_lexer *lexer, const material_token *token)
{
    lexer->saved = *token;
    lexer->pending = true;
}

static bool equal(const char *a, const char *b)
{
    for (; *a || *b; ++a, ++b) {
        unsigned char x = (unsigned char)*a, y = (unsigned char)*b;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return false;
    }
    return true;
}

static bool prefix(const char *value, const char *start)
{
    while (*start) {
        unsigned char c = (unsigned char)*value++;
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != (unsigned char)*start++) return false;
    }
    return true;
}

static void line_skip(material_lexer *lexer)
{
    material_token token;
    while (token_next(lexer, false, &token)) {
        if (!strcmp(token.text, "{") || !strcmp(token.text, "}")) {
            token_put(lexer, &token);
            break;
        }
    }
}

bool qa_material_script_catalog(qa_material_library *library, qa_bytes source, qa_error *error)
{
    material_lexer lexer = { .source = source, .line = 1, .error = error };
    material_token name, opening, token;
    while (token_next(&lexer, true, &name)) {
        if (name.text[0] == '{' || name.text[0] == '}') {
            qa_error_set(error, QA_ERROR_FORMAT, name.start, "Expected shader name at line %zu", name.line);
            return false;
        }
        if (!token_next(&lexer, true, &opening) || opening.text[0] != '{') {
            if (!lexer.failed) qa_error_set(error, QA_ERROR_FORMAT, name.end,
                                            "Missing shader opening brace after '%s'", name.text);
            return false;
        }
        unsigned depth = 1;
        size_t end = opening.end;
        while (depth && token_next(&lexer, true, &token)) {
            if (token.text[0] == '{') ++depth;
            if (token.text[0] == '}') --depth;
            end = token.end;
        }
        if (lexer.failed) return false;
        char *key = qa_material_name(name.text, error);
        if (!key) return false;
        unsigned bucket = qa_material_hash(key);
        qa_material_script *existing = library->scripts[bucket];
        while (existing && strcmp(existing->name, key)) existing = existing->next;
        if (existing) { free(key); continue; }
        qa_material_script *script = calloc(1, sizeof(*script));
        size_t length = end - opening.start;
        uint8_t *text = malloc(length ? length : 1);
        if (!script || !text) {
            free(script); free(text); free(key);
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining shader script definition");
            return false;
        }
        memcpy(text, source.data + opening.start, length);
        script->name = key;
        script->text = text;
        script->size = length;
        script->source = library->catalog_current;
        script->source_offset = opening.start;
        script->name_offset = name.start;
        script->name_size = name.end - name.start;
        script->next = library->scripts[bucket];
        library->scripts[bucket] = script;
    }
    return !lexer.failed;
}

static float number_value(material_parser *parser, const material_token *token)
{
    char *end;
    float value = (float)strtod(token->text, &end);
    if (end == token->text) value = 0;
    if (!isfinite(value)) {
        parser->lexer.failed = true;
        qa_error_set(parser->lexer.error, QA_ERROR_FORMAT, token->start,
                     "Shader number is not finite at line %zu", token->line);
        return 0;
    }
    return value;
}

static bool number_next(material_parser *parser, float *value)
{
    material_token token;
    if (!token_next(&parser->lexer, false, &token)) return false;
    *value = number_value(parser, &token);
    return !parser->lexer.failed;
}

static bool parameter(material_parser *parser, material_token *token)
{
    if (token_next(&parser->lexer, false, token)) return true;
    parser->rejected = true;
    return false;
}

static bool vector_parse(material_parser *parser, qa_vec3 *value)
{
    material_token token;
    if (!token_next(&parser->lexer, false, &token) || strcmp(token.text, "(")) return false;
    if (!number_next(parser, &value->x) || !number_next(parser, &value->y) ||
        !number_next(parser, &value->z)) return false;
    return token_next(&parser->lexer, false, &token) && !strcmp(token.text, ")");
}

static bool wave_parse(material_parser *parser, qa_material_wave *wave)
{
    material_token token;
    if (!token_next(&parser->lexer, false, &token)) return false;
    if (equal(token.text, "square")) wave->kind = QA_WAVE_SQUARE;
    else if (equal(token.text, "triangle")) wave->kind = QA_WAVE_TRIANGLE;
    else if (equal(token.text, "sawtooth")) wave->kind = QA_WAVE_SAWTOOTH;
    else if (equal(token.text, "inversesawtooth")) wave->kind = QA_WAVE_INVERSE_SAWTOOTH;
    else if (equal(token.text, "noise")) wave->kind = QA_WAVE_NOISE;
    else wave->kind = QA_WAVE_SIN;
    return number_next(parser, &wave->base) && number_next(parser, &wave->amplitude) &&
           number_next(parser, &wave->phase) && number_next(parser, &wave->frequency);
}

static char *image_name(const char *name, qa_error *error)
{
    char *copy = qa_material_string(name, error);
    if (copy && *copy != '*') {
        for (char *c = copy; *c; ++c) {
            if (*c >= 'A' && *c <= 'Z') *c += 'a' - 'A';
            if (*c == '\\') *c = '/';
        }
    }
    return copy;
}

static bool image_bind(material_parser *parser, qa_material_stage *stage, size_t index,
                         const char *source_name, bool clamp, bool builtins)
{
    char *name = image_name(source_name, parser->lexer.error);
    if (!name) { parser->lexer.failed = true; return false; }
    qa_scene_image *image = NULL;
    if (builtins && (equal(name, "$whiteimage") || equal(name, "$lightmap"))) {
        image = (qa_scene_image *)qa_scene_white(parser->library->resources);
        qa_scene_image_retain(image);
        stage->lightmap = equal(name, "$lightmap");
        stage->is_lightmap |= stage->lightmap;
    } else {
        qa_scene_image_options options = parser->options;
        options.mipmap = !parser->material->no_mipmaps;
        options.wrap = clamp ? QA_SCENE_CLAMP : QA_SCENE_REPEAT;
        bool base_matches = false;
        if (parser->base_image != NULL && parser->base_name != NULL) {
            char *requested = qa_material_name(name, parser->lexer.error);
            char *base = qa_material_name(parser->base_name, parser->lexer.error);
            if (requested == NULL || base == NULL) {
                free(requested); free(base); free(name); parser->lexer.failed = true; return false;
            }
            base_matches = strcmp(requested, base) == 0 ||
                (strncmp(requested, "textures/", 9) == 0 && strcmp(requested+9, base) == 0);
            free(requested); free(base);
        }
        qa_error error = {0};
        bool loaded = base_matches ? qa_material_sample_image(parser->library, parser->base_image,
            options.mipmap, options.wrap, &image, &error) :
            qa_scene_image_load(parser->library->resources, source_name, &options, &image, &error);
        if (!loaded) {
            if (error.code == QA_ERROR_MEMORY) {
                if (parser->lexer.error) *parser->lexer.error = error;
                parser->lexer.failed = true;
            } else parser->rejected = true;
        }
        if (index == 0) stage->lightmap = false;
    }
    bool ok = qa_material_stage_image(stage, index, name, image, parser->lexer.error);
    free(name);
    if (!ok) { qa_scene_image_release(image); parser->lexer.failed = true; }
    stage->clamp = clamp;
    return ok && !parser->lexer.failed && !parser->rejected;
}

static qa_scene_blend blend_factor(const char *token, bool destination)
{
    if (equal(token, "gl_zero")) return QA_BLEND_ZERO;
    if (equal(token, "gl_one")) return QA_BLEND_ONE;
    if (equal(token, "gl_src_alpha")) return QA_BLEND_SRC_ALPHA;
    if (equal(token, "gl_one_minus_src_alpha")) return QA_BLEND_ONE_MINUS_SRC_ALPHA;
    if (equal(token, "gl_dst_alpha")) return QA_BLEND_DST_ALPHA;
    if (equal(token, "gl_one_minus_dst_alpha")) return QA_BLEND_ONE_MINUS_DST_ALPHA;
    if (destination && equal(token, "gl_src_color")) return QA_BLEND_SRC_COLOR;
    if (destination && equal(token, "gl_one_minus_src_color")) return QA_BLEND_ONE_MINUS_SRC_COLOR;
    if (!destination && equal(token, "gl_dst_color")) return QA_BLEND_DST_COLOR;
    if (!destination && equal(token, "gl_one_minus_dst_color")) return QA_BLEND_ONE_MINUS_DST_COLOR;
    if (!destination && equal(token, "gl_src_alpha_saturate")) return QA_BLEND_SRC_ALPHA_SATURATE;
    return QA_BLEND_ONE;
}

static void rgb_parse(material_parser *parser, qa_material_stage *stage)
{
    material_token token;
    if (!token_next(&parser->lexer, false, &token)) return;
    if (equal(token.text, "identity")) stage->rgb = QA_COLOR_IDENTITY;
    else if (equal(token.text, "identitylighting")) stage->rgb = QA_COLOR_IDENTITY_LIGHTING;
    else if (equal(token.text, "entity")) stage->rgb = QA_COLOR_ENTITY;
    else if (equal(token.text, "oneminusentity")) stage->rgb = QA_COLOR_ONE_MINUS_ENTITY;
    else if (equal(token.text, "exactvertex")) stage->rgb = QA_COLOR_EXACT_VERTEX;
    else if (equal(token.text, "vertex")) {
        stage->rgb = QA_COLOR_VERTEX;
        if (stage->alpha == QA_COLOR_IDENTITY) stage->alpha = QA_COLOR_VERTEX;
    } else if (equal(token.text, "oneminusvertex")) stage->rgb = QA_COLOR_ONE_MINUS_VERTEX;
    else if (equal(token.text, "lightingdiffuse")) stage->rgb = QA_COLOR_LIGHTING_DIFFUSE;
    else if (equal(token.text, "const")) {
        qa_vec3 value = {stage->constant.x, stage->constant.y, stage->constant.z};
        if (!vector_parse(parser, &value)) { parser->rejected = true; return; }
        stage->constant.x = value.x; stage->constant.y = value.y; stage->constant.z = value.z;
        stage->rgb = QA_COLOR_CONSTANT;
    } else if (equal(token.text, "wave")) {
        (void)wave_parse(parser, &stage->rgb_wave);
        stage->rgb = QA_COLOR_WAVE;
    }
}

static void alpha_parse(material_parser *parser, qa_material_stage *stage)
{
    material_token token;
    if (!token_next(&parser->lexer, false, &token)) return;
    if (equal(token.text, "identity")) stage->alpha = QA_COLOR_IDENTITY;
    else if (equal(token.text, "entity")) stage->alpha = QA_COLOR_ENTITY;
    else if (equal(token.text, "oneminusentity")) stage->alpha = QA_COLOR_ONE_MINUS_ENTITY;
    else if (equal(token.text, "vertex")) stage->alpha = QA_COLOR_VERTEX;
    else if (equal(token.text, "oneminusvertex")) stage->alpha = QA_COLOR_ONE_MINUS_VERTEX;
    else if (equal(token.text, "lightingspecular")) stage->alpha = QA_COLOR_LIGHTING_SPECULAR;
    else if (equal(token.text, "const")) {
        double value = 0;
        material_token argument;
        if (token_next(&parser->lexer, false, &argument)) value = strtod(argument.text, NULL);
        /* Source alphaGen const uses a double atof before its byte conversion. */
        double integer = trunc(value * 255);
        if (!isfinite(integer) || integer < INT32_MIN || integer > INT32_MAX) {
            parser->lexer.failed = true;
            qa_error_set(parser->lexer.error, QA_ERROR_FORMAT, token.start,
                         "alphaGen const exceeds defined source byte conversion");
            return;
        }
        stage->constant.w = (float)(uint8_t)(uint32_t)(int32_t)integer / 255;
        stage->alpha = QA_COLOR_CONSTANT;
    } else if (equal(token.text, "wave")) {
        (void)wave_parse(parser, &stage->alpha_wave);
        stage->alpha = QA_COLOR_WAVE;
    } else if (equal(token.text, "portal")) {
        float range = 256;
        (void)number_next(parser, &range);
        stage->alpha = QA_COLOR_PORTAL;
        parser->material->portal_range = range;
    }
}

static void tcgen_parse(material_parser *parser, qa_material_stage *stage)
{
    material_token token;
    if (!token_next(&parser->lexer, false, &token)) return;
    if (equal(token.text, "texture") || equal(token.text, "base")) stage->tcgen = QA_TC_TEXTURE;
    else if (equal(token.text, "lightmap")) stage->tcgen = QA_TC_LIGHTMAP;
    else if (equal(token.text, "environment")) stage->tcgen = QA_TC_ENVIRONMENT;
    else if (equal(token.text, "vector")) {
        stage->tcgen = QA_TC_VECTOR;
        (void)vector_parse(parser, &stage->tc_vectors[0]);
        (void)vector_parse(parser, &stage->tc_vectors[1]);
    } else return;
}

static void tcmod_parse(material_parser *parser, qa_material_stage *stage)
{
    if (stage->tcmod_count == QA_MATERIAL_MAX_TCMODS) {
        parser->lexer.failed = true;
        qa_error_set(parser->lexer.error, QA_ERROR_FORMAT, parser->lexer.at,
                     "Shader '%s' exceeds four tcMod directives", parser->material->name);
        return;
    }
    if (!stage->tcmods) {
        stage->tcmods = calloc(QA_MATERIAL_MAX_TCMODS, sizeof(*stage->tcmods));
        if (!stage->tcmods) {
            parser->lexer.failed = true;
            qa_error_set(parser->lexer.error, QA_ERROR_MEMORY, 0, "Allocating texture modifiers");
            return;
        }
    }
    qa_material_tcmod *output = &stage->tcmods[stage->tcmod_count++];
    output->kind = QA_TCMOD_NONE;
    material_token token;
    if (!token_next(&parser->lexer, false, &token)) return;
    qa_material_tcmod mod = { .kind = QA_TCMOD_NONE, .wave = {.kind = QA_WAVE_NONE} };
    size_t numbers = 0;
    if (equal(token.text, "scale")) { mod.kind = QA_TCMOD_SCALE; numbers = 2; }
    else if (equal(token.text, "scroll")) { mod.kind = QA_TCMOD_SCROLL; numbers = 2; }
    else if (equal(token.text, "rotate")) { mod.kind = QA_TCMOD_ROTATE; numbers = 1; }
    else if (equal(token.text, "transform")) { mod.kind = QA_TCMOD_TRANSFORM; numbers = 6; }
    else if (equal(token.text, "entitytranslate")) mod.kind = QA_TCMOD_ENTITY_TRANSLATE;
    else if (equal(token.text, "stretch")) {
        mod.kind = QA_TCMOD_STRETCH;
        if (!wave_parse(parser, &mod.wave)) return;
    } else if (equal(token.text, "turb")) {
        mod.kind = QA_TCMOD_TURBULENCE;
        mod.wave.kind = QA_WAVE_SIN;
        if (!number_next(parser, &mod.wave.base) || !number_next(parser, &mod.wave.amplitude) ||
            !number_next(parser, &mod.wave.phase) || !number_next(parser, &mod.wave.frequency)) {
            return;
        }
    }
    for (size_t i = 0; i < numbers; ++i) {
        if (!number_next(parser, &mod.values[i])) return;
    }
    *output = mod;
    line_skip(&parser->lexer);
}

static void video_parse(material_parser *parser, qa_material_stage *stage, const char *source)
{
    qa_error error = {0};
    const qa_scene_image *image = parser->library->video_start
        ? parser->library->video_start(parser->library->video_context, source, &error) : NULL;
    if (error.code == QA_ERROR_MEMORY) {
        if (parser->lexer.error) *parser->lexer.error = error;
        parser->lexer.failed = true;
        return;
    }
    if (!image) {
        if (stage->video) {
            free(stage->video_name);
            stage->video_name = NULL;
            stage->video_identity = 0;
            stage->retain_texture = true;
        }
        return;
    }
    char *name = image_name(source, parser->lexer.error);
    if (!name) { parser->lexer.failed = true; return; }
    qa_scene_image_retain(image);
    if (!qa_material_stage_image(stage, 0, name, (qa_scene_image *)image, parser->lexer.error)) {
        qa_scene_image_release(image);
        free(name);
        parser->lexer.failed = true;
        return;
    }
    free(stage->video_name);
    stage->video_name = name;
    stage->video_identity = image->identity;
    stage->video = true;
    stage->retain_texture = false;
    stage->lightmap = false;
}

static bool stage_parse(material_parser *parser)
{
    qa_material *material = parser->material;
    if (material->stage_count == QA_MATERIAL_MAX_STAGES) {
        parser->rejected = true;
        return false;
    }
    if (!material->stages) {
        material->stages = calloc(QA_MATERIAL_MAX_STAGES, sizeof(*material->stages));
        if (!material->stages) {
            qa_error_set(parser->lexer.error, QA_ERROR_MEMORY, 0, "Allocating shader stages");
            parser->lexer.failed = true;
            return false;
        }
    }
    qa_material_stage *stage = &material->stages[material->stage_count++];
    qa_material_stage_init(stage);
    qa_scene_state state = stage->state;
    stage->state.depth_write = false;
    bool explicit_depth = false, source_blend_assigned = false;
    bool destination_blend_assigned = false;
    size_t animation_count = 0;
    material_token token, argument;
    while (token_next(&parser->lexer, true, &token)) {
        if (token.text[0] == '}') {
            if (stage->rgb == QA_COLOR_BAD) stage->rgb = !source_blend_assigned ||
                state.blend_source == QA_BLEND_ONE || state.blend_source == QA_BLEND_SRC_ALPHA
                    ? QA_COLOR_IDENTITY_LIGHTING : QA_COLOR_IDENTITY;
            if (state.blend_source == QA_BLEND_ONE && state.blend_destination == QA_BLEND_ZERO &&
                (!source_blend_assigned || destination_blend_assigned))
                state.depth_write = true;
            /* ParseStage compares alphaGen_t to numeric CGEN_IDENTITY. */
            if (stage->alpha == QA_COLOR_ENTITY &&
                (stage->rgb == QA_COLOR_IDENTITY || stage->rgb == QA_COLOR_LIGHTING_DIFFUSE))
                stage->alpha = QA_COLOR_SKIP;
            stage->invalid_blend = source_blend_assigned && !destination_blend_assigned;
            stage->state = state;
            return true;
        }
        if (equal(token.text, "map") || equal(token.text, "clampmap")) {
            if (!parameter(parser, &argument)) break;
            bool clamp = equal(token.text, "clampmap");
            if (!image_bind(parser, stage, 0, argument.text, clamp, !clamp)) break;
        } else if (equal(token.text, "animmap")) {
            if (!parameter(parser, &argument)) break;
            stage->animation_frequency = number_value(parser, &argument);
            while (token_next(&parser->lexer, false, &argument)) {
                if (!strcmp(argument.text, "{") || !strcmp(argument.text, "}")) {
                    token_put(&parser->lexer, &argument); break;
                }
                if (animation_count == QA_MATERIAL_MAX_ANIMATION) continue;
                if (!image_bind(parser, stage, animation_count, argument.text, false, false)) {
                    if (animation_count) stage->image_count = animation_count;
                    break;
                }
                ++animation_count;
            }
        } else if (equal(token.text, "videomap")) {
            if (!parameter(parser, &argument)) break;
            video_parse(parser, stage, argument.text);
        } else if (equal(token.text, "blendfunc")) {
            if (!token_next(&parser->lexer, false, &argument)) continue;
            bool complete = true;
            source_blend_assigned = true;
            if (equal(argument.text, "add")) {
                state.blend_source = QA_BLEND_ONE; state.blend_destination = QA_BLEND_ONE;
            } else if (equal(argument.text, "filter")) {
                state.blend_source = QA_BLEND_DST_COLOR; state.blend_destination = QA_BLEND_ZERO;
            } else if (equal(argument.text, "blend")) {
                state.blend_source = QA_BLEND_SRC_ALPHA; state.blend_destination = QA_BLEND_ONE_MINUS_SRC_ALPHA;
            } else {
                state.blend_source = blend_factor(argument.text, false);
                if (token_next(&parser->lexer, false, &argument)) {
                    state.blend_destination = blend_factor(argument.text, true);
                } else complete = false;
            }
            if (complete) destination_blend_assigned = true;
            if (complete && !explicit_depth) state.depth_write = false;
        } else if (equal(token.text, "depthwrite")) {
            explicit_depth = true; state.depth_write = true;
        } else if (equal(token.text, "depthfunc")) {
            if (!parameter(parser, &argument)) break;
            if (equal(argument.text, "equal")) state.depth_test = QA_DEPTH_EQUAL;
            else if (equal(argument.text, "lequal")) state.depth_test = QA_DEPTH_LEQUAL;
        } else if (equal(token.text, "alphafunc")) {
            if (!parameter(parser, &argument)) break;
            state.alpha_test = equal(argument.text, "gt0") ? QA_ALPHA_GT0 :
                equal(argument.text, "lt128") ? QA_ALPHA_LT128 :
                equal(argument.text, "ge128") ? QA_ALPHA_GE128 : QA_ALPHA_NONE;
        } else if (equal(token.text, "detail")) stage->detail = true;
        else if (equal(token.text, "rgbgen")) rgb_parse(parser, stage);
        else if (equal(token.text, "alphagen")) alpha_parse(parser, stage);
        else if (equal(token.text, "tcgen") || equal(token.text, "texgen"))
            tcgen_parse(parser, stage);
        else if (equal(token.text, "tcmod")) tcmod_parse(parser, stage);
        else parser->rejected = true;
        if (parser->rejected || parser->lexer.failed) break;
    }
    parser->rejected = true;
    return false;
}

static void deform_parse(material_parser *parser)
{
    material_token token;
    if (!token_next(&parser->lexer, false, &token)) return;
    qa_material *material = parser->material;
    if (material->deform_count == QA_MATERIAL_MAX_DEFORMS) return;
    if (!material->deforms) {
        material->deforms = calloc(QA_MATERIAL_MAX_DEFORMS, sizeof(*material->deforms));
        if (!material->deforms) {
            parser->lexer.failed = true;
            qa_error_set(parser->lexer.error, QA_ERROR_MEMORY, 0, "Allocating shader deformations");
            return;
        }
    }
    qa_material_deform *output = &material->deforms[material->deform_count++];
    output->kind = QA_DEFORM_NONE;
    qa_material_deform deform = {.kind = QA_DEFORM_NONE, .wave = {.kind = QA_WAVE_NONE}};
    if (equal(token.text, "projectionshadow")) deform.kind = QA_DEFORM_PROJECTION_SHADOW;
    else if (equal(token.text, "autosprite")) deform.kind = QA_DEFORM_AUTOSPRITE;
    else if (equal(token.text, "autosprite2")) deform.kind = QA_DEFORM_AUTOSPRITE2;
    else if (prefix(token.text, "text")) {
        deform.kind = QA_DEFORM_TEXT;
        deform.text_index = token.text[4] >= '0' && token.text[4] <= '7' ? (uint32_t)(token.text[4] - '0') : 0;
    } else if (equal(token.text, "bulge")) {
        if (!number_next(parser, &deform.width) || !number_next(parser, &deform.height) ||
            !number_next(parser, &deform.speed)) return;
        deform.kind = QA_DEFORM_BULGE;
    } else if (equal(token.text, "normal")) {
        if (!number_next(parser, &deform.wave.amplitude) || !number_next(parser, &deform.wave.frequency)) return;
        deform.kind = QA_DEFORM_NORMAL;
    } else if (equal(token.text, "move")) {
        if (!number_next(parser, &deform.vector.x) || !number_next(parser, &deform.vector.y) ||
            !number_next(parser, &deform.vector.z)) return;
        deform.kind = QA_DEFORM_MOVE;
        (void)wave_parse(parser, &deform.wave);
    } else if (equal(token.text, "wave")) {
        float divisor;
        if (!number_next(parser, &divisor)) return;
        deform.kind = QA_DEFORM_WAVE;
        deform.spread = divisor == 0 ? 100 : 1 / divisor;
        (void)wave_parse(parser, &deform.wave);
    }
    *output = deform;
}

static void surface_parameter(qa_material *material, const char *name)
{
    static const struct { const char *name; uint32_t surface, content; } flags[] = {
        {"nodamage", 1, 0}, {"slick", 2, 0}, {"sky", 4, 0}, {"ladder", 8, 0},
        {"noimpact", 0x10, 0}, {"nomarks", 0x20, 0}, {"flesh", 0x40, 0},
        {"nodraw", 0x80, 0}, {"hint", 0x100, 0}, {"nolightmap", 0x400, 0},
        {"pointlight", 0x800, 0}, {"metalsteps", 0x1000, 0}, {"nosteps", 0x2000, 0},
        {"nonsolid", 0x4000, 0}, {"lightfilter", 0x8000, 0}, {"alphashadow", 0x10000, 0},
        {"nodlight", 0x20000, 0}, {"dust", 0x40000, 0}, {"lava", 0, 8},
        {"slime", 0, 16}, {"water", 0, 32}, {"fog", 0, 64},
        {"areaportal", 0, 0x8000}, {"playerclip", 0, 0x10000}, {"monsterclip", 0, 0x20000},
        {"clusterportal", 0, 0x100000}, {"donotenter", 0, 0x200000},
        {"origin", 0, 0x1000000}, {"detail", 0, 0x8000000},
        {"structural", 0, 0x10000000}, {"trans", 0, 0x20000000}, {"nodrop", 0, UINT32_C(0x80000000)}
    };
    for (size_t i = 0; i < sizeof(flags) / sizeof(flags[0]); ++i) {
        if (equal(name, flags[i].name)) {
            material->surface_flags |= flags[i].surface;
            material->content_flags |= flags[i].content;
            return;
        }
    }
}

static float sort_value(material_parser *parser, const material_token *token)
{
    static const struct { const char *name; float value; } names[] = {
        {"portal", 1}, {"sky", 2}, {"opaque", 3}, {"decal", 4}, {"seethrough", 5},
        {"banner", 6}, {"underwater", 8}, {"additive", 10}, {"nearest", 16}
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (equal(token->text, names[i].name)) return names[i].value;
    return number_value(parser, token);
}

static bool sky_images(material_parser *parser, const char *base, bool outer)
{
    static const char *const faces[6] = {"rt", "bk", "lf", "ft", "up", "dn"};
    const qa_scene_image **images = outer ? parser->material->sky_outer_images : parser->material->sky_inner_images;
    qa_scene_image_options options = parser->options;
    options.mipmap = true;
    options.usage = QA_IMAGE_USAGE_SKY;
    options.wrap = outer ? QA_SCENE_CLAMP : QA_SCENE_REPEAT;
    size_t length = strlen(base);
    char *path = malloc(length + sizeof("_rt.tga"));
    if (!path) {
        parser->lexer.failed = true;
        qa_error_set(parser->lexer.error, QA_ERROR_MEMORY, 0, "Allocating sky image path");
        return false;
    }
    memcpy(path, base, length);
    path[length] = '_';
    for (size_t i = 0; i < 6; ++i) {
        memcpy(path + length + 1, faces[i], 2);
        memcpy(path + length + 3, ".tga", 5);
        qa_scene_image *image = NULL;
        qa_error error = {0};
        if (!qa_scene_image_load(parser->library->resources, path, &options, &image, &error)) {
            if (error.code == QA_ERROR_MEMORY) {
                free(path);
                parser->lexer.failed = true;
                if (parser->lexer.error) *parser->lexer.error = error;
                return false;
            }
            image = (qa_scene_image *)qa_scene_missing(parser->library->resources);
            qa_scene_image_retain(image);
        }
        qa_scene_image_release(images[i]);
        images[i] = image;
    }
    free(path);
    return true;
}

static void sky_parse(material_parser *parser)
{
    material_token token;
    qa_material *material = parser->material;
    if (!token_next(&parser->lexer, false, &token)) return;
    if (strcmp(token.text, "-")) {
        if (!sky_images(parser, token.text, true)) return;
        char *name = image_name(token.text, parser->lexer.error);
        if (!name) { parser->lexer.failed = true; return; }
        free(material->sky_outer); material->sky_outer = name;
    }
    float height;
    if (!number_next(parser, &height)) return;
    material->sky_height = parser->library->sky_height = height == 0 ? 512 : height;
    if (!token_next(&parser->lexer, false, &token)) return;
    if (strcmp(token.text, "-")) {
        if (!sky_images(parser, token.text, false)) return;
        char *name = image_name(token.text, parser->lexer.error);
        if (!name) { parser->lexer.failed = true; return; }
        free(material->sky_inner); material->sky_inner = name;
    }
    material->sky = true;
}

static void sun_parse(material_parser *parser)
{
    float red = 0, green = 0, blue = 0, intensity = 0, azimuth = 0, elevation = 0;
    (void)number_next(parser, &red);
    (void)number_next(parser, &green);
    (void)number_next(parser, &blue);
    (void)number_next(parser, &intensity);
    (void)number_next(parser, &azimuth);
    (void)number_next(parser, &elevation);
    if (parser->lexer.failed) return;
    qa_vec3 light = {red, green, blue};
    float length = qa_vec_length(light);
    if (length != 0) light = qa_vec_scale(light, 1 / length);
    light = qa_vec_scale(light, intensity);
    float a = (float)((azimuth / 180.0f) * 3.14159265358979323846);
    float b = (float)((elevation / 180.0f) * 3.14159265358979323846);
    qa_vec3 direction = {(float)(cos(a) * cos(b)), (float)(sin(a) * cos(b)), (float)sin(b)};
    parser->material->sun_light = parser->library->sun_light = light;
    parser->material->sun_direction = parser->library->sun_direction = direction;
    parser->material->has_sun = parser->library->has_sun = true;
}

bool qa_material_script_register(qa_material_library *library, qa_material *material,
                                  qa_bytes source, const qa_scene_image_options *options,
                                  int32_t lightmap_index, const char *base_name,
                                  const qa_scene_image *base_image, qa_error *error)
{
    material_parser parser = { .lexer = {.source = source, .line = 1, .error = error},
        .library = library, .material = material, .options = *options,
        .base_name = base_name, .base_image = base_image };
    material_token token, argument;
    bool closed = false;
    if (!token_next(&parser.lexer, true, &token) || token.text[0] != '{') parser.rejected = true;
    while (!parser.rejected && token_next(&parser.lexer, true, &token)) {
        if (token.text[0] == '}') { closed = true; break; }
        if (token.text[0] == '{') { if (!stage_parse(&parser)) break; }
        else if (equal(token.text, "surfaceparm")) {
            if (token_next(&parser.lexer, false, &argument)) surface_parameter(material, argument.text);
        } else if (equal(token.text, "nomipmaps")) {
            material->no_mipmaps = true; material->no_picmip = true;
        } else if (equal(token.text, "nopicmip")) material->no_picmip = true;
        else if (equal(token.text, "polygonoffset")) material->polygon_offset = true;
        else if (equal(token.text, "entitymergable")) material->entity_mergable = true;
        else if (equal(token.text, "clamptime")) (void)number_next(&parser, &material->clamp_time);
        else if (equal(token.text, "portal")) material->sort = 1;
        else if (equal(token.text, "sort")) {
            if (token_next(&parser.lexer, false, &argument)) material->sort = sort_value(&parser, &argument);
        } else if (equal(token.text, "cull")) {
            if (token_next(&parser.lexer, false, &argument)) {
                if (equal(argument.text, "none") || equal(argument.text, "twosided") || equal(argument.text, "disable"))
                    material->cull = QA_CULL_NONE;
                else if (equal(argument.text, "back") || equal(argument.text, "backside") || equal(argument.text, "backsided"))
                    material->cull = QA_CULL_BACK;
            }
        } else if (equal(token.text, "deformvertexes")) deform_parse(&parser);
        else if (equal(token.text, "skyparms")) sky_parse(&parser);
        else if (equal(token.text, "fogparms")) {
            if (!vector_parse(&parser, &material->fog.color)) { parser.rejected = true; break; }
            material->fog.kind = QA_FOG_EXP2;
            material->fog.effect = QA_FOG_OVERLAY;
            if (number_next(&parser, &material->fog.amount)) {
                material->fog.density = 1 / fmaxf(1, material->fog.amount);
                line_skip(&parser.lexer);
            }
        } else if (equal(token.text, "q3map_sun")) sun_parse(&parser);
        else if (equal(token.text, "light")) (void)token_next(&parser.lexer, false, &argument);
        else if (prefix(token.text, "qer") || prefix(token.text, "q3map") || equal(token.text, "tesssize"))
            line_skip(&parser.lexer);
        else parser.rejected = true;
        if (parser.lexer.failed) break;
    }
    if (parser.lexer.failed) return false;
    if (!closed || (!material->stage_count && !material->sky && !(material->content_flags & 64)))
        parser.rejected = true;
    material->default_shader = parser.rejected;
    qa_material_finish(material, lightmap_index);
    return true;
}
