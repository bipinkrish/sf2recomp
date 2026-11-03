#include "../constants/platform.h"
// macOS / Linux session log (included by macos.c and linux.c): lasrecomp.log in the data folder is always the latest
// run; a new start moves the previous one aside as lasrecomp-backup-<YYYYMMDD-HHMMSS>-<pid>.log (its last write, the
// pid from its first line, 0 if unknown) and keeps the five newest of those, as on Windows. Moving instead of
// truncating also means a copy still running or quitting keeps writing to its own (moved) file. Processes started by
// this one (the start screen re-executing itself for the game) get LASRECOMP_LOG_APPEND and append to the same log.
#pragma once
#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define SESSION_LOG_BACKUPS SF2C_POSIX_LOG_SESSION_LOG_BACKUPS
static void session_log_backup(const char *dir, const char *name) {
    char from[SF2C_POSIX_LOG_N_1100_AB98], to[SF2C_POSIX_LOG_N_1200_1519];
    snprintf(from, sizeof from, SF2C_POSIX_LOG_S_S_S_22D8, dir, name);
    struct stat st;
    if (lstat(from, &st) != 0 || !S_ISREG(st.st_mode)) return;
    int pid = 0;
    FILE *f = fopen(from, SF2C_POSIX_LOG_S_R_2659);
    if (f) {
        char line[SF2C_POSIX_LOG_N_128_2747];
        if (!fgets(line, sizeof line, f) || sscanf(line, SF2C_POSIX_LOG_S_LOG_PID_D_22C1, &pid) != 1 || pid < 0) pid = 0;
        fclose(f);
    }
    char when[SF2C_POSIX_LOG_N_32_E29C];
    strftime(when, sizeof when, SF2C_POSIX_LOG_S_Y_M_D_H_M_S_84CE, localtime(&st.st_mtime));
    snprintf(to, sizeof to, SF2C_POSIX_LOG_S_S_LASRECOMP_BACKUP_S_D_LOG_1DBE, dir, when, pid);
    for (int i = 1; access(to, F_OK) == 0 && i < SF2C_POSIX_LOG_N_100_AD57; i++)  // (never replace another backup)
        snprintf(to, sizeof to, SF2C_POSIX_LOG_S_S_LASRECOMP_BACKUP_S_D_D_LOG_32BC, dir, when, pid, i);
    rename(from, to);
}

typedef struct { char name[SF2C_POSIX_LOG_N_256_51E8]; time_t mtime; } SessionLogBackup;
static int session_log_newest(const void *a, const void *b) {
    const SessionLogBackup *x = a, *y = b;
    if (x->mtime != y->mtime) return x->mtime < y->mtime ? 1 : -1;
    return strcmp(y->name, x->name);
}

static void session_log_prune(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return;
    SessionLogBackup *logs = NULL;
    size_t n = 0, cap = 0;
    char path[SF2C_POSIX_LOG_N_1400_55FD];
    for (struct dirent *e; (e = readdir(d));) {
        size_t len = strlen(e->d_name);
        if (strncmp(e->d_name, SF2C_POSIX_LOG_S_LASRECOMP_BACKUP_9CFD, SF2C_POSIX_LOG_N_17_4523) || len < SF2C_POSIX_LOG_N_22_785F || len >= sizeof logs->name ||
            strcmp(e->d_name + len - SF2C_POSIX_LOG_N_4_4B22, SF2C_POSIX_LOG_S_LOG_0386)) continue;
        struct stat st;
        snprintf(path, sizeof path, SF2C_POSIX_LOG_S_S_S_22D8, dir, e->d_name);
        if (lstat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        if (n == cap) {
            SessionLogBackup *more = realloc(logs, (cap = cap ? cap * SF2C_POSIX_LOG_N_2_D473 : SF2C_POSIX_LOG_N_16_B17E) * sizeof *logs);
            if (!more) break;
            logs = more;
        }
        snprintf(logs[n].name, sizeof logs[n].name, SF2C_POSIX_LOG_S_S_0030, e->d_name);
        logs[n++].mtime = st.st_mtime;
    }
    closedir(d);
    if (n > 1) qsort(logs, n, sizeof *logs, session_log_newest);
    for (size_t i = SESSION_LOG_BACKUPS; i < n; i++) {
        snprintf(path, sizeof path, SF2C_POSIX_LOG_S_S_S_22D8, dir, logs[i].name);
        unlink(path);
    }
    free(logs);
}

// stdout / stderr -> <dir>/lasrecomp.log (path_out: its path, emptied on failure).
static void session_log_open(const char *dir, char *path_out, size_t n) {
    snprintf(path_out, n, SF2C_POSIX_LOG_S_S_LASRECOMP_LOG_22EC, dir);
    bool append = getenv(SF2C_POSIX_LOG_S_LASRECOMP_LOG_APPEND_A5DB) != NULL;
    if (!append) {
        session_log_backup(dir, SF2C_POSIX_LOG_S_LASRECOMP_PREV_LOG_2ADB);  // (the earlier macOS scheme's one backup)
        session_log_backup(dir, SF2C_POSIX_LOG_S_LASRECOMP_LOG_B1B2);
        session_log_prune(dir);
    }
    if (!freopen(path_out, SF2C_POSIX_LOG_S_A_AC8D, stderr)) { path_out[0] = 0; return; }
    setvbuf(stderr, NULL, _IONBF, 0);
    dup2(STDERR_FILENO, STDOUT_FILENO);
    setenv(SF2C_POSIX_LOG_S_LASRECOMP_LOG_APPEND_A5DB, SF2C_POSIX_LOG_S_1_3915, 1);
    // (a first line: an empty log then means writing failed - e.g. a full disk - not another start; its pid names
    // the backup later)
    time_t now = time(NULL);
    char when[SF2C_POSIX_LOG_N_64_A68B];
    strftime(when, sizeof when, SF2C_POSIX_LOG_S_Y_M_D_H_M_S_648A, localtime(&now));
    fprintf(stderr, SF2C_POSIX_LOG_S_LOG_S_PID_D_S_N_2472, when, (int)getpid(), append ? SF2C_POSIX_LOG_S_RESTARTED_28E2 : SF2C_POSIX_LOG_S_EMPTY_12AE);
}

// The open-file limit: a macOS app starts with 256 (Linux usually 1024). Raised toward the hard limit (at most
// 10240, macOS's OPEN_MAX) so a long session never runs out - running out fails saves ("could not be saved") and
// game file reads. Called first thing (plat_init), after the log is set up.
#include <errno.h>
#include <sys/resource.h>
static void raise_open_file_limit(void) {
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl)) {
        fprintf(stderr, SF2C_POSIX_LOG_S_FD_OPEN_FILE_LIMIT_UNKNOWN_S_B1B3, strerror(errno));
        return;
    }
    rlim_t before = rl.rlim_cur, want = SF2C_POSIX_LOG_N_10240_205E;
    if (rl.rlim_max != RLIM_INFINITY && rl.rlim_max < want) want = rl.rlim_max;
    if (before != RLIM_INFINITY && before < want) {
        rl.rlim_cur = want;
        if (setrlimit(RLIMIT_NOFILE, &rl)) {
            fprintf(stderr, SF2C_POSIX_LOG_S_FD_OPEN_FILE_LIMIT_LLU_COULD_777A, (unsigned long long)before,
                    (unsigned long long)want, strerror(errno));
            return;
        }
    }
    getrlimit(RLIMIT_NOFILE, &rl);
    char hard[SF2C_POSIX_LOG_N_32_E29C] = SF2C_POSIX_LOG_S_UNLIMITED_EA9C;
    if (rl.rlim_max != RLIM_INFINITY) snprintf(hard, sizeof hard, SF2C_POSIX_LOG_S_LLU_0A0E, (unsigned long long)rl.rlim_max);
    fprintf(stderr, SF2C_POSIX_LOG_S_FD_OPEN_FILE_LIMIT_LLU_WAS_L_387F, (unsigned long long)rl.rlim_cur,
            (unsigned long long)before, hard);
}
