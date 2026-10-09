"""Exercise an installed VS Code CLI without touching the user's profile.

This tests configuration acceptance, not authenticated MCP tool execution.
VS Code is an external development test dependency, never a release dependency.
"""
import argparse
import json
import os
import pathlib
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', required=True)
    parser.add_argument('--code', required=True)
    parser.add_argument('--cli', required=True)
    args = parser.parse_args()
    exe, code, cli = [pathlib.Path(value).resolve(strict=True)
                      for value in (args.exe, args.code, args.cli)]
    with tempfile.TemporaryDirectory(prefix='tangos-vscode-client-') as folder:
        profile = pathlib.Path(folder)
        definition = {'name': 'tangos-lite', 'type': 'stdio', 'command': str(exe),
                      'args': ['--mcp-stdio', str(profile / 'connection.json'), 'fixture-agent']}
        result = subprocess.run(
            [str(code), str(cli), '--user-data-dir', str(profile), '--add-mcp', json.dumps(definition)],
            env=dict(os.environ, ELECTRON_RUN_AS_NODE='1'), capture_output=True, text=True, timeout=40)
        assert result.returncode == 0, result.stderr
        files = list(profile.rglob('mcp.json'))
        assert len(files) == 1, files
        installed = json.loads(files[0].read_text(encoding='utf-8-sig'))['servers']['tangos-lite']
        for key in ('command', 'args', 'type'):
            assert installed[key] == definition[key], (key, installed)
    print('PASS installed VS Code CLI accepts native stdio definition in an isolated profile')
    print('Registration only; authenticated server/tool execution remains unverified')


if __name__ == '__main__':
    main()
