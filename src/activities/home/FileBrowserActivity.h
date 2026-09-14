#pragma once

#include <ReadingStatus.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "util/LibraryBookDetails.h"
#include "util/LibraryCoverLoader.h"

class LibraryCoverRenderer;
class OptionPopup;

class FileBrowserActivity final : public UiListActivity {
 public:
  // Books = standard reader browser; PickFirmware = filter to .bin only and return path via ActivityResult.
  enum class Mode { Books, PickFirmware };

 private:
  // File actions
  bool removeDirFile(const std::string& fullPath);
  void startRename();
  void renameSelectedFile(const std::string& oldPath, const std::string& oldEntry, const std::string& newStem,
                          const std::string& extension);
  void deleteSelected();

  Mode mode = Mode::Books;

  // Files state
  std::string basepath = "/";
  std::vector<std::string> files;
  std::unique_ptr<char[]> fileNameBuffer;

  // Pull-based rows: the SDK list resolves each drawn row on demand through
  // provideRow() (fui::ListProps::rowProvider), so the only per-file
  // residency is `files` itself — no full-length rowNames/rowExtensions/
  // rowItems arrays (a 1000-file folder used to pin ~100KB of vectors plus a
  // heap copy of every display name, which aborted under -fno-exceptions
  // when the contiguous blocks no longer fit). The label/value strings for
  // the row being laid out live in these scratch buffers; the provider
  // contract only needs them valid until the next provideRow() call.
  static constexpr size_t ROW_NAME_BUF_SIZE = 512;  // NAME_BUFFER_SIZE + "[]" + terminator slack
  char rowNameBuf[ROW_NAME_BUF_SIZE]{};
  char rowExtBuf[16]{};
  static void provideRow(void* ctx, uint16_t index, freeink::ui::ListItem& item);
  struct StatusRow {
    std::string path;
    ReadingStatus::Status status;
    char label[32] = {};
  };
  // Reused visible-page storage, never sized to the directory.
  std::vector<StatusRow> statusRows;
  std::string statusPath;
  const ReadingStatus::Status& statusFor(int index, int slot);

  // CJK fallback glyphs are prewarmed for a bounded window of rows around the
  // viewport (one SD pass per list page, like the reader TOC) instead of the
  // whole folder. -1 = nothing prewarmed; reset by loadFiles().
  static constexpr int PREWARM_WINDOW = 24;
  int prewarmedStart = -1;
  void prewarmRowGlyphs(int start);
  static constexpr int MAX_COVER_ROWS = 8;
  struct CoverRow {
    LibraryBookDetails details;
    bool loaded = false;
  };
  std::array<CoverRow, MAX_COVER_ROWS> coverRows;
  std::unique_ptr<LibraryCoverRenderer> coverRenderer;
  std::unique_ptr<OptionPopup> optionsPopup;
  int pendingOption = -1;
  bool choosingView = false;
  bool leaving = false;
  bool coverAllocationFailed = false;
  int coverTop = -1;
  int coverCount = 0;
  uint32_t folderGeneration = 0;
  uint32_t pageChangedAt = 0;
  uint32_t detailsRenderedAt = 0;
  bool detailsDirty = false;
  LibraryCoverLoader coverLoader;

  bool coverView() const;
  void buildCoverList(UiScreen& screen);
  void serviceCoverLoader();
  void stopCoverLoader();
  void showOptions(bool viewOnly = false);

  int listCount() const override { return static_cast<int>(files.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  // Long-press BACK goes to root; short Back goes up a directory (home/cancel at
  // root), and Confirm activates on release while a hold opens file actions.
  bool handleCustomInput() override;
  bool handleButtons() override;
  // Header shows the current folder name (battery indicator via GUI.drawHeader);
  // footer labels depend on path depth and picker mode.
  void drawChrome() override;
  void drawFooter() override;
  void activateSelected();

  // Data loading
  void loadFiles();
  size_t findEntry(const std::string& name) const;

 public:
  explicit FileBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initialPath = "/",
                               Mode mode = Mode::Books);
  ~FileBrowserActivity() override;
  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool preventAutoSleep() override { return coverLoader.working(); }
  bool skipLoopDelay() override { return coverLoader.working(); }
};
