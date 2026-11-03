"""Expand subsystem constants when a test compiles a production source slice."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from centralize_literals import expanded


def read_source(path):
    return expanded(path)
