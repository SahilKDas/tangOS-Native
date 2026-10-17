#pragma once
#include <string>
#include <map>
#include <atomic>
#include <functional>
namespace lite {
std::string fetchHttps(const std::string &url);
struct HttpResponse {
  unsigned status;
  std::string body;
  std::string session;
  std::string location;
};
HttpResponse requestHttp(const std::string &url, const std::string &method,
                         const std::string &body = {},
                         const std::map<std::string, std::string> &headers = {});
HttpResponse requestHttpCancelable(const std::string &url, const std::string &method,
                                   const std::string &body,
                                   const std::map<std::string, std::string> &headers,
                                   std::atomic<bool> &stop);
unsigned streamHttp(const std::string &url, const std::map<std::string, std::string> &headers,
                    std::atomic<bool> &stop, const std::function<void(const std::string &)> &chunk);
} // namespace lite
