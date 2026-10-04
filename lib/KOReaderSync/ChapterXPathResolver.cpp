#include "ChapterXPathResolver.h"

#include <Epub/VisibleTextUtils.h>
#include <Logging.h>
#include <Print.h>
#include <Utf8.h>
#include <XmlParserUtils.h>
#include <expat.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr size_t MAX_HIGHLIGHT_QUOTE_BYTES = 2048;

bool isAsciiWhitespace(uint32_t cp) { return cp == ' ' || (cp >= '\t' && cp <= '\r'); }

bool normalizeQuote(const std::string& source, std::string& normalized) {
  if (source.size() > MAX_HIGHLIGHT_QUOTE_BYTES) return false;
  // Only the bounded expected quote is allocated; source chapter text is compared as it streams.
  normalized.reserve(source.size());
  bool pendingSpace = false;
  for (const unsigned char byte : source) {
    if (isAsciiWhitespace(byte)) {
      pendingSpace = !normalized.empty();
    } else {
      if (pendingSpace) normalized += ' ';
      normalized += static_cast<char>(byte);
      pendingSpace = false;
    }
  }
  return !normalized.empty();
}

std::string stripPrefix(const XML_Char* name) {
  if (!name) {
    return "";
  }

  const char* local = std::strrchr(name, ':');
  return local ? std::string(local + 1) : std::string(name);
}

struct NameCounter {
  std::string name;
  int count;
};

struct ParentState {
  std::vector<NameCounter> children;

  int nextIndex(const std::string& name) {
    for (auto& child : children) {
      if (child.name == name) {
        child.count++;
        return child.count;
      }
    }

    children.push_back({name, 1});
    return 1;
  }
};

struct PathSegment {
  std::string name;
  int index;
};

std::string buildParagraphXPath(const int spineIndex, const std::vector<PathSegment>& path, const int textNodeIndex,
                                const size_t charOffset) {
  std::string xpath = "/body/DocFragment[" + std::to_string(spineIndex + 1) + "]/body";
  for (const auto& segment : path) {
    xpath += "/" + segment.name + "[" + std::to_string(segment.index) + "]";
  }
  if (textNodeIndex > 0) {
    xpath += "/text()[" + std::to_string(textNodeIndex) + "]." + std::to_string(charOffset);
  }
  return xpath;
}

size_t countUtf8Codepoints(const XML_Char* data, const int len) {
  if (!data || len <= 0) {
    return 0;
  }

  size_t count = 0;
  const unsigned char* ptr = reinterpret_cast<const unsigned char*>(data);
  const unsigned char* end = ptr + len;
  while (ptr < end) {
    utf8NextCodepoint(&ptr);
    count++;
  }

  return count;
}

class ParagraphTextCounter final : public Print {
 public:
  ParagraphTextCounter() {
    parser = XML_ParserCreate(nullptr);
    if (!parser) {
      LOG_ERR("KOX", "Failed to create XML parser");
      return;
    }

    XML_SetUserData(parser, this);
    XML_SetElementHandler(parser, &ParagraphTextCounter::startElement, &ParagraphTextCounter::endElement);
    XML_SetCharacterDataHandler(parser, &ParagraphTextCounter::characterData);
  }

  ~ParagraphTextCounter() override { destroyXmlParser(parser); }

  bool ok() const { return parser != nullptr && parseOk; }

  bool finish() {
    if (!parser || !parseOk || stopped) {
      return parseOk;
    }

    if (XML_Parse(parser, "", 0, XML_TRUE) == XML_STATUS_ERROR) {
      LOG_ERR("KOX", "Final XML parse error: %s", XML_ErrorString(XML_GetErrorCode(parser)));
      parseOk = false;
    }
    return parseOk;
  }

  size_t write(uint8_t c) override { return write(&c, 1); }

  size_t write(const uint8_t* buffer, size_t size) override {
    if (!parser || !parseOk || stopped) {
      return size;
    }

    if (XML_Parse(parser, reinterpret_cast<const char*>(buffer), static_cast<int>(size), XML_FALSE) != XML_STATUS_OK) {
      const enum XML_Error error = XML_GetErrorCode(parser);
      if (error != XML_ERROR_ABORTED) {
        LOG_ERR("KOX", "XML parse error: %s", XML_ErrorString(error));
        parseOk = false;
      }
    }

    return size;
  }

  size_t totalVisibleChars() const { return visibleChars; }

 private:
  static void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char**) {
    auto* self = static_cast<ParagraphTextCounter*>(userData);
    self->onStartElement(name);
  }

  static void XMLCALL endElement(void* userData, const XML_Char* name) {
    auto* self = static_cast<ParagraphTextCounter*>(userData);
    self->onEndElement(name);
  }

  static void XMLCALL characterData(void* userData, const XML_Char* data, const int len) {
    auto* self = static_cast<ParagraphTextCounter*>(userData);
    self->onCharacterData(data, len);
  }

  void onStartElement(const XML_Char* rawName) {
    const std::string name = stripPrefix(rawName);

    if (!insideBody) {
      if (name == "body") {
        insideBody = true;
        bodyDepth = depth;
      }
      depth++;
      return;
    }

    if (nonVisibleDepth > 0 || VisibleTextUtils::isNonVisibleElement(name)) {
      nonVisibleDepth++;
    }
    if (name == "p") {
      paragraphDepth++;
    }
    depth++;
  }

  void onEndElement(const XML_Char* rawName) {
    const std::string name = stripPrefix(rawName);

    depth--;
    if (!insideBody) {
      return;
    }

    if (depth == bodyDepth && name == "body") {
      insideBody = false;
      nonVisibleDepth = 0;
      return;
    }

    if (nonVisibleDepth > 0) {
      nonVisibleDepth--;
    }
    if (name == "p" && paragraphDepth > 0) {
      paragraphDepth--;
    }
  }

  void onCharacterData(const XML_Char* data, const int len) {
    if (!insideBody || nonVisibleDepth > 0 || paragraphDepth <= 0 || len <= 0) {
      return;
    }

    visibleChars += countUtf8Codepoints(data, len);
  }

 private:
  XML_Parser parser = nullptr;
  bool parseOk = true;
  bool insideBody = false;
  bool stopped = false;
  int depth = 0;
  int bodyDepth = -1;
  int paragraphDepth = 0;
  uint16_t nonVisibleDepth = 0;
  size_t visibleChars = 0;
};

class XPathParagraphResolver final : public Print {
 public:
  explicit XPathParagraphResolver(const int targetParagraph) : targetParagraph(targetParagraph) {
    parser = XML_ParserCreate(nullptr);
    if (!parser) {
      LOG_ERR("KOX", "Failed to create XML parser");
      return;
    }

    XML_SetUserData(parser, this);
    XML_SetElementHandler(parser, &XPathParagraphResolver::startElement, &XPathParagraphResolver::endElement);
  }

  ~XPathParagraphResolver() override { destroyXmlParser(parser); }

  bool ok() const { return parser != nullptr && parseOk; }

  bool finish() {
    if (!parser || !parseOk || stopped) {
      return parseOk;
    }

    if (XML_Parse(parser, "", 0, XML_TRUE) == XML_STATUS_ERROR) {
      LOG_ERR("KOX", "Final XML parse error: %s", XML_ErrorString(XML_GetErrorCode(parser)));
      parseOk = false;
    }
    return parseOk;
  }

  bool hasMatch() const { return !xpath.empty(); }
  const std::string& getXPath() const { return xpath; }

  size_t write(uint8_t c) override { return write(&c, 1); }

  size_t write(const uint8_t* buffer, size_t size) override {
    if (!parser || !parseOk || stopped) {
      return size;
    }

    if (XML_Parse(parser, reinterpret_cast<const char*>(buffer), static_cast<int>(size), XML_FALSE) != XML_STATUS_OK) {
      const enum XML_Error error = XML_GetErrorCode(parser);
      if (error != XML_ERROR_ABORTED) {
        LOG_ERR("KOX", "XML parse error: %s", XML_ErrorString(error));
        parseOk = false;
      }
    }

    return size;
  }

  int spineIndex = 0;

 private:
  static void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char**) {
    auto* self = static_cast<XPathParagraphResolver*>(userData);
    self->onStartElement(name);
  }

  static void XMLCALL endElement(void* userData, const XML_Char* name) {
    auto* self = static_cast<XPathParagraphResolver*>(userData);
    self->onEndElement(name);
  }

  void onStartElement(const XML_Char* rawName) {
    const std::string name = stripPrefix(rawName);

    if (!insideBody) {
      if (name == "body") {
        insideBody = true;
        bodyDepth = depth;
        parentStates.emplace_back();
      }
      depth++;
      return;
    }

    const int siblingIndex = parentStates.back().nextIndex(name);
    path.push_back({name, siblingIndex});
    parentStates.emplace_back();

    // Count both <p> and <li> as paragraph-like positions, matching how the section
    // layout tracks them (xpathParagraphIndex and xpathListItemIndex). This ensures
    // KOReader progress in list items maps to the correct XPath.
    if (name == "p") {
      paragraphCount++;
    } else if (name == "li") {
      paragraphCount++;
    }
    if (paragraphCount == targetParagraph) {
      xpath = buildParagraphXPath(spineIndex, path, 0, 0);
      stopped = true;
      XML_StopParser(parser, XML_FALSE);
    }

    depth++;
  }

  void onEndElement(const XML_Char* rawName) {
    const std::string name = stripPrefix(rawName);

    depth--;
    if (!insideBody) {
      return;
    }

    if (depth == bodyDepth && name == "body") {
      insideBody = false;
      parentStates.clear();
      path.clear();
      return;
    }

    if (!path.empty()) {
      path.pop_back();
    }
    if (!parentStates.empty()) {
      parentStates.pop_back();
    }
  }

  XML_Parser parser = nullptr;
  const int targetParagraph;
  bool parseOk = true;
  bool insideBody = false;
  bool stopped = false;
  int depth = 0;
  int bodyDepth = -1;
  int paragraphCount = 0;
  std::vector<ParentState> parentStates;
  std::vector<PathSegment> path;
  std::string xpath;
};

class XPathProgressResolver final : public Print {
 public:
  enum class BoundaryMode { Exclusive, Inclusive };

  explicit XPathProgressResolver(const size_t targetVisibleChar,
                                 const BoundaryMode boundaryMode = BoundaryMode::Exclusive,
                                 const bool strictRange = false)
      : targetVisibleChar(targetVisibleChar), boundaryMode(boundaryMode), strictRange(strictRange) {
    parser = XML_ParserCreate(nullptr);
    if (!parser) {
      LOG_ERR("KOX", "Failed to create XML parser");
      return;
    }

    XML_SetUserData(parser, this);
    XML_SetElementHandler(parser, &XPathProgressResolver::startElement, &XPathProgressResolver::endElement);
    XML_SetCharacterDataHandler(parser, &XPathProgressResolver::characterData);
    XML_SetCommentHandler(parser, &XPathProgressResolver::comment);
    XML_SetProcessingInstructionHandler(parser, &XPathProgressResolver::processingInstruction);
    XML_SetCdataSectionHandler(parser, &XPathProgressResolver::startCdataSection,
                               &XPathProgressResolver::endCdataSection);
    if (strictRange) {
      // Do not silently drop entities that the renderer's HTML entity handler counts.
      XML_SetDefaultHandler(parser, &XPathProgressResolver::unhandledData);
      XML_SetSkippedEntityHandler(parser, &XPathProgressResolver::skippedEntity);
    }
  }

  ~XPathProgressResolver() override { destroyXmlParser(parser); }

  bool ok() const { return parser != nullptr && parseOk; }

  void verifyQuote(uint32_t start, uint32_t end, const std::string& expected) {
    quoteStart = start;
    quoteEnd = end;
    expectedQuote = &expected;
  }

  bool quoteMatches() const { return !expectedQuote || (quoteOk && quotePosition == expectedQuote->size()); }

  bool finish() {
    if (!parser || !parseOk || stopped) {
      return parseOk;
    }

    if (XML_Parse(parser, "", 0, XML_TRUE) == XML_STATUS_ERROR) {
      LOG_ERR("KOX", "Final XML parse error: %s", XML_ErrorString(XML_GetErrorCode(parser)));
      parseOk = false;
    }
    return parseOk;
  }

  bool hasMatch() const { return !xpath.empty(); }
  const std::string& getXPath() const { return xpath; }

  size_t write(uint8_t c) override { return write(&c, 1); }

  size_t write(const uint8_t* buffer, size_t size) override {
    if (!parser || !parseOk || stopped) {
      return size;
    }

    if (XML_Parse(parser, reinterpret_cast<const char*>(buffer), static_cast<int>(size), XML_FALSE) != XML_STATUS_OK) {
      const enum XML_Error error = XML_GetErrorCode(parser);
      if (error != XML_ERROR_ABORTED) {
        LOG_ERR("KOX", "XML parse error: %s", XML_ErrorString(error));
        parseOk = false;
      }
    }

    return size;
  }

  int spineIndex = 0;

 private:
  static void XMLCALL unhandledData(void* userData, const XML_Char* data, int len) {
    if (len > 0 && data[0] == '&') static_cast<XPathProgressResolver*>(userData)->parseOk = false;
  }

  static void XMLCALL skippedEntity(void* userData, const XML_Char*, int) {
    static_cast<XPathProgressResolver*>(userData)->parseOk = false;
  }

  static void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char**) {
    auto* self = static_cast<XPathProgressResolver*>(userData);
    self->onStartElement(name);
  }

  static void XMLCALL endElement(void* userData, const XML_Char* name) {
    auto* self = static_cast<XPathProgressResolver*>(userData);
    self->onEndElement(name);
  }

  static void XMLCALL characterData(void* userData, const XML_Char* data, const int len) {
    auto* self = static_cast<XPathProgressResolver*>(userData);
    self->onCharacterData(data, len);
  }

  static void XMLCALL comment(void* userData, const XML_Char*) {
    auto* self = static_cast<XPathProgressResolver*>(userData);
    self->onMarkupBoundary();
  }

  static void XMLCALL processingInstruction(void* userData, const XML_Char*, const XML_Char*) {
    auto* self = static_cast<XPathProgressResolver*>(userData);
    self->onMarkupBoundary();
  }

  static void XMLCALL startCdataSection(void* userData) {
    auto* self = static_cast<XPathProgressResolver*>(userData);
    self->onMarkupBoundary();
  }

  static void XMLCALL endCdataSection(void* userData) {
    auto* self = static_cast<XPathProgressResolver*>(userData);
    self->onMarkupBoundary();
  }

  void onStartElement(const XML_Char* rawName) {
    const std::string name = stripPrefix(rawName);

    if (!insideBody) {
      if (name == "body" || (strictRange && VisibleTextUtils::equalsTag(name, "body"))) {
        insideBody = true;
        bodyDepth = depth;
        parentStates.emplace_back();
        if (strictRange) textNodeIndexStack.push_back({});
      }
      depth++;
      return;
    }

    finishStrictTextNode();
    const int siblingIndex = parentStates.back().nextIndex(name);
    path.push_back({name, siblingIndex});
    parentStates.emplace_back();
    textNodeIndexStack.push_back({});
    pendingTextNode = true;

    if (nonVisibleDepth > 0 || VisibleTextUtils::isNonVisibleElement(name)) {
      nonVisibleDepth++;
    }

    if (name == "p") {
      paragraphDepth++;
    }
    if (name == "li") {
      liDepth++;
    }

    depth++;
  }

  void onEndElement(const XML_Char* rawName) {
    const std::string name = stripPrefix(rawName);

    finishStrictTextNode();
    depth--;
    if (!insideBody) {
      return;
    }

    if (depth == bodyDepth && (name == "body" || (strictRange && VisibleTextUtils::equalsTag(name, "body")))) {
      insideBody = false;
      parentStates.clear();
      path.clear();
      textNodeIndexStack.clear();
      nonVisibleDepth = 0;
      return;
    }

    if (nonVisibleDepth > 0) {
      nonVisibleDepth--;
    }
    if (name == "p" && paragraphDepth > 0) {
      paragraphDepth--;
    }
    if (name == "li" && liDepth > 0) {
      liDepth--;
    }

    if (!textNodeIndexStack.empty()) {
      textNodeIndexStack.pop_back();
    }
    if (strictRange || paragraphDepth > 0 || liDepth > 0) {
      pendingTextNode = true;
    }
    if (!path.empty()) {
      path.pop_back();
    }
    if (!parentStates.empty()) {
      parentStates.pop_back();
    }
  }

  void onCharacterData(const XML_Char* data, const int len) {
    if (!insideBody || nonVisibleDepth > 0 || (!strictRange && paragraphDepth <= 0 && liDepth <= 0) || len <= 0 ||
        stopped) {
      return;
    }

    const size_t codepointCount = countUtf8Codepoints(data, len);
    if (codepointCount == 0) {
      return;
    }

    // Start a new text node on first non-empty content after any structural boundary.
    // Only counting non-empty nodes matches KOReader's text()[N] indexing behavior,
    // which skips empty text nodes created by bare <a id="anchor"/> anchors.
    if (pendingTextNode) {
      if (!textNodeIndexStack.empty()) {
        textNodeIndexStack.back().index++;
      }
      textNodeStartChars = visibleChars;
      textNodeCodepoints = 0;
      nodeHasNonSpace = false;
      nodeLastSpace = false;
      nodeAmbiguousWhitespace = false;
      nodeHasEndpoint = false;
      pendingTextNode = false;
    }

    if (strictRange) {
      const auto* ptr = reinterpret_cast<const unsigned char*>(data);
      const auto* end = ptr + len;
      while (ptr < end) {
        // Start affinity selects the next character's node; end affinity keeps
        // the preceding character's node, including a final text-node boundary.
        if (xpath.empty() && boundaryMode == BoundaryMode::Exclusive && targetVisibleChar == visibleChars) {
          xpath = buildParagraphXPath(spineIndex, path, textNodeIndexStack.back().index, textNodeCodepoints);
          nodeHasEndpoint = true;
        }
        const auto* cpStart = ptr;
        const uint32_t cp = utf8NextCodepoint(&ptr);
        if (expectedQuote && visibleChars >= quoteStart && visibleChars < quoteEnd) {
          compareQuoteCodepoint(cp, cpStart, static_cast<size_t>(ptr - cpStart));
        }
        const bool space = cp == ' ' || cp == '\n' || cp == '\r' || cp == '\t';
        if (space && (textNodeCodepoints == 0 || nodeLastSpace || cp != ' ')) nodeAmbiguousWhitespace = true;
        nodeHasNonSpace |= !space;
        nodeLastSpace = space;
        // CRengine's ldomXPointer indexes lString32 text, including non-BMP characters.
        ++textNodeCodepoints;
        ++visibleChars;
        if (xpath.empty() && boundaryMode == BoundaryMode::Inclusive && targetVisibleChar == visibleChars) {
          xpath = buildParagraphXPath(spineIndex, path, textNodeIndexStack.back().index, textNodeCodepoints);
          nodeHasEndpoint = true;
        }
      }
      // Validate the complete XML stream even after finding the endpoint.
      return;
    }

    const size_t nextVisibleChars = visibleChars + codepointCount;
    const bool targetInCurrentChunk = boundaryMode == BoundaryMode::Inclusive ? targetVisibleChar <= nextVisibleChars
                                                                              : targetVisibleChar < nextVisibleChars;
    if (targetInCurrentChunk) {
      const size_t delta = targetVisibleChar - visibleChars;
      const int texNode = textNodeIndexStack.empty() ? 0 : textNodeIndexStack.back().index;
      const size_t charOff = visibleChars - textNodeStartChars + delta;
      xpath = buildParagraphXPath(spineIndex, path, texNode, charOff);
      stopped = true;
      XML_StopParser(parser, XML_FALSE);
      return;
    }

    visibleChars = nextVisibleChars;
  }

  void onMarkupBoundary() {
    finishStrictTextNode();
    if (!insideBody || nonVisibleDepth > 0 || (!strictRange && paragraphDepth <= 0 && liDepth <= 0) || stopped ||
        pendingTextNode) {
      return;
    }

    pendingTextNode = true;
  }

  void finishStrictTextNode() {
    if (!strictRange || pendingTextNode || !insideBody || nonVisibleDepth > 0 || textNodeIndexStack.empty()) return;
    auto& node = textNodeIndexStack.back();
    // CRengine may collapse/trim whitespace or omit whitespace-only nodes depending
    // on CSS. Without that DOM, reject ambiguous endpoints rather than export drift.
    if (nodeHasEndpoint && (node.uncertainIndex || nodeAmbiguousWhitespace || nodeLastSpace || !nodeHasNonSpace)) {
      parseOk = false;
    }
    if (!nodeHasNonSpace) node.uncertainIndex = true;
    pendingTextNode = true;
  }

  void compareQuoteCodepoint(uint32_t cp, const unsigned char* bytes, size_t byteCount) {
    if (!quoteOk) return;
    quoteBytes += byteCount;
    if (quoteBytes > MAX_HIGHLIGHT_QUOTE_BYTES) {
      quoteOk = false;
      return;
    }
    if (isAsciiWhitespace(cp)) {
      quotePendingSpace = quotePosition > 0;
      return;
    }
    if (quotePendingSpace) {
      if (quotePosition >= expectedQuote->size() || (*expectedQuote)[quotePosition++] != ' ') {
        quoteOk = false;
        return;
      }
      quotePendingSpace = false;
    }
    if (byteCount > expectedQuote->size() - quotePosition ||
        std::memcmp(expectedQuote->data() + quotePosition, bytes, byteCount) != 0) {
      quoteOk = false;
      return;
    }
    quotePosition += byteCount;
  }

  XML_Parser parser = nullptr;
  const size_t targetVisibleChar;
  const BoundaryMode boundaryMode;
  const bool strictRange;
  bool parseOk = true;
  bool insideBody = false;
  bool stopped = false;
  bool pendingTextNode = true;
  int depth = 0;
  int bodyDepth = -1;
  int paragraphDepth = 0;
  int liDepth = 0;
  uint16_t nonVisibleDepth = 0;
  size_t visibleChars = 0;
  size_t textNodeStartChars = 0;
  size_t textNodeCodepoints = 0;
  struct TextNodeState {
    int index = 0;
    bool uncertainIndex = false;
  };
  bool nodeHasNonSpace = false;
  bool nodeLastSpace = false;
  bool nodeAmbiguousWhitespace = false;
  bool nodeHasEndpoint = false;
  const std::string* expectedQuote = nullptr;
  uint32_t quoteStart = 0;
  uint32_t quoteEnd = 0;
  size_t quotePosition = 0;
  size_t quoteBytes = 0;
  bool quotePendingSpace = false;
  bool quoteOk = true;
  std::vector<TextNodeState> textNodeIndexStack;
  std::vector<ParentState> parentStates;
  std::vector<PathSegment> path;
  std::string xpath;
};
}  // namespace

std::string ChapterXPathResolver::findXPathForParagraph(const std::shared_ptr<Epub>& epub, const int spineIndex,
                                                        const uint16_t paragraphIndex) {
  if (!epub || paragraphIndex == 0 || spineIndex < 0 || spineIndex >= epub->getSpineItemsCount()) {
    return "";
  }

  const auto href = epub->getSpineItem(spineIndex).href;
  if (href.empty()) {
    return "";
  }

  XPathParagraphResolver resolver(paragraphIndex);
  if (!resolver.ok()) {
    return "";
  }

  resolver.spineIndex = spineIndex;
  if (!epub->readItemContentsToStream(href, resolver, 1024) || !resolver.finish()) {
    return "";
  }

  if (resolver.hasMatch()) {
    LOG_DBG("KOX", "Resolved paragraph %u in spine %d -> %s", paragraphIndex, spineIndex, resolver.getXPath().c_str());
    return resolver.getXPath();
  }

  LOG_DBG("KOX", "Paragraph %u not found in spine %d", paragraphIndex, spineIndex);
  return "";
}

std::string ChapterXPathResolver::findXPathForVisibleTextOffset(const std::shared_ptr<Epub>& epub, const int spineIndex,
                                                                const uint32_t visibleTextOffset) {
  if (!epub || spineIndex < 0 || spineIndex >= epub->getSpineItemsCount()) {
    return "";
  }

  const auto href = epub->getSpineItem(spineIndex).href;
  if (href.empty()) {
    return "";
  }

  XPathProgressResolver resolver(visibleTextOffset);
  if (!resolver.ok()) {
    return "";
  }

  resolver.spineIndex = spineIndex;
  if (!epub->readItemContentsToStream(href, resolver, 1024) || !resolver.finish()) {
    return "";
  }

  if (resolver.hasMatch()) {
    LOG_DBG("KOX", "Resolved visible offset %u in spine %d -> %s", visibleTextOffset, spineIndex,
            resolver.getXPath().c_str());
    return resolver.getXPath();
  }

  LOG_DBG("KOX", "Visible offset %u not found in spine %d", visibleTextOffset, spineIndex);
  return "";
}

bool ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(const std::shared_ptr<Epub>& epub, const int spineIndex,
                                                               const uint32_t start, const uint32_t end,
                                                               std::string& pos0, std::string& pos1,
                                                               const std::string& expectedQuote) {
  pos0.clear();
  pos1.clear();
  if (!epub || start >= end || spineIndex < 0 || spineIndex >= epub->getSpineItemsCount()) return false;
  const auto href = epub->getSpineItem(spineIndex).href;
  if (href.empty()) return false;
  std::string normalizedQuote;
  if (!expectedQuote.empty() && !normalizeQuote(expectedQuote, normalizedQuote)) return false;

  // Sequential passes keep only one streaming parser (and no chapter-sized DOM) resident.
  const auto resolve = [&](uint32_t offset, XPathProgressResolver::BoundaryMode mode, std::string& result) {
    XPathProgressResolver resolver(offset, mode, true);
    resolver.spineIndex = spineIndex;
    if (mode == XPathProgressResolver::BoundaryMode::Exclusive && !expectedQuote.empty()) {
      resolver.verifyQuote(start, end, normalizedQuote);
    }
    if (!resolver.ok() || !epub->readItemContentsToStream(href, resolver, 1024) || !resolver.finish() ||
        !resolver.hasMatch() || !resolver.quoteMatches())
      return false;
    result = resolver.getXPath();
    return true;
  };
  std::string first;
  std::string last;
  if (!resolve(start, XPathProgressResolver::BoundaryMode::Exclusive, first) ||
      !resolve(end, XPathProgressResolver::BoundaryMode::Inclusive, last))
    return false;
  pos0 = std::move(first);
  pos1 = std::move(last);
  return true;
}

std::string ChapterXPathResolver::findXPathForProgress(const std::shared_ptr<Epub>& epub, const int spineIndex,
                                                       const float intraSpineProgress) {
  if (!epub || spineIndex < 0 || spineIndex >= epub->getSpineItemsCount()) {
    return "";
  }

  const auto href = epub->getSpineItem(spineIndex).href;
  if (href.empty()) {
    return "";
  }

  if (!(intraSpineProgress > 0.0f)) {
    return "/body/DocFragment[" + std::to_string(spineIndex + 1) + "]/body";
  }

  ParagraphTextCounter counter;
  if (!counter.ok() || !epub->readItemContentsToStream(href, counter, 1024) || !counter.finish()) {
    return "";
  }

  const size_t totalVisibleChars = counter.totalVisibleChars();
  if (totalVisibleChars == 0) {
    return "";
  }

  const float clamped = std::max(0.0f, std::min(1.0f, intraSpineProgress));
  const size_t targetVisibleChar =
      std::max<size_t>(1, std::min(totalVisibleChars, static_cast<size_t>(std::ceil(clamped * totalVisibleChars))));

  XPathProgressResolver resolver(targetVisibleChar, XPathProgressResolver::BoundaryMode::Inclusive);
  if (!resolver.ok()) {
    return "";
  }

  resolver.spineIndex = spineIndex;
  if (!epub->readItemContentsToStream(href, resolver, 1024) || !resolver.finish()) {
    return "";
  }

  if (resolver.hasMatch()) {
    LOG_DBG("KOX", "Resolved progress %.3f in spine %d -> %s", intraSpineProgress, spineIndex,
            resolver.getXPath().c_str());
    return resolver.getXPath();
  }

  LOG_DBG("KOX", "Could not resolve progress %.3f in spine %d", intraSpineProgress, spineIndex);
  return "";
}
