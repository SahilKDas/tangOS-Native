#pragma once
#include <string>
#include <map>
namespace lite {
std::string fetchHttps(const std::string &url);
struct HttpResponse {
  unsigned status;
  std::string body;
  std::string session;
};
HttpResponse requestHttp(const std::string &url, const std::string &method,
                         const std::string &body = {},
                         const std::map<std::string, std::string> &headers = {});
} // namespace lite
