/* Donor: network/q3/client.ts, snapshot-history.ts, client-server-command.ts. */
#include "client_private.h"
#include "qa/text.h"
#include <limits.h>
#include <stdio.h>

static bool fail(qa_error *e, qa_status code, const char *s) { qa_error_set(e, code, 0, "%s", s); return false; }
static bool current(const qa_q3_client_peer *p, qa_error *e) {
    if (p->hooks.generation(p->hooks.context) != p->generation) return fail(e, QA_ERROR_FORMAT, "Q3 client callback belongs to a retired session");
    return true;
}
const char *q3_client_command_text(const qa_q3_client_peer *p,size_t slot)
{
    const qa_q3_command *record=p->server_commands[slot] ? qa_event_lease_record(p->server_commands[slot]) : NULL;
    return record ? record->text : "";
}
static const char *lookup(void *user, int32_t sequence)
{ return q3_client_command_text(user,(uint32_t)sequence & (QA_Q3_RELIABLE-1)); }
bool q3_client_command_admit(qa_q3_client_peer *p,const qa_q3_command *value,qa_error *e)
{
    /* The wire keeps its original 64-command XOR history. Presentation reads
     * immutable records from the same pages, with an independent cursor. */
    uint64_t first=qa_event_ring_first(p->command_events),next=qa_event_ring_next(p->command_events);
    while(first<next) {
        const qa_q3_command *row=qa_event_ring_at(p->command_events,first);
        if((int64_t)row->sequence>(int64_t)value->sequence-QA_Q3_RELIABLE) break;
        ++first;
    }
    qa_event_ring_retire(p->command_events,first);
    qa_event_transaction transaction;
    if(!qa_event_ring_begin(p->command_events,&transaction))
        return fail(e,QA_ERROR_FORMAT,"Q3 received command history is full");
    qa_q3_command *record=qa_event_ring_alloc(&transaction,sizeof(*record),_Alignof(qa_q3_command),e);
    if(!record) { qa_event_ring_abort(&transaction); return false; }
    *record=*value;
    uint64_t id=qa_event_ring_commit(&transaction,record);
    if(!id) return fail(e,QA_ERROR_FORMAT,"Q3 received command history is full");
    size_t slot=(uint32_t)value->sequence & (QA_Q3_RELIABLE-1);
    qa_event_lease *old=p->server_commands[slot];
    p->server_commands[slot]=qa_event_ring_retain(p->command_events,id);
    if(old) qa_event_lease_release(old);
    return true;
}
static const qa_q3_snapshot *history(void *user, int32_t sequence) { return &((qa_q3_client_peer *)user)->history[(uint32_t)sequence & 31].value; }
static bool system_info(qa_q3_client_peer *p, qa_error *e) {
    const char *text = qa_q3_configstring(&p->gamestate, 1);
    if (!text || !qa_q3_client_command_state_update(QA_Q3_SERVER_SYSTEM_INFO,text,
        &p->server_id,p->commands,64,e)) return false;
    return p->hooks.system_info(p->hooks.context, text, e) && current(p, e);
}
static bool create_peer(qa_q3_identity identity, qa_q3_product product, const qa_net_address *remote,
                               int32_t challenge, uint16_t qport, const qa_q3_client_hooks *hooks,
                               bool demo, qa_q3_client_peer **out, qa_error *e) {
    if ((!remote && !demo) || !hooks || !out || !hooks->generation || !hooks->gamestate || !hooks->system_info
        || !hooks->clear_active || !hooks->download_size || !hooks->snapshot || !hooks->download || !hooks->command || !hooks->map_restart
        || !hooks->disconnect || !hooks->level_shot || !hooks->local_server_running || (!demo && !hooks->send)
        || (product != QA_Q3_ARENA && product != QA_Q3_TEAM_ARENA))
        return fail(e, QA_ERROR_ARGUMENT, "Q3 client peer requires complete session bindings");
    qa_q3_client_peer *p = calloc(1, sizeof(*p));
    if (!p) return fail(e, QA_ERROR_MEMORY, "Allocating Q3 client peer");
    if (!demo && !qa_q3_channel_create(QA_Q3_CLIENT, qport, &p->channel, e)) { free(p); return false; }
    p->command_events=qa_event_ring_create((QA_Q3_RELIABLE+2)*4096,2048,QA_Q3_RELIABLE+2,e);
    if(!p->command_events) { qa_q3_channel_destroy(p->channel); free(p); return false; }
    if(!qa_q3_reliable_prepare(&p->reliable,e)) { qa_q3_client_peer_destroy(p);return false; }
    p->command_cursor=qa_event_ring_first(p->command_events);
    p->identity = identity; p->product = product; if (remote) p->remote = *remote; p->demo = demo; p->challenge = challenge; p->hooks = *hooks;
    p->generation = hooks->generation(hooks->context); p->demo_waiting = true;
    qa_q3_gamestate_init(&p->gamestate);
    for (unsigned i = 0; i < QA_Q3_PACKET_BACKUP; ++i) qa_q3_slot_clear(&p->history[i], product);
    *out = p; return true;
}
bool qa_q3_client_peer_create(qa_q3_identity identity, qa_q3_product product, const qa_net_address *remote,
                               int32_t challenge, uint16_t qport, const qa_q3_client_hooks *hooks,
                               qa_q3_client_peer **out, qa_error *e) {
    return create_peer(identity, product, remote, challenge, qport, hooks, false, out, e);
}
bool qa_q3_client_peer_create_demo(qa_q3_identity identity, qa_q3_product product,
                                    const qa_q3_client_hooks *hooks, qa_q3_client_peer **out, qa_error *e) {
    return create_peer(identity, product, NULL, 0, 0, hooks, true, out, e);
}
void qa_q3_client_peer_destroy(qa_q3_client_peer *p) {
    if (!p) return;
    qa_q3_channel_destroy(p->channel);
    qa_q3_reliable_clear(&p->reliable);
    for(size_t i=0;i<QA_Q3_RELIABLE;++i) if(p->server_commands[i]) qa_event_lease_release(p->server_commands[i]);
    qa_event_ring_destroy(&p->command_events);
    for (unsigned i = 0; i < QA_Q3_PACKET_BACKUP; ++i) free(p->history[i].entities);
    free(p);
}
const qa_q3_identity *qa_q3_client_peer_identity(const qa_q3_client_peer *p) { return &p->identity; }
qa_q3_product qa_q3_client_peer_product(const qa_q3_client_peer *p) { return p->product; }
bool qa_q3_client_peer_demo(const qa_q3_client_peer *p) { return p && p->demo; }
const qa_q3_gamestate *qa_q3_client_peer_gamestate(const qa_q3_client_peer *p) { return &p->gamestate; }
const qa_q3_snapshot *qa_q3_client_peer_snapshot(const qa_q3_client_peer *p) { return p->has_snapshot ? &p->history[(uint32_t)p->latest_snapshot & 31].value : NULL; }
const qa_q3_snapshot *qa_q3_client_peer_snapshot_at(const qa_q3_client_peer *p, int32_t number) {
    if (!p || !p->has_snapshot || number < 0 || number > p->latest_snapshot ||
        (int64_t)p->latest_snapshot - number >= QA_Q3_PACKET_BACKUP) return NULL;
    const qa_q3_snapshot *value = &p->history[(uint32_t)number & (QA_Q3_PACKET_BACKUP - 1)].value;
    return value->valid && value->message_number == number ? value : NULL;
}
const qa_q3_snapshot *qa_q3_client_peer_presentation_snapshot_at(const qa_q3_client_peer *p, int32_t number) {
    const qa_q3_snapshot *snapshot = qa_q3_client_peer_snapshot_at(p, number);
    return snapshot && p->parse_entities_number >= snapshot->parse_entities_number &&
        p->parse_entities_number - snapshot->parse_entities_number < QA_Q3_PARSE_ENTITIES ? snapshot : NULL;
}
bool qa_q3_client_peer_command(qa_q3_client_peer *p, const char *text, qa_error *e) {
    if (!p || p->disconnected || p->disconnect_started) return fail(e, QA_ERROR_ARGUMENT, "Q3 client is disconnected or closing");
    return current(p, e) && qa_q3_reliable_add(&p->reliable, QA_Q3_CLIENT, text, e);
}
bool qa_q3_client_peer_usercmd(qa_q3_client_peer *p, const qa_q3_usercmd *command, qa_error *e) {
    if (!p || !command || p->disconnected || p->disconnect_started || p->command_number == UINT64_MAX)
        return fail(e, QA_ERROR_ARGUMENT, "Invalid Q3 generated user command");
    if (!current(p, e)) return false;
    ++p->command_number; p->commands[p->command_number & 63] = *command; return true;
}
uint64_t qa_q3_client_peer_usercmd_number(const qa_q3_client_peer *p) { return p->command_number; }
const qa_q3_usercmd *qa_q3_client_peer_usercmd_at(const qa_q3_client_peer *p, uint64_t number) {
    if (number > p->command_number || p->command_number - number >= 64) return NULL;
    return &p->commands[number & 63];
}
const qa_q3_usercmd *qa_q3_client_peer_usercmd_signed_at(const qa_q3_client_peer *p, int32_t number) {
    if (!p) return NULL;
    uint32_t head_word = (uint32_t)p->command_number, floor_word = head_word - 64;
    int32_t head, floor;
    memcpy(&head, &head_word, sizeof(head)); memcpy(&floor, &floor_word, sizeof(floor));
    if (number > head || number <= floor) return NULL;
    return &p->commands[(uint32_t)number & 63];
}
int32_t qa_q3_client_peer_server_command_sequence(const qa_q3_client_peer *p) { return p->server_command_sequence; }
static bool consume(void *user, const qa_q3_server_event *event, qa_error *e) {
    qa_q3_client_peer *p = user;
    if (!current(p, e)) return false;
    switch (event->kind) {
    case QA_Q3_EVENT_ACK:
        p->reliable.acknowledged = event->value.acknowledge;
        return true;
    case QA_Q3_EVENT_GAMESTATE_START:
        if (!p->hooks.clear_active(p->hooks.context, e) || !current(p, e)) return false;
        for (unsigned i = 0; i < QA_Q3_PACKET_BACKUP; ++i) qa_q3_slot_clear(&p->history[i], p->product);
        p->has_snapshot = false; p->latest_snapshot = 0; p->parse_entities_number = 0;
        p->command_number = 0; memset(p->commands, 0, sizeof(p->commands)); memset(p->packets, 0, sizeof(p->packets));
        qa_q3_big_configstring_clear(&p->big_configstring);
        return true;
    case QA_Q3_EVENT_GAMESTATE:
        p->server_command_sequence = event->value.gamestate->command_sequence;
        if (!system_info(p, e)) return false;
        return p->hooks.gamestate(p->hooks.context, event->value.gamestate, e) && current(p, e);
    case QA_Q3_EVENT_COMMAND:
        if(!q3_client_command_admit(p,event->value.command,e)) return false;
        p->server_command_sequence = event->value.command->sequence;
        return true;
    case QA_Q3_EVENT_DOWNLOAD:
        return p->hooks.download(p->hooks.context, event->value.download, e) && current(p, e);
    case QA_Q3_EVENT_SNAPSHOT: {
        const qa_q3_snapshot *snapshot = event->value.snapshot.value;
        if (snapshot->delta_number <= 0) p->demo_waiting = false;
        if (event->value.snapshot.validity != QA_Q3_SNAPSHOT_VALID) return true;
        int64_t skipped = p->has_snapshot ? (int64_t)p->latest_snapshot + 1 : 1;
        if ((int64_t)snapshot->message_number - skipped >= QA_Q3_PACKET_BACKUP) skipped = (int64_t)snapshot->message_number - QA_Q3_PACKET_BACKUP + 1;
        while (skipped < snapshot->message_number) p->history[(uint32_t)skipped++ & 31].value.valid = false;
        qa_q3_snapshot_slot *slot = &p->history[(uint32_t)snapshot->message_number & 31];
        if (!qa_q3_slot_store(slot, snapshot, e)) return false;
        p->has_snapshot = true; p->latest_snapshot = snapshot->message_number;
        int32_t ping = 999;
        for (unsigned i = 0; i < QA_Q3_PACKET_BACKUP; ++i) {
            const sent_packet *packet = &p->packets[((p->channel ? qa_q3_channel_outgoing(p->channel) : 1U) - 1 - i) & 31];
            if (snapshot->player.commandTime >= packet->server_time) {
                int64_t elapsed = (int64_t)p->receive_time - packet->real_time;
                ping = elapsed > INT32_MAX ? INT32_MAX : elapsed < INT32_MIN ? INT32_MIN : (int32_t)elapsed;
                break;
            }
        }
        slot->value.player.ping = ping;
        return p->hooks.snapshot(p->hooks.context, &slot->value, ping, e) && current(p, e);
    }
    }
    return fail(e, QA_ERROR_FORMAT, "Unknown Q3 client event");
}
static bool download_size(void *user, int32_t wire_size, int32_t *effective, qa_error *error) {
    qa_q3_client_peer *p = user;
    return p->hooks.download_size(p->hooks.context, wire_size, effective, error) && current(p, error);
}
static bool receive_continue(qa_q3_client_peer *p, bool source_callbacks, qa_error *e) {
    if (p->receive_running) return fail(e, QA_ERROR_ARGUMENT, "Recursive Q3 server packet continuation");
    p->receive_running = true;
    qa_q3_server_decode decoder = {
        .product = p->product, .message_number = p->server_message_sequence, .reliable_sequence = p->reliable.sequence,
        .server_command_sequence = p->server_command_sequence, .parse_entities_number = p->parse_entities_number,
        .gamestate = &p->gamestate, .history = history, .history_context = p,
        .entity_scratch = p->scratch, .entity_scratch_capacity = QA_Q3_ENTITY_NONE,
        .download_size = download_size, .download_context = p
    };
    bool pending;
    bool ok = qa_q3_server_cursor_continue(&p->receive_cursor, &decoder, consume, p, source_callbacks, &pending, e);
    p->parse_entities_number = decoder.parse_entities_number;
    p->receive_running = false;
    p->receive_cursor.reader.raw.error = NULL;
    if (ok && !pending) {
        p->receive_size = 0;
        memset(&p->receive_cursor, 0, sizeof(p->receive_cursor));
    }
    return ok;
}
bool qa_q3_client_peer_receive_pending(const qa_q3_client_peer *p) {
    return p && p->receive_size &&
        (p->receive_cursor.phase == QA_Q3_SERVER_CURSOR_GAMESTATE ||
         p->receive_cursor.phase == QA_Q3_SERVER_CURSOR_DOWNLOAD);
}
bool qa_q3_client_peer_continue(qa_q3_client_peer *p, qa_error *e) {
    if (!p || p->disconnected || p->disconnect_started || !qa_q3_client_peer_receive_pending(p))
        return fail(e, QA_ERROR_ARGUMENT, "Q3 client has no live held source boundary");
    return current(p, e) && receive_continue(p, true, e);
}
bool qa_q3_client_peer_message(qa_q3_client_peer *p, int32_t sequence, qa_bytes plaintext, int32_t now, qa_error *e) {
    if (!p || p->disconnected || sequence < 0 || !plaintext.data || !plaintext.size ||
        plaintext.size > sizeof(p->receive_packet) || p->receive_size || p->receive_running)
        return fail(e, QA_ERROR_ARGUMENT, "Invalid or overlapping Q3 client message");
    if (!current(p, e)) return false;
    if (!p->demo && (sequence < 1 || sequence <= p->server_message_sequence))
        return fail(e, QA_ERROR_FORMAT, "Q3 server message sequence must advance");
    memcpy(p->receive_packet, plaintext.data, plaintext.size);
    p->receive_size = (uint16_t)plaintext.size;
    if (!qa_q3_server_cursor_init(&p->receive_cursor,
        (qa_bytes){p->receive_packet, p->receive_size}, e)) return false;
    p->server_message_sequence = sequence; p->receive_time = now;
    bool okay = receive_continue(p, !p->hooks.defer_source, e);
    if (okay && p->hooks.accepted_message) {
        p->receive_running = true;
        okay = p->hooks.accepted_message(p->hooks.context, sequence, plaintext,
            !p->demo_waiting, e) && current(p, e);
        p->receive_running = false;
    }
    return okay;
}
bool qa_q3_client_peer_demo_sequence(qa_q3_client_peer *p, int32_t sequence, qa_error *e) {
    if (!p || !p->demo || p->disconnected || p->disconnect_started ||
        p->receive_size || p->receive_running)
        return fail(e, QA_ERROR_ARGUMENT, "Demo sequence requires its idle actual Q3 receiver");
    if (!current(p, e)) return false;
    p->server_message_sequence = sequence;
    return true;
}
bool qa_q3_client_peer_record_seed(qa_q3_client_peer *p, qa_q3_writer *writer,
    int32_t *sequence, qa_error *e) {
    if (!p || !writer || !sequence || p->demo || p->disconnected || p->disconnect_started ||
        p->receive_size || p->receive_running || p->server_message_sequence < 1 ||
        p->gamestate.string_bytes <= 1)
        return fail(e, QA_ERROR_ARGUMENT, "Recording seed requires its idle received Q3 gamestate");
    if (!current(p, e)) return false;
    qa_q3_gamestate *state = malloc(sizeof(*state));
    if (!state) return fail(e, QA_ERROR_MEMORY, "Retaining actual Q3 recording signon");
    *state = p->gamestate;
    state->command_sequence = p->server_command_sequence;
    bool okay = qa_q3_server_begin(writer, p->reliable.sequence) &&
        qa_q3_server_gamestate(writer, state) && qa_q3_server_end(writer);
    free(state);
    if (okay && current(p, e)) {
        p->demo_waiting = true;
        *sequence = p->server_message_sequence - 1;
        return true;
    }
    return false;
}
bool qa_q3_client_peer_receive(qa_q3_client_peer *p, qa_bytes datagram, int32_t now, qa_q3_receive_kind *kind, qa_error *e) {
    if (!p || !kind || p->disconnected || p->demo || p->receive_size || p->receive_running)
        return fail(e, QA_ERROR_ARGUMENT, "Invalid or overlapping Q3 client datagram");
    if (!current(p, e)) return false;
    qa_q3_packet packet;
    if (!qa_q3_channel_receive(p->channel, datagram, &packet, e)) return false;
    *kind = packet.kind;
    if (packet.kind != QA_Q3_PACKET_MESSAGE) return true;
    qa_q3_reader reader; qa_q3_reader_init(&reader, packet.payload, false, e);
    int32_t acknowledge = (int32_t)qa_q3_read_bits(&reader, 32);
    if (reader.raw.failed) return false;
    uint8_t plaintext[QA_Q3_MESSAGE_BYTES]; memcpy(plaintext, packet.payload.data, packet.payload.size);
    qa_q3_xor_server(plaintext, packet.payload.size, p->challenge, packet.sequence, qa_q3_reliable_lookup(&p->reliable, acknowledge));
    return qa_q3_client_peer_message(p, (int32_t)packet.sequence, (qa_bytes){plaintext, packet.payload.size}, now, e);
}
void qa_q3_big_configstring_clear(qa_q3_big_configstring *state)
{ state->length=0;state->text[0]=0; }

bool qa_q3_big_configstring_feed(qa_q3_big_configstring *state,
    const qa_q3_tokens *tokens,bool *complete,qa_error *error)
{
    char *buffer=state->text;size_t capacity=sizeof(state->text),*length=&state->length;
    const char *name = qa_q3_token(tokens, 0);
    bool start = !strcmp(name, "bcs0"), last = !strcmp(name, "bcs2");
    *complete = true;
    if (!start && !last && strcmp(name, "bcs1")) return true;
    *complete = false;
    const char *fragment = qa_q3_token(tokens, 2);
    if (start) {
        int used = snprintf(buffer, capacity, "cs %s \"%s", qa_q3_token(tokens, 1), fragment);
        if (used < 0 || (size_t)used >= capacity) {
            *length = 0;
            buffer[0] = 0;
            return fail(error, QA_ERROR_FORMAT, "Q3 big configstring overflow");
        }
        *length = (size_t)used;
        return true;
    }
    size_t added = strlen(fragment), quote = last ? 1u : 0u;
    if (!*length || *length >= capacity || added + quote >= capacity - *length)
        return fail(error, QA_ERROR_FORMAT, "Q3 big configstring overflow or missing start");
    memcpy(buffer + *length, fragment, added + 1);
    *length += added;
    if (last) {
        buffer[(*length)++] = '"';
        buffer[*length] = 0;
        *complete = true;
    }
    return true;
}

bool qa_q3_server_command_apply(qa_q3_reliable *history,qa_q3_gamestate *gamestate,
    qa_q3_big_configstring *continued,int32_t sequence,const char *text,qa_q3_tokens *tokens,
    qa_q3_server_command_result *result,qa_error *error)
{
    *result=(qa_q3_server_command_result){.kind=QA_Q3_SERVER_FRAGMENT,.config_index=UINT32_MAX,.text=text};
    if(history) {
        if(sequence>history->sequence) return fail(error,QA_ERROR_ARGUMENT,"Q3 server command is in the future");
        if(sequence<=0) return true;
        if((int64_t)history->sequence-sequence>=QA_Q3_RELIABLE)
            return fail(error,QA_ERROR_FORMAT,"Q3 server command has left its retained history");
    }
    if(!qa_q3_tokenize(text,tokens,error)) return false;
    const char *name=qa_q3_token(tokens,0);
    if(!strcmp(name,"disconnect")) {
        result->kind=QA_Q3_SERVER_DISCONNECT;
        result->effect_text=tokens->count>=2?qa_q3_token(tokens,1):"Server disconnected";
        return true;
    }
    bool complete;
    if(!qa_q3_big_configstring_feed(continued,tokens,&complete,error)) return false;
    if(!complete) return true;
    if(!strcmp(name,"bcs2")) {
        result->text=continued->text;
        if(!qa_q3_tokenize(continued->text,tokens,error)) return false;
        name=qa_q3_token(tokens,0);
    }
    result->kind=QA_Q3_SERVER_COMMAND;result->effect_text=result->text;
    if(!strcmp(name,"cs")) {
        int32_t index=qa_q3_parse_integer(qa_q3_token(tokens,1));
        if(!gamestate || index<0 || index>=QA_Q3_CONFIGSTRINGS)
            return fail(error,QA_ERROR_FORMAT,"Q3 server command has invalid configstring state or index");
        const char *words[1024];
        size_t count=tokens->count>2?tokens->count-2:0;
        for(size_t i=0;i<count;++i) words[i]=qa_q3_token(tokens,i+2);
        char value[QA_Q3_BIG_INFO_CHARS];
        if(!qa_text_join_words(words,count,value,sizeof(value),error)) return false;
        const char *old=qa_q3_configstring(gamestate,(unsigned)index);
        bool changed=!old || strcmp(old,value)!=0;
        if(changed && !qa_q3_configstring_set(gamestate,(unsigned)index,value,error)) return false;
        result->config_index=(uint32_t)index;
        result->effect_text=qa_q3_configstring(gamestate,(unsigned)index);
        result->kind=changed && index==1?QA_Q3_SERVER_SYSTEM_INFO:QA_Q3_SERVER_CONFIGSTRING;
    } else if(!strcmp(name,"map_restart")) result->kind=QA_Q3_SERVER_MAP_RESTART;
    else if(!strcmp(name,"clientLevelShot")) result->kind=QA_Q3_SERVER_LEVEL_SHOT;
    return true;
}

bool qa_q3_server_command_ack(qa_q3_reliable *history,qa_q3_gamestate *gamestate,
    int32_t sequence,int32_t *consumed,qa_error *error)
{
    if(gamestate && sequence>gamestate->command_sequence) gamestate->command_sequence=sequence;
    if(history) {
        bool clamped;
        int32_t acknowledged=sequence>history->acknowledged?sequence:history->acknowledged;
        if(!qa_q3_reliable_ack(history,QA_Q3_SERVER,acknowledged,&clamped,error)) return false;
    }
    if(sequence>*consumed) *consumed=sequence;
    return true;
}

bool qa_q3_client_command_state_update(qa_q3_server_command_kind kind,const char *text,
    int32_t *server_id,qa_q3_usercmd *commands,size_t count,qa_error *error)
{
    if(kind==QA_Q3_SERVER_SYSTEM_INFO) {
        char value[64];
        if(!qa_q3_info_value(text,"sv_serverid",value,sizeof(value),error)) return false;
        *server_id=qa_q3_parse_integer(value);
    } else if(kind==QA_Q3_SERVER_MAP_RESTART) memset(commands,0,count*sizeof(*commands));
    return true;
}

static bool execute(qa_q3_client_peer *p,int32_t sequence,const char *text,qa_error *e)
{
    qa_q3_tokens tokens;qa_q3_server_command_result result;
    if(!qa_q3_server_command_apply(NULL,&p->gamestate,&p->big_configstring,sequence,text,&tokens,&result,e)) return false;
    if(result.kind==QA_Q3_SERVER_FRAGMENT) return true;
    if(result.kind==QA_Q3_SERVER_DISCONNECT) {
        p->disconnected=true;
        return p->hooks.disconnect(p->hooks.context,result.effect_text,e);
    }
    if(result.kind==QA_Q3_SERVER_SYSTEM_INFO && !system_info(p,e)) return false;
    if(result.kind==QA_Q3_SERVER_MAP_RESTART &&
        (!qa_q3_client_command_state_update(result.kind,result.effect_text,&p->server_id,p->commands,64,e) ||
        !p->hooks.map_restart(p->hooks.context,e) || !current(p,e))) return false;
    if(result.kind==QA_Q3_SERVER_LEVEL_SHOT) {
        if(!p->hooks.local_server_running(p->hooks.context)) return true;
        if(!p->hooks.level_shot(p->hooks.context,e) || !current(p,e)) return false;
    }
    return p->hooks.command(p->hooks.context,sequence,&tokens,e) && current(p,e) &&
        qa_q3_server_command_ack(NULL,&p->gamestate,sequence,&p->last_executed_server_command,e);
}
bool qa_q3_client_peer_execute(qa_q3_client_peer *p, int32_t sequence, bool demo, qa_error *e) {
    if (!p) return fail(e, QA_ERROR_ARGUMENT, "Missing Q3 client peer");
    if (!current(p, e)) return false;
    if ((int64_t)sequence <= (int64_t)p->server_command_sequence - 64) {
        if (demo || p->demo) return true;
        return fail(e, QA_ERROR_FORMAT, "Q3 server command was cycled out");
    }
    if (sequence > p->server_command_sequence) return fail(e, QA_ERROR_FORMAT, "Requested Q3 server command has not arrived");
    p->last_executed_server_command = sequence;
    qa_event_lease *lease=p->server_commands[(uint32_t)sequence & (QA_Q3_RELIABLE-1)];
    uint64_t id=lease ? qa_event_lease_id(lease) : 0;
    const qa_q3_command *record=id ? qa_event_ring_at(p->command_events,id) : NULL;
    if(!record && lease) record=qa_event_lease_record(lease);
    bool okay=execute(p,sequence,record ? record->text : "",e);
    if(okay && id>=p->command_cursor) {
        p->command_cursor=id+1;
        qa_event_ring_retire(p->command_events,p->command_cursor);
    }
    return okay;
}
static bool transmit_pending(qa_q3_client_peer *p, int32_t real_time, qa_error *e)
{
    while (p->transmit_size || qa_q3_channel_pending(p->channel)) {
        if (!p->transmit_size) {
            bool present; qa_bytes packet;
            if (!qa_q3_channel_prepare(p->channel, &present, &packet, e)) return false;
            if (!present || packet.size > sizeof(p->transmit_packet))
                return fail(e, QA_ERROR_FORMAT, "Q3 channel produced no bounded pending client datagram");
            memcpy(p->transmit_packet, packet.data, packet.size); p->transmit_size = (uint16_t)packet.size;
        }
        qa_net_send_result result = p->hooks.send(p->hooks.context, &p->remote,
            (qa_bytes){p->transmit_packet, p->transmit_size}, e);
        if (result != QA_NET_SEND_ACCEPTED) return result == QA_NET_SEND_FULL;
        qa_bytes packet = {p->transmit_packet, p->transmit_size};
        uint32_t sequence = qa_q3_channel_outgoing(p->channel);
        qa_q3_channel_sent(p->channel, packet);
        p->packets[sequence & 31].real_time = real_time;
        p->last_packet_sent_time = real_time;
        p->transmit_size = 0;
        if (!current(p, e)) return false;
    }
    return true;
}
bool qa_q3_client_peer_send(qa_q3_client_peer *p, const qa_q3_client_send *options, qa_error *e) {
    if (!p || !options || p->disconnected || p->demo) return fail(e, QA_ERROR_ARGUMENT, "Invalid Q3 client send");
    if (!current(p, e)) return false;
    if (p->transmit_size || qa_q3_channel_pending(p->channel)) return transmit_pending(p, options->real_time, e);
    qa_q3_client_message message = {0};
    message.header = (qa_q3_client_header){p->server_id, p->server_message_sequence, p->server_command_sequence};
    int64_t pending = (int64_t)p->reliable.sequence - p->reliable.acknowledged;
    if (pending > QA_Q3_RELIABLE + 1) return fail(e, QA_ERROR_FORMAT, "Invalid Q3 client reliable range");
    for (int64_t n = (int64_t)p->reliable.acknowledged + 1; n <= p->reliable.sequence; ++n) {
        qa_q3_command *command = &message.commands[message.command_count++];
        command->sequence = (int32_t)n; snprintf(command->text,sizeof(command->text),"%s",qa_q3_reliable_lookup(&p->reliable,(int32_t)n));
    }
    uint32_t sequence = qa_q3_channel_outgoing(p->channel);
    unsigned dup = options->packet_dup > 5 ? 5 : options->packet_dup;
    const sent_packet *old = &p->packets[(sequence - 1 - dup) & 31];
    uint64_t count = p->command_number >= old->command_number ? p->command_number - old->command_number : 0;
    if (count > QA_Q3_USERCMDS) count = QA_Q3_USERCMDS;
    message.usercmd_count = (size_t)count; message.movement = count != 0;
    const qa_q3_snapshot *latest = qa_q3_client_peer_snapshot(p);
    message.no_delta = options->no_delta || !latest || p->demo_waiting || latest->message_number != p->server_message_sequence;
    for (size_t i = 0; i < message.usercmd_count; ++i) message.usercmds[i] = p->commands[(p->command_number - count + i + 1) & 63];
    uint8_t data[QA_Q3_MESSAGE_BYTES]; qa_q3_writer writer;
    qa_q3_writer_init(&writer, data, sizeof(data), false, e);
    if (!qa_q3_encode_client(&writer, &message, p->gamestate.checksum_feed, lookup(p, p->server_command_sequence))) return false;
    size_t size = qa_q3_writer_size(&writer);
    if (!qa_q3_xor_client(data, size, p->challenge, lookup, p, e)) return false;
    p->packets[sequence & 31] = (sent_packet){p->command_number,
        count ? message.usercmds[count - 1].serverTime : 0, options->real_time};
    if (!qa_q3_channel_begin(p->channel, (qa_bytes){data, size}, e)) return false;
    return transmit_pending(p, options->real_time, e);
}
bool qa_q3_client_peer_ready(const qa_q3_client_peer *p, const qa_q3_client_readiness *o) {
    if (!p || !o || p->disconnected || p->disconnect_started || p->demo || o->cinematic) return false;
    int64_t elapsed = (int64_t)o->real_time - p->last_packet_sent_time;
    if (o->downloading && elapsed < 50) return false;
    if (!o->active && !o->primed && !o->downloading && elapsed < 1000) return false;
    if (o->local || o->lan) return true;
    unsigned maximum = o->maximum_packets < 15 ? 15 : o->maximum_packets > 125 ? 125 : o->maximum_packets;
    const sent_packet *previous = &p->packets[(qa_q3_channel_outgoing(p->channel) - 1) & 31];
    return (int64_t)o->real_time - previous->real_time >= 1000 / maximum;
}
bool qa_q3_client_peer_disconnect(qa_q3_client_peer *p, const qa_q3_client_send *o, qa_error *e) {
    if (!p || !o) return fail(e, QA_ERROR_ARGUMENT, "Invalid Q3 disconnect source owner");
    if (p->demo || p->disconnected) return true;
    if (!p->disconnect_started) {
        /* Complete any older interrupted message before inserting disconnect;
         * its eventual delivery is not one of the three close packets. */
        if ((p->transmit_size || qa_q3_channel_pending(p->channel)) && !transmit_pending(p, o->real_time, e)) return false;
        if (p->transmit_size || qa_q3_channel_pending(p->channel)) return true;
        if (!qa_q3_client_peer_command(p, "disconnect", e)) return false;
        p->disconnect_started = true;
    }
    while (p->disconnect_packets < 3) {
        if (!qa_q3_client_peer_send(p, o, e)) return false;
        if (p->transmit_size || qa_q3_channel_pending(p->channel)) return true;
        ++p->disconnect_packets;
    }
    p->disconnected = true; return true;
}
