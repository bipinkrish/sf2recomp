"""Progress messages shared by the converter and its GUI."""
from _sf2_tool_constants import CONVERSION_PROGRESS as K
import json
import os
import re

PREFIX = K.S_SF2_PROGRESS_FA33
COUNTS = {
    K.S_TRANSLATE_GAME_CODE_80DD: re.compile(K.S_R_MAIN_PROGRESS_D_D_FILES_8F39),
    K.S_TRANSLATE_SDK_CODE_912B: re.compile(K.S_R_SDK_PROGRESS_D_D_FILES_4C0C),
    K.S_COMPILE_GAME_5C31: re.compile(K.S_R_BUILDING_GAME_D_D_FILES_RE_69CB),
}


class ConversionProgress:
    def __init__(self, steps):
        self.steps = iter(enumerate(steps, 1))
        self.total = len(steps)

    def advance(self):
        step, label = next(self.steps)
        if os.environ.get(K.S_SF2_GUI_PROGRESS_98F6) == K.S_1_3915:
            print(PREFIX + json.dumps([step, self.total, label]), flush=True)


class ProgressDisplay:
    def __init__(self):
        self.step, self.total, self.label = 0, 1, K.S_EMPTY_12AE

    def update(self, line):
        """Return overall percent and stage text when this line reports progress."""
        if line.startswith(PREFIX):
            self.step, self.total, self.label = json.loads(line[len(PREFIX):])
            return K.N_100_AD57 * (self.step - 1) / self.total, f"Step {self.step}/{self.total} · {self.label}"
        counter = COUNTS.get(self.label)
        if counter and (match := counter.match(line)):
            done, total = map(int, match.groups())
            if total:
                return (K.N_100_AD57 * (self.step - 1 + min(done / total, 1)) / self.total,
                        f"Step {self.step}/{self.total} · {self.label} ({done}/{total})")
        return None
