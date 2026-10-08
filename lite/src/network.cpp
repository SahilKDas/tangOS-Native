#include "network.h"
#include "core.h"
#include <windows.h>
#include <winhttp.h>
#include <stdexcept>
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
  WinHttpSetTimeouts(session.handle, 5000, 5000, 10000, 10000);
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
} // namespace lite
