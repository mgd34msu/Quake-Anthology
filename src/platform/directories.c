#include "qa/platform_services.h"

#include <SDL.h>
#include <stdlib.h>
#include <string.h>

bool qa_platform_directory_read(qa_platform_directory_kind kind,
    qa_buffer *out, qa_error *error)
{
    char *native;
    switch (kind) {
    case QA_PLATFORM_DIRECTORY_EXECUTABLE:
        native = SDL_GetBasePath();
        break;
    case QA_PLATFORM_DIRECTORY_USER_SETTINGS:
        native = SDL_GetPrefPath("quake-anthology", "content");
        break;
    default:
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Unknown platform directory kind");
        return false;
    }
    if (!native) {
        qa_error_set(error, QA_ERROR_IO, 0, "Reading platform directory: %s", SDL_GetError());
        return false;
    }
    size_t size = strlen(native);
    uint8_t *copy = malloc(size + 1);
    if (!copy) {
        SDL_free(native);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining platform directory");
        return false;
    }
    memcpy(copy, native, size + 1);
    SDL_free(native);
    *out = (qa_buffer){copy, size};
    return true;
}
