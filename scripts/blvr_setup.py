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
        settings = {**json.loads(path.read_text(encoding='utf-8-sig')), **settings}
    atomic_text(path, json.dumps(settings, indent=2) + '\n')
    progress('Ready. Start Play VR.cmd. Animations use the running game; no motion recording is required.')


def find_game(root):
    saved = root / 'settings.json'
    if saved.is_file():
        return json.loads(saved.read_text(encoding='utf-8-sig')).get('game_dir', '')
    import winreg
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
            libraries.extend(Path(p.replace('\\\\', '\\')) for p in re.findall(r'"path"\s+"([^"]+)"', vdf.read_text(encoding='utf-8')))
    for library in libraries:
        game = library / 'steamapps/common/BrutalLegend'
        if (game / 'BrutalLegend.exe').is_file():
            return str(game)
    return ''


def controls_config(path):
    raw = configparser.ConfigParser(interpolation=None, strict=False, inline_comment_prefixes=(';', '#'))
    raw.read(path, encoding='utf-8-sig')
    config = configparser.ConfigParser(interpolation=None)
    for section in raw.sections():
        canonical = section.lower()
        if not config.has_section(canonical):
            config.add_section(canonical)
        for key, value in raw.items(section):
            config.set(canonical, key, value)
    return config


def save_controls(root, config):
    import io
    stream = io.StringIO()
    stream.write('; Saved changes reload during play within 250 ms.\n')
    config.write(stream)
    temporary = root / f'controls.{os.getpid()}.tmp'
    temporary.write_text(stream.getvalue(), encoding='utf-8')
    env = {**os.environ, 'BLVR_CONTROLS_FILE': str(temporary)}
    host = root / 'tools/blvr_xr_host.exe'
    checked = subprocess.run([str(host), '--check-controls'], env=env, capture_output=True,
                             text=True, creationflags=subprocess.CREATE_NO_WINDOW)
    if checked.returncode:
        temporary.unlink()
        raise ValueError(checked.stderr.strip() or checked.stdout.strip() or 'The controls file was rejected.')
    os.replace(temporary, root / 'controls.ini')


def gui(root, controls_only=False):
    import tkinter as tk
    from tkinter import ttk, filedialog, messagebox
    window = tk.Tk()
    window.title('Brütal Legend VR — Setup and Controls')
    window.geometry('860x650')
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
    except configparser.Error:
        current = controls_config(root / 'assets/ui/controls-defaults.ini')
        ttk.Label(controls, text='The existing file could not be read. Defaults are loaded; Save repairs the file.').pack(anchor='w')
    labels = {}
    for line in (root / 'assets/ui/controls-defaults.ini').read_text(encoding='utf-8-sig').splitlines():
        if '=' in line and not line.startswith(';'):
            key = line.split('=', 1)[0].strip()
            label = re.sub(r'(?<=[a-z0-9])(?=[A-Z])|(?<=[a-z])(?=[0-9])', ' ', key)
            labels[key.lower()] = label.replace('_', ' ').replace('Ui ', 'Menu ')
    for section in defaults.sections():
        if not current.has_section(section):
            current.add_section(section)
        for key, value in defaults.items(section):
            if not current.has_option(section, key):
                current.set(section, key, value)
    ttk.Label(controls, text='Select any action or VR control, change its input, then Save.\n'
              'Saved changes apply while playing. Menu / combat / command contexts still apply.', wraplength=790).pack(anchor='w')
    box = ttk.Frame(controls)
    box.pack(fill='both', expand=True, pady=12)
    tree = ttk.Treeview(box, columns=('binding',), show='tree headings', selectmode='browse')
    tree.heading('#0', text='Action / control'); tree.heading('binding', text='Binding')
    tree.column('#0', width=330); tree.column('binding', width=350)
    scroll = ttk.Scrollbar(box, command=tree.yview)
    tree.configure(yscrollcommand=scroll.set)
    tree.pack(side='left', fill='both', expand=True); scroll.pack(side='right', fill='y')
    rows = {}

    def populate():
        tree.delete(*tree.get_children()); rows.clear()
        for section in ('actions', 'controls'):
            parent = tree.insert('', 'end', text='Game actions' if section == 'actions' else 'VR controls', open=True)
            for key, value in current.items(section):
                row = tree.insert(parent, 'end', text=labels.get(key, key), values=(value,)); rows[row] = (section, key)
    populate()
    edit = ttk.Frame(controls); edit.pack(fill='x')
    selected = tk.StringVar(value='Select a row')
    value_var = tk.StringVar()
    command = tk.BooleanVar()
    ttk.Label(edit, textvariable=selected, width=28).pack(side='left')
    chooser = ttk.Combobox(edit, textvariable=value_var, values=INPUTS, state='readonly', width=23)
    chooser.pack(side='left', padx=8)
    command_box = ttk.Checkbutton(edit, text='Require command chord', variable=command)
    command_box.pack(side='left')

    def select(_=None):
        choice = tree.selection()
        if not choice or choice[0] not in rows:
            return
        section, key = rows[choice[0]]
        value = current[section][key]
        selected.set(labels.get(key, key)); command.set(value.startswith('command+'))
        value_var.set(value.removeprefix('command+'))
        chooser.configure(values=('left', 'right') if key in STICKS else INPUTS)
        command_box.configure(state='normal' if section == 'actions' else 'disabled')
    tree.bind('<<TreeviewSelect>>', select)

    def apply_row():
        choice = tree.selection()
        if not choice or choice[0] not in rows or not value_var.get():
            return
        section, key = rows[choice[0]]
        value = ('command+' if command.get() and section == 'actions' else '') + value_var.get()
        current[section][key] = value
        tree.item(choice[0], values=(value,))
    ttk.Button(controls, text='Apply to selected row', command=apply_row).pack(anchor='w', pady=8)
    footer = ttk.Frame(controls); footer.pack(fill='x', pady=8)

    def save():
        apply_row()
        try:
            save_controls(root, current)
            messagebox.showinfo('Controls saved', 'Bindings will update during play within 250 ms. Release held buttons before continuing.')
        except Exception as error:
            messagebox.showerror('Controls', str(error))
    def reset():
        nonlocal current
        current = controls_config(root / 'assets/ui/controls-defaults.ini'); populate()
        selected.set('Select a row'); value_var.set('')
    ttk.Button(footer, text='Save controls', command=save).pack(side='left')
    ttk.Button(footer, text='Restore defaults', command=reset).pack(side='left', padx=12)
    ttk.Label(footer, text='Earthshaker defaults to left grip + right grip.').pack(side='right')
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
