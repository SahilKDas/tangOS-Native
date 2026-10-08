# Native backend and user-owned connections

TangOS Lite provides a native backend callable from the executable and a read-only bridge for authenticated MCP agents. It does not need the repository owner's API keys, accounts, OAuth application or hosted server. Credentials are never compiled into source or release binaries.

## Calling services

Place requests and responses outside the selected Git checkout. Use:

```powershell
.\TangOSLite.exe --backend C:\work\decomp C:\Users\you\AppData\Local\TangOSLite request.json response.json
```

Use `-` instead of the repository path for project registration, preferences, reports and external-connection services that do not require a checkout. Each request is `{ "method": "catalog", "arguments": {} }`. The catalog lists supported methods. Exit 0 means the request was handled; inspect the returned HTTP status or tool exit code too. Errors return JSON and exit 1.

Mutations first return a concrete preview and a single-use confirmation. Inspect it and resubmit the same arguments with its `confirmation` value. Confirmations expire after ten minutes and are invalidated by changed repository content or settings. Commit previews contain the entire staged diff; push previews contain outgoing history. Agents cannot confirm these operations through MCP.

## Connection ownership

`connections.set` writes local `connections.json`, never a repository file. Each profile needs the user's explicit `enabled: true` and a full URL/method. There are no enabled profiles by default. For example:

```json
{
  "atlas.live": {"enabled": false, "url": "https://your-service.example/atlas", "method": "GET"},
  "claims": {"enabled": false, "url": "https://your-service.example/claims?project=your-project", "method": "GET", "keyEnv": "MY_CLAIMS_KEY"}
}
```

Store credentials in Settings > Key vault or supply an environment variable on your own machine. A profile references `keyEnv`, `keyHeader` (default Authorization) and `keyPrefix` (default Bearer plus a space). Do not put credentials in URLs, requests, prompts or JSON settings. API provider model/base URL/key choices remain in the user's AI profile. GitHub uses the user's existing Git/gh authentication; run `gh auth login` yourself if needed.

`network.read` calls an enabled GET profile. `network.write` needs its own confirmed preview. HTTPS is required; HTTP is allowed only for 127.0.0.1 test/local services. Redirects are refused so headers cannot be forwarded to another destination. Native WinHTTP bounds body sizes and timeouts. Returned environment/vault secrets are redacted. Test services use fake credentials and loopback only.

## Remote leases

Optional automatic API/CLI coordination requires three user-enabled profiles: `claims.acquire`, `claims.heartbeat`, and `claims.release`, each with `automatic: true`, its own URL, method, key reference and server-specific `bodyTemplate`. The user must configure all three before a run. Their templates can use whole JSON placeholders `{module}`, `{start}`, `{end}`, `{agent}`, `{name}`, `{lease}`. Literal project/handle fields can be supplied in the template. `successField` defaults to `ok`, `leaseField` to `lease`, and heartbeatSeconds to 15.

These profiles adapt to the user's claims service without embedding a server endpoint or key. The service must provide atomic acquisition and expiring leases. Refusal prevents execution; heartbeat failure cancels the process tree; normal finish, cancellation and partial acquisition release acquired leases. A service/network failure can prevent release; its TTL and claim board remain authoritative. MCP clients can read claims and use user-coordinated reservations; their external connection setup remains theirs.

Local CLAIMS.md active/partial rows and remote module/address ranges prevent assignment of held work. `queue.adopt` vets an existing JSONL queue rather than blindly recreating it. Local fleet queues continue to reject duplicate targets.

## Capability map

- Projects: local registration, remote/no-clone descriptor registration, list/get; user-triggered Git clone into a fresh local directory.
- Descriptors: detection/check-derived generation preview, validated writing; tool listing, visibility policies, typed argv and confirmed manual execution.
- Atlas: local data/source, filtered parent/child attempt history and best near-miss tip; configured live atlas, cosmetics, counts, progress and GitHub credits reads. A frontend can poll these services; no background connection starts just by opening the app.
- Coordination: persistent local fleet queues/worktrees, external queue adoption, claim overlays and optional atomic remote leases for API/CLI runs; authenticated MCP read bridge and waiting next_batch.
- Policy: source transcription classification, adaptive roles, safe mode, delegation/fanout instructions, optional user-enabled landing in isolated worktrees. Port-only mode always blocks source landing. Auto-push is refused because every push must be previewed.
- Recovery/observability: retained logs, local harvest records, lifetime/recent statistics, opt-in activity reporting, credential-free report export and explicit local changed-file backups with manifests. Driver-declared matches are reported separately from independent verification, never treated as byte proof.
- Git/GitHub: existing fetch/pull/merge/rebase/commit/push/PR helpers plus confirmed branch/worktree creation, clean checkout, rebase abort/continue and explicit path discard. Explicit sync previews an exact target SHA and allowed untracked deletions, requires confirmation, backs up local changes and pins old history before resetting. Protected/ignored local assets are preserved, and port-only source changes block sync. Force pushing is not provided.
- Requirements/update: local preflight exposes available checks, Python and user GitHub-auth status; configured update-manifest lookup is supported, while downloading/installing and authentication remain user-controlled.

## Scope and validation limits

Connections, Requirements, Services, queue editing and source/history screens now expose more native services. Remaining frontend widgets and exact public-contract parity are tracked separately. Native backend tests use disposable repositories, mock transports and a real loopback HTTP server. Actual provider/server interoperability and ROM/compiler-dependent verification need the user's own setup. No paid API, owner API key, real remote write or game data is needed to build or test. Third-party service protocols and independent clients cannot be inferred from a URL: configure their profiles/contracts locally. External executables are trusted code, not sandboxed processes.

Statistics count unique functions, preserve late first wins in the 16-outcome recent ring, use the reference size buckets and near-miss ratio, and retain per-agent token totals when drivers report them. Driver-declared matches remain explicitly labeled as declarations rather than independent byte proof. Local comparisons against the original statistics implementation require Node only during development; Node is never packaged.

Clone service execution now accepts a caller-owned process runner. The native Services screen lists git.clone and provides Cancel; Stop also cancels clone work, and repository switching is blocked while a service operation is pending. Caller cancellation returns an explicit cancelled flag, retains its durable log even before launch, and resetting the runner permits another clone. Disposable local repositories test cancellation and restart without external authentication. HTTP transport cancellation is not provided by this runner.
