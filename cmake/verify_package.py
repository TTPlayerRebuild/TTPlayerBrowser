"""Verify the public runtime ZIP without extracting or running its contents."""
import argparse
import hashlib
from pathlib import Path
import struct
import zipfile


def verify(path):
    members = {'ttpbrowser.dll', 'THIRD_PARTY_NOTICES.md'}
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        if len(names) != 3 or set(names) != members | {'SHA256SUMS.txt'}:
            raise ValueError('Unexpected package files or installation directory')
        hashes = {}
        for line in archive.read('SHA256SUMS.txt').decode('utf-8-sig').splitlines():
            digest, name = line.split('  ', 1)
            if name in hashes:
                raise ValueError('Duplicate checksum entry')
            hashes[name] = digest
        if set(hashes) != members:
            raise ValueError('Incomplete package checksum list')
        for name in sorted(members):
            data = archive.read(name)
            if hashlib.sha256(data).hexdigest() != hashes[name]:
                raise ValueError(f'Checksum mismatch: {name}')
            if name.endswith('.dll'):
                if data[:2] != b'MZ':
                    raise ValueError(f'Invalid DLL: {name}')
                pe = struct.unpack_from('<I', data, 60)[0]
                if data[pe:pe+4] != b'PE\0\0' or struct.unpack_from('<H', data, pe+4)[0] != 0x14c:
                    raise ValueError(f'DLL must be x86: {name}')
    print(f'Runtime ZIP verified: {path.name}, one x86 DLL, notices and SHA-256 checksums')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    verify(parser.parse_args().archive)
