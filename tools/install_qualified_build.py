#!/usr/bin/env python3
"""Install a qa-c build qualified with a private copy of the owner's settings."""

import argparse
import json
import os
from pathlib import Path
import stat
import sys
import tempfile


FILES = {
    'qa-native-runner': 'native-runtime/linux-x86_64/qa-native-runner',
    'native-profile/client/qa-native-profile.so':
        'native-runtime/linux-x86_64/qa-native-profile.so',
    'quake-anthology': 'qa-c',
}
IDENTITY_FIELDS = ('device', 'inode', 'size', 'mtime_ns')


def identity(info):
    return dict(zip(IDENTITY_FIELDS,
                    (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns)))


def require(condition, message):
    if not condition:
        raise ValueError(message)


def qualify(build, profile, qualification):
    require(qualification.get('result') == 'PASS', 'Qualification must pass')
    require(qualification.get('artifact') == str(build / 'quake-anthology'),
            'Qualification artifact differs from this build')
    require(qualification.get('owner_profile_source') == str(profile),
            'Qualification used another owner profile')
    copied = qualification.get('copied_owner_settings')
    require(isinstance(copied, list) and copied and all(
        isinstance(name, str) and name and not Path(name).is_absolute() and
        '..' not in Path(name).parts for name in copied),
        'Qualification must record copied owner settings')
    require(qualification.get('owner_profile_unchanged') is True,
            'Qualification must preserve the original owner profile')
    require(qualification.get('normal_exit') is True and
            type(qualification.get('exit_code')) is int and
            qualification['exit_code'] == 0,
            'Qualification must record a normal exit with code zero')
    containment = qualification.get('private_containment')
    require(isinstance(containment, dict) and containment,
            'Qualification must record private containment')
    files = qualification.get('candidate_files')
    require(isinstance(files, dict) and set(files) == set(FILES),
            'Qualification must identify all three build files')
    for name in FILES:
        info = (build / name).stat()
        require(stat.S_ISREG(info.st_mode) and files[name] == identity(info),
                'Qualified file identity differs: ' + name)
    return files


def stage(source, target, expected):
    target.parent.mkdir(parents=True, exist_ok=True)
    descriptor, name = tempfile.mkstemp(prefix=target.name + '.', suffix='.tmp',
                                      dir=target.parent)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, 'w+b') as copy, source.open('rb') as original:
            info = os.fstat(original.fileno())
            require(identity(info) == expected,
                    'Qualified file changed before copying: ' + str(source))
            while chunk := original.read(1024 * 1024):
                copy.write(chunk)
            copy.flush()
            original.seek(0)
            copy.seek(0)
            while True:
                chunk = original.read(1024 * 1024)
                require(chunk == copy.read(1024 * 1024),
                        'Staged bytes differ: ' + str(source))
                if not chunk:
                    break
            require(identity(os.fstat(original.fileno())) == expected,
                    'Qualified file changed during copying: ' + str(source))
            os.fchmod(copy.fileno(), stat.S_IMODE(info.st_mode))
            os.fsync(copy.fileno())
        os.utime(temporary, ns=(info.st_atime_ns, info.st_mtime_ns))
        return temporary
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise


def install(build, destination, profile, qualification_path):
    qualification = json.loads(qualification_path.read_text())
    require(isinstance(qualification, dict), 'Qualification must be a JSON object')
    expected = qualify(build, profile, qualification)
    staged = {}
    try:
        for name, relative in FILES.items():
            target = destination / relative
            staged[name] = stage(build / name, target, expected[name])
        for name in FILES:
            require(identity((build / name).stat()) == expected[name],
                    'Qualified file changed before installation: ' + name)
        installed = {}
        for name, relative in FILES.items():
            target = destination / relative
            os.replace(staged[name], target)
            installed[str(target)] = {
                'source': str(build / name), 'byte_equal': True,
                **identity(target.stat()),
            }
        return {
            'result': 'PASS', 'qualification': str(qualification_path),
            'owner_profile_source': str(profile), 'files': installed,
        }
    finally:
        for temporary in staged.values():
            temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--destination', type=Path, required=True)
    parser.add_argument('--owner-profile', type=Path, required=True)
    parser.add_argument('--qualification', type=Path, required=True)
    options = parser.parse_args()
    try:
        receipt = install(options.build_dir.resolve(strict=True),
                          options.destination.resolve(),
                          options.owner_profile.resolve(strict=True),
                          options.qualification.resolve(strict=True))
    except (OSError, ValueError) as error:
        print('Install refused: ' + str(error), file=sys.stderr)
        return 1
    print(json.dumps(receipt, indent=2))
    return 0


if __name__ == '__main__':
    sys.exit(main())
