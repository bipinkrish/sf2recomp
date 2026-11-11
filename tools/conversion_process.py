"""Start and cancel the converter with all its build subprocesses."""
from _sf2_tool_constants import CONVERSION_PROCESS as K
import os
import signal
import subprocess
import sys
import threading


class WindowsJob:
    def __init__(self):
        import ctypes
        from ctypes import wintypes

        self.api = ctypes.WinDLL(K.S_KERNEL32_5504, use_last_error=True)
        self.error = ctypes.WinError
        for name, result, args in (
            (K.S_CREATEJOBOBJECTW_C215, wintypes.HANDLE, (wintypes.LPVOID, wintypes.LPCWSTR)),
            (K.S_OPENPROCESS_C468, wintypes.HANDLE, (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)),
            (K.S_ASSIGNPROCESSTOJOBOBJECT_3F38, wintypes.BOOL, (wintypes.HANDLE, wintypes.HANDLE)),
            (K.S_TERMINATEJOBOBJECT_7B29, wintypes.BOOL, (wintypes.HANDLE, wintypes.UINT)),
            (K.S_CLOSEHANDLE_416F, wintypes.BOOL, (wintypes.HANDLE,)),
        ):
            function = getattr(self.api, name)
            function.restype, function.argtypes = result, args
        self.handle = self.api.CreateJobObjectW(None, None)
        if not self.handle:
            raise self.error()

    def assign(self, pid):
        # AssignProcessToJobObject needs PROCESS_SET_QUOTA and PROCESS_TERMINATE.
        process = self.api.OpenProcess(K.N_0X0100_1781 | 0x0001, False, pid)
        if not process:
            raise self.error()
        try:
            if not self.api.AssignProcessToJobObject(self.handle, process):
                raise self.error()
        finally:
            self.api.CloseHandle(process)

    def terminate(self):
        if not self.api.TerminateJobObject(self.handle, 1):
            raise self.error()

    def close(self):
        self.api.CloseHandle(self.handle)


class ConversionProcess:
    def __init__(self):
        self.lock = threading.Lock()
        self.process = None
        self.job = None
        self.cancelled = False

    def cancel(self):
        with self.lock:
            self.terminate()
            self.cancelled = True

    def terminate(self):
        if self.process:
            if self.job:
                self.job.terminate()
            else:
                try:
                    os.killpg(self.process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass

    def run(self, command, cwd, env, on_line):
        with self.lock:
            if self.cancelled:
                return 1
            options = {K.S_START_NEW_SESSION_A94C: True}
            if sys.platform == K.S_WIN32_0211:
                # Start a hidden console so console tools started by the worker
                # inherit it, including nested compiler and Android processes.
                startup = subprocess.STARTUPINFO()
                startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
                startup.wShowWindow = subprocess.SW_HIDE
                options = {K.S_STARTUPINFO_4AD7: startup, K.S_CREATIONFLAGS_23A9: subprocess.CREATE_NEW_CONSOLE,
                           K.S_STDIN_EA20: subprocess.PIPE}
                # Wait for the parent to assign the Job before spawning any helpers.
                bootstrap = K.S_IMPORT_SUBPROCESS_SYS_SYS_EX_5FDB
                command = [sys.executable, K.S_I_C4CD, K.S_C_542D, bootstrap, *command]
                self.job = WindowsJob()
            try:
                self.process = subprocess.Popen(command, cwd=cwd, env=env, stdout=subprocess.PIPE,
                                                stderr=subprocess.STDOUT, text=True, encoding=K.S_UTF_8_D983,
                                                errors=K.S_REPLACE_197D, **options)
                if self.job:
                    self.job.assign(self.process.pid)
                    self.process.stdin.write(K.S_1_3915)
                    self.process.stdin.close()
            except BaseException:
                if self.process:
                    self.process.kill()
                    self.process.wait()
                    self.process = None
                if self.job:
                    self.job.close()
                    self.job = None
                raise

        try:
            with self.process as process:
                try:
                    for line in process.stdout:
                        on_line(line)
                except BaseException:
                    with self.lock:
                        self.terminate()
                    raise
                return process.wait()
        finally:
            with self.lock:
                self.process = None
                if self.job:
                    self.job.close()
                    self.job = None
