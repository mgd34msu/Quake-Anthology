#ifndef QA_AUDIO_BANK_PRIVATE_H
#define QA_AUDIO_BANK_PRIVATE_H
#include "qa/audio.h"

struct qa_audio_asset {
    atomic_uint references;
    qa_audio_sample *sample;
    qa_resource *resource;
    qa_vfs *files;
    uint64_t resource_id;
    qa_mount_id mount;
    qa_game_family family;
    qa_audio_wav_policy policy;
    char name[];
};

typedef struct bank_entry {
    qa_audio_asset *asset;
    uint64_t touched;
} bank_entry;

struct qa_audio_bank {
    qa_vfs *view;
    bank_entry *entries;
    size_t count, capacity;
    size_t *names;
    size_t name_capacity;
    uint64_t registration;
    uint64_t lookup_generation;
};
void qa_audio_bank_sync(qa_audio_bank *);

#endif
