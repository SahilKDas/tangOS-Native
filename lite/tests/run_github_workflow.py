"""Opt-in live native clone/commit/push/PR test in a NEW private synthetic repo.

Uses existing gh authentication. Retains the repo and draft PR for review, never
merges, deletes remote data, reads keys, or touches a user's existing checkout.
"""
import argparse
import json
import pathlib
import subprocess
import uuid

parser = argparse.ArgumentParser()
parser.add_argument('--exe', required=True)
parser.add_argument('--output', required=True)
parser.add_argument('--create-private-fixture', action='store_true', required=True)
a = parser.parse_args()
exe = pathlib.Path(a.exe).resolve()
output = pathlib.Path(a.output).resolve()
output.mkdir(parents=True, exist_ok=False)
repo = output / 'checkout'
state = output / 'state'

def command(args, cwd=None, timeout=120):
    run = subprocess.run(args, cwd=cwd, capture_output=True, text=True, timeout=timeout)
    if run.returncode:
        raise RuntimeError(f'{args[0]} failed ({run.returncode}): {run.stderr}')
    return run.stdout.strip()

calls = 0
def call(method, arguments, checkout=None):
    global calls
    calls += 1
    request = output / f'{calls:03}-request.json'
    response = output / f'{calls:03}-response.json'
    request.write_text(json.dumps({'method': method, 'arguments': arguments}), encoding='utf-8')
    run = subprocess.run([str(exe), '--backend', str(checkout or '-'), str(state),
                          str(request), str(response)], capture_output=True, timeout=180)
    value = json.loads(response.read_text(encoding='utf-8-sig'))
    if run.returncode:
        raise RuntimeError(value)
    return value

def reviewed(method, arguments, checkout=None):
    preview = call(method, arguments, checkout)
    assert preview['requiresConfirmation'], preview
    details = preview.get('details', {})
    if arguments.get('action') == 'Commit staged':
        assert details.get('diff'), 'No staged diff in commit preview'
    if arguments.get('action') == 'Push reviewed':
        assert details.get('commits'), 'No commit review before push'
    return call(method, dict(arguments, confirmation=preview['confirmation']), checkout)

def git_action(action, **arguments):
    result = reviewed('git.action', dict(action=action, **arguments), repo)
    assert result['exit'] == 0, result
    assert pathlib.Path(result['log']).is_file(), result
    return result

owner = command(['gh', 'api', 'user', '--jq', '.login'])
name = 'tangos-lite-validation-' + uuid.uuid4().hex[:12]
slug = owner + '/' + name
manifest = {'repository': slug, 'url': 'https://github.com/' + slug,
            'private': True, 'syntheticFilesOnly': True, 'retainedForReview': True}
(output / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
command(['gh', 'repo', 'create', slug, '--private', '--add-readme', '--description',
         'Disposable TangOS Lite native workflow validation; synthetic files only.'])
print('Created private synthetic fixture:', manifest['url'], flush=True)
clone = reviewed('git.clone', {'url': manifest['url'] + '.git', 'destination': str(repo)})
assert clone['exit'] == 0, clone
command(['git', 'config', 'user.name', 'TangOS Lite validation'], repo)
command(['git', 'config', 'user.email', 'validation@example.invalid'], repo)
base = command(['git', 'branch', '--show-current'], repo)
descriptor = {'tangosVersion': '1', 'project': {'name': name, 'title': 'Synthetic workflow fixture'},
              'tools': []}
(repo / 'tangos.json').write_text(json.dumps(descriptor, indent=2) + '\n', encoding='utf-8')
workflow = repo / '.github/workflows/native-validation.yml'
workflow.parent.mkdir(parents=True)
workflow.write_text('''name: Synthetic native validation
on: [push, pull_request]
permissions:
  contents: read
jobs:
  fixture:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Validate synthetic descriptor
        run: python3 -c "import json; assert json.load(open('tangos.json'))['tangosVersion'] == '1'"
''', encoding='utf-8')
git_action('Stage paths', text='tangos.json\n.github/workflows/native-validation.yml')
git_action('Commit staged', text='Add synthetic validation descriptor and check')
git_action('Push reviewed', remote='origin', ref=base)
git_action('Create branch', ref='codex/native-workflow')
git_action('Switch branch', ref='codex/native-workflow')
(repo / 'port').mkdir()
(repo / 'port/fixture.txt').write_text('Synthetic port-only workflow validation.\n', encoding='utf-8')
git_action('Stage paths', text='port/fixture.txt')
git_action('Commit staged', text='Review synthetic port-only change')
git_action('Push reviewed', remote='origin', ref='codex/native-workflow')
created = git_action('Create draft PR', remote=slug, ref=base,
                     text='Synthetic TangOS Lite native workflow validation')
url = next(line.strip() for line in created['output'].splitlines()
           if line.strip().startswith(manifest['url'] + '/pull/'))
manifest['pullRequest'] = url
(output / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
print('Created draft PR:', url, flush=True)
selector = url.rsplit('/', 1)[1]
readiness = git_action('PR readiness', remote=slug, ref=selector)
metadata = json.loads(next(line for line in readiness['output'].splitlines()
                          if line.lstrip().startswith('{')))
assert metadata['isDraft'] and metadata['state'] == 'OPEN', metadata
# Wait on actual Actions only in this explicitly opted-in fixture. The native
# readiness/check paths are exercised again after those external jobs finish.
watched = subprocess.run(['gh', 'pr', 'checks', selector, '--repo', slug, '--watch', '--interval', '5'],
                         capture_output=True, text=True, timeout=300)
(output / 'checks-watch.log').write_text(watched.stdout + watched.stderr, encoding='utf-8')
assert watched.returncode == 0, 'Actual GitHub check did not pass; inspect checks-watch.log'
checks = git_action('PR checks', remote=slug, ref=selector)
assert 'fixture' in checks['output'], checks
manifest.update(state='passed', nativeClone=True, reviewedCommits=True, firstBranchPush=True,
                draftCreation=True, actualChecksPassed=True)
(output / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
print('PASS native live GitHub clone, reviewed commits/push, draft PR, actual Actions checks', flush=True)
