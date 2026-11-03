// Host resources captured by a command list belong to that list, not its submission.
// A loader can record an upload, finalize its texture, then submit the upload later.
// Queue ordering alone does not keep that texture alive until the upload is translated.
#include <pthread.h>
#include <stdlib.h>

#include "nvn.h"

struct NvnCommands {
    unsigned refs;
    uint64_t handle;
    struct NvnCommands *next;
    NvnTexture **textures;
    size_t ntextures, cap_textures;
    struct NvnCommands **calls;
    size_t ncalls, cap_calls;
};

enum { COMMAND_BUCKETS = 1024 };
static NvnCommands *handles[COMMAND_BUCKETS];
static pthread_mutex_t handles_lock = PTHREAD_MUTEX_INITIALIZER;

static NvnCommands **handle_slot(uint64_t handle) {
    NvnCommands **slot = &handles[(handle >> 3) % COMMAND_BUCKETS];
    while (*slot && (*slot)->handle != handle) slot = &(*slot)->next;
    return slot;
}

NvnCommands *nvn_commands_begin(void) {
    NvnCommands *commands = calloc(1, sizeof *commands);
    commands->refs = 1;
    return commands;
}

// Only the recording thread changes these arrays; published lists are immutable.
void nvn_commands_texture(NvnCommands *commands, NvnTexture *texture) {
    if (!texture) return;
    for (size_t i = 0; i < commands->ntextures; ++i)
        if (commands->textures[i] == texture) return;
    if (commands->ntextures == commands->cap_textures) {
        commands->cap_textures = commands->cap_textures ? commands->cap_textures * 2 : 8;
        commands->textures = realloc(commands->textures, commands->cap_textures * sizeof *commands->textures);
    }
    nvn_texture_retain(texture);
    commands->textures[commands->ntextures++] = texture;
}

NvnCommands *nvn_commands_acquire(uint64_t handle) {
    pthread_mutex_lock(&handles_lock);
    NvnCommands *commands = *handle_slot(handle);
    if (commands) __atomic_add_fetch(&commands->refs, 1, __ATOMIC_RELAXED);
    pthread_mutex_unlock(&handles_lock);
    return commands;
}

void nvn_commands_call(NvnCommands *commands, uint64_t handle) {
    NvnCommands *child = nvn_commands_acquire(handle);
    if (!child) return;
    for (size_t i = 0; i < commands->ncalls; ++i)
        if (commands->calls[i] == child) { nvn_commands_release(child); return; }
    if (commands->ncalls == commands->cap_calls) {
        commands->cap_calls = commands->cap_calls ? commands->cap_calls * 2 : 4;
        commands->calls = realloc(commands->calls, commands->cap_calls * sizeof *commands->calls);
    }
    commands->calls[commands->ncalls++] = child;
}

void nvn_commands_release(NvnCommands *commands) {
    if (!commands || __atomic_sub_fetch(&commands->refs, 1, __ATOMIC_ACQ_REL)) return;
    for (size_t i = 0; i < commands->ntextures; ++i) nvn_texture_release(commands->textures[i]);
    for (size_t i = 0; i < commands->ncalls; ++i) nvn_commands_release(commands->calls[i]);
    free(commands->textures);
    free(commands->calls);
    free(commands);
}

void nvn_commands_end(NvnCommands *commands, uint64_t handle) {
    commands->handle = handle;
    pthread_mutex_lock(&handles_lock);
    NvnCommands **slot = handle_slot(handle), *old = *slot;
    commands->next = old ? old->next : NULL;
    *slot = commands;
    pthread_mutex_unlock(&handles_lock);
    // Reusing command memory also retires a handle the guest did not finalize.
    nvn_commands_release(old);
}

void nvn_commands_finalize(uint64_t handle) {
    pthread_mutex_lock(&handles_lock);
    NvnCommands **slot = handle_slot(handle), *commands = *slot;
    if (commands) *slot = commands->next;
    pthread_mutex_unlock(&handles_lock);
    nvn_commands_release(commands);
}

RT_ON_RESET(command_handles_reset) {
    // Guest threads and queued translations have stopped before reset callbacks.
    for (size_t i = 0; i < COMMAND_BUCKETS; ++i) {
        NvnCommands *commands = handles[i];
        handles[i] = NULL;
        while (commands) {
            NvnCommands *next = commands->next;
            nvn_commands_release(commands);
            commands = next;
        }
    }
}
