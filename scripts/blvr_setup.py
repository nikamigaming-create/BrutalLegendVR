"""First-run owned-model import and live controller remapping for BLVR."""
import argparse
import configparser
import hashlib
import json
import os
from pathlib import Path
import queue
import re
import subprocess
import sys
import threading

SUPPORTED_EXE = '872dc676e8fd77ad3351dd9dfcc99e89353aae0ed9857272a65fd47f298fb0b1'
INPUTS = ('a b x y left_trigger right_trigger left_grip right_grip '
          'left_stick_click right_stick_click menu left_up left_down left_left '
          'left_right right_up right_down right_left right_right none').split()
STICKS = {'movement_stick', 'turn_stick', 'radial_stick', 'opening_menu_stick'}
INPUT_LABELS = {
    'a': 'A · right controller', 'b': 'B · right controller',
    'x': 'X · left controller', 'y': 'Y · left controller',
    'menu': 'Menu · left controller', 'none': 'Unbound',
    **{key: key.replace('_', ' ').capitalize() for key in INPUTS
       if key not in {'a', 'b', 'x', 'y', 'menu', 'none'}},
    'left': 'Left stick', 'right': 'Right stick',
    **{f'{side}_{direction}': f'{side.capitalize()} stick {direction}'
       for side in ('left', 'right') for direction in ('up', 'down', 'left', 'right')},
}
CONTROL_HELP = {
    'uia': 'Select or confirm a native menu item.',
    'uib': 'Go back or cancel in native menus.',
    'uix': 'Alternate native menu action, including Watch Tutorial in stage-battle setup. Keep separate from Select to avoid activating both.',
    'uiy': 'Alternate native menu action shown in that menu.',
    'uistart': 'Native menu Start action; also used to pause where the game permits it.',
    'uiback': 'Native menu Back action while the command chord is held by default.',
    'uiup': 'Move upward in native menus.', 'uidown': 'Move downward in native menus.',
    'uileft': 'Move left in native menus, including the opening carousel. Separate from the stick that turns the 3D room.',
    'uiright': 'Move right in native menus, including the opening carousel. Separate from the stick that turns the 3D room.',
    'uitriggerleft': 'Native left-trigger menu action, such as changing a page. The menu determines its function.',
    'uitriggerright': 'Native right-trigger menu action, such as changing a page. The menu determines its function.',
    'uishoulderleft': 'Native left-shoulder menu action, such as changing a tab. The menu determines its function.',
    'uishoulderright': 'Native right-shoulder menu action, such as changing a tab. The menu determines its function.',
    'accept': 'Accept a dialog or selection when that game context permits it.',
    'cancel': 'Cancel a dialog or selection when that game context permits it.',
    'use': 'Interact, activate or enter/exit a vehicle. Also requests build-wheel research upgrades with an input not required by the held build chord; Build research alternate supplies a separate upgrade input.',
    'map': 'Open the map.', 'journal': 'Pause or open the journal.',
    'command_grip': 'Hold this with the command-click input to issue orders, fly or open command menus.',
    'command_click': 'Hold this with the command-grip input. The chord suppresses combat and ordinary interactions.',
    'equip_axe': 'Select the axe for your right hand. The Axe action controls button attacks.',
    'equip_guitar': 'Select the guitar for your left hand. The Guitar action controls button attacks.',
    'earthshaker_left': 'Hold this together with the other Earthshaker input. Release both to prepare another slam.',
    'earthshaker_right': 'Hold this together with the other Earthshaker input. Solos, vehicles and command mode suppress the slam.',
    'wheel_grip': 'Hold to grab the steering wheel with your right hand. The movement stick can override hand steering.',
    'command_boost': 'Vehicle boost while the command chord is held.',
    'recenter_modifier': 'Hold this with the recenter-click input to reset your view. The chord suppresses game input.',
    'recenter_click': 'Hold this with the recenter-modifier input to reset your view.',
    'support_left': 'Grip input for the left support hand.', 'support_right': 'Grip input for the right support hand.',
    'solo_fret': 'Hold while strumming a solo. The live guitar legend explains remapped A/X/Y note inputs.',
    'solo_accept_alternate': 'Alternative confirmation for solo and held stage build wheels. For build confirmation, choose an input not required by the held build chord.',
    'build_research_alternate': 'Research/upgrade input only while the native build wheel is open and held. Defaults to left trigger; choose an input not required by the held build chord.',
    'opening_confirm_alternate': 'Alternative input for confirming the opening menu.',
    'movement_stick': 'Walking and vehicle steering stick.', 'turn_stick': 'Snap-turn stick, including horizontal room turning in the opening. Native carousel navigation follows UiLeft/UiRight separately.',
    'radial_stick': 'Selection stick for solo and stage build wheels; also switches targets.',
    'opening_menu_stick': 'Vertical selection stick for the opening menu. Horizontal native carousel choices follow UiLeft/UiRight; room turning follows Turn stick.',
    'axe': 'Button attack with the selected axe. Physical axe swings also attack.',
    'guitar': 'Button attack with the selected guitar. Physical strumming also attacks.',
    'evade': 'Evade or roll during combat.', 'block': 'Block during combat.',
    'target': 'Hold native targeting; the radial stick switches targets.',
    'solonote1': 'Input for the A note artwork during a solo.',
    'solonote2': 'Input for the X note artwork during a solo.',
    'solonote3': 'Input for the Y note artwork during a solo.',
    'rockstance': 'Open or close the solo-selection wheel.',
    'radialaccept': 'Confirm a solo or stage build selection. In a held build wheel, choose an input not required by the held build chord.',
    'ordercharge': 'Send units to charge.', 'orderdefend': 'Tell units to defend.',
    'ordermove': 'Send units to the beacon.', 'orderfollow': 'Tell units to follow you.',
    'buildmenu': 'Hold to keep the stage build wheel open. Choose with the radial stick, then confirm while still holding. Releasing closes the wheel.',
    'fly': 'Toggle stage-battle flight.',
    'beacon': 'Place the stage-battle command beacon.',
    'cancelbuilditem': 'Cancel a queued unit while holding the stage build wheel. Choose an input not required by the held build chord.',
    'ascend': 'Move up while flying.', 'descend': 'Move down while flying.',
    'handbrake': 'Vehicle handbrake.', 'boost': 'Sprint or vehicle boost, according to the game context.',
    'alternatecam': 'Change the native camera where that game mode permits it.',
    'primaryvehicleattack': 'Vehicle throttle / primary attack input.',
    'secondaryvehicleattack': 'Vehicle brake / secondary attack input.',
    'leftcoopattack': 'Native left co-op attack input where the game permits it.',
    'rightcoopattack': 'Native right co-op attack input where the game permits it.',
    'playlistui': 'Show the vehicle playlist interface.',
    'playlisttoggle': 'Toggle playlist playback.',
    'playlistnext': 'Play the next playlist track.', 'playlistprev': 'Play the previous playlist track.',
    'playlistrewind': 'Rewind playlist playback.',
}


def project_root():
    source = Path(sys.executable) if getattr(sys, 'frozen', False) else Path(__file__)
    return source.resolve().parent.parent


def validate_game(game):
    game = Path(game).resolve()
    if game.is_file():
        game = game.parent
    exe = game / 'BrutalLegend.exe'
    if not exe.is_file() or not (game / 'Win/Packs').is_dir():
        raise ValueError('Select the installed PC game folder containing BrutalLegend.exe and Win/Packs.')
    if hashlib.sha256(exe.read_bytes()).hexdigest() != SUPPORTED_EXE:
        raise ValueError('This preview supports the tested Steam PC executable only. The selected executable differs; no hooks were installed.')
    return game


def atomic_text(path, text):
    temporary = path.with_name(path.name + f'.{os.getpid()}.tmp')
    temporary.write_text(text, encoding='utf-8')
    os.replace(temporary, path)


def prepare(root, game, progress=print):
    from import_eddie_assets import import_owned
    from build_eddie_rig import build
    game = validate_game(game)
    progress('Importing the model, skeleton and textures from your installation...')
    import_owned(game, root / 'artifacts/eddie-assets', progress)
    progress('Preparing tracked hands and equipment...')
    build(root / 'artifacts/eddie-assets', root / 'artifacts/eddie-rig')
    if not (root / 'artifacts/eddie-rig/eddie.rigcache').is_file():
        raise ValueError('Model preparation did not produce a rig.')
    settings = {'game_dir': str(game), 'executable_sha256': SUPPORTED_EXE}
    path = root / 'settings.json'
    if path.is_file():
        try:
            previous = json.loads(path.read_text(encoding='utf-8-sig'))
            if isinstance(previous, dict):
                settings = {**previous, **settings}
        except ValueError:
            pass  # Preparing the selected installation repairs malformed settings.
    atomic_text(path, json.dumps(settings, indent=2) + '\n')
    progress('Ready. Start Play VR.cmd. Animations use the running game; no motion recording is required.')


def find_game(root):
    saved = root / 'settings.json'
    if saved.is_file():
        try:
            game = json.loads(saved.read_text(encoding='utf-8-sig')).get('game_dir', '')
            if isinstance(game, str) and (Path(game) / 'BrutalLegend.exe').is_file():
                return game
        except (OSError, ValueError, AttributeError):
            pass  # A stale setup file must not prevent opening the controls editor.
    try:
        import winreg
    except ImportError:
        return ''
    libraries = []
    for hive, key, value in [(winreg.HKEY_CURRENT_USER, r'Software\Valve\Steam', 'SteamPath'),
                             (winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\WOW6432Node\Valve\Steam', 'InstallPath')]:
        try:
            with winreg.OpenKey(hive, key) as handle:
                libraries.append(Path(winreg.QueryValueEx(handle, value)[0]))
        except OSError:
            pass
    for steam in libraries.copy():
        vdf = steam / 'steamapps/libraryfolders.vdf'
        if vdf.is_file():
            try:
                libraries.extend(Path(p.replace('\\\\', '\\')) for p in re.findall(r'"path"\s+"([^"]+)"', vdf.read_text(encoding='utf-8')))
            except (OSError, UnicodeError):
                pass
    for library in libraries:
        game = library / 'steamapps/common/BrutalLegend'
        if (game / 'BrutalLegend.exe').is_file():
            return str(game)
    return ''


def controls_config(path):
    config = configparser.ConfigParser(interpolation=None)
    if not path.is_file():
        return config
    section = ''
    # Match the native parser's line order and last-assignment semantics,
    # including mixed-case/repeated sections and comments without a space.
    for number, source in enumerate(path.read_text(encoding='utf-8-sig').splitlines(), 1):
        line = re.split(r'[;#]', source, maxsplit=1)[0].strip().lower()
        if not line:
            continue
        if line.startswith('[') and line.endswith(']'):
            section = line[1:-1].strip()
            if section not in ('actions', 'controls'):
                raise configparser.ParsingError(str(path))
            if not config.has_section(section):
                config.add_section(section)
            continue
        if '=' not in line or not section:
            error = configparser.ParsingError(str(path)); error.append(number, source)
            raise error
        key, value = (part.strip() for part in line.split('=', 1))
        if value.startswith('command+'):
            value = 'command+' + value[8:].strip()
        config.set(section, key, value)
    return config


def control_snapshot(config):
    return tuple((section, tuple(sorted(config.items(section)))) for section in sorted(config.sections()))


def stage_control(config, section, key, value, require_command=False):
    allowed = ('left', 'right') if key in STICKS else INPUTS
    if value not in allowed:
        raise ValueError('Choose one of the available controller inputs.')
    config.set(section, key, ('command+' if section == 'actions' and require_command else '') + value)


def display_binding(value):
    chord = value.startswith('command+')
    key = value.removeprefix('command+')
    return ('Command chord + ' if chord else '') + INPUT_LABELS.get(key, key)


def save_controls(root, config):
    import io
    stream = io.StringIO()
    stream.write('; Saved changes reload during play within 250 ms.\n')
    config.write(stream)
    temporary = root / f'controls.{os.getpid()}.tmp'
    temporary.write_text(stream.getvalue(), encoding='utf-8')
    env = {**os.environ, 'BLVR_CONTROLS_FILE': str(temporary)}
    host = root / 'tools/blvr_xr_host.exe'
    try:
        checked = subprocess.run([str(host), '--check-controls'], env=env, capture_output=True,
                                 text=True, timeout=10, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        if checked.returncode:
            raise ValueError(checked.stderr.strip() or checked.stdout.strip() or 'The controls file was rejected.')
        os.replace(temporary, root / 'controls.ini')
    finally:
        temporary.unlink(missing_ok=True)


def gui(root, controls_only=False):
    import tkinter as tk
    from tkinter import ttk, filedialog, messagebox
    window = tk.Tk()
    window.title('Brütal Legend VR — Setup and Controls')
    window.geometry('860x720')
    window.minsize(760, 650)
    tabs = ttk.Notebook(window)
    tabs.pack(fill='both', expand=True, padx=12, pady=12)
    setup = ttk.Frame(tabs, padding=14)
    controls = ttk.Frame(tabs, padding=14)
    tabs.add(setup, text='Game setup')
    tabs.add(controls, text='Controls')
    events = queue.Queue()
    game_var = tk.StringVar(value=find_game(root))
    ttk.Label(setup, text='Choose your installed Steam PC copy of Brütal Legend.').pack(anchor='w')
    line = ttk.Frame(setup)
    line.pack(fill='x', pady=12)
    ttk.Entry(line, textvariable=game_var).pack(side='left', fill='x', expand=True)

    def browse():
        selected = filedialog.askopenfilename(title='Select BrutalLegend.exe', filetypes=[('Brütal Legend', 'BrutalLegend.exe')])
        if selected:
            game_var.set(str(Path(selected).parent))
    ttk.Button(line, text='Browse…', command=browse).pack(side='right', padx=8)
    ttk.Label(setup, text='Setup imports your model and textures locally. Animation comes from the live game.\n'
              'After setup, connect your headset and start Play VR.cmd.', wraplength=780).pack(anchor='w', pady=10)
    status = tk.StringVar(value='Ready to prepare your installation.')
    ttk.Label(setup, textvariable=status, wraplength=780).pack(anchor='w', pady=16)

    def start_prepare():
        try:
            game = validate_game(game_var.get())
        except Exception as error:
            messagebox.showerror('Setup', str(error)); return
        prepare_button.configure(state='disabled')
        def worker():
            try:
                prepare(root, game, lambda text: events.put(('progress', text)))
                events.put(('done', 'Preparation complete. Start Play VR.cmd.'))
            except Exception as error:
                events.put(('error', str(error)))
        threading.Thread(target=worker, daemon=True).start()
    prepare_button = ttk.Button(setup, text='Prepare game', command=start_prepare)
    prepare_button.pack(anchor='w')

    defaults = controls_config(root / 'assets/ui/controls-defaults.ini')
    try:
        current = controls_config(root / 'controls.ini')
    except (configparser.Error, OSError, UnicodeError):
        current = controls_config(root / 'assets/ui/controls-defaults.ini')
        ttk.Label(controls, text='The existing file could not be read. Defaults are loaded; Save repairs the file.').pack(anchor='w')
    labels = {}
    for line in (root / 'assets/ui/controls-defaults.ini').read_text(encoding='utf-8-sig').splitlines():
        if '=' in line and not line.startswith(';'):
            key = line.split('=', 1)[0].strip()
            label = re.sub(r'(?<=[a-z0-9])(?=[A-Z])|(?<=[a-z])(?=[0-9])', ' ', key)
            label = label.replace('_', ' ').replace('Ui ', 'Menu ')
            labels[key.lower()] = label.capitalize() if key.islower() else label
    for section in defaults.sections():
        if not current.has_section(section):
            current.add_section(section)
        for key, value in defaults.items(section):
            if not current.has_option(section, key):
                current.set(section, key, value)
    saved_snapshot = control_snapshot(current)
    ttk.Label(controls, text='Select an action, choose its input, then Save controls. Row changes stay pending as you browse.\n'
              'Saved changes apply while playing. Menu / combat / command contexts still apply.', wraplength=790).pack(anchor='w')
    filter_line = ttk.Frame(controls); filter_line.pack(fill='x', pady=(10, 0))
    ttk.Label(filter_line, text='Find a control:').pack(side='left')
    filter_var = tk.StringVar()
    ttk.Entry(filter_line, textvariable=filter_var).pack(side='left', fill='x', expand=True, padx=8)
    ttk.Button(filter_line, text='Clear', command=lambda: filter_var.set('')).pack(side='right')
    box = ttk.Frame(controls)
    box.pack(fill='both', expand=True, pady=12)
    tree = ttk.Treeview(box, columns=('binding',), show='tree headings', selectmode='browse')
    tree.heading('#0', text='Action / control'); tree.heading('binding', text='Binding')
    tree.column('#0', width=330); tree.column('binding', width=350)
    scroll = ttk.Scrollbar(box, command=tree.yview)
    tree.configure(yscrollcommand=scroll.set)
    tree.pack(side='left', fill='both', expand=True); scroll.pack(side='right', fill='y')
    rows = {}

    def populate(*_):
        tree.delete(*tree.get_children()); rows.clear()
        query = filter_var.get().lower().strip()
        for section in ('actions', 'controls'):
            parent = tree.insert('', 'end', text='Game actions' if section == 'actions' else 'VR controls', open=True)
            for key, value in current.items(section):
                label = labels.get(key, key)
                if query and query not in ' '.join((label, key.replace('_', ' '), value, display_binding(value), CONTROL_HELP.get(key, ''))).lower():
                    continue
                row = tree.insert(parent, 'end', text=label, values=(display_binding(value),)); rows[row] = (section, key)
    populate()
    edit = ttk.Frame(controls); edit.pack(fill='x')
    selected = tk.StringVar(value='Select a row')
    value_var = tk.StringVar()
    command = tk.BooleanVar()
    ttk.Label(edit, textvariable=selected, width=28).pack(side='left')
    chooser = ttk.Combobox(edit, textvariable=value_var, values=[INPUT_LABELS[key] for key in INPUTS], state='disabled', width=25)
    chooser.pack(side='left', padx=8)
    command_box = ttk.Checkbutton(edit, text='Require command chord', variable=command, state='disabled')
    command_box.pack(side='left')
    help_text = tk.StringVar(value='Every game action and VR control can be remapped. Select a row to see what it does.')
    ttk.Label(controls, textvariable=help_text, wraplength=790).pack(anchor='w', pady=(10, 0))
    chords_text = tk.StringVar()
    ttk.Label(controls, textvariable=chords_text, wraplength=790).pack(anchor='w', pady=8)
    controls_status = tk.StringVar()
    ttk.Label(controls, textvariable=controls_status, wraplength=790).pack(anchor='w')

    def update_status(saved=False):
        dirty = control_snapshot(current) != saved_snapshot
        controls_status.set('Unsaved changes — Save controls to apply them during play.' if dirty else
            'Saved. Changes reload within 250 ms; release held buttons before continuing.' if saved else
            '51 game actions + 20 VR controls. The saved layout is active during play.')
        grip = current['controls']['command_grip']; click = current['controls']['command_click']
        left = current['controls']['earthshaker_left']; right = current['controls']['earthshaker_right']
        chords_text.set(f'Command chord: {display_binding(grip)} + {display_binding(click)}.\n'
            f'Earthshaker: {display_binding(left)} + {display_binding(right)} together.' +
            (' Both entries use one input, so a single press can trigger it.' if left == right and left != 'none' else
             ' An unbound entry disables the slam.' if 'none' in (left, right) else ''))
    update_status()

    def select(_=None):
        choice = tree.selection()
        if not choice or choice[0] not in rows:
            selected.set('Select a row'); value_var.set('')
            chooser.configure(state='disabled'); command_box.configure(state='disabled')
            return
        section, key = rows[choice[0]]
        value = current[section][key]
        selected.set(labels.get(key, key)); command.set(value.startswith('command+'))
        value_var.set(INPUT_LABELS.get(value.removeprefix('command+'), value.removeprefix('command+')))
        chooser.configure(values=[INPUT_LABELS[input] for input in (('left', 'right') if key in STICKS else INPUTS)], state='readonly')
        command_box.configure(state='normal' if section == 'actions' else 'disabled')
        help_text.set(CONTROL_HELP.get(key, 'Native game action. Menu, combat, vehicle and command contexts still apply.'))
    tree.bind('<<TreeviewSelect>>', select)

    def apply_row(_=None):
        choice = tree.selection()
        if not choice or choice[0] not in rows or not value_var.get():
            return
        section, key = rows[choice[0]]
        key_by_label = {label: key for key, label in INPUT_LABELS.items()}
        value = key_by_label.get(value_var.get())
        if value is None:  # An invalid hand-edited entry remains visible until repaired.
            return
        stage_control(current, section, key, value, command.get())
        tree.item(choice[0], values=(display_binding(current[section][key]),))
        update_status()
    chooser.bind('<<ComboboxSelected>>', apply_row)
    command_box.configure(command=apply_row)
    filter_var.trace_add('write', populate)
    footer = ttk.Frame(controls); footer.pack(fill='x', pady=8)

    def save():
        nonlocal saved_snapshot
        apply_row()
        try:
            save_controls(root, current)
            saved_snapshot = control_snapshot(current); update_status(saved=True)
        except Exception as error:
            messagebox.showerror('Controls', str(error))
    def reset():
        nonlocal current
        current = controls_config(root / 'assets/ui/controls-defaults.ini'); populate()
        selected.set('Select a row'); value_var.set('')
        chooser.configure(state='disabled'); command_box.configure(state='disabled'); update_status()
    ttk.Button(footer, text='Save controls', command=save).pack(side='left')
    ttk.Button(footer, text='Restore defaults', command=reset).pack(side='left', padx=12)
    if controls_only:
        tabs.select(controls)

    def poll():
        while not events.empty():
            kind, text = events.get_nowait(); status.set(text)
            if kind in ('done', 'error'):
                prepare_button.configure(state='normal')
            if kind == 'error':
                messagebox.showerror('Setup', text)
        window.after(100, poll)
    poll(); window.mainloop()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path)
    parser.add_argument('--prepare', action='store_true')
    parser.add_argument('--game-dir', type=Path)
    parser.add_argument('--controls', action='store_true')
    args = parser.parse_args()
    root = (args.root or project_root()).resolve()
    if args.prepare:
        game = args.game_dir or find_game(root)
        if not game:
            raise ValueError('Open Setup VR.cmd and select your game installation first.')
        prepare(root, game)
    else:
        gui(root, args.controls)


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        if sys.stderr:
            print(f'BLVR setup failed: {error}', file=sys.stderr)
        elif '--prepare' in sys.argv:
            project_root().joinpath('tools/blvr_setup-error.log').write_text(str(error), encoding='utf-8')
        else:
            import tkinter.messagebox
            tkinter.messagebox.showerror('BLVR setup', str(error))
        sys.exit(1)
