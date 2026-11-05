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
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import downloads
import package_release
import release
import setup_android
import setup_ffmpeg
import centralize_literals
import host_tools
import ci_release
import windows_dlls
import release_support


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


@unittest.skipUnless(os.name != 'nt' and shutil.which('clang'), 'Requires a configured native Clang toolchain')
class ReleaseCompilerTests(unittest.TestCase):
    def test_compile_and_link_do_not_emit_unused_argument_warnings(self):
        with tempfile.TemporaryDirectory(prefix='sf2 compiler ') as temporary:
            root = Path(temporary)
            source = root / 'module.c'
            source.write_text('int main(void) { return 0; }\n')
            # Older manifests put -L in the compiler command. This triggers the
            # reported warning on each compile, and -Werror makes it testable.
            data = {'compiler': [shutil.which('clang'), '-Werror', '-L' + str(root / 'lib')],
                    'cflags': [], 'ldflags': [], 'probe_ldflags': [],
                    'version': 'test', 'build_id': 'test'}
            run = subprocess.run
            diagnostics = []
            def capture(command, **options):
                result = run(command, capture_output=True, text=True, **options)
                diagnostics.append(result.stderr)
                return result
            with patch.object(release_support, 'ROOT', root), \
                    patch.object(release_support, 'manifest', return_value=data), \
                    patch.object(release_support.subprocess, 'run', side_effect=capture):
                binary = release_support.compile_sources([source], root / ('probe.exe' if os.name == 'nt' else 'probe'), runtime=False)
            self.assertEqual(diagnostics, ['', ''])
            run([str(binary)], check=True)


class ReleaseBoundaryTests(unittest.TestCase):
    def test_release_sources_keep_conversion_and_android_without_development_commands(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            keep = ('tools/release.py', 'tools/converter_ui.py', 'tools/setup_android.py', 'tools/recompr/gen.py',
                    'runtime/cpu.h', 'assets/sf2recomp.png', 'assets/sf2recomp.ico', 'assets/README.md',
                    'assets/sf2recomp-readme.png')
            omit = ('tools/test_lan.py', 'tools/package_release.py', 'tools/ci_release.py',
                    'tools/centralize_literals.py', 'tools/check_doc_links.py', 'tools/prepare_icon.py')
            for name in (*keep, *omit):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(name)
            smaa = root / package_release.K.S_THIRD_PARTY_SMAA_8DBE
            smaa.mkdir(parents=True)
            (smaa / 'LICENSE').write_text('shader license fixture')
            with patch.object(package_release, 'ROOT', root), \
                    patch.object(package_release.subprocess, 'check_output', return_value='\0'.join((*keep, *omit)).encode()):
                destination = root / 'release'
                package_release.tracked_sources(destination)
            for name in keep:
                self.assertTrue((destination / name).is_file(), name)
            for name in omit:
                self.assertFalse((destination / name).exists(), name)

    def test_windows_archive_preserves_the_full_version_and_platform_name(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary) / 'sf2recomp-0.1.0-windows-x86_64'
            (folder / 'python').mkdir(parents=True)
            (folder / 'python/python.exe').write_bytes(b'interpreter fixture')
            (folder / 'release.json').write_text(json.dumps({'version': '0.1.0'}))
            windows = SimpleNamespace(name='nt', environ=os.environ)
            with patch.object(ci_release, 'os', windows), patch.object(ci_release.subprocess, 'run'):
                ci_release.verify_and_archive(folder)
            archive = folder.with_name(folder.name + '.zip')
            self.assertTrue(archive.is_file())
            with zipfile.ZipFile(archive) as contents:
                self.assertIn(folder.name + '/python/python.exe', contents.namelist())
            self.assertEqual(archive.with_name(archive.name + '.sha256').read_text(),
                             hashlib.sha256(archive.read_bytes()).hexdigest() + '  ' + archive.name + '\n')

    def test_windows_toolchain_copies_runtime_dlls_and_link_inputs(self):
        with tempfile.TemporaryDirectory(prefix='sf2 GCC support ') as temporary:
            root = Path(temporary)
            prefix = root / 'MSYS2/ucrt64'
            gcc = prefix / 'lib/gcc/x86_64-w64-mingw32/16.2.0'
            for relative in ('bin/clang.exe', 'bin/ld.lld.exe', 'bin/SDL3.dll',
                             'bin/libshaderc_shared.dll', 'bin/vulkan-1.dll', 'include/stdio.h', 'lib/libucrt.a',
                             'lib/crt2.o', 'lib/crtbegin.o', 'lib/crtend.o',
                             'lib/clang/23/include/stddef.h', 'lib/clang/23/lib/windows/clang_rt.builtins.a',
                             'include/clang/AST.h', 'include/clang-c/Index.h',
                             'include/llvm/Module.h', 'include/llvm-c/Core.h',
                             'lib/libclangAST.a', 'lib/libclang-cpp.dll.a', 'lib/libLLVMCodeGen.a',
                             'bin/libLLVM-23.dll', 'bin/libclang-cpp.dll', 'bin/libstdc++-6.dll'):
                file = prefix / relative
                file.parent.mkdir(parents=True, exist_ok=True)
                file.write_bytes(relative.encode())
            gcc.mkdir(parents=True)
            for name in ('libgcc.a', 'libgcc_eh.a'):
                (gcc / name).write_bytes(('support ' + name).encode())
            def query(command, *, text):
                self.assertEqual(command[:2], [str(prefix / 'bin/clang.exe'), '--target=x86_64-w64-windows-gnu'])
                name = command[2].removeprefix('-print-file-name=')
                return str(gcc / name) + '\n'
            def dependencies(binaries, directories):
                shared = {'libstdc++-6.dll': prefix / 'bin/libstdc++-6.dll'}
                if any(p.name == 'clang.exe' for p in binaries):
                    shared.update({name.lower(): prefix / 'bin' / name
                                   for name in ('libLLVM-23.dll', 'libclang-cpp.dll')})
                return shared
            with patch.object(package_release.subprocess, 'check_output', side_effect=query), \
                    patch('windows_dlls.dependency_closure', side_effect=dependencies):
                destination = root / 'portable tool'
                compiler = package_release.windows_toolchain(destination, prefix, [])
                self.assertFalse(any(arg.startswith('-L') for arg in compiler))
                self.assertEqual((destination / 'native/lib/vulkan-1.dll').read_bytes(), b'bin/vulkan-1.dll')
                for name in ('libgcc.a', 'libgcc_eh.a'):
                    self.assertEqual((destination / 'native/toolchain/lib' / name).read_bytes(),
                                     ('support ' + name).encode())
                for name in ('crt2.o', 'crtbegin.o', 'crtend.o'):
                    self.assertEqual((destination / 'native/toolchain/lib' / name).read_bytes(),
                                     ('lib/' + name).encode())
                toolchain = destination / 'native/toolchain'
                self.assertTrue((toolchain / 'include/stdio.h').is_file())
                self.assertTrue((toolchain / 'lib/libucrt.a').is_file())
                self.assertTrue((toolchain / 'lib/clang/23/include/stddef.h').is_file())
                self.assertTrue((toolchain / 'lib/clang/23/lib/windows/clang_rt.builtins.a').is_file())
                for name in ('clang', 'clang-c', 'llvm', 'llvm-c'):
                    self.assertFalse((toolchain / 'include' / name).exists())
                for name in ('libclangAST.a', 'libclang-cpp.dll.a', 'libLLVMCodeGen.a'):
                    self.assertFalse((toolchain / 'lib' / name).exists())
                for name in ('libLLVM-23.dll', 'libclang-cpp.dll'):
                    self.assertTrue((toolchain / 'bin' / name).is_file())
                    self.assertFalse((destination / 'native/lib' / name).exists())
                self.assertTrue((destination / 'native/lib/libstdc++-6.dll').is_file())
                self.assertFalse((toolchain / 'bin/libstdc++-6.dll').exists())
                self.assertFalse((toolchain / 'bin/vulkan-1.dll').exists())
                (gcc / 'libgcc_eh.a').unlink()
                with self.assertRaisesRegex(ValueError, 'required support archive libgcc_eh.a'):
                    package_release.windows_toolchain(root / 'incomplete tool', prefix, [])

    def test_zig_keeps_native_headers_and_runtime_sources_without_other_platforms(self):
        for target, source, header, other in (
                ('linux', 'glibc', 'x86-linux-gnu', 'any-macos-any'),
                ('macos', 'darwin', 'any-macos-any', 'any-linux-any')):
            with self.subTest(target=target), tempfile.TemporaryDirectory() as temporary:
                prefix = Path(temporary)
                zig = prefix / 'lib/python3.12/site-packages/ziglang'
                license_dir = prefix / 'lib/python3.12/site-packages/ziglang.dist-info/licenses/ziglang'
                license_dir.mkdir(parents=True)
                (license_dir / 'LICENSE').write_text('upstream license fixture')
                for name in ('libc/glibc/runtime.c', 'libc/musl/runtime.c', 'libc/darwin/runtime.c',
                             'libc/mingw/runtime.c', 'libc/include/x86-linux-gnu/stdio.h',
                             'libc/include/any-linux-any/linux.h', 'libc/include/generic-glibc/features.h',
                             'libc/include/any-macos-any/stdio.h', 'libc/include/any-windows-any/windows.h',
                             'libcxx/vector', 'libcxxabi/runtime.cpp', 'libtsan/runtime.cpp',
                             'docs/index.html', 'compiler_rt.zig', 'std/std.zig',
                             'include/stddef.h', 'libunwind/unwind.c'):
                    file = zig / 'lib' / name
                    file.parent.mkdir(parents=True, exist_ok=True)
                    file.write_text(name)
                (zig / 'zig').write_text('compiler fixture')
                package_release.trim_zig(prefix, target)
                self.assertTrue((zig / 'zig').is_file())
                self.assertTrue((license_dir / 'LICENSE').is_file())
                self.assertTrue((zig / 'lib/libc' / source / 'runtime.c').is_file())
                self.assertTrue((zig / 'lib/libc/include' / header / 'stdio.h').is_file())
                self.assertFalse((zig / 'lib/libc/include' / other).exists())
                self.assertFalse((zig / 'lib/libc/mingw').exists())
                self.assertFalse((zig / 'lib/libtsan').exists())
                self.assertTrue((zig / 'lib/std/std.zig').is_file())
                self.assertTrue((zig / 'lib/compiler_rt.zig').is_file())
                self.assertTrue((zig / 'lib/include/stddef.h').is_file())
                if target == 'linux':
                    self.assertTrue((zig / 'lib/libc/musl/runtime.c').is_file())
                    self.assertTrue((zig / 'lib/libc/include/generic-glibc/features.h').is_file())
                    self.assertFalse((zig / 'lib/libcxx').exists())
                else:
                    self.assertTrue((zig / 'lib/libcxx/vector').is_file())
                    self.assertTrue((zig / 'lib/libcxxabi/runtime.cpp').is_file())
                # Trimming an already trimmed staging folder is safe.
                package_release.trim_zig(prefix, target)

    def test_python_runtime_cleanup_keeps_shared_modules_compiler_inputs_and_licenses(self):
        for layout in ('Lib', 'lib/python3.12'):
            with self.subTest(layout=layout), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                stdlib = root / layout
                for relative in ('include/Python.h', 'libs/python312.lib', 'bin/pip3',
                                 'bin/python3-config', 'bin/python3', 'python.exe',
                                 'lib/libpython3.12.so', 'lib/libpython3.12.a', 'lib/tcl8.6/init.tcl',
                                 'Scripts/pip.exe', 'share/terminfo/x/xterm', 'share/licenses/python/LICENSE',
                                 'DLLs/python-runtime.dll', 'DLLs/_tkinter.pyd'):
                    path = root / relative
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(relative.encode())
                for relative in ('os.py', 'sysconfig.py', '_sysconfigdata_native.py', 'ssl.py', 'threading.py',
                                 'ensurepip/bundled.whl', 'unittest/__init__.py', 'tkinter/__init__.py',
                                 'config-native/Makefile', 'site-packages/pip/__init__.py',
                                 'site-packages/pip-1.dist-info/METADATA',
                                 'site-packages/unicorn/lib/libunicorn.a',
                                 'site-packages/unicorn/lib/unicorn.lib',
                                 'site-packages/unicorn/lib/libunicorn.so.2',
                                 'site-packages/unicorn/lib/unicorn.dll',
                                 'site-packages/unicorn/unicorn_py3/__init__.py',
                                 'site-packages/unicorn/tests/test_runtime.py',
                                 'site-packages/unicorn-1.dist-info/licenses/LICENSE',
                                 'site-packages/ziglang/lib/include/stddef.h',
                                 'site-packages/ziglang/lib/runtime.a',
                                 'site-packages/ziglang-1.dist-info/licenses/ziglang/lib/NOTICE',
                                 'site-packages/certifi/cacert.pem', 'site-packages/lz4/block/module.so',
                                 'site-packages/lz4/frame/module.so', 'site-packages/cffi/__pycache__/api.pyc'):
                    path = stdlib / relative
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(relative.encode())
                self.assertGreater(package_release.trim_python(root), 0)
                for relative in ('bin/python3', 'python.exe', 'lib/libpython3.12.so',
                                 'share/licenses/python/LICENSE', 'DLLs/python-runtime.dll',
                                 'lib/tcl8.6/init.tcl', 'DLLs/_tkinter.pyd'):
                    self.assertTrue((root / relative).is_file(), relative)
                for relative in ('os.py', 'sysconfig.py', '_sysconfigdata_native.py', 'ssl.py', 'threading.py', 'tkinter/__init__.py',
                                 'site-packages/unicorn/lib/libunicorn.so.2',
                                 'site-packages/unicorn/lib/unicorn.dll',
                                 'site-packages/unicorn/unicorn_py3/__init__.py',
                                 'site-packages/unicorn-1.dist-info/licenses/LICENSE',
                                 'site-packages/ziglang/lib/include/stddef.h',
                                 'site-packages/ziglang/lib/runtime.a',
                                 'site-packages/ziglang-1.dist-info/licenses/ziglang/lib/NOTICE',
                                 'site-packages/certifi/cacert.pem', 'site-packages/lz4/block/module.so'):
                    self.assertTrue((stdlib / relative).is_file(), relative)
                for relative in ('include', 'libs', 'Scripts', 'bin/pip3', 'bin/python3-config',
                                 'lib/libpython3.12.a', 'share/terminfo'):
                    self.assertFalse((root / relative).exists(), relative)
                for relative in ('ensurepip', 'unittest', 'config-native', 'site-packages/pip',
                                 'site-packages/pip-1.dist-info', 'site-packages/unicorn/tests',
                                 'site-packages/unicorn/lib/libunicorn.a', 'site-packages/unicorn/lib/unicorn.lib',
                                 'site-packages/lz4/frame', 'site-packages/cffi/__pycache__'):
                    self.assertFalse((stdlib / relative).exists(), relative)
                self.assertEqual(package_release.trim_python(root), 0)

    def test_python_cleanup_preserves_tcl_data_and_threading_for_the_converter(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            library = root / 'lib'
            (library / 'thread2.8.7').mkdir(parents=True)
            (library / 'thread2.8.7/pkgIndex.tcl').write_text('unused Tcl data')
            # Windows Lib and lib refer to the same directory. The Tcl cleanup
            # must not match the threading.py module used by subprocess.
            (library / 'threading.py').write_text('stdlib threading fixture')
            package_release.trim_python(root)
            self.assertTrue((library / 'threading.py').is_file())
            self.assertTrue((library / 'thread2.8.7/pkgIndex.tcl').is_file())

    def test_windows_decoder_collects_external_transitive_imports_before_release(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            decoder, system = root / 'decoder', root / 'ucrt64/bin'
            (decoder / 'bin').mkdir(parents=True)
            system.mkdir(parents=True)
            for path in (decoder / 'bin/ffmpeg.exe', decoder / 'bin/avcodec-1.dll',
                         system / 'libwinpthread-1.dll'):
                path.write_bytes(path.name.encode())
            graph = {'ffmpeg.exe': ['avcodec-1.dll'], 'avcodec-1.dll': ['libwinpthread-1.dll'],
                     'libwinpthread-1.dll': ['KERNEL32.dll']}
            with patch.object(windows_dlls, 'imports', side_effect=lambda path, _: graph[path.name]):
                package_release.bundle_windows_decoder(decoder, [system])
            self.assertEqual((decoder / 'bin/libwinpthread-1.dll').read_bytes(), b'libwinpthread-1.dll')
            (decoder / 'bin/libwinpthread-1.dll').unlink()
            (system / 'libwinpthread-1.dll').unlink()
            with patch.object(windows_dlls, 'imports', side_effect=lambda path, _: graph[path.name]):
                with self.assertRaisesRegex(ValueError, 'Missing Windows DLL libwinpthread-1.dll'):
                    package_release.bundle_windows_decoder(decoder, [system])

    def test_literal_stdlib_lookup_does_not_depend_on_case_sensitive_glob(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            stdlib = root / 'Lib'
            (stdlib / 'ensurepip').mkdir(parents=True)
            (stdlib / 'os.py').write_text('stdlib fixture')
            (stdlib / 'ensurepip/bundled.whl').write_bytes(b'unused installer')
            original_glob = Path.glob
            def glob(path, pattern):
                # Models macOS: literal Lib exists, but POSIX glob sees the
                # directory's on-disk spelling lib and does not match Lib.
                if path == root and pattern == 'Lib':
                    return iter(())
                return original_glob(path, pattern)
            with patch.object(Path, 'glob', glob):
                package_release.trim_python(root)
            self.assertTrue((stdlib / 'os.py').is_file())
            self.assertFalse((stdlib / 'ensurepip').exists())

    def test_decoder_cleanup_keeps_runtime_libraries_and_corresponding_source(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for relative in ('bin/ffmpeg', 'lib/libavcodec.so.1', 'lib/avcodec.dll', 'lib/libavcodec.1.dylib',
                             'lib/libavcodec.a', 'include/avcodec.h', 'lib/pkgconfig/avcodec.pc',
                             'share/ffmpeg/source.tar.xz', 'share/ffmpeg/BUILD.txt',
                             'share/licenses/FFmpeg/LICENSE.md'):
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(relative.encode())
            package_release.trim_decoder(root)
            for relative in ('bin/ffmpeg', 'lib/libavcodec.so.1', 'lib/avcodec.dll', 'lib/libavcodec.1.dylib',
                             'share/ffmpeg/source.tar.xz', 'share/ffmpeg/BUILD.txt',
                             'share/licenses/FFmpeg/LICENSE.md'):
                self.assertTrue((root / relative).is_file(), relative)
            for relative in ('lib/libavcodec.a', 'include', 'lib/pkgconfig'):
                self.assertFalse((root / relative).exists(), relative)

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
