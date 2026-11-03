"""Pruning unrelated save hooks must retain SF2's atomic pending-file commit."""
from source_values import read_source
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class SaveJournalTests(unittest.TestCase):
    def test_commit_and_failed_commit_preserve_saved_data(self):
        source = read_source(ROOT / "runtime/services/fs.c")
        harness = '''#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
'''
        for name in ("static void sync_dir_of(", "static int replace_file(", "static bool copy_fd(",
                     "static bool copy_file(", "static bool sync_save_file(", "static bool commit_slot(",
                     "static void drop_slot("):
            start = source.index(name)
            harness += source[start:source.index("\n}", start) + 2] + "\n"
        harness += r'''
static void put(const char *p, const char *text) {
    FILE *f = fopen(p, "w"); assert(f); fputs(text, f); assert(!fclose(f));
}
static void expect(const char *text) {
    char buf[64] = {0}; FILE *f = fopen("save", "r"); assert(f);
    fread(buf, 1, sizeof buf - 1, f); fclose(f); assert(!strcmp(buf, text));
}
int main(void) {
    put("save", "old progress");
    assert(copy_file("save", "save.pending"));
    put("save.pending", "new progress"); expect("old progress");
    assert(commit_slot("save")); expect("new progress");
    assert(access("save.pending", F_OK));
    assert(!commit_slot("save")); expect("new progress");
    put("save.pending", "incomplete"); drop_slot("save");
    expect("new progress"); assert(access("save.pending", F_OK));
    assert(!copy_file("missing", "save.pending")); expect("new progress");
}
'''
        with tempfile.TemporaryDirectory(prefix="sf2-save-") as directory:
            path = pathlib.Path(directory)
            (path / "test.c").write_text(harness)
            subprocess.run(["clang", "-O2", str(path / "test.c"), "-o", str(path / "test")], check=True)
            subprocess.run([str(path / "test")], cwd=path, check=True)


# Exercise the whole save filesystem and file service handlers with synthetic IPC
# messages and generated fixture text, never user/game save files.
import os
from test_settings import compile_harness


class SaveFilesystemTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.workspace = tempfile.TemporaryDirectory(prefix="sf2-save-service-harness-")
        cls.binary = pathlib.Path(cls.workspace.name) / "test"
        source = cls.binary.with_suffix(".c")
        source.write_text(r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "settings.h"
#include "services/fs.c"

static int notices;
void plat_notice(const char *text) { assert(text && *text); ++notices; }
Service *service_new(const char *name, ServiceHandler handler, void *state) {
    Service *s = calloc(1, sizeof *s); assert(s);
    s->name = name; s->handler = handler; s->state = state; s->refs = 1;
    return s;
}
void ipc_return_object(IpcResp *resp, Service *s) { resp->objects[resp->nobjects++] = s; }

static IpcResp rpc(Service *s, uint32_t cmd, const char *path, int64_t arg) {
    int64_t payload[4] = {arg, 0, 0, 0};
    IpcReq req = {.cmd = cmd, .data = (const uint8_t *)payload, .data_size = sizeof payload};
    if (path) { req.nx = 1; req.x[0] = (IpcBuf){(uintptr_t)path, strlen(path) + 1}; }
    IpcResp resp = {0};
    s->handler(s, &req, &resp);
    assert(!resp.result);
    return resp;
}
static Service *open_save(Service *mount, int mode) {
    IpcResp resp = rpc(mount, 8, "/userdata/progress.xml", mode);
    assert(resp.nobjects == 1);
    return resp.objects[0];
}
static void close_service(Service *s) {
    if (s->on_close) s->on_close(s);
    free(s);
}
static void write_save(Service *f, const char *text) {
    int64_t payload[3] = {0, 0, (int64_t)strlen(text)};
    IpcReq req = {.cmd = 1, .data = (const uint8_t *)payload, .data_size = sizeof payload,
                  .na = 1, .a = {{(uintptr_t)text, strlen(text)}}};
    IpcResp resp = {0};
    f->handler(f, &req, &resp);
    assert(!resp.result);
}
static void expect_save(Service *mount, const char *expected) {
    Service *f = open_save(mount, 1);
    char text[128] = {0};
    int64_t payload[3] = {0, 0, sizeof text - 1};
    IpcReq req = {.cmd = 0, .data = (const uint8_t *)payload, .data_size = sizeof payload,
                  .nb = 1, .b = {{(uintptr_t)text, sizeof text - 1}}};
    IpcResp resp = {0};
    f->handler(f, &req, &resp);
    assert(!resp.result && *(int64_t *)resp.data == (int64_t)strlen(expected));
    assert(!strcmp(text, expected));
    close_service(f);
}
static void expect_committed(HostFs *fs, const char *expected) {
    char path[1200], text[128] = {0};
    snprintf(path, sizeof path, "%s/userdata/progress.xml", fs->root);
    FILE *f = fopen(path, "r"); assert(f);
    assert(fread(text, 1, sizeof text - 1, f) == strlen(expected));
    fclose(f); assert(!strcmp(text, expected));
}
int main(int argc, char **argv) {
    assert(argc == 3);
    assert(settings_load_file(argv[1]));
    settings_init();
    Service *mount = open_host_fs("save_0");
    if (!strcmp(argv[2], "missing_profile")) { assert(!mount && notices == 1); return 0; }
    assert(mount);
    HostFs *fs = mount->state;
    printf("save_root=%s\n", fs->root);
    if (!strcmp(argv[2], "seed")) {
        char userdata[1200]; snprintf(userdata, sizeof userdata, "%s/userdata", fs->root);
        if (access(userdata, F_OK)) rpc(mount, 2, "/userdata", 0);
        rpc(mount, 0, "/userdata/progress.xml", 0);
        Service *file = open_save(mount, 2);
        write_save(file, "committed progress");
        rpc(file, 2, NULL, 0);  // Flush staged bytes
        close_service(file);
        expect_save(mount, "committed progress"); // reads pending copy before Commit
        rpc(mount, 10, NULL, 0);
        expect_committed(fs, "committed progress");
    } else if (!strcmp(argv[2], "pending") || !strcmp(argv[2], "unmount")) {
        Service *file = open_save(mount, 2);
        write_save(file, "uncommitted progress");
        close_service(file);
        expect_save(mount, "uncommitted progress");
        expect_committed(fs, "committed progress");
        if (!strcmp(argv[2], "pending")) return 0; // process exits without Commit/unmount
        close_service(mount);
        mount = open_host_fs("save_0"); assert(mount);
        expect_save(mount, "committed progress");
    } else if (!strcmp(argv[2], "failed")) {
        Service *file = open_save(mount, 2);
        write_save(file, "damaged attempt");
        HostFile *host = file->state;
        assert(!close(host->fd)); host->fd = -1; // real EBADF write failure
        write_save(file, "must not become committed");
        assert(fs->failed[host->slot]);
        close_service(file);
        rpc(mount, 10, NULL, 0);
        expect_save(mount, "committed progress");
        expect_committed(fs, "committed progress");
        assert(notices == 1);
    } else {
        assert(!strcmp(argv[2], "verify"));
        expect_save(mount, "committed progress");
        expect_committed(fs, "committed progress");
    }
    close_service(mount);
}
''')
        compile_harness(source, cls.binary)

    @classmethod
    def tearDownClass(cls):
        cls.workspace.cleanup()

    def invoke(self, folder, config, mode, save_override=None):
        env = os.environ.copy()
        for key in ("LARECOMP_SAVE_DIR", "LARECOMP_LANG", "LARECOMP_VOLUME", "LARECOMP_REVERB"):
            env.pop(key, None)
        env["LARECOMP_LOG_FILES"] = "1"
        if save_override is not None:
            env["LARECOMP_SAVE_DIR"] = str(save_override)
        result = subprocess.run([str(self.binary), str(config), mode], cwd=folder, env=env,
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def test_committed_progress_survives_processes_and_failed_write(self):
        with tempfile.TemporaryDirectory(prefix="sf2-save-persist-") as directory:
            folder = pathlib.Path(directory)
            config = folder / "sf2recomp.ini"
            config.write_text('[storage]\nsave_directory = "custom saves"\n')
            save = folder / "custom saves/save_0/userdata/progress.xml"
            self.invoke(folder, config, "seed")
            self.assertEqual(save.read_text(), "committed progress")
            result = self.invoke(folder, config, "verify")
            self.assertIn("custom saves/save_0/userdata/progress.xml", result.stderr)
            self.assertIn("-> 18 bytes", result.stderr)
            self.invoke(folder, config, "failed")
            self.invoke(folder, config, "verify")
            self.assertEqual(save.read_text(), "committed progress")
            self.assertFalse(save.with_name(save.name + ".pending").exists())

    def test_uncommitted_work_is_visible_then_dropped_at_next_mount(self):
        with tempfile.TemporaryDirectory(prefix="sf2-save-uncommitted-") as directory:
            folder = pathlib.Path(directory)
            config = folder / "sf2recomp.ini"
            config.write_text('[storage]\nsave_directory = "my saves"\n')
            save = folder / "my saves/save_0/userdata/progress.xml"
            pending = save.with_name(save.name + ".pending")
            self.invoke(folder, config, "seed")
            outside = folder / "outside selected save mount"
            outside.mkdir()
            protected = outside / "do-not-delete.pending"
            protected.write_text("unrelated file")
            (save.parents[1] / "external").symlink_to(outside, target_is_directory=True)
            self.invoke(folder, config, "pending")
            self.assertEqual(save.read_text(), "committed progress")
            self.assertEqual(pending.read_text(), "uncommitted progress")
            self.invoke(folder, config, "verify")
            self.assertFalse(pending.exists())
            self.invoke(folder, config, "unmount")
            self.assertEqual(save.read_text(), "committed progress")
            self.assertFalse(pending.exists())
            self.assertEqual(protected.read_text(), "unrelated file")

    def test_absolute_ini_and_environment_override_select_save_folders(self):
        with tempfile.TemporaryDirectory(prefix="sf2-save-location-") as directory:
            folder = pathlib.Path(directory)
            config = folder / "sf2recomp.ini"
            ini_root, override = folder / "absolute configured saves", folder / "session override"
            config.write_text('[storage]\nsave_directory = "' + str(ini_root) + '"\n')
            self.invoke(folder, config, "seed")
            self.invoke(folder, config, "verify")
            self.assertEqual((ini_root / "save_0/userdata/progress.xml").read_text(), "committed progress")
            self.invoke(folder, config, "seed", override)
            self.invoke(folder, config, "verify", override)
            self.assertEqual((override / "save_0/userdata/progress.xml").read_text(), "committed progress")
            self.assertFalse((folder / "save").exists())

    def test_completed_profile_seeds_once_and_switches_without_overwriting(self):
        with tempfile.TemporaryDirectory(prefix="sf2-completed-profile-") as directory:
            folder = pathlib.Path(directory)
            config = folder / "sf2recomp.ini"
            config.write_text('[storage]\nsave_directory = "custom saves"\nuse_completed_save = false\n')
            self.invoke(folder, config, "seed")
            normal = folder / "custom saves/save_0/userdata/progress.xml"
            template = folder / "profiles/completed/save_0/userdata"
            template.mkdir(parents=True)
            (template / "users.xml").write_text("completed campaign snapshot")
            (template / "initSettings.bin").write_bytes(b"preferences")
            config.write_text('[storage]\nsave_directory = "custom saves"\nuse_completed_save = true\n')
            result = self.invoke(folder, config, "seed")
            self.assertIn("custom saves/completed/save_0", result.stdout)
            completed = folder / "custom saves/completed/save_0/userdata"
            self.assertEqual((completed / "users.xml").read_text(), "completed campaign snapshot")
            self.assertEqual((completed / "initSettings.bin").read_bytes(), b"preferences")
            self.assertEqual(normal.read_text(), "committed progress")
            (completed / "users.xml").write_text("changed equipment in completed profile")
            (template / "users.xml").write_text("updated package snapshot")
            self.invoke(folder, config, "verify")
            self.assertEqual((completed / "users.xml").read_text(), "changed equipment in completed profile")
            config.write_text('[storage]\nsave_directory = "custom saves"\nuse_completed_save = false\n')
            result = self.invoke(folder, config, "verify")
            self.assertIn("save_root=custom saves/save_0", result.stdout)
            self.assertEqual(normal.read_text(), "committed progress")

    def test_completed_profile_recovers_interrupted_seed_and_reports_missing_template(self):
        with tempfile.TemporaryDirectory(prefix="sf2-completed-retry-") as directory:
            folder = pathlib.Path(directory)
            config = folder / "sf2recomp.ini"
            config.write_text('[storage]\nuse_completed_save = true\n')
            self.invoke(folder, config, "missing_profile")
            self.assertFalse((folder / "save/save_0").exists())
            template = folder / "profiles/completed/save_0/userdata"
            template.mkdir(parents=True)
            (template / "users.xml").write_text("complete")
            (template / "initSettings.bin").write_bytes(b"settings")
            target = folder / "save/completed/save_0/userdata"
            target.mkdir(parents=True, exist_ok=True)
            (target / "users.xml.seed").write_text("interrupted copy")
            self.invoke(folder, config, "seed")
            self.assertEqual((target / "users.xml").read_text(), "complete")
            self.assertFalse((target / "users.xml.seed").exists())
