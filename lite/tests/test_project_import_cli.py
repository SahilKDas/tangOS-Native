"""Exercise native ZIP import with disposable archives; no extracted file is executed."""
import argparse
import json
import pathlib
import subprocess
import tempfile
import zipfile

parser = argparse.ArgumentParser()
parser.add_argument('--exe', required=True)
exe = pathlib.Path(parser.parse_args().exe).resolve()
descriptor = {'tangosVersion': '1', 'project': {'name': 'zip-fixture', 'title': 'ZIP fixture'}, 'tools': []}

with tempfile.TemporaryDirectory(prefix='TangOS-ZIP-fixture-') as folder:
    root = pathlib.Path(folder)
    data = root / 'data'

    def call(args, succeeds=True):
        request, response = root / 'request.json', root / 'response.json'
        request.write_text(json.dumps({'method': 'projects.importZip', 'arguments': args}), encoding='utf-8')
        result = subprocess.run([str(exe), '--backend', '-', str(data), str(request), str(response)],
                                capture_output=True, timeout=30)
        value = json.loads(response.read_text(encoding='utf-8-sig'))
        assert (result.returncode == 0) == succeeds, value
        return value

    def make(name, entries=(), compression=zipfile.ZIP_DEFLATED):
        archive = root / (name + '.zip')
        with zipfile.ZipFile(archive, 'w', compression=compression) as z:
            z.writestr('project-main/tangos.json', json.dumps(descriptor))
            z.writestr('project-main/src/fixture.cpp', 'int fixture() { return 42; }\n')
            z.writestr('project-main/port/fixture.cpp', 'int port_fixture() { return 1; }\n')
            for path, content in entries:
                if isinstance(path, str) and '\\' in path:
                    info = zipfile.ZipInfo('placeholder')
                    info.filename = path  # Preserve the malicious byte spelling on Windows.
                    path = info
                z.writestr(path, content)
        return archive

    for compression in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED):
        archive = make(str(compression), compression=compression)
        dest = root / ('imported ' + str(compression))
        args = {'archive': str(archive), 'destination': str(dest)}
        preview = call(args)
        assert not dest.exists() and preview['requiresConfirmation']
        assert 'src/fixture.cpp' in preview['details']['files']
        result = call(dict(args, confirmation=preview['confirmation']))
        assert result['path'] == str(dest)
        assert (dest / '.git').is_dir() and (dest / 'src/fixture.cpp').read_text().startswith('int fixture')
        assert json.loads((dest / 'tangos.json').read_text()) == descriptor
        status = subprocess.run(['git', 'status', '--porcelain'], cwd=dest, capture_output=True, text=True, check=True)
        assert '?? src/' in status.stdout  # Import never stages or commits source automatically.
        call(args, succeeds=False)  # Existing destinations cannot be overwritten.

    unsafe = [('../escaped.txt', 'x'), ('project-main/../escaped.txt', 'x'),
              ('project-main/C:escape', 'x'), ('project-main/back\\slash', 'x'),
              ('project-main/CON.txt', 'x'), ('project-main/trailing.', 'x'),
              ('project-main/.git/config', 'x'), ('project-main/baserom.nds', 'x'),
              ('project-main/private/secret.txt', 'x'),
              ('project-main/SRC/FIXTURE.cpp', 'case alias'),
              ('project-main/token.txt', '-----BEGIN RSA PRIVATE KEY-----\nprivate')]
    for n, entry in enumerate(unsafe):
        archive = make('unsafe-' + str(n), [entry])
        dest = root / ('rejected-' + str(n))
        call({'archive': str(archive), 'destination': str(dest)}, succeeds=False)
        assert not dest.exists() and not (root / 'escaped.txt').exists()

    symlink = zipfile.ZipInfo('project-main/link')
    symlink.create_system = 3
    symlink.external_attr = 0o120777 << 16
    archive = make('symlink', [(symlink, '../escaped.txt')])
    call({'archive': str(archive), 'destination': str(root / 'symlink-out')}, succeeds=False)

    archive = make('tamper')
    args = {'archive': str(archive), 'destination': str(root / 'tamper-out')}
    preview = call(args)
    with zipfile.ZipFile(archive, 'a') as z:
        z.writestr('project-main/README.md', 'changed after preview')
    call(dict(args, confirmation=preview['confirmation']), succeeds=False)
    assert not pathlib.Path(args['destination']).exists()
    print('PASS native ZIP: stored/deflated, source preservation, reviewed import, traversal/device/link/asset/credential/case guards, changed-archive confirmation')
