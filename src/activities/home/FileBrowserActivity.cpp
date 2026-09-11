#include "FileBrowserActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <Utf8.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/OptionPopup.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "util/BookCacheUtils.h"

namespace fui = freeink::ui;

namespace {
constexpr unsigned long GO_HOME_MS = 1000;
constexpr size_t NAME_BUFFER_SIZE = 500;
}  // namespace

std::string getFileName(std::string filename);
std::string getFileExtension(const std::string& filename);

FileBrowserActivity::FileBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                         std::string initialPath, const Mode mode)
    : UiListActivity("FileBrowser", renderer, mappedInput, /*wantsTouchLongPress=*/true),
      mode(mode),
      basepath(initialPath.empty() ? "/" : std::move(initialPath)) {}

FileBrowserActivity::~FileBrowserActivity() = default;

void FileBrowserActivity::loadFiles() {
  ++folderGeneration;
  coverTop = -1;
  files.clear();

  auto root = Storage.open(basepath.c_str());
  if (!root || !root.isDirectory()) {
    rebuildRowItems();  // files is empty; also drops any now-stale cached rows
    return;
  }

  root.rewindDirectory();

  if (!fileNameBuffer) {
    LOG_ERR("FileBrowser", "fileNameBuffer not allocated");
    root.close();
    rebuildRowItems();
    return;
  }

  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(fileNameBuffer.get(), NAME_BUFFER_SIZE);
    const bool isDirectory = file.isDirectory();
    if ((!SETTINGS.showHiddenFiles && fileNameBuffer[0] == '.') ||
        strcmp(fileNameBuffer.get(), "System Volume Information") == 0) {
      continue;
    }

    if (isDirectory) {
      files.emplace_back(std::string(fileNameBuffer.get()) + "/");
    } else {
      std::string_view filename{fileNameBuffer.get()};
      if (mode == Mode::PickFirmware) {
        // Firmware picker: only show .bin files.
        if (FsHelpers::checkFileExtension(filename, ".bin")) {
          files.emplace_back(filename);
        }
      } else if (FsHelpers::hasEpubExtension(filename) || FsHelpers::hasXtcExtension(filename) ||
                 FsHelpers::hasTxtExtension(filename) || FsHelpers::hasMarkdownExtension(filename) ||
                 FsHelpers::hasBmpExtension(filename) || FsHelpers::hasPngExtension(filename)) {
        files.emplace_back(filename);
      }
    }
  }
  root.close();
  FsHelpers::sortFileList(files);
  rebuildRowItems();
}

// Derives rowNames/rowExtensions/rowItems from `files`. Called whenever
// `files` changes (end of loadFiles()) so buildScreen() can reuse the cached
// rows on every repaint instead of re-deriving a name/extension string (and a
// ListItem) per file each time it's called.
void FileBrowserActivity::rebuildRowItems() {
  rowsUseFileIcons = UITheme::getInstance().getTheme().showsFileIcons();
  rowNames.resize(files.size());
  rowExtensions.resize(files.size());
  rowItems.clear();
  rowItems.reserve(files.size());
  for (size_t i = 0; i < files.size(); i++) {
    rowNames[i] = getFileName(files[i]);
    rowExtensions[i] = getFileExtension(files[i]);
    fui::ListItem item;
    item.label = rowNames[i].c_str();
    if (!rowExtensions[i].empty()) item.value = rowExtensions[i].c_str();
    item.icon = listIconFor(UITheme::getFileIcon(files[i]));
    item.actionValue = static_cast<int16_t>(i);
    rowItems.push_back(item);
  }

  // One SD pass for every CJK filename in the folder; repaints then hit the
  // resident tables instead of re-reading per-string. Getter form: no
  // concatenated copy (a bare-new string append aborts under heap pressure).
  // The last index covers the bottom path band: basepath (possibly a CJK
  // folder name) draws in the same small font, so it must live in the same
  // batch or it would evict the rows' glyphs when the heap gate disables
  // union merging. (prewarmFallbackText appends the truncation ellipsis.)
  struct PrewarmCtx {
    const std::vector<std::string>* names;
    const std::string* path;
  } prewarmCtx{&rowNames, &basepath};
  renderer.prewarmFallbackText(
      uiScaleSpec().smallFontId,
      [](const void* ctx, uint32_t i) -> const char* {
        const auto* c = static_cast<const PrewarmCtx*>(ctx);
        return i < c->names->size() ? (*c->names)[i].c_str() : c->path->c_str();
      },
      &prewarmCtx, static_cast<uint32_t>(rowNames.size()) + 1);
}

void FileBrowserActivity::onEnter() {
  UiListActivity::onEnter();

  fileNameBuffer = makeUniqueNoThrow<char[]>(NAME_BUFFER_SIZE);
  if (!fileNameBuffer) {
    LOG_ERR("FileBrowser", "malloc failed for name buffer");
    return;
  }

  auto root = Storage.open(basepath.c_str());
  if (!root) {
    basepath = "/";
    loadFiles();
  } else if (!root.isDirectory()) {
    const std::string oldPath = basepath;
    basepath = FsHelpers::extractFolderPath(basepath);
    loadFiles();

    const auto pos = oldPath.find_last_of('/');
    const std::string fileName = oldPath.substr(pos + 1);
    // The first screen build pulls the viewport to it (ListNav follow-on-build).
    nav.selected = static_cast<int>(findEntry(fileName));
  } else {
    loadFiles();
  }
}

void FileBrowserActivity::onExit() {
  stopCoverLoader();
  Activity::onExit();
  files.clear();
  rowNames.clear();
  rowExtensions.clear();
  rowItems.clear();
  fileNameBuffer.reset();
  coverRenderer.reset();
  optionsPopup.reset();
}

bool FileBrowserActivity::coverView() const {
  return mode == Mode::Books && SETTINGS.libraryView == CrossPointSettings::COVER_LIST && !coverAllocationFailed;
}

void FileBrowserActivity::loop() {
  leaving = false;
  UiListActivity::loop();
  if (!leaving) serviceCoverLoader();
}

void FileBrowserActivity::coverLoaderTask(void* context) {
  auto* self = static_cast<FileBrowserActivity*>(context);
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (self->stopRequested.load()) break;
    if (self->loadState.load() != LoadState::Working) continue;
    LOG_DBG("LIB", "Preview start: heap=%u", static_cast<unsigned>(ESP.getFreeHeap()));
    loadLibraryBookDetails(self->loadPath, BaseTheme::LIBRARY_COVER_HEIGHT, self->loadResult);
    LOG_DBG("LIB", "Preview complete: heap=%u, stack remaining=%u", static_cast<unsigned>(ESP.getFreeHeap()),
            static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    self->loadState.store(LoadState::Ready);
  }
  // All parser objects and SD handles have unwound before onExit deletes this task.
  self->loaderStopped.store(true);
  vTaskSuspend(nullptr);
}

void FileBrowserActivity::stopCoverLoader() {
  if (!loaderTask) return;
  stopRequested.store(true);
  xTaskNotifyGive(loaderTask);
  while (!loaderStopped.load()) vTaskDelay(pdMS_TO_TICKS(10));
  vTaskDelete(loaderTask);
  loaderTask = nullptr;
  loadState.store(LoadState::Idle);
}

void FileBrowserActivity::serviceCoverLoader() {
  bool changed = false;
  {
    RenderLock lock(RenderLock::Mode::Try);
    if (!lock) return;
    if (loadState.load() == LoadState::Ready) {
      const int slot = loadIndex - coverTop;
      if (loadGeneration == folderGeneration && slot >= 0 && slot < coverCount) {
        coverRows[slot].details = std::move(loadResult);
        coverRows[slot].loaded = true;
        detailsDirty = true;
      }
      loadState.store(LoadState::Idle);
    }
    if (coverView() && coverRenderer && coverTop >= 0 && !loaderFailed && loadState.load() == LoadState::Idle &&
        millis() - pageChangedAt >= 250 && !(optionsPopup && optionsPopup->isActive())) {
      for (int slot = 0; slot < coverCount; ++slot) {
        if (coverRows[slot].loaded) continue;
        const int index = coverTop + slot;
        if (!FsHelpers::hasEpubExtension(files[index])) {
          coverRows[slot].loaded = true;
          continue;
        }
        // One reusable worker stack keeps ZIP/image decoding off the input loop.
        if (!loaderTask) {
          stopRequested.store(false);
          loaderStopped.store(false);
          if (xTaskCreate(&FileBrowserActivity::coverLoaderTask, "LibraryCovers", 8192, this, 1, &loaderTask) !=
              pdPASS) {
            LOG_ERR("FileBrowser", "OOM: cover loader task");
            loaderTask = nullptr;
            loaderFailed = true;
            changed = true;
            break;
          }
        }
        loadPath = basepath;
        if (loadPath.back() != '/') loadPath += '/';
        loadPath += files[index];
        loadIndex = index;
        loadGeneration = folderGeneration;
        loadResult = {};
        loadState.store(LoadState::Working);
        xTaskNotifyGive(loaderTask);
        break;
      }
    }
    bool complete = true;
    for (int slot = 0; slot < coverCount; ++slot) complete &= coverRows[slot].loaded;
    if (detailsDirty && (complete || millis() - detailsRenderedAt >= 1500)) {
      detailsDirty = false;
      detailsRenderedAt = millis();
      changed = true;
    }
  }
  if (changed) requestUpdate();
}

void FileBrowserActivity::showOptions(bool viewOnly) {
  if (mode != Mode::Books) return;
  RenderLock lock(*this);
  app.clearTapFlash();
  if (!optionsPopup) {
    optionsPopup = makeUniqueNoThrow<OptionPopup>();
    if (!optionsPopup) {
      LOG_ERR("FileBrowser", "OOM: library options");
      return;
    }
  }
  choosingView = viewOnly || files.empty();
  static constexpr StrId views[] = {StrId::STR_COMPACT_LIST, StrId::STR_COVER_LIST};
  static constexpr StrId actions[] = {StrId::STR_OPEN, StrId::STR_DELETE, StrId::STR_LIBRARY_VIEW};
  optionsPopup->show(choosingView ? StrId::STR_LIBRARY_VIEW : StrId::STR_BROWSER_OPTIONS,
                     choosingView ? views : actions, choosingView ? 2 : 3, choosingView ? SETTINGS.libraryView : 0,
                     [this](int option) { pendingOption = option; });
  lock.unlock();
  requestUpdate();
}

// To avoid traversing directories twice (once for cache clearing, once for deletion),
// we do both in one pass here, instead of using Storage.removeDir
bool FileBrowserActivity::removeDirFile(const std::string& fullPath) {
  auto file = Storage.open(fullPath.c_str());
  if (!file) {
    LOG_ERR("FileBrowser", "Failed to open for metadata clearing: %s", fullPath.c_str());
    return false;
  }

  if (!file.isDirectory()) {
    file.close();
    clearBookCache(fullPath);
    return Storage.remove(fullPath.c_str());
  }
  file.close();

  if (!fileNameBuffer) {
    LOG_ERR("FileBrowser", "fileNameBuffer not allocated");
    return false;
  }

  // Stack of (dirPath, postOrder): postOrder=true means rmdir this path after children are processed.
  std::vector<std::pair<std::string, bool>> stack;
  stack.reserve(16);
  stack.push_back({fullPath, false});

  while (!stack.empty()) {
    auto [currentPath, postOrder] = std::move(stack.back());
    stack.pop_back();

    if (postOrder) {
      if (!Storage.rmdir(currentPath.c_str())) {
        LOG_ERR("FileBrowser", "Failed to rmdir: %s", currentPath.c_str());
        return false;
      }
      continue;
    }

    auto dir = Storage.open(currentPath.c_str());
    if (!dir) {
      LOG_ERR("FileBrowser", "Failed to open dir: %s", currentPath.c_str());
      return false;
    }
    if (!dir.isDirectory()) {
      LOG_ERR("FileBrowser", "Not a directory: %s", currentPath.c_str());
      return false;
    }

    // Push this dir for post-order rmdir (after all children are processed).
    stack.push_back({currentPath, true});

    dir.rewindDirectory();
    for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
      entry.getName(fileNameBuffer.get(), NAME_BUFFER_SIZE);
      if (strcmp(fileNameBuffer.get(), ".") == 0 || strcmp(fileNameBuffer.get(), "..") == 0) {
        continue;
      }
      std::string entryPath = currentPath;
      if (entryPath.back() != '/') {
        entryPath += "/";
      }
      entryPath += fileNameBuffer.get();

      const bool isDir = entry.isDirectory();
      entry.close();

      if (isDir) {
        stack.push_back({std::move(entryPath), false});
      } else {
        clearBookCache(entryPath);
        if (!Storage.remove(entryPath.c_str())) {
          LOG_ERR("FileBrowser", "Failed to remove file: %s", entryPath.c_str());
          return false;
        }
      }
    }
  }

  return true;
}

void FileBrowserActivity::activateIndex(const int index) {
  (void)index;  // base already synced nav.selected to the tapped row
  // Activation navigates or opens; a lingering flash would gray an unrelated
  // row on the next list.
  app.clearTapFlash();
  activateSelected();
}

void FileBrowserActivity::onRowLongPress(const int index) {
  (void)index;  // base already synced nav.selected to the pressed row
  // Activation navigates or opens; a lingering flash would gray an unrelated
  // row on the next list.
  app.clearTapFlash();
  showOptions();
}

void FileBrowserActivity::activateSelected(const bool forceDelete) {
  if (files.empty()) return;
  // A touch activation can carry a row index captured before a delete/reload
  // shrank the list; the next render re-registers the rows.
  if (nav.selected < 0 || nav.selected >= listCount()) return;

  const std::string& entry = files[nav.selected];
  bool isDirectory = (entry.back() == '/');

  // Firmware picker: select file -> return path; navigate into directories normally.
  if (mode == Mode::PickFirmware && !isDirectory) {
    std::string cleanBasePath = basepath;
    if (cleanBasePath.back() != '/') cleanBasePath += "/";
    ActivityResult res{FilePathResult{cleanBasePath + entry}};
    res.isCancelled = false;
    setResult(std::move(res));
    finish();
    return;
  }

  if (mode == Mode::Books && forceDelete) {
    stopCoverLoader();
    // --- LONG PRESS ACTION: DELETE FILE OR DIRECTORY ---
    std::string cleanBasePath = basepath;
    if (cleanBasePath.back() != '/') cleanBasePath += "/";
    const std::string fullPath = cleanBasePath + entry;

    auto handler = [this, fullPath](const ActivityResult& res) {
      leaving = false;
      if (!res.isCancelled) {
        LOG_DBG("FileBrowser", "Attempting to delete: %s", fullPath.c_str());
        if (removeDirFile(fullPath)) {
          LOG_DBG("FileBrowser", "Deleted successfully");
          {
            // buildScreen() reads the row caches on the render task; see loop().
            RenderLock lock(*this);
            loadFiles();
            if (files.empty()) {
              nav.selected = 0;
            } else if (nav.selected >= listCount()) {
              // Move selection to the new "last" item
              nav.selected = listCount() - 1;
            }
            nav.follow(listCount());
          }

          requestUpdate(true);
        } else {
          LOG_ERR("FileBrowser", "Failed to delete: %s", fullPath.c_str());
        }
      } else {
        LOG_DBG("FileBrowser", "Delete cancelled by user");
      }
    };

    std::string heading = tr(STR_DELETE) + std::string("? ");

    // Compose the display copy; `entry` stays raw for the delete path itself.
    leaving = true;
    startActivityForResult(
        std::make_unique<ConfirmationActivity>(renderer, mappedInput, heading, utf8ComposeNfc(entry)), handler);
    return;
  } else {
    // --- SHORT PRESS ACTION: OPEN/NAVIGATE ---
    // buildScreen() runs on the render task and reads basepath plus the
    // ListItem label/value pointers into rowNames/rowExtensions that
    // rebuildRowItems() frees; mutate only under the render lock.
    RenderLock lock(*this);
    if (basepath.back() != '/') basepath += "/";

    if (isDirectory) {
      basepath += entry.substr(0, entry.length() - 1);
      loadFiles();
      nav.selected = 0;
      nav.top = 0;
      lock.unlock();
      requestUpdate();
    } else {
      const std::string fullPath = basepath + entry;
      lock.unlock();  // onSelectBook launches an activity; don't hold the lock across it
      leaving = true;
      stopCoverLoader();
      onSelectBook(fullPath);
    }
  }
  return;
}

bool FileBrowserActivity::handleCustomInput() {
  if (optionsPopup && optionsPopup->isActive()) {
    {
      RenderLock lock(*this);
      optionsPopup->handleInput(mappedInput, [this] { requestUpdate(); });
    }
    if (pendingOption >= 0) {
      const int option = pendingOption;
      pendingOption = -1;
      if (choosingView) {
        if (SETTINGS.libraryView != option) {
          {
            RenderLock lock(*this);
            SETTINGS.libraryView = static_cast<uint8_t>(option);
            coverAllocationFailed = false;
            loaderFailed = false;
            coverTop = -1;
            nav.drawnRows = 0;
            nav.followOnBuild = true;
          }
          SETTINGS.saveToFile();
          if (option == CrossPointSettings::COMPACT_LIST) {
            stopCoverLoader();
            RenderLock lock(*this);
            coverRenderer.reset();
            for (auto& row : coverRows) row = {};
            loadResult = {};
            coverCount = 0;
          }
          requestUpdate();
        }
      } else if (option == 2) {
        showOptions(true);
      } else {
        activateSelected(option == 1);
      }
    }
    return true;
  }
  // Long press BACK (1s+) goes to root folder (Books mode only).
  // In firmware-pick mode we keep navigation simple: short Back = up dir / cancel.
  if (mode == Mode::Books && mappedInput.wasReleased(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() >= GO_HOME_MS && basepath != "/") {
    {
      // buildScreen() runs on the render task and reads basepath plus the
      // row caches rebuildRowItems() frees; mutate only under the render lock.
      RenderLock lock(*this);
      basepath = "/";
      loadFiles();
      nav.selected = 0;
      nav.top = 0;
    }
    requestUpdate();
    return true;
  }

  return false;
}

bool FileBrowserActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (mode == Mode::Books && (mappedInput.getHeldTime() >= GO_HOME_MS || files.empty())) {
      showOptions();
    } else {
      activateSelected();
    }
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // Short press: go up one directory, or go home if at root
    if (mappedInput.getHeldTime() < GO_HOME_MS) {
      if (basepath != "/") {
        const std::string oldPath = basepath;

        {
          // buildScreen() runs on the render task and reads basepath plus the
          // row caches rebuildRowItems() frees; mutate only under the render lock.
          RenderLock lock(*this);
          basepath.replace(basepath.find_last_of('/'), std::string::npos, "");
          if (basepath.empty()) basepath = "/";
          loadFiles();

          const auto pos = oldPath.find_last_of('/');
          const std::string dirName = oldPath.substr(pos + 1) + "/";
          nav.selected = static_cast<int>(findEntry(dirName));
          nav.top = 0;
          nav.follow(listCount());
        }

        requestUpdate();
      } else if (mode == Mode::PickFirmware) {
        // Firmware picker at root: cancel back to caller instead of going home.
        ActivityResult res;
        res.isCancelled = true;
        setResult(std::move(res));
        finish();
      } else {
        onGoHome();
      }
    }
    return true;
  }

  return false;
}

std::string getFileName(std::string filename) {
  // Display copy only — `files[]` keeps the raw directory-entry bytes, because
  // FAT long-filename lookup is byte-exact: an NFC-normalized path would fail
  // to open the NFD entry macOS wrote. Composing here fixes rendering (fonts
  // carry precomposed syllables / letters only) without touching paths.
  filename = utf8ComposeNfc(filename);
  if (filename.back() == '/') {
    filename.pop_back();
    if (!UITheme::getInstance().getTheme().showsFileIcons()) {
      return "[" + filename + "]";
    }
    return filename;
  }
  const auto pos = filename.rfind('.');
  return filename.substr(0, pos);
}

std::string getFileExtension(const std::string& filename) {
  if (filename.back() == '/') {
    return "";
  }
  const auto pos = filename.rfind('.');
  return filename.substr(pos);
}

void FileBrowserActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // Full path band at the bottom: separator on top, left-truncated so the
  // deepest directory stays visible.
  {
    const int pathLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
    const fui::Rect band = screen.takeBottom(static_cast<int16_t>(pathLineHeight + metrics.verticalSpacing));
    screen.target().fill(fui::Rect{band.x, band.y, band.width, 3}, fui::Paint::solid(fui::Color::Black));
    const int pathY =
        band.y + metrics.verticalSpacing / 2 + (band.height - metrics.verticalSpacing / 2 - pathLineHeight) / 2;
    const int pathMaxWidth = band.width - metrics.contentSidePadding * 2;
    const char* pathStr = basepath.c_str();
    const char* pathDisplay = pathStr;
    char leftTruncBuf[256];
    if (renderer.getTextWidth(SMALL_FONT_ID, pathStr) > pathMaxWidth) {
      const char ellipsis[] = "\xe2\x80\xa6";  // UTF-8 ellipsis (…)
      const int ellipsisWidth = renderer.getTextWidth(SMALL_FONT_ID, ellipsis);
      const int available = pathMaxWidth - ellipsisWidth;
      // Walk forward from the start until the suffix fits, skipping UTF-8 continuation bytes
      const char* p = pathStr;
      while (*p) {
        if (renderer.getTextWidth(SMALL_FONT_ID, p) <= available) break;
        ++p;
        while (*p && (static_cast<unsigned char>(*p) & 0xC0) == 0x80) ++p;
      }
      snprintf(leftTruncBuf, sizeof(leftTruncBuf), "%s%s", ellipsis, p);
      pathDisplay = leftTruncBuf;
    }
    renderer.drawText(SMALL_FONT_ID, band.x + metrics.contentSidePadding, pathY, pathDisplay);
  }

  if (files.empty()) {
    screen.centeredText(mode == Mode::PickFirmware ? tr(STR_NO_BIN_FILES) : tr(STR_NO_FILES_FOUND),
                        screen.theme().bodyText);
    return;
  }

  // rowNames/rowExtensions/rowItems are built once per loadFiles() call (see
  // rebuildRowItems()) and reused here. getFileName()'s folder-bracket format
  // depends on the theme, so a theme change picked up while this activity was
  // paused underneath another screen invalidates the cache before it's read.
  if (rowsUseFileIcons != UITheme::getInstance().getTheme().showsFileIcons()) {
    rebuildRowItems();
  }

  if (coverView()) {
    if (!coverRenderer) {
      // Reuse the BMP palette, scanline buffers and card props across all visible rows.
      coverRenderer = makeUniqueNoThrow<LibraryCoverRenderer>();
      if (!coverRenderer) {
        LOG_ERR("FileBrowser", "OOM: cover row renderer; using compact list");
        coverAllocationFailed = true;
      }
    }
    if (coverRenderer) {
      buildCoverList(screen);
      return;
    }
  }

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  // Tap opens/navigates; long-press prompts delete (physical buttons stay in loop()).
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  props.valueInset = 8;  // air between the extension and the row edge
  // Names use up to two small-font lines; shared list layout sizes each row.
  fui::TextStyle label = screen.theme().smallText;
  label.maxLines = 2;
  props.labelText = label;

  // The trailing value here is just the short extension: skip the balanced
  // 60%-band wrap cap and let both name lines run the full width before it.
  props.balanceWrappedLabelWithValue = false;
  syncListViewport(screen, props);
  screen.list(props);
}

void FileBrowserActivity::buildCoverList(UiScreen& screen) {
  const auto status = screen.takeBottom(
      static_cast<int16_t>(screen.target().lineHeight(screen.theme().smallText.font) + screen.theme().spaceSm));
  const int rowHeight = GUI.getLibraryRowHeight(screen);
  const int gap = screen.theme().listRowGap;
  const auto body = screen.body();
  const int rows = std::max(1, std::min(MAX_COVER_ROWS, (body.height + gap) / (rowHeight + gap)));
  const bool resized = nav.visibleRows != rows;
  nav.drawnRows = rows;
  nav.drawnCount = listCount();
  if (resized) nav.followOnBuild = true;
  auto viewportBody = body;
  viewportBody.height = static_cast<int16_t>(std::min<int>(body.height, rows * (rowHeight + gap) - gap));
  auto& viewport = coverRenderer->viewport;
  nav.syncToProps(viewportBody, rowHeight, gap, listCount(), viewport);
  const int count = std::min(rows, listCount() - nav.top);
  if (coverTop != nav.top || coverCount != count) {
    coverTop = nav.top;
    coverCount = count;
    pageChangedAt = millis();
    detailsRenderedAt = pageChangedAt;
    for (auto& row : coverRows) {
      row.details.title.clear();
      row.details.author.clear();
      row.details.coverPath.clear();
      row.loaded = false;
    }
  }
  for (int slot = 0; slot < count; ++slot) {
    const int index = nav.top + slot;
    const auto& details = coverRows[slot].details;
    GUI.drawLibraryBookRow(
        screen, renderer, *coverRenderer, details.title.empty() ? rowNames[index].c_str() : details.title.c_str(),
        details.author.empty() ? nullptr : details.author.c_str(), details.coverPath.c_str(),
        UITheme::getFileIcon(files[index]), index == viewport.selectedIndex, index, ACTION_ROW, rowHeight);
  }
  nav.onListRendered(static_cast<uint16_t>(nav.top), count,
                     viewport.selectedIndex >= nav.top && viewport.selectedIndex < nav.top + count);
  fui::drawListScrollIndicator(screen.target(), body, listCount(), rows, nav.top, screen.theme().listScrollWidth,
                               screen.theme().listScrollSide, screen.theme().listScrollInset);
  bool loading = false;
  for (int slot = 0; slot < count; ++slot) {
    loading |= !coverRows[slot].loaded && FsHelpers::hasEpubExtension(files[nav.top + slot]);
  }
  if (loading && !loaderFailed) screen.target().text(status, tr(STR_LOADING_COVERS), screen.theme().smallText);
}

void FileBrowserActivity::drawChrome() {
  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();

  std::string folderName =
      (mode == Mode::PickFirmware)
          ? std::string(tr(STR_SELECT_FIRMWARE_FILE))
          : ((basepath == "/") ? std::string(tr(STR_SD_CARD)) : basepath.substr(basepath.rfind('/') + 1));
  // Header via GUI.drawHeader (already FreeInkUI-themed) for the battery
  // indicator; the rest of the screen renders through the app.
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, folderName.c_str());
}

void FileBrowserActivity::drawFooter() {
  if (optionsPopup && optionsPopup->isActive()) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    optionsPopup->render(renderer);
    return;
  }
  const char* backLabel = (basepath == "/") ? (mode == Mode::PickFirmware ? tr(STR_BACK) : tr(STR_HOME)) : tr(STR_BACK);
  // In PickFirmware mode, Confirm on a .bin returns the path to the caller (not "open"); show
  // STR_SELECT instead. Directories in the same picker still descend, so keep STR_OPEN there.
  const bool selectingFirmwareFile = mode == Mode::PickFirmware && !files.empty() && nav.selected >= 0 &&
                                     nav.selected < listCount() && files[nav.selected].back() != '/';
  const char* confirmLabel = mode == Mode::Books
                                 ? tr(STR_OPEN_OPTIONS)
                                 : (files.empty() ? "" : (selectingFirmwareFile ? tr(STR_SELECT) : tr(STR_OPEN)));
  const auto labels = mappedInput.mapLabels(backLabel, confirmLabel, files.empty() ? "" : tr(STR_DIR_UP),
                                            files.empty() ? "" : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

size_t FileBrowserActivity::findEntry(const std::string& name) const {
  const auto entry = std::find(files.begin(), files.end(), name);
  return entry != files.end() ? static_cast<size_t>(entry - files.begin()) : 0;
}
