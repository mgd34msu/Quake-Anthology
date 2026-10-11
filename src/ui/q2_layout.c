#include "qa/hud_q2.h"
#include "qa/tokenizer.h"
#include "qa/text.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

enum { Q2_LAYOUT_RERELEASE=1, Q2_LAYOUT_ALTERNATE=2, Q2_LAYOUT_CENTERED=4, Q2_LAYOUT_RIGHT=8 };
#define Q2_LAYOUT_COMMANDS(X) \
    X(IF, "if", 1, 0) \
    X(IFGEF, "ifgef", 1, Q2_LAYOUT_RERELEASE) \
    X(ENDIF, "endif", 0, 0) \
    X(XL, "xl", 1, 0) \
    X(XR, "xr", 1, 0) \
    X(XV, "xv", 1, 0) \
    X(YT, "yt", 1, 0) \
    X(YB, "yb", 1, 0) \
    X(YV, "yv", 1, 0) \
    X(PIC, "pic", 1, 0) \
    X(PICN, "picn", 1, 0) \
    X(NUM, "num", 2, 0) \
    X(LIVES_NUM, "lives_num", 1, Q2_LAYOUT_RERELEASE) \
    X(HNUM, "hnum", 0, 0) \
    X(ANUM, "anum", 0, 0) \
    X(RNUM, "rnum", 0, 0) \
    X(STAT_STRING, "stat_string", 1, 0) \
    X(LOC_STAT_STRING, "loc_stat_string", 1, Q2_LAYOUT_RERELEASE) \
    X(LOC_STAT_RSTRING, "loc_stat_rstring", 1, Q2_LAYOUT_RERELEASE | Q2_LAYOUT_RIGHT) \
    X(LOC_STAT_CSTRING, "loc_stat_cstring", 1, Q2_LAYOUT_RERELEASE | Q2_LAYOUT_CENTERED) \
    X(LOC_STAT_CSTRING2, "loc_stat_cstring2", 1, Q2_LAYOUT_RERELEASE | Q2_LAYOUT_ALTERNATE | Q2_LAYOUT_CENTERED) \
    X(STRING, "string", 1, 0) \
    X(STRING2, "string2", 1, Q2_LAYOUT_ALTERNATE) \
    X(CSTRING, "cstring", 1, Q2_LAYOUT_CENTERED) \
    X(CSTRING2, "cstring2", 1, Q2_LAYOUT_ALTERNATE | Q2_LAYOUT_CENTERED) \
    X(LOC_STRING, "loc_string", 1, Q2_LAYOUT_RERELEASE) \
    X(LOC_STRING2, "loc_string2", 1, Q2_LAYOUT_RERELEASE | Q2_LAYOUT_ALTERNATE) \
    X(LOC_RSTRING, "loc_rstring", 1, Q2_LAYOUT_RERELEASE | Q2_LAYOUT_RIGHT) \
    X(LOC_RSTRING2, "loc_rstring2", 1, Q2_LAYOUT_RERELEASE | Q2_LAYOUT_ALTERNATE | Q2_LAYOUT_RIGHT) \
    X(LOC_CSTRING, "loc_cstring", 1, Q2_LAYOUT_RERELEASE | Q2_LAYOUT_CENTERED) \
    X(LOC_CSTRING2, "loc_cstring2", 1, Q2_LAYOUT_RERELEASE | Q2_LAYOUT_ALTERNATE | Q2_LAYOUT_CENTERED) \
    X(CLIENT, "client", 6, 0) \
    X(CTF, "ctf", 6, 0) \
    X(TIME_LIMIT, "time_limit", 1, Q2_LAYOUT_RERELEASE) \
    X(DOGTAG, "dogtag", 1, Q2_LAYOUT_RERELEASE) \
    X(START_TABLE, "start_table", 1, Q2_LAYOUT_RERELEASE) \
    X(TABLE_ROW, "table_row", 1, Q2_LAYOUT_RERELEASE) \
    X(DRAW_TABLE, "draw_table", 0, Q2_LAYOUT_RERELEASE) \
    X(STAT_PNAME, "stat_pname", 1, Q2_LAYOUT_RERELEASE) \
    X(HEALTH_BARS, "health_bars", 0, Q2_LAYOUT_RERELEASE) \
    X(STORY, "story", 0, Q2_LAYOUT_RERELEASE)
#define COMMAND_ENUM(name, text, arguments, flags) Q2_HUD_##name,
typedef enum q2_hud_command { Q2_HUD_UNKNOWN, Q2_LAYOUT_COMMANDS(COMMAND_ENUM) } q2_hud_command;
#undef COMMAND_ENUM
#define COMMAND_INFO(name, text, arguments, flags) {text,arguments,flags},
static const struct { const char *name; uint8_t arguments, flags; } command_info[] = {
    {"",0,0}, Q2_LAYOUT_COMMANDS(COMMAND_INFO)};
#undef COMMAND_INFO
#undef Q2_LAYOUT_COMMANDS

typedef struct q2_hud_token {
    uint32_t offset;
    int32_t number;
    qa_string_id picture;
    q2_hud_command command;
} q2_hud_token;
struct qa_hud_q2_program {
    qa_strings *strings;
    qa_arena storage;
    q2_hud_token *tokens;
    char *text;
    size_t count, token_capacity, text_capacity;
    qa_scene_resources *images;
    qa_scene_filter filter;
    bool rerelease, images_ready;
    uint64_t image_generation;
};
static bool image_prepare(qa_scene_resources *images,qa_strings *strings,qa_string_id id,
    bool rerelease,qa_scene_filter filter,qa_error *error)
{
    qa_error issue={0};
    if (!id || qa_scene_q2_picture(images,strings,id,rerelease,filter,&issue) || issue.code==QA_OK || issue.code==QA_ERROR_NOT_FOUND) return true;
    if (error) *error=issue;
    return false;
}
bool qa_hud_q2_program_prepare_images(qa_hud_q2_program *program,qa_scene_resources *images,
    bool rerelease,qa_scene_filter filter,qa_error *error)
{
    if (!program) return true;
    uint64_t generation=qa_scene_resources_lookup_generation(images);
    if (program->images_ready && program->images==images && program->filter==filter &&
        program->rerelease==rerelease && program->image_generation==generation) return true;
    program->images=images;program->filter=filter;program->rerelease=rerelease;
    program->images_ready=false;
    for (size_t i=0;i<program->count;++i)
        if (program->tokens[i].picture && !image_prepare(images,program->strings,
            program->tokens[i].picture,rerelease,filter,error)) return false;
    program->image_generation=generation;program->images_ready=true;
    return true;
}
static int32_t decimal(const char *);

static bool program_compile(qa_hud_q2_program *p, const char *source, bool rerelease, qa_error *error)
{
    if (!source) source = "";
    size_t length = strlen(source);
    if (length >= QA_HUD_Q2_LAYOUT_BYTES || length >= p->text_capacity) {
        qa_error_set(error, QA_ERROR_FORMAT, length, "Q2 layout exceeds its protocol limit"); return false;
    }
    qa_tokenizer parser;
    if (!qa_tokenizer_init(&parser, (qa_bytes){(const uint8_t *)source, length}, error)) return false;
    p->count = 0;
    size_t used = 0;
    for (;;) {
        qa_token token; bool found;
        if (!qa_tokenizer_next(&parser, &token, &found, error)) return false;
        if (!found) break;
        if (p->count == p->token_capacity || token.text.size + 1 > p->text_capacity - used) {
            qa_error_set(error, QA_ERROR_FORMAT, token.offset, "Q2 layout tokens exceed reserved storage"); return false;
        }
        if (!qa_token_copy(&token, p->text + used, p->text_capacity - used, error)) return false;
        const char *text = p->text + used;
        q2_hud_command command = Q2_HUD_UNKNOWN;
        for (size_t i = 1; i < sizeof(command_info) / sizeof(command_info[0]); ++i)
            if (!strcmp(text, command_info[i].name)) { command = (q2_hud_command)i; break; }
        p->tokens[p->count++] = (q2_hud_token){.offset = (uint32_t)used, .number = decimal(text), .command = command};
        used += strlen(text) + 1;
    }
    if (!p->strings) return true;
    for (size_t i = 0; i < p->count;) {
        q2_hud_command command = p->tokens[i++].command;
        if (!rerelease && (command_info[command].flags & Q2_LAYOUT_RERELEASE)) continue;
        size_t arguments = command_info[command].arguments, picture = SIZE_MAX;
        if (command == Q2_HUD_CLIENT) arguments = rerelease ? 5 : 6;
        if (command == Q2_HUD_CTF) { arguments = rerelease ? 6 : 5; if (rerelease) picture = 5; }
        if (command == Q2_HUD_PICN) picture = 0;
        if ((command >= Q2_HUD_LOC_STRING && command <= Q2_HUD_LOC_CSTRING2) ||
            command == Q2_HUD_START_TABLE || command == Q2_HUD_TABLE_ROW) {
            int32_t count = i < p->count ? p->tokens[i].number : 0;
            arguments = 1 + (count > 0 ? (size_t)count : 0) +
                (command >= Q2_HUD_LOC_STRING && command <= Q2_HUD_LOC_CSTRING2);
        }
        if (picture < p->count - i &&
            !qa_strings_intern_cstr(p->strings, p->text + p->tokens[i + picture].offset,
                &p->tokens[i + picture].picture, error)) return false;
        if (arguments > p->count - i) break;
        i += arguments;
    }
    return true;
}
bool qa_hud_q2_program_create(qa_strings *strings, qa_hud_q2_program **out, qa_error *error)
{
    *out = NULL;
    qa_hud_q2_program *p = calloc(1, sizeof(*p));
    if (!p) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating Q2 layout program"); return false; }
    p->strings = strings; qa_strings_retain(strings);
    p->token_capacity = QA_HUD_Q2_LAYOUT_BYTES / 2 + 1;
    p->text_capacity = QA_HUD_Q2_LAYOUT_BYTES;
    if (!qa_arena_reserve(&p->storage, QA_HUD_Q2_LAYOUT_SCRATCH_BYTES, error)) {
        qa_hud_q2_program_destroy(p); return false;
    }
    p->tokens = qa_arena_alloc(&p->storage, p->token_capacity * sizeof(*p->tokens), _Alignof(q2_hud_token), error);
    p->text = qa_arena_alloc(&p->storage, p->text_capacity, 1, error);
    qa_arena_seal(&p->storage);
    if (!p->tokens || !p->text) { qa_hud_q2_program_destroy(p); return false; }
    *out = p; return true;
}
bool qa_hud_q2_program_update(qa_hud_q2_program *p, const char *source, bool rerelease, qa_error *error)
{ p->images_ready=false;return program_compile(p, source, rerelease, error) && (!p->images ||
    qa_hud_q2_program_prepare_images(p,p->images,rerelease,p->filter,error)); }
void qa_hud_q2_program_clear(qa_hud_q2_program *p) { if (p) {p->count=0;p->images=NULL;p->images_ready=false;} }
void qa_hud_q2_program_destroy(qa_hud_q2_program *p)
{ if (p) { qa_strings_destroy(p->strings); qa_arena_destroy(&p->storage); free(p); } }
bool qa_hud_q2_picture_names_bind(qa_strings *strings,qa_hud_q2_picture_names *names,qa_error *error)
{
    for(unsigned style=0;style<2;++style) for(unsigned n=0;n<11;++n) {
        char name[32]; if(n==10)snprintf(name,sizeof name,"%s_minus",style?"anum":"num");
        else snprintf(name,sizeof name,"%s_%u",style?"anum":"num",n);
        if(!qa_strings_intern_cstr(strings,name,&names->digits[style][n],error))return false;
    }
    return qa_strings_intern_cstr(strings,"field_3",&names->field,error)&&
        qa_strings_intern_cstr(strings,"ping",&names->ping,error)&&
        qa_strings_intern_cstr(strings,"inventory",&names->inventory,error)&&
        qa_strings_intern_cstr(strings,"conchars",&names->conchars,error)&&
        qa_strings_intern_cstr(strings,"marker",&names->marker,error)&&
        qa_hud_q2_player_picture_names(strings,"",&names->player_icon,&names->tag,error);
}

qa_hud_q2_player_names qa_hud_q2_player_names_read(const char *info)
{
    const char *first = strchr(info, '\\');
    const char *skin = first && first[1] ? first + 1 : "male/grunt";
    const char *end = strchr(skin, '\\');
    const char *tag = end && end[1] ? end + 1 : "default";
    const char *tag_end = strchr(tag, '\\');
    return (qa_hud_q2_player_names){
        .name = {(const uint8_t *)info, first ? (size_t)(first - info) : 0},
        .appearance = {(const uint8_t *)skin, end ? (size_t)(end - skin) : strlen(skin)},
        .tag = {(const uint8_t *)tag, tag_end ? (size_t)(tag_end - tag) : strlen(tag)}};
}
bool qa_hud_q2_player_icon_path(qa_bytes appearance,char *path,size_t capacity,qa_error *error)
{
    int n=snprintf(path,capacity,"/players/%.*s_i.pcx",(int)appearance.size,(const char *)appearance.data);
    if(n<0||(size_t)n>=capacity) {qa_error_set(error,QA_ERROR_FORMAT,0,"Q2 player icon path is too long");return false;}
    return true;
}
bool qa_hud_q2_player_icon(qa_strings *strings,const char *appearance,size_t length,qa_string_id *icon,qa_error *error)
{
    char path[4096];
    if (!qa_hud_q2_player_icon_path((qa_bytes){(const uint8_t *)appearance,length},path,sizeof(path),error)) return false;
    return qa_strings_intern_cstr(strings,path,icon,error);
}
static bool player_names(qa_strings *strings,const char *info,qa_string_id *appearance,qa_string_id *icon,qa_string_id *dogtag,qa_error *error)
{
    qa_hud_q2_player_names names=qa_hud_q2_player_names_read(info);
    char path[4096];
    int n=snprintf(path,sizeof(path),"/tags/%.*s.pcx",(int)names.tag.size,(const char *)names.tag.data);
    if(n<0||(size_t)n>=sizeof(path)) {qa_error_set(error,QA_ERROR_FORMAT,0,"Q2 player tag path is too long");return false;}
    return (!appearance || qa_strings_intern(strings,names.appearance,appearance,error)) &&
        qa_hud_q2_player_icon(strings,(const char *)names.appearance.data,names.appearance.size,icon,error) &&
        qa_strings_intern_cstr(strings,path,dogtag,error);
}
bool qa_hud_q2_player_picture_names(qa_strings *strings,const char *info,qa_string_id *icon,qa_string_id *dogtag,qa_error *error)
{ return player_names(strings,info,NULL,icon,dogtag,error); }
bool qa_hud_q2_names_init(qa_hud_q2_names *names,qa_strings *strings,qa_error *error)
{
    *names=(qa_hud_q2_names){.strings=strings};
    return qa_hud_q2_program_create(strings,&names->status,error) &&
        player_names(strings,"",NULL,&names->default_icon,&names->default_tag,error);
}
void qa_hud_q2_names_clear(qa_hud_q2_names *names)
{
    memset(names->values,0,sizeof(names->values));
    memset(names->appearances,0,sizeof(names->appearances));
    memset(names->icons,0,sizeof(names->icons));memset(names->tags,0,sizeof(names->tags));
    qa_hud_q2_program_clear(names->status);
    names->images=NULL;
    names->images_ready=false;
}
bool qa_hud_q2_names_configure(qa_hud_q2_names *names,qa_net_protocol_id protocol,qa_error *error)
{
    if(names->configured && names->protocol.kind==protocol.kind &&
        names->protocol.revision==protocol.revision && names->protocol.flags==protocol.flags) return true;
    qa_q2_codec codec;qa_q2_config_layout layout;
    if(!qa_q2_codec_init(&codec,protocol,error)||!qa_q2_config_layout_read(&codec,&layout,error)) return false;
    if(names->configured) qa_hud_q2_names_clear(names);
    names->layout=layout;names->protocol=protocol;names->configured=true;
    names->rerelease=layout.extended;
    return true;
}
bool qa_hud_q2_names_set(qa_hud_q2_names *names,uint32_t index,qa_string_id value,qa_error *error)
{
    if(index>=names->layout.max_configs) return false;
    if(names->values[index]==value) return true;
    const char *text=value?qa_strings_cstr(names->strings,value):"";
    if(index==5&&!qa_hud_q2_program_update(names->status,text,names->rerelease,error)) return false;
    if(index>=names->layout.player_skins&&index-names->layout.player_skins<256) {
        uint32_t slot=index-names->layout.player_skins;
        if(!player_names(names->strings,text,names->appearances+slot,names->icons+slot,names->tags+slot,error)) return false;
    }
    names->values[index]=value;
    if (names->images && index>names->layout.images && index-names->layout.images<names->layout.max_images)
        return image_prepare(names->images,names->strings,value,names->rerelease,names->filter,error);
    if (names->images && index>=names->layout.player_skins && index-names->layout.player_skins<256) {
        uint32_t slot=index-names->layout.player_skins;
        return image_prepare(names->images,names->strings,names->icons[slot],names->rerelease,names->filter,error) &&
            image_prepare(names->images,names->strings,names->tags[slot],names->rerelease,names->filter,error);
    }
    return true;
}
bool qa_hud_q2_names_prepare_images(qa_hud_q2_names *names,qa_scene_resources *images,
    const qa_hud_q2_picture_names *pictures,qa_scene_filter filter,qa_error *error)
{
    uint64_t generation=qa_scene_resources_lookup_generation(images);
    if (names->images_ready && names->images==images && names->filter==filter && names->image_generation==generation) return true;
    names->images=images;names->filter=filter;names->images_ready=false;
    if (!qa_hud_q2_program_prepare_images(names->status,images,names->rerelease,filter,error)) return false;
    for (unsigned set=0;set<2;++set) for(unsigned digit=0;digit<11;++digit)
        if (!image_prepare(images,names->strings,pictures->digits[set][digit],names->rerelease,filter,error)) return false;
    const qa_string_id fixed[]={pictures->field,pictures->ping,pictures->inventory,pictures->conchars,
        pictures->marker,pictures->player_icon,pictures->tag};
    for(size_t i=0;i<sizeof(fixed)/sizeof(*fixed);++i)
        if (!image_prepare(images,names->strings,fixed[i],names->rerelease,filter,error)) return false;
    for(uint32_t index=1;index<names->layout.max_images;++index)
        if (!image_prepare(images,names->strings,names->values[names->layout.images+index],names->rerelease,filter,error)) return false;
    for(unsigned slot=0;slot<256;++slot)
        if (!image_prepare(images,names->strings,names->icons[slot],names->rerelease,filter,error) ||
            !image_prepare(images,names->strings,names->tags[slot],names->rerelease,filter,error)) return false;
    names->image_generation=generation;names->images_ready=true;
    return true;
}
qa_string_id qa_hud_q2_names_picture(const qa_hud_q2_names *names,int32_t index,unsigned variant)
{
    if(index<0||(uint32_t)index>=names->layout.max_configs) return QA_STRING_NONE;
    if(!variant) return names->values[index];
    int32_t slot=index-(int32_t)names->layout.player_skins;
    if(slot<0||slot>=256) return QA_STRING_NONE;
    qa_string_id name=variant==1?names->icons[slot]:names->tags[slot];
    return name?name:variant==1?names->default_icon:names->default_tag;
}
void qa_hud_q2_names_destroy(qa_hud_q2_names *names)
{ qa_hud_q2_program_destroy(names->status);*names=(qa_hud_q2_names){0}; }

typedef struct layout_context {
    const qa_hud_q2_options *options;
    const qa_hud_q2_frame *frame;
    qa_scene_frame *scene;
    qa_arena *storage;
    qa_error *error;
    const qa_hud_q2_program *program;
    q2_hud_command token_command;
    int32_t token_number;
    size_t token;
    qa_string_id token_name;
    qa_hud_q2_table *table;
    float x, y, width, height;
    int32_t images, items, skins, max_images, max_strings, max_clients;
    bool rerelease, failed;
    qa_hud_q2_stat_references *references;
} layout_context;
static const qa_vec4 white = {1, 1, 1, 1}, black = {0, 0, 0, 1};
static void fail(layout_context *c, const char *message) {
    if (!c->failed) qa_error_set(c->error, QA_ERROR_FORMAT, c->token, "%s", message);
    c->failed = true;
}
static char *allocate(layout_context *c, size_t bytes) {
    char *out = qa_arena_alloc(c->storage, bytes, 1, c->error);
    if (!out) c->failed = true;
    return out;
}
static const char *copy_span(layout_context *c, const char *text, size_t length) {
    if (length == SIZE_MAX) { fail(c, "HUD text is too long"); return ""; }
    char *out = allocate(c, length + 1);
    if (!out) return "";
    memcpy(out, text, length); out[length] = 0;
    return out;
}
static const char *copy(layout_context *c, const char *text) {
    return text ? copy_span(c, text, strlen(text)) : "";
}
static const char *config(layout_context *c, int32_t index) {
    return copy(c, c->options->configstring ? c->options->configstring(c->options->context, index) : NULL);
}
static const char *next(layout_context *c, bool *found) {
    *found = c->program && c->token < c->program->count;
    const q2_hud_token *token = *found ? c->program->tokens + c->token++ : NULL;
    c->token_name = token ? token->picture : QA_STRING_NONE;
    c->token_command = token ? token->command : Q2_HUD_UNKNOWN;
    c->token_number = token ? token->number : 0;
    return token ? c->program->text + token->offset : "";
}
static const char *argument(layout_context *c) { bool found; return next(c, &found); }
static int32_t decimal(const char *p) {
    while (*p && (uint8_t)*p <= 32) ++p;
    bool negative = *p == '-';
    if (*p == '+' || *p == '-') ++p;
    uint32_t value = 0;
    while (*p >= '0' && *p <= '9') value = value * 10u + (uint32_t)(*p++ - '0');
    if (negative) value = 0u - value;
    return value <= INT32_MAX ? (int32_t)value : -1 - (int32_t)(UINT32_MAX - value);
}
static int32_t integer(layout_context *c) { argument(c); return c->token_number; }
static void stat_reference(layout_context *c, int32_t index, bool image) {
    if (!c->references) return;
    if (index < 0 || index >= QA_Q2_MAX_STATS) { fail(c, "HUD stat outside playerstate"); return; }
    uint64_t *mask = image ? &c->references->images : &c->references->configstrings;
    *mask |= UINT64_C(1) << (unsigned)index;
}
static double stat(layout_context *c, int32_t index) {
    if (index < 0 || (size_t)index >= c->frame->stat_count) { fail(c, "HUD stat outside playerstate"); return 0; }
    const qa_hud_q2_arsenal *a = c->frame->arsenal;
    if (a && a->has_ammunition && index == 2) return a->unlimited_ammunition ? 0 : 1;
    if (a && a->has_ammunition && index == 3) return a->unlimited_ammunition ? -1 : a->ammunition;
    return c->frame->stats[index];
}
static int32_t stat_int(layout_context *c, int32_t index) {
    double value = stat(c, index);
    if (value < INT32_MIN || value > INT32_MAX) { fail(c, "HUD integer stat exceeds source range"); return 0; }
    return (int32_t)value;
}
static bool quad(layout_context *c, const qa_scene_image *image, float x, float y, float w, float h,
                 qa_vec4 uv, qa_vec4 color) {
    if (c->failed || !image) return !c->failed;
    const qa_hud_q2_options *o = c->options;
    if (!qa_scene_frame_picture_f(c->scene, image, o->viewport,
        (qa_scene_rect_f){(float)o->viewport.x + x * o->scale, (float)o->viewport.y + y * o->scale,
                          w * o->scale, h * o->scale}, uv, color, c->error)) c->failed = true;
    return !c->failed;
}
static void fill(layout_context *c, float x, float y, float w, float h, qa_vec4 color) {
    quad(c, c->options->white, x, y, w, h, (qa_vec4){0, 0, 1, 1}, color);
}
static void picture(layout_context *c, qa_string_id name, float x, float y, float w, float h, bool before) {
    if (c->failed || !name || !c->options->picture) return;
    const qa_scene_image *image = c->options->picture(c->options->context, name, c->error);
    if (!image) return;
    if (before) x -= (float)image->logical_width + 2;
    quad(c, image, x, y, w > 0 ? w : (float)image->logical_width, h > 0 ? h : (float)image->logical_height,
         (qa_vec4){0, 0, 1, 1}, white);
}
static void classic_glyph(layout_context *c, uint32_t code, float x, float y, bool shadow) {
    code &= 255;
    if ((code & 127) == 32) return;
    qa_font_glyph glyph;
    if (!qa_font_find_glyph(c->options->fonts.classic, code, &glyph) || !glyph.visible) return;
    if (shadow) quad(c, glyph.image, x + 1, y + 1, 8, 8, glyph.uv, black);
    quad(c, glyph.image, x, y, 8, 8, glyph.uv, white);
}
static size_t utf16_length(const char *value) {
    qa_bytes bytes = {(const uint8_t *)value, strlen(value)};
    size_t cursor = 0, units = 0; uint32_t code;
    while (qa_utf8_next(bytes, &cursor, &code)) units += code > 0xffff ? 2 : 1;
    return units;
}
static void classic_text(layout_context *c, const char *value, float x, float y,
                         bool alternate, bool byte_xor, bool shadow) {
    uint32_t mask = alternate ? 128 : 0;
    if (byte_xor) {
        for (size_t i = 0; value[i]; ++i) classic_glyph(c, (uint8_t)value[i] ^ mask, x + (float)i * 8, y, shadow);
    } else {
        qa_bytes bytes = {(const uint8_t *)value, strlen(value)};
        size_t cursor = 0, index = 0; uint32_t code;
        while (qa_utf8_next(bytes, &cursor, &code)) {
            if (code > 0xffff) {
                code -= 0x10000;
                classic_glyph(c, (0xd800 + (code >> 10)) | mask, x + (float)index++ * 8, y, shadow);
                code = 0xdc00 + (code & 1023);
            }
            classic_glyph(c, code | mask, x + (float)index++ * 8, y, shadow);
        }
    }
}
static bool font_layout(layout_context *c, const char *value, qa_font_layout *out) {
    qa_font_info info;
    const qa_font *font = c->options->fonts.primary ? c->options->fonts.primary : c->options->fonts.classic;
    if (!qa_font_describe(font, &info) || info.line_height <= 0) { fail(c, "HUD font unavailable"); return false; }
    qa_font_layout_options o = {.text = {(const uint8_t *)value, strlen(value)},
        .scale = c->options->font_line_height / info.line_height, .color = white,
        .line_height = c->options->font_line_height, .color_codes = QA_FONT_COLOR_LITERAL};
    if (!qa_font_layout_build(&c->options->fonts, &o, &c->scene->storage, out, c->error)) {
        c->failed = true; return false;
    }
    return true;
}
static float measure(layout_context *c, const char *value, bool force_font) {
    if (!c->rerelease) return (float)utf16_length(value) * 8;
    if (!c->options->use_font && !force_font) return (float)strlen(value) * 8;
    qa_font_layout layout;
    return font_layout(c, value, &layout) ? layout.width : 0;
}
static void text(layout_context *c, const char *value, bool alternate, float x, float y, bool force_font) {
    bool font = c->rerelease && (c->options->use_font || force_font);
    if (!font) { classic_text(c, value, x, y, alternate, c->rerelease, c->rerelease); return; }
    qa_font_layout layout;
    if (!font_layout(c, value, &layout)) return;
    qa_vec4 color = alternate ? (qa_vec4){112.f / 255, 1, 52.f / 255, 1} : white;
    float offset = (c->options->font_line_height - 8) / 2;
    for (size_t pass = 0; pass < 2; ++pass) {
        for (size_t i = 0; i < layout.glyph_count; ++i) {
            const qa_font_positioned_glyph *g = &layout.glyphs[i];
            if (!g->glyph.visible) continue;
            float shadow = pass == 0 ? 1 : 0;
            quad(c, g->glyph.image, x + g->rect.x + shadow, y - offset + g->rect.y + shadow,
                 g->rect.width, g->rect.height, g->glyph.uv,
                 pass == 0 ? black : g->glyph.baked_color ? white : color);
        }
    }
}
static void centered(layout_context *c, const char *value, bool alternate, float x, float y) {
    const char *line = value;
    for (;;) {
        const char *end = strchr(line, '\n');
        const char *part = end ? copy_span(c, line, (size_t)(end - line)) : line;
        float offset = (320 - measure(c, part, false)) / 2;
        text(c, part, alternate, x + (c->rerelease ? offset : truncf(offset)), y, false);
        if (!end) break;
        line = end + 1; y += c->rerelease && c->options->use_font ? 10 : 8;
    }
}
static const char *number_string(layout_context *c, double value, bool truncate) {
    char buffer[512] = {0};
    if (truncate) {
        if (!qa_format_fixed(trunc(value), 0, buffer, sizeof buffer, c->error)) c->failed = true;
    } else if (!qa_format_number(value, buffer, c->error)) c->failed = true;
    return copy(c, buffer);
}
static void field(layout_context *c, double value, int32_t digits, bool alternate) {
    if (digits > 5) digits = 5;
    if (digits < 1) return;
    const char *s = number_string(c, value, true);
    size_t length = strlen(s); if (length > (size_t)digits) length = (size_t)digits;
    float x = c->x + 2 + (float)(16 * (digits - (int32_t)length));
    for (size_t i = 0; i < length; ++i, x += 16) {
        qa_string_id name=c->options->picture_names->digits[alternate][s[i]=='-'?10:(unsigned)(s[i]-'0')];
        picture(c, name, x, c->y, 0, 0, false);
    }
}
static const char *client_info(layout_context *c, int32_t slot) {
    double maximum = 256;
    if (!c->rerelease) {
        const char *borrowed = c->options->configstring ? c->options->configstring(c->options->context, c->max_clients) : NULL;
        const char *raw = copy(c, borrowed);
        if (borrowed) {
            maximum = 0;
            if (*raw && !qa_parse_number((qa_bytes){(const uint8_t *)raw, strlen(raw)}, &maximum, NULL)) maximum = NAN;
        }
        if (!isnan(maximum)) maximum = fmax(1, maximum);
    }
    if (slot < 0 || slot >= maximum) { fail(c, "HUD client outside clientinfo"); return ""; }
    return config(c, c->skins + slot);
}
static const char *client_name(layout_context *c, int32_t slot) {
    const char *info = client_info(c, slot), *slash = strchr(info, '\\');
    return slash ? copy_span(c, info, (size_t)(slash - info)) : info;
}
static const char *localized(layout_context *c, const char *base, const char *const *args, size_t count) {
    const qa_localization_entry *entry = *base == '$' ? qa_localization_find(c->options->localization, base + 1) : NULL;
    size_t base_length = strlen(base), format_length = entry ? strlen(entry->format) : 0;
    if (base_length > SIZE_MAX - format_length || base_length + format_length > SIZE_MAX - 8193) {
        fail(c, "HUD localized text too long"); return "";
    }
    size_t buffer_size = entry && entry->argument_count ? base_length + format_length + 8193
                                                       : (entry ? format_length : base_length) + 1;
    char *buffer = allocate(c, buffer_size); if (!buffer) return "";
    qa_localize(c->options->localization, base, args, count, false, false, buffer, buffer_size);
    if (!strstr(buffer, "##P")) return buffer;
    size_t total = strlen(buffer), capacity = total + 1;
    typedef struct replacement { const char *name; size_t offset, end; } replacement;
    size_t maximum = total / 4;
    if (maximum > SIZE_MAX / sizeof(replacement)) { fail(c, "HUD player substitution overflow"); return ""; }
    replacement *replacements = maximum ? qa_arena_alloc(c->storage, maximum * sizeof(replacement),
                                                         _Alignof(replacement), c->error) : NULL;
    if (maximum && !replacements) { c->failed = true; return ""; }
    size_t found = 0;
    for (size_t i = 0; i + 3 < total; ++i) {
        if (buffer[i] != '#' || buffer[i + 1] != '#' || buffer[i + 2] != 'P' ||
            buffer[i + 3] < '0' || buffer[i + 3] > '9') continue;
        size_t end = i + 3; uint32_t slot = 0;
        while (end < total && buffer[end] >= '0' && buffer[end] <= '9') {
            if (slot > 255) { fail(c, "HUD localized player outside clientinfo"); return ""; }
            slot = slot * 10 + (uint32_t)(buffer[end++] - '0');
        }
        if (slot >= 256) { fail(c, "HUD localized player outside clientinfo"); return ""; }
        const char *name = client_name(c, (int32_t)slot);
        replacements[found++] = (replacement){name, i, end};
        size_t n = strlen(name);
        if (n > SIZE_MAX - capacity) { fail(c, "HUD localized text too long"); return ""; }
        capacity += n; i = end - 1;
    }
    if (!found) return buffer;
    char *out = allocate(c, capacity); if (!out) return "";
    size_t cursor = 0, written = 0;
    for (size_t i = 0; i < found; ++i) {
        replacement r = replacements[i];
        size_t n = r.offset - cursor; memcpy(out + written, buffer + cursor, n); written += n;
        n = strlen(r.name); memcpy(out + written, r.name, n); written += n; cursor = r.end;
    }
    memcpy(out + written, buffer + cursor, total - cursor + 1);
    return out;
}
static const char *config_stat(layout_context *c, int32_t index) {
    int32_t value = stat_int(c, index);
    if (value < 0 || value >= c->max_strings) { fail(c, "HUD stat string outside configstrings"); return ""; }
    return config(c, value);
}
static void item_picture(layout_context *c, int32_t index) {
    const qa_hud_q2_arsenal *a = c->frame->arsenal;
    const qa_scene_image *image = NULL; float aspect = 1;
    if (a && index == 2 && a->has_ammunition) {
        if (!a->unlimited_ammunition) image = a->ammunition_icon;
        aspect = a->ammunition_aspect;
    } else if (a && index == 6 && a->has_selected_item) {
        if (stat(c, index) != 0) image = a->selected_icon;
        aspect = a->selected_aspect;
    } else {
        int32_t value = stat_int(c, index);
        if (value < 0 || value >= c->max_images) { fail(c, "HUD image outside configstrings"); return; }
        picture(c, c->options->config_picture(c->options->context,c->images+value,0), c->x, c->y, 0, 0, false); return;
    }
    if (image) {
        if (!isfinite(aspect) || aspect <= 0) { fail(c, "HUD arsenal image has invalid aspect"); return; }
        float w = 24 * fminf(1, aspect), h = 24 / fmaxf(1, aspect);
        quad(c, image, c->x + (24 - w) / 2, c->y + (24 - h) / 2, w, h,
             (qa_vec4){0, 0, 1, 1}, white);
    }
}
static bool initialize(layout_context *c, const qa_hud_q2_options *options,
                       const qa_hud_q2_frame *frame, qa_scene_frame *scene, qa_error *error) {
    if (!options || !frame || !scene || (frame->stat_count && !frame->stats) ||
        (frame->inventory_count && !frame->inventory) || !isfinite(options->scale) || options->scale <= 0 ||
        !options->fonts.classic || !options->white || !isfinite(options->font_line_height) ||
        options->font_line_height <= 0 ||
        (frame->arsenal && ((frame->arsenal->inventory_count && !frame->arsenal->inventory) ||
                           !isfinite(frame->arsenal->ammunition)))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid Q2 HUD frame or resources"); return false;
    }
    *c = (layout_context){.options = options, .frame = frame, .scene = scene, .storage = &scene->storage, .error = error,
        .width = (float)options->viewport.width / options->scale, .height = (float)options->viewport.height / options->scale,
        .table = options->table};
    if (!isfinite(c->width) || !isfinite(c->height)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 HUD scale exceeds logical coordinate range"); return false;
    }
    if (frame->protocol.kind != QA_NET_Q2_34 && frame->protocol.kind != QA_NET_R1Q2_35 &&
        frame->protocol.kind != QA_NET_Q2PRO_36 && frame->protocol.kind != QA_NET_Q2REPRO_1038 &&
        frame->protocol.kind != QA_NET_Q2KEX_2023 && frame->protocol.kind != QA_NET_Q2KEX_DEMO_2022 &&
        frame->protocol.kind != QA_NET_Q2PRIVATE_4038) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 HUD requires a Q2 protocol"); return false;
    }
    c->rerelease = frame->protocol.kind == QA_NET_Q2REPRO_1038 || frame->protocol.kind == QA_NET_Q2PRIVATE_4038 || frame->protocol.kind == QA_NET_Q2KEX_2023 ||
                   frame->protocol.kind == QA_NET_Q2KEX_DEMO_2022;
    qa_q2_codec codec; qa_q2_config_layout layout;
    if (!qa_q2_codec_init(&codec, frame->protocol, error) || !qa_q2_config_layout_read(&codec, &layout, error)) return false;
    c->images = (int32_t)layout.images; c->items = (int32_t)layout.items;
    c->skins = (int32_t)layout.player_skins; c->max_clients = (int32_t)layout.max_clients;
    c->max_images = (int32_t)layout.max_images; c->max_strings = (int32_t)layout.max_configs;
    return true;
}
static void table_cell(layout_context *c, char out[70], const char *value) {
    size_t length = strlen(value); if (length > 23) length = 23;
    qa_bytes bytes = {(const uint8_t *)value, length};
    size_t cursor = 0, written = 0; uint32_t scalar;
    while (qa_utf8_next(bytes, &cursor, &scalar)) {
        char encoded[4]; size_t n = qa_utf8_encode(scalar, encoded);
        memcpy(out + written, encoded, n); written += n;
    }
    out[written] = 0;
    (void)c;
}
static void draw_table(layout_context *c) {
    qa_hud_q2_table *t = c->table;
    float space = truncf(measure(c, " ", true)), w = 0;
    for (size_t i = 0; i < t->column_count; ++i) w += t->columns[i];
    if (t->column_count) w += (float)(t->column_count - 1) * space;
    float step = 8 + (c->options->font_line_height - 8) / 2;
    float h = (float)t->row_count * step, left = c->x - truncf(w / 2), top = c->y + 8;
    classic_glyph(c, 18, left - 8, top - 8, false);
    classic_glyph(c, 20, left + w, top - 8, false);
    classic_glyph(c, 24, left - 8, top + h, false);
    classic_glyph(c, 26, left + w, top + h, false);
    double first_x = left + ceil(fmax(0, -(double)left - 8) / 8) * 8;
    double end_x = fmin((double)left + w, (double)c->width + 8);
    for (double x = first_x; x < end_x; x += 8) {
        classic_glyph(c, 19, (float)x, top - 8, false); classic_glyph(c, 25, (float)x, top + h, false);
    }
    double first_y = top + ceil(fmax(0, -(double)top - 8) / 8) * 8;
    double end_y = fmin((double)top + h, (double)c->height + 8);
    for (double y = first_y; y < end_y; y += 8) {
        classic_glyph(c, 21, left - 8, (float)y, false); classic_glyph(c, 23, left + w, (float)y, false);
    }
    fill(c, left, top, w, h, black);
    float x = left;
    for (size_t column = 0; column < t->column_count; ++column) {
        for (size_t row = 0; row < t->row_count; ++row) {
            const char *value = t->cells[row][column];
            float offset = t->columns[column] - measure(c, value, true);
            if (!row) offset /= 2; else if (!column) offset = 0;
            text(c, value, row == 0, x + offset, top + (float)row * step, true);
        }
        x += t->columns[column] + space;
    }
}
static const char *padded(layout_context *c, const char *value, size_t width, bool right) {
    size_t units = utf16_length(value), bytes = strlen(value);
    if (units >= width) return value;
    size_t padding = width - units;
    if (bytes > SIZE_MAX - padding - 1) { fail(c, "HUD padding overflow"); return ""; }
    char *out = allocate(c, bytes + padding + 1); if (!out) return "";
    if (right) { memcpy(out, value, bytes); memset(out + bytes, ' ', padding); }
    else { memset(out, ' ', padding); memcpy(out + padding, value, bytes); }
    out[bytes + padding] = 0; return out;
}
static const char *join(layout_context *c, const char *a, const char *b, const char *d) {
    size_t n = strlen(a), m = strlen(b), k = strlen(d);
    if (n > SIZE_MAX - m || n + m > SIZE_MAX - k - 1) { fail(c, "HUD string overflow"); return ""; }
    char *out = allocate(c, n + m + k + 1); if (!out) return "";
    memcpy(out, a, n); memcpy(out + n, b, m); memcpy(out + n + m, d, k + 1); return out;
}
static void name12(layout_context *c, const char *value, bool alternate, float x, float y) {
    qa_bytes bytes = {(const uint8_t *)value, strlen(value)};
    size_t cursor = 0, units = 0; uint32_t scalar;
    while (units < 12 && qa_utf8_next(bytes, &cursor, &scalar)) {
        uint32_t mask = alternate ? 128 : 0;
        if (scalar > 0xffff) {
            scalar -= 0x10000;
            classic_glyph(c, (0xd800 + (scalar >> 10)) | mask, x + (float)units++ * 8, y, false);
            if (units == 12) break;
            scalar = 0xdc00 + (scalar & 1023);
        }
        classic_glyph(c, scalar | mask, x + (float)units++ * 8, y, false);
    }
}
static bool execute(layout_context *c, const qa_hud_q2_program *program) {
    if (!program) return true;
    c->program = program; c->token = 0;
    c->x = c->y = 0;
    bool local_conditions[16], *conditions = local_conditions;
    size_t depth = 0, capacity = 16;
    for (;;) {
        bool found;
        next(c, &found);
        q2_hud_command command = c->token_command;
        if (c->failed || !found) break;
        uint8_t flags=command_info[command].flags;
        if (!c->rerelease && (flags & Q2_LAYOUT_RERELEASE)) continue;
        bool draw = !c->references && (!depth || conditions[depth - 1]);
        if (command == Q2_HUD_IF || command == Q2_HUD_IFGEF) {
            int32_t value = integer(c);
            if (c->references) continue;
            if (!c->rerelease) {
                if (stat(c, value) == 0)
                    do { next(c, &found); command = c->token_command; } while (!c->failed && found && command != Q2_HUD_ENDIF);
                continue;
            }
            bool enabled = draw && (command == Q2_HUD_IF ? stat(c, value) != 0 : c->frame->server_frame >= value);
            if (depth == capacity) {
                if (capacity > SIZE_MAX / (2 * sizeof(bool))) { fail(c, "HUD conditional nesting overflow"); break; }
                bool *larger = qa_arena_alloc(c->storage, capacity * 2 * sizeof(bool), _Alignof(bool), c->error);
                if (!larger) { c->failed = true; break; }
                memcpy(larger, conditions, depth * sizeof(bool)); conditions = larger; capacity *= 2;
            }
            conditions[depth++] = enabled;
        } else if (command == Q2_HUD_ENDIF) {
            if (c->references) continue;
            if (c->rerelease) { if (!depth) fail(c, "HUD endif without matching if"); else --depth; }
        } else if (command == Q2_HUD_XL || command == Q2_HUD_XR || command == Q2_HUD_XV) {
            int32_t value = integer(c);
            if (draw) c->x = (float)value + (command == Q2_HUD_XR ? c->width : command == Q2_HUD_XV ? truncf(c->width / 2) - 160 : 0);
        } else if (command == Q2_HUD_YT || command == Q2_HUD_YB || command == Q2_HUD_YV) {
            int32_t value = integer(c);
            if (draw) c->y = (float)value + (command == Q2_HUD_YB ? c->height : command == Q2_HUD_YV ? truncf(c->height / 2) - 120 : 0);
        } else if (command == Q2_HUD_PIC) {
            int32_t index = integer(c); stat_reference(c, index, true); if (draw) item_picture(c, index);
        } else if (command == Q2_HUD_PICN) {
            argument(c); if (draw) picture(c, c->token_name, c->x, c->y, 0, 0, false);
        } else if (command == Q2_HUD_NUM) {
            int32_t digits = integer(c), index = integer(c); if (draw) field(c, stat(c, index), digits, false);
        } else if (command == Q2_HUD_LIVES_NUM) {
            int32_t index = integer(c);
            if (draw) { double value = stat(c, index); field(c, fmax(0, value - 2), 1, value <= 2 && c->frame->time_ns % UINT64_C(1000000000) < UINT64_C(500000000)); }
        } else if (command == Q2_HUD_HNUM || command == Q2_HUD_ANUM || command == Q2_HUD_RNUM) {
            if (!draw) continue;
            int32_t index = command == Q2_HUD_HNUM ? 1 : command == Q2_HUD_ANUM ? 3 : 5;
            double value = stat(c, index);
            if (index != 1 && value < (c->rerelease || index == 3 ? 0 : 1)) continue;
            bool flash = c->rerelease ? c->frame->time_ns % UINT64_C(1000000000) < UINT64_C(500000000)
                                      : (((uint32_t)c->frame->server_frame >> 2) & 1) != 0;
            int32_t warning = 5;
            if (c->rerelease) {
                const char *weapon = config(c, 12350 + stat_int(c, 53));
                for (size_t i = 0; i < 6; ++i) { const char *bar = strchr(weapon, '|'); weapon = bar ? bar + 1 : ""; }
                int32_t parsed = decimal(weapon); if (parsed) warning = parsed;
            }
            bool alternate = index == 1 ? value <= 0 || (value <= 25 && flash) : index == 3 && value <= warning && flash;
            if (stat_int(c, 15) & (index == 1 ? 1 : index == 3 ? 4 : 2)) picture(c, c->options->picture_names->field, c->x, c->y, 0, 0, false);
            field(c, value, 3, alternate);
        } else if (command == Q2_HUD_STAT_STRING || (command >= Q2_HUD_LOC_STAT_STRING && command <= Q2_HUD_LOC_STAT_CSTRING2)) {
            int32_t index = integer(c); stat_reference(c, index, false); if (!draw) continue;
            const qa_hud_q2_arsenal *a = c->frame->arsenal;
            bool selected = c->rerelease && index == 51 && stat(c, index) != 0 && a && a->has_selected_item;
            const char *raw = selected && a->selected_label ? a->selected_label : config_stat(c, index);
            const char *value = command == Q2_HUD_STAT_STRING ? raw : selected && a->selected_localized_label
                ? a->selected_localized_label : localized(c, raw, NULL, 0);
            if ((flags & Q2_LAYOUT_CENTERED)) centered(c, value, (flags & Q2_LAYOUT_ALTERNATE) != 0, c->x, c->y);
            else text(c, value, false, c->x - ((flags & Q2_LAYOUT_RIGHT) ? measure(c, value, false) : 0), c->y, false);
        } else if (command == Q2_HUD_STRING || command == Q2_HUD_STRING2 || command == Q2_HUD_CSTRING || command == Q2_HUD_CSTRING2) {
            const char *value = argument(c);
            if (draw) {
                bool alternate = (flags & Q2_LAYOUT_ALTERNATE) != 0;
                if ((flags & Q2_LAYOUT_CENTERED)) centered(c, value, alternate, c->x, c->y);
                else text(c, value, alternate, c->x, c->y, false);
            }
        } else if ((command == Q2_HUD_LOC_STRING || command == Q2_HUD_LOC_STRING2 ||
                   command == Q2_HUD_LOC_RSTRING || command == Q2_HUD_LOC_RSTRING2 ||
                   command == Q2_HUD_LOC_CSTRING || command == Q2_HUD_LOC_CSTRING2)) {
            int32_t count = integer(c);
            if (count < 0 || count > 7) { fail(c, "HUD localized argument count exceeds source limit"); break; }
            const char *base = argument(c), *args[7];
            for (int32_t i = 0; i < count; ++i) args[i] = argument(c);
            if (!draw) continue;
            const char *value = localized(c, base, args, (size_t)count);
            bool alternate = (flags & Q2_LAYOUT_ALTERNATE) != 0;
            if ((flags & Q2_LAYOUT_CENTERED)) centered(c, value, alternate, c->x, c->y);
            else text(c, value, alternate, c->x - ((flags & Q2_LAYOUT_RIGHT) ? measure(c, value, false) : 0), c->y, false);
        } else if (command == Q2_HUD_CLIENT) {
            int32_t px = integer(c), py = integer(c), slot = integer(c), score = integer(c), ping = integer(c);
            int32_t time = c->rerelease ? 0 : integer(c);
            if (!draw) continue;
            c->x = truncf(c->width / 2) - 160 + (float)px + (c->rerelease ? 8 : 0);
            c->y = truncf(c->height / 2) - 120 + (float)py + (c->rerelease ? 7 : 0);
            const char *info = client_info(c, slot), *slash = strchr(info, '\\');
            const char *name = slash ? copy_span(c, info, (size_t)(slash - info)) : info;
            text(c, name, !c->rerelease, c->x + 32, c->y, false);
            if (c->rerelease) {
                text(c, number_string(c, score, true), !c->options->use_font, c->x + 32, c->y + 10, false);
                picture(c, c->options->picture_names->ping, c->x + 96, c->y + 10, 9, 9, false);
                text(c, number_string(c, ping, true), false, c->x + (c->options->use_font ? 107 : 105), c->y + 10, false);
            } else {
                text(c, "Score: ", false, c->x + 32, c->y + 8, false);
                text(c, number_string(c, score, true), true, c->x + 88, c->y + 8, false);
                text(c, join(c, "Ping:  ", number_string(c, ping, true), ""), false, c->x + 32, c->y + 16, false);
                text(c, join(c, "Time:  ", number_string(c, time, true), ""), false, c->x + 32, c->y + 24, false);
                picture(c, c->options->config_picture(c->options->context,c->skins+slot,1), c->x, c->y, 0, 0, false);
            }
        } else if (command == Q2_HUD_CTF) {
            int32_t px = integer(c), py = integer(c), slot = integer(c), score = integer(c), ping = integer(c);
            if (ping > 999) ping = 999;
            if(c->rerelease)argument(c);
            qa_string_id icon=c->rerelease?c->token_name:QA_STRING_NONE;
            if (!draw) continue;
            c->x = truncf(c->width / 2) - 160 + (float)px; c->y = truncf(c->height / 2) - 120 + (float)py;
            bool alternate = slot == c->frame->player_number;
            const char *name = client_name(c, slot);
            if (c->rerelease) {
                text(c, number_string(c, score, true), alternate, c->x, c->y, true); c->x += 27;
                text(c, number_string(c, ping, true), alternate, c->x, c->y, true); c->x += 27;
                text(c, name, alternate, c->x, c->y, true);
                picture(c, icon, c->x, c->y, 0, 0, true);
            } else {
                const char *s = join(c, padded(c, number_string(c, score, true), 3, false), " ", padded(c, number_string(c, ping, true), 3, false));
                const char *prefix = join(c, s, " ", "");
                text(c, prefix, alternate, c->x, c->y, false);
                name12(c, name, alternate, c->x + (float)utf16_length(prefix) * 8, c->y);
            }
        } else if (command == Q2_HUD_TIME_LIMIT) {
            int32_t end = integer(c); if (!draw || end < c->frame->server_frame) continue;
            double ns = c->frame->frame_ns ? (double)c->frame->frame_ns : 25000000.;
            double seconds = trunc(((double)end - c->frame->server_frame) * ns / 1.e9);
            const char *minutes = padded(c, number_string(c, trunc(seconds / 60), true), 2, false);
            const char *remainder = padded(c, number_string(c, fmod(seconds, 60), true), 2, false);
            /* Source padStart uses zero rather than blank for the clock. */
            char *clock = (char *)join(c, minutes, ":", remainder);
            for (size_t i = 0; clock[i]; ++i) if (clock[i] == ' ') clock[i] = '0';
            const char *args[] = {clock}, *value = localized(c, "$g_score_time", args, 1);
            text(c, value, true, c->x - measure(c, value, false), c->y, false);
        } else if (command == Q2_HUD_DOGTAG) {
            int32_t slot = integer(c); if (!draw) continue;
            client_info(c,slot);
            picture(c, c->options->config_picture(c->options->context,c->skins+slot,2), c->x, c->y, 198, 32, false);
        } else if ((command == Q2_HUD_START_TABLE || command == Q2_HUD_TABLE_ROW)) {
            bool start = command == Q2_HUD_START_TABLE;
            int32_t count = integer(c); const char *values[6];
            if (count < 0 || count > (start ? 5 : 6)) { fail(c, "HUD table argument count exceeds source limit"); break; }
            for (int32_t i = 0; i < count; ++i) values[i] = argument(c);
            if (!draw) continue;
            if (start) { memset(c->table, 0, sizeof *c->table); c->table->column_count = (size_t)count; }
            if (c->table->row_count >= 11) { fail(c, "HUD table exceeds source dimensions"); break; }
            size_t row = c->table->row_count++;
            size_t cells = (size_t)count > c->table->column_count ? (size_t)count : c->table->column_count;
            for (size_t i = 0; i < cells; ++i) {
                const char *value = i < (size_t)count ? values[i] : "";
                if (start) value = localized(c, value, NULL, 0);
                table_cell(c, c->table->cells[row][i], value);
                if (i < c->table->column_count) c->table->columns[i] = fmaxf(c->table->columns[i], truncf(measure(c, c->table->cells[row][i], true)));
            }
        } else if (command == Q2_HUD_DRAW_TABLE) {
            if (draw) draw_table(c);
        } else if (command == Q2_HUD_STAT_PNAME) {
            int32_t index = integer(c); if (draw) text(c, client_name(c, stat_int(c, index) - 1), false, c->x, c->y, false);
        } else if (command == Q2_HUD_HEALTH_BARS) {
            if (!draw) continue;
            centered(c, localized(c, config(c, c->skins + 256 + 266), NULL, 0), false, truncf(c->width / 2) - 160, c->y);
            c->y += c->options->font_line_height;
            uint32_t value = (uint32_t)stat_int(c, 52);
            float w = c->width / 2, left = c->width / 4;
            for (size_t i = 0; i < 2; ++i) {
                uint32_t packed = (value >> (i * 8)) & 255; if (!(packed & 128)) continue;
                float fraction = (float)(packed & 127u) / 127.f;
                fill(c, left, c->y, w + 1, 5, black);
                if (fraction > 0) fill(c, left, c->y, w * fraction, 4, (qa_vec4){1, 0, 0, 1});
                if (fraction < 1) fill(c, left + w * fraction, c->y, w * (1 - fraction), 4, (qa_vec4){80.f / 255, 80.f / 255, 80.f / 255, 1});
                c->y += 12;
            }
        } else if (command == Q2_HUD_STORY) {
            if (c->references) continue;
            /* Source story deliberately ignores the conditional draw flag. */
            const char *raw = config(c, c->skins + 256 + 267); if (!*raw) continue;
            const char *value = localized(c, raw, NULL, 0);
            qa_font_layout block; if (!font_layout(c, value, &block)) break;
            const char *line = value; size_t index = 0;
            for (;;) {
                const char *end = strchr(line, '\n'), *part = end ? copy_span(c, line, (size_t)(end - line)) : line;
                /* text() applies the ordinary font baseline adjustment. */
                text(c, part, false, (c->width - measure(c, part, true)) / 2,
                     (c->height - block.height) / 2 + (float)index * c->options->font_line_height + (c->options->font_line_height - 8) / 2, true);
                if (!end) break;
                line = end + 1; ++index;
            }
        }
        if (c->failed) break;
    }
    if (c->rerelease && depth && !c->failed) fail(c, "HUD if without matching endif");
    return !c->failed;
}
bool qa_hud_q2_layout_stat_references(const char *source, bool rerelease, qa_arena *scratch,
    qa_hud_q2_stat_references *out, qa_error *error) {
    qa_arena_reset(scratch);
    if (!source || !out) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing Q2 layout reference input"); return false; }
    qa_hud_q2_stat_references value = {0};
    layout_context c = {.storage = scratch, .error = error, .rerelease = rerelease, .references = &value};
    size_t length = strlen(source);
    qa_hud_q2_program program = {.token_capacity = length / 2 + 1, .text_capacity = length + 1};
    program.tokens = qa_arena_alloc(scratch, program.token_capacity * sizeof(*program.tokens), _Alignof(q2_hud_token), error);
    program.text = qa_arena_alloc(scratch, program.text_capacity, 1, error);
    bool ok = program.tokens && program.text && program_compile(&program, source, rerelease, error) && execute(&c, &program);
    qa_arena_reset(scratch);
    if (ok) *out = value;
    return ok;
}
static bool inventory_draw(layout_context *c) {
    const qa_hud_q2_arsenal *a = c->frame->arsenal;
    bool foreign = a && a->has_inventory;
    size_t count = 0, selected_row = 0;
    int32_t selected = c->frame->stat_count > 12 ? c->frame->stats[12] : 0;
    if (foreign) {
        count = a->inventory_count;
        for (size_t i = 0; i < count; ++i) if (a->inventory[i].selected) { selected_row = i; break; }
    } else {
        for (size_t i = 0; i < c->frame->inventory_count; ++i) {
            if (!c->frame->inventory[i]) continue;
            if (selected >= 0 && (size_t)selected < c->frame->inventory_count && i < (size_t)selected) ++selected_row;
            ++count;
        }
    }
    size_t page = c->rerelease ? 19 : 17, half = c->rerelease ? 9 : 8;
    size_t maximum = count > page ? count - page : 0;
    size_t top = selected_row > half ? selected_row - half : 0;
    if (top > maximum) top = maximum;
    float x = c->rerelease ? truncf(c->width / 2) - 128 : floorf((c->width - 256) / 2);
    float y = c->rerelease ? truncf(c->height / 2) - 108 : floorf((c->height - 240) / 2);
    picture(c, c->options->picture_names->inventory, x, y + 8, 0, 0, false);
    if (!c->rerelease) {
        classic_text(c, "hotkey ### item", x + 24, y + 24, false, false, false);
        classic_text(c, "------ --- ----", x + 24, y + 32, false, false, false);
    }
    size_t ordinal = 0, rows = 0, source_count = foreign ? a->inventory_count : c->frame->inventory_count;
    for (size_t i = 0; i < source_count && rows < page; ++i) {
        if (!foreign && !c->frame->inventory[i]) continue;
        if (ordinal++ < top) continue;
        const char *name = foreign ? copy(c, a->inventory[i].label) :
            i <= (size_t)(INT32_MAX - c->items) ? config(c, c->items + (int32_t)i) : "";
        double quantity = foreign ? a->inventory[i].count : c->frame->inventory[i];
        if (!isfinite(quantity)) { fail(c, "HUD inventory has nonfinite quantity"); break; }
        bool chosen = foreign ? a->inventory[i].selected : selected >= 0 && (size_t)selected == i;
        float py = y + (c->rerelease ? 27 : 40) + (float)rows++ * 8;
        const char *quantity_text = number_string(c, quantity, false);
        if (c->rerelease) {
            name = localized(c, name, NULL, 0);
            /* Source inventory cursor uses timeMilliseconds * 10, then ToInt32. */
            bool blink = ((c->frame->time_ns / UINT64_C(100000)) & 1) != 0;
            if (chosen && blink) classic_glyph(c, 15, x + 14, py, false);
            if (c->options->use_font) {
                text(c, quantity_text, chosen, x + 222 - measure(c, quantity_text, false), py, false);
                text(c, name, chosen, x + 38, py, false);
            } else classic_text(c, join(c, padded(c, quantity_text, 3, false), " ", name),
                                x + 22, py, chosen, true, false);
        } else {
            const char *command = join(c, "use ", name, "");
            const char *binding = copy(c, c->options->binding ? c->options->binding(c->options->context, command) : NULL);
            const char *line = join(c, padded(c, binding, 6, false), " ", padded(c, quantity_text, 3, false));
            classic_text(c, join(c, line, " ", name), x + 24, py, !chosen, false, false);
            if (chosen && ((c->frame->time_ns / UINT64_C(100000000)) & 1)) classic_glyph(c, 15, x + 16, py, false);
        }
        if (c->failed) break;
    }
    return !c->failed;
}
static bool table_valid(const qa_hud_q2_table *table, qa_error *error) {
    if (table->row_count > 11 || table->column_count > 5) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid retained Q2 HUD table"); return false;
    }
    for (size_t column = 0; column < table->column_count; ++column) {
        if (!isfinite(table->columns[column]) || table->columns[column] < 0) {
            qa_error_set(error, QA_ERROR_ARGUMENT, column, "invalid Q2 HUD table width"); return false;
        }
        for (size_t row = 0; row < table->row_count; ++row)
            if (!memchr(table->cells[row][column], 0, sizeof table->cells[row][column])) {
                qa_error_set(error, QA_ERROR_ARGUMENT, row, "unterminated Q2 HUD table cell"); return false;
            }
    }
    return true;
}
bool qa_hud_q2_draw(const qa_hud_q2_options *options, const qa_hud_q2_frame *frame,
                    bool overlay_only, qa_scene_frame *scene, qa_error *error) {
    layout_context c;
    if (!initialize(&c, options, frame, scene, error)) return false;
    qa_hud_q2_table table = {0};
    if (!c.table) c.table = &table;
    if (!table_valid(c.table, error)) return false;
    int32_t layouts = frame->stat_count > 13 ? frame->stats[13] : 0;
    if (!overlay_only && !(c.rerelease && (layouts & 4)) && !execute(&c,options->status)) return false;
    if ((layouts & 1) && !execute(&c,frame->layout)) return false;
    if (!overlay_only && (layouts & 2)) return inventory_draw(&c);
    return !c.failed;
}

static double marker_value(const qa_cvar_view *view,double fallback)
{ return view && isfinite(view->number)?view->number:fallback; }
qa_hud_q2_marker_cvars qa_hud_q2_marker_bind(const qa_cvars *registry)
{
    return (qa_hud_q2_marker_cvars){
        .mode=qa_cvars_resolve(registry,"cl_hit_markers"),
        .crosshair=qa_cvars_resolve(registry,"crosshair"),
        .duration=qa_cvars_resolve(registry,"scr_hit_marker_time"),
        .scale=qa_cvars_resolve(registry,"ch_scale"),
        .alpha=qa_cvars_resolve(registry,"ch_alpha"),
        .x=qa_cvars_resolve(registry,"ch_x"), .y=qa_cvars_resolve(registry,"ch_y")};
}
void qa_hud_q2_marker_settings(const qa_cvars *registry, const qa_hud_q2_marker_cvars *handles,
    qa_hud_q2_marker *marker)
{
    marker->mode=qa_cvars_read(registry,handles->mode);
    marker->crosshair=qa_cvars_read(registry,handles->crosshair);
    marker->duration=qa_cvars_read(registry,handles->duration);
    marker->scale=qa_cvars_read(registry,handles->scale);
    marker->alpha=qa_cvars_read(registry,handles->alpha);
    marker->x=qa_cvars_read(registry,handles->x); marker->y=qa_cvars_read(registry,handles->y);
}
bool qa_hud_q2_marker_draw(const qa_hud_q2_marker *marker,uint32_t *count,
    qa_scene_rect viewport,qa_scene_frame *frame,qa_error *error)
{
    if (!*count || marker_value(marker->mode,0)<=0 || marker_value(marker->crosshair,0)==0 || (marker->layout_flags&(4|32))) return true;
    double elapsed=marker->now_ns>=marker->started_ns?(double)(marker->now_ns-marker->started_ns)/1e6:0;
    double limit=trunc(marker_value(marker->duration,0));
    const qa_scene_image *image=marker->image;
    if (!image || limit<=0 || elapsed>limit) {*count=0;return true;}
    float fraction=(float)(elapsed/limit);
    float scale=fmaxf(1,1.5f*(1-fraction))*(float)fmax(.1,fmin(9,marker_value(marker->scale,1)));
    float opacity=(float)fmax(0,fmin(1,marker_value(marker->alpha,1)));
    float dx=(float)fmax(INT32_MIN,fmin(INT32_MAX,trunc(marker_value(marker->x,0))));
    float dy=(float)fmax(INT32_MIN,fmin(INT32_MAX,trunc(marker_value(marker->y,0))));
    float width=truncf((float)image->logical_width*scale),height=truncf((float)image->logical_height*scale);
    qa_scene_rect_f rect={(float)viewport.x+truncf(((float)viewport.width-width)*.5f)+dx,
        (float)viewport.y+truncf(((float)viewport.height-height)*.5f)+dy,width,height};
    return qa_scene_frame_picture_f(frame,image,viewport,rect,(qa_vec4){0,0,1,1},
        (qa_vec4){1,0,0,opacity*(1-fraction*fraction)},error);
}
