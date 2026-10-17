"""Film the real launcher with example paths, without opening personal data."""
from pathlib import Path
from unittest.mock import patch
import tkinter.filedialog as dialogs

import blvr_setup

ROOT = Path(__file__).resolve().parents[1]
DEMO = ROOT / 'artifacts/tutorial-demo'
DEMO.mkdir(parents=True, exist_ok=True)
for name in ('assets/ui/controls-defaults.ini', 'assets/installer/guitar-workshop.png'):
    target = DEMO / name
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes((ROOT / name).read_bytes())
(DEMO / 'support-exports').mkdir(exist_ok=True)
native_save = dialogs.asksaveasfilename


def clean_save(**options):
    return native_save(**{**options, 'initialdir': str(DEMO / 'support-exports')})


if __name__ == '__main__':
    # Only discovery and the initial file-dialog directory are substituted.
    # The visible controls/settings and save dialog are the production UI.
    with patch.object(blvr_setup, 'find_game', return_value=r'C:\Games\BrutalLegend'), \
         patch.object(dialogs, 'asksaveasfilename', side_effect=clean_save):
        blvr_setup.gui(DEMO)
