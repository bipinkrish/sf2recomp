#!/usr/bin/env python3
"""Collect SF2 logs across app restarts without copying saves or clearing data."""
from _sf2_tool_constants import ANDROID_DIAGNOSTICS as K
import argparse
from datetime import datetime, timezone
import os
from pathlib import Path
import re
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(K.S_SERIAL_63A3, required=True, help=K.S_ADB_DEVICE_SERIAL_0C03)
    parser.add_argument(K.S_ADB_27CE, default=str(Path(os.environ.get(K.S_ANDROID_HOME_00C5, Path.home() / K.S_ANDROID_SDK_C772)) / K.S_PLATFORM_TOOLS_ADB_4519))
    parser.add_argument(K.S_OUTPUT_3033, type=Path)
    parser.add_argument(K.S_WATCH_SECONDS_11F4, type=float, default=0)
    parser.add_argument(K.S_LAUNCH_9F72, action=K.S_STORE_TRUE_2C29, help=K.S_RESTART_THE_GAME_TO_MEASURE__DDA8)
    args = parser.parse_args()
    if args.watch_seconds < 0:
        parser.error(K.S_WATCH_SECONDS_MUST_BE_NONNEG_2D28)
    stamp = datetime.now(timezone.utc).strftime(K.S_Y_M_DT_H_M_SZ_A84E)
    output = args.output or Path(K.S_BUILD_VALIDATION_E20A) / (K.S_ANDROID_DIAGNOSTICS_CF64 + stamp)
    output.mkdir(parents=True, exist_ok=True)
    adb = [args.adb, K.S_S_6B2C, args.serial]

    def read(*command):
        return subprocess.run(adb + list(command), stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True).stdout

    root = K.S_SDCARD_ANDROID_DATA_ORG_SF2R_7600
    (output / K.S_SF2RECOMP_INI_8714).write_bytes(read(K.S_EXEC_OUT_4447, K.S_CAT_2EB1, root + K.S_SF2RECOMP_INI_8714))
    if args.launch:
        read(K.S_SHELL_20CB, K.S_AM_D4DE, K.S_FORCE_STOP_F59C, K.S_ORG_SF2RECOMP_APP_E5FD)
        read(K.S_SHELL_20CB, K.S_AM_D4DE, K.S_START_F2B4, K.S_N_61DF, K.S_ORG_SF2RECOMP_APP_GAMEACTIVI_2252)
    deadline = time.monotonic() + args.watch_seconds
    while True:
        log = read(K.S_EXEC_OUT_4447, K.S_CAT_2EB1, root + K.S_LASRECOMP_LOG_5283)
        header = log.split(K.B_B_N_D4EC, 1)[0]
        pid = re.search(K.B_B_PID_D_124E, header)
        name = K.S_GAME_PID_EA3B + pid.group(1).decode() if pid else K.S_GAME_CF8E
        (output / (name + K.S_LOG_A6B2)).write_bytes(log)
        if time.monotonic() >= deadline:
            break
        time.sleep(min(K.N_2_D473, max(0, deadline - time.monotonic())))
    (output / K.S_STARTUP_LOGCAT_TXT_0E4D).write_bytes(read(K.S_LOGCAT_5020, K.S_D_3157, K.S_V_66F7, K.S_THREADTIME_3AA3, K.S_S_6B2C, K.S_SHADOWFIGHT2_E29A))
    print(output.resolve())


if __name__ == K.S_MAIN_ACB5:
    main()
