"""Portable release boundaries: private inputs, downloads, retries and isolation."""
import hashlib
import ast
import io
import json
import os
import shutil
from pathlib import Path
import sys
import tempfile
import tarfile
import subprocess
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import downloads
import package_release
import release
import setup_android
import setup_ffmpeg
import centralize_literals
import host_tools


class DownloadTests(unittest.TestCase):
    def test_failed_checksum_preserves_previous_file_and_retry_uses_verified_cache(self):
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / 'tool.zip'
            target.write_bytes(b'previous file')
            checksum = hashlib.sha256(b'correct archive').hexdigest()
            with patch.object(downloads.urllib.request, 'urlopen', return_value=io.BytesIO(b'corrupt transfer')):
                with self.assertRaisesRegex(ValueError, 'Checksum mismatch'):
                    downloads.download('https://example.invalid/tool.zip', target, checksum)
            self.assertEqual(target.read_bytes(), b'previous file')
            self.assertFalse(target.with_suffix('.zip.part').exists())
            with patch.object(downloads.urllib.request, 'urlopen', return_value=io.BytesIO(b'correct archive')):
                downloads.download('https://example.invalid/tool.zip', target, checksum)
            with patch.object(downloads.urllib.request, 'urlopen', side_effect=AssertionError('cache should be offline')):
                downloads.download('https://example.invalid/tool.zip', target, checksum)

    def test_archive_traversal_is_rejected_without_publishing_partial_install(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = root / 'tools.zip'
            with zipfile.ZipFile(archive, 'w') as output:
                output.writestr('cmdline-tools/bin/valid', b'first file')
                output.writestr('../escape', b'bad file')
            with self.assertRaisesRegex(ValueError, 'Unsafe'):
                downloads.unpack(archive, root / 'installed', root='cmdline-tools')
            self.assertFalse((root / 'installed').exists())
            self.assertFalse((root / 'escape').exists())
            with zipfile.ZipFile(archive, 'w') as output:
                entry = zipfile.ZipInfo('cmdline-tools/bin/tool')
                entry.external_attr = 0o100755 << 16
                output.writestr(entry, b'complete install')
            downloads.unpack(archive, root / 'installed', root='cmdline-tools')
            self.assertEqual((root / 'installed/bin/tool').read_bytes(), b'complete install')
            if os.name != 'nt':
                self.assertTrue((root / 'installed/bin/tool').stat().st_mode & 0o111)


class ReleaseBoundaryTests(unittest.TestCase):
    @unittest.skipIf(os.name == 'nt', 'exercises POSIX shell forwarding')
    def test_windows_dependency_script_reuses_its_selected_bash(self):
        shell = shutil.which('bash')
        self.assertIsNotNone(shell)
        with tempfile.TemporaryDirectory(prefix='sf2 shadow bash ') as temporary:
            shadow = Path(temporary) / 'bash'
            shadow.write_text('#!/bin/sh\nexit 97\n')
            shadow.chmod(0o755)
            env = {**os.environ, 'TARGET': 'windows', 'PATH': temporary + os.pathsep + os.environ['PATH']}
            result = subprocess.run([shell, str(ROOT / 'tools/setup_runtime.sh'), '--help'],
                                    env=env, check=True, capture_output=True, text=True)
            self.assertIn('Use MSYS2 UCRT64', result.stdout)

    def test_windows_build_shell_uses_msys2_even_when_wsl_bash_is_on_path(self):
        with tempfile.TemporaryDirectory(prefix='sf2 Windows shell ') as temporary:
            root = Path(temporary)
            shell = root / 'MSYS2/usr/bin/bash.exe'
            shell.parent.mkdir(parents=True)
            shell.write_bytes(b'MSYS2 bash fixture')
            cygpath = str(shell.with_name('cygpath.exe'))
            def which(name):
                return cygpath if name == 'cygpath' else str(root / 'Windows/System32/bash.exe')
            with patch.object(host_tools, 'host_target', return_value='windows'), \
                    patch.dict(os.environ, {'MSYSTEM': 'UCRT64'}), \
                    patch.object(host_tools.shutil, 'which', side_effect=which), \
                    patch.object(host_tools.subprocess, 'check_output', return_value=str(shell) + '\n') as query:
                self.assertEqual(host_tools.build_bash(), str(shell))
            query.assert_called_once_with([cygpath, '-m', '/usr/bin/bash.exe'], text=True)

    def test_windows_build_shell_fails_instead_of_falling_back_to_wsl(self):
        with patch.object(host_tools, 'host_target', return_value='windows'), \
                patch.dict(os.environ, {'MSYSTEM': 'UCRT64'}), \
                patch.object(host_tools.shutil, 'which', return_value=None):
            with self.assertRaisesRegex(ValueError, 'cygpath is missing'):
                host_tools.build_bash()
        with patch.object(host_tools, 'host_target', return_value='windows'), \
                patch.dict(os.environ, {'MSYSTEM': 'UCRT64'}), \
                patch.object(host_tools.shutil, 'which', return_value='cygpath.exe'), \
                patch.object(host_tools.subprocess, 'check_output', return_value='missing/bash.exe\n'):
            with self.assertRaisesRegex(ValueError, 'bash is missing'):
                host_tools.build_bash()

    def test_ffmpeg_source_extraction_does_not_depend_on_the_host_tar_command(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = root / 'decoder source.tar.xz'
            with tarfile.open(archive, 'w:xz') as output:
                for name in ('configure', 'COPYING.LGPLv2.1', 'LICENSE.md'):
                    entry = tarfile.TarInfo(f'ffmpeg-{setup_ffmpeg.VERSION}/{name}')
                    entry.size = len(b'fixture')
                    output.addfile(entry, io.BytesIO(b'fixture'))
            with patch.object(setup_ffmpeg, 'source_archive', return_value=archive), \
                    patch.object(setup_ffmpeg, 'installed', side_effect=[False, True]), \
                    patch.object(setup_ffmpeg, 'build_bash', return_value='verified-msys-bash.exe'), \
                    patch.object(setup_ffmpeg.shutil, 'which', return_value='build-tool'), \
                    patch.object(setup_ffmpeg.subprocess, 'run') as run:
                setup_ffmpeg.build(root / 'installed decoder', root / 'build work', target='windows')
            source = root / f'build work/ffmpeg-{setup_ffmpeg.VERSION}-windows-source'
            self.assertEqual((source / 'configure').read_bytes(), b'fixture')
            self.assertEqual([call.args[0][0] for call in run.call_args_list], ['verified-msys-bash.exe', 'make', 'make'])

    def test_archive_audit_rejects_keys_game_inputs_and_external_links(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ('prod.keys', 'game.nsp', 'game/main.c'):
                file = root / name
                file.parent.mkdir(exist_ok=True)
                file.write_bytes(b'private fixture')
                with self.assertRaisesRegex(ValueError, 'Private/generated'):
                    package_release.audit(root)
                file.unlink()
                if file.parent != root: file.parent.rmdir()
            if os.name != 'nt':
                (root / 'escape').symlink_to('/etc/passwd')
                with self.assertRaisesRegex(ValueError, 'symlink escapes'):
                    package_release.audit(root)

    def test_competing_conversions_fail_and_lock_releases_after_error(self):
        with tempfile.TemporaryDirectory() as temporary, patch.object(release, 'ROOT', Path(temporary)):
            with release.conversion_lock():
                with self.assertRaisesRegex(ValueError, 'Another conversion'):
                    with release.conversion_lock(): pass
            try:
                with release.conversion_lock(): raise RuntimeError('interrupted conversion')
            except RuntimeError: pass
            with release.conversion_lock(): pass

    def test_android_is_optional_and_all_managed_state_stays_in_extension(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / 'tool/extensions/android'
            env = setup_android.environment(root)
            for key in ('JAVA_HOME', 'ANDROID_HOME', 'ANDROID_SDK_ROOT', 'GRADLE_USER_HOME', 'ANDROID_USER_HOME', 'ANDROID_PREFS_ROOT'):
                # macOS /var aliases and Windows temp-path normalization must
                # be resolved on both sides of the containment comparison.
                self.assertTrue(Path(env[key]).resolve().is_relative_to(root.resolve()), (key, env[key]))
            with patch.object(setup_android.urllib.request, 'urlopen', side_effect=AssertionError('unexpected download')):
                with self.assertRaisesRegex(ValueError, 'SDK licenses'):
                    setup_android.ensure(directory=root)
            self.assertFalse(root.exists())

    def test_android_ready_cache_is_reused_offline_and_version_mismatch_is_detected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            env = setup_android.environment(root)
            for path in setup_android.components(env):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'installed component fixture')
            stamp = root / 'ready.json'
            stamp.write_text(json.dumps({'packages': setup_android.PACKAGES, 'jdk': setup_android.JDK_VERSION}))
            with patch.object(setup_android, 'download', side_effect=AssertionError('unexpected download')):
                self.assertEqual(setup_android.ensure(directory=root)['JAVA_HOME'], env['JAVA_HOME'])
            stamp.write_text(json.dumps({'packages': setup_android.PACKAGES, 'jdk': 'old-version'}))
            self.assertFalse(setup_android.ready(root))


class LiteralMigrationTests(unittest.TestCase):
    def test_existing_catalog_collision_does_not_change_a_new_literal(self):
        with tempfile.TemporaryDirectory() as temporary, patch.object(centralize_literals, 'ROOT', Path(temporary)), \
                patch.object(centralize_literals, 'key', return_value='TEXT_COLLISION'):
            root = Path(temporary)
            (root / 'tools').mkdir()
            source = root / 'tools/collision.py'
            source.write_text('FIRST = "first value"\n')
            centralize_literals.migrate_python(source, True)
            source.write_text(source.read_text() + 'SECOND = "different value"\n')
            before = source.read_text()
            with self.assertRaisesRegex(ValueError, 'Literal key collision'):
                centralize_literals.migrate_python(source, True)
            self.assertEqual(source.read_text(), before)

    def test_conditional_c_definitions_keep_their_distinct_values(self):
        with tempfile.TemporaryDirectory() as temporary, patch.object(centralize_literals, 'ROOT', Path(temporary)):
            root = Path(temporary)
            (root / 'runtime').mkdir()
            source = root / 'runtime/variants.h'
            original = '#ifdef __APPLE__\n#define CHANNEL "mac"\n#else\n#define CHANNEL "linux"\n#endif\n'
            source.write_text(original)
            centralize_literals.migrate_c(source, True)
            centralize_literals.name_declarations()
            centralize_literals.prune()
            self.assertEqual(centralize_literals.expanded(source), original)

    def test_python_repeat_migration_preserves_existing_imports_and_values(self):
        with tempfile.TemporaryDirectory() as temporary, patch.object(centralize_literals, 'ROOT', Path(temporary)):
            root = Path(temporary)
            (root / 'tools').mkdir()
            source = root / 'tools/repeat.py'
            source.write_text('"""Fixture documentation."""\nVALUE = 42\n')
            centralize_literals.migrate_python(source, True)
            source.write_text(source.read_text() + 'EXTRA = VALUE + 42 + 84\nassert EXTRA == 168\n')
            expected = ast.dump(ast.parse(centralize_literals.expanded(source)))
            centralize_literals.migrate_python(source, True)
            self.assertEqual(ast.dump(ast.parse(centralize_literals.expanded(source))), expected)
            subprocess.run([sys.executable, str(source)], check=True)

    def test_c_repeat_migration_preserves_pasted_type_tokens(self):
        with tempfile.TemporaryDirectory() as temporary, patch.object(centralize_literals, 'ROOT', Path(temporary)):
            root = Path(temporary)
            (root / 'runtime').mkdir()
            source = root / 'runtime/repeat.c'
            source.write_text('#include <stdint.h>\n#define WORD(N) uint##N##_t\nWORD(32) value = 16;\n')
            centralize_literals.migrate_c(source, True)
            source.write_text(source.read_text() + 'int extra = 16 + 64;\n')
            expected = centralize_literals.expanded(source)
            centralize_literals.migrate_c(source, True)
            self.assertEqual(centralize_literals.expanded(source), expected)
            self.assertIn('WORD(32)', source.read_text())


if __name__ == '__main__':
    unittest.main()
