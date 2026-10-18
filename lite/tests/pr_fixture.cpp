#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
int main(int argc, char **argv) {
  auto mode = std::getenv("TANGOS_PR_FIXTURE");
  auto trace = std::getenv("TANGOS_PR_TRACE");
  if (!mode || !trace || argc < 3 ||
      (std::string(argv[1]) != "pr" && std::string(argv[1]) != "api"))
    return 90;
  std::string operation = std::string(argv[1]) == "api" ? "list" : argv[2];
  std::ofstream(trace, std::ios::app | std::ios::binary) << operation << "\n";
  if (std::string(argv[1]) == "api") {
    if (argc != 11 || std::string(argv[2]) != "--method" || std::string(argv[3]) != "GET" ||
        std::string(argv[4]) != "repos/fixture/project/pulls" ||
        std::string(argv[6]) != "state=open" || std::string(argv[8]) != "head=fixture:main" ||
        std::string(argv[10]) != ".[0].html_url // empty")
      return 92;
  }
  if (operation == "list") {
    if (std::string(mode) == "failure") {
      std::cerr << "Fixture authentication failed\n";
      return 4;
    }
    if (std::string(mode) == "invalid") {
      std::cout << "https://github.com/other/project/pull/8\n";
      return 0;
    }
    if (std::string(mode) == "existing")
      std::cout << "https://github.com/fixture/project/pull/7\n";
    return 0;
  }
  if (operation == "create") {
    std::cout << "https://github.com/fixture/project/pull/8\n";
    return 0;
  }
  return 91;
}