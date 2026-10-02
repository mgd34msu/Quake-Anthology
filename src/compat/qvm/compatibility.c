#include "internal.h"
#include "qa/json.h"
#include "qa/strings.h"
#include "qa/text.h"
#include "qa/vfs.h"

#include <stdlib.h>
#include <string.h>

static bool folded_path(qa_bytes input, qa_buffer *out, qa_error *error)
{
    if (input.data == NULL || input.size == 0 || memchr(input.data,0,input.size) != NULL)
        return qa_qvm_error(error,QA_ERROR_FORMAT,0,"invalid QVM compatibility resource path");
    qa_buffer normalized = {malloc(input.size),input.size};
    if (normalized.data == NULL) return qa_qvm_error(error,QA_ERROR_MEMORY,0,"allocating compatibility path");
    for (size_t i = 0; i < input.size; ++i) normalized.data[i] = input.data[i] == '\\' ? '/' : input.data[i];
    bool valid = true;
    if (input.size >= 2 && normalized.data[1] == ':'
        && ((normalized.data[0] >= 'a' && normalized.data[0] <= 'z') || (normalized.data[0] >= 'A' && normalized.data[0] <= 'Z'))) valid = false;
    for (size_t begin = 0, i = 0; i <= normalized.size; ++i) {
        if (i != normalized.size && normalized.data[i] != '/') continue;
        size_t length = i - begin;
        if (length == 0 || (length == 1 && normalized.data[begin] == '.')
            || (length == 2 && normalized.data[begin] == '.' && normalized.data[begin + 1] == '.')) valid = false;
        begin = i + 1;
    }
    bool result = valid ? qa_utf8_lower((qa_bytes){normalized.data,normalized.size},out,error)
        : qa_qvm_error(error,QA_ERROR_FORMAT,0,"invalid QVM compatibility resource path");
    qa_buffer_free(&normalized);
    return result;
}

static bool copy_value(const qa_json_document *document, qa_json_id id, qa_buffer *out, qa_error *error)
{
    if (id == QA_JSON_NONE) return true;
    qa_bytes bytes = qa_json_source(document,id);
    qa_buffer copy = {malloc(bytes.size + 1),bytes.size};
    if (copy.data == NULL) return qa_qvm_error(error,QA_ERROR_MEMORY,0,"allocating compatibility declaration");
    memcpy(copy.data,bytes.data,bytes.size); copy.data[bytes.size] = 0;
    *out = copy;
    return true;
}

void qa_qvm_compatibility_free(qa_qvm_compatibility *compatibility)
{
    if (compatibility == NULL) return;
    qa_buffer_free(&compatibility->primary);
    qa_buffer_free(&compatibility->equipment_presentation);
    qa_buffer_free(&compatibility->collision_scene);
    *compatibility = (qa_qvm_compatibility){0};
}

bool qa_qvm_compatibility_parse(qa_bytes json, const char *artifact_path, const qa_sha256_digest *digest,
                                 qa_qvm_role role, qa_qvm_compatibility *out, qa_error *error)
{
    if (artifact_path == NULL || digest == NULL || out == NULL || (unsigned)role > QA_QVM_UI)
        return qa_qvm_error(error,QA_ERROR_ARGUMENT,0,"invalid QVM compatibility request");
    qa_json_document *document = NULL;
    qa_strings *seen[3] = {NULL,NULL,NULL};
    qa_buffer selected_path = {0}, path = {0}, folded = {0}, digest_text = {0};
    qa_qvm_compatibility selected = {0};
    bool success = false;
    if (!folded_path((qa_bytes){(const uint8_t *)artifact_path,strlen(artifact_path)},&selected_path,error)
        || !qa_json_parse(json,&document,error)) goto finished;
    qa_json_id root = qa_json_root(document), modules = qa_json_get(document,root,"modules");
    int64_t version;
    if (qa_json_type(document,root) != QA_JSON_OBJECT
        || !qa_json_i64(document,qa_json_get(document,root,"version"),&version,error)
        || version != 1 || qa_json_type(document,modules) != QA_JSON_ARRAY) {
        qa_qvm_error(error,QA_ERROR_FORMAT,0,"invalid QVM compatibility document"); goto finished;
    }
    for (size_t i = 0; i < 3; ++i) if (!qa_strings_create(&seen[i],error)) goto finished;
    for (size_t i = 0; i < qa_json_size(document,modules); ++i) {
        qa_json_id entry = qa_json_at(document,modules,i), role_id = qa_json_get(document,entry,"role");
        qa_qvm_role entry_role;
        if (qa_json_string_equal(document,role_id,"qagame")) entry_role = QA_QVM_GAME;
        else if (qa_json_string_equal(document,role_id,"cgame")) entry_role = QA_QVM_CGAME;
        else if (qa_json_string_equal(document,role_id,"ui")) entry_role = QA_QVM_UI;
        else { qa_qvm_error(error,QA_ERROR_FORMAT,i,"invalid QVM compatibility role"); goto finished; }
        if (!qa_json_string(document,qa_json_get(document,entry,"artifactPath"),&path,error)
            || !folded_path((qa_bytes){path.data,path.size},&folded,error)) goto finished;
        qa_bytes key = {folded.data,folded.size};
        if (qa_strings_find(seen[entry_role],key) != QA_STRING_NONE) {
            qa_qvm_error(error,QA_ERROR_FORMAT,i,"duplicate QVM compatibility module"); goto finished;
        }
        qa_string_id identity;
        if (!qa_strings_intern(seen[entry_role],key,&identity,error)
            || !qa_json_string(document,qa_json_get(document,entry,"artifactDigest"),&digest_text,error)) goto finished;
        bool valid_digest = digest_text.size == 71 && memcmp(digest_text.data,"sha256:",7) == 0;
        for (size_t n = 7; valid_digest && n < digest_text.size; ++n)
            if (!((digest_text.data[n] >= '0' && digest_text.data[n] <= '9') || (digest_text.data[n] >= 'a' && digest_text.data[n] <= 'f'))) valid_digest = false;
        qa_sha256_digest expected;
        if (!valid_digest || !qa_sha256_parse((const char *)digest_text.data,&expected,error)) {
            qa_qvm_error(error,QA_ERROR_FORMAT,i,"QVM compatibility requires an exact lowercase sha256 digest"); goto finished;
        }
        qa_json_id profile = qa_json_get(document,entry,"profile");
        qa_qvm_abi abi;
        if (qa_json_string_equal(document,profile,"q3-modern")) abi = QA_QVM_Q3_MODERN;
        else if (qa_json_string_equal(document,profile,"q3-1.16n-base")) abi = QA_QVM_Q3_116N;
        else { qa_qvm_error(error,QA_ERROR_FORMAT,i,"invalid QVM compatibility ABI profile"); goto finished; }
        qa_json_id primary = qa_json_get(document,entry,"primary"), equipment = qa_json_get(document,entry,"equipmentPresentation");
        qa_json_id collision = qa_json_get(document,entry,"collisionScene");
        if ((primary != QA_JSON_NONE && entry_role != QA_QVM_GAME) || (equipment != QA_JSON_NONE && entry_role != QA_QVM_CGAME) ||
            (collision != QA_JSON_NONE && entry_role != QA_QVM_CGAME)) {
            qa_qvm_error(error,QA_ERROR_FORMAT,i,"QVM compatibility interface belongs to another role"); goto finished;
        }
        if (entry_role == role && key.size == selected_path.size && memcmp(key.data,selected_path.data,key.size) == 0) {
            if (!qa_sha256_equal(&expected,digest)) { qa_qvm_error(error,QA_ERROR_FORMAT,i,"QVM compatibility declaration belongs to different artifact bytes"); goto finished; }
            selected.abi = abi; selected.declared = true;
            if (!copy_value(document,primary,&selected.primary,error)
                || !copy_value(document,equipment,&selected.equipment_presentation,error)
                || !copy_value(document,collision,&selected.collision_scene,error)) goto finished;
        }
        qa_buffer_free(&path); qa_buffer_free(&folded); qa_buffer_free(&digest_text);
    }
    success = true; *out = selected; selected = (qa_qvm_compatibility){0};
finished:
    qa_qvm_compatibility_free(&selected);
    qa_buffer_free(&selected_path); qa_buffer_free(&path); qa_buffer_free(&folded); qa_buffer_free(&digest_text);
    for (size_t i = 0; i < 3; ++i) qa_strings_destroy(seen[i]);
    qa_json_destroy(document);
    return success;
}

bool qa_qvm_compatibility_read(qa_vfs *vfs, const char *artifact_path, const qa_sha256_digest *digest,
                                qa_qvm_role role, qa_qvm_compatibility *out, qa_error *error)
{
    if (vfs == NULL || artifact_path == NULL || digest == NULL || out == NULL || (unsigned)role > QA_QVM_UI)
        return qa_qvm_error(error,QA_ERROR_ARGUMENT,0,"invalid QVM compatibility read");
    qa_resource *resource;
    qa_error open_error = {0};
    if (!qa_vfs_acquire(vfs,"qvm-compatibility.json",&resource,NULL,&open_error)) {
        if (open_error.code == QA_ERROR_NOT_FOUND) { *out = (qa_qvm_compatibility){0}; return true; }
        if (error != NULL) *error = open_error;
        return false;
    }
    bool success = qa_qvm_compatibility_parse(qa_resource_bytes(resource),artifact_path,digest,role,out,error);
    qa_resource_release(resource);
    return success;
}

bool qa_qvm_image_open(qa_vfs *vfs, const char *artifact_path, qa_qvm_role role,
                         qa_qvm_image **out, qa_qvm_compatibility *compatibility, qa_error *error)
{
    if (out == NULL || compatibility == NULL) return qa_qvm_error(error,QA_ERROR_ARGUMENT,0,"missing QVM artifact output");
    qa_resource *resource;
    if (!qa_vfs_acquire(vfs,artifact_path,&resource,NULL,error)) return false;
    qa_qvm_image *image = NULL;
    bool success = qa_qvm_image_load(qa_resource_bytes(resource),&image,error);
    qa_resource_release(resource);
    if (success) success = qa_qvm_compatibility_read(vfs,artifact_path,&image->digest,role,compatibility,error);
    if (!success) { qa_qvm_image_release(image); return false; }
    *out = image;
    return true;
}
