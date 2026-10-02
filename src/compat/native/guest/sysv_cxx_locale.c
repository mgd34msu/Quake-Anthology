#include "sysv_libc_private.h"

enum locale_operation { LO_SLOT = 1, LO_IS, LO_UPPER, LO_LOWER, LO_UPPER_RANGE,
    LO_LOWER_RANGE, LO_WIDEN, LO_WIDEN_RANGE, LO_NARROW };
static uint16_t character_mask(int value)
{
    bool upper = value >= 65 && value <= 90, lower = value >= 97 && value <= 122;
    bool digit = value >= 48 && value <= 57, print = value >= 32 && value < 127;
    bool graph = value > 32 && value < 127, blank = value == 32 || value == 9;
    bool space = value == 32 || (value >= 9 && value <= 13);
    return (upper ? 0x100 : 0) | (lower ? 0x200 : 0) | (upper || lower ? 0x400 : 0) |
        (digit ? 0x800 : 0) | (digit || (value >= 65 && value <= 70) || (value >= 97 && value <= 102) ? 0x1000 : 0) |
        (space ? 0x2000 : 0) | (print ? 0x4000 : 0) | (graph ? 0x8000 : 0) | (blank ? 1 : 0) |
        ((value >= 0 && value < 32) || value == 127 ? 2 : 0) | (graph && !upper && !lower && !digit ? 4 : 0) |
        (upper || lower || digit ? 8 : 0);
}
static bool facet_name(const char *pattern,bool wide,char **out,qa_error *error)
{
    if (!sysv_copy_text(pattern,out,error)) return false;
    for (char *c = *out; *c; ++c) if (*c == '?') *c = wide ? 'w' : 'c';
    return true;
}
static bool facet(guest_sysv_runtime *r,const char *name,size_t bytes,
    const uint64_t *methods,size_t count,uint64_t *out,qa_error *error)
{
    sysv_object *base = sysv_object_find(r,10,"NSt6locale5facetE");
    if (!base) return sysv_fail(error,QA_ERROR_ARGUMENT,"classic locale lacks its actual facet RTTI base");
    uint64_t parent = base->address, type, table, entries[32]; int64_t offset = 0; uint32_t flags = 2;
    if (count > 30 || !sysv_allocate(r,bytes,true,out,error) ||
        !sysv_cxx_type(r,name,&parent,&offset,&flags,1,&type,error)) return false;
    const char *suffixes[] = {"_destructor","_deleting_destructor"};
    for (size_t i = 0; i < 2; ++i) {
        char *symbol = NULL;
        if (!sysv_name("__guest_",name,suffixes[i],&symbol,error)) return false;
        bool ok = sysv_cxx_unsupported(r,symbol,entries+i,error); free(symbol); if (!ok) return false;
    }
    if (count) memcpy(entries+2,methods,count*sizeof(*methods));
    return sysv_cxx_vtable(r,name,type,entries,count+2,&table,error) &&
        sysv_put_pointer(r,*out,table,error) && sysv_store(r,*out+r->target.pointer_bytes,4,1,error);
}
static bool locale_text(guest_sysv_runtime *r,const char *text,bool wide,uint64_t *out,qa_error *error)
{
    if (!wide) return sysv_cxx_text(r,text,out,error);
    size_t count = strlen(text);
    if (!sysv_allocate(r,(count+1)*4,true,out,error)) return false;
    for (size_t i = 0; i < count; ++i) if (!sysv_store(r,*out+i*4,4,(uint8_t)text[i],error)) return false;
    return true;
}
typedef struct cache_cursor { guest_sysv_runtime *runtime; uint64_t address; size_t offset; } cache_cursor;
static bool cache_value(cache_cursor *c,size_t width,uint64_t value,qa_error *error)
{
    c->offset = (c->offset+width-1)/width*width;
    if (c->offset > 1024-width) return sysv_fail(error,QA_ERROR_ARGUMENT,"classic locale cache exceeds actual allocation");
    if (!sysv_store(c->runtime,c->address+c->offset,width,value,error)) return false;
    c->offset += width; return true;
}
static bool cache_text(cache_cursor *c,const char *text,bool wide,qa_error *error)
{
    uint64_t address;
    return locale_text(c->runtime,text,wide,&address,error) &&
        cache_value(c,c->runtime->target.pointer_bytes,address,error);
}
static bool cache(guest_sysv_runtime *r,const char *name,bool wide,size_t kind,uint64_t *out,qa_error *error)
{
    size_t p = r->target.pointer_bytes, width = wide ? 4 : 1;
    if (!facet(r,name,1024,NULL,0,out,error) || !sysv_store(r,*out+p,4,2,error)) return false;
    cache_cursor cursor = {r,*out,2*p};
    if (kind == 2) {
        const char *patterns[] = {"%m/%d/%y","%m/%d/%y","%H:%M:%S","%H:%M:%S","","","AM","PM",""};
        const char *days[] = {"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
        const char *months[] = {"January","February","March","April","May","June","July","August","September","October","November","December"};
        for (size_t i = 0; i < 9; ++i) if (!cache_text(&cursor,patterns[i],wide,error)) return false;
        for (size_t group = 0; group < 4; ++group) {
            size_t count = group < 2 ? 7 : 12;
            for (size_t i = 0; i < count; ++i) {
                const char *text = group < 2 ? days[i] : months[i]; char short_text[4];
                if (group & 1) { memcpy(short_text,text,3); short_text[3] = 0; text = short_text; }
                if (!cache_text(&cursor,text,wide,error)) return false;
            }
        }
        return true;
    }
    if (!cache_text(&cursor,"",false,error) || !cache_value(&cursor,p,0,error)) return false;
    ++cursor.offset;
    if (!kind) {
        if (!cache_text(&cursor,"true",wide,error) || !cache_value(&cursor,p,4,error) ||
            !cache_text(&cursor,"false",wide,error) || !cache_value(&cursor,p,5,error) ||
            !cache_value(&cursor,width,46,error) || !cache_value(&cursor,width,44,error)) return false;
        const char *atoms[] = {"-+xX0123456789abcdef0123456789ABCDEF","-+xX0123456789abcdefABCDEF"};
        for (size_t i = 0; i < 2; ++i) for (const char *c = atoms[i]; *c; ++c)
            if (!cache_value(&cursor,width,(uint8_t)*c,error)) return false;
    } else {
        if (!cache_value(&cursor,width,46,error) || !cache_value(&cursor,width,44,error)) return false;
        for (size_t i = 0; i < 3; ++i) if (!cache_text(&cursor,"",wide,error) || !cache_value(&cursor,p,0,error)) return false;
        cursor.offset = (cursor.offset+3)/4*4+4;
        const uint8_t format[] = {2,3,0,4,2,3,0,4};
        if (!sysv_write(r,*out+cursor.offset,format,sizeof(format),error)) return false;
        cursor.offset += sizeof(format);
        for (const char *c = "-0123456789"; *c; ++c) if (!cache_value(&cursor,width,(uint8_t)*c,error)) return false;
    }
    return true;
}
static bool ctype_method(guest_sysv_runtime *r,bool wide,const char *suffix,uint32_t operation,
    const qa_native_value_type *types,size_t count,qa_native_value_type result,uint64_t *out,qa_error *error)
{
    char *name = NULL;
    if (!sysv_name(wide ? "__guest_ctype_w_" : "__guest_ctype_c_",suffix,"",&name,error)) return false;
    const char *versions[] = {"GLIBCXX_3.4",NULL};
    bool ok = operation ? sysv_service_add(r,SYSV_LOCALE,operation,wide,0,0,"libstdc++.so.6",name,
        versions,2,types,count,result,NULL,out,error) : sysv_cxx_unsupported(r,name,out,error);
    free(name); return ok;
}
static bool ctype_methods(guest_sysv_runtime *r,bool wide,uint64_t *methods,size_t *count,qa_error *error)
{
    *count = 0; qa_native_value_type p = QA_NATIVE_ADDRESS, ch = wide ? QA_NATIVE_U32 : QA_NATIVE_I32;
    const qa_native_value_type pcc[] = {p,QA_NATIVE_U32,QA_NATIVE_U32}, pc[] = {p,ch}, ppp[] = {p,p,p};
    const qa_native_value_type pppp[] = {p,p,p,p}, pi[] = {p,QA_NATIVE_I32}, pci[] = {p,ch,QA_NATIVE_I32};
    if (wide) {
        if (!ctype_method(r,true,"is",LO_IS,pcc,3,QA_NATIVE_I32,methods+(*count)++,error)) return false;
        const char *names[] = {"is_range","scan_is","scan_not"};
        for (size_t i = 0; i < 3; ++i) if (!ctype_method(r,true,names[i],0,NULL,0,QA_NATIVE_VOID,methods+(*count)++,error)) return false;
    }
    const char *names[] = {"toupper","toupper_range","tolower","tolower_range","widen","widen_range","narrow","narrow_range"};
    const uint32_t operations[] = {LO_UPPER,LO_UPPER_RANGE,LO_LOWER,LO_LOWER_RANGE,LO_WIDEN,LO_WIDEN_RANGE,LO_NARROW,0};
    for (size_t i = 0; i < 8; ++i) {
        const qa_native_value_type *types = i == 4 ? pi : i == 5 ? pppp : i == 6 ? pci : (i & 1) ? ppp : pc;
        size_t n = i == 5 ? 4 : i == 6 || (i & 1) ? 3 : 2;
        qa_native_value_type result = i == 6 ? QA_NATIVE_I32 : (i & 1) ? p : ch;
        if (!ctype_method(r,wide,names[i],operations[i],types,n,result,methods+(*count)++,error)) return false;
    }
    return true;
}
static bool construct_facets(guest_sysv_runtime *r,bool wide,uint64_t facets,uint64_t caches,
    uint64_t c_locale,uint64_t classification,uint64_t lower,uint64_t upper,uint64_t *selected,qa_error *error)
{
    static const char *patterns[] = {"St5ctypeI?E","St7codecvtI?c11__mbstate_tE","St8numpunctI?E",
        "St7num_getI?St19istreambuf_iteratorI?St11char_traitsI?EEE","St7num_putI?St19ostreambuf_iteratorI?St11char_traitsI?EEE",
        "St7collateI?E","St10moneypunctI?Lb0EE","St10moneypunctI?Lb1EE",
        "St9money_getI?St19istreambuf_iteratorI?St11char_traitsI?EEE","St9money_putI?St19ostreambuf_iteratorI?St11char_traitsI?EEE",
        "St11__timepunctI?E","St8time_getI?St19istreambuf_iteratorI?St11char_traitsI?EEE",
        "St8time_putI?St19ostreambuf_iteratorI?St11char_traitsI?EEE","St8messagesI?E"};
    size_t p = r->target.pointer_bytes;
    for (size_t i = 0; i < 14; ++i) {
        char *name = NULL; uint64_t methods[16]; size_t count = 0;
        if (!facet_name(patterns[i],wide,&name,error)) return false;
        bool ok = true;
        if (!i) ok = ctype_methods(r,wide,methods,&count,error);
        else for (size_t slot = 0; slot < 14 && ok; ++slot) {
            char short_suffix[16];
            memcpy(short_suffix,"_virtual_",9); size_t n = 9;
            if (slot >= 10) short_suffix[n++] = '1';
            short_suffix[n++] = (char)('0'+slot%10); short_suffix[n] = 0;
            char *symbol = NULL;
            ok = sysv_name("__guest_",name,short_suffix,&symbol,error) && sysv_cxx_unsupported(r,symbol,methods+count,error);
            free(symbol); if (ok) ++count;
        }
        size_t bytes = !i ? wide ? p == 4 ? 1264 : 1344 : (7*p+514+p-1)/p*p :
            i == 10 ? 5*p : i == 13 ? 4*p : i == 1 || i == 2 || i == 5 || i == 6 || i == 7 ? 3*p : 2*p;
        uint64_t address = 0,id_address; size_t id = (wide ? 14 : 0)+i;
        if (ok) ok = facet(r,name,bytes,methods,count,&address,error) &&
            sysv_put_pointer(r,facets+id*p,address,error) && sysv_store(r,address+p,4,2,error);
        char *id_symbol = NULL;
        if (ok) ok = sysv_name("_ZN",name,"2idE",&id_symbol,error) &&
            sysv_cxx_data(r,id_symbol,"GLIBCXX_3.4",p,&id_address,error) && sysv_store(r,id_address,p,id+1,error);
        free(id_symbol); free(name); if (!ok) return false;
        if (!i) {
            selected[0] = address;
            if (!sysv_put_pointer(r,address+2*p,c_locale,error)) return false;
            if (!wide) {
                if (!sysv_put_pointer(r,address+4*p,upper,error) || !sysv_put_pointer(r,address+5*p,lower,error) ||
                    !sysv_put_pointer(r,address+6*p,classification,error)) return false;
            } else {
                size_t narrow = 3*p, widen = (narrow+129+3)/4*4, masks = (widen+1056+p-1)/p*p;
                if (!sysv_store(r,address+narrow,1,1,error)) return false;
                for (size_t c = 0; c < 128; ++c) if (!sysv_store(r,address+narrow+1+c,1,c,error)) return false;
                for (size_t c = 0; c < 256; ++c) if (!sysv_store(r,address+widen+c*4,4,c < 128 ? c : UINT32_MAX,error)) return false;
                for (size_t bit = 0; bit < 12; ++bit) {
                    uint16_t flag = (uint16_t)(bit < 8 ? 1u << (bit+8) : 1u << (bit-8));
                    if (!sysv_store(r,address+widen+1024+bit*2,2,flag,error)) return false;
                    if (bit == 8) continue;
                    uint64_t table;
                    if (!sysv_allocate(r,56,true,&table,error)) return false;
                    const uint32_t header[] = {7,1,5,3,0,24};
                    for (size_t h = 0; h < 6; ++h) if (!sysv_store(r,table+h*4,4,header[h],error)) return false;
                    for (size_t group = 0; group < 4; ++group) {
                        uint32_t bits = 0;
                        for (size_t offset = 0; offset < 32; ++offset)
                            if (character_mask((int)(group*32+offset)) & flag) bits |= UINT32_C(1) << offset;
                        if (!sysv_store(r,table+24+group*4,4,40+group*4,error) || !sysv_store(r,table+40+group*4,4,bits,error)) return false;
                    }
                    if (!sysv_put_pointer(r,address+masks+bit*p,table,error)) return false;
                }
            }
        } else if (i == 2 || i == 6 || i == 7 || i == 10) {
            const char *pattern = i == 2 ? "St16__numpunct_cacheI?E" : i == 10 ? "St17__timepunct_cacheI?E" :
                i == 6 ? "St18__moneypunct_cacheI?Lb0EE" : "St18__moneypunct_cacheI?Lb1EE";
            char *cache_name = NULL; uint64_t cached;
            if (!facet_name(pattern,wide,&cache_name,error)) return false;
            ok = cache(r,cache_name,wide,i == 2 ? 0 : i == 10 ? 2 : 1,&cached,error); free(cache_name);
            if (!ok || !sysv_put_pointer(r,address+2*p,cached,error) || !sysv_put_pointer(r,caches+id*p,cached,error)) return false;
            if (i == 10) { uint64_t text;
                if (!sysv_put_pointer(r,address+3*p,c_locale,error) || !sysv_cxx_text(r,"C",&text,error) || !sysv_put_pointer(r,address+4*p,text,error)) return false; }
        } else if (i == 1 || i == 5 || i == 13) {
            if (!sysv_put_pointer(r,address+2*p,c_locale,error)) return false;
            if (i == 13) { uint64_t text;
                if (!sysv_cxx_text(r,"C",&text,error) || !sysv_put_pointer(r,address+3*p,text,error)) return false; }
        }
        if (i == 3) selected[1] = address;
        if (i == 4) selected[2] = address;
    }
    return true;
}
bool sysv_locale_install(guest_sysv_runtime *r,qa_error *error)
{
    size_t p = r->target.pointer_bytes; uint64_t facet_base,c_locale,tables,c_name,facets,caches,names;
    if (!sysv_cxx_type(r,"NSt6locale5facetE",NULL,NULL,NULL,0,&facet_base,error) ||
        !sysv_allocate(r,29*p,true,&c_locale,error) || !sysv_allocate(r,384*10,true,&tables,error)) return false;
    uint64_t classification = tables+128*2, lower = tables+384*2+128*4, upper = tables+384*6+128*4;
    for (int c = -128; c < 256; ++c) {
        int byte = c < 0 && c != -1 ? c+256 : c;
        if (!sysv_store(r,tables+(size_t)(c+128)*2,2,character_mask(byte),error) ||
            !sysv_store(r,tables+384*2+(size_t)(c+128)*4,4,(uint32_t)(byte >= 65 && byte <= 90 ? byte+32 : byte),error) ||
            !sysv_store(r,tables+384*6+(size_t)(c+128)*4,4,(uint32_t)(byte >= 97 && byte <= 122 ? byte-32 : byte),error)) return false;
    }
    if (!sysv_put_pointer(r,c_locale+13*p,classification,error) || !sysv_put_pointer(r,c_locale+14*p,lower,error) ||
        !sysv_put_pointer(r,c_locale+15*p,upper,error) || !sysv_cxx_text(r,"C",&c_name,error)) return false;
    for (size_t i = 0; i < 13; ++i) if (!sysv_put_pointer(r,c_locale+(16+i)*p,c_name,error)) return false;
    const char *symbols[] = {"__ctype_b_loc","__ctype_tolower_loc","__ctype_toupper_loc"};
    const uint64_t values[] = {classification,lower,upper}; const char *versions[] = {"GLIBC_2.3",NULL};
    for (size_t i = 0; i < 3; ++i) {
        uint64_t slot;
        if (!sysv_allocate(r,p,true,&slot,error) || !sysv_put_pointer(r,slot,values[i],error) ||
            !sysv_service_add(r,SYSV_LOCALE,LO_SLOT,slot,0,0,"libc.so.6",symbols[i],versions,2,NULL,0,QA_NATIVE_ADDRESS,NULL,NULL,error)) return false;
    }
    if (!sysv_allocate(r,28*p,true,&facets,error) || !sysv_allocate(r,28*p,true,&caches,error) ||
        !sysv_allocate(r,12*p,true,&names,error) || !sysv_put_pointer(r,names,c_name,error) ||
        !sysv_allocate(r,5*p,true,&r->classic_locale,error) || !sysv_store(r,r->classic_locale,4,2,error) ||
        !sysv_put_pointer(r,r->classic_locale+p,facets,error) || !sysv_store(r,r->classic_locale+2*p,p,28,error) ||
        !sysv_put_pointer(r,r->classic_locale+3*p,caches,error) || !sysv_put_pointer(r,r->classic_locale+4*p,names,error)) return false;
    uint64_t narrow[3],wide[3];
    if (!construct_facets(r,false,facets,caches,c_locale,classification,lower,upper,narrow,error) ||
        !construct_facets(r,true,facets,caches,c_locale,classification,lower,upper,wide,error)) return false;
    uint64_t selected[] = {narrow[0],wide[0],narrow[2],wide[2],narrow[1],wide[1],c_locale};
    return sysv_object_add(r,20,"classic",r->classic_locale,5*p,selected,7,error);
}
static int32_t signed_byte(uint32_t value)
{ uint8_t byte = (uint8_t)value; return byte < 128 ? byte : (int32_t)byte-256; }
static bool convert(guest_sysv_runtime *r,uint64_t object,bool wide,bool upper,uint32_t input,uint32_t *out,qa_error *error)
{
    if (wide) { *out = upper ? input >= 97 && input <= 122 ? input-32 : input : input >= 65 && input <= 90 ? input+32 : input; return true; }
    uint64_t table,value;
    if (!sysv_pointer(r,object+(upper ? UINT64_C(4) : UINT64_C(5))*r->target.pointer_bytes,&table,error) || !table ||
        !sysv_unsigned(r,table+(input&255)*4,4,&value,error)) return false;
    *out = (uint32_t)signed_byte((uint32_t)value); return true;
}
static bool widen(guest_sysv_runtime *r,uint64_t object,bool wide,uint8_t input,uint32_t *out,qa_error *error)
{
    if (!wide) { *out = (uint32_t)signed_byte(input); return true; }
    uint64_t value; size_t offset = ((size_t)3*r->target.pointer_bytes+129+3)/4*4;
    if (!sysv_unsigned(r,object+offset+input*4,4,&value,error)) return false;
    *out = (uint32_t)value; return true;
}
bool sysv_locale_call(sysv_service *service,const qa_native_value *args,qa_native_value *out,qa_error *error)
{
    guest_sysv_runtime *r = service->runtime; bool wide = service->a != 0;
    out->type = service->result.kind;
    if (service->operation == LO_SLOT) { out->as.address = service->a; return true; }
    uint64_t object = args[0].as.address; uint32_t value = 0;
    bool range = service->operation == LO_UPPER_RANGE || service->operation == LO_LOWER_RANGE || service->operation == LO_WIDEN_RANGE;
    if (!range && !object) return sysv_fail(error,QA_ERROR_ARGUMENT,"ctype operation requires its actual facet object");
    if (service->operation == LO_IS) {
        uint32_t requested = args[1].as.u32, input = args[2].as.u32;
        size_t p = r->target.pointer_bytes, offset = (3*p+129+3)/4*4, masks = (offset+1056+p-1)/p*p;
        out->as.i32 = 0;
        for (size_t bit = 0; bit < 12; ++bit) {
            uint64_t flag,table,shift,count,lookup,index_mask;
            if (!sysv_unsigned(r,object+offset+1024+bit*2,2,&flag,error)) return false;
            if (!(flag&requested)) continue;
            if (!sysv_pointer(r,object+masks+bit*p,&table,error)) return false;
            if (!table) continue;
            if (!sysv_unsigned(r,table,4,&shift,error) || !sysv_unsigned(r,table+4,4,&count,error)) return false;
            uint32_t first = input >> (shift&31);
            if (first >= count) continue;
            if (!sysv_unsigned(r,table+20+(uint64_t)first*4,4,&lookup,error)) return false;
            if (!lookup) continue;
            if (!sysv_unsigned(r,table+8,4,&shift,error) || !sysv_unsigned(r,table+12,4,&index_mask,error)) return false;
            uint32_t second = (input >> (shift&31))&(uint32_t)index_mask;
            if (!sysv_unsigned(r,table+lookup+(uint64_t)second*4,4,&lookup,error)) return false;
            if (!lookup) continue;
            if (!sysv_unsigned(r,table+16,4,&index_mask,error) ||
                !sysv_unsigned(r,table+lookup+(uint64_t)((input>>5)&(uint32_t)index_mask)*4,4,&count,error)) return false;
            if (((uint32_t)count >> (input&31))&1) { out->as.i32 = 1; return true; }
        }
        return true;
    }
    if (service->operation == LO_UPPER || service->operation == LO_LOWER) {
        if (!convert(r,object,wide,service->operation == LO_UPPER,(uint32_t)sysv_integer(args+1),&value,error)) return false;
    } else if (service->operation == LO_WIDEN) {
        if (!widen(r,object,wide,(uint8_t)args[1].as.i32,&value,error)) return false;
    } else if (service->operation == LO_NARROW) {
        uint32_t input = (uint32_t)sysv_integer(args+1); uint64_t enabled,character;
        if (wide && input < 128) {
            size_t offset = 3*r->target.pointer_bytes;
            if (!sysv_unsigned(r,object+offset,1,&enabled,error)) return false;
            if (enabled) { if (!sysv_unsigned(r,object+offset+1+input,1,&character,error)) return false; input = (uint32_t)character; }
        } else if (wide && input > 127) input = (uint32_t)args[2].as.i32;
        value = (uint32_t)signed_byte(input);
    } else {
        uint64_t source = args[1].as.address,end = args[2].as.address;
        uint64_t target = service->operation == LO_WIDEN_RANGE ? args[3].as.address : source;
        if (!source || !end || !target) return sysv_fail(error,QA_ERROR_ARGUMENT,"ctype range requires its actual nonnull endpoints");
        if (source < end && !object) return sysv_fail(error,QA_ERROR_ARGUMENT,"nonempty ctype range requires its actual facet object");
        size_t width = wide ? 4 : 1;
        for (; source < end; source += service->operation == LO_WIDEN_RANGE ? 1 : width,target += width) {
            uint64_t input;
            if (!sysv_unsigned(r,source,service->operation == LO_WIDEN_RANGE ? 1 : width,&input,error)) return false;
            bool ok = service->operation == LO_WIDEN_RANGE ? widen(r,object,wide,(uint8_t)input,&value,error) :
                convert(r,object,wide,service->operation == LO_UPPER_RANGE,(uint32_t)input,&value,error);
            if (!ok || !sysv_store(r,target,width,value,error)) return false;
        }
        out->as.address = end; return true;
    }
    if (out->type == QA_NATIVE_U32) out->as.u32 = value;
    else out->as.i32 = value <= INT32_MAX ? (int32_t)value : -(int32_t)(UINT32_MAX-value)-1;
    return true;
}
