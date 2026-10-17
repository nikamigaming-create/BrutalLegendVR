"""Local troubleshooting logs and an explicit, bounded support ZIP export."""
from datetime import datetime, timezone
import hashlib
import json
import logging
from logging.handlers import RotatingFileHandler
import os
from pathlib import Path
import platform
import subprocess
import tempfile
import sys
import zipfile

FILE_LIMIT = 2 * 1024 * 1024
TOTAL_LIMIT = 24 * 1024 * 1024


def recent_files(folder, pattern, count):
    found = []
    for path in folder.glob(pattern):
        try:
            found.append((path.stat().st_mtime, path))
        except OSError:
            continue  # Another launcher may have just rotated/deleted this file.
    return [path for _, path in sorted(found, key=lambda item: item[0], reverse=True)[:count]]


def logs_dir():
    candidates = [Path(os.environ.get('LOCALAPPDATA') or tempfile.gettempdir()) /
                  'BrutalLegendVR/Logs', Path(tempfile.gettempdir()) / 'BrutalLegendVR/Logs']
    for path in candidates:
        try:
            path.mkdir(parents=True, exist_ok=True)
            # Check write access as well as the existence of the directory.
            with tempfile.TemporaryFile(dir=path):
                pass
            return path
        except OSError:
            continue
    raise OSError('Could not create a troubleshooting log in LocalAppData or TEMP.')


def start_logging(root):
    logger = logging.getLogger('blvr_setup')
    if logger.handlers:
        return logger
    folder = logs_dir()
    path = folder / f'launcher-{datetime.now():%Y%m%d-%H%M%S}-{os.getpid()}.log'
    # Keep a small recent history. Logging must also work on a failed install.
    for old in recent_files(folder, 'launcher-*.log*', 1000)[9:]:
        try:
            old.unlink()
        except OSError:
            pass
    handler = RotatingFileHandler(path, maxBytes=1024 * 1024, backupCount=1, encoding='utf-8')
    handler.setFormatter(logging.Formatter('%(asctime)s %(levelname)s %(message)s'))
    logger.addHandler(handler)
    logger.setLevel(logging.INFO)
    logger.propagate = False
    logger.info('Launcher started; app=%s; Windows=%s; frozen=%s', root,
                platform.platform(), getattr(sys, 'frozen', False))
    return logger


def log_output(logger, label, value):
    if isinstance(value, bytes):
        value = value.decode('utf-8', errors='replace')
    if value:
        logger.info('%s:\n%s', label, value)


def _hash(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def system_info():
    result = {'windows': platform.platform(), 'architecture': platform.machine()}
    if os.name == 'nt':
        shell = Path(os.environ['SystemRoot']) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
        try:
            output = subprocess.run([str(shell), '-NoProfile', '-Command',
                'Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion,Status | ConvertTo-Json'],
                capture_output=True, timeout=15, text=True, encoding='utf-8', errors='replace',
                creationflags=subprocess.CREATE_NO_WINDOW)
            result['graphics'] = output.stdout.strip() or output.stderr.strip()
        except (OSError, subprocess.TimeoutExpired) as error:
            result['graphics_error'] = str(error)
    return result


def export_support(root, game, destination):
    """Copy only known logs/configuration; never scan or copy owned game assets."""
    from blvr_settings import runtimes
    root, destination = Path(root), Path(destination)
    game = Path(game) if game else None
    report = {'created_utc': datetime.now(timezone.utc).isoformat(), 'app_directory': str(root),
              'game_directory': str(game) if game else '', 'system': system_info(),
              'openxr_runtime_candidates': runtimes(), 'files': [], 'binaries': {}}
    candidates = [(root / name, 'app/' + name) for name in
                  ('settings.json', 'controls.ini', 'guitar-placement.txt', 'SHA256SUMS.txt')]
    candidates += [(root / 'tools' / name, 'host/' + name) for name in
                   ('blvr_xr_host.log', 'blvr_setup-error.log')]
    if game:
        candidates += [(game / name, 'game/' + name) for name in
                       ('blvr.log', 'blvr_input.log', 'blvr-install.json', 'BrutalLegend_d3d9.log')]
    folder = logs_dir()
    # Exact product-specific patterns avoid unrelated installer or system logs.
    for pattern in ('launcher-*.log*', 'preflight-*.log', 'installer-*.log*', 'uninstaller-*.log*'):
        candidates += [(p, 'launcher/' + p.name) for p in
                       recent_files(folder, pattern, 10)]
    candidates.append((folder / 'blvr_telemetry.jsonl', 'launcher/blvr_telemetry.jsonl'))
    for location, prefix in [(folder, 'launcher'), (root / 'tools', 'host')] + ([(game, 'game')] if game else []):
        for pattern in ('blvr-*-perf-*.csv', 'blvr-*-gpu-*.csv'):
            candidates += [(p, prefix + '/' + p.name) for p in
                           recent_files(location, pattern, 2)]
    for base, names in [(root, ('tools/blvr_setup.exe', 'tools/blvr_xr_host.exe',
                               'bin/d3d9.dll', 'bin/openxr_loader.dll', 'bin/dxvk_d3d9.dll'))] + (
            [(game, ('BrutalLegend.exe', 'd3d9.dll', 'openxr_loader.dll', 'dxvk_d3d9.dll'))] if game else []):
        for name in names:
            key = ('game/' if base == game else 'app/') + name
            try:
                path = base / name
                report['binaries'][key] = {'bytes': path.stat().st_size, 'sha256': _hash(path)}
            except OSError as error:
                report['binaries'][key] = {'error': str(error)}
    temporary = destination.with_name(destination.name + f'.{os.getpid()}.tmp')
    remaining = TOTAL_LIMIT
    try:
        with zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED) as bundle:
            for path, name in candidates:
                entry = {'name': name}
                report['files'].append(entry)
                try:
                    with path.open('rb') as stream:
                        size = stream.seek(0, 2)
                        count = min(size, FILE_LIMIT, remaining)
                        stream.seek(size - count)
                        data = stream.read(count)
                    entry.update(bytes=size, included_bytes=len(data), truncated=len(data) < size,
                                 modified_utc=datetime.fromtimestamp(path.stat().st_mtime, timezone.utc).isoformat())
                    bundle.writestr(name, data)
                    remaining -= len(data)
                except OSError as error:
                    entry['error'] = str(error)
            bundle.writestr('report.json', json.dumps(report, indent=2))
            bundle.writestr('READ-ME.txt',
                'BLVR support report. Review before sharing: logs and settings can contain your Windows username and paths.\n'
                'No game assets, save games, recordings or environment dump are included. Nothing was uploaded.\n'
                'Read report.json for timestamps, missing/unreadable files, binary hashes and truncated log tails.\n')
        os.replace(temporary, destination)
    finally:
        temporary.unlink(missing_ok=True)
    return destination
