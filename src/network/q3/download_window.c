#include "qa/network_q3_download.h"
#include "qa/source_save.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { BLOCK_BYTES = 2048, BLOCKS = 8 };
struct qa_q3_download_window {
    qa_q3_download_source source;
    uint64_t revision;
    char name[64], denial[QA_Q3_COMMAND_CHARS];
    uint8_t *file;
    qa_sha256_digest digest;
    int32_t size, count, current_block, client_block, transmit_block, send_time;
    int32_t block_sizes[BLOCKS];
    uint8_t blocks[BLOCKS][BLOCK_BYTES];
    bool eof, denied;
};
static bool fail(qa_error *error, qa_status code, const char *text)
{ qa_error_set(error, code, 0, "%s", text); return false; }
static bool advance(qa_q3_download_window *window, qa_error *error)
{
    if (window->revision == UINT64_MAX) return fail(error, QA_ERROR_FORMAT, "Q3 download mutation sequence exhausted");
    ++window->revision; return true;
}
static int32_t signed_word(uint32_t value)
{ return value <= INT32_MAX ? (int32_t)value : (int32_t)((int64_t)value - INT64_C(4294967296)); }
bool qa_q3_download_window_create(const qa_q3_download_source *source, qa_q3_download_window **out, qa_error *error)
{
    if (!source || !source->resolve || !out || *out)
        return fail(error, QA_ERROR_ARGUMENT, "Q3 download requires its actual mounted resolver and empty output");
    qa_q3_download_window *window = calloc(1, sizeof(*window));
    if (!window) return fail(error, QA_ERROR_MEMORY, "Allocating Q3 source download window");
    window->source = *source; window->revision = 1; *out = window; return true;
}
void qa_q3_download_window_close(qa_q3_download_window *window)
{
    if (!window) return;
    free(window->file); window->file = NULL; window->name[0] = 0;
    window->denied = false; window->denial[0] = 0;
    /* Source CloseDownload retains physical block storage and counters. */
    if (window->revision < UINT64_MAX) ++window->revision;
}
void qa_q3_download_window_destroy(qa_q3_download_window *window)
{ if (window) { free(window->file); free(window); } }
void qa_q3_download_window_rebind(qa_q3_download_window *window, void *context)
{ if (window) window->source.context = context; }
const char *qa_q3_download_window_name(const qa_q3_download_window *window)
{ return window ? window->name : ""; }
bool qa_q3_download_window_active(const qa_q3_download_window *window)
{ return window && *window->name && !window->denied; }
bool qa_q3_download_window_begin(qa_q3_download_window *window, const char *name, qa_error *error)
{
    if (!window || !name) return fail(error, QA_ERROR_ARGUMENT, "Missing Q3 download source name");
    if (window->revision == UINT64_MAX) return fail(error, QA_ERROR_FORMAT, "Q3 download mutation sequence exhausted");
    qa_q3_download_window_close(window);
    size_t length = 0; while (name[length] && length < sizeof(window->name) - 1) ++length;
    memcpy(window->name, name, length); window->name[length] = 0; return true;
}
bool qa_q3_download_window_acknowledge(qa_q3_download_window *window, int32_t block, int32_t time,
    bool *broken, qa_error *error)
{
    if (!window || !broken) return fail(error, QA_ERROR_ARGUMENT, "Missing Q3 download acknowledgement owner");
    *broken = block != window->client_block;
    if (*broken) return true;
    if (window->block_sizes[(uint32_t)window->client_block % BLOCKS] != 0 && window->client_block == INT32_MAX)
        return fail(error, QA_ERROR_FORMAT, "Q3 download block sequence exhausted");
    if (!advance(window, error)) return false;
    if (window->block_sizes[(uint32_t)window->client_block % BLOCKS] == 0) {
        qa_q3_download_window_close(window); return true;
    }
    window->send_time = time; ++window->client_block; return true;
}
static bool open_file(qa_q3_download_window *window, bool enabled, bool pure, qa_error *error)
{
    unsigned stock = qa_q3_stock_package(window->name);
    qa_bytes bytes = {0}; const qa_sha256_digest *digest = NULL;
    if (enabled && !stock) {
        if (!qa_q3_download_name(window->name, error) ||
            !window->source.resolve(window->source.context, window->name, &bytes, &digest, error)) return false;
        window->size = bytes.data && digest && bytes.size > 0 && bytes.size <= INT32_MAX ? (int32_t)bytes.size : -1;
        if (bytes.data && digest && bytes.size > 0 && bytes.size <= INT32_MAX) {
            uint8_t *copy = malloc(bytes.size);
            if (!copy) return fail(error, QA_ERROR_MEMORY, "Retaining actual Q3 mounted download file");
            memcpy(copy, bytes.data, bytes.size); window->file = copy; window->digest = *digest;
            window->size = (int32_t)bytes.size; window->count = 0; window->current_block = 0;
            window->client_block = 0; window->transmit_block = 0; window->eof = false;
            return true;
        }
    }
    window->denied = true;
    if (stock == 2) snprintf(window->denial, sizeof(window->denial),
        "Cannot autodownload Team Arena file \"%s\"\nThe Team Arena mission pack can be found in your local game store.", window->name);
    else if (stock) snprintf(window->denial, sizeof(window->denial), "Cannot autodownload id pk3 file \"%s\"", window->name);
    else if (!enabled) snprintf(window->denial, sizeof(window->denial),
        "Could not download \"%s\" because autodownloading is disabled on the server.\n\n%s", window->name,
        pure ? "You will need to get this file elsewhere before you can connect to this pure server.\n" :
        "The server you are connecting to is not a pure server, set autodownload to No in your settings and you might be able to join the game anyway.\n");
    else snprintf(window->denial, sizeof(window->denial), "File \"%s\" not found on server for autodownloading.\n", window->name);
    return true;
}
static int32_t allowed_blocks(const qa_q3_server_rate *settings)
{
    double selected_rate = settings->bytes_per_second;
    if (settings->maximum_rate) {
        double maximum = settings->maximum_rate < 1000 ? 1000 : settings->maximum_rate;
        if (selected_rate > maximum) selected_rate = maximum;
    }
    if (!selected_rate) return 1;
    /* Math.imul converts the selected positive finite rate to Uint32 here. */
    uint32_t rate = (uint32_t)selected_rate;
    int32_t product = signed_word(rate * settings->snapshot_ms);
    int32_t numerator = signed_word((uint32_t)(product / 1000) + BLOCK_BYTES);
    int32_t blocks = numerator / BLOCK_BYTES;
    return blocks < 0 ? 1 : blocks;
}
bool qa_q3_download_window_write(qa_q3_download_window *window, bool enabled, bool pure, int32_t time,
    const qa_q3_server_rate *rate, qa_q3_writer *writer,
    qa_q3_download_offer *offer, qa_error *error)
{
    if (!window || !rate || !writer || !offer || !isfinite(rate->maximum_rate))
        return fail(error, QA_ERROR_ARGUMENT, "Missing Q3 download offer owner or output");
    if (!advance(window, error)) return false;
    *offer = (qa_q3_download_offer){.owner = window, .revision = window->revision,
        .next_transmit = window->transmit_block, .send_time = window->send_time};
    if (!*window->name) return true;
    if (!window->file && !window->denied && !open_file(window, enabled, pure, error)) return false;
    if (window->denied) {
        qa_q3_download message = {.file_size = -1};
        memcpy(message.error, window->denial, sizeof(message.error));
        if (!qa_q3_server_download(writer, &message)) return false;
        offer->count = 1; offer->denied = true; return true;
    }
    if (window->current_block - window->client_block < BLOCKS && window->count != window->size) {
        qa_bytes actual = {0}; const qa_sha256_digest *digest = NULL;
        bool unchanged = window->source.resolve(window->source.context, window->name, &actual, &digest, error) &&
            actual.data && digest && actual.size == (size_t)window->size && qa_sha256_equal(digest, &window->digest);
        if (!unchanged) {
            qa_q3_download_window_close(window);
            if (!error || error->code == QA_OK) fail(error, QA_ERROR_FORMAT, "Q3 source download changed after genuine open");
            return false;
        }
    }
    while (window->current_block - window->client_block < BLOCKS && window->count != window->size) {
        size_t slot = (uint32_t)window->current_block % BLOCKS;
        int32_t bytes = window->size - window->count; if (bytes > BLOCK_BYTES) bytes = BLOCK_BYTES;
        memcpy(window->blocks[slot], window->file + window->count, (size_t)bytes);
        window->block_sizes[slot] = bytes; window->count += bytes; ++window->current_block;
    }
    if (window->count == window->size && !window->eof && window->current_block - window->client_block < BLOCKS) {
        window->block_sizes[(uint32_t)window->current_block % BLOCKS] = 0; ++window->current_block; window->eof = true;
    }
    offer->next_transmit = window->transmit_block; offer->send_time = window->send_time;
    int32_t blocks = allowed_blocks(rate);
    while (blocks-- > 0) {
        if (window->client_block == window->current_block) break;
        if (offer->next_transmit == window->current_block) {
            if (signed_word((uint32_t)time - (uint32_t)offer->send_time) > 1000) offer->next_transmit = window->client_block;
            else break;
        }
        size_t slot = (uint32_t)offer->next_transmit % BLOCKS;
        if (offer->next_transmit == INT32_MAX)
            return fail(error, QA_ERROR_FORMAT, "Q3 download transmit block sequence exhausted");
        if (!qa_q3_server_download_block(writer, offer->next_transmit, window->size,
            (qa_bytes){window->blocks[slot], (size_t)window->block_sizes[slot]})) return false;
        ++offer->count; ++offer->next_transmit; offer->send_time = time;
    }
    return true;
}
bool qa_q3_download_window_commit(qa_q3_download_window *window, const qa_q3_download_offer *offer, qa_error *error)
{
    if (!window || !offer || offer->owner != window || offer->revision != window->revision ||
        offer->denied != window->denied ||
        offer->next_transmit < 0)
        return fail(error, QA_ERROR_ARGUMENT, "Q3 packet no longer owns its genuine download offer");
    if (!advance(window, error)) return false;
    if (offer->denied) qa_q3_download_window_close(window);
    else { window->transmit_block = offer->next_transmit; window->send_time = offer->send_time; }
    return true;
}
static bool fields(qa_source_save_io *io, qa_q3_download_window *window, bool *opened)
{
    uint32_t magic = UINT32_C(0x57443351);
    if (!qa_source_save_u32(io, &magic) || magic != UINT32_C(0x57443351) ||
        !qa_source_save_u64(io, &window->revision) ||
        !qa_source_save_bytes(io, window->name, sizeof(window->name)) || !memchr(window->name, 0, sizeof(window->name)) ||
        !qa_source_save_bytes(io, window->denial, sizeof(window->denial)) || !memchr(window->denial, 0, sizeof(window->denial)) ||
        !qa_source_save_bytes(io, window->digest.bytes, sizeof(window->digest.bytes)) ||
        !qa_source_save_bool(io, opened) || !qa_source_save_bool(io, &window->denied) || !qa_source_save_bool(io, &window->eof) ||
        !qa_source_save_i32(io, &window->size) || !qa_source_save_i32(io, &window->count) ||
        !qa_source_save_i32(io, &window->current_block) || !qa_source_save_i32(io, &window->client_block) ||
        !qa_source_save_i32(io, &window->transmit_block) || !qa_source_save_i32(io, &window->send_time)) return false;
    for (size_t i = 0; i < BLOCKS; ++i) if (!qa_source_save_i32(io, window->block_sizes + i)) return false;
    return qa_source_save_bytes(io, window->blocks, sizeof(window->blocks));
}
static bool valid(const qa_q3_download_window *window, bool opened, qa_error *error)
{
    if (!window->revision || window->revision == UINT64_MAX || window->size < -1 || window->count < 0 ||
        (opened && window->count > window->size) || window->client_block < 0 || window->current_block < 0 ||
        (window->current_block >= window->client_block && window->current_block - window->client_block > BLOCKS) ||
        window->transmit_block < 0 || (opened && (!*window->name || window->denied || window->size <= 0)) ||
        (window->denied && (!*window->name || opened || !*window->denial)))
        return fail(error, QA_ERROR_FORMAT, "Invalid physical Q3 download continuation");
    for (size_t i = 0; i < BLOCKS; ++i) if (window->block_sizes[i] < 0 || window->block_sizes[i] > BLOCK_BYTES)
        return fail(error, QA_ERROR_FORMAT, "Invalid retained Q3 download block size");
    if (!opened) return true;
    int32_t data_blocks = window->size / BLOCK_BYTES + (window->size % BLOCK_BYTES != 0);
    if (window->current_block > data_blocks + 1 ||
        window->count != (window->current_block >= data_blocks ? window->size : window->current_block * BLOCK_BYTES) ||
        window->eof != (window->current_block == data_blocks + 1))
        return fail(error, QA_ERROR_FORMAT, "Q3 download read cursor differs from its retained immutable file");
    for (int32_t block = window->client_block; block < window->current_block; ++block) {
        size_t slot = (uint32_t)block % BLOCKS;
        size_t offset = (size_t)block * BLOCK_BYTES;
        int32_t expected = block == data_blocks ? 0 : (int32_t)((size_t)window->size - offset);
        if (expected > BLOCK_BYTES) expected = BLOCK_BYTES;
        if (window->block_sizes[slot] != expected || (window->file && expected &&
            memcmp(window->blocks[slot], window->file + offset, (size_t)expected)))
            return fail(error, QA_ERROR_FORMAT, "Q3 download window bytes differ from its actual mounted source");
    }
    return true;
}
bool qa_q3_download_window_checkpoint(const qa_q3_download_window *window, qa_buffer *out, qa_error *error)
{
    if (!window || !out || !valid(window, window->file != NULL, error)) return false;
    qa_q3_download_window *copy = malloc(sizeof(*copy));
    if (!copy) return fail(error, QA_ERROR_MEMORY, "Capturing physical Q3 download window");
    *copy = *window; bool opened = window->file != NULL; qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, NULL, error) && fields(&io, copy, &opened) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); free(copy); return ok;
}
bool qa_q3_download_window_restore(qa_bytes bytes, const qa_q3_download_source *source, qa_q3_download_window **out, qa_error *error)
{
    if (!out || *out) return fail(error, QA_ERROR_ARGUMENT, "Q3 download restore requires empty output");
    qa_q3_download_window *window = NULL;
    if (!qa_q3_download_window_create(source, &window, error)) return false;
    qa_source_save_io io = {0}; bool opened = false;
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && fields(&io, window, &opened) &&
        qa_source_save_finish(&io, NULL) && valid(window, opened, error);
    qa_source_save_dispose(&io);
    if (ok && opened) {
        qa_bytes actual = {0}; const qa_sha256_digest *digest = NULL;
        ok = qa_q3_download_name(window->name, error) && !qa_q3_stock_package(window->name) &&
            source->resolve(source->context, window->name, &actual, &digest, error) && actual.data && digest &&
            actual.size == (size_t)window->size && qa_sha256_equal(digest, &window->digest);
        if (ok) {
            window->file = malloc(actual.size);
            if (!window->file) ok = fail(error, QA_ERROR_MEMORY, "Retaining restored genuine Q3 download source");
            else { memcpy(window->file, actual.data, actual.size); ok = valid(window, true, error); }
        } else if (!error || error->code == QA_OK) fail(error, QA_ERROR_FORMAT, "Saved Q3 download lacks its exact mounted package");
    }
    if (!ok) { qa_q3_download_window_destroy(window); return false; }
    *out = window; return true;
}
