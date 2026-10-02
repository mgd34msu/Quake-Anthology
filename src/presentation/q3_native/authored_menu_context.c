#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#define CGAME 1
#define MISSIONPACK 1
#include "authored_menu_context.h"
#include "qa/text.h"

static _Thread_local q3menu_context *active_context;
q3menu_context *q3menu_active(void) { return active_context; }
q3menu_context *q3menu_enter(q3menu_context *context)
{ q3menu_context *previous = active_context; active_context = context; return previous; }
void q3menu_leave(q3menu_context *previous) { active_context = previous; }

int q3menu_stricmp(const char *a, const char *b)
{
    if (!a) return b ? -1 : 0;
    if (!b) return 1;
    while (*a || *b) {
        unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return (int)x - (int)y;
    }
    return 0;
}
char *q3menu_strupr(char *text)
{ for (char *p = text; *p; ++p) if (*p >= 'a' && *p <= 'z') *p -= 32; return text; }
void q3menu_strncpyz(char *out, const char *text, int capacity)
{
    if (capacity <= 0) return;
    size_t size = strlen(text); if (size >= (size_t)capacity) size = (size_t)capacity - 1;
    memcpy(out, text, size); out[size] = 0;
}
void q3menu_strcat(char *out, int capacity, const char *text)
{
    size_t used = strlen(out);
    if (capacity > 0 && used < (size_t)capacity) q3menu_strncpyz(out + used, text, capacity - (int)used);
}
char *q3menu_parse(char **text, qboolean allow_line_break)
{
    q3menu_context *context = q3menu_active();
    context->common_parser.token[0] = 0;
    context->common_parser.token_length = 0;
    if (!text || !*text || context->failed) return context->common_parser.token;
    qa_common_cursor cursor;
    qa_bytes bytes = {(const uint8_t *)*text, strlen(*text)};
    if (!qa_common_cursor_init(&cursor, bytes, QA_COMMON_TERMINATED, context->error) ||
        !qa_common_parse(&context->common_parser, &cursor, allow_line_break != 0, context->error)) {
        context->failed = true; return context->common_parser.token;
    }
    *text = cursor.ended ? NULL : *text + cursor.offset;
    return context->common_parser.token;
}
char *q3menu_format(const char *format, ...)
{
    q3menu_context *context = q3menu_active();
    char *out = context->format_buffers[context->format_cursor++ % 8];
    va_list arguments; va_start(arguments, format); vsnprintf(out, 4096, format, arguments); va_end(arguments);
    return out;
}
void q3menu_print(const char *format, ...)
{
    q3menu_context *context = q3menu_active(); char text[4096];
    va_list arguments; va_start(arguments, format); vsnprintf(text, sizeof(text), format, arguments); va_end(arguments);
    if (context->display->Print) context->display->Print("%s", text);
}
void q3menu_error(int level, const char *format, ...)
{
    (void)level; q3menu_context *context = q3menu_active(); char text[256];
    va_list arguments; va_start(arguments, format); vsnprintf(text, sizeof(text), format, arguments); va_end(arguments);
    qa_error_set(context->error, QA_ERROR_FORMAT, 0, "%s", text); context->failed = true;
}
void q3menu_axis_clear(vec3_t axis[3])
{ memset(axis, 0, sizeof(vec3_t) * 3); axis[0][0] = axis[1][1] = axis[2][2] = 1; }
void q3menu_angles(const vec3_t angles, vec3_t axis[3])
{
    float yaw = angles[1] * (float)(M_PI / 180), pitch = angles[0] * (float)(M_PI / 180);
    float roll = angles[2] * (float)(M_PI / 180);
    float sy = sinf(yaw), cy = cosf(yaw), sp = sinf(pitch), cp = cosf(pitch), sr = sinf(roll), cr = cosf(roll);
    axis[0][0] = cp * cy; axis[0][1] = cp * sy; axis[0][2] = -sp;
    axis[1][0] = sr * sp * cy - cr * sy; axis[1][1] = sr * sp * sy + cr * cy; axis[1][2] = sr * cp;
    axis[2][0] = cr * sp * cy + sr * sy; axis[2][1] = cr * sp * sy - sr * cy; axis[2][2] = cr * cp;
}
int q3menu_define(char *definition)
{ q3menu_context *context = q3menu_active(); return qa_script_defines_add(context->global_defines, definition, context->error) ? 1 : 0; }
int q3menu_random(void) { q3menu_context *c=q3menu_active(); return c->random_integer(c->owner); }
void *q3menu_alloc(int native_size,int source_size,q3menu_allocation_kind kind)
{
    q3menu_context *c=q3menu_active();
    if(source_size<0 || native_size<0 || c->alloc_point+source_size>128*1024 ||
        c->native_alloc_point+native_size>(int)sizeof(c->allocation.bytes) || c->allocation_count>=8192) {
        c->out_of_memory=qtrue;
        q3menu_print("UI_Alloc: Failure. Out of memory!\n"); q3menu_error(0,"Authored menu allocation exhausted source storage");
        if(c->allocation_guard)longjmp(c->allocation_failure,1);
        return NULL;
    }
    int offset=c->native_alloc_point;
    c->allocations[c->allocation_count++]=(q3menu_allocation){(uint32_t)offset,(uint32_t)source_size,kind};
    c->alloc_point+=(source_size+15)&~15; c->native_alloc_point+=(native_size+15)&~15;
    return c->allocation.bytes+offset;
}
int q3menu_source_open(const char *path)
{
    q3menu_context *context = q3menu_active();
    time_t now=time(NULL); struct tm local_time;
#ifdef _WIN32
    bool have_time=localtime_s(&local_time,&now)==0;
#else
    bool have_time=localtime_r(&now,&local_time)!=NULL;
#endif
    if(!have_time || local_time.tm_mon<0 || local_time.tm_mon>=12 ||
        local_time.tm_mday<1 || local_time.tm_mday>31 ||
        local_time.tm_year<-1900 || local_time.tm_year>8099) {
        q3menu_error(0,"Cannot capture authored source builtin date and time"); return 0;
    }
    static const char *months[]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    unsigned day = (unsigned)local_time.tm_mday;
    unsigned year = (unsigned)(local_time.tm_year + 1900);
    memcpy(context->script_date, months[local_time.tm_mon], 3);
    context->script_date[3] = ' ';
    context->script_date[4] = day < 10 ? ' ' : (char)('0' + day / 10);
    context->script_date[5] = (char)('0' + day % 10);
    context->script_date[6] = ' ';
    for (unsigned i = 0, divisor = 1000; i < 4; ++i, divisor /= 10)
        context->script_date[7 + i] = (char)('0' + year / divisor % 10);
    context->script_date[11] = 0;
    snprintf(context->script_time,sizeof(context->script_time),"%02d:%02d:%02d",local_time.tm_hour,local_time.tm_min,local_time.tm_sec);
    context->scripts.date=context->script_date; context->scripts.time=context->script_time;
    for (unsigned i = 0; i < 64; ++i) if (!context->sources[i]) {
        qa_error local = {0};
        if (!qa_script_open(path, &context->scripts, &context->script_options, &context->sources[i], &local)) {
            if (local.code != QA_ERROR_NOT_FOUND) { if (context->error) *context->error = local; context->failed = true; }
            return 0;
        }
        return (int)i + 1;
    }
    q3menu_error(0, "Authored menu source handle table is full"); return 0;
}
int q3menu_source_close(int handle)
{
    q3menu_context *context = q3menu_active();
    if (handle < 1 || handle > 64 || !context->sources[handle - 1]) return 0;
    qa_script_close(context->sources[handle - 1]); context->sources[handle - 1] = NULL; return 1;
}
int q3menu_source_token(int handle, pc_token_t *out)
{
    q3menu_context *context = q3menu_active(); memset(out, 0, sizeof(*out));
    if (context->failed || handle < 1 || handle > 64 || !context->sources[handle - 1]) return 0;
    qa_script_token token; bool found;
    qa_error local={0};
    if (!qa_script_next(context->sources[handle - 1], &token, &found, &local)) {
        if (!qa_script_source_failure(context->sources[handle - 1])) { if(context->error)*context->error=local; context->failed = true; }
        return 0;
    }
    if (!found) return 0;
    qa_bytes value = token.kind==QA_SCRIPT_STRING?qa_script_token_value(&token):token.text;
    if (value.size >= sizeof(out->string)) { q3menu_error(0, "Authored menu token exceeds source storage"); return 0; }
    memcpy(out->string, value.data, value.size); out->string[value.size] = 0;
    out->type = token.kind == QA_SCRIPT_NUMBER ? TT_NUMBER : token.kind == QA_SCRIPT_STRING ? TT_STRING :
        token.kind == QA_SCRIPT_LITERAL ? TT_LITERAL : token.kind == QA_SCRIPT_NAME ? TT_NAME : TT_PUNCTUATION;
    out->subtype = (int)token.subtype; out->intvalue = token.integer; out->floatvalue = (float)token.number;
    return 1;
}
int q3menu_source_position(int handle, char *path, int *line)
{
    q3menu_context *context = q3menu_active();
    if (handle < 1 || handle > 64 || !context->sources[handle - 1]) return 0;
    qa_script_location location = qa_script_position(context->sources[handle - 1]);
    q3menu_strncpyz(path, location.path, 128); *line = (int)location.line; return 1;
}
void q3menu_reset(q3menu_context *context, bool strings)
{
    q3menu_context *previous = q3menu_enter(context);
    if (strings) { String_Init(); memset(context->menus,0,sizeof(context->menus)); memset(context->menu_stack,0,sizeof(context->menu_stack)); }
    else Menu_Reset();
    context->item_capture = context->bind_item = context->edit_item = NULL;
    context->capture_data = NULL; context->capture_func = NULL;
    context->waiting_for_key = context->editing_field = qfalse;
    q3menu_leave(previous);
}
void q3menu_destroy(q3menu_context *context)
{
    if (!context) return;
    for (unsigned i = 0; i < 64; ++i) qa_script_close(context->sources[i]);
    qa_script_defines_release(context->global_defines);
    free(context);
}
