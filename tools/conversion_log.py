"""Name conversion logs consistently and mirror command-line output into them."""
from _sf2_tool_constants import CONVERSION_LOG as K
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime
import os
from pathlib import Path
import subprocess
import sys
import threading

from release_support import ROOT

LOG_ACTIVE = K.S_SF2_CONVERSION_LOG_ACTIVE_F26F


def new_conversion_log():
    directory = ROOT / K.S_LOGS_A052
    directory.mkdir(parents=True, exist_ok=True)
    timestamp = datetime.now().strftime(K.S_Y_M_D_H_M_S_F_25D5)
    return directory / f"sf2recomp-conversion-{timestamp}.log"


def run_logged_cli(entry, arguments, *, isolated=False):
    log_path = new_conversion_log()
    command = [sys.executable, K.S_U_A2FD, K.S_X_2FB0, K.S_UTF8_DEF4]
    if isolated:
        command.append(K.S_I_C4CD)
    command.extend((str(Path(entry).resolve()), *arguments))
    env = dict(os.environ, **{LOG_ACTIVE: K.S_1_3915})
    with log_path.open(K.S_XB_3EFC) as log:
        print(f"Conversion log: {log_path}", file=sys.stderr, flush=True)
        lock = threading.Lock()

        def append(data):
            with lock:
                log.write(data)
                log.flush()

        def mirror(pipe, output):
            pending = K.B_B_62D3
            while data := pipe.read1(K.N_65536_0F25):
                # Keep fragments from stdout and stderr from sharing a log line.
                # Still forward partial lines immediately for interactive prompts.
                pending += data
                end = pending.rfind(K.B_B_N_9579) + 1
                if end:
                    append(pending[:end])
                    pending = pending[end:]
                if output is not None:
                    try:
                        output.buffer.write(data)
                        output.buffer.flush()
                    except BrokenPipeError:
                        output = None
            if pending:
                append(pending + K.B_B_N_9579)

        with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env) as process:
            with ThreadPoolExecutor(max_workers=K.N_2_D473) as pool:
                copies = [pool.submit(mirror, process.stdout, sys.stdout),
                          pool.submit(mirror, process.stderr, sys.stderr)]
                try:
                    for copy in as_completed(copies):
                        copy.result()
                except BaseException:
                    process.kill()
                    raise
            return process.wait()
