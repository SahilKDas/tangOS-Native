# Native Console and fleet

Choose your own checkout. TangOS Lite reads that repository's tangos.json; no SM64DS path, fork, provider model or API credential is built into the program.

1. Open Chaos Controller and Add AI. Choose API, CLI or MCP; configure the provider URL/model and encrypted key variable, or an explicit CLI command. API URLs require HTTPS except local test servers.
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

Open Batches from Controller to save a title, instructions and Viewer cart as a project draft. Choose an agent and enqueue it, then use Go in Controller to execute. The batch history keeps up to 30 completed records and preserves all log files. Stop an agent before reordering/removing its batch. Worked means attempted/processed; it does not assert byte matching. Interrupted batches and queues from earlier Lite versions are recovered.
