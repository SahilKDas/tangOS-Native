#include <winsock2.h>
#include <ws2tcpip.h>
#include "mcp.h"
#include "network.h"
#include <iostream>
#include <algorithm>
#include <chrono>
#include <sstream>
#include <stdexcept>
#include <wincrypt.h>
namespace lite {
namespace {
constexpr const char *modernVersion = "2026-07-28";
Json protocolVersions() {
  return Json::array({modernVersion, "2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05"});
}
std::string wireHeader(const std::string &value) {
  bool safe = !value.empty() && value.front() != ' ' && value.back() != ' ' &&
              value.rfind("=?base64?", 0) != 0;
  for (unsigned char c : value)
    safe = safe && c >= 32 && c <= 126;
  if (safe)
    return value;
  DWORD count = 0;
  CryptBinaryToStringA(reinterpret_cast<const BYTE *>(value.data()), DWORD(value.size()),
                       CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &count);
  std::string encoded(count, '\0');
  if (!CryptBinaryToStringA(reinterpret_cast<const BYTE *>(value.data()), DWORD(value.size()),
                            CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, encoded.data(), &count))
    throw std::runtime_error("Cannot encode MCP header");
  encoded.resize(count);
  while (!encoded.empty() && encoded.back() == '\0')
    encoded.pop_back();
  return "=?base64?" + encoded + "?=";
}
std::string readWireHeader(const std::string &value) {
  if (value.rfind("=?base64?", 0) != 0)
    return value;
  if (value.size() < 11 || value.substr(value.size() - 2) != "?=")
    throw std::runtime_error("Invalid encoded MCP header");
  auto encoded = value.substr(9, value.size() - 11);
  DWORD count = 0;
  if (!CryptStringToBinaryA(encoded.c_str(), DWORD(encoded.size()),
                            CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT, nullptr, &count, nullptr,
                            nullptr))
    throw std::runtime_error("Invalid base64 MCP header");
  std::string decoded(count, '\0');
  if (!CryptStringToBinaryA(encoded.c_str(), DWORD(encoded.size()),
                            CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT,
                            reinterpret_cast<BYTE *>(decoded.data()), &count, nullptr, nullptr))
    throw std::runtime_error("Invalid base64 MCP header");
  decoded.resize(count);
  return decoded;
}
} // namespace
Json mcpClientConfiguration(const std::string &client, const fs::path &executable,
                            const fs::path &connection, const std::string &agent) {
  if (agent.empty())
    throw std::runtime_error("Select an MCP agent first");
  Json entry = {{"command", utf8(executable.wstring())},
                {"args", Json::array({"--mcp-stdio", utf8(connection.wstring()), agent})}};
  if (client == "VS Code") {
    entry["type"] = "stdio";
    return {{"servers", {{"tangos-lite", entry}}}};
  }
  if (client != "Claude Code" && client != "Claude Desktop" && client != "Cursor" &&
      client != "Generic")
    throw std::runtime_error("Unknown MCP client template");
  return {{"mcpServers", {{"tangos-lite", entry}}}};
}
int runMcpStdio(const fs::path &connection, const std::string &agent) {
  std::string session;
  auto input = GetStdHandle(STD_INPUT_HANDLE), output = GetStdHandle(STD_OUTPUT_HANDLE);
  auto config = Json::parse(read(connection)).at("mcpServers").at("tangos-lite");
  auto url = config.at("url").get<std::string>();
  if (url.rfind("http://127.0.0.1:", 0) != 0 || url.substr(url.size() - 4) != "/mcp")
    throw std::runtime_error("MCP bridge only connects to the local Lite server");
  auto headers = config.at("headers").get<std::map<std::string, std::string>>();
  headers["Accept"] = "application/json, text/event-stream";
  headers["MCP-Protocol-Version"] = "2025-03-26";
  std::string line;
  std::mutex outputMutex;
  struct Workers {
    std::vector<std::pair<std::thread, std::shared_ptr<std::atomic<bool>>>> rows;
    void reap() {
      for (auto it = rows.begin(); it != rows.end();)
        if (it->second->load()) {
          it->first.join();
          it = rows.erase(it);
        } else
          ++it;
    }
    ~Workers() {
      for (auto &worker : rows)
        if (worker.first.joinable())
          worker.first.join();
    }
  };
  bool hasToolCalls = false, disconnected = false, modernTraffic = false;
  auto transportInstance = uniqueId();
  auto disconnect = [&] {
    if ((session.empty() && !modernTraffic) || disconnected)
      return;
    disconnected = true;
    try {
      if (modernTraffic) {
        auto finalHeaders = headers;
        finalHeaders.erase("Mcp-Session-Id");
        finalHeaders["MCP-Protocol-Version"] = modernVersion;
        finalHeaders["Mcp-Method"] = "notifications/io.tangos/disconnect";
        Json message{{"jsonrpc", "2.0"},
                     {"method", "notifications/io.tangos/disconnect"},
                     {"params",
                      {{"_meta",
                        {{"io.modelcontextprotocol/protocolVersion", modernVersion},
                         {"io.modelcontextprotocol/clientCapabilities", Json::object()},
                         {"io.tangos/transportInstance", transportInstance}}}}}};
        requestHttp(url, "POST", message.dump(), finalHeaders);
      }
      if (session.empty())
        return;
      auto finalHeaders = headers;
      finalHeaders["Mcp-Session-Id"] = session;
      requestHttp(url, "DELETE", "", finalHeaders);
    } catch (...) {
    }
  };
  struct DisconnectGuard {
    std::function<void()> close;
    ~DisconnectGuard() { close(); }
  };
  auto send = [&](Json request, std::map<std::string, std::string> requestHeaders,
                  bool initialize) {
    try {
      auto response = requestHttp(url, "POST", request.dump(), requestHeaders);
      if (response.status != 200 && response.status != 202 && response.body.empty())
        throw std::runtime_error("Local MCP HTTP " + std::to_string(response.status));
      if (initialize && !response.session.empty())
        session = response.session;
      if (!response.body.empty()) {
        auto parsed = Json::parse(response.body);
        if (initialize && parsed.contains("result"))
          headers["MCP-Protocol-Version"] = parsed["result"].value("protocolVersion", "2025-03-26");
        auto message = parsed.dump() + "\n";
        std::lock_guard<std::mutex> lock(outputMutex);
        DWORD wrote;
        if (!WriteFile(output, message.data(), DWORD(message.size()), &wrote, nullptr) ||
            wrote != message.size())
          throw std::runtime_error("MCP output closed");
      }
    } catch (...) {
      if (request.is_object() && request.contains("id")) {
        auto message = Json({{"jsonrpc", "2.0"},
                             {"id", request["id"]},
                             {"error",
                              {{"code", -32603},
                               {"message", "Local MCP connection failed; keep Lite running and "
                                           "refresh the client config"}}}})
                           .dump() +
                       "\n";
        std::lock_guard<std::mutex> lock(outputMutex);
        DWORD wrote;
        WriteFile(output, message.data(), DWORD(message.size()), &wrote, nullptr);
      }
    }
  };
  Workers workers;
  DisconnectGuard closeOnExit{disconnect};
  char c;
  DWORD got;
  while (ReadFile(input, &c, 1, &got, nullptr) && got) {
    if (c != '\n') {
      if (line.size() >= 1024 * 1024)
        throw std::runtime_error("MCP request exceeds 1 MiB");
      line += c;
      continue;
    }
    if (trim(line).empty()) {
      line.clear();
      continue;
    }
    Json request;
    try {
      request = Json::parse(line);
      if (request.value("method", std::string()) == "initialize")
        request["params"]["clientInfo"]["name"] = agent;
      if (!session.empty())
        headers["Mcp-Session-Id"] = session;
      auto method = request.value("method", std::string());
      auto metadata = request.value("params", Json::object()).value("_meta", Json::object());
      bool modern = method == "server/discover" ||
                    metadata.contains("io.modelcontextprotocol/protocolVersion") ||
                    (modernTraffic && session.empty() && method != "initialize");
      auto requestHeaders = headers;
      if (modern) {
        modernTraffic = true;
        if (method.rfind("notifications/", 0) == 0) {
          request["params"]["_meta"]["io.modelcontextprotocol/protocolVersion"] = modernVersion;
          request["params"]["_meta"]["io.modelcontextprotocol/clientCapabilities"] = Json::object();
        }
        request["params"]["_meta"]["io.tangos/agent"] = agent;
        request["params"]["_meta"]["io.tangos/transportInstance"] = transportInstance;
        requestHeaders.erase("Mcp-Session-Id");
        requestHeaders["MCP-Protocol-Version"] =
            metadata.value("io.modelcontextprotocol/protocolVersion", std::string(modernVersion));
        requestHeaders["Mcp-Method"] = method;
        if (method == "tools/call")
          requestHeaders["Mcp-Name"] =
              wireHeader(request.at("params").at("name").get<std::string>());
      }
      if (method == "tools/call")
        hasToolCalls = true;
      if (method == "initialize")
        send(request, requestHeaders, true);
      else if (method == "notifications/cancelled")
        send(request, requestHeaders, false);
      else {
        workers.reap();
        if (workers.rows.size() >= 24)
          throw std::runtime_error("MCP bridge pending request limit reached");
        auto done = std::make_shared<std::atomic<bool>>(false);
        workers.rows.emplace_back(std::thread([&, request, requestHeaders, done] {
                                    send(request, requestHeaders, false);
                                    done->store(true);
                                  }),
                                  done);
      }
    } catch (const std::exception &e) {
      if (request.is_object() && request.contains("id")) {
        auto message = Json({{"jsonrpc", "2.0"},
                             {"id", request["id"]},
                             {"error",
                              {{"code", -32603},
                               {"message", "Local MCP connection failed; keep Lite running and "
                                           "refresh the client config"}}}})
                           .dump() +
                       "\n";
        DWORD wrote;
        std::lock_guard<std::mutex> lock(outputMutex);
        WriteFile(output, message.data(), DWORD(message.size()), &wrote, nullptr);
      }
    }
    line.clear();
  }
  // EOF after tool calls cancels session-owned work before joining blocked HTTP calls.
  if (hasToolCalls)
    disconnect();
  for (auto &worker : workers.rows)
    if (worker.first.joinable())
      worker.first.join();
  disconnect();
  return 0;
}
struct McpServer::Impl {
  Fleet &fleet;
  Descriptor descriptor;
  SOCKET listener = INVALID_SOCKET;
  std::thread server;
  std::vector<std::pair<std::thread, std::shared_ptr<std::atomic<bool>>>> clients;
  std::atomic<bool> stopping{false};
  unsigned short boundPort = 0;
  std::string token;
  std::mutex sessionsMutex;
  struct Session {
    std::string agent, name;
    int64_t connectedAt, lastSeen;
  };
  std::map<std::string, Session> sessions;
  std::map<std::string, Session> statelessPresence;
  std::mutex requestsMutex;
  struct PendingRequest {
    Runner process;
    std::atomic<bool> cancelledByPeer{false};
    void cancel() {
      cancelledByPeer = true;
      process.cancel();
    }
    bool isCancelled() const { return process.isCancelled(); }
  };
  std::map<std::string, std::shared_ptr<PendingRequest>> pendingRequests;
  struct RequestGuard {
    Impl *owner;
    std::string key;
    std::shared_ptr<PendingRequest> runner;
    std::atomic<bool> monitorDone{false};
    std::thread monitor;
    explicit RequestGuard(Impl *value) : owner(value) {}
    ~RequestGuard() {
      monitorDone = true;
      if (monitor.joinable())
        monitor.join();
      if (!runner)
        return;
      std::lock_guard<std::mutex> lock(owner->requestsMutex);
      owner->pendingRequests.erase(key);
    }
  };
  std::atomic<uint64_t> requestsSeen{0};
  std::atomic<int64_t> lastContactAt{0};
  static int64_t now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
  }
  void expireLocked() {
    auto cutoff = now() - 30 * 60 * 1000;
    for (auto it = sessions.begin(); it != sessions.end();) {
      if (it->second.lastSeen < cutoff)
        it = sessions.erase(it);
      else
        ++it;
    }
    for (auto it = statelessPresence.begin(); it != statelessPresence.end();)
      if (it->second.lastSeen < cutoff)
        it = statelessPresence.erase(it);
      else
        ++it;
  }
  Impl(Fleet &f, Descriptor d) : fleet(f), descriptor(std::move(d)) {}
  Json rpc(const Json &request, std::string &session, SOCKET socket = INVALID_SOCKET) {
    auto id = request.value("id", Json());
    RequestGuard operation(this);
    try {
      auto method = request.at("method").get<std::string>();
      auto params = request.value("params", Json::object());
      auto meta = params.value("_meta", Json::object());
      bool modern =
          method == "server/discover" || meta.contains("io.modelcontextprotocol/protocolVersion");
      std::string requestScope = session;
      if (modern) {
        if (!meta.contains("io.modelcontextprotocol/protocolVersion") ||
            !meta["io.modelcontextprotocol/protocolVersion"].is_string() ||
            !meta.contains("io.modelcontextprotocol/clientCapabilities") ||
            !meta["io.modelcontextprotocol/clientCapabilities"].is_object())
          throw std::runtime_error(
              "Every modern MCP request requires protocolVersion and clientCapabilities metadata");
        if (meta.at("io.modelcontextprotocol/protocolVersion") != modernVersion)
          return {{"jsonrpc", "2.0"},
                  {"id", id},
                  {"error",
                   {{"code", -32022},
                    {"message", "Unsupported protocol version"},
                    {"data",
                     {{"supported", protocolVersions()},
                      {"requested", meta.at("io.modelcontextprotocol/protocolVersion")}}}}}};
        auto instance = meta.value("io.tangos/transportInstance", std::string());
        if (!instance.empty() && (instance.size() > 128 ||
                                  instance.find_first_not_of("0123456789abcdef-") != instance.npos))
          throw std::runtime_error("Invalid transport instance identifier");
        requestScope = "stateless:" + (instance.empty() ? uniqueId() : instance);
      }
      if (method != "server/discover" && method != "initialize" && method != "ping" &&
          method != "tools/list" && method != "tools/call" &&
          method.rfind("notifications/", 0) != 0)
        return {{"jsonrpc", "2.0"},
                {"id", id},
                {"error", {{"code", -32601}, {"message", "Method not found: " + method}}}};
      if (method == "notifications/cancelled") {
        try {
          auto cancelled = request.at("params").at("requestId");
          if (!cancelled.is_string() && !cancelled.is_number_integer())
            return nullptr;
          std::lock_guard<std::mutex> lock(requestsMutex);
          auto found = pendingRequests.find(requestScope + "\n" + cancelled.dump());
          if (found != pendingRequests.end())
            found->second->cancel();
        } catch (...) {
        } // Unknown, completed and malformed notifications are ignored.
        return nullptr;
      }
      if (method == "notifications/io.tangos/disconnect" && modern) {
        std::lock_guard<std::mutex> lock(requestsMutex);
        auto prefix = requestScope + "\n";
        for (auto &pending : pendingRequests)
          if (pending.first.rfind(prefix, 0) == 0)
            pending.second->cancel();
        std::lock_guard<std::mutex> presenceLock(sessionsMutex);
        statelessPresence.erase(requestScope);
        return nullptr;
      }
      if (method.rfind("notifications/", 0) == 0)
        return nullptr;
      Json result;
      if (method == "server/discover") {
        result = {{"supportedVersions", protocolVersions()},
                  {"capabilities", {{"tools", Json::object()}}},
                  {"instructions", "Use next_batch with explicit io.tangos/agent metadata; follow "
                                   "the assigned AGENTS.md. No automatic commits or pushes."},
                  {"ttlMs", 0},
                  {"cacheScope", "private"}};
      } else if (method == "initialize" && !modern) {
        auto params = request.at("params");
        auto name = params.at("clientInfo").at("name").get<std::string>();
        std::string agent;
        for (auto &s : fleet.snapshot())
          if (s.spec.kind == "mcp" && s.spec.name == name) {
            if (!agent.empty())
              throw std::runtime_error(
                  "Ambiguous MCP agent name; rename duplicate agents in Controller");
            agent = s.id;
          }
        if (agent.empty())
          throw std::runtime_error(
              "Add an MCP agent in Controller with the exact clientInfo.name: " + name);
        session = uniqueId();
        {
          std::lock_guard<std::mutex> lock(sessionsMutex);
          expireLocked();
          if (sessions.size() >= 128)
            throw std::runtime_error("Too many MCP sessions; disconnect an existing client");
          sessions[session] = {agent, name, now(), now()};
        }
        auto requested = params.value("protocolVersion", std::string("2025-03-26"));
        static const std::vector<std::string> supported = {"2025-11-25", "2025-06-18", "2025-03-26",
                                                           "2024-11-05"};
        auto version = std::find(supported.begin(), supported.end(), requested) != supported.end()
                           ? requested
                           : supported.front();
        result = {{"protocolVersion", version},
                  {"capabilities", {{"tools", Json::object()}}},
                  {"serverInfo", {{"name", "TangOS Lite"}, {"version", "0.25.0"}}},
                  {"instructions", "Pull next_batch and follow its scoped AGENTS.md instructions. "
                                   "Work only in the assigned worktree."}};
      } else if (method == "ping")
        result = Json::object();
      else if (method.rfind("notifications/", 0) == 0)
        return Json();
      else {
        std::string agent;
        if (modern) {
          auto selected = meta.value("io.tangos/agent", std::string());
          for (auto &s : fleet.snapshot())
            if (s.spec.kind == "mcp" && (s.id == selected || s.spec.name == selected)) {
              if (!agent.empty())
                throw std::runtime_error("Ambiguous explicitly selected MCP agent");
              agent = s.id;
            }
          auto name = meta.value("io.modelcontextprotocol/clientInfo", Json::object())
                          .value("name", std::string("MCP client"));
          if (!agent.empty() && !meta.value("io.tangos/transportInstance", std::string()).empty()) {
            std::lock_guard<std::mutex> lock(sessionsMutex);
            if (statelessPresence.size() >= 128 && !statelessPresence.count(requestScope))
              statelessPresence.erase(statelessPresence.begin());
            auto old = statelessPresence.find(requestScope);
            statelessPresence[requestScope] = {
                agent, name, old == statelessPresence.end() ? now() : old->second.connectedAt,
                now()};
          }
        } else {
          std::lock_guard<std::mutex> lock(sessionsMutex);
          auto it = sessions.find(session);
          if (it == sessions.end())
            throw std::runtime_error("Initialize a session first");
          it->second.lastSeen = now();
          agent = it->second.agent;
        }
        if (method == "tools/list") {
          Json tools = Json::array();
          auto add = [&](const std::string &name, const std::string &description, Json schema) {
            tools.push_back(
                {{"name", name}, {"description", description}, {"inputSchema", schema}});
          };
          add("next_batch", "Claim the queued batch and read the assigned worktree instructions",
              {{"type", "object"}, {"properties", Json::object()}});
          add("finish_batch",
              "Finish current batch; changes remain uncommitted for independent verification and "
              "review",
              {{"type", "object"}, {"properties", Json::object()}});
          add("progress", "Read agent states and queue progress",
              {{"type", "object"}, {"properties", Json::object()}});
          add("backend_read",
              "Read local backend services. External connections require the user's enabled "
              "configuration; mutations are forbidden to agents.",
              {{"type", "object"},
               {"properties",
                {{"method", {{"type", "string"}}}, {"arguments", {{"type", "object"}}}}},
               {"required", Json::array({"method"})}});
          for (auto &tool : descriptor.tools) {
            if (!fleet.toolEnabled(tool.id))
              continue;
            Json properties = Json::object(), required = Json::array();
            for (auto &arg : tool.args) {
              Json p = {{"type", arg.type == "enum" ? "string" : arg.type},
                        {"description", arg.description}};
              if (arg.type == "enum")
                p["enum"] = arg.choices;
              properties[arg.name] = p;
              if (arg.required)
                required.push_back(arg.name);
            }
            add(tool.id, tool.description,
                {{"type", "object"}, {"properties", properties}, {"required", required}});
          }
          result = {{"tools", tools}};
        } else if (method == "tools/call") {
          if (!id.is_string() && !id.is_number_integer())
            throw std::runtime_error("Tool requests require a string or integer request ID");
          operation.key = requestScope + "\n" + id.dump();
          {
            std::lock_guard<std::mutex> lock(requestsMutex);
            if (pendingRequests.count(operation.key))
              throw std::runtime_error("Duplicate in-progress request ID");
            operation.runner = std::make_shared<PendingRequest>();
            pendingRequests.emplace(operation.key, operation.runner);
          }
          if (modern && socket != INVALID_SOCKET) {
            operation.monitor = std::thread([&operation, socket] {
              while (!operation.monitorDone) {
                fd_set readers;
                FD_ZERO(&readers);
                FD_SET(socket, &readers);
                timeval timeout{0, 100000};
                if (select(0, &readers, nullptr, nullptr, &timeout) > 0) {
                  char byte;
                  if (recv(socket, &byte, 1, MSG_PEEK) <= 0) {
                    operation.runner->cancel();
                    break;
                  }
                  Sleep(50);
                }
              }
            });
          }
          auto params = request.at("params");
          auto name = params.at("name").get<std::string>();
          if (modern && agent.empty() && name != "progress" && name != "backend_read")
            throw std::runtime_error(
                "Select a configured MCP agent using explicit io.tangos/agent request metadata");
          auto args = params.value("arguments", Json::object());
          std::string text;
          if (name == "backend_read")
            text =
                fleet.backend(args.at("method"), args.value("arguments", Json::object())).dump(2);
          else if (name == "next_batch") {
            auto deadline = GetTickCount64() + std::clamp(args.value("timeoutMs", 45000), 0, 45000);
            Json batch;
            do {
              if (operation.runner->isCancelled())
                return nullptr;
              batch = fleet.takeBatch(agent);
              if (batch["status"] != "empty" || stopping || GetTickCount64() >= deadline)
                break;
              Sleep(100);
            } while (true);
            text = batch.dump(2);
          } else if (name == "finish_batch") {
            fleet.finishBatch(agent, &operation.runner->process);
            text = "Batch finished. User verification/review required; do not commit or push.";
          } else if (name == "progress") {
            Json states = Json::array();
            for (auto &s : fleet.snapshot())
              states.push_back(agentJson(s));
            text = states.dump(2);
          } else {
            auto res = fleet.runTool(agent, name, args, &operation.runner->process);
            text = res.output;
            result["isError"] = res.code != 0;
          }
          result["content"] = Json::array({{{"type", "text"}, {"text", text}}});
        } else
          throw std::runtime_error("Unsupported JSON-RPC method: " + method);
      }
      if (operation.runner && operation.runner->cancelledByPeer)
        return nullptr;
      if (modern) {
        result["resultType"] = "complete";
        result["_meta"]["io.modelcontextprotocol/serverInfo"] = {{"name", "TangOS Lite"},
                                                                 {"version", "0.25.0"}};
        if (method == "tools/list") {
          result["ttlMs"] = 0;
          result["cacheScope"] = "private";
        }
      }
      return {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
    } catch (const std::exception &e) {
      if (operation.runner && operation.runner->cancelledByPeer)
        return nullptr;
      return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", -32602}, {"message", e.what()}}}};
    }
  }
  void serve(SOCKET socket) {
    ++requestsSeen;
    lastContactAt = now();
    DWORD timeout = 5000;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
    setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));
    auto reply = [&](int code, const std::string &body, const std::string &session) {
      std::string out =
          "HTTP/1.1 " + std::to_string(code) + (code == 200 ? " OK\r\n" : " Error\r\n");
      out += "Content-Type: application/json\r\nConnection: close\r\nContent-Length: " +
             std::to_string(body.size()) + "\r\n";
      if (!session.empty())
        out += "Mcp-Session-Id: " + session + "\r\n";
      out += "\r\n" + body;
      size_t at = 0;
      while (at < out.size()) {
        int n = send(socket, out.data() + at, (int)std::min<size_t>(65536, out.size() - at), 0);
        if (n <= 0)
          break;
        at += n;
      }
    };
    try {
      std::string buffer;
      char bytes[8192];
      size_t split = std::string::npos;
      while ((split = buffer.find("\r\n\r\n")) == buffer.npos) {
        int n = recv(socket, bytes, sizeof(bytes), 0);
        if (n <= 0)
          throw std::runtime_error("Incomplete request");
        buffer.append(bytes, n);
        if (buffer.size() > 65536)
          throw std::runtime_error("Headers too large");
      }
      auto headers = buffer.substr(0, split);
      std::istringstream lines(headers);
      std::string line;
      std::getline(lines, line);
      bool deleting = line == "DELETE /mcp HTTP/1.1\r";
      if (line != "POST /mcp HTTP/1.1\r" && !deleting) {
        reply(405, "{}", "");
        closesocket(socket);
        return;
      }
      std::map<std::string, std::string> fields;
      while (std::getline(lines, line)) {
        auto at = line.find(':');
        if (at == line.npos)
          continue;
        auto key = line.substr(0, at);
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (fields.count(key))
          throw std::runtime_error("Duplicate HTTP header");
        fields[key] = trim(line.substr(at + 1));
      }
      // An authenticated loopback endpoint, with no browser-origin access or
      // chunked body ambiguity. Never bind to public interfaces.
      if (fields.count("origin") || fields.count("transfer-encoding") ||
          fields["authorization"] != "Bearer " + token) {
        reply(403, "{}", "");
        closesocket(socket);
        return;
      }
      if (deleting) {
        bool removed;
        {
          std::lock_guard<std::mutex> lock(sessionsMutex);
          removed = sessions.erase(fields["mcp-session-id"]) != 0;
        }
        if (removed) {
          std::lock_guard<std::mutex> lock(requestsMutex);
          auto prefix = fields["mcp-session-id"] + "\n";
          for (auto &pending : pendingRequests)
            if (pending.first.rfind(prefix, 0) == 0)
              pending.second->cancel();
        }
        reply(removed ? 200 : 404, "{}", "");
        closesocket(socket);
        return;
      }
      auto length = std::stoull(fields.at("content-length"));
      if (length > 1024 * 1024)
        throw std::runtime_error("Request exceeds 1 MiB");
      auto body = buffer.substr(split + 4);
      while (body.size() < length) {
        int n = recv(socket, bytes, sizeof(bytes), 0);
        if (n <= 0)
          throw std::runtime_error("Incomplete body");
        body.append(bytes, n);
      }
      Json request;
      try {
        request = Json::parse(body.substr(0, length));
      } catch (const Json::parse_error &) {
        reply(400,
              Json({{"jsonrpc", "2.0"},
                    {"id", nullptr},
                    {"error", {{"code", -32700}, {"message", "Parse error"}}}})
                  .dump(),
              "");
        closesocket(socket);
        return;
      }
      auto id = request.is_object() ? request.value("id", Json()) : Json();
      auto fail = [&](int status, int code, const std::string &message, Json data = nullptr) {
        Json error = {{"code", code}, {"message", message}};
        if (!data.is_null())
          error["data"] = data;
        reply(status, Json({{"jsonrpc", "2.0"}, {"id", id}, {"error", error}}).dump(), "");
      };
      if (!request.is_object() || request.value("jsonrpc", std::string()) != "2.0" ||
          !request.contains("method") || !request["method"].is_string() ||
          (request.contains("id") && !id.is_string() && !id.is_number_integer())) {
        fail(400, -32600, "Invalid JSON-RPC request");
        closesocket(socket);
        return;
      }
      auto method = request["method"].get<std::string>();
      auto params = request.value("params", Json::object());
      if (!params.is_object()) {
        fail(400, -32602, "MCP parameters must be an object");
        closesocket(socket);
        return;
      }
      auto meta = params.is_object() ? params.value("_meta", Json::object()) : Json::object();
      bool modern = method == "server/discover" ||
                    meta.contains("io.modelcontextprotocol/protocolVersion") ||
                    fields["mcp-protocol-version"] == modernVersion;
      auto session = modern ? std::string() : fields["mcp-session-id"];
      if (modern) {
        if (!meta.is_object() || !meta.contains("io.modelcontextprotocol/protocolVersion") ||
            !meta["io.modelcontextprotocol/protocolVersion"].is_string() ||
            !meta.contains("io.modelcontextprotocol/clientCapabilities") ||
            !meta["io.modelcontextprotocol/clientCapabilities"].is_object()) {
          fail(400, -32602,
               "Required per-request protocolVersion and clientCapabilities metadata is missing");
          closesocket(socket);
          return;
        }
        auto version = meta["io.modelcontextprotocol/protocolVersion"].get<std::string>();
        bool mirrors = fields["mcp-protocol-version"] == version && fields["mcp-method"] == method;
        if (method == "tools/call" || method == "resources/read" || method == "prompts/get") {
          auto key = method == "resources/read" ? "uri" : "name";
          try {
            mirrors = mirrors && params.contains(key) && params[key].is_string() &&
                      fields.count("mcp-name") &&
                      readWireHeader(fields["mcp-name"]) == params[key].get<std::string>();
          } catch (...) {
            mirrors = false;
          }
        }
        if (!mirrors) {
          fail(400, -32020, "MCP routing headers do not match request metadata");
          closesocket(socket);
          return;
        }
        if (version != modernVersion) {
          fail(400, -32022, "Unsupported protocol version",
               {{"supported", protocolVersions()}, {"requested", version}});
          closesocket(socket);
          return;
        }
      }
      if (!session.empty()) {
        std::lock_guard<std::mutex> lock(sessionsMutex);
        expireLocked();
        if (!sessions.count(session)) {
          reply(404, "{}", "");
          closesocket(socket);
          return;
        }
        sessions.at(session).lastSeen = now();
      }
      auto result = rpc(request, session, socket);
      int status = result.is_null() ? 202 : 200;
      if (modern && result.contains("error")) {
        auto code = result["error"].value("code", 0);
        status = code == -32601 ? 404 : 400;
      }
      reply(status, result.is_null() ? "" : result.dump(), modern ? "" : session);
    } catch (const std::exception &) {
      reply(400, "{}", "");
    }
    closesocket(socket);
  }
};
McpServer::McpServer(Fleet &fleet, Descriptor descriptor, fs::path config, unsigned short port)
    : impl(std::make_unique<Impl>(fleet, std::move(descriptor))) {
  WSADATA data;
  if (WSAStartup(MAKEWORD(2, 2), &data))
    throw std::runtime_error("Winsock startup failed");
  impl->token = uniqueId() + uniqueId();
  impl->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  BOOL exclusive = TRUE;
  setsockopt(impl->listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (char *)&exclusive,
             sizeof(exclusive));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(port);
  if (bind(impl->listener, (sockaddr *)&addr, sizeof(addr)) || listen(impl->listener, 16)) {
    closesocket(impl->listener);
    WSACleanup();
    throw std::runtime_error("Cannot bind loopback MCP server");
  }
  int size = sizeof(addr);
  getsockname(impl->listener, (sockaddr *)&addr, &size);
  impl->boundPort = ntohs(addr.sin_port);
  write(config, configuration());
  impl->server = std::thread([this] {
    while (!impl->stopping) {
      auto client = accept(impl->listener, nullptr, nullptr);
      if (client == INVALID_SOCKET)
        break;
      for (auto it = impl->clients.begin(); it != impl->clients.end();) {
        if (*it->second) {
          it->first.join();
          it = impl->clients.erase(it);
        } else
          ++it;
      }
      if (impl->clients.size() >= 32) {
        closesocket(client);
        continue;
      }
      auto done = std::make_shared<std::atomic<bool>>(false);
      impl->clients.emplace_back(std::thread([this, client, done] {
                                   impl->serve(client);
                                   *done = true;
                                 }),
                                 done);
    }
  });
}
McpServer::~McpServer() {
  impl->stopping = true;
  closesocket(impl->listener);
  impl->fleet.stopExternal();
  if (impl->server.joinable())
    impl->server.join();
  for (auto &worker : impl->clients)
    if (worker.first.joinable())
      worker.first.join();
  WSACleanup();
}
unsigned short McpServer::port() const { return impl->boundPort; }
Json McpServer::state() const {
  std::lock_guard<std::mutex> lock(impl->sessionsMutex);
  impl->expireLocked();
  Json clients = Json::array();
  for (auto &entry : impl->sessions)
    clients.push_back({{"id", entry.first},
                       {"agentId", entry.second.agent},
                       {"name", entry.second.name},
                       {"connectedAt", entry.second.connectedAt},
                       {"lastSeen", entry.second.lastSeen}});
  for (auto &entry : impl->statelessPresence)
    clients.push_back({{"id", entry.first},
                       {"agentId", entry.second.agent},
                       {"name", entry.second.name},
                       {"connectedAt", entry.second.connectedAt},
                       {"lastSeen", entry.second.lastSeen}});
  return {{"running", true},
          {"url", "http://127.0.0.1:" + std::to_string(impl->boundPort) + "/mcp"},
          {"connectedClients", clients.size()},
          {"requestsSeen", impl->requestsSeen.load()},
          {"lastContactAt", impl->lastContactAt.load()},
          {"clients", clients}};
}
std::string McpServer::configuration() const {
  return Json({{"mcpServers",
                {{"tangos-lite",
                  {{"type", "http"},
                   {"url", "http://127.0.0.1:" + std::to_string(impl->boundPort) + "/mcp"},
                   {"headers", {{"Authorization", "Bearer " + impl->token}}}}}}}})
      .dump(2);
}
} // namespace lite
