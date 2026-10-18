#include "verification.h"
#include "platform.h"
#include <iostream>
#include <stdexcept>
using namespace lite;
int main() {
  try {
    auto expect = [](bool value) {
      if (!value)
        throw std::runtime_error("Check verdict mismatch");
    };
    auto actual =
        checkOutcome("ROM data comparison", 0,
                     "  VERIFIED 914 (47,812 bytes)\n  PARTIAL 357\n  DIFFERS 3\n  UNNAMED 59\n");
    expect(actual["state"] == "completed-with-differences");
    expect(actual["dataCounts"] ==
           Json{{"verified", 914}, {"partial", 357}, {"differs", 3}, {"unnamed", 59}});
    expect(checkOutcome("ROM data comparison", 0,
                        "VERIFIED 4\nPARTIAL 1\nDIFFERS 0\nUNNAMED 0")["state"] ==
           "completed-with-partial-data");
    expect(checkOutcome("ROM data comparison", 0,
                        "VERIFIED 0\nPARTIAL 0\nDIFFERS 0\nUNNAMED 0")["state"] ==
           "skipped: no data verified");
    expect(checkOutcome("ROM data comparison", 0, "VERIFIED 914")["state"] ==
           "unverified: incomplete data report");
    expect(checkOutcome("ROM data comparison", 0,
                        "VERIFIED 1,234\nPARTIAL 0\nDIFFERS 0\nUNNAMED 0")["state"] == "passed");
    expect(checkOutcome("ROM data comparison", 1,
                        "VERIFIED 1\nPARTIAL 0\nDIFFERS 0\nUNNAMED 0")["state"] == "failed");
    expect(checkOutcome("Declaration agreement", 0, "12228 disagreement(s)")["state"] ==
           "passed-with-baseline");
    expect(checkOutcome("Link checks", 0, "nothing to verify")["state"] == "skipped");
    expect(checkOutcome("Port references", 0, "all references resolve")["state"] == "passed");
    expect(checkOutcome("ROM data comparison", 1223, "")["state"] == "cancelled");
    expect(checkOutcome("Declaration agreement", 0,
                        "99999999999999999999999999999 disagreement(s)")["state"] ==
           "unverified: invalid baseline count");
    auto fixture = fs::temp_directory_path() / fs::u8path("tangos-verdict-" + uniqueId() + ".log");
    auto compare = [&](const std::string &name, const std::string &text) {
      write(fixture, text);
      expect(checkOutcomeLog(name, 0, fixture) == checkOutcome(name, 0, text));
    };
    compare("ROM data comparison",
            std::string(8186, ' ') + "\nVERIFIED 914\nPARTIAL 357\nDIFFERS 3\nUNNAMED 59");
    compare("Declaration agreement", "before\n12228 disagreement(s)\nafter\n");
    compare("Link checks", "prefix\nnothing to verify\nsuffix\n");
    compare("ROM data comparison", "VERIFIED 12\nDIFFERS 0\n");
    compare("Port references", "all references resolve");
    write(fixture, std::string(65537, 'x'));
    expect(checkOutcomeLog("Link checks", 0, fixture)["state"] ==
           "unverified: oversized verification line");
    expect(checkOutcome("ROM data comparison", 0,
                        std::string(100000, ' ') +
                            "VERIFIED 1\nPARTIAL 0\nDIFFERS 0\nUNNAMED 0")["state"] == "passed");
    expect(checkOutcome("Declaration agreement", 0,
                        std::string(100000, '9') + " disagreement(s)")["state"] ==
           "unverified: invalid baseline count");
    fs::remove(fixture);
    expect(checkOutcomeLog("Link checks", 0, fixture)["state"] ==
           "unverified: verification log unavailable");
    expect(checkOutcomeLog("Link checks", 1, fixture)["state"] == "failed");
    expect(checkOutcomeLog("Link checks", 1223, fixture)["state"] == "cancelled");
    std::cout
        << "PASS native verification outcomes: real differences, partial data, baselines, skips\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
