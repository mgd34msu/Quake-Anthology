/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include "qa/json.h"

#include <locale.h>
#include <stdlib.h>
#include <string.h>

typedef struct json_node {
    size_t start, end, count;
    qa_json_id first, next;
    qa_json_kind kind;
} json_node;

struct qa_json_document {
    qa_bytes source;
    json_node *nodes;
    size_t count, capacity;
    locale_t numeric_locale;
};

typedef struct json_frame {
    qa_json_id container, last;
    bool after_value, required;
} json_frame;

typedef struct json_parser {
    qa_json_document *document;
    size_t cursor;
    qa_error *error;
} json_parser;

static bool fail(json_parser *parser, const char *message) {
    qa_error_set(parser->error, QA_ERROR_FORMAT, parser->cursor, "%s", message);
    return false;
}

static void whitespace(json_parser *parser) {
    qa_bytes source = parser->document->source;
    while (parser->cursor < source.size) {
        uint8_t c = source.data[parser->cursor];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        ++parser->cursor;
    }
}

/* Advance one valid Unicode scalar. This also rejects overlong encodings,
 * surrogate code points, and values outside Unicode. */
static bool utf8(qa_bytes source, size_t *cursor) {
    if (*cursor >= source.size) return false;
    uint8_t first = source.data[*cursor];
    size_t count;
    uint32_t value, minimum;
    if (first < 0x80) { ++*cursor; return true; }
    if (first >= 0xc2 && first <= 0xdf) { count=2; value=first&0x1fu; minimum=0x80; }
    else if (first >= 0xe0 && first <= 0xef) { count=3; value=first&0x0fu; minimum=0x800; }
    else if (first >= 0xf0 && first <= 0xf4) { count=4; value=first&0x07u; minimum=0x10000; }
    else return false;
    if (count > source.size - *cursor) return false;
    for (size_t i=1; i<count; ++i) {
        uint8_t c = source.data[*cursor+i];
        if ((c & 0xc0u) != 0x80u) return false;
        value = (value<<6) | (c&0x3fu);
    }
    if (value < minimum || value > 0x10ffff || (value>=0xd800 && value<=0xdfff)) return false;
    *cursor += count;
    return true;
}

static bool hex4(qa_bytes source, size_t *cursor, uint32_t *out) {
    if (source.size - *cursor < 4) return false;
    uint32_t value = 0;
    for (size_t i=0; i<4; ++i) {
        uint8_t c=source.data[*cursor+i];
        uint32_t digit;
        if (c>='0' && c<='9') digit=(uint32_t)(c-'0');
        else if (c>='a' && c<='f') digit=(uint32_t)(c-'a')+10u;
        else if (c>='A' && c<='F') digit=(uint32_t)(c-'A')+10u;
        else return false;
        value=(value<<4)|digit;
    }
    *cursor+=4;
    *out=value;
    return true;
}

static bool escape(qa_bytes source, size_t *cursor, uint32_t *out) {
    if (*cursor >= source.size) return false;
    uint8_t c=source.data[(*cursor)++];
    switch (c) {
    case '"': case '\\': case '/': *out=c; return true;
    case 'b': *out='\b'; return true;
    case 'f': *out='\f'; return true;
    case 'n': *out='\n'; return true;
    case 'r': *out='\r'; return true;
    case 't': *out='\t'; return true;
    case 'u': break;
    default: return false;
    }
    uint32_t value;
    if (!hex4(source,cursor,&value)) return false;
    if (value>=0xdc00 && value<=0xdfff) return false;
    if (value>=0xd800 && value<=0xdbff) {
        if (source.size-*cursor<6 || source.data[*cursor]!='\\' || source.data[*cursor+1]!='u') return false;
        *cursor+=2;
        uint32_t low;
        if (!hex4(source,cursor,&low) || low<0xdc00 || low>0xdfff) return false;
        value=0x10000u+((value-0xd800u)<<10)+(low-0xdc00u);
    }
    *out=value;
    return true;
}

static size_t encode_utf8(uint32_t value, uint8_t output[4]) {
    if (value<0x80) { output[0]=(uint8_t)value; return 1; }
    if (value<0x800) {
        output[0]=(uint8_t)(0xc0u|(value>>6)); output[1]=(uint8_t)(0x80u|(value&0x3fu)); return 2;
    }
    if (value<0x10000) {
        output[0]=(uint8_t)(0xe0u|(value>>12)); output[1]=(uint8_t)(0x80u|((value>>6)&0x3fu));
        output[2]=(uint8_t)(0x80u|(value&0x3fu)); return 3;
    }
    output[0]=(uint8_t)(0xf0u|(value>>18)); output[1]=(uint8_t)(0x80u|((value>>12)&0x3fu));
    output[2]=(uint8_t)(0x80u|((value>>6)&0x3fu)); output[3]=(uint8_t)(0x80u|(value&0x3fu)); return 4;
}

static bool string_end(json_parser *parser) {
    qa_bytes source=parser->document->source;
    ++parser->cursor;
    while (parser->cursor<source.size) {
        uint8_t c=source.data[parser->cursor];
        if (c=='"') { ++parser->cursor; return true; }
        if (c<0x20) return fail(parser,"unescaped control byte in JSON string");
        if (c=='\\') {
            uint32_t value;
            ++parser->cursor;
            if (!escape(source,&parser->cursor,&value)) return fail(parser,"invalid JSON string escape");
        } else if (!utf8(source,&parser->cursor)) return fail(parser,"invalid UTF-8 in JSON string");
    }
    return fail(parser,"unterminated JSON string");
}

static bool add_node(json_parser *parser, json_node node, qa_json_id *out) {
    qa_json_document *document=parser->document;
    if (document->count == UINT32_MAX) {
        qa_error_set(parser->error,QA_ERROR_MEMORY,parser->cursor,"JSON index exceeds its address space");
        return false;
    }
    if (document->count==document->capacity) {
        size_t capacity=document->capacity ? document->capacity*2 : 64;
        if (capacity<document->capacity || capacity>UINT32_MAX) capacity=UINT32_MAX;
        if (capacity>SIZE_MAX/sizeof(*document->nodes)) {
            qa_error_set(parser->error,QA_ERROR_MEMORY,parser->cursor,"JSON index size overflow"); return false;
        }
        json_node *nodes=realloc(document->nodes,capacity*sizeof(*nodes));
        if (!nodes) { qa_error_set(parser->error,QA_ERROR_MEMORY,parser->cursor,"allocating JSON index"); return false; }
        document->nodes=nodes; document->capacity=capacity;
    }
    *out=(qa_json_id)document->count;
    document->nodes[document->count++]=node;
    return true;
}

static bool digit(uint8_t c) { return c>='0' && c<='9'; }

static bool value(json_parser *parser, qa_json_id *out) {
    whitespace(parser);
    qa_bytes source=parser->document->source;
    if (parser->cursor==source.size) return fail(parser,"missing JSON value");
    json_node node={.start=parser->cursor,.first=QA_JSON_NONE,.next=QA_JSON_NONE};
    uint8_t c=source.data[parser->cursor];
    if (c=='{' || c=='[') {
        node.kind=c=='{' ? QA_JSON_OBJECT : QA_JSON_ARRAY;
        ++parser->cursor;
    } else if (c=='"') {
        node.kind=QA_JSON_STRING;
        if (!string_end(parser)) return false;
    } else if (c=='n' || c=='t' || c=='f') {
        const char *literal=c=='n' ? "null" : c=='t' ? "true" : "false";
        size_t length=c=='f' ? 5u : 4u;
        if (source.size-parser->cursor<length || memcmp(source.data+parser->cursor,literal,length))
            return fail(parser,"invalid JSON literal");
        parser->cursor+=length;
        node.kind=c=='n' ? QA_JSON_NULL : QA_JSON_BOOL;
    } else {
        node.kind=QA_JSON_NUMBER;
        if (c=='-') ++parser->cursor;
        if (parser->cursor==source.size || !digit(source.data[parser->cursor])) return fail(parser,"invalid JSON number");
        if (source.data[parser->cursor]=='0') ++parser->cursor;
        else while (parser->cursor<source.size && digit(source.data[parser->cursor])) ++parser->cursor;
        if (parser->cursor<source.size && source.data[parser->cursor]=='.') {
            ++parser->cursor;
            if (parser->cursor==source.size || !digit(source.data[parser->cursor])) return fail(parser,"missing JSON fraction digits");
            while (parser->cursor<source.size && digit(source.data[parser->cursor])) ++parser->cursor;
        }
        if (parser->cursor<source.size && (source.data[parser->cursor]=='e' || source.data[parser->cursor]=='E')) {
            ++parser->cursor;
            if (parser->cursor<source.size && (source.data[parser->cursor]=='+' || source.data[parser->cursor]=='-')) ++parser->cursor;
            if (parser->cursor==source.size || !digit(source.data[parser->cursor])) return fail(parser,"missing JSON exponent digits");
            while (parser->cursor<source.size && digit(source.data[parser->cursor])) ++parser->cursor;
        }
    }
    node.end=parser->cursor;
    return add_node(parser,node,out);
}

static bool push_frame(json_parser *parser, json_frame **frames, size_t *count,
                       size_t *capacity, qa_json_id id) {
    if (*count==*capacity) {
        size_t next=*capacity ? *capacity*2 : 16;
        if (next<*capacity || next>SIZE_MAX/sizeof(**frames)) {
            qa_error_set(parser->error,QA_ERROR_MEMORY,parser->cursor,"JSON nesting size overflow"); return false;
        }
        json_frame *storage=realloc(*frames,next*sizeof(*storage));
        if (!storage) { qa_error_set(parser->error,QA_ERROR_MEMORY,parser->cursor,"allocating JSON nesting stack"); return false; }
        *frames=storage; *capacity=next;
    }
    (*frames)[(*count)++]=(json_frame){.container=id,.last=QA_JSON_NONE};
    return true;
}

bool qa_json_parse(qa_bytes source, qa_json_document **out, qa_error *error) {
    if (!out || (!source.data && source.size)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"invalid JSON input"); return false;
    }
    qa_json_document *document=calloc(1,sizeof(*document));
    if (!document) { qa_error_set(error,QA_ERROR_MEMORY,0,"allocating JSON document"); return false; }
    document->source=source;
    document->numeric_locale=newlocale(LC_NUMERIC_MASK,"C",(locale_t)0);
    if (!document->numeric_locale) {
        qa_error_set(error,QA_ERROR_MEMORY,0,"creating JSON numeric locale"); free(document); return false;
    }
    json_parser parser={document,0,error};
    json_frame *frames=NULL;
    size_t count=0, capacity=0;
    qa_json_id root;
    if (!value(&parser,&root)) goto failure;
    if (document->nodes[root].kind>=QA_JSON_ARRAY && !push_frame(&parser,&frames,&count,&capacity,root)) goto failure;
    while (count) {
        json_frame *frame=&frames[count-1];
        qa_json_id parent=frame->container;
        bool object=document->nodes[parent].kind==QA_JSON_OBJECT;
        uint8_t closing=object ? '}' : ']';
        whitespace(&parser);
        if (parser.cursor==source.size) { fail(&parser,"unterminated JSON container"); goto failure; }
        if (source.data[parser.cursor]==closing) {
            if (frame->required) { fail(&parser,"trailing comma in JSON container"); goto failure; }
            document->nodes[parent].end=++parser.cursor;
            --count; continue;
        }
        if (frame->after_value) {
            if (source.data[parser.cursor]!=',') { fail(&parser,"missing JSON separator"); goto failure; }
            ++parser.cursor; frame->after_value=false; frame->required=true; continue;
        }
        qa_json_id item, child;
        if (object) {
            if (source.data[parser.cursor]!='"') { fail(&parser,"JSON object key is not a string"); goto failure; }
            if (!value(&parser,&item)) goto failure;
            whitespace(&parser);
            if (parser.cursor==source.size || source.data[parser.cursor]!=':') { fail(&parser,"missing JSON object colon"); goto failure; }
            ++parser.cursor;
            if (!value(&parser,&child)) goto failure;
            document->nodes[item].first=child;
        } else {
            if (!value(&parser,&child)) goto failure;
            item=child;
        }
        if (frame->last==QA_JSON_NONE) document->nodes[parent].first=item;
        else document->nodes[frame->last].next=item;
        frame->last=item;
        frame->after_value=true; frame->required=false;
        ++document->nodes[parent].count;
        if (document->nodes[child].kind>=QA_JSON_ARRAY && !push_frame(&parser,&frames,&count,&capacity,child)) goto failure;
    }
    whitespace(&parser);
    if (parser.cursor!=source.size) { fail(&parser,"trailing bytes after JSON value"); goto failure; }
    free(frames); *out=document; return true;
failure:
    free(frames); qa_json_destroy(document); return false;
}

void qa_json_destroy(qa_json_document *document) {
    if (!document) return;
    freelocale(document->numeric_locale);
    free(document->nodes); free(document);
}

static const json_node *node_at(const qa_json_document *document, qa_json_id id) {
    return document && (size_t)id<document->count ? &document->nodes[id] : NULL;
}

qa_json_id qa_json_root(const qa_json_document *document) { return document && document->count ? 0 : QA_JSON_NONE; }
qa_json_kind qa_json_type(const qa_json_document *document, qa_json_id id) {
    const json_node *node=node_at(document,id); return node ? node->kind : QA_JSON_INVALID;
}
size_t qa_json_size(const qa_json_document *document, qa_json_id id) {
    const json_node *node=node_at(document,id); return node ? node->count : 0;
}

static qa_json_id member(const qa_json_document *document, qa_json_id id, size_t index) {
    const json_node *node=node_at(document,id);
    if (!node || index>=node->count) return QA_JSON_NONE;
    id=node->first;
    while (index--) id=document->nodes[id].next;
    return id;
}
qa_json_id qa_json_at(const qa_json_document *document, qa_json_id id, size_t index) {
    qa_json_kind kind=qa_json_type(document,id);
    if (kind!=QA_JSON_ARRAY && kind!=QA_JSON_OBJECT) return QA_JSON_NONE;
    qa_json_id child=member(document,id,index);
    return child!=QA_JSON_NONE && kind==QA_JSON_OBJECT ? document->nodes[child].first : child;
}
qa_json_id qa_json_key_at(const qa_json_document *document, qa_json_id id, size_t index) {
    return qa_json_type(document,id)==QA_JSON_OBJECT ? member(document,id,index) : QA_JSON_NONE;
}
qa_bytes qa_json_source(const qa_json_document *document, qa_json_id id) {
    const json_node *node=node_at(document,id);
    return node ? (qa_bytes){document->source.data+node->start,node->end-node->start} : (qa_bytes){0};
}

bool qa_json_string_equal(const qa_json_document *document, qa_json_id id, const char *text) {
    if (!text || qa_json_type(document,id)!=QA_JSON_STRING) return false;
    qa_bytes source=qa_json_source(document,id);
    size_t cursor=1, position=0, length=strlen(text);
    while (cursor<source.size-1) {
        if (source.data[cursor]=='\\') {
            ++cursor;
            uint32_t code;
            if (!escape(source,&cursor,&code)) return false;
            uint8_t bytes[4]; size_t count=encode_utf8(code,bytes);
            if (count>length-position || memcmp(text+position,bytes,count)) return false;
            position+=count;
        } else {
            if (position==length || (uint8_t)text[position++]!=source.data[cursor++]) return false;
        }
    }
    return position==length;
}
qa_json_id qa_json_get(const qa_json_document *document, qa_json_id id, const char *key) {
    if (qa_json_type(document,id)!=QA_JSON_OBJECT) return QA_JSON_NONE;
    qa_json_id result=QA_JSON_NONE;
    for (qa_json_id child=document->nodes[id].first;child!=QA_JSON_NONE;child=document->nodes[child].next)
        if (qa_json_string_equal(document,child,key)) result=document->nodes[child].first;
    return result;
}

static bool require_kind(const qa_json_document *document, qa_json_id id, qa_json_kind kind, const void *out, qa_error *error) {
    if (!out || qa_json_type(document,id)!=kind) {
        const json_node *node=node_at(document,id);
        qa_error_set(error,QA_ERROR_ARGUMENT,node ? node->start : 0,"JSON value has the wrong type or output is null"); return false;
    }
    return true;
}
bool qa_json_bool(const qa_json_document *document, qa_json_id id, bool *out, qa_error *error) {
    if (!require_kind(document,id,QA_JSON_BOOL,out,error)) return false;
    *out=document->source.data[document->nodes[id].start]=='t'; return true;
}
bool qa_json_number(const qa_json_document *document, qa_json_id id, double *out, qa_error *error) {
    if (!require_kind(document,id,QA_JSON_NUMBER,out,error)) return false;
    qa_bytes source=qa_json_source(document,id);
    char local[128], *text=local;
    if (source.size>=sizeof(local)) {
        if (source.size==SIZE_MAX || !(text=malloc(source.size+1))) {
            qa_error_set(error,QA_ERROR_MEMORY,document->nodes[id].start,"allocating JSON number text"); return false;
        }
    }
    memcpy(text,source.data,source.size); text[source.size]=0;
    double number=strtod_l(text,NULL,document->numeric_locale);
    if (text!=local) free(text);
    *out=number; return true;
}

static bool integer(const qa_json_document *document, qa_json_id id, bool signed_value,
                    uint64_t *out, bool *negative, qa_error *error) {
    if (!require_kind(document,id,QA_JSON_NUMBER,out,error)) return false;
    qa_bytes source=qa_json_source(document,id);
    *negative=source.data[0]=='-';
    if (*negative && !signed_value) goto bad;
    uint64_t limit=signed_value ? (uint64_t)INT64_MAX+(*negative ? 1u : 0u) : UINT64_MAX;
    uint64_t result=0;
    for (size_t i=*negative ? 1u : 0u;i<source.size;++i) {
        uint8_t c=source.data[i];
        if (!digit(c)) goto bad;
        uint64_t number=(uint64_t)(c-'0');
        if (result>(limit-number)/10u) goto bad;
        result=result*10u+number;
    }
    *out=result; return true;
bad:
    qa_error_set(error,QA_ERROR_FORMAT,document->nodes[id].start,"JSON number is not a representable integer literal"); return false;
}
bool qa_json_i64(const qa_json_document *document, qa_json_id id, int64_t *out, qa_error *error) {
    uint64_t value; bool negative;
    if (!require_kind(document,id,QA_JSON_NUMBER,out,error) || !integer(document,id,true,&value,&negative,error)) return false;
    *out=negative ? (value==(uint64_t)INT64_MAX+1u ? INT64_MIN : -(int64_t)value) : (int64_t)value;
    return true;
}
bool qa_json_u64(const qa_json_document *document, qa_json_id id, uint64_t *out, qa_error *error) {
    uint64_t value; bool negative;
    if (!require_kind(document,id,QA_JSON_NUMBER,out,error) || !integer(document,id,false,&value,&negative,error)) return false;
    *out=value; return true;
}
bool qa_json_string(const qa_json_document *document, qa_json_id id, qa_buffer *out, qa_error *error) {
    if (!require_kind(document,id,QA_JSON_STRING,out,error)) return false;
    qa_bytes source=qa_json_source(document,id);
    uint8_t *output=malloc(source.size-1);
    if (!output) { qa_error_set(error,QA_ERROR_MEMORY,document->nodes[id].start,"allocating JSON string"); return false; }
    size_t cursor=1, count=0;
    while (cursor<source.size-1) {
        if (source.data[cursor]=='\\') {
            ++cursor;
            uint32_t code;
            if (!escape(source,&cursor,&code)) { free(output); return false; }
            uint8_t bytes[4]; size_t length=encode_utf8(code,bytes);
            memcpy(output+count,bytes,length); count+=length;
        } else output[count++]=source.data[cursor++];
    }
    output[count]=0; *out=(qa_buffer){output,count}; return true;
}
bool qa_json_quote(qa_bytes source, qa_buffer *out, qa_error *error) {
    if (!out || (!source.data && source.size)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"invalid JSON string input"); return false;
    }
    size_t length=2, cursor=0;
    while (cursor<source.size) {
        size_t start=cursor;
        uint8_t c=source.data[cursor];
        if (!utf8(source,&cursor)) { qa_error_set(error,QA_ERROR_FORMAT,start,"invalid UTF-8 for JSON string"); return false; }
        size_t bytes=c<0x20 ? 6u : c=='"' || c=='\\' ? 2u : cursor-start;
        if (length>SIZE_MAX-1-bytes) { qa_error_set(error,QA_ERROR_MEMORY,start,"JSON quoted string size overflow"); return false; }
        length+=bytes;
    }
    uint8_t *output=malloc(length+1);
    if (!output) { qa_error_set(error,QA_ERROR_MEMORY,0,"allocating quoted JSON string"); return false; }
    static const char hex[]="0123456789abcdef";
    size_t count=0; output[count++]='"';
    for (size_t i=0;i<source.size;++i) {
        uint8_t c=source.data[i];
        if (c<0x20) {
            output[count++]='\\'; output[count++]='u'; output[count++]='0'; output[count++]='0';
            output[count++]=(uint8_t)hex[c>>4]; output[count++]=(uint8_t)hex[c&15u];
        } else {
            if (c=='"' || c=='\\') output[count++]='\\';
            output[count++]=c;
        }
    }
    output[count++]='"'; output[count]=0; *out=(qa_buffer){output,count}; return true;
}
