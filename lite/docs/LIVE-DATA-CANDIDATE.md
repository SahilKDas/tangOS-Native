# Live-data candidate — 2026-10-11

This checkpoint implements the shared Atlas service and UI workers, but is **not a verified release**. The user requested finishing the current build and stopping coding. The existing verified executable remains in `lite/release/`; the candidate is in `lite/release-live-candidate/`. Do not describe this checkpoint as full parity.

Connections remain disabled until configured by the user in local `connections.json`. A profile uses a full project-specific URL and optional `keyEnv`, `keyHeader`, `keyPrefix`; secrets remain in the local vault/environment. No provider account, key or hosted service is bundled. Example (enable only your own endpoints):

```json
{
  "atlas.events": {"enabled": false, "url": "https://your-service.example/live", "method": "GET", "keyEnv": "MY_SERVICE_KEY"},
  "atlas.live": {"enabled": false, "url": "https://your-service.example/atlas", "method": "GET", "keyEnv": "MY_SERVICE_KEY"},
  "github.credits": {"enabled": false, "url": "https://api.github.com/repos/YOUR_OWNER/YOUR_REPO", "mode": "github-api", "keyEnv": "MY_GITHUB_KEY"}
}
```

The UI also requires the user's Live Refresh preference for SSE. Disabled connections and unopened remote projects do not start a stream. The Windows transport uses asynchronous WinHTTP, bounded SSE frames, reconnect delay, cancellable reads and no redirects. Feed events are coalesced before UI processing, keeping worker threads separate from drawing. Publication invalidates live-cache throttling; progress preserves already-matched local rows.

`atlas.load`, `atlas.current`, `atlas.live`, `atlas.state` and feed reads share project/descriptor/connection/credential-scoped state. `atlas.current` performs no network request. Passive live reads use 60 seconds; forced reads use 20 seconds; publication bypasses those timers. Failed refresh can retain last-good live data. Connection or credential changes create a separate cache scope. Cache writes are atomic, bounded and serialized across processes, with a 25 GiB free-space guard.

`atlas.refreshStatus` reports stale input files, including changes inside existing source/configuration directories. `atlas.authorizeRefresh` requires a confirmed command preview before automatic generation. Script/descriptor changes revoke approval; `enabled: false` revokes it explicitly. `atlas.refresh` runs only approved stale generation, uses cancellable command execution and retained logs, and invalidates local cached data after success. Repository scripts are trusted code, not an OS sandbox; output-path protection does not sandbox arbitrary scripts. No ROM or asset distribution is added.

GitHub mapping mode reads contributors, up to eight commit pages, and PR authors from the user's configured API repository base. It excludes contributor/PR bots, deduplicates PR authors, maps noreply/email/name aliases, retains first alias mappings, caches results, supports forced reads and reports partial endpoint failures. The existing mapped-response mode remains supported.

Validation commands after sufficient storage is available:

```powershell
cmake -S lite -B lite/out/live-state -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build lite/out/live-state -j 1
ctest --test-dir lite/out/live-state --output-on-failure
python lite/tests/test_live_data_reference.py --exe lite/out/live-state/TangOSLite.exe
python lite/tests/test_sse_transport.py --helper lite/out/live-state/lite_live_data_tests.exe
```

Twenty direct comparisons against retained Console feed/SSE/GitHub code passed. Full native state tests, actual reconnect/cache integration, packaged GUI workflows and two clean identical builds must pass before promoting this candidate. Backend and frontend full parity remain unproven. No external account workflows were exercised for this checkpoint.
Final build: **7,077,376 bytes**, SHA256 **BC318A7AE8D8F5907EA8B105C2467B237CD2D52BEEB94A62C37D750BCF3E4B12**. Eight native suites passed; backend parity and Atlas live-state suites stopped at the 25 GiB cache guard (120.55 seconds total). Packaged loopback GET/POST, confirmation, credential redaction and redirect checks passed. The packaged GUI workflow stopped at the 25 GiB capture guard. Actual SSE/cache integration and clean-build reproduction remain pending. Generated build trees were removed after retaining compact evidence. No source changes followed this build.
