#!/usr/bin/env python3
"""Native file-picker interface for the existing conversion commands."""
from _sf2_tool_constants import CONVERTER_UI as K
import os
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading

from host_tools import host_target
from release_support import ROOT, environment, manifest

TARGETS = (K.S_THIS_COMPUTER_737B, K.S_ANDROID_PHONE_ARM64_BADE, K.S_ANDROID_EMULATOR_X86_64_3D7D)


def conversion_command(source_kind, source, keys, output, target, accept_licenses=False):
    """Validate picker inputs and build argv without invoking a shell."""
    if source_kind not in (K.S_NSP_FILE_C79E, K.S_EXTRACTED_GAME_FOLDER_DCD2) or target not in TARGETS:
        raise ValueError(K.S_CHOOSE_A_SUPPORTED_INPUT_AND_A0EF)
    if not source.strip() or not output.strip():
        raise ValueError(K.S_CHOOSE_YOUR_GAME_INPUT_AND_A_559F)
    source_path = Path(source.strip().strip(K.S_EMPTY_3251)).expanduser().resolve()
    output_path = Path(output.strip().strip(K.S_EMPTY_3251)).expanduser().resolve()
    nsp = source_kind == K.S_NSP_FILE_C79E
    if not (source_path.is_file() if nsp else source_path.is_dir()):
        raise ValueError(K.S_THE_SELECTED_GAME_INPUT_DOES_5F6B)
    if nsp and source_path.suffix.lower() != K.S_NSP_98FD:
        raise ValueError(K.S_CHOOSE_THE_SUPPORTED_COMBINE_8C3C)
    if output_path.exists() and not output_path.is_dir():
        raise ValueError(K.S_THE_OUTPUT_MUST_BE_A_FOLDER_CE58)
    if (ROOT.is_relative_to(output_path) or source_path.is_relative_to(output_path)
            or (not nsp and output_path.is_relative_to(source_path))):
        raise ValueError(K.S_CHOOSE_AN_OUTPUT_FOLDER_SEPA_E9CC)
    arguments = [K.S_NSP_14EB if nsp else K.S_GAME_3AF1, str(source_path), K.S_OUTPUT_E459, str(output_path)]
    if nsp:
        if not keys.strip():
            raise ValueError(K.S_CHOOSE_YOUR_PROD_KEYS_FILE_F_B4A6)
        keys_path = Path(keys.strip().strip(K.S_EMPTY_3251)).expanduser().resolve()
        if not keys_path.is_file():
            raise ValueError(K.S_THE_SELECTED_PROD_KEYS_FILE__5C1E)
        arguments += [K.S_KEYS_77AF, str(keys_path)]
    release = manifest()
    if target != TARGETS[0]:
        if not release:
            raise ValueError(K.S_USE_A_DESKTOP_RELEASE_FOR_AN_8E74)
        from setup_android import ready
        if not accept_licenses and not ready():
            raise ValueError(K.S_READ_AND_ACCEPT_THE_ANDROID__CD12)
        arguments += [K.S_ANDROID_BF78, K.S_ABI_FB9C, K.S_ARM64_V8A_8231 if target == TARGETS[1] else K.S_X86_64_AC0E]
        if accept_licenses:
            arguments.append(K.S_ACCEPT_ANDROID_LICENSES_98C2)
    entry = K.S_RELEASE_ENTRY_PY_4DB7 if release else K.S_CONVERT_PY_0CD7
    command = [sys.executable, K.S_U_A2FD, K.S_X_2FB0, K.S_UTF8_DEF4]
    if release:
        command.append(K.S_I_C4CD)
    return [*command, str(ROOT / K.S_TOOLS_FB82 / entry), *arguments], output_path


def stream_conversion(command, log_path, on_line):
    """Keep the full subprocess log on disk and forward output to the UI queue."""
    env = environment() if manifest() else os.environ.copy()
    env.update(PYTHONUNBUFFERED=K.S_1_3915, PYTHONIOENCODING=K.S_UTF_8_D983)
    options = {K.S_CREATIONFLAGS_23A9: subprocess.CREATE_NO_WINDOW} if os.name == K.S_NT_ABB5 else {}
    with log_path.open(K.S_W_7265, encoding=K.S_UTF_8_D983) as log:
        with subprocess.Popen(command, cwd=ROOT, env=env, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, text=True, encoding=K.S_UTF_8_D983,
                              errors=K.S_REPLACE_197D, **options) as process:
            for line in process.stdout:
                log.write(line)
                log.flush()
                on_line(line)
            return process.wait()


class ConverterWindow:
    def __init__(self, window):
        import tkinter as tk
        from tkinter import ttk, filedialog, messagebox
        from tkinter.scrolledtext import ScrolledText
        self.window, self.filedialog, self.messagebox = window, filedialog, messagebox
        self.events = queue.Queue()
        self.running = False
        self.output_path = None
        window.title(K.S_SF2RECOMP_CONVERTER_E815)
        self.icon = tk.PhotoImage(file=str(ROOT / K.S_ASSETS_SF2RECOMP_PNG_B232))
        window.iconphoto(True, self.icon)
        self.badge = self.icon.subsample(K.N_8_2C62, K.N_8_2C62)
        window.minsize(K.N_700_99EE, K.N_570_085B)
        window.columnconfigure(0, weight=1)
        window.rowconfigure(0, weight=1)
        panel = ttk.Frame(window, padding=K.N_24_C235)
        panel.grid(sticky=K.S_NSEW_C79F)
        panel.columnconfigure(1, weight=1)
        panel.rowconfigure(K.N_9_1958, weight=1)
        heading = ttk.Frame(panel)
        heading.grid(row=0, column=0, columnspan=K.N_3_4E07, sticky=K.S_W_7265)
        tk.Label(heading, image=self.badge, background=K.S_232A36_D57D, width=K.N_72_8722, height=K.N_72_8722).pack(
            side=K.S_LEFT_6157, padx=(0, K.N_16_B17E))
        ttk.Label(heading, text=K.APP_NAME, font=(K.S_EMPTY_12AE, K.N_19_9400, K.S_BOLD_AD3D)).pack(side=K.S_LEFT_6157)
        ttk.Label(panel, text=K.S_USE_YOUR_COMBINED_BASE_V1_1__75D0,
                  wraplength=K.N_650_2099).grid(row=1, column=0, columnspan=K.N_3_4E07, sticky=K.S_W_7265, pady=(K.N_6_E7F6, K.N_20_F5CA))
        self.kind = tk.StringVar(value=K.S_NSP_FILE_C79E)
        self.source = tk.StringVar()
        self.keys = tk.StringVar()
        self.output = tk.StringVar(value=str(ROOT / f"out/shadow-fight-2-{host_target()}"))
        self.target = tk.StringVar(value=TARGETS[0])
        self.accept = tk.BooleanVar()
        self.status = tk.StringVar(value=K.S_CHOOSE_YOUR_GAME_FILES_TO_BE_2FE6)
        self.log_label = tk.StringVar()
        self.controls = []
        self.choices = []

        def choice(label, variable, values, row):
            ttk.Label(panel, text=label).grid(row=row, column=0, sticky=K.S_W_7265, padx=(0, K.N_12_6B51), pady=K.N_6_E7F6)
            widget = ttk.Combobox(panel, textvariable=variable, values=values, state=K.S_READONLY_C4A4)
            widget.grid(row=row, column=1, columnspan=K.N_2_D473, sticky=K.S_EW_F64D, pady=K.N_6_E7F6)
            self.choices.append(widget)
            return widget

        choice(K.S_INPUT_TYPE_C617, self.kind, (K.S_NSP_FILE_C79E, K.S_EXTRACTED_GAME_FOLDER_DCD2), K.N_2_D473).bind(
            K.S_COMBOBOXSELECTED_E8BC, self.input_changed)
        for row, label, variable, picker in (
            (K.N_3_4E07, K.S_GAME_INPUT_A49F, self.source, self.pick_source),
            (K.N_4_4B22, K.S_PROD_KEYS_8404, self.keys, self.pick_keys),
            (K.N_5_EF2D, K.S_OUTPUT_FOLDER_362F, self.output, self.pick_output),
        ):
            ttk.Label(panel, text=label).grid(row=row, column=0, sticky=K.S_W_7265, padx=(0, K.N_12_6B51), pady=K.N_6_E7F6)
            entry = ttk.Entry(panel, textvariable=variable, width=K.N_54_2FCA)
            entry.grid(row=row, column=1, sticky=K.S_EW_F64D, pady=K.N_6_E7F6)
            button = ttk.Button(panel, text=K.S_BROWSE_36B5, command=picker)
            button.grid(row=row, column=K.N_2_D473, padx=(K.N_8_2C62, 0), pady=K.N_6_E7F6)
            self.controls.extend((entry, button))
        choice(K.S_CONVERT_FOR_B340, self.target, TARGETS, K.N_6_E7F6).bind(K.S_COMBOBOXSELECTED_E8BC, self.target_changed)
        self.android = ttk.Frame(panel)
        self.android.grid(row=K.N_7_7902, column=0, columnspan=K.N_3_4E07, sticky=K.S_EW_F64D, pady=K.N_6_E7F6)
        ttk.Label(self.android, text=K.S_FIRST_ANDROID_SETUP_DOWNLOAD_DBCD).pack(anchor=K.S_W_7265)
        terms = ttk.Button(self.android, text=K.S_READ_ANDROID_SDK_TERMS_A742, command=self.open_terms)
        terms.pack(side=K.S_LEFT_6157)
        accept = ttk.Checkbutton(self.android, text=K.S_I_ACCEPT_THE_ANDROID_SDK_TER_EB06, variable=self.accept)
        accept.pack(side=K.S_LEFT_6157, padx=K.N_12_6B51)
        self.controls.append(accept)
        self.android.grid_remove()
        self.progress = ttk.Progressbar(panel, mode=K.S_INDETERMINATE_DC8D)
        self.progress.grid(row=K.N_8_2C62, column=0, columnspan=K.N_3_4E07, sticky=K.S_EW_F64D, pady=(K.N_12_6B51, K.N_8_2C62))
        self.log = ScrolledText(panel, height=K.N_10_4A44, width=K.N_80_4844, state=K.S_DISABLED_F693, wrap=K.S_WORD_3E29)
        self.log.grid(row=K.N_9_1958, column=0, columnspan=K.N_3_4E07, sticky=K.S_NSEW_C79F)
        ttk.Label(panel, textvariable=self.status, wraplength=K.N_650_2099).grid(
            row=K.N_10_4A44, column=0, columnspan=K.N_3_4E07, sticky=K.S_W_7265, pady=(K.N_10_4A44, 0))
        ttk.Label(panel, textvariable=self.log_label, wraplength=K.N_650_2099).grid(
            row=K.N_11_4FC8, column=0, columnspan=K.N_3_4E07, sticky=K.S_W_7265, pady=(K.N_4_4B22, K.N_12_6B51))
        self.start_button = ttk.Button(panel, text=K.S_CONVERT_C44A, command=self.start)
        self.start_button.grid(row=K.N_12_6B51, column=1, sticky=K.S_E_8C86)
        self.open_button = ttk.Button(panel, text=K.S_OPEN_OUTPUT_FOLDER_43A5, command=self.open_output, state=K.S_DISABLED_F693)
        self.open_button.grid(row=K.N_12_6B51, column=K.N_2_D473, padx=(K.N_8_2C62, 0))
        window.protocol(K.S_WM_DELETE_WINDOW_20BA, self.close)
        window.after(K.N_100_AD57, self.poll)

    def input_changed(self, event=None):
        self.source.set(K.S_EMPTY_12AE)
        state = K.S_NORMAL_82FB if self.kind.get() == K.S_NSP_FILE_C79E else K.S_DISABLED_F693
        for control in self.controls[K.N_2_D473:K.N_4_4B22]:
            control.configure(state=state)

    def target_changed(self, event=None):
        if self.target.get() == TARGETS[0]:
            self.android.grid_remove()
        else:
            self.android.grid()

    def pick_source(self):
        path = (self.filedialog.askopenfilename(title=K.S_CHOOSE_THE_COMBINED_NSP_15AA, filetypes=[(K.S_NSP_FILES_817B, K.S_NSP_7CDC)])
                if self.kind.get() == K.S_NSP_FILE_C79E else self.filedialog.askdirectory(title=K.S_CHOOSE_EXTRACTED_UPDATED_GAM_C1FC))
        if path:
            self.source.set(path)

    def pick_keys(self):
        path = self.filedialog.askopenfilename(title=K.S_CHOOSE_PROD_KEYS_B9A7, filetypes=[(K.S_KEY_FILES_2617, K.S_KEYS_4302), (K.S_ALL_FILES_B6F9, K.S_EMPTY_ED72)])
        if path:
            self.keys.set(path)

    def pick_output(self):
        path = self.filedialog.askdirectory(title=K.S_CHOOSE_OUTPUT_FOLDER_56E2, mustexist=False)
        if path:
            self.output.set(path)

    def start(self):
        if self.running:
            return
        try:
            command, output = conversion_command(self.kind.get(), self.source.get(), self.keys.get(),
                                                 self.output.get(), self.target.get(), self.accept.get())
            logs = ROOT / K.S_BUILD_LOGS_31AC
            logs.mkdir(parents=True, exist_ok=True)
            with tempfile.NamedTemporaryFile(prefix=K.S_CONVERSION_D39C, suffix=K.S_LOG_0386, dir=logs, delete=False) as log:
                log_path = Path(log.name)
        except (OSError, ValueError) as error:
            self.messagebox.showerror(K.S_CHECK_CONVERSION_INPUTS_FE61, str(error), parent=self.window)
            return
        self.running = True
        self.output_path = output
        self.log.configure(state=K.S_NORMAL_82FB)
        self.log.delete(K.S_1_0_A511, K.S_END_2410)
        self.log.configure(state=K.S_DISABLED_F693)
        self.log_label.set(f"Full log: {log_path}")
        self.status.set(K.S_CONVERTING_THIS_CAN_TAKE_A_W_6E30)
        for control in [*self.controls, *self.choices, self.start_button, self.open_button]:
            control.configure(state=K.S_DISABLED_F693)
        self.progress.start()

        def work():
            try:
                code = stream_conversion(command, log_path, lambda line: self.events.put((K.S_LINE_42D2, line)))
            except Exception as error:
                self.events.put((K.S_LINE_42D2, f"Conversion failed: {error}\n"))
                code = 1
            self.events.put((K.S_DONE_58BF, code))

        threading.Thread(target=work, name=K.S_SF2_CONVERTER_070A).start()

    def poll(self):
        # Bound each UI update so a busy compiler cannot starve window events.
        for _ in range(K.N_200_27BA):
            try:
                kind, value = self.events.get_nowait()
            except queue.Empty:
                break
            if kind == K.S_LINE_42D2:
                self.log.configure(state=K.S_NORMAL_82FB)
                self.log.insert(K.S_END_2410, value)
                lines = int(self.log.index(K.S_END_1C_EA5D).split(K.S_EMPTY_7299)[0])
                if lines > K.N_1000_4051:
                    self.log.delete(K.S_1_0_A511, f"{lines - K.N_1000_4051}.0")
                self.log.see(K.S_END_2410)
                self.log.configure(state=K.S_DISABLED_F693)
                if value.startswith((K.S_BUILDING_GAME_EAA9, K.S_LINKING_GAME_1BED, K.S_NATIVE_GAME_FOLDER_912C, K.S_INSTALL_THIS_APK_430C)):
                    self.status.set(value.strip())
            else:
                self.running = False
                self.progress.stop()
                for control in [*self.controls, self.start_button]:
                    control.configure(state=K.S_NORMAL_82FB)
                for control in self.choices:
                    control.configure(state=K.S_READONLY_C4A4)
                if self.kind.get() != K.S_NSP_FILE_C79E:
                    for control in self.controls[K.N_2_D473:K.N_4_4B22]:
                        control.configure(state=K.S_DISABLED_F693)
                if value == 0:
                    self.open_button.configure(state=K.S_NORMAL_82FB)
                    self.status.set(K.S_CONVERSION_COMPLETE_OPEN_THE_8DF6)
                else:
                    self.status.set(K.S_CONVERSION_FAILED_SEE_THE_LO_6278)
        self.window.after(K.N_100_AD57, self.poll)

    def open_output(self):
        try:
            if os.name == K.S_NT_ABB5:
                os.startfile(self.output_path)
            else:
                subprocess.Popen([K.S_OPEN_35FF if sys.platform == K.S_DARWIN_8867 else K.S_XDG_OPEN_1D79, str(self.output_path)])
        except OSError as error:
            self.messagebox.showerror(K.S_COULD_NOT_OPEN_FOLDER_8FB6, str(error), parent=self.window)

    def open_terms(self):
        import webbrowser
        webbrowser.open(K.S_HTTPS_DEVELOPER_ANDROID_COM__AE58)

    def close(self):
        if self.running:
            self.messagebox.showinfo(K.S_CONVERSION_IS_RUNNING_1D2F, K.S_WAIT_FOR_CONVERSION_TO_FINIS_F367, parent=self.window)
        else:
            self.window.destroy()


def main():
    try:
        import tkinter as tk
    except ImportError as error:
        print(f"Cannot open the converter UI: {error}. Use the command line, or install Python Tk support.", file=sys.stderr)
        return 1
    try:
        window = tk.Tk()
    except tk.TclError as error:
        print(f"Cannot open the converter UI: {error}. Use --cli for terminal prompts.", file=sys.stderr)
        return 1
    ConverterWindow(window)
    window.mainloop()
    return 0


if __name__ == K.S_MAIN_8B1C:
    raise SystemExit(main())
