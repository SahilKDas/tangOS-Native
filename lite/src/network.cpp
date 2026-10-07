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
std::string fetchHttps(const std::string &url) {
  auto address = wide(url);
  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof(parts);
  parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength =
      parts.dwUserNameLength = parts.dwPasswordLength = (DWORD)-1;
  if (!WinHttpCrackUrl(address.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS ||
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
      request{WinHttpOpenRequest(connect.handle, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                 WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
  if (!connect.handle || !request.handle)
    throw std::runtime_error("Cannot create HTTPS request");
  DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
  WinHttpSetOption(request.handle, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
  if (!WinHttpSendRequest(request.handle, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA,
                          0, 0, 0) ||
      !WinHttpReceiveResponse(request.handle, nullptr))
    throw std::runtime_error("Atlas download failed; check network/proxy");
  DWORD status = 0, size = sizeof(status);
  WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
  if (status != 200)
    throw std::runtime_error("Atlas HTTP " + std::to_string(status));
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
  return result;
}
} // namespace lite
