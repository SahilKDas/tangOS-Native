#include <winsock2.h>
#include <ws2tcpip.h>
#include "mcp.h"
#include <algorithm>
#include <sstream>
#include <stdexcept>
namespace lite {
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
  std::map<std::string, std::string> sessions;
  Impl(Fleet &f, Descriptor d) : fleet(f), descriptor(std::move(d)) {}
  Json rpc(const Json &request, std::string &session) {
    auto id = request.value("id", Json());
    try {
      auto method = request.at("method").get<std::string>();
      Json result;
      if (method == "initialize") {
        auto params = request.at("params");
        auto name = params.at("clientInfo").at("name").get<std::string>();
        std::string agent;
        for (auto &s : fleet.snapshot())
          if (s.spec.kind == "mcp" && s.spec.name == name) {
            agent = s.id;
            break;
          }
        if (agent.empty())
          throw std::runtime_error(
              "Add an MCP agent in Controller with the exact clientInfo.name: " + name);
        session = uniqueId();
        {
          std::lock_guard<std::mutex> lock(sessionsMutex);
          sessions[session] = agent;
        }
        result = {{"protocolVersion", "2025-03-26"},
                  {"capabilities", {{"tools", Json::object()}}},
                  {"serverInfo", {{"name", "TangOS Lite"}, {"version", "0.3.0"}}},
                  {"instructions", "Pull next_batch and follow its scoped AGENTS.md instructions. "
                                   "Work only in the assigned worktree."}};
      } else if (method == "ping")
        result = Json::object();
      else if (method.rfind("notifications/", 0) == 0)
        return Json();
      else {
        std::string agent;
        {
          std::lock_guard<std::mutex> lock(sessionsMutex);
          auto it = sessions.find(session);
          if (it == sessions.end())
            throw std::runtime_error("Initialize a session first");
          agent = it->second;
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
          for (auto &tool : descriptor.tools) {
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
          auto params = request.at("params");
          auto name = params.at("name").get<std::string>();
          auto args = params.value("arguments", Json::object());
          std::string text;
          if (name == "next_batch")
            text = fleet.takeBatch(agent).dump(2);
          else if (name == "finish_batch") {
            fleet.finishBatch(agent);
            text = "Batch finished. User verification/review required; do not commit or push.";
          } else if (name == "progress") {
            Json states = Json::array();
            for (auto &s : fleet.snapshot())
              states.push_back(agentJson(s));
            text = states.dump(2);
          } else {
            auto res = fleet.runTool(agent, name, args);
            text = res.output;
            result["isError"] = res.code != 0;
          }
          result["content"] = Json::array({{{"type", "text"}, {"text", text}}});
        } else
          throw std::runtime_error("Unsupported JSON-RPC method: " + method);
      }
      return {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
    } catch (const std::exception &e) {
      return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", -32602}, {"message", e.what()}}}};
    }
  }
  void serve(SOCKET socket) {
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
      if (line != "POST /mcp HTTP/1.1\r") {
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
      auto session = fields["mcp-session-id"];
      auto result = rpc(Json::parse(body.substr(0, length)), session);
      reply(result.is_null() ? 202 : 200, result.is_null() ? "" : result.dump(), session);
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
  impl->fleet.stopAll();
  if (impl->server.joinable())
    impl->server.join();
  for (auto &worker : impl->clients)
    if (worker.first.joinable())
      worker.first.join();
  WSACleanup();
}
unsigned short McpServer::port() const { return impl->boundPort; }
std::string McpServer::configuration() const {
  return Json({{"mcpServers",
                {{"tangos-lite",
                  {{"type", "http"},
                   {"url", "http://127.0.0.1:" + std::to_string(impl->boundPort) + "/mcp"},
                   {"headers", {{"Authorization", "Bearer " + impl->token}}}}}}}})
      .dump(2);
}
} // namespace lite
