// fsp-srv: RomFS as a raw storage image, save data and SD card backed by host directories.
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#undef IN   // (windows.h's empty parameter markers; ipc.h defines its own)
#undef OUT
#endif

#include <pthread.h>

#include "../hle.h"
#include "../ipc.h"
#include "../romfs_build.h"
#include "../settings.h"

#define FS_PATH_NOT_FOUND MAKE_RESULT(2, 1)
#define FS_PATH_EXISTS MAKE_RESULT(2, 2)
#define FS_TARGET_LOCKED MAKE_RESULT(2, 7)
#define FS_NOT_EMPTY MAKE_RESULT(2, 8)

const char *g_save_dir = g_settings.save_directory;

// ------------------------------------------------------------------ the base RomFS image
// The game reads its data as one raw RomFS image. It's served from the player's romfs/ folder: the header and tables
// laid out in memory (runtime/romfs_build.c), each file's bytes read from its own file, the gaps between files zeros -
// no image file on disk, so the game data isn't kept twice. A romfs.bin from an older version is used only when the
// folder isn't there.
static RomfsLayout g_base;
static bool g_base_open;
static int g_base_fd = -1;  // (an older version's romfs.bin)
static pthread_mutex_t g_base_lock = PTHREAD_MUTEX_INITIALIZER;
#define BASE_FDS 32  // files kept open (macOS allows 256 open files by default)
static struct { int file, fd; uint64_t used; } g_base_fds[BASE_FDS];
static uint64_t g_base_tick;

static int base_file_fd(int i) {  // (g_base_lock held)
    int slot = 0;
    for (int k = 0; k < BASE_FDS; k++) {
        if (g_base_fds[k].fd > 0 && g_base_fds[k].file == i) { g_base_fds[k].used = ++g_base_tick; return g_base_fds[k].fd; }
        if (g_base_fds[k].used < g_base_fds[slot].used) slot = k;
    }
    if (g_base_fds[slot].fd > 0) close(g_base_fds[slot].fd);
    int fd = open(g_base.files[i].path, O_RDONLY);
    g_base_fds[slot].file = i, g_base_fds[slot].fd = fd > 0 ? fd : 0, g_base_fds[slot].used = ++g_base_tick;
    return fd;
}

static uint64_t base_size(void) {
    if (g_base_fd >= 0) {
        struct stat sb;
        fstat(g_base_fd, &sb);
        return (uint64_t)sb.st_size;
    }
    return g_base.size;
}

// n bytes of the base image at off (past its end: zeros)
static ssize_t base_pread(void *dst_, uint64_t n, uint64_t off) {
    if (g_base_fd >= 0) return pread(g_base_fd, dst_, n, (off_t)off);
    uint8_t *dst = dst_;
    memset(dst, 0, n);
    uint64_t end = off + n;
    if (off < ROMFS_DATA_START) memcpy(dst, g_base.header + off, (end < ROMFS_DATA_START ? end : ROMFS_DATA_START) - off);
    uint64_t ta = g_base.tables_off, tb = ta + g_base.tables_size;
    if (off < tb && end > ta) {
        uint64_t a = off > ta ? off : ta, b = end < tb ? end : tb;
        memcpy(dst + (a - off), g_base.tables + (a - ta), b - a);
    }
    // the files overlapping [off, end): the first by binary search, then in order
    int lo = 0, hi = g_base.nfiles - 1, first = g_base.nfiles;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (g_base.files[mid].start + g_base.files[mid].size > off) first = mid, hi = mid - 1;
        else lo = mid + 1;
    }
    pthread_mutex_lock(&g_base_lock);
    for (int i = first; i < g_base.nfiles && g_base.files[i].start < end; i++) {
        const RomfsLayoutFile *f = &g_base.files[i];
        uint64_t a = off > f->start ? off : f->start, b = end < f->start + f->size ? end : f->start + f->size;
        if (a >= b) continue;
        int fd = base_file_fd(i);
        if (fd <= 0 || pread(fd, dst + (a - off), b - a, (off_t)(a - f->start)) != (ssize_t)(b - a)) {
            pthread_mutex_unlock(&g_base_lock);
            LOG("[fs] can't read %s\n", f->path);
            return -1;
        }
    }
    pthread_mutex_unlock(&g_base_lock);
    return (ssize_t)n;
}

static void base_close(void) {
    for (int k = 0; k < BASE_FDS; k++) if (g_base_fds[k].fd > 0) close(g_base_fds[k].fd);
    memset(g_base_fds, 0, sizeof g_base_fds);
    romfs_layout_free(&g_base);
    if (g_base_fd >= 0) close(g_base_fd);
    g_base_fd = -1;
    g_base_open = false;
}

static void base_open(void) {
    if (g_base_open) return;
    char path[1100], why[256] = "";
    if (romfs_layout(g_romfs_dir, &g_base, why, sizeof why)) {
        LOG("[fs] romfs: %d files served from %s\n", g_base.nfiles, g_romfs_dir);
    } else {
        snprintf(path, sizeof path, "%s.bin", g_romfs_dir);  // (an older version's image)
        g_base_fd = open(path, O_RDONLY);
        if (g_base_fd < 0) {
            snprintf(path, sizeof path, "%s.img", g_romfs_dir);  // (or romfs.img)
            g_base_fd = open(path, O_RDONLY);
        }
        if (g_base_fd >= 0) {
            LOG("[fs] romfs: image served from %s\n", path);
        } else {
            FATAL("cannot read the game data in %s %s(run the local conversion tools first)\n",
                  g_romfs_dir, why);
        }
    }
    g_base_open = true;
}

// ------------------------------------------------------------------ RomFS file index (offset -> path)
// The index is diagnostic only; the guest receives the original RomFS layout.
typedef struct { uint64_t off, size; char *path; } RomFile;
static RomFile *rom_files;
static int rom_nfiles;
static uint64_t rom_data_off;

static void mounts_reset(void);
RT_ON_RESET(fs_reset) {
    for (int i = 0; i < rom_nfiles; i++) free(rom_files[i].path);
    free(rom_files);
    rom_files = NULL;
    rom_nfiles = 0;
    base_close();
    mounts_reset();
    g_save_dir = g_settings.save_directory;
}

static int rom_cmp(const void *a, const void *b) {
    const RomFile *x = a, *y = b;
    return x->off < y->off ? -1 : x->off > y->off;
}

static void rom_dir_path(const uint8_t *dmeta, uint32_t dsize, uint32_t dir, char *out, size_t cap) {
    char parts[16][256];
    int n = 0;
    while (dir != 0 && dir < dsize && n < 16) {  // root (offset 0) has an empty name
        uint32_t parent, namelen;
        memcpy(&parent, dmeta + dir, 4);
        memcpy(&namelen, dmeta + dir + 0x14, 4);
        if (namelen > 255) namelen = 255;
        memcpy(parts[n], dmeta + dir + 0x18, namelen);
        parts[n][namelen] = 0;
        n++;
        dir = parent;
    }
    out[0] = 0;
    for (int i = n - 1; i >= 0; i--) { strlcat(out, parts[i], cap); strlcat(out, "/", cap); }
}

static void rom_index(void) {
    uint64_t h[10];
    if (base_pread(h, sizeof h, 0) != (ssize_t)sizeof h) return;
    // header: size, dir hash (off,size), dir meta (off,size), file hash (off,size), file meta (off,size), data off
    uint64_t doff = h[3], dsz = h[4], foff = h[7], fsz = h[8];
    rom_data_off = h[9];
    uint8_t *dmeta = malloc(dsz), *fmeta = malloc(fsz);
    if (base_pread(dmeta, dsz, doff) != (ssize_t)dsz || base_pread(fmeta, fsz, foff) != (ssize_t)fsz) return;
    int cap = 4096;
    rom_files = malloc(sizeof(RomFile) * cap);
    for (uint64_t o = 0; o + 0x20 <= fsz;) {
        uint32_t parent, namelen;
        uint64_t data, size;
        memcpy(&parent, fmeta + o, 4);
        memcpy(&data, fmeta + o + 8, 8);
        memcpy(&size, fmeta + o + 0x10, 8);
        memcpy(&namelen, fmeta + o + 0x1C, 4);
        char dir[1024], name[256];
        rom_dir_path(dmeta, (uint32_t)dsz, parent, dir, sizeof dir);
        uint32_t nl = namelen > 255 ? 255 : namelen;
        memcpy(name, fmeta + o + 0x20, nl);
        name[nl] = 0;
        if (rom_nfiles == cap) { cap *= 2; rom_files = realloc(rom_files, sizeof(RomFile) * cap); }
        char *path = malloc(strlen(dir) + nl + 1);
        strcpy(path, dir);
        strcat(path, name);
        rom_files[rom_nfiles++] = (RomFile){rom_data_off + data, size, path};
        o += 0x20 + ((namelen + 3) & ~3u);
    }
    rt_romfs_files = rom_nfiles;
    qsort(rom_files, rom_nfiles, sizeof *rom_files, rom_cmp);
    free(dmeta);
    free(fmeta);
    LOG("[fs] romfs index: %d files\n", rom_nfiles);
}

// Path of the RomFS file containing image offset `off` (NULL if none).
const char *romfs_file_at(uint64_t off) {
    int lo = 0, hi = rom_nfiles - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (off < rom_files[mid].off) hi = mid - 1;
        else if (off >= rom_files[mid].off + rom_files[mid].size) lo = mid + 1;
        else return rom_files[mid].path;
    }
    return NULL;
}

// ------------------------------------------------------------------ IStorage (raw RomFS image)
typedef struct { uint64_t size; } Storage;

static void storage(Service *s, IpcReq *req, IpcResp *resp) {
    Storage *st = s->state;
    switch (req->cmd) {
    case 0: {  // Read(offset, size) -> B buffer
        uint64_t off = IN(uint64_t, 0), size = IN(uint64_t, 8);
        IpcBuf *b = out_buf(req, 0);
        if (!b) { resp->result = MAKE_RESULT(2, 6001); break; }
        if (size > b->size) size = b->size;
        ssize_t n = base_pread((uint8_t *)(uintptr_t)b->addr, size, off);
        if (n < 0) LOG("[fs] storage read failed at %llx: %s\n", off, strerror(errno));
        {
            const char *path = romfs_file_at(off);
            static const char *last;
            if (path && path != last) {
                last = path;
                if (getenv("LARECOMP_LOG_FILES")) LOG("[fs] read %s\n", path);
            }
        }
        static uint64_t reads, bytes;
        reads++;
        bytes += size;
        if ((reads & (reads - 1)) == 0) LOG("[fs] romfs reads: %llu (%llu KB)\n", reads, bytes >> 10);
        break;
    }
    case 4: OUT(uint64_t, 0) = st->size; resp_size(resp, 8); break;  // GetSize
    default: LOG("[fs] IStorage cmd %u (ok)\n", req->cmd);
    }
}

static Service *open_romfs_storage(void) {
    if (!g_base_open) {
        base_open();
        rom_index();
    }
    Storage *st = calloc(1, sizeof *st);
    st->size = base_size();
    return service_new("IStorage(romfs)", storage, st);
}

// ------------------------------------------------------------------ host-directory filesystem
// Save data is all-or-nothing, as on the Switch (its save storage commits a mount's changes at once): a file opened
// for writing on a save mount works on a copy, <file>.pending, which the mount's Commit syncs and renames over the
// file - so a crash or power loss mid-save leaves the last committed save, never a half-written one (the save
// backups PR #55 was closed in favour of this). Reads see the pending copy, as the Switch's do before a commit. A
// copy left by a crash is dropped at the next mount (uncommitted, as on the Switch). The SD card mount writes in place.
// A save that can't be written (save folder not writable, disk full, ...) is dropped whole and the committed save
// stays as it was, but the game is never told: its SDK aborts on any failed OpenFile / Write / Flush / Commit (the
// crash dialog), and it has no "could not save" path anyway. The player gets one warning (save_failed) instead.
#define PENDING_MAX 16
typedef struct {
    char root[1024];
    bool journaled;
    int npending;
    char pending[PENDING_MAX][1100];
    bool failed[PENDING_MAX];  // a write to this working copy failed: dropped at the Commit, never published
    bool live;                 // a journaled mount the game hasn't closed yet (g_mount_lock)
} HostFs;
// The game mounts the save folder for each save. A new mount drops the working copies a crash left behind - but
// never while another mount of the same folder is still open: those could be its copies, mid-save.
static pthread_mutex_t g_mount_lock = PTHREAD_MUTEX_INITIALIZER;
#define LIVE_MOUNTS_MAX 16
static HostFs *g_live_mounts[LIVE_MOUNTS_MAX];
static void mounts_reset(void) {
    pthread_mutex_lock(&g_mount_lock);
    memset(g_live_mounts, 0, sizeof g_live_mounts);
    pthread_mutex_unlock(&g_mount_lock);
}
typedef struct {
    int fd;
    char path[1100];
    HostFs *fs;    // a save file opened for writing (journaled mount), else NULL
    int slot;      // its working copy in fs->pending (-1: a scratch copy)
    bool scratch;  // staging failed: the writes go to a temporary file outside the save folder, never committed
    bool dropped;  // no scratch copy either: the writes are dropped (fd: the committed file read-only, or -1)
} HostFile;

// Saving failed: say so loudly in the log, and once per run in a dialog (the game itself still says "Game saved").
static void save_failed(HostFs *fs, const char *what, const char *path, int error) {
    LOG("[save] SAVE FAILED: %s %s (%s) - not saved, the last committed save is kept\n", what, path,
        error ? strerror(error) : "earlier error");
    static bool told;
    if (told) return;
    told = true;
    char text[1400];
    snprintf(text, sizeof text, "Your game could not be saved (the game may still say it was). Your previous save "
             "is unchanged.\n\nMake sure this folder can be written to and the disk has free space:\n%s", fs->root);
    plat_notice(text);
}

static void sync_dir_of(const char *path) {
    char d[1100];
    snprintf(d, sizeof d, "%s", path);
    char *slash = strrchr(d, '/');
    if (!slash) return;
    *slash = 0;
#ifndef _WIN32
    int fd = open(d, O_RDONLY);
    if (fd >= 0) { fsync(fd); close(fd); }
#endif
}

// the working copy over the file in one step (Windows' rename doesn't replace an existing file; with the UTF-8
// code page the A function takes our paths as they are)
static int replace_file(const char *from, const char *to) {
#ifdef _WIN32
    if (MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return 0;
    LOG("[save] replace %s failed (Windows error %lu)\n", to, (unsigned long)GetLastError());
    errno = EIO;
    return -1;
#else
    return rename(from, to);
#endif
}

// in -> out from the start (errno set on failure)
static bool copy_fd(int in, int out) {
    char buf[65536];
    ssize_t n;
    while ((n = read(in, buf, sizeof buf)) > 0)
        for (ssize_t done = 0; done < n;) {
            ssize_t w = write(out, buf + done, (size_t)(n - done));
            if (w < 0 && errno == EINTR) continue;
            if (w <= 0) { if (!w) errno = EIO; return false; }
            done += w;
        }
    return n == 0;
}

static bool copy_file(const char *from, const char *to) {
    int in = open(from, O_RDONLY);
    if (in < 0) return false;
    int out = open(to, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    bool ok = out >= 0 && copy_fd(in, out);
    int error = errno;
    close(in);
    if (out >= 0 && close(out)) { error = errno; ok = false; }
    errno = error;
    return ok;
}

// A copy of the save file outside the save folder, deleted when closed: what the game writes when its working copy
// can't be made. Writing over the committed file instead would put the only copy at risk (a crash, a full disk or a
// failed write mid-save leaves it half-written, and a later load could see partial data).
static int scratch_copy(const char *from) {
    int in = open(from, O_RDONLY);
    if (in < 0) return -1;
#ifdef _WIN32
    char dir[MAX_PATH], name[MAX_PATH];
    int fd = -1;
    if (GetTempPathA(sizeof dir, dir) && GetTempFileNameA(dir, "las", 0, name))
        fd = _open(name, _O_RDWR | _O_BINARY | _O_TEMPORARY);
#else
    char name[1100];
    const char *dir = getenv("TMPDIR");
    snprintf(name, sizeof name, "%s/sf2recomp-save-XXXXXX", dir && *dir ? dir : "/tmp");
    int fd = mkstemp(name);
    if (fd >= 0) unlink(name);
#endif
    if (fd >= 0 && !copy_fd(in, fd)) { close(fd); fd = -1; }
    close(in);
    return fd;
}

// the working copy of a save file (made on its first open for writing): its index in fs->pending; -1 for the SD
// card mount, a file not being saved, or (errno set) when the copy can't be made
static int pending_of(HostFs *fs, const char *p, bool create, char *out, size_t n) {
    if (!fs->journaled) return -1;
    snprintf(out, n, "%s.pending", p);
    for (int i = 0; i < fs->npending; i++) if (!strcmp(fs->pending[i], p)) return i;
    if (!create) return -1;
    if (fs->npending == PENDING_MAX) { errno = ENOSPC; return -1; }
    if (strlen(p) >= sizeof fs->pending[0]) { errno = ENAMETOOLONG; return -1; }
    if (!copy_file(p, out)) { int error = errno; unlink(out); errno = error; return -1; }
    fs->failed[fs->npending] = false;
    snprintf(fs->pending[fs->npending], sizeof fs->pending[0], "%s", p);
    return fs->npending++;
}

static bool sync_save_file(const char *path) {
    int fd = open(path, O_RDWR);
    if (fd < 0) return false;
    bool ok = fsync(fd) == 0;
    int error = errno;
    if (close(fd)) return false;
    if (!ok) errno = error;
    return ok;
}
// SF2 saves use ordinary journal copies; no game-specific flag sidecars.
static bool commit_slot(const char *path) {
    char pending[1200];
    snprintf(pending, sizeof pending, "%s.pending", path);
    if (!sync_save_file(pending) || replace_file(pending, path)) return false;
    sync_dir_of(path);
    return true;
}

static void drop_slot(const char *path) {
    char pending[1200];
    snprintf(pending, sizeof pending, "%s.pending", path);
    unlink(pending);
}

static void commit(HostFs *fs) {
    for (int i = 0; i < fs->npending; i++) {
        const char *path = fs->pending[i];
        if (fs->failed[i]) {
            drop_slot(path);
            save_failed(fs, "dropped the failed save", path, 0);
        } else if (!commit_slot(path)) {
            int error = errno;
            drop_slot(path);
            save_failed(fs, "could not commit", path, error);
        } else LOG("[save] committed %s\n", path);
    }
    fs->npending = 0;
}

typedef struct { DIR *d; char path[1024]; int mode; } HostDir;

// The game closed the mount. (Its HostFs stays allocated: a file opened on it may still point at it.)
static void fs_unmount(Service *s) {
    HostFs *fs = s->state;
    pthread_mutex_lock(&g_mount_lock);
    for (int i = 0; fs && i < LIVE_MOUNTS_MAX; i++) if (g_live_mounts[i] == fs) g_live_mounts[i] = NULL;
    if (fs) fs->live = false;
    pthread_mutex_unlock(&g_mount_lock);
}

// The game closed the file / directory: close the host one too. They used to stay open for the whole run - a leak
// everywhere, and on Windows the open working copy blocked the Commit's replace, so saves never landed (a file can't
// be replaced while it's open there; the game closes its files before it commits, as nn::fs requires).
static void file_close(Service *s) {
    HostFile *f = s->state;
    if (f) { if (f->fd >= 0) close(f->fd); free(f); s->state = NULL; }
}
static void dir_close(Service *s) {
    HostDir *d = s->state;
    if (d) { if (d->d) closedir(d->d); free(d); s->state = NULL; }
}

static const char *in_path(IpcReq *req, int i) {
    if (i < req->nx) return (const char *)(uintptr_t)req->x[i].addr;
    if (i < req->na) return (const char *)(uintptr_t)req->a[i].addr;
    return "";
}

static void host_path(HostFs *fs, const char *p, char *out, size_t n) {
    snprintf(out, n, "%s%s%s", fs->root, p[0] == '/' ? "" : "/", p);
}

static uint32_t errno_result(void) {
    if (errno == ENOENT || errno == ENOTDIR) return FS_PATH_NOT_FOUND;
    if (errno == EEXIST) return FS_PATH_EXISTS;
    if (errno == ENOTEMPTY) return FS_NOT_EMPTY;
    return MAKE_RESULT(2, 4000);
}

// a save file's write / flush / resize failed: that save is dropped at the Commit (the game is told it worked)
static void file_failed(HostFile *f, const char *what, int error) {
    if (f->slot >= 0) f->fs->failed[f->slot] = true;
    save_failed(f->fs, what, f->path, error);
}

static void file(Service *s, IpcReq *req, IpcResp *resp) {
    HostFile *f = s->state;
    switch (req->cmd) {
    case 0: {  // Read(option, offset, size) -> B
        int64_t off = IN(int64_t, 8), size = IN(int64_t, 16);
        IpcBuf *b = out_buf(req, 0);
        if (!b || off < 0 || size < 0) { resp->result = MAKE_RESULT(2, 6001); break; }
        if ((uint64_t)size > b->size) size = (int64_t)b->size;
        if (f->dropped && f->fd < 0) { OUT(int64_t, 0) = 0; resp_size(resp, 8); break; }  // (nothing open: empty)
        ssize_t n = pread(f->fd, (void *)(uintptr_t)b->addr, (size_t)size, off);
        if (n < 0) resp->result = errno_result();
        if (n > 0 && !f->scratch && (getenv("LARECOMP_LOG_FILES") || strstr(f->path, "savedata"))) {
            LOG("[save] read %s off %lld size %lld -> %lld bytes\n", f->path, (long long)off, (long long)size, (long long)n);
        }
        OUT(int64_t, 0) = n < 0 ? 0 : n;
        resp_size(resp, 8);
        break;
    }
    case 1: {  // Write(option, offset, size) <- A
        int64_t off = IN(int64_t, 8), size = IN(int64_t, 16);
        IpcBuf *a = in_buf(req, 0);
        if (!a || off < 0 || size < 0 || (uint64_t)size > a->size) {
            resp->result = MAKE_RESULT(2, 6001); break;
        }
        const char *wpath = strstr(f->path, "savedata");
        if (getenv("LARECOMP_LOG_FILES") || wpath) {
            // (debug) content preview of every write when LARECOMP_LOG_FILES=1: the all-zeros users.xml
            // story needed the payload, not just the address
            if (getenv("LARECOMP_LOG_FILES")) {
                unsigned char pb[16] = {0};
                memcpy(pb, (const void *)(uintptr_t)a->addr, (uint64_t)size < sizeof pb ? (size_t)size : sizeof pb);
                LOG("[save] write %s off %lld size %lld <- guest %llx: %.16s\n", f->path, (long long)off,
                    (long long)size, (unsigned long long)a->addr, (const char *)pb);
            } else {
                LOG("[save] write %s off %lld size %lld <- guest %llx\n", f->path, (long long)off, (long long)size,
                    (unsigned long long)a->addr);
            }
        }
        if (f->dropped) break;  // (reported when it was opened; the game is never told, see save_failed)
        int error = 0;
        for (int64_t done = 0; done < size;) {  // (a short write goes on from where it stopped)
            ssize_t n = pwrite(f->fd, (void *)(uintptr_t)(a->addr + done), (size_t)(size - done), off + done);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) { error = n ? errno : EIO; break; }
            done += n;
        }
        if (error && f->fs) file_failed(f, "write", error);
        else if (error) { errno = error; resp->result = errno_result(); }
        break;
    }
    case 2:  // Flush
        if (f->dropped) break;
        if (fsync(f->fd) && f->fs) file_failed(f, "flush", errno);
        break;
    case 3:  // SetSize
        if (f->dropped) break;
        if (!ftruncate(f->fd, IN(int64_t, 0))) break;
        if (f->fs) file_failed(f, "resize", errno);
        else resp->result = errno_result();
        break;
    case 4: {
        struct stat sb;
        OUT(int64_t, 0) = f->fd >= 0 && !fstat(f->fd, &sb) ? sb.st_size : 0;
        resp_size(resp, 8);
        break;
    }
    default: LOG("[fs] IFile cmd %u (ok)\n", req->cmd);
    }
}

static void directory(Service *s, IpcReq *req, IpcResp *resp) {
    HostDir *d = s->state;
    switch (req->cmd) {
    case 0: {  // Read -> B buffer of 0x310-byte entries
        IpcBuf *b = out_buf(req, 0);
        uint8_t *out = (uint8_t *)(uintptr_t)b->addr;
        int64_t max = (int64_t)(b->size / 0x310), n = 0;
        struct dirent *e;
        while (n < max && (e = readdir(d->d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
#ifdef _WIN32  // no d_type
            char full[2048];
            struct stat es;
            snprintf(full, sizeof full, "%s/%s", d->path, e->d_name);
            bool isdir = !stat(full, &es) && S_ISDIR(es.st_mode);
#else
            bool isdir = e->d_type == DT_DIR;
#endif
            if ((isdir && !(d->mode & 1)) || (!isdir && !(d->mode & 2))) continue;
            uint8_t *ent = out + n * 0x310;
            memset(ent, 0, 0x310);
            strncpy((char *)ent, e->d_name, 0x300);
            ent[0x304] = isdir ? 0 : 1;
            if (!isdir) {
                char p[2048];
                struct stat sb;
                snprintf(p, sizeof p, "%s/%s", d->path, e->d_name);
                if (!stat(p, &sb)) *(int64_t *)(ent + 0x308) = sb.st_size;
            }
            n++;
        }
        OUT(int64_t, 0) = n;
        resp_size(resp, 8);
        break;
    }
    case 1: {  // GetEntryCount
        long pos = telldir(d->d);
        rewinddir(d->d);
        int64_t n = 0;
        struct dirent *e;
        while ((e = readdir(d->d)))
            if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) n++;
        seekdir(d->d, pos);
        OUT(int64_t, 0) = n;
        resp_size(resp, 8);
        break;
    }
    }
}

static int rm_rf(const char *p, bool keep_root) {
    DIR *d = opendir(p);
    if (!d) return unlink(p);
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char c[2048];
        snprintf(c, sizeof c, "%s/%s", p, e->d_name);
        rm_rf(c, false);
    }
    closedir(d);
    return keep_root ? 0 : rmdir(p);
}

static void filesystem(Service *s, IpcReq *req, IpcResp *resp) {
    HostFs *fs = s->state;
    char p[2048], p2[2048];
    host_path(fs, in_path(req, 0), p, sizeof p);
    switch (req->cmd) {
    case 0: {  // CreateFile(option, size)
        int fd = open(p, O_CREAT | O_EXCL | O_RDWR, 0644);
        if (fd < 0) { resp->result = errno_result(); break; }
        if (ftruncate(fd, IN(int64_t, 8))) resp->result = errno_result();
        close(fd);
        break;
    }
    case 1: if (unlink(p)) resp->result = errno_result(); break;
    case 2: if (mkdir(p, 0755)) resp->result = errno_result(); break;
    case 3: if (rmdir(p)) resp->result = errno_result(); break;
    case 4: if (rm_rf(p, false)) resp->result = errno_result(); break;
    case 5: case 6:
        host_path(fs, in_path(req, 1), p2, sizeof p2);
        if (rename(p, p2)) resp->result = errno_result();
        break;
    case 7: {  // GetEntryType
        struct stat sb;
        if (stat(p, &sb)) { resp->result = FS_PATH_NOT_FOUND; break; }
        OUT(uint32_t, 0) = S_ISDIR(sb.st_mode) ? 0 : 1;
        resp_size(resp, 4);
        break;
    }
    case 8: {  // OpenFile(mode)
        uint32_t mode = IN(uint32_t, 0);
        if (getenv("LARECOMP_LOG_FILES")) LOG("[fs] OpenFile %s (mode %u)\n", p, mode);
        bool save = fs->journaled && (mode & 2);
        char work[2100];
        struct stat sb;
        int slot = -1, fd = -1;
        bool scratch = false, dropped = false;
        bool exists = !stat(p, &sb);
        if (exists) slot = pending_of(fs, p, save, work, sizeof work);
        if (slot >= 0) {
            snprintf(p, sizeof p, "%s", work);
        } else if (save && exists) {  // (its working copy can't be made)
            save_failed(fs, "could not stage", p, errno);
            fd = scratch_copy(p);
            scratch = true;
            if (fd < 0) {  // no scratch copy either: the writes are dropped (never sent to a read-only fd)
                save_failed(fs, "could not make a scratch copy of", p, errno);
                dropped = true;
                fd = open(p, O_RDONLY);  // (reads still see the committed file; -1 if even that can't be opened)
            }
        }
        if (fd < 0 && !scratch) fd = open(p, (mode & 2) ? O_RDWR : O_RDONLY);
        if (fd < 0 && !dropped) { resp->result = errno_result(); break; }
        HostFile *f = calloc(1, sizeof *f);
        f->fd = fd;
        snprintf(f->path, sizeof f->path, "%s%s", p, dropped ? " (writes dropped)" : scratch ? " (scratch copy)" : "");
        if (save) { f->fs = fs; f->slot = scratch ? -1 : slot; f->scratch = scratch; f->dropped = dropped; }
        Service *fsvc = service_new("IFile", file, f);
        fsvc->on_close = file_close;
        ipc_return_object(resp, fsvc);
        break;
    }
    case 9: {  // OpenDirectory(mode)
        DIR *d = opendir(p);
        if (!d) { resp->result = errno_result(); break; }
        HostDir *hd = calloc(1, sizeof *hd);
        hd->d = d;
        hd->mode = (int)IN(uint32_t, 0);
        snprintf(hd->path, sizeof hd->path, "%s", p);
        Service *dsvc = service_new("IDirectory", directory, hd);
        dsvc->on_close = dir_close;
        ipc_return_object(resp, dsvc);
        break;
    }
    case 10: commit(fs); break;  // Commit (always succeeds for the game: see save_failed)
    case 11: case 12: OUT(int64_t, 0) = 0x40000000; resp_size(resp, 8); break;
    case 13: rm_rf(p, true); break;
    case 14: memset(resp->data, 0, 0x20); resp_size(resp, 0x20); break;
    default: LOG("[fs] IFileSystem cmd %u (ok)\n", req->cmd);
    }
}

// Working files can live in userdata/ and other subdirectories. Never traverse
// directory symlinks/junctions while removing a previous mount's uncommitted data.
static void drop_uncommitted_tree(const char *root, unsigned depth) {
    if (depth > 64) return;
    DIR *d = opendir(root);
    struct dirent *e;
    while (d && (e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char path[2100];
        if (snprintf(path, sizeof path, "%s/%s", root, e->d_name) >= sizeof path) continue;
#ifdef _WIN32
        DWORD attributes = GetFileAttributesA(path);
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        bool directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
        struct stat st;
        if (lstat(path, &st) || S_ISLNK(st.st_mode)) continue;
        bool directory = S_ISDIR(st.st_mode);
#endif
        if (directory) drop_uncommitted_tree(path, depth + 1);
        else {
            size_t length = strlen(e->d_name);
            if (length > 8 && !strcmp(e->d_name + length - 8, ".pending")) {
                LOG("[save] dropping uncommitted %s\n", path);
                unlink(path);
            }
        }
    }
    if (d) closedir(d);
}

// Seed once from the packaged snapshot. Publish users.xml last, so an
// interrupted first launch can retry without replacing an existing profile.
static bool seed_completed_save(const char *root) {
    char users[1200], directory[1200];
    if (snprintf(users, sizeof users, "%s/userdata/users.xml", root) >= sizeof users ||
        snprintf(directory, sizeof directory, "%s/userdata", root) >= sizeof directory) {
        errno = ENAMETOOLONG; return false;
    }
    if (!access(users, F_OK)) return true;
    if (mkdir(directory, 0755) && errno != EEXIST) return false;
    static const char *const names[] = {"initSettings.bin", "users.xml"};
    for (unsigned i = 0; i < sizeof names / sizeof names[0]; ++i) {
        char source[1200], target[1200], temporary[1300];
        snprintf(source, sizeof source, "profiles/completed/save_0/userdata/%s", names[i]);
        if (snprintf(target, sizeof target, "%s/%s", directory, names[i]) >= sizeof target) {
            errno = ENAMETOOLONG; return false;
        }
        snprintf(temporary, sizeof temporary, "%s.seed", target);
        if (!copy_file(source, temporary) || !sync_save_file(temporary) || replace_file(temporary, target)) {
            int error = errno; unlink(temporary); errno = error; return false;
        }
        sync_dir_of(target);
    }
    LOG("[save] initialized completed campaign profile at %s\n", root);
    return true;
}

static Service *open_host_fs(const char *sub) {
    HostFs *fs = calloc(1, sizeof *fs);
    // Settings are loaded after the data-root chdir; relative paths therefore
    // refer to that writable root. LARECOMP_SAVE_DIR is applied by settings_init.
    g_save_dir = g_settings.save_directory;
    bool completed = g_settings.use_completed_save && strcmp(sub, "sdcard");
    int length = snprintf(fs->root, sizeof fs->root, "%s/%s%s", g_save_dir,
                          completed ? "completed/" : "", sub);
    if (length >= sizeof fs->root) { free(fs); errno = ENAMETOOLONG; return NULL; }
    char cmd[1200];
    snprintf(cmd, sizeof cmd, "%s", fs->root);
    for (char *q = cmd + 1; *q; q++)
        if (*q == '/') { *q = 0; mkdir(cmd, 0755); *q = '/'; }
    mkdir(cmd, 0755);
    fs->journaled = strcmp(sub, "sdcard") != 0;
    pthread_mutex_lock(&g_mount_lock);
    if (completed && !strcmp(sub, "save_0") && !seed_completed_save(fs->root)) {
        int error = errno;
        LOG("[save] cannot initialize completed campaign profile at %s: %s\n", fs->root, strerror(error));
        pthread_mutex_unlock(&g_mount_lock);
        plat_notice("The completed campaign save could not be loaded. Keep profiles/completed with the app, "
                    "or set [storage] use_completed_save = false and restart to load your normal save.");
        free(fs); errno = error; return NULL;
    }
    bool other_live = false;
    for (int i = 0; i < LIVE_MOUNTS_MAX; i++)
        if (g_live_mounts[i] && !strcmp(g_live_mounts[i]->root, fs->root)) other_live = true;
    if (fs->journaled && other_live) LOG("[save] %s is still mounted: its working copies are left alone\n", fs->root);
    if (fs->journaled && !other_live) {  // (a working copy left by a crash: never committed - dropped, as on the Switch)
        drop_uncommitted_tree(fs->root, 0);
    }
    for (int i = 0; fs->journaled && i < LIVE_MOUNTS_MAX; i++)
        if (!g_live_mounts[i]) { g_live_mounts[i] = fs, fs->live = true; break; }
    pthread_mutex_unlock(&g_mount_lock);
    LOG("[fs] mounted host directory %s\n", fs->root);
    Service *svc = service_new("IFileSystem", filesystem, fs);
    svc->on_close = fs_unmount;
    return svc;
}

// ------------------------------------------------------------------ fsp-srv
SERVICE("fsp-srv", fsp_srv) {
    (void)s;
    switch (req->cmd) {
    case 1: break;  // SetCurrentProcess
    case 18: ipc_return_object(resp, open_host_fs("sdcard")); break;
    case 22: break;  // CreateSaveDataFileSystem
    case 51: case 53: {  // OpenSaveDataFileSystem(space, attribute)
        uint8_t type = IN(uint8_t, 0x8 + 0x18);  // attribute.type
        char sub[64];
        snprintf(sub, sizeof sub, "save_%u", type);
        Service *mount = open_host_fs(sub);
        if (mount) ipc_return_object(resp, mount);
        else resp->result = errno_result();
        break;
    }
    case 200: ipc_return_object(resp, open_romfs_storage()); break;  // OpenDataStorageByCurrentProcess
    case 202:  // OpenDataStorageByDataId (system data archives)
        LOG("[fs] OpenDataStorageByDataId %llx (not available)\n", IN(uint64_t, 8));
        resp->result = MAKE_RESULT(2, 1001);
        break;
    case 1005: OUT(uint32_t, 0) = 0; resp_size(resp, 4); break;  // GetGlobalAccessLogMode
    case 1003: case 1004: case 1006: break;
    case 1011: memset(resp->data, 0, 0x10); resp_size(resp, 0x10); break;
    default: LOG("[fs] fsp-srv cmd %u (ok)\n", req->cmd);
    }
}
