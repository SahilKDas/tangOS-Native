#include "network.h"
#include "core.h"
#include <windows.h>
#include <winhttp.h>
#include <stdexcept>
#include <array>
#include <mutex>
namespace lite {
namespace {
struct Internet {
  HINTERNET handle = nullptr;
  ~Internet() {
    if (handle)
      WinHttpCloseHandle(handle);
  }
};
} // namespace
HttpResponse requestHttp(const std::string &url, const std::string &method, const std::string &body,
                         const std::map<std::string, std::string> &headers) {
  if (method != "GET" && method != "POST" && method != "DELETE" && method != "PUT" &&
      method != "PATCH" && method != "DELETE")
    throw std::runtime_error("Unsupported HTTP method");
  if (body.size() > 1024 * 1024)
    throw std::runtime_error("Request body exceeds 1 MiB");
  auto address = wide(url);
  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof(parts);
  parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength =
      parts.dwUserNameLength = parts.dwPasswordLength = (DWORD)-1;
  if (!WinHttpCrackUrl(address.c_str(), 0, 0, &parts) ||
      (parts.nScheme != INTERNET_SCHEME_HTTPS &&
       !(parts.nScheme == INTERNET_SCHEME_HTTP &&
         std::wstring(parts.lpszHostName, parts.dwHostNameLength) == L"127.0.0.1")) ||
      parts.dwUserNameLength || parts.dwPasswordLength)
    throw std::runtime_error("Atlas URL must be HTTPS without credentials");
  Internet session{WinHttpOpen(L"TangOSLite/0.3", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
  if (!session.handle)
    throw std::runtime_error("Cannot initialize HTTPS");
  bool localMcp = parts.nScheme == INTERNET_SCHEME_HTTP &&
                  std::wstring(parts.lpszHostName, parts.dwHostNameLength) == L"127.0.0.1" &&
                  std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) == L"/mcp";
  // Local tools and next_batch are long-running, while external services stay bounded.
  WinHttpSetTimeouts(session.handle, 5000, 5000, 10000, localMcp ? 600000 : 10000);
  auto host = std::wstring(parts.lpszHostName, parts.dwHostNameLength);
  auto path = std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) +
              (parts.dwExtraInfoLength ? std::wstring(parts.lpszExtraInfo, parts.dwExtraInfoLength)
                                       : std::wstring());
  Internet connect{WinHttpConnect(session.handle, host.c_str(), parts.nPort, 0)},
      request{WinHttpOpenRequest(connect.handle, wide(method).c_str(), path.c_str(), nullptr,
                                 WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                 parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)};
  if (!connect.handle || !request.handle)
    throw std::runtime_error("Cannot create HTTPS request");
  DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
  WinHttpSetOption(request.handle, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
  std::wstring headerText = L"Content-Type: application/json\r\n";
  for (auto &entry : headers) {
    if (entry.first.find_first_of("\r\n:") != std::string::npos ||
        entry.second.find_first_of("\r\n") != std::string::npos)
      throw std::runtime_error("Invalid HTTP header");
    headerText += wide(entry.first + ": " + entry.second + "\r\n");
  }
  if (!WinHttpSendRequest(request.handle, headerText.c_str(), (DWORD)-1,
                          body.empty() ? WINHTTP_NO_REQUEST_DATA : (void *)body.data(),
                          (DWORD)body.size(), (DWORD)body.size(), 0) ||
      !WinHttpReceiveResponse(request.handle, nullptr))
    throw std::runtime_error("Atlas download failed; check network/proxy");
  DWORD status = 0, size = sizeof(status);
  WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);

  std::string result;
  char bytes[65536];
  DWORD got = 0;
  for (;;) {
    if (!WinHttpReadData(request.handle, bytes, sizeof(bytes), &got))
      throw std::runtime_error("Incomplete atlas download");
    if (!got)
      break;
    if (result.size() + got > 128 * 1024 * 1024)
      throw std::runtime_error("Atlas download exceeds 128 MiB");
    result.append(bytes, got);
  }
  DWORD headerBytes = 0;
  WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_CUSTOM, L"Mcp-Session-Id", nullptr,
                      &headerBytes, WINHTTP_NO_HEADER_INDEX);
  std::string mcpSession;
  if (headerBytes && headerBytes <= 4096) {
    std::wstring header(headerBytes / sizeof(wchar_t), 0);
    if (WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_CUSTOM, L"Mcp-Session-Id", header.data(),
                            &headerBytes, WINHTTP_NO_HEADER_INDEX)) {
      header.resize(wcslen(header.c_str()));
      mcpSession = utf8(header);
    }
  }
  std::string location;
  headerBytes = 0;
  WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, nullptr,
                      &headerBytes, WINHTTP_NO_HEADER_INDEX);
  if (headerBytes && headerBytes <= 16384) {
    std::wstring header(headerBytes / sizeof(wchar_t), 0);
    if (WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                            header.data(), &headerBytes, WINHTTP_NO_HEADER_INDEX)) {
      header.resize(wcslen(header.c_str()));
      location = utf8(header);
    }
  }
  return {status, result, mcpSession, location};
}
std::string fetchHttps(const std::string &url) {
  if (url.rfind("https://", 0) != 0)
    throw std::runtime_error("Atlas URL must be HTTPS");
  auto response = requestHttp(url, "GET");
  if (response.status != 200)
    throw std::runtime_error("Atlas HTTP " + std::to_string(response.status));
  return response.body;
}
namespace {
struct StreamContext {
  HANDLE ready = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  HANDLE closed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  std::mutex mutex;
  DWORD error = 0, bytes = 0;
  std::array<char, 65536> buffer{};
  std::wstring headers;
  ~StreamContext() {
    if (ready)
      CloseHandle(ready);
    if (closed)
      CloseHandle(closed);
  }
  void reset() {
    std::lock_guard<std::mutex> lock(mutex);
    error = bytes = 0;
    ResetEvent(ready);
  }
  void wait(std::atomic<bool> &stop, DWORD timeout = INFINITE) {
    auto start = GetTickCount64();
    for (;;) {
      if (stop)
        throw std::runtime_error("Live stream cancelled");
      auto result = WaitForSingleObject(ready, 50);
      if (result == WAIT_OBJECT_0)
        break;
      if (result == WAIT_FAILED || (timeout != INFINITE && GetTickCount64() - start >= timeout))
        throw std::runtime_error("Live stream operation timed out");
    }
    std::lock_guard<std::mutex> lock(mutex);
    if (error)
      throw std::runtime_error("Live stream transport error " + std::to_string(error));
  }
};
void CALLBACK streamStatus(HINTERNET, DWORD_PTR context, DWORD status, void *info, DWORD length) {
  auto *state = reinterpret_cast<StreamContext *>(context);
  if (!state)
    return;
  if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) {
    SetEvent(state->closed);
    return;
  }
  if (status != WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE &&
      status != WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE &&
      status != WINHTTP_CALLBACK_STATUS_READ_COMPLETE &&
      status != WINHTTP_CALLBACK_STATUS_REQUEST_ERROR)
    return;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR && info &&
        length >= sizeof(WINHTTP_ASYNC_RESULT))
      state->error = reinterpret_cast<WINHTTP_ASYNC_RESULT *>(info)->dwError;
    if (status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE)
      state->bytes = length;
  }
  SetEvent(state->ready);
}
struct StreamRequest {
  HINTERNET handle;
  StreamContext &context;
  ~StreamRequest() {
    // Only asynchronous handles are cancelled this way. Keep callback buffers alive
    // through HANDLE_CLOSING, the final notification, before releasing the context.
    WinHttpCloseHandle(handle);
    WaitForSingleObject(context.closed, INFINITE);
  }
};
} // namespace
unsigned streamHttp(const std::string &url, const std::map<std::string, std::string> &headers,
                    std::atomic<bool> &stop,
                    const std::function<void(const std::string &)> &chunk) {
  if (stop)
    return 0;
  auto address = wide(url);
  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof(parts);
  parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength =
      parts.dwUserNameLength = parts.dwPasswordLength = DWORD(-1);
  if (!WinHttpCrackUrl(address.c_str(), 0, 0, &parts) || parts.dwUserNameLength ||
      parts.dwPasswordLength ||
      (parts.nScheme != INTERNET_SCHEME_HTTPS &&
       !(parts.nScheme == INTERNET_SCHEME_HTTP &&
         std::wstring(parts.lpszHostName, parts.dwHostNameLength) == L"127.0.0.1")))
    throw std::runtime_error(
        "Live stream requires HTTPS without URL credentials (loopback HTTP is allowed)");
  Internet session{WinHttpOpen(L"TangOSLite", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC)};
  if (!session.handle)
    throw std::runtime_error("Cannot initialize live stream");
  WinHttpSetTimeouts(session.handle, 5000, 5000, 10000, 300000);
  auto host = std::wstring(parts.lpszHostName, parts.dwHostNameLength);
  auto path = std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) +
              (parts.dwExtraInfoLength ? std::wstring(parts.lpszExtraInfo, parts.dwExtraInfoLength)
                                       : std::wstring());
  Internet connect{WinHttpConnect(session.handle, host.c_str(), parts.nPort, 0)};
  if (!connect.handle)
    throw std::runtime_error("Cannot connect live stream");
  StreamContext context;
  if (!context.ready || !context.closed)
    throw std::runtime_error("Cannot create live stream completion events");
  Internet temporary{
      WinHttpOpenRequest(connect.handle, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                         WINHTTP_DEFAULT_ACCEPT_TYPES,
                         parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)};
  if (!temporary.handle)
    throw std::runtime_error("Cannot create live stream request");
  DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
  WinHttpSetOption(temporary.handle, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
  DWORD_PTR pointer = reinterpret_cast<DWORD_PTR>(&context);
  if (!WinHttpSetOption(temporary.handle, WINHTTP_OPTION_CONTEXT_VALUE, &pointer,
                        sizeof(pointer)) ||
      WinHttpSetStatusCallback(temporary.handle, streamStatus,
                               WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS |
                                   WINHTTP_CALLBACK_FLAG_HANDLES,
                               0) == WINHTTP_INVALID_STATUS_CALLBACK)
    throw std::runtime_error("Cannot configure live stream callbacks");
  StreamRequest request{temporary.handle, context};
  temporary.handle = nullptr;
  context.headers = L"Accept: text/event-stream\r\n";
  for (auto &entry : headers) {
    if (entry.first.empty() || entry.first.find_first_of("\r\n:") != std::string::npos ||
        entry.second.find_first_of("\r\n") != std::string::npos)
      throw std::runtime_error("Invalid live stream header");
    context.headers += wide(entry.first + ": " + entry.second + "\r\n");
  }
  context.reset();
  if (!WinHttpSendRequest(request.handle, context.headers.c_str(), DWORD(-1),
                          WINHTTP_NO_REQUEST_DATA, 0, 0, pointer))
    throw std::runtime_error("Cannot send live stream request");
  context.wait(stop, 15000);
  context.reset();
  if (!WinHttpReceiveResponse(request.handle, nullptr))
    throw std::runtime_error("Cannot receive live stream");
  context.wait(stop, 20000);
  DWORD status = 0, size = sizeof(status);
  if (!WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX))
    throw std::runtime_error("Cannot read live stream status");
  if (status != 200)
    return status; // Never follow redirects or forward credentials.
  wchar_t contentType[256]{};
  DWORD typeBytes = sizeof(contentType);
  if (!WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX,
                           contentType, &typeBytes, WINHTTP_NO_HEADER_INDEX) ||
      std::wstring(contentType).rfind(L"text/event-stream", 0) != 0)
    throw std::runtime_error("Live endpoint must return Content-Type: text/event-stream");
  for (;;) {
    context.reset();
    if (!WinHttpReadData(request.handle, context.buffer.data(), DWORD(context.buffer.size()),
                         nullptr))
      throw std::runtime_error("Cannot read live stream");
    context.wait(stop);
    DWORD bytes;
    {
      std::lock_guard<std::mutex> lock(context.mutex);
      bytes = context.bytes;
    }
    if (!bytes)
      break;
    chunk(std::string(context.buffer.data(), bytes));
    if (stop)
      break;
  }
  return status;
}
} // namespace lite
