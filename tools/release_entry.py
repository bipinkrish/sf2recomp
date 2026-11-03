"""Bootstrap imports even when the bundled interpreter runs in isolated mode."""
from pathlib import Path
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
from release import main

if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"SF2Recomp: {error}", file=sys.stderr)
        sys.exit(1)
