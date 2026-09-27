#ifndef QA_Q3_KEY_H
#define QA_Q3_KEY_H

#include "qa/console.h"
#include "qa/filesystem.h"

typedef struct qa_q3_key qa_q3_key;
typedef enum qa_q3_key_product { QA_Q3_KEY_BASE, QA_Q3_KEY_EXPANSION } qa_q3_key_product;
bool qa_q3_key_valid(const char *key, const char *checksum);
bool qa_q3_key_create(qa_cvars *, bool dedicated, qa_q3_key **, qa_error *);
void qa_q3_key_destroy(qa_q3_key *);
/* Root names one selected product's loose configuration directory. */
bool qa_q3_key_load(qa_q3_key *, qa_fs_root *, qa_q3_key_product, qa_error *);
bool qa_q3_key_save(const qa_q3_key *, qa_fs_root *, qa_q3_key_product,
                     uint64_t replace_nonce, qa_error *);
void qa_q3_key_authorization(const qa_q3_key *, uint8_t out[33]);
void qa_q3_key_read_ui(const qa_q3_key *, int32_t unique, const char *game_directory,
                        uint8_t out[17]);
void qa_q3_key_write_ui(qa_q3_key *, int32_t unique, const char *game_directory,
                         const uint8_t input[16]);
/* Exact private bytes, shared by the client checkpoint owner. */
void qa_q3_key_capture(const qa_q3_key *, uint8_t out[34]);
bool qa_q3_key_restore(qa_q3_key *, qa_bytes, qa_error *);

#endif
