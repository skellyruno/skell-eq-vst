"""Run the latest static sanity checks for Skelly EQ."""
from pathlib import Path
import runpy
runpy.run_path(str(Path(__file__).with_name("check_v20.py")), run_name="__main__")
