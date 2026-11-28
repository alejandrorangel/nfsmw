#!/usr/bin/env python3
"""Career save to disk and back: the career-save benchmark scenario's phases.

    python3 port/tools/career_save_test.py [--keep DIR] [--screens]
    python3 port/tools/career_save_test.py --rival        # the rival flow's autosave instead

Phase "save" boots with an empty scratch profile folder (NFSMW_SAVE_DIR),
creates a profile, starts a career, wins an event and lets the game autosave,
writing the career's state; phase "load" boots again, loads the profile and
checks it (saved again byte for byte, the career's state after the load and in
free roam); phase "menu-save" loads it, changes the career (cash) in the
safehouse's career menu and saves it with the menu's Save (the memory card
screens), and a last "load" checks what that save wrote. --rival runs the
career-rival scenario with profiles on instead (a new profile, the boss races,
the rival flow's autosave, MCE_RivalFlow, into the next bin), then "load".
Never touches the player's own profiles.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def run_phase(binary, data, work, phase, screens, timeout, label, scenario='career-save'):
    env = dict(os.environ)
    for key in list(env):
        if key.startswith('NFSMW_BENCHMARK') or key.startswith('NFSMW_TRACE') or key.startswith('NFSMW_AUTO'):
            del env[key]
    env.update({
        'NFSMW_DATA': data,
        'NFSMW_BENCHMARK': scenario,
        'NFSMW_BENCHMARK_PHASE': phase,
        'NFSMW_BENCHMARK_OUT': os.path.join(work, 'out'),
        'NFSMW_SAVE_DIR': os.path.join(work, 'saves'),
        'NFSMW_VIDEO_CONFIG': os.path.join(work, 'options.cfg'),
    })
    if screens:
        env['NFSMW_BENCHMARK_SCREENS'] = '1'
    log_path = os.path.join(work, label + '.log')
    with open(log_path, 'w') as log:
        try:
            code = subprocess.run([binary], env=env, stdout=log, stderr=subprocess.STDOUT, timeout=timeout).returncode
        except subprocess.TimeoutExpired:
            code = 'timeout'
    subprocess.run(['pkill', '-f', '^' + binary], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    lines = [l.rstrip() for l in open(log_path, errors='replace') if l.startswith('[benchmark]')]
    return code, lines


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--bin', default=os.path.join(ROOT, 'build/port/NFSMW.app/Contents/MacOS/NFSMW'))
    parser.add_argument('--data', default=os.path.expanduser('~/Games/NFSMW'))
    parser.add_argument('--keep', help='work folder to keep (logs, saves, state, screenshots)')
    parser.add_argument('--screens', action='store_true', help='a screenshot of every screen (NFSMW_BENCHMARK_SCREENS)')
    parser.add_argument('--timeout', type=int, default=900)
    parser.add_argument('--rival', action='store_true', help="career-rival with profiles (the rival flow's autosave), then load")
    args = parser.parse_args()
    binary = os.path.abspath(args.bin)
    work = os.path.abspath(args.keep) if args.keep else tempfile.mkdtemp(prefix='nfsmw-career-save-')
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(os.path.join(work, 'out'))
    os.makedirs(os.path.join(work, 'saves'))
    ok = True
    phases = (('rival', 'career-rival'), ('load', 'career-save')) if args.rival else \
        (('save', 'career-save'), ('load', 'career-save'), ('menu-save', 'career-save'), ('load', 'career-save'))
    for i, (phase, scenario) in enumerate(phases):
        code, lines = run_phase(binary, args.data, work, phase, args.screens, args.timeout, '%d-%s' % (i + 1, phase), scenario)
        print('%s: exit %s' % (phase, code))
        for l in lines:
            if not l.startswith('[benchmark]   car ') and not l.startswith('[benchmark]   race '):
                print('  ' + l)
        if code != 0:
            ok = False
            break
        if phase == 'rival' and not any('profile written' in l for l in lines):
            print('  no profile written')
            ok = False
            break
        if phase not in ('save', 'rival') and not any('PASS' in l for l in lines):
            ok = False
            break
    print('career save: %s (%s)' % ('PASS' if ok else 'FAIL', work))
    if not args.keep and ok:
        shutil.rmtree(work, ignore_errors=True)
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
