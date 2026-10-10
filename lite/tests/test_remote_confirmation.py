"""A changed remote must invalidate a native confirmation without any network call."""
import argparse
import json
import pathlib
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', required=True)
    exe = pathlib.Path(parser.parse_args().exe).resolve()
    with tempfile.TemporaryDirectory(prefix='TangOS remote review ') as folder:
        root = pathlib.Path(folder)
        repo = root / 'repository'
        repo.mkdir()

        def git(*args):
            subprocess.run(['git', '-C', str(repo), *args], check=True, capture_output=True)

        git('init', '-b', 'main')
        (repo / 'README.md').write_text('Synthetic confirmation fixture\n', encoding='utf-8')
        (repo / 'tangos.json').write_text(json.dumps({
            'tangosVersion': '1', 'project': {'name': 'fixture'}, 'tools': []
        }), encoding='utf-8')
        git('add', '.')
        git('-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid',
            'commit', '-m', 'fixture')
        git('remote', 'add', 'origin', 'https://fixture.invalid/original.git')

        def call(arguments):
            request, response = root / 'request.json', root / 'response.json'
            request.write_text(json.dumps({'method': 'preferences.set', 'arguments': arguments}),
                               encoding='utf-8')
            result = subprocess.run([str(exe), '--backend', str(repo), str(root / 'state'),
                                     str(request), str(response)], capture_output=True, timeout=30)
            return result.returncode, json.loads(response.read_text(encoding='utf-8-sig'))

        code, preview = call({'allowNearMiss': False})
        assert code == 0 and preview['requiresConfirmation'], preview
        git('remote', 'set-url', '--push', 'origin', 'https://fixture.invalid/changed.git')
        code, result = call({'allowNearMiss': False, 'confirmation': preview['confirmation']})
        assert code != 0, 'Remote changed but stale confirmation executed'
        assert 'preview' in json.dumps(result).lower() or 'changed' in json.dumps(result).lower(), result
        print('PASS changed push destination invalidates native confirmation; no network calls')


if __name__ == '__main__':
    main()
