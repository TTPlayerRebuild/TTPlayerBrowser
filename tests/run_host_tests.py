"""Run the real PlayerWindow integration probe in fresh, isolated runtimes."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path, help='rebuild/build directory')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    runtime = build / 'Release'
    browser = build / 'ttpbrowser' / 'Release' / 'ttpbrowser.dll'
    results = []
    for mode in ('present', 'absent', 'incompatible'):
        fixture = output / mode
        # Never remove or reuse a populated runtime/configuration accidentally.
        fixture.mkdir(exist_ok=False)
        (fixture / '.host-probe-isolated').touch()
        for name in ('ttpbrowser_host_tests.exe', 'ttpres.dll', 'ttpcomm.dll'):
            shutil.copy2(runtime / name, fixture / name)
        if mode == 'present':
            shutil.copy2(browser, fixture / 'ttpbrowser.dll')
        elif mode == 'incompatible':
            # Valid x86 DLL with no ttp_browser_get_api export.
            shutil.copy2(runtime / 'ttpcomm.dll', fixture / 'ttpbrowser.dll')
        result = subprocess.run([
            str(fixture / 'ttpbrowser_host_tests.exe'), '--isolated',
            '--expect-present' if mode == 'present' else '--expect-absent',
            'http://127.0.0.1:9/index.html'
        ], cwd=fixture, capture_output=True, text=True, encoding='utf-8',
            errors='replace', timeout=30)
        (fixture / 'results.txt').write_text(result.stdout + result.stderr, encoding='utf-8')
        hashes = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                  for p in fixture.iterdir() if p.suffix.lower() in ('.dll', '.exe')}
        results.append(dict(mode=mode, exit_code=result.returncode, sha256=hashes))
        print(f'{mode}: exit {result.returncode}\n{result.stdout}{result.stderr}', flush=True)
    (output / 'results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    return int(any(result['exit_code'] != 0 for result in results))


if __name__ == '__main__':
    raise SystemExit(main())
