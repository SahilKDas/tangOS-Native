# Native Console and fleet

The Controller footer centers Writes, Review and Push. Writes toggles permission
for declared mutating tools and is remembered locally. Review stays ON: click it
to inspect the selected AI's isolated work, or Git controls when no AI is selected.
Push stays OFF: click it for the outgoing-commit preview and reviewed Git workflow.
Unattended publishing is disabled to preserve the required preview of every push.
Click the document icon for Encyclopedia; right-click it for Git, Batches,
Settings, Tango guide, and Help/updates/reports. GitHub sign-in uses your own `gh`
credentials. No sign-in or provider call starts automatically.

Choose your own checkout. TangOS Lite reads that repository's tangos.json; no SM64DS path, fork, provider model or API credential is built into the program.

1. Open Settings and select Advanced interface, then use Chaos Controller's Add AI. Choose API, CLI or MCP; configure the provider URL/model and encrypted key variable, or an explicit CLI command. API URLs require HTTPS except local test servers.
2. Pick a role, effort, count, attempts and parallel jobs. Go previews execution and creates a codex/lite-* branch in an isolated worktree. Root and nested AGENTS.md plus descriptor rules are included in instructions. Continuous execution is optional.
3. Chaos Viewer displays local/published atlas data. Group, filter, search and zoom; Ctrl-click targets into the cart, then assign them to a selected AI. A target cannot be queued by two local agents. Published data loads only when you choose Live data.
4. Each card shows progress and live output. Stop terminates its process tree and keeps worktrees, queued work and complete logs. Details opens logs and the worktree.
5. Review changes shows the entire staged diff. Commit reviewed checks that the staged tree has not changed. It commits only the isolated branch. Use Git & reviews to merge it and separately preview outgoing commits before pushing.
6. Python matching drivers that expose INSTRUCTIONS/SYSTEM_PROMPT/SYSTEM are wrapped with the repository rules. Drivers without an instruction hook fail closed. CLI commands must consume {prompt}, {worklist} and {out} as appropriate; {prompt} is an instruction-file path.
7. Drivers may produce results.output rather than source changes. Land driver results invokes the descriptor's console.land tool in the isolated worktree, runs independent available checks, then requires diff review. Landing is blocked in port-only mode because decomp landing can change src/.

Encyclopedia discovers the repository's tools and typed arguments. Edit arguments opens a native form; Run previews the exact argv and requires permission for writes/apply. Mutating tools in the primary checkout are blocked in port-only mode. Only run trusted repository code.

Settings stores API keys with Windows DPAPI. Agent definitions and queues use human-readable fleet.json under local application data; worktrees and logs persist there too. Secrets are supplied to child environments and redacted from captured logs, including pipe-chunk boundaries. Avoid writing credentials into command arguments or prompts.

MCP configuration provides a loopback HTTP endpoint and random bearer token. Configure your client with the generated local JSON, then initialize with the configured agent name. next_batch returns instructions/targets; descriptor tools operate in that agent's worktree; finish_batch runs independent checks. This is an authenticated local service, not a public server.

An isolated worktree starts from committed HEAD. Untracked toolchains and local assets are not copied automatically. Configure your own permitted paths/toolchain and inspect missing-input errors. Verification remains unverified when no repository gate is available; driver exit success is not proof of byte matching.

The MCP connection panel shows server status, active client sessions and request traffic. Start/Stop runs in the background and remembers your choice. Copy config and Copy AI prompt provide setup for your own client; no external client file is modified. Stopping this server cancels external MCP jobs while preserving independent API/CLI jobs. Clients can disconnect using authenticated DELETE /mcp; idle sessions expire after 30 minutes.

MCP request cancellation uses `notifications/cancelled` with the in-progress
request ID. It cancels that session's waiting batch request or tool process and
suppresses the cancelled response. Cancelling a tool retains its complete log
and leaves its batch available for another tool call. Stopping the agent/server
still revokes the batch. Unknown, completed, malformed and other-session request
IDs are ignored. The stdio bridge accepts cancellation while tools are running;
it has 24 concurrent request slots and reserves notification handling separately.
External network reads retain their bounded timeout, so cancellation can suppress
their response without interrupting an already-blocked network request.

Open Batches from Controller to save a title, instructions and Viewer cart as a project draft. Choose an agent and enqueue it, then use Go in Controller to execute. The batch history keeps up to 30 completed records and preserves all log files. Stop an agent before reordering/removing its batch. Worked means attempted/processed; it does not assert byte matching. Interrupted batches and queues from earlier Lite versions are recovered.

Go drains all queued execution chunks even in one-shot mode; continuous mode additionally schedules work when its queue empties. API agents stop individually on an explicit usage-exhaustion message or five consecutive runs under 20 seconds without a landed match or compiling near-miss. Slow/productive runs clear the streak, and manual Go restarts it. The stopped batch stays pending. Every execution preserves a separate instruction file, worklist and result file; complete stream logs remain appended. Actual driver results[] and source/token summaries are ingested with the transcription gate. Driver declarations remain separate from independent byte-match proof.

Authoritative per-target `results` ledgers now control completed-work accounting for API/CLI and MCP finishes. Reported targets are removed from the queue; omitted targets remain reserved and queued. A zero-progress ledger parks a one-shot run for manual inspection rather than repeatedly spinning. Partial ledgers can drain the remaining targets over later runs; restart preserves zero-progress pending work. Legacy drivers that do not provide per-target ledgers retain aggregate completion behavior. Independent checks and review still decide whether changes are safe to commit; attempted work is not proof of a match. Token-only summaries accept input/output aliases without phantom attempts, and null output totals use the original post-transcription declared-landing fallback. Forty-eight actual original-code token comparisons cover these cases.
