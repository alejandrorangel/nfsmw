"""Runs the native port's benchmark mode several times and prints each scenario's frame times.

    python3 port/tools/benchmark.py                          # race, pursuit, frontend; 3 runs each
    python3 port/tools/benchmark.py race rain forest         # also: rain, forest
    python3 port/tools/benchmark.py -n 5 race                # one scenario, 5 runs
    python3 port/tools/benchmark.py --vary FSAALevel=0,3 race
    python3 port/tools/benchmark.py --set ShadowDetail=0 --set RacingResolution=3
    python3 port/tools/benchmark.py --screenshots shots/before race pursuit
    python3 port/tools/benchmark.py --compare shots/before shots/after

NFSMW_BENCHMARK=<scenario> boots straight into a fixed race (or the main menu), lets the race AI drive the
player's car and runs every frame as exactly one 1/60 s sim tick with a fixed seed and synchronous file
loading, drawing offscreen as fast as the machine can (no pacing, no vsync). A run is always the same
frames: the screenshots of two runs are pixel-identical, so --compare tells rendering changes apart from
noise. The game writes a CSV of per-frame times (wall, game loop, render build, present, encode, GPU) and
the screenshots, then exits.

As in normal play, the main thread encodes a frame while the game thread builds the next one, so the
frame time is about the longer of the two (or the GPU's). The build and encode columns are the medians of
each on its own; --serial runs the old lockstep (NFSMW_BENCHMARK_SERIAL=1: the game waits for each
encode), whose frame is their sum.

Times are wall time per frame with the first frame (the load before it) left out. The median leaves out
the hitches that swing the average fps. Other programs, the Mac warming up and other games running
change the numbers a lot: the spread column is how far the runs' medians are apart, and runs with a
spread over 5% get a warning.

Settings are the video options' names (g_FSAALevel, or just FSAALevel; see VideoOptions.cfg or
port/src/Platform/GraphicsOptionsNative.cpp). They're written to a temporary NFSMW_VIDEO_CONFIG, starting
from the game's defaults for this Mac (--base user starts from your saved options instead; your file is
only read). --vary takes turns run by run, so a drift over time hits each value alike.
"""
import argparse
import csv
import os
import shutil
import statistics
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_BIN = os.path.join(ROOT, 'build', 'port', 'NFSMW.app', 'Contents', 'MacOS', 'NFSMW')
USER_OPTIONS = os.path.expanduser('~/Library/Application Support/NFSMW/VideoOptions.cfg')

# Mirrors kScenarios in port/src/Platform/BenchmarkNative.cpp.
SCENARIOS = {
    'race': 'downtown Rockport circuit, 3 opponents, light traffic: renderer and GPU',
    'pursuit': 'Rosewood sprint, 3 opponents, heat 4 pursuit: CPU (AI, physics), many cars',
    'rain': 'the downtown circuit in the rain: rain particles, wet road',
    'frontend': 'main menu over the garage car: car shading, frontend',
    'forest': 'Rosewood forest circuit, 3 opponents: trees and long views',
    'freeroam': "a new career's free roam (skip D-Day), the AI driving: traffic, cops, world events",
    'career-event': "free roam, into a bin 15 event's trigger: engage dialog, the race, rewards, back to free roam",
    'career-milestone': "free roam, a heat 2 pursuit for the bin's milestones, escaped: rewards, into the safehouse",
    'career-busted': "free roam, a heat 2 pursuit, the car stopped until it's busted: the arrest NIS (skinned characters)",
}
DEFAULT = ['race', 'pursuit', 'frontend']


def option_name(name):
    if name in ('VERSION', 'SIZE', 'FirstTime') or name.startswith('g_'):
        return name
    return 'g_' + name


def parse_setting(text):
    name, sep, value = text.partition('=')
    if not sep or not name:
        sys.exit('A setting is KEY=VALUE: %s' % text)
    return option_name(name.strip()), value.strip()


def read_options(path):
    options = {}
    with open(path) as f:
        for line in f:
            parts = line.split()
            if len(parts) == 2:
                options[parts[0]] = parts[1]
    return options


def write_options(path, options):
    with open(path, 'w') as f:
        for name, value in options.items():
            f.write('%s %s\n' % (name, value))


def game_env(binary_data, out_dir, options_path, extra):
    env = dict(os.environ)
    for key in list(env):
        if key.startswith('NFSMW_BENCHMARK') or key.startswith('NFSMW_TRACE') or key.startswith('NFSMW_AUTO') or key == 'NFSMW_RAIN':
            del env[key]
    env['NFSMW_DATA'] = binary_data
    env['NFSMW_BENCHMARK_OUT'] = out_dir
    env['NFSMW_VIDEO_CONFIG'] = options_path
    env.update(extra)
    return env


def kill(binary):
    # Only this exact binary: other sessions may be running other builds.
    subprocess.run(['pkill', '-f', '^' + binary], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def default_options(binary, data, work):
    """The options the game picks for this Mac on a first run (it writes them to a missing file)."""
    path = os.path.join(work, 'defaults.cfg')
    env = game_env(data, work, path, {'NFSMW_BENCHMARK': 'write-options'})
    try:
        subprocess.run([binary], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)
    except subprocess.TimeoutExpired:
        pass
    kill(binary)
    if not os.path.exists(path):
        sys.exit('The game wrote no default options, run it by hand: NFSMW_BENCHMARK=write-options NFSMW_VIDEO_CONFIG=x.cfg %s' % binary)
    return read_options(path)


def frame_times(path):
    rows = []
    with open(path, newline='') as f:
        reader = csv.DictReader(line for line in f if not line.startswith('#'))
        for row in reader:
            rows.append(row)
    return rows


def percentile(values, fraction):
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(len(ordered) * fraction))]


def run(binary, data, scenario, options, out_dir, args):
    options_path = os.path.join(out_dir, 'options.cfg')
    write_options(options_path, options)
    extra = {'NFSMW_BENCHMARK': scenario}
    if args.frames:
        extra['NFSMW_BENCHMARK_FRAMES'] = str(args.frames)
    if args.shots is not None:
        extra['NFSMW_BENCHMARK_SHOTS'] = args.shots
    if args.serial:
        extra['NFSMW_BENCHMARK_SERIAL'] = '1'
    if args.vsync:
        extra['NFSMW_BENCHMARK_VSYNC'] = '1'
        extra['NFSMW_BENCHMARK_PRESENT'] = '1'
    for setting in args.env:
        name, sep, value = setting.partition('=')
        if not sep or not name:
            sys.exit('An environment variable is NAME=VALUE: %s' % setting)
        extra[name.strip()] = value.strip()
    env = game_env(data, out_dir, options_path, extra)
    load = os.getloadavg()[0]
    started = time.time()
    log = open(os.path.join(out_dir, 'game.log'), 'w')
    try:
        code = subprocess.run([binary], env=env, stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout).returncode
    except subprocess.TimeoutExpired:
        print('  timed out after %d s, see %s' % (args.timeout, log.name))
        return None
    finally:
        log.close()
        kill(binary)
    csv_path = os.path.join(out_dir, scenario + '.csv')
    if code != 0 or not os.path.exists(csv_path):
        print('  the game exited with %d and wrote no CSV, see %s' % (code, log.name))
        return None
    rows = frame_times(csv_path)
    times = [float(r['frame_ms']) for r in rows if int(r['frame']) > 1]
    if not times:
        return None
    def column(name):
        return statistics.median(float(r.get(name) or 0) for r in rows if int(r['frame']) > 1)
    cars = max(int(r.get('cars') or 0) for r in rows)
    return {'median': statistics.median(times), 'p90': percentile(times, 0.9), 'p99': percentile(times, 0.99),
            'fps': len(times) / (sum(times) / 1000.0), 'gpu': column('gpu_ms'), 'build': column('render_ms'),
            'encode': column('encode_ms'), 'frames': len(rows), 'cars': cars,
            'wall': time.time() - started, 'load': load,
            'shots': sorted(f for f in os.listdir(out_dir) if f.endswith('.png'))}


# ---------------------------------------------------------------------------
# Images: PNG -> BMP with sips (every Mac has it), compared with numpy when
# it's there, else in plain Python.
# ---------------------------------------------------------------------------
def load_image(path, work):
    bmp = os.path.join(work, 'compare.bmp')
    subprocess.run(['sips', '-s', 'format', 'bmp', path, '--out', bmp], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    with open(bmp, 'rb') as f:
        data = f.read()
    os.remove(bmp)
    offset = int.from_bytes(data[10:14], 'little')
    width = int.from_bytes(data[18:22], 'little', signed=True)
    height = abs(int.from_bytes(data[22:26], 'little', signed=True))
    bpp = int.from_bytes(data[28:30], 'little')
    channels = bpp // 8
    stride = (width * channels + 3) & ~3
    rows = [data[offset + y * stride: offset + y * stride + width * channels] for y in range(height)]
    return width, height, channels, rows


def compare_images(a, b, work):
    """(max channel difference, mean channel difference, fraction of pixels that differ), or None if the sizes differ."""
    wa, ha, ca, rows_a = load_image(a, work)
    wb, hb, cb, rows_b = load_image(b, work)
    if (wa, ha, ca) != (wb, hb, cb):
        return None
    try:
        import numpy
        x = numpy.frombuffer(b''.join(rows_a), dtype=numpy.uint8).astype(numpy.int16)
        y = numpy.frombuffer(b''.join(rows_b), dtype=numpy.uint8).astype(numpy.int16)
        d = numpy.abs(x - y)
        per_pixel = d.reshape(-1, ca).max(axis=1)
        return int(d.max()), float(d.mean()), float((per_pixel > 0).mean())
    except ImportError:
        worst, total, pixels = 0, 0, 0
        for ra, rb in zip(rows_a, rows_b):
            if ra == rb:
                continue
            for i in range(0, len(ra), ca):
                diff = max(abs(ra[i + c] - rb[i + c]) for c in range(ca))
                if diff:
                    pixels += 1
                    worst = max(worst, diff)
                    total += sum(abs(ra[i + c] - rb[i + c]) for c in range(ca))
        return worst, total / float(wa * ha * ca), pixels / float(wa * ha)


def compare_sets(a, b, work):
    """Compares the PNGs two directories have in common (by relative path); returns the worst difference."""
    names = set()
    for base in (a, b):
        for dirpath, _, files in os.walk(base):
            for f in files:
                if f.endswith('.png'):
                    names.add(os.path.relpath(os.path.join(dirpath, f), base))
    worst = 0
    print('%-40s %6s %10s %10s' % ('image', 'max', 'mean', 'differ'))
    for name in sorted(names):
        pa, pb = os.path.join(a, name), os.path.join(b, name)
        if not (os.path.exists(pa) and os.path.exists(pb)):
            print('%-40s only in %s' % (name, a if os.path.exists(pa) else b))
            worst = max(worst, 256)
            continue
        result = compare_images(pa, pb, work)
        if result is None:
            print('%-40s sizes differ' % name)
            worst = max(worst, 256)
            continue
        print('%-40s %6d %10.4f %9.3f%%' % (name, result[0], result[1], result[2] * 100))
        worst = max(worst, result[0])
    return worst


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0], epilog=__doc__.split('\n', 1)[1],
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('scenarios', nargs='*', help='default: ' + ' '.join(DEFAULT) + '; there are ' + ', '.join(SCENARIOS))
    parser.add_argument('-n', '--runs', type=int, default=3, help='runs of each scenario and value, 3 by default')
    parser.add_argument('--bin', default=DEFAULT_BIN, help='the game, build/port/NFSMW.app/Contents/MacOS/NFSMW by default')
    parser.add_argument('--data', default=os.environ.get('NFSMW_DATA', os.path.expanduser('~/Games/NFSMW')),
                        help='the game data, $NFSMW_DATA or ~/Games/NFSMW')
    parser.add_argument('--set', action='append', default=[], metavar='KEY=VALUE', help='a video option for every run')
    parser.add_argument('--vary', metavar='KEY=A,B,...', help='a video option whose values take turns, run by run')
    parser.add_argument('--base', choices=('defaults', 'user'), default='defaults',
                        help="the options the settings start from: the game's defaults for this Mac, or your saved ones (read only)")
    parser.add_argument('--frames', type=int, help="frames each run records, the scenario's (3600) by default")
    parser.add_argument('--shots', help="the frames to screenshot, comma separated, the scenario's by default; '' for none")
    parser.add_argument('--screenshots', metavar='DIR', help="keep the first run's screenshots in DIR/<setting>/ (DIR/ without --vary)")
    parser.add_argument('--keep', metavar='DIR', help='keep every run (CSV, log, screenshots, options) in DIR')
    parser.add_argument('--serial', action='store_true', help='the game thread waits for each frame\'s encode (the frame is build + encode)')
    parser.add_argument('--vsync', action='store_true', help='present every frame with the display sync on (the frame rate is the display\'s)')
    parser.add_argument('--timeout', type=int, default=300, help='seconds a run may take, 300 by default')
    parser.add_argument('--env', action='append', default=[], metavar='NAME=VALUE',
                        help='an environment variable for every run (e.g. NFSMW_BENCHMARK_NOS=600-900)')
    parser.add_argument('--compare', nargs=2, metavar=('A', 'B'), help='compare two screenshot directories and exit')
    args = parser.parse_args()

    work = tempfile.mkdtemp(prefix='nfsmw-benchmark-')
    try:
        if args.compare:
            worst = compare_sets(args.compare[0], args.compare[1], work)
            print('identical' if worst == 0 else ('missing or resized images' if worst > 255 else 'max difference %d' % worst))
            return 0 if worst == 0 else 1
        return benchmark(args, work)
    finally:
        shutil.rmtree(work, ignore_errors=True)


def benchmark(args, work):
    binary = os.path.abspath(args.bin)
    if not os.path.exists(binary):
        sys.exit('No game at %s, build it (ninja -C build/port nfsmw) or give --bin' % binary)
    names = args.scenarios or DEFAULT
    for name in names:
        if name not in SCENARIOS:
            sys.exit('Unknown scenario %s, there are: %s' % (name, ', '.join(SCENARIOS)))

    if args.base == 'user':
        if not os.path.exists(USER_OPTIONS):
            sys.exit('No saved options at %s' % USER_OPTIONS)
        base = read_options(USER_OPTIONS)
    else:
        base = default_options(binary, args.data, work)
    fixed = [parse_setting(s) for s in args.set]
    variants = [None]
    if args.vary:
        name, values = parse_setting(args.vary)
        variants = [(name, v) for v in values.split(',')]
    for name, _ in fixed + [v for v in variants if v]:
        if name not in base:
            sys.exit('No video option %s, there are: %s' % (name, ', '.join(n for n in base if n.startswith('g_'))))

    print('%s, %s options%s' % (binary, args.base, ''.join(', %s=%s' % s for s in fixed)))
    results = {}
    shots = {}
    run_index = 0
    try:
        for scenario in names:
            print('\n%s: %s' % (scenario, SCENARIOS[scenario]))
            for i in range(args.runs):
                for variant in variants:
                    options = dict(base)
                    options['g_VSyncOn'] = '1' if args.vsync else '0'
                    for name, value in fixed + ([variant] if variant else []):
                        options[name] = value
                    label = '%s=%s' % (variant[0][2:], variant[1]) if variant else ''
                    run_index += 1
                    out = os.path.join(work, 'run%03d-%s' % (run_index, scenario))
                    os.makedirs(out)
                    result = run(binary, args.data, scenario, options, out, args)
                    if args.keep:
                        dest = os.path.join(args.keep, '%s%s-run%d' % (scenario, '-' + label if label else '', i + 1))
                        shutil.rmtree(dest, ignore_errors=True)
                        shutil.copytree(out, dest)
                    if not result:
                        continue
                    key = (scenario, label)
                    # Screenshots: keep the first run's, and check the others against it.
                    note = ''
                    if key not in shots:
                        keep = os.path.join(work, 'shots', scenario, label or '-')
                        os.makedirs(keep)
                        for f in result['shots']:
                            shutil.copy(os.path.join(out, f), keep)
                        shots[key] = keep
                        if args.screenshots:
                            dest = os.path.join(args.screenshots, label) if label else args.screenshots
                            os.makedirs(dest, exist_ok=True)
                            for f in result['shots']:
                                shutil.copy(os.path.join(out, f), dest)
                    elif result['shots']:
                        worst = 0
                        for f in result['shots']:
                            other = os.path.join(shots[key], f)
                            diff = compare_images(os.path.join(out, f), other, work) if os.path.exists(other) else None
                            worst = max(worst, diff[0] if diff else 256)
                        result['diff'] = worst
                        note = '  screenshots ' + ('identical' if worst == 0 else 'DIFFER (max %d)' % worst)
                    results.setdefault(key, []).append(result)
                    print('  run %d %-20s median %6.3f ms  p90 %6.3f  p99 %6.3f  avg %6.1f fps  build %5.3f  encode %5.3f  gpu %5.3f  (%d frames, %d cars, %.0f s, load %.1f)%s'
                          % (i + 1, label, result['median'], result['p90'], result['p99'], result['fps'], result['build'], result['encode'],
                             result['gpu'], result['frames'], result['cars'], result['wall'], result['load'], note))
                    shutil.rmtree(out, ignore_errors=True)
    except KeyboardInterrupt:
        print('\nStopped')
        kill(binary)

    if not results:
        return 1
    print('\n%-10s %-20s %9s %8s %8s %9s %8s %8s %8s %8s %5s' % ('scenario', 'setting', 'median ms', 'p90', 'p99', 'avg fps', 'build ms',
                                                              'encode ms', 'gpu ms', 'spread', 'runs'))
    noisy = []
    for (scenario, label), runs in results.items():
        medians = [r['median'] for r in runs]
        middle = statistics.median(medians)
        spread = (max(medians) - min(medians)) / middle * 100 if len(medians) > 1 else 0
        if spread > 5:
            noisy.append('%s %s' % (scenario, label))
        print('%-10s %-20s %9.3f %8.3f %8.3f %9.1f %8.3f %8.3f %8.3f %7.1f%% %5d'
              % (scenario, label or '-', middle, statistics.median(r['p90'] for r in runs), statistics.median(r['p99'] for r in runs),
                 statistics.median(r['fps'] for r in runs), statistics.median(r['build'] for r in runs),
                 statistics.median(r['encode'] for r in runs), statistics.median(r['gpu'] for r in runs), spread, len(runs)))
    if noisy:
        print('\nWarning: the runs of %s are more than 5%% apart; something else is using the machine '
              '(load average now %.1f), run again or with more -n.' % (', '.join(noisy), os.getloadavg()[0]))
    differ = [k for k, runs in results.items() if any(r.get('diff') for r in runs)]
    if differ:
        print('Warning: screenshots differ between runs of %s: the run is not deterministic.' % ', '.join('%s %s' % k for k in differ))
    return 0


if __name__ == '__main__':
    sys.exit(main())
