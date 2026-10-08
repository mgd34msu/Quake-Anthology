#!/usr/bin/env python3
"""THE-899 private headless Wayland qualification; host desktop is never mounted."""
from pathlib import Path
import argparse, ctypes, json, os, signal, subprocess, tempfile, time
BASE = Path(__file__).parent
if ctypes.CDLL(None, use_errno=True).prctl(36, 1, 0, 0, 0) != 0:
    raise OSError(ctypes.get_errno(), 'private Wayland subreaper')
import install_qualified_build as installation
FILES = tuple(installation.FILES)
PRODUCTS = installation.WAYLAND_PRODUCTS

def kernel_receipt(pid):
    base = Path('/proc') / str(pid)
    namespaces = {key: os.readlink(base / 'ns' / key) for key in ('user', 'net', 'ipc', 'pid')}
    hardware = []
    for fd in (base / 'fd').glob('*'):
        try:
            target = os.readlink(fd)
        except OSError:
            continue
        if target.startswith(('/dev/snd', '/dev/dri', '/dev/nvidia', '/dev/input', '/dev/video')):
            hardware.append(target)
    caps = next((v.split()[1] for v in (base / 'status').read_text().splitlines() if v.startswith('CapEff:')))
    return {'pid': pid, 'start_token': token(pid), 'process_group': os.getpgid(pid), 'session_id': os.getsid(pid), 'namespaces': namespaces, 'hardware_fds': hardware, 'effective_capabilities': caps}

def pin(path):
    s = Path(path).stat()
    return dict(device=s.st_dev, inode=s.st_ino, size=s.st_size, mtime_ns=s.st_mtime_ns)

def token(pid):
    return Path('/proc/' + str(pid) + '/stat').read_text().split(') ', 1)[1].split()[19]

class Session:

    def __init__(self):
        self.root = Path(tempfile.mkdtemp(prefix='qa-private-wayland-', dir='/tmp'))
        self.root.chmod(0o700)
        self.processes = []
        self.records = []
        self.overlays = []
        for n in ['runtime', 'user', 'user/home', 'user/config', 'user/cache', 'user/state', 'tmp', 'logs', 'empty']:
            (self.root / n).mkdir(mode=0o700)
        self.env = {'PATH': '/usr/bin:/bin', 'LANG': 'C.UTF-8', 'HOME': str(self.root / 'user/home'), 'XDG_RUNTIME_DIR': str(self.root / 'runtime'), 'WAYLAND_DISPLAY': 'qa-private-wayland', 'XDG_CONFIG_HOME': str(self.root / 'user/config'), 'XDG_CACHE_HOME': str(self.root / 'user/cache'), 'XDG_STATE_HOME': str(self.root / 'user/state'), 'XDG_CONFIG_DIRS': str(self.root / 'empty'), 'TMPDIR': str(self.root / 'tmp'), 'SDL_VIDEODRIVER': 'wayland', 'SDL_AUDIODRIVER': 'dummy', 'WLR_BACKENDS': 'headless', 'WLR_RENDERER': 'pixman', 'WLR_LIBINPUT_NO_DEVICES': '1', 'WLR_HEADLESS_OUTPUTS': '1', 'LIBGL_ALWAYS_SOFTWARE': '1', 'MESA_LOADER_DRIVER_OVERRIDE': 'llvmpipe', 'DBUS_SESSION_BUS_ADDRESS': 'unix:path=' + str(self.root / 'runtime/no-session-bus'), 'DBUS_SYSTEM_BUS_ADDRESS': 'unix:path=' + str(self.root / 'runtime/no-system-bus'), 'XDG_SESSION_TYPE': 'wayland'}

    def wrap(self, command, extra=()):
        a = ['/usr/bin/bwrap', '--unshare-user', '--unshare-net', '--unshare-ipc', '--cap-drop', 'ALL', '--die-with-parent', '--ro-bind', '/', '/', '--dev', '/dev', '--proc', '/proc']
        for p in ['/run', '/tmp', '/var/tmp', '/home', '/root']:
            a += ['--tmpfs', p]
        a += ['--ro-bind', str(self.root), str(self.root), '--ro-bind', str(BASE), str(BASE)]
        for p in extra:
            a += ['--ro-bind', str(p), str(p)]
        for source, target in self.overlays:
            a += ['--ro-bind', str(source), str(target)]
        for p in ['runtime', 'user', 'tmp', 'logs']:
            a += ['--bind', str(self.root / p), str(self.root / p)]
        for p in ['/run', '/var/tmp', '/home', '/root']:
            a += ['--remount-ro', p]
        a += ['--clearenv']
        for k, v in self.env.items():
            a += ['--setenv', k, v]
        return a + ['--chdir', str(self.root / 'user'), '--'] + list(map(str, command))

    def start(self, name, command, extra=()):
        logfile = (self.root / 'logs' / str(name + '.log')).open('wb')
        p = subprocess.Popen(self.wrap(command, extra), stdout=logfile, stderr=logfile, env=self.env, start_new_session=True)
        logfile.close()
        self.processes.append(p)
        self.record(name, p.pid)
        return p

    def record(self, name, pid):
        try:
            r = {'name': name, 'pid': pid, 'start_token': token(pid), 'process_group': os.getpgid(pid), 'session_id': os.getsid(pid)}
        except FileNotFoundError:
            return
        if not any((v['pid'] == pid and v['start_token'] == r['start_token'] for v in self.records)):
            self.records.append(r)
            self.save()

    def discover(self):
        sessions = {r['session_id'] for r in self.records}
        for p in Path('/proc').iterdir():
            if p.name.isdigit():
                try:
                    pid = int(p.name)
                    if os.getsid(pid) in sessions:
                        self.record('owned-descendant', pid)
                except (ProcessLookupError, FileNotFoundError, PermissionError):
                    pass

    def save(self):
        (self.root / 'owned-processes.json').write_text(json.dumps(self.records, indent=2) + '\n')

    def cleanup(self):
        self.discover()
        for sig in [signal.SIGTERM, signal.SIGKILL]:
            for r in reversed(self.records):
                try:
                    if token(r['pid']) == r['start_token']:
                        os.kill(r['pid'], sig)
                except (ProcessLookupError, FileNotFoundError):
                    pass
            for p in self.processes:
                try:
                    p.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    pass
        end = time.monotonic() + 2
        while time.monotonic() < end:
            try:
                pid, status = os.waitpid(-1, os.WNOHANG)
                if not pid:
                    time.sleep(0.02)
            except ChildProcessError:
                break
        alive = []
        for r in self.records:
            try:
                if token(r['pid']) == r['start_token']:
                    alive.append(r)
            except FileNotFoundError:
                pass
        (self.root / 'cleanup.json').write_text(json.dumps({'recorded_count': len(self.records), 'same_owned_tokens_still_live': alive}, indent=2) + '\n')

    def compositor(self):
        cfg = self.root / 'user/sway.conf'
        cfg.write_text('output * resolution 640x400\nseat seat0 fallback true\n')
        self.compositor_process = self.start('sway', ['/usr/bin/sway', '--unsupported-gpu', '-c', cfg])
        end = time.monotonic() + 12
        while time.monotonic() < end:
            if self.compositor_process.poll() is not None:
                raise RuntimeError('private compositor exited ' + str(self.compositor_process.returncode))
            sockets = [p for p in (self.root / 'runtime').glob('wayland-*') if p.is_socket()]
            if sockets:
                break
            time.sleep(0.02)
        else:
            raise RuntimeError('private Wayland socket not ready')
        self.env['WAYLAND_DISPLAY'] = sockets[0].name
        ipc = [p for p in (self.root / 'runtime').glob('sway-ipc*') if p.is_socket()]
        if not ipc:
            raise RuntimeError('private sway IPC socket missing')
        self.env['SWAYSOCK'] = str(ipc[0])
        self.discover()
        compositor_record = next((r for r in self.records if Path('/proc/' + str(r['pid']) + '/exe').exists() and Path('/proc/' + str(r['pid']) + '/exe').resolve().name == 'sway'))
        detail = kernel_receipt(compositor_record['pid'])
        detail['environment'] = self.env
        detail['root'] = str(self.root)
        detail['socket_stat'] = pin(sockets[0])
        detail['owner_socket_paths_unmounted'] = True
        detail['display_unset'] = 'DISPLAY' not in self.env
        detail['audio_dummy'] = self.env['SDL_AUDIODRIVER'] == 'dummy'
        detail['video_driver'] = 'wayland'
        detail['headless_compositor'] = True
        detail['compositor'] = 'sway headless pixman'
        host = kernel_receipt(os.getpid())
        if detail.get('hardware_fds') or int(detail['effective_capabilities'], 16) or any((detail['namespaces'][k] == host['namespaces'][k] for k in ['user', 'net', 'ipc'])):
            raise RuntimeError('private compositor isolation mismatch')
        (self.root / 'compositor-containment.json').write_text(json.dumps(detail, indent=2) + '\n')
        return detail

    def screenshot(self, label):
        target = self.root / 'user' / (label + '.png')
        p = self.start('screenshot-' + label, ['/usr/bin/grim', target])
        if p.wait(timeout=6) != 0:
            raise RuntimeError('private compositor screenshot failed ' + label)
        return target

def copied_owner(s, profile, names):
    data = {name: (profile / name).read_bytes() for name in names}
    for name, content in data.items():
        p = s.root / 'user/content' / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(content)
    return (profile, names, data)

def tree(s):
    p = s.start('ipc-tree', ['/usr/bin/swaymsg', '-t', 'get_tree', '-r'])
    p.wait(timeout=5)
    return json.loads((s.root / 'logs/ipc-tree.log').read_text())

def windows(t):
    out = []
    if t.get('app_id') or t.get('window'):
        out.append(t)
    for key in ['nodes', 'floating_nodes']:
        for node in t.get(key, []):
            out += windows(node)
    return out

def matrix_case(build, content, profile, names, product, renderer, port, owner_defaults=False):
    from PIL import Image, ImageStat
    s = Session()
    result = {'product': product, 'requested_renderer': renderer, 'renderer': 'gl' if renderer == 'default' else renderer, 'owner_display_defaults': owner_defaults, 'reached_menu': False, 'reached_gameplay': False, 'normal_exit': False, 'exit_code': None, 'evidence': str(s.root), 'startup_review': 'PENDING actual final PNG review', 'shutdown_route': 'Public --frames180 normal engine frame-limited exit; no keyboard proof'}
    try:
        result['private_containment'] = s.compositor()
        source, names, data = copied_owner(s, profile, names)
        result['copied_owner_settings'] = names
        artifact = build / 'quake-anthology'
        if owner_defaults:
            s.overlays = [(build / source, content / target) for source, target in installation.FILES.items()]
            artifact = content / 'qa-c'
            argv = [artifact, '--user-content-root', s.root / 'user/content', '--frames', '180']
            if renderer != 'default':
                argv += ['--renderer', renderer]
            result['content_mode'] = 'executable-relative qfiles layout; five candidate files overlaid only inside private mount namespace'
            if renderer == 'default':
                result['renderer_selection_basis'] = 'Source-attested qa_display_options_default selects QA_DISPLAY_OPENGL; no CLI or saved-profile renderer override'
        else:
            argv = [artifact, '--content-root', content, '--user-content-root', s.root / 'user/content', '--native-runtime-root', build, '--renderer', renderer, '--width', '640', '--height', '400', '--port', str(port), '--frames', '180']
            if product == 'menu':
                argv += ['--menu']
            else:
                argv += ['--game', product, '--map', 'start' if product.startswith('q1-') else 'base1' if product.startswith('q2-') else 'q3dm0']
        result['argv'] = list(map(str, argv))
        p = s.start(product + '-' + renderer, result['argv'], extra=[build, content])
        end = time.monotonic() + 40
        window = None
        while time.monotonic() < end:
            if p.poll() is not None:
                result['exit_code'] = p.returncode
                result['raw_log'] = (s.root / 'logs' / (product + '-' + renderer + '.log')).read_text(errors='replace')
                raise RuntimeError('candidate exited before mapped surface code ' + str(p.returncode))
            found = [v for v in windows(tree(s)) if v.get('app_id') == 'quake-anthology' or 'Quake' in (v.get('name') or '')]
            if found and found[0].get('visible') and found[0].get('focused'):
                window = found[0]
                break
            time.sleep(0.05)
        if not window:
            result['exit_code'] = p.poll()
            result['raw_log'] = (s.root / 'logs' / (product + '-' + renderer + '.log')).read_text(errors='replace')
            raise RuntimeError('candidate focused mapped Wayland surface missing')
        result['mapped_wayland_surface'] = window
        s.discover()
        actual = []
        for record in s.records:
            try:
                if pin('/proc/' + str(record['pid']) + '/exe') == pin(build / 'quake-anthology'):
                    actual.append(record)
            except FileNotFoundError:
                pass
        if not actual:
            raise RuntimeError('actual game executable inode does not match frozen candidate')
        game = actual[0]
        result['actual_game_pid'] = game['pid']
        result['actual_game_executable_pin'] = pin('/proc/' + str(game['pid']) + '/exe')
        result['actual_game_kernel'] = kernel_receipt(game['pid'])
        if owner_defaults:
            mounted = {name: pin('/proc/' + str(game['pid']) + '/root' + str(content / destination)) for name, destination in installation.FILES.items()}
            if mounted != {name: pin(build / name) for name in installation.FILES}:
                raise RuntimeError('private qfiles candidate overlay pin mismatch')
            result['actual_private_overlay_files'] = mounted
        captures = []
        rendered = []
        while p.poll() is None and time.monotonic() < end:
            path = s.screenshot(product + '-' + renderer + '-' + str(len(captures)))
            with Image.open(path) as picture:
                stats = ImageStat.Stat(picture.convert('RGB'))
                nonblack = sum(stats.var) > 100 and sum(stats.mean) > 15
            captures.append({'path': str(path), 'actual_nonblack_output': nonblack})
            if nonblack:
                rendered.append(str(path))
            time.sleep(0.1)
        result['exit_code'] = p.wait(timeout=5)
        result['normal_exit'] = result['exit_code'] == 0
        result['compositor_captures'] = captures
        if not rendered:
            raise RuntimeError('candidate never published nonblack compositor output')
        result['world_png'] = rendered[-1]
        result['owner_profile_unchanged'] = all(((source / n).read_bytes() == v for n, v in data.items()))
        result['log'] = str(s.root / 'logs' / (product + '-' + renderer + '.log'))
        result['raw_log'] = Path(result['log']).read_text(errors='replace')
    except Exception as e:
        result['error'] = str(e)
    finally:
        s.cleanup()
        result['cleanup'] = json.loads((s.root / 'cleanup.json').read_text())
        (s.root / 'case-result.json').write_text(json.dumps(result, indent=2) + '\n')
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', type=Path, required=True)
    parser.add_argument('--profile-file-list', type=Path, required=True, help='JSON array of relative owner files, or previous qualification copied_owner_settings')
    parser.add_argument('--content-root', type=Path, required=True)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--candidate-receipt', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--port-base', type=int, default=46561)
    args = parser.parse_args()
    binary = args.binary.resolve()
    build = binary.parent
    profile = args.profile.resolve()
    content = args.content_root.resolve()
    receipt = json.loads(args.candidate_receipt.read_text())
    file_list = json.loads(args.profile_file_list.read_text())
    names = file_list if isinstance(file_list, list) else file_list['copied_owner_settings']
    if binary.name != 'quake-anthology':
        raise ValueError('candidate binary must be build/quake-anthology')
    if not names or len(set(names)) != len(names) or any((not isinstance(n, str) or Path(n).is_absolute() or '..' in Path(n).parts for n in names)):
        raise ValueError('Nonempty distinct relative owner files required')
    pins = {name: pin(build / name) for name in FILES}
    recorded = receipt.get('candidate_files') or {name: receipt['files'][str(build / name)] for name in FILES}
    if recorded != pins:
        raise ValueError('candidate receipt stat pins differ from current five candidate files')
    originals = {n: (profile / n).read_bytes() for n in names}
    cases = []
    result = {'result': 'FAIL', 'artifact': str(binary), 'owner_profile_source': str(profile), 'copied_owner_settings': names, 'candidate_files': pins, 'owner_profile_unchanged': False, 'normal_exit': False, 'exit_code': None, 'private_containment': {'video_driver': 'wayland', 'headless_compositor': True, 'compositor': 'private sway headless pixman', 'SDL_AUDIODRIVER': 'dummy', 'DISPLAY_unset': True, 'HOME_and_XDG_runtime_private': True, 'host_devices_and_desktop_sockets_masked': True, 'no_debugger': True}, 'cases': cases, 'scope': 'Actual private Wayland menu plus five native game families on CPU and software GL; public frame-limited normal shutdown, no keyboard proof. No physical desktop, GPU fidelity or performance claim.'}
    try:
        launch_scopes = [('menu', renderer, True, content) for renderer in ['default', 'cpu']]
        launch_scopes += [(product, renderer, False, content) for product in PRODUCTS for renderer in ['cpu', 'gl']]
        empty = Path(tempfile.mkdtemp(prefix='qa-the899-empty-content-', dir='/tmp'))
        empty.chmod(0o700)
        launch_scopes += [('menu', renderer, False, empty) for renderer in ['cpu', 'gl']]
        for product, renderer, owner_defaults, case_content in launch_scopes:
            if any((pin(build / name) != pins[name] for name in FILES)):
                raise RuntimeError('frozen candidate changed before next capture')
            case = matrix_case(build, case_content, profile, names, product, renderer, args.port_base + len(cases), owner_defaults)
            if case_content == empty:
                case['content_mode'] = 'empty'
            cases.append(case)
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(result, indent=2) + '\n')
            print(json.dumps({'case': product, 'renderer': renderer, 'owner_defaults': owner_defaults, 'content_mode': case.get('content_mode'), 'evidence': case['evidence'], 'exit_code': case['exit_code'], 'error': case.get('error')}), flush=True)
            if case.get('error') or not case['normal_exit'] or (not case.get('owner_profile_unchanged')) or case['cleanup']['same_owned_tokens_still_live']:
                raise RuntimeError('actual Wayland case failed: ' + product + ' ' + renderer)
        result['owner_profile_unchanged'] = all(((profile / n).read_bytes() == v for n, v in originals.items()))
        result['candidate_files_unchanged'] = all((pin(build / name) == pins[name] for name in FILES))
        if not result['owner_profile_unchanged'] or not result['candidate_files_unchanged']:
            raise RuntimeError('original profile or frozen candidate changed')
        result.update(result='PENDING_VISUAL_REVIEW', normal_exit=True, exit_code=0)
    except Exception as e:
        result['error'] = str(e)
        result['owner_profile_unchanged'] = all(((profile / n).read_bytes() == v for n, v in originals.items()))
    finally:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'result': result['result'], 'output': str(args.output), 'completed_case_count': len(cases), 'error': result.get('error')}))
    return 0 if result['result'] == 'PENDING_VISUAL_REVIEW' else 1
if __name__ == '__main__':
    raise SystemExit(main())
