#include "qa/server_admin.h"
#include "qa/network_services_save.h"
#include "../service_save_fields.h"
#include "qa/network_q1_channel.h"
#include "qa/network_q2.h"
#include "qa/tokenizer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct ip_filter { uint8_t mask[4], compare[4]; } ip_filter;
typedef struct rate_entry { qa_net_address address; double tokens; uint64_t time; bool active; } rate_entry;
struct qa_server_admin {
    qa_admin_options options;
    ip_filter *filters;
    size_t filter_count;
    rate_entry *rates;
    char **prefixes, **rotation;
    size_t prefix_count, rotation_count, rotation_index;
    qa_net_address *masters[4];
    size_t master_count[4];
    qa_buffer master_names;
    uint64_t heartbeat_time, rcon_time;
    uint32_t heartbeat_sequence;
    bool heartbeat_sent, rcon_sent, shuffle, callback, executing;
    qa_cvars *rate_registry;
    char *rate_text;
    uint64_t rate_revision;
    uint32_t rate_time, credit, credit_cap, credit_cost;
};
static bool fail(qa_error *error, const char *text) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", text); return false;
}
static void strings_free(char **strings, size_t count) {
    for (size_t i = 0; i < count; ++i) free(strings[i]);
    free(strings);
}
static char *copy(const char *text) {
    size_t n = strlen(text); char *out = malloc(n + 1); if (out) memcpy(out, text, n + 1); return out;
}
bool qa_server_admin_declarations(qa_cvars *cvars, uint64_t owner, qa_error *error)
{
    if (!cvars || !owner) return fail(error,"Source administration needs its actual declaration owner");
    qa_console_dialect dialect=qa_cvars_dialect(cvars);
    bool q2=dialect==QA_CONSOLE_Q2 || dialect==QA_CONSOLE_Q2_RERELEASE;
    const char *names[]={dialect==QA_CONSOLE_Q3?"rconPassword":"rcon_password",
        "filterban","public","lrcon_password","sv_rcon_limit","timeout"};
    const char *values[]={"","1","0","","1","125"};
    uint32_t flags[]={q2?QA_Q2_CVAR_PRIVATE:dialect==QA_CONSOLE_Q3?QA_CVAR_TEMPORARY:0,
        0,QA_Q2_CVAR_LATCH,QA_Q2_CVAR_PRIVATE,0,0};
    size_t count=q2?6:dialect==QA_CONSOLE_Q3?1:2;
    for (size_t i=0;i<count;++i) {
        const qa_cvar_view *v=qa_cvars_find(cvars,names[i]);
        if (v && !v->console_created) {
            if (!qa_cvars_add_flags(cvars,names[i],flags[i],error)) return false;
        } else if (!qa_cvars_register(cvars,names[i],values[i],flags[i],owner,NULL,error)) return false;
        if (!qa_cvars_declare_save_policy(cvars,names[i],QA_CVAR_SAVE_SETTING,error)) return false;
    }
    for (unsigned i=1;dialect==QA_CONSOLE_Q3 && i<=5;++i) {
        char name[16]; snprintf(name,sizeof(name),"sv_master%u",i);
        const qa_cvar_view *v=qa_cvars_find(cvars,name); uint32_t master_flags=i==1?0:QA_CVAR_ARCHIVE;
        if (v && !v->console_created) {
            if (!qa_cvars_add_flags(cvars,name,master_flags,error)) return false;
        } else if (!qa_cvars_register(cvars,name,i==1?"master.quake3arena.com":"",master_flags,owner,NULL,error)) return false;
        if (!qa_cvars_declare_save_policy(cvars,name,QA_CVAR_SAVE_SETTING,error)) return false;
    }
    return true;
}
bool qa_server_admin_create(const qa_admin_options *options, qa_server_admin **out, qa_error *error) {
    if (!options || !out || !options->filters || options->filters > 4096 || !options->rate_entries ||
        options->rate_entries > 4096 || !options->burst || !options->rate_interval_ns ||
        !options->heartbeat_interval_ns || !options->hooks.password || !options->hooks.execute ||
        !options->hooks.send || !options->hooks.travel || !options->hooks.players || !options->hooks.random)
        return fail(error, "Invalid server administration options");
    qa_server_admin *admin = calloc(1, sizeof(*admin));
    if (!admin) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating server administration"); return false; }
    admin->filters = calloc(options->filters, sizeof(*admin->filters));
    admin->rates = calloc(options->rate_entries, sizeof(*admin->rates));
    if (!admin->filters || !admin->rates) { qa_server_admin_destroy(admin); qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating server filters/limiter"); return false; }
    admin->options = *options; *out = admin; return true;
}
bool qa_server_admin_adopt(qa_server_admin *destination,qa_server_admin **source,qa_error *error)
{
    qa_server_admin *held=source?*source:NULL;
    if (!destination || !held || destination==held || destination->callback || held->callback ||
        destination->options.filters!=held->options.filters ||
        destination->options.rate_entries!=held->options.rate_entries ||
        destination->options.burst!=held->options.burst ||
        destination->options.rate_interval_ns!=held->options.rate_interval_ns ||
        destination->options.heartbeat_interval_ns!=held->options.heartbeat_interval_ns)
        return fail(error,"Administration adoption differs from its retained state and returned custody");
    qa_server_admin previous=*destination;
    *destination=*held; destination->options=previous.options;
    destination->rate_registry=NULL;
    *held=previous; qa_server_admin_destroy(held); *source=NULL; return true;
}
void qa_server_admin_destroy(qa_server_admin *admin) {
    if (!admin || admin->callback) return;
    free(admin->filters); free(admin->rates); free(admin->rate_text);
    for (size_t i=0;i<4;++i) free(admin->masters[i]);
    qa_buffer_free(&admin->master_names);
    strings_free(admin->prefixes, admin->prefix_count); strings_free(admin->rotation, admin->rotation_count); free(admin);
}
bool qa_server_admin_policy(qa_server_admin *admin,qa_console_dialect dialect,
    bool deny_matches,bool public_server,qa_error *error)
{
    if (!admin || (admin->callback && !admin->executing) || dialect>QA_CONSOLE_Q3)
        return fail(error,"Administration policy requires its actual returned Source");
    if (admin->options.dialect!=dialect) admin->heartbeat_sent=false;
    admin->options.dialect=dialect; admin->options.deny_matches=deny_matches;
    admin->options.public_server=public_server; return true;
}
static bool filter_parse(const char *text, ip_filter *out) {
    *out = (ip_filter){0}; if (!text || !*text) return false;
    for (size_t i = 0; i < 4; ++i) {
        if (*text < '0' || *text > '9') return false;
        uint8_t value = 0;
        while (*text >= '0' && *text <= '9') value = (uint8_t)(value * 10u + (unsigned)(*text++ - '0'));
        out->compare[i] = value; out->mask[i] = value ? 255 : 0;
        if (!*text) return true;
        if (*text++ != '.' || i == 3 || !*text) return false;
    }
    return !*text;
}
bool qa_server_admin_filter(qa_server_admin *admin, const char *text, bool remove, qa_error *error) {
    if (!admin || (admin->callback && !admin->executing)) return fail(error, "Invalid server filter operation");
    ip_filter value; if (!filter_parse(text, &value)) return fail(error, "Invalid source IPv4 filter");
    for (size_t i = 0; i < admin->filter_count; ++i) if (!memcmp(&admin->filters[i], &value, sizeof(value))) {
        if (remove) {
            memmove(admin->filters + i, admin->filters + i + 1, (admin->filter_count - i - 1) * sizeof(value)); --admin->filter_count;
        }
        return true;
    }
    if (remove) return true;
    if (admin->filter_count == admin->options.filters) return fail(error, "Server filter capacity exhausted");
    admin->filters[admin->filter_count++] = value; return true;
}
bool qa_server_admin_rejects(const qa_server_admin *admin, const qa_net_address *address) {
    if (!admin || !address || address->kind != QA_NET_IPV4 || admin->options.dialect == QA_CONSOLE_Q3) return false;
    bool match = false;
    for (size_t i = 0; i < admin->filter_count; ++i) {
        bool equal = true;
        for (size_t byte = 0; byte < 4; ++byte)
            if ((address->host.ipv4[byte] & admin->filters[i].mask[byte]) != admin->filters[i].compare[byte]) { equal = false; break; }
        if (equal) { match = true; break; }
    }
    return admin->options.deny_matches ? match : !match;
}
static bool strings_copy(const char *const *strings, size_t count, size_t maximum,
                          char ***out, qa_error *error) {
    if ((count && !strings) || count > 256) return fail(error, "Invalid server string list");
    char **owned = count ? calloc(count, sizeof(*owned)) : NULL;
    if (count && !owned) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating server list"); return false; }
    for (size_t i = 0; i < count; ++i) {
        if (!strings[i] || !*strings[i] || strlen(strings[i]) > maximum || !(owned[i] = copy(strings[i]))) {
            strings_free(owned, count); return fail(error, "Invalid or unretainable server list entry");
        }
    }
    *out = owned; return true;
}
bool qa_server_admin_limited_prefixes(qa_server_admin *admin, const char *const *prefixes, size_t count, qa_error *error) {
    if (!admin || (admin->callback && !admin->executing)) return fail(error, "Invalid limited command update");
    char **owned; if (!strings_copy(prefixes, count, 1023, &owned, error)) return false;
    strings_free(admin->prefixes, admin->prefix_count); admin->prefixes = owned; admin->prefix_count = count; return true;
}
bool qa_server_admin_limited_command(qa_server_admin *admin,const char *name,const char *raw,
    qa_admin_write_fn write,void *context,qa_error *error) {
    if (!admin || (admin->callback && !admin->executing) || !name || !raw || !write)
        return fail(error,"Limited administration command lost its actual operator");
    if (!strcmp(name,"listlrconcmds")) {
        if (!write(context,admin->prefix_count ? "id command\n-- -------\n" : "No lrconcmds registered.\n",error)) return false;
        for (size_t i=0;i<admin->prefix_count;++i) {
            char row[1100]; (void)snprintf(row,sizeof(row),"%2zu %s\n",i+1,admin->prefixes[i]);
            if (!write(context,row,error)) return false;
        }
        return true;
    }
    bool add=!strcmp(name,"addlrconcmd");
    if (!add && strcmp(name,"dellrconcmd")) return fail(error,"Unknown limited administration operation");
    if (!*raw) return write(context,add ? "Usage: addlrconcmd <command>\n" : "Usage: dellrconcmd <id|cmd|all>\n",error);
    size_t size=strlen(raw);
    if (size>=2 && raw[0]=='"' && raw[size-1]=='"') { ++raw; size-=2; }
    if (!size || size>1023) return fail(error,"Limited administration prefix exceeds its retained extent");
    char value[1024]; memcpy(value,raw,size); value[size]=0;
    size_t at=0; while (at<admin->prefix_count && strcmp(value,admin->prefixes[at])) ++at;
    if (add) {
        if (at<admin->prefix_count) {
            char row[1100]; (void)snprintf(row,sizeof(row),"Lrconcmd already exists: %s\n",value); return write(context,row,error);
        }
        if (admin->prefix_count==256) return fail(error,"Limited administration prefix capacity exhausted");
        char *owned=copy(value);
        if (!owned) { qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining limited administration prefix"); return false; }
        char **next=realloc(admin->prefixes,(admin->prefix_count+1)*sizeof(*next));
        if (!next) { free(owned); qa_error_set(error,QA_ERROR_MEMORY,0,"Growing limited administration prefixes"); return false; }
        admin->prefixes=next; next[admin->prefix_count++]=owned; return true;
    }
    if (!admin->prefix_count) return write(context,"No lrconcmds registered.\n",error);
    if (!strcmp(value,"all")) { strings_free(admin->prefixes,admin->prefix_count); admin->prefixes=NULL; admin->prefix_count=0; return true; }
    bool numbered=true; size_t index=0;
    for (size_t i=0;i<size;++i) {
        if (value[i]<'0' || value[i]>'9') { numbered=false; break; }
        unsigned digit=(unsigned)(value[i]-'0');
        if (index>(SIZE_MAX-digit)/10) index=SIZE_MAX; else index=index*10+digit;
    }
    if (numbered) at=index && index<=admin->prefix_count ? index-1 : admin->prefix_count;
    if (at==admin->prefix_count) {
        char row[1100]; (void)snprintf(row,sizeof(row),numbered ? "No such lrconcmd index: %s\n" : "No such lrconcmd string: %s\n",value);
        return write(context,row,error);
    }
    free(admin->prefixes[at]); memmove(admin->prefixes+at,admin->prefixes+at+1,(admin->prefix_count-at-1)*sizeof(*admin->prefixes));
    --admin->prefix_count; return true;
}
bool qa_server_admin_filters_text(const qa_server_admin *admin,bool commands,
    qa_console_dialect dialect,bool deny,qa_buffer *out,qa_error *error) {
    if (!admin || (admin->callback && !admin->executing) || !out || out->data || out->size)
        return fail(error,"Filter text requires its retained operator and empty output");
    size_t capacity=64+admin->filter_count*32;
    char *text=malloc(capacity);
    if (!text) { qa_error_set(error,QA_ERROR_MEMORY,0,"Encoding actual server filter commands"); return false; }
    int initial=commands ? snprintf(text,capacity,"set filterban %u\n",deny?1u:0u) : snprintf(text,capacity,"Filter list:\n");
    size_t used=(size_t)initial;
    const char *prefix=commands ? (dialect==QA_CONSOLE_Q2 || dialect==QA_CONSOLE_Q2_RERELEASE ? "sv addip " : "addip ") : "";
    for (size_t i=0;i<admin->filter_count;++i) {
        const uint8_t *v=admin->filters[i].compare;
        int n=snprintf(text+used,capacity-used,"%s%u.%u.%u.%u\n",prefix,(unsigned)v[0],(unsigned)v[1],(unsigned)v[2],(unsigned)v[3]);
        if (n<0 || (size_t)n>=capacity-used) { free(text); return fail(error,"Filter text exceeds its actual extent"); }
        used+=(size_t)n;
    }
    if (!commands) { text[used++]='\n'; text[used]=0; }
    *out=(qa_buffer){(uint8_t *)text,used}; return true;
}
static bool limited(qa_server_admin *admin, const char *command) {
    /* Limited commands cannot smuggle a second command through separators. */
    for (const char *p = command; *p; ++p) if (*p == ';' || *p == '\n' || *p == '\r') return false;
    for (size_t i = 0; i < admin->prefix_count; ++i)
        if (!strncmp(command, admin->prefixes[i], strlen(admin->prefixes[i]))) return true;
    return false;
}
static bool equal_secret(const char *left, const char *right) {
    size_t a = strlen(left), b = strlen(right), n = a > b ? a : b; size_t different = a ^ b;
    for (size_t i = 0; i < n; ++i) different |= (size_t)((i < a ? (unsigned char)left[i] : 0) ^ (i < b ? (unsigned char)right[i] : 0));
    return different == 0;
}
static uint32_t rate_unsigned(const char *text,size_t *offset)
{
    size_t at=*offset;
    while (text[at] && strchr(" \t\n\v\f\r",text[at])) ++at;
    bool negative=text[at]=='-'; if (text[at]=='+' || negative) ++at;
    size_t first=at; uint64_t value=0; bool overflow=false;
    while (text[at]>='0' && text[at]<='9') {
        unsigned digit=(unsigned)(text[at++]-'0');
        if (value>(UINT64_MAX-digit)/10) overflow=true;
        else if (!overflow) value=value*10+digit;
    }
    if (at==first) return 0;
    *offset=at;
    return overflow?UINT32_MAX:negative?0u-(uint32_t)value:(uint32_t)value;
}
static uint32_t rate_credits(uint32_t rate)
{
    return rate>UINT32_MAX/32000u?(rate/10000u)*32000u:
        (uint32_t)((uint64_t)rate*32000u/10000u);
}
static bool rerelease_allow(qa_server_admin *admin,uint64_t now,qa_error *error)
{
    bool outer=admin->callback; admin->callback=true;
    qa_cvars *registry=admin->options.hooks.rate_registry(admin->options.hooks.context);
    admin->callback=outer;
    const qa_cvar_view *setting=registry?qa_cvars_find(registry,"sv_rcon_limit"):NULL;
    if (!setting || qa_cvars_dialect(registry)!=QA_CONSOLE_Q2_RERELEASE)
        return fail(error,"Rerelease RCON requires its actual Source rate declaration");
    uint32_t time=(uint32_t)(now/UINT64_C(1000000));
    if (!admin->rate_text || strcmp(admin->rate_text,setting->value) ||
        (admin->rate_registry==registry && admin->rate_revision!=setting->modification_count)) {
        size_t extent=strlen(setting->value);
        if (extent>65535) return fail(error,"Source RCON rate setting exceeds its retained extent");
        char *text=copy(setting->value);
        if (!text) { qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining Source RCON rate recipe"); return false; }
        size_t cursor=0; uint32_t limit=rate_unsigned(text,&cursor),period=1,multiplier=1;
        if (text[cursor]=='/') {
            ++cursor; period=rate_unsigned(text,&cursor); if (!period) period=1;
            char unit=text[cursor]; if (unit>='A' && unit<='Z') unit=(char)(unit-'A'+'a');
            if (unit=='s' || unit=='m' || unit=='h') { multiplier=unit=='h'?3600u:unit=='m'?60u:1u; ++cursor; }
        }
        const char *diagnostic=NULL; uint32_t burst=5;
        if (!limit) { admin->rate_time=0; admin->credit=admin->credit_cap=admin->credit_cost=0; }
        else if (period>UINT32_MAX/(10000u*multiplier)) diagnostic="Period too large";
        else {
            uint32_t rate=10000u*period*multiplier/limit;
            const char *star=strchr(text+cursor,'*');
            if (star) { cursor=(size_t)(star-text)+1; burst=rate_unsigned(text,&cursor); }
            if (!rate) diagnostic="Limit too large";
            else if (burst>UINT32_MAX/rate) diagnostic="Burst too large";
            else { admin->rate_time=time; admin->credit=admin->credit_cap=rate_credits(rate*burst); admin->credit_cost=rate_credits(rate); }
        }
        if (diagnostic && admin->options.hooks.print) {
            char message[96]; uint32_t value=!strcmp(diagnostic,"Period too large")?period:
                !strcmp(diagnostic,"Limit too large")?limit:burst;
            snprintf(message,sizeof(message),"%s: %u\n",diagnostic,(unsigned)value);
            outer=admin->callback; admin->callback=true;
            admin->options.hooks.print(admin->options.hooks.context,message); admin->callback=outer;
        }
        free(admin->rate_text); admin->rate_text=text;
    }
    admin->rate_registry=registry; admin->rate_revision=setting->modification_count;
    admin->credit+=(time-admin->rate_time)*32u; admin->rate_time=time;
    if (admin->credit>admin->credit_cap) admin->credit=admin->credit_cap;
    if (admin->credit<admin->credit_cost) return false;
    admin->credit-=admin->credit_cost; return true;
}
static bool allow(qa_server_admin *admin, const qa_net_address *address, uint64_t now,qa_error *error) {
    if (admin->options.dialect==QA_CONSOLE_Q2_RERELEASE && admin->options.hooks.rate_registry)
        return rerelease_allow(admin,now,error);
    if (admin->options.dialect == QA_CONSOLE_Q3) {
        uint32_t milliseconds = (uint32_t)(now / 1000000), previous = (uint32_t)(admin->rcon_time / 1000000);
        if (milliseconds < previous + 500u) return false;
        admin->rcon_time = now; admin->rcon_sent = true; return true;
    }
    rate_entry *entry = NULL, *oldest = &admin->rates[0];
    for (uint32_t i = 0; i < admin->options.rate_entries; ++i) {
        rate_entry *candidate = &admin->rates[i];
        if (candidate->active && qa_net_address_equal(&candidate->address, address, false)) { entry = candidate; break; }
        if (!candidate->active && !entry) entry = candidate;
        if (candidate->time < oldest->time) oldest = candidate;
    }
    if (!entry) entry = oldest;
    if (!entry->active || !qa_net_address_equal(&entry->address, address, false))
        *entry = (rate_entry){.address = *address, .time = now, .tokens = admin->options.burst, .active = true};
    if (now >= entry->time) {
        entry->tokens += (double)(now - entry->time) / (double)admin->options.rate_interval_ns;
        if (entry->tokens > admin->options.burst) entry->tokens = admin->options.burst;
        entry->time = now;
    }
    if (entry->tokens < 1) return false;
    entry->tokens -= 1; return true;
}
typedef struct rcon_output { qa_server_admin *admin; qa_net_address address; char text[1009]; size_t size; } rcon_output;
static bool flush(rcon_output *output, qa_error *error) {
    if (!output->size) return true;
    uint8_t bytes[1030]; qa_net_writer writer; qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
    bool ok;
    if (output->admin->options.dialect == QA_CONSOLE_QW) {
        qa_net_write_u32(&writer, UINT32_MAX); qa_net_write_u8(&writer, 'n');
        ok = qa_net_write_string(&writer, output->text);
    } else {
        char message[1020]; memcpy(message, "print\n", 6); memcpy(message + 6, output->text, output->size + 1);
        ok = qa_q2_oob_write(&writer, message);
    }
    if (ok) ok = output->admin->options.hooks.send(output->admin->options.hooks.context,
        &output->address, (qa_bytes){bytes, qa_net_writer_size(&writer)}, error);
    if (ok) output->size = 0;
    return ok;
}
static bool write_output(void *context, const char *text, qa_error *error) {
    rcon_output *output = context; if (!text) return fail(error, "Missing administration output");
    for (; *text; ++text) {
        if (output->size == sizeof(output->text) - 1 && !flush(output, error)) return false;
        output->text[output->size++] = *text; output->text[output->size] = 0;
    }
    return true;
}
bool qa_server_admin_receive(qa_server_admin *admin, const qa_net_datagram *packet, qa_admin_result *out, qa_error *error) {
    if (!admin || admin->callback || !packet || !out) return fail(error, "Invalid remote administration delivery");
    *out = QA_ADMIN_IGNORED;
    if (packet->kind != QA_NET_POLL_PACKET || packet->payload.size < 5 ||
        qa_load_u32le(packet->payload.data) != UINT32_MAX || packet->payload.size > 16384) return true;
    qa_bytes text = {packet->payload.data + 4, packet->payload.size - 4};
    if (text.size && !text.data[text.size - 1]) --text.size;
    if (memchr(text.data, 0, text.size)) return true;
    qa_tokenizer lexer;
    qa_tokenizer_options options = {.maximum_units = 8192, .reject_quoted_newlines = true};
    if (!qa_tokenizer_init_options(&lexer, text, &options, error)) return false;
    qa_token token; bool found;
    if (!qa_tokenizer_next(&lexer, &token, &found, error)) return false;
    if (!found || token.text.size != 4 || memcmp(token.text.data, "rcon", 4)) return true;
    qa_error rate_error={0};
    if (!allow(admin, &packet->from, packet->received_ns,&rate_error)) {
        if (rate_error.code!=QA_OK) { if (error) *error=rate_error; return false; }
        *out = QA_ADMIN_THROTTLED; return true;
    }
    char supplied[8193] = {0};
    if (!qa_tokenizer_next(&lexer, &token, &found, error)) return false;
    if (found && !qa_token_copy(&token, supplied, sizeof(supplied), error)) return false;
    size_t offset = lexer.offset;
    if (admin->options.dialect==QA_CONSOLE_Q3) {
        offset=4;
        while (offset<text.size && text.data[offset]==' ') ++offset;
        while (offset<text.size && text.data[offset]!=' ') ++offset;
        while (offset<text.size && text.data[offset]==' ') ++offset;
    } else while (offset < text.size && (text.data[offset] == ' ' || text.data[offset] == '\t')) ++offset;
    size_t length = text.size - offset;
    if (admin->options.dialect == QA_CONSOLE_Q3 && length > 1023) length = 1023;
    char command[16385]; memcpy(command, text.data + offset, length); command[length] = 0;
    admin->callback = true;
    const char *full = admin->options.hooks.password(admin->options.hooks.context, false);
    bool is_full = full && *full && equal_secret(full, supplied);
    bool disabled = !full || !*full;
    const char *small = admin->options.hooks.password(admin->options.hooks.context, true);
    bool valid_limited = !is_full && small && *small && equal_secret(small, supplied);
    bool is_limited = valid_limited && limited(admin, command);
    if ((is_full || valid_limited) && admin->rate_text && admin->options.dialect==QA_CONSOLE_Q2_RERELEASE) {
        admin->credit+=admin->credit_cost; if (admin->credit>admin->credit_cap) admin->credit=admin->credit_cap;
    }
    memset(supplied, 0, sizeof(supplied));
    rcon_output output = {.admin = admin, .address = packet->from};
    bool ok;
    if (!is_full && !is_limited) {
        *out = disabled ? QA_ADMIN_DISABLED : QA_ADMIN_DENIED;
        ok = write_output(&output, disabled ? "No rconpassword set on the server.\n" :
            admin->options.dialect==QA_CONSOLE_Q3?"Bad rconpassword.\n":"Bad rconpassword or command not permitted.\n", error);
    } else {
        *out = QA_ADMIN_EXECUTED;
        admin->executing=true;
        ok = !*command || admin->options.hooks.execute(admin->options.hooks.context, &packet->from, command,
            is_limited, write_output, &output, error);
        admin->executing=false;
    }
    if (ok) ok = flush(&output, error);
    if (admin->options.hooks.record) admin->options.hooks.record(admin->options.hooks.context, &packet->from, *out);
    admin->callback = false; return ok;
}
static size_t master_group(qa_console_dialect dialect)
{
    return dialect==QA_CONSOLE_QW?0:
        dialect==QA_CONSOLE_Q2 || dialect==QA_CONSOLE_Q2_RERELEASE?1:
        dialect==QA_CONSOLE_Q3?2:3;
}
bool qa_server_admin_source_masters(qa_server_admin *admin,qa_console_dialect dialect,
    const qa_net_address *addresses,size_t count,qa_error *error) {
    if (!admin || (admin->callback && !admin->executing) || dialect>QA_CONSOLE_Q3 || count > 32 || (count && !addresses)) return fail(error, "Invalid server masters");
    for (size_t i = 0; i < count; ++i)
        if (!service_address_valid(addresses + i)) return fail(error, "Invalid server master address");
    qa_net_address *owned = count ? malloc(count * sizeof(*owned)) : NULL;
    if (count && !owned) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining server masters"); return false; }
    if (count) memcpy(owned, addresses, count * sizeof(*owned));
    size_t group=master_group(dialect);
    free(admin->masters[group]); admin->masters[group]=owned; admin->master_count[group]=count;
    admin->heartbeat_sent=false; return true;
}
bool qa_server_admin_masters(qa_server_admin *admin,const qa_net_address *addresses,size_t count,qa_error *error)
{
    if (!admin) return fail(error,"Missing actual master owner");
    return qa_server_admin_source_masters(admin,admin->options.dialect,addresses,count,error);
}
bool qa_server_admin_request_heartbeat(qa_server_admin *admin,qa_error *error)
{
    if (!admin || (admin->callback && !admin->executing)) return fail(error,"Heartbeat request lost its actual operator");
    admin->heartbeat_sent=false; return true;
}
bool qa_server_admin_refresh_masters(qa_server_admin *admin,qa_cvars *registry,qa_error *error)
{
    if (!admin || admin->callback || !registry || admin->options.dialect!=QA_CONSOLE_Q3 ||
        qa_cvars_dialect(registry)!=QA_CONSOLE_Q3)
        return fail(error,"Q3 master refresh requires its returned Source registry");
    const char *values[5]; size_t size=0;
    for (unsigned i=0;i<5;++i) {
        char name[16]; snprintf(name,sizeof(name),"sv_master%u",i+1);
        const qa_cvar_view *v=qa_cvars_find(registry,name);
        if (!v) return fail(error,"Q3 master refresh has no actual Source declaration");
        values[i]=v->value; size_t length=strlen(v->value)+1;
        if (length>65536-size) return fail(error,"Q3 master names exceed their retained extent");
        size+=length;
    }
    uint8_t *names=malloc(size);
    if (!names) { qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining actual Q3 master names"); return false; }
    size_t at=0;
    for (size_t i=0;i<5;++i) { size_t length=strlen(values[i])+1; memcpy(names+at,values[i],length); at+=length; }
    if (admin->master_names.size==size && !memcmp(admin->master_names.data,names,size)) { free(names); return true; }
    qa_net_address addresses[5]; size_t count=0; at=0;
    for (size_t i=0;i<5;++i) {
        const char *name=(const char *)names+at; at+=strlen(name)+1; if (!*name) continue;
        qa_error local={0};
        if (qa_net_address_resolve(name,27950,4,&addresses[count],&local)) ++count;
        else if (admin->options.hooks.print) {
            char message[768]; snprintf(message,sizeof(message),"Bad master address %.255s: %.400s\n",name,local.message);
            admin->callback=true; admin->options.hooks.print(admin->options.hooks.context,message); admin->callback=false;
        }
    }
    if (!qa_server_admin_masters(admin,addresses,count,error)) { free(names); return false; }
    qa_buffer_free(&admin->master_names); admin->master_names=(qa_buffer){names,size}; return true;
}
static bool heartbeat(qa_server_admin *admin, bool shutdown, qa_error *error) {
    uint8_t bytes[128]; qa_net_writer writer; qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
    bool ok;
    if (admin->options.dialect == QA_CONSOLE_QW) {
        uint32_t players=0;
        if (!shutdown && !admin->options.hooks.players(admin->options.hooks.context,&players,error)) return false;
        ok = shutdown ? qa_qw_shutdown(&writer) : qa_qw_heartbeat(++admin->heartbeat_sequence,players,&writer);
    }
    else if (admin->options.dialect == QA_CONSOLE_Q3)
        ok = qa_q2_oob_write(&writer, shutdown ? "heartbeat flatline\n" : "heartbeat QuakeArena-1\n");
    else ok = qa_q2_oob_write(&writer, shutdown ? "shutdown\n" : "heartbeat\n");
    if (!ok) return false;
    size_t group=master_group(admin->options.dialect);
    if (group==1) {
        const qa_net_address builtin={.kind=QA_NET_IPV4,.port=27900,.host.ipv4={192,246,40,37}};
        if (!admin->options.hooks.send(admin->options.hooks.context,&builtin,
            (qa_bytes){bytes,qa_net_writer_size(&writer)},error)) return false;
    }
    for (size_t i = 0; i < admin->master_count[group]; ++i)
        if (!admin->options.hooks.send(admin->options.hooks.context, &admin->masters[group][i],
            (qa_bytes){bytes, qa_net_writer_size(&writer)}, error)) return false;
    return true;
}
bool qa_server_admin_tick(qa_server_admin *admin, uint64_t now, bool force, qa_error *error) {
    if (!admin || (admin->callback && !admin->executing)) return fail(error, "Invalid administration heartbeat");
    size_t group=master_group(admin->options.dialect);
    if (!admin->options.public_server || group==3 || (!admin->master_count[group] && group!=1)) return true;
    if (!force && admin->heartbeat_sent && (now < admin->heartbeat_time || now - admin->heartbeat_time < admin->options.heartbeat_interval_ns)) return true;
    bool outer_callback = admin->callback, outer_execution = admin->executing;
    admin->callback = true; admin->executing = false;
    bool ok = heartbeat(admin, false, error);
    admin->callback = outer_callback; admin->executing = outer_execution;
    if (ok) { admin->heartbeat_sent = true; admin->heartbeat_time = now; } return ok;
}
bool qa_server_admin_shutdown(qa_server_admin *admin, qa_error *error) {
    if (!admin || admin->callback) return fail(error, "Invalid server shutdown");
    if (!admin->options.public_server || master_group(admin->options.dialect)==3) return true;
    admin->callback = true; bool ok = heartbeat(admin, true, error); admin->callback = false; return ok;
}
static bool map_name(const char *text) {
    if (!text || !*text || strlen(text) > 127) return false;
    bool segment = false;
    for (; *text; ++text) {
        unsigned char c = (unsigned char)*text;
        if (c == '/') { if (!segment) return false; segment = false; }
        else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') segment = true;
        else return false;
    }
    return segment;
}
bool qa_server_admin_rotation(qa_server_admin *admin, const char *const *maps, size_t count, bool shuffle, qa_error *error) {
    if (!admin || (admin->callback && !admin->executing) || count > 256 || (count && !maps)) return fail(error, "Invalid map rotation");
    for (size_t i = 0; i < count; ++i) if (!map_name(maps[i])) return fail(error, "Invalid rotation map name");
    char **owned; if (!strings_copy(maps, count, 127, &owned, error)) return false;
    strings_free(admin->rotation, admin->rotation_count); admin->rotation = owned;
    admin->rotation_count = count; admin->rotation_index = 0; admin->shuffle = shuffle; return true;
}
bool qa_server_admin_next_map(qa_server_admin *admin, const char *current, bool *rotated, qa_error *error) {
    if (!admin || (admin->callback && !admin->executing) || !rotated) return fail(error, "Invalid rotation transition");
    *rotated = false; if (!admin->rotation_count) return true;
    size_t next = admin->rotation_index;
    if (current) for (size_t i = 0; i < admin->rotation_count; ++i)
        if (!strcmp(admin->rotation[i], current)) { next = (i + 1) % admin->rotation_count; break; }
    bool outer_callback = admin->callback, outer_execution = admin->executing;
    admin->callback = true; admin->executing = false;
    bool ok = admin->options.hooks.travel(admin->options.hooks.context, admin->rotation[next], error);
    if (ok) {
        admin->rotation_index = (next + 1) % admin->rotation_count; *rotated = true;
        if (!admin->rotation_index && admin->shuffle) {
            for (size_t i = admin->rotation_count - 1; i > 0; --i) {
                size_t other = admin->options.hooks.random(admin->options.hooks.context) % (i + 1);
                char *temporary = admin->rotation[i]; admin->rotation[i] = admin->rotation[other]; admin->rotation[other] = temporary;
            }
        }
    }
    admin->callback = outer_callback; admin->executing = outer_execution; return ok;
}
bool qa_server_admin_save_filters(const qa_server_admin *admin, qa_buffer *out, qa_error *error) {
    if (!admin || !out) return fail(error, "Missing server filter output");
    size_t size = 13 + admin->filter_count * 8;
    uint8_t *data = malloc(size); if (!data) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Encoding server filters"); return false; }
    qa_net_writer writer; qa_net_writer_init(&writer, data, size, error);
    qa_net_write_data(&writer, "QAIP", 4); qa_net_write_u32(&writer, 1);
    qa_net_write_u8(&writer, admin->options.deny_matches ? 1 : 0); qa_net_write_u32(&writer, (uint32_t)admin->filter_count);
    for (size_t i = 0; i < admin->filter_count; ++i) {
        qa_net_write_data(&writer, admin->filters[i].mask, 4); qa_net_write_data(&writer, admin->filters[i].compare, 4);
    }
    if (writer.failed) { free(data); return false; } *out = (qa_buffer){data, size}; return true;
}
bool qa_server_admin_restore_filters(qa_server_admin *admin, qa_bytes bytes, qa_error *error) {
    if (!admin || admin->callback || bytes.size < 13 || !bytes.data || memcmp(bytes.data, "QAIP", 4)) return fail(error, "Invalid saved server filters");
    qa_net_reader reader; qa_net_reader_init(&reader, bytes, error); reader.bit = 32;
    if (qa_net_read_u32(&reader) != 1) return fail(error, "Unsupported server filter version");
    uint8_t deny = qa_net_read_u8(&reader); uint32_t count = qa_net_read_u32(&reader);
    if (deny > 1 || count > admin->options.filters || bytes.size != 13 + (size_t)count * 8) return fail(error, "Invalid server filter extent");
    ip_filter *candidate = calloc(admin->options.filters, sizeof(*candidate));
    if (!candidate) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Restoring server filters"); return false; }
    bool ok = true;
    for (uint32_t i = 0; ok && i < count; ++i) {
        ok = qa_net_read_data(&reader, candidate[i].mask, 4) && qa_net_read_data(&reader, candidate[i].compare, 4);
        for (size_t n = 0; ok && n < 4; ++n) if ((candidate[i].mask[n] != 0 && candidate[i].mask[n] != 255) ||
            (candidate[i].compare[n] & candidate[i].mask[n]) != candidate[i].compare[n]) ok = false;
    }
    if (ok) ok = qa_net_reader_finish(&reader);
    if (!ok) { free(candidate); return fail(error, "Invalid stored source filter"); }
    free(admin->filters); admin->filters = candidate; admin->filter_count = count; admin->options.deny_matches = deny != 0; return true;
}

static bool admin_checkpoint_valid(const qa_server_admin *a)
{
    if (!a || a->callback || a->filter_count > a->options.filters || a->prefix_count > 256 ||
        a->rotation_count > 256 || a->master_names.size>65536 ||
        (a->rotation_count ? a->rotation_index >= a->rotation_count : a->rotation_index != 0) ||
        (a->rate_text && (strlen(a->rate_text)>65535 || a->credit>a->credit_cap))) return false;
    for (size_t i = 0; i < a->filter_count; ++i) {
        for (size_t j = 0; j < 4; ++j) if ((a->filters[i].mask[j] != 0 && a->filters[i].mask[j] != 255) ||
            (a->filters[i].compare[j] & a->filters[i].mask[j]) != a->filters[i].compare[j]) return false;
        for (size_t j = 0; j < i; ++j) if (!memcmp(&a->filters[i], &a->filters[j], sizeof(ip_filter))) return false;
    }
    for (uint32_t i = 0; i < a->options.rate_entries; ++i) {
        const rate_entry *v = &a->rates[i]; if (!v->active) continue;
        if (!service_address_valid(&v->address) || !isfinite(v->tokens) || v->tokens < 0 || v->tokens > a->options.burst) return false;
        for (uint32_t j = 0; j < i; ++j) if (a->rates[j].active && qa_net_address_equal(&v->address, &a->rates[j].address, false)) return false;
    }
    for (size_t i = 0; i < a->prefix_count; ++i)
        if (!a->prefixes[i] || !*a->prefixes[i] || strlen(a->prefixes[i]) > 1023) return false;
    for (size_t i = 0; i < a->rotation_count; ++i) if (!map_name(a->rotation[i])) return false;
    for (size_t group=0;group<4;++group) {
        if (a->master_count[group]>32 || (a->master_count[group] && !a->masters[group])) return false;
        for (size_t i=0;i<a->master_count[group];++i) if (!service_address_valid(&a->masters[group][i])) return false;
    }
    if (a->master_names.size) {
        if (!a->master_names.data) return false;
        size_t at=0;
        for (size_t i=0;i<5;++i) {
            if (at>=a->master_names.size) return false;
            const uint8_t *end=memchr(a->master_names.data+at,0,a->master_names.size-at);
            if (!end) return false;
            at=(size_t)(end-a->master_names.data)+1;
        }
        if (at!=a->master_names.size) return false;
    }
    return true;
}
bool qa_server_admin_checkpoint(const qa_server_admin *a, qa_buffer *out, qa_error *error)
{
    if (!out || !admin_checkpoint_valid(a)) return fail(error, "Invalid administration continuation ownership");
    size_t capacity = 2 * 65536 + 512 + (size_t)a->options.filters * 8 + (size_t)a->options.rate_entries * 160 +
        a->prefix_count * 1024 + a->rotation_count * 128 + 4 * 32 * 160;
    uint8_t *data = malloc(capacity);
    if (!data) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Encoding administration continuation"); return false; }
    qa_net_writer w; qa_net_writer_init(&w, data, capacity, error);
    bool ok = qa_net_write_u32(&w, UINT32_C(0x41534151)) &&
        qa_net_write_u32(&w, a->options.dialect) && qa_net_write_u32(&w, a->options.filters) &&
        qa_net_write_u32(&w, a->options.rate_entries) && qa_net_write_u32(&w, a->options.burst) &&
        qa_net_write_u64(&w, a->options.rate_interval_ns) && qa_net_write_u64(&w, a->options.heartbeat_interval_ns) &&
        qa_net_write_u8(&w, a->options.deny_matches) && qa_net_write_u8(&w, a->options.public_server) &&
        qa_net_write_u64(&w, a->heartbeat_time) && qa_net_write_u64(&w, a->rcon_time) &&
        qa_net_write_u32(&w, a->heartbeat_sequence) && qa_net_write_u8(&w, a->heartbeat_sent) &&
        qa_net_write_u8(&w, a->rcon_sent) && qa_net_write_u8(&w, a->shuffle) && qa_net_write_u32(&w, (uint32_t)a->filter_count);
    for (size_t i = 0; ok && i < a->filter_count; ++i)
        ok = qa_net_write_data(&w, a->filters[i].mask, 4) && qa_net_write_data(&w, a->filters[i].compare, 4);
    for (uint32_t i = 0; ok && i < a->options.rate_entries; ++i) {
        const rate_entry *v = &a->rates[i]; ok = qa_net_write_u8(&w, v->active); if (!ok || !v->active) continue;
        ok = q3_save_address(&w, &v->address) && qa_net_write_f64(&w, v->tokens) && qa_net_write_u64(&w, v->time);
    }
    ok = ok && qa_net_write_u32(&w, (uint32_t)a->prefix_count);
    for (size_t i = 0; ok && i < a->prefix_count; ++i) ok = qa_net_write_string(&w, a->prefixes[i]);
    ok = ok && qa_net_write_u32(&w, (uint32_t)a->rotation_count) && qa_net_write_u32(&w, (uint32_t)a->rotation_index);
    for (size_t i = 0; ok && i < a->rotation_count; ++i) ok = qa_net_write_string(&w, a->rotation[i]);
    for (size_t group=0;ok && group<4;++group) {
        ok=qa_net_write_u32(&w,(uint32_t)a->master_count[group]);
        for (size_t i=0;ok && i<a->master_count[group];++i) ok=q3_save_address(&w,&a->masters[group][i]);
    }
    ok=ok && qa_net_write_u32(&w,(uint32_t)a->master_names.size) &&
        qa_net_write_data(&w,a->master_names.data,a->master_names.size);
    ok=ok && qa_net_write_u8(&w,a->options.hooks.rate_registry!=NULL) && qa_net_write_u8(&w,a->rate_text!=NULL);
    if (ok && a->rate_text) ok=qa_net_write_string(&w,a->rate_text) && qa_net_write_u64(&w,a->rate_revision) &&
        qa_net_write_u32(&w,a->rate_time) && qa_net_write_u32(&w,a->credit) &&
        qa_net_write_u32(&w,a->credit_cap) && qa_net_write_u32(&w,a->credit_cost);
    if (!ok || w.failed) { free(data); return false; }
    *out = (qa_buffer){data, qa_net_writer_size(&w)}; return true;
}
bool qa_server_admin_restore_checkpoint(qa_bytes bytes, const qa_admin_options *options,
    qa_server_admin **out, qa_error *error)
{
    if (!out || *out || !options || bytes.size > 2 * 1048576 || (bytes.size && !bytes.data))
        return fail(error, "Invalid administration continuation extent/output");
    qa_net_reader r; qa_net_reader_init(&r, bytes, error);
    uint32_t magic=qa_net_read_u32(&r);
    if (magic != UINT32_C(0x41534151) ||
        qa_net_read_u32(&r) != (uint32_t)options->dialect || qa_net_read_u32(&r) != options->filters ||
        qa_net_read_u32(&r) != options->rate_entries || qa_net_read_u32(&r) != options->burst ||
        qa_net_read_u64(&r) != options->rate_interval_ns || qa_net_read_u64(&r) != options->heartbeat_interval_ns)
        return fail(error, "Administration continuation policy differs");
    qa_server_admin *a = NULL; if (!qa_server_admin_create(options, &a, error)) return false;
    a->options.deny_matches = q3_save_bool(&r); a->options.public_server = q3_save_bool(&r);
    a->heartbeat_time = qa_net_read_u64(&r); a->rcon_time = qa_net_read_u64(&r);
    a->heartbeat_sequence = qa_net_read_u32(&r); a->heartbeat_sent = q3_save_bool(&r);
    a->rcon_sent = q3_save_bool(&r); a->shuffle = q3_save_bool(&r); a->filter_count = qa_net_read_u32(&r);
    bool ok = !r.failed && a->filter_count <= options->filters;
    for (size_t i = 0; ok && i < a->filter_count; ++i)
        ok = qa_net_read_data(&r, a->filters[i].mask, 4) && qa_net_read_data(&r, a->filters[i].compare, 4);
    for (uint32_t i = 0; ok && !r.failed && i < options->rate_entries; ++i) {
        rate_entry *v = &a->rates[i]; v->active = q3_save_bool(&r); if (!v->active) continue;
        ok = q3_restore_address(&r, &v->address); v->tokens = qa_net_read_f64(&r); v->time = qa_net_read_u64(&r);
    }
    a->prefix_count = qa_net_read_u32(&r);
    if (a->prefix_count > 256) { a->prefix_count = 0; ok = false; }
    if (ok && a->prefix_count) { a->prefixes = calloc(a->prefix_count, sizeof(*a->prefixes)); if (!a->prefixes) ok = false; }
    if (!a->prefixes) a->prefix_count = 0;
    for (size_t i = 0; ok && i < a->prefix_count; ++i) ok = service_restore_text(&r, &a->prefixes[i], 1023);
    a->rotation_count = qa_net_read_u32(&r); a->rotation_index = qa_net_read_u32(&r);
    if (a->rotation_count > 256) { a->rotation_count = 0; ok = false; }
    if (ok && a->rotation_count) { a->rotation = calloc(a->rotation_count, sizeof(*a->rotation)); if (!a->rotation) ok = false; }
    if (!a->rotation) a->rotation_count = 0;
    for (size_t i = 0; ok && i < a->rotation_count; ++i) ok = service_restore_text(&r, &a->rotation[i], 127);
    for (size_t group=0;ok && group<4;++group) {
        size_t count=qa_net_read_u32(&r);
        if (r.failed || count>32) { ok=false; break; }
        if (count) {
            a->masters[group]=calloc(count,sizeof(*a->masters[group]));
            if (!a->masters[group]) { qa_error_set(error,QA_ERROR_MEMORY,0,"Restoring master addresses"); ok=false; break; }
        }
        a->master_count[group]=count;
        for (size_t i=0;ok && i<count;++i) ok=q3_restore_address(&r,&a->masters[group][i]);
    }
    if (ok) {
        size_t size=qa_net_read_u32(&r);
        if (r.failed || size>65536) ok=false;
        else if (size) {
            a->master_names.data=malloc(size); a->master_names.size=size;
            if (!a->master_names.data) { qa_error_set(error,QA_ERROR_MEMORY,0,"Restoring master name recipe"); ok=false; }
            else ok=qa_net_read_data(&r,a->master_names.data,size);
        }
    }
    if (ok) {
        bool has_registry=q3_save_bool(&r),has_rate=q3_save_bool(&r);
        ok=!r.failed && has_registry==(options->hooks.rate_registry!=NULL) && (!has_rate || has_registry);
        if (ok && has_rate) {
            a->rate_text=calloc(65536,1);
            if (!a->rate_text) { qa_error_set(error,QA_ERROR_MEMORY,0,"Restoring Source RCON rate recipe"); ok=false; }
            else ok=qa_net_read_string(&r,a->rate_text,65536);
            a->rate_revision=qa_net_read_u64(&r); a->rate_time=qa_net_read_u32(&r);
            a->credit=qa_net_read_u32(&r); a->credit_cap=qa_net_read_u32(&r); a->credit_cost=qa_net_read_u32(&r);
        }
    }
    if (!ok || !qa_net_reader_finish(&r) || !admin_checkpoint_valid(a)) {
        qa_server_admin_destroy(a); return fail(error, "Invalid administration continuation fields");
    }
    *out = a; return true;
}
