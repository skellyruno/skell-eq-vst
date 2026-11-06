"""Run source checks for the current Skelly EQ release."""
from pathlib import Path
import runpy
runpy.run_path(str(Path(__file__).with_name("check_v22.py")), run_name="__main__")
