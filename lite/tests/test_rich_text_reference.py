"""Compare native styled help spans with the retained renderer's actual parser."""
import argparse
import itertools
import json
import pathlib
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--exe', required=True)
exe = pathlib.Path(parser.parse_args().exe).resolve()
root = pathlib.Path(__file__).resolve().parents[2]
source = (root / 'console/src/renderer/src/richText.tsx').read_text(encoding='utf-8')
body = source[source.index('const JOKE ='):]
body = body.replace('export function', 'function').replace('text: string | undefined', 'text')
body = body.replace(': ReactNode', '').replace('const parts[]', 'const parts')
body = body.replace('let m: RegExpExecArray | null', 'let m')
body = body.replace('<Fragment key={key++}>{text.slice(last, m.index)}</Fragment>',
                    '{text:text.slice(last,m.index),joke:false}')
body = body.replace('<Fragment key={key++}>{text.slice(last)}</Fragment>',
                    '{text:text.slice(last),joke:false}')
body, count = re.subn(r'<span className="joke-text" key=\{key\+\+\}>\s*\{m\[1\]\}\s*</span>',
                      '{text:m[1],joke:true}', body)
assert count == 1, 'Reference markup changed; update the node-only adapter'
cases = [''] + [prefix + marker + suffix for prefix, marker, suffix in itertools.product(
    ('', 'Before ', 'λ🐢\n', '<script>'),
    (':joke[]', ':joke[food pellets]', ':joke[λ🐢]', ':joke[broken',
     ':joke[a]:joke[b]', ':joke[:joke[nested]]', 'plain', ':JOKE[x]'),
    ('', ' after', ']', '\n:joke[second]'))]
with tempfile.TemporaryDirectory(prefix='tangos-richtext-reference-') as folder:
    tmp = pathlib.Path(folder)
    fixture = tmp / 'cases.json'
    fixture.write_text(json.dumps(cases), encoding='utf-8')
    runner = tmp / 'reference.cjs'
    runner.write_text(body + "\nconst fs=require('fs');process.stdout.write(JSON.stringify(" +
        "JSON.parse(fs.readFileSync(process.argv[2],'utf8')).map(text=>{" +
        "const result=richText(text);return typeof result==='string' ? " +
        "(result ? [{text:result,joke:false}] : []) : result;})));", encoding='utf-8')
    reference = subprocess.run(['node', str(runner), str(fixture)], capture_output=True,
                               text=True, encoding='utf-8', timeout=20)
    assert reference.returncode == 0, reference.stderr
    for text, expected in zip(cases, json.loads(reference.stdout)):
        request, response = tmp / 'request.json', tmp / 'response.json'
        request.write_text(json.dumps({'method':'guide.richText', 'arguments':{'text':text}}),
                           encoding='utf-8')
        native = subprocess.run([str(exe), '--backend', '-', str(tmp/'data'),
                                 str(request), str(response)], capture_output=True, timeout=20)
        assert native.returncode == 0, native.stderr
        actual = json.loads(response.read_text(encoding='utf-8-sig'))
        assert actual == expected, (text, actual, expected)
print(f'PASS {len(cases)} original styled help span comparisons')
