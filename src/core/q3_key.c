#include "qa/q3_key.h"

#include <stdlib.h>
#include <string.h>

struct qa_q3_key {
    qa_cvars *cvars;
    uint8_t bytes[34];
    bool dedicated,writing;
    qa_q3_key_storage_fn storage;
    void *storage_context;
};

bool qa_q3_key_valid(const char *key, const char *checksum)
{
    if (!key || strlen(key) != 16 || (checksum && strlen(checksum) != 2)) return false;
    unsigned sum = 0;
    for (size_t i = 0; i < 16; ++i) {
        unsigned c = (uint8_t)key[i];
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        if (!strchr("237ABCDGHJLPRSTW", (int)c)) return false;
        sum = (sum + c) & 255u;
    }
    if (!checksum) return true;
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < 2; ++i) {
        unsigned c = (uint8_t)checksum[i];
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != (unsigned)hex[i ? sum & 15u : sum >> 4]) return false;
    }
    return true;
}

bool qa_q3_key_create(qa_cvars *cvars, bool dedicated, qa_q3_key **out, qa_error *error)
{
    if (!cvars || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q3 key state requires shared cvars and output"); return false;
    }
    qa_q3_key *key=NULL;
    if (!qa_q3_key_create_detached(dedicated,&key,error)) return false;
    key->cvars=cvars; *out=key; return true;
}

bool qa_q3_key_create_detached(bool dedicated,qa_q3_key **out,qa_error *error)
{
    if (!out || *out) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"detached Q3 key requires an empty output"); return false;
    }
    qa_q3_key *key = calloc(1, sizeof(*key));
    if (!key) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q3 key state"); return false; }
    key->dedicated = dedicated;
    if (dedicated) for (size_t i = 0; i < 9; ++i) key->bytes[i] = (uint8_t)('1' + i);
    else memset(key->bytes, ' ', 32);
    *out = key; return true;
}

bool qa_q3_key_rebind_cvars(qa_q3_key *key,qa_cvars *cvars,qa_error *error)
{
    if (!key || key->writing || (cvars && qa_cvars_dialect(cvars)!=QA_RULESET_Q3)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q3 key registry binding requires its returned source owner"); return false;
    }
    key->cvars=cvars; return true;
}

bool qa_q3_key_bind_storage(qa_q3_key *key,qa_q3_key_storage_fn storage,void *context,qa_error *error)
{
    if (!key || key->writing || !storage || key->storage) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q3 key storage already has its actual profile owner"); return false;
    }
    key->storage=storage; key->storage_context=context; return true;
}

void qa_q3_key_destroy(qa_q3_key *key)
{
    if (!key) return;
    volatile uint8_t *bytes = key->bytes;
    for (size_t i = 0; i < sizeof(key->bytes); ++i) bytes[i] = 0;
    free(key);
}

static bool file_name_equal(const char *a,const char *b,void *context)
{
    (void)context;
    for (;;++a,++b) {
        unsigned x=(uint8_t)*a,y=(uint8_t)*b;
        if (x>='A' && x<='Z') x+='a'-'A';
        if (y>='A' && y<='Z') y+='a'-'A';
        if (x!=y) return false;
        if (!x) return true;
    }
}
static bool file_key(qa_q3_key *key, qa_fs_root *root, qa_fs_root *fallback,uint8_t value[17], bool *present,
                       qa_error *error)
{
    qa_fs_file *file; qa_fs_identity identity;
    qa_error local = {0}; char *path=NULL;
    if (!qa_fs_root_resolve(root,"q3key",file_name_equal,NULL,false,&path,&local)) {
        if (local.code==QA_ERROR_NOT_FOUND && fallback) return file_key(key,fallback,NULL,value,present,error);
        if (local.code==QA_ERROR_NOT_FOUND) { *present=false; return true; }
        if (error) *error=local;
        return false;
    }
    bool opened=qa_fs_root_file_open(root,path,&file,&identity,&local); free(path);
    if (!opened) {
        if (local.code == QA_ERROR_NOT_FOUND) { *present = false; return true; }
        if (error) *error = local;
        return false;
    }
    qa_buffer bytes = {0};
    bool ok = qa_fs_file_read_snapshot(file, &identity, &bytes, error);
    qa_fs_file_close(file);
    if (!ok) return false;
    size_t count = bytes.size < 16 ? bytes.size : 16;
    memset(value, 0, 17);
    const uint8_t *nul = count ? memchr(bytes.data, 0, count) : NULL;
    if (nul) count = (size_t)(nul - bytes.data);
    if (count) memcpy(value, bytes.data, count);
    qa_buffer_free(&bytes);
    *present = key->dedicated || qa_q3_key_valid((const char *)value, NULL);
    return true;
}

bool qa_q3_key_load(qa_q3_key *key, qa_fs_root *root, qa_q3_key_product product, qa_error *error)
{
    return qa_q3_key_load_profile(key,root,NULL,product,error);
}
bool qa_q3_key_load_profile(qa_q3_key *key,qa_fs_root *root,qa_fs_root *fallback,
    qa_q3_key_product product,qa_error *error)
{
    if (!key || !root || (unsigned)product > QA_Q3_KEY_EXPANSION) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid Q3 key load"); return false;
    }
    uint8_t value[17]; bool present;
    if (!file_key(key, root, fallback,value, &present, error)) return false;
    size_t offset = product == QA_Q3_KEY_BASE ? 0 : 16;
    if (!present) {
        memset(key->bytes + offset, ' ', 16); key->bytes[offset + 16] = 0; return true;
    }
    if (product == QA_Q3_KEY_BASE) memcpy(key->bytes, value, 17);
    else {
        while (offset < sizeof(key->bytes) && key->bytes[offset]) ++offset;
        size_t length = strlen((const char *)value) + 1;
        if (offset > sizeof(key->bytes) - length) {
            qa_error_set(error, QA_ERROR_FORMAT, 0, "CD key storage overflow"); return false;
        }
        memcpy(key->bytes + offset, value, length);
    }
    return true;
}

bool qa_q3_key_save(const qa_q3_key *key, qa_fs_root *root, qa_q3_key_product product,
                      uint64_t nonce, qa_error *error)
{
    if (!key || !root || (unsigned)product > QA_Q3_KEY_EXPANSION) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid Q3 key save"); return false;
    }
    static const char suffix[] = "\n// generated by quake, do not modify\r\n"
        "// Do not give this file to ANYONE.\r\n"
        "// id Software and Activision will NOT ask you to send this file to them.\r\n";
    char text[16 + sizeof(suffix)];
    memcpy(text, key->bytes + (product == QA_Q3_KEY_BASE ? 0 : 16), 16); text[16] = 0;
    if (!qa_q3_key_valid(text, NULL)) return true;
    memcpy(text + 16, suffix, sizeof(suffix));
    return qa_fs_root_replace(root, "q3key", (qa_bytes){(const uint8_t *)text, sizeof(text) - 1}, nonce, error);
}

void qa_q3_key_authorization(const qa_q3_key *key, uint8_t out[33])
{
    memcpy(out, key->bytes, 32); out[32] = 0;
}

static size_t ui_offset(int32_t unique, const char *directory)
{
    return unique == 1 && directory && directory[0] ? 16u : 0u;
}

void qa_q3_key_read_ui(const qa_q3_key *key, int32_t unique, const char *directory, uint8_t out[17])
{
    memcpy(out, key->bytes + ui_offset(unique, directory), 16); out[16] = 0;
}

void qa_q3_key_write_ui(qa_q3_key *key, int32_t unique, const char *directory, const uint8_t input[16])
{
    size_t offset = ui_offset(unique, directory);
    memcpy(key->bytes + offset, input, 16);
    if (offset == 16) key->bytes[32] = 0;
    if (key->cvars) qa_cvars_mark_modified_flags(key->cvars, QA_CVAR_ARCHIVE);
}

bool qa_q3_key_write_ui_stored(qa_q3_key *key,int32_t unique,const char *directory,
    const uint8_t input[16],qa_error *error)
{
    if (!key || !input || !key->cvars || key->writing) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q3 UI key write has no actual idle source registry"); return false;
    }
    key->writing=true;
    qa_q3_key_write_ui(key,unique,directory,input);
    bool ok=!key->storage || key->storage(key->storage_context,
        ui_offset(unique,directory)?QA_Q3_KEY_EXPANSION:QA_Q3_KEY_BASE,error);
    key->writing=false; return ok;
}

void qa_q3_key_capture(const qa_q3_key *key, uint8_t out[34])
{
    memcpy(out, key->bytes, 34);
}

bool qa_q3_key_restore(qa_q3_key *key, qa_bytes input, qa_error *error)
{
    if (!key || !input.data || input.size != 34 || input.data[33]) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "invalid Q3 key checkpoint"); return false;
    }
    memcpy(key->bytes, input.data, 34); return true;
}
