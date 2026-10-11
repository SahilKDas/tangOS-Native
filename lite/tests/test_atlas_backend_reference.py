"""Compare native Atlas reads/history directly against retained Console functions."""
import argparse
import json
import pathlib
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', required=True)
    exe = pathlib.Path(parser.parse_args().exe).resolve()
    root = pathlib.Path(__file__).resolve().parents[2]
    count = 0
    with tempfile.TemporaryDirectory(prefix='tangos-atlas-reference-') as folder:
        tmp = pathlib.Path(folder)
        repo = tmp / 'repository with spaces'
        repo.mkdir()
        descriptor = {'tangosVersion': '1', 'project': {'name': 'fixture', 'title': 'Atlas fixture'}, 'tools': []}
        (repo / 'tangos.json').write_text(json.dumps(descriptor), encoding='utf-8')
        def native(method, arguments=None):
            request, response = tmp / 'request.json', tmp / 'response.json'
            request.write_text(json.dumps({'method': method, 'arguments': arguments or {}}), encoding='utf-8')
            result = subprocess.run([str(exe), '--backend', str(repo), str(tmp / 'data'), str(request), str(response)], capture_output=True, timeout=30)
            assert result.returncode == 0, (method, result.stderr, response.read_text(encoding='utf-8-sig'))
            return json.loads(response.read_text(encoding='utf-8-sig'))
        runner = tmp / 'reference.mjs'
        runner.write_text(
            'import {readAtlas} from ' + json.dumps((root / 'console/src/main/atlas.ts').as_uri()) + ';\n'
            'import {recentlyAddedSrc,defaultBranch} from ' + json.dumps((root / 'console/src/main/gitsafe.ts').as_uri()) + ';\n'
            'const repo=process.argv[2];const mode=process.argv[3];\n'
            'const result=mode==="load"?readAtlas(repo,{data:{dbPath:process.argv[4]}}):await recentlyAddedSrc(repo,await defaultBranch(repo),Number(process.argv[4]));\n'
            'process.stdout.write(JSON.stringify(result));\n', encoding='utf-8')
        def reference(mode, value):
            result = subprocess.run(['node', '--disable-warning=MODULE_TYPELESS_PACKAGE_JSON', str(runner), str(repo), mode, str(value)], capture_output=True, text=True, encoding='utf-8', timeout=30)
            assert result.returncode == 0, result.stderr
            return json.loads(result.stdout)
        for content in [None, '', '{broken', 'null', '[]', '{}', '{"functions":null}', '{"functions":{}}', '{"functions":[]}', '{"functions":[{"id":"Unicode 日本語","size":12}],"metadata":{"custom":true}}']:
            db = repo / 'chaos-db.json'
            if content is None:
                db.unlink(missing_ok=True)
            else:
                db.write_text(content, encoding='utf-8')
            assert native('atlas.load') == reference('load', 'chaos-db.json'), content
            count += 1
        def git(*args):
            result = subprocess.run(['git', '-C', str(repo), *args], capture_output=True, text=True, encoding='utf-8', timeout=30)
            assert result.returncode == 0, result.stderr
            return result.stdout.strip()
        git('init', '-b', 'main')
        git('config', 'user.name', 'Fixture')
        git('config', 'user.email', 'fixture@example.invalid')
        (repo / 'src/nested').mkdir(parents=True)
        for name in ['first.cpp', 'nested/space name.c', 'ignored.h']:
            (repo / 'src' / name).write_text('fixture\n', encoding='utf-8')
        git('add', '.')
        git('commit', '-m', 'initial')
        git('update-ref', 'refs/remotes/origin/master', 'HEAD')
        for hours in [1, 24, 48]:
            assert native('atlas.recentAdds', {'sinceHours': hours}) == reference('recent', hours)
            count += 1
        (repo / 'src/first.cpp').write_text('modified\n', encoding='utf-8')
        (repo / 'src/nested/new.cpp').write_text('new\n', encoding='utf-8')
        git('add', '.')
        git('commit', '-m', 'modified and new')
        git('update-ref', 'refs/remotes/origin/topic', 'HEAD')
        git('symbolic-ref', 'refs/remotes/origin/HEAD', 'refs/remotes/origin/topic')
        for hours in [1, 24, 48]:
            assert native('atlas.recentAdds', {'sinceHours': hours}) == reference('recent', hours)
            count += 1
    print(f'PASS {count} original Atlas database and upstream recent-source comparisons')


if __name__ == '__main__':
    main()