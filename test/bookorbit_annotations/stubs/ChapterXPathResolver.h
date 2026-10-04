#pragma once
#include <Epub.h>

#include <cstdint>
#include <memory>
#include <string>
extern bool rangeResolves;
extern std::string lastExpectedQuote;
class ChapterXPathResolver {
 public:
  static bool findXPathRangeForVisibleTextOffsets(const std::shared_ptr<Epub>&, int spine, uint32_t start, uint32_t end,
                                                  std::string& pos0, std::string& pos1,
                                                  const std::string& expectedQuote) {
    lastExpectedQuote = expectedQuote;
    const std::string base = "/body/DocFragment[" + std::to_string(spine + 1) + "]/body/p/text().";
    pos0 = base + std::to_string(start);
    pos1 = base + std::to_string(end);
    return rangeResolves;
  }
};
