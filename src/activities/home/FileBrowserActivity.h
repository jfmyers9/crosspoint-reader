#pragma once

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
  // Deletion
  bool removeDirFile(const std::string& fullPath);

  Mode mode = Mode::Books;

  // Files state
  std::string basepath = "/";
  std::vector<std::string> files;
  std::unique_ptr<char[]> fileNameBuffer;

  // Per-row render buffers, derived from `files` and rebuilt only when it
  // changes (loadFiles()) rather than on every repaint — buildScreen() used to
  // rebuild a name/extension string and a ListItem per file on every render
  // (cursor move, tap flash, ...), which meant a 500-file directory allocated
  // 500 strings per repaint instead of once per directory load.
  std::vector<std::string> rowNames;
  std::vector<std::string> rowExtensions;
  std::vector<freeink::ui::ListItem> rowItems;
  // getFileName()'s "[folder]" bracket formatting depends on the active
  // theme's showsFileIcons(); tracked so a theme change while this activity is
  // paused underneath (e.g. a Settings screen reached via a picker flow)
  // invalidates the cached rows on return instead of rendering stale ones.
  bool rowsUseFileIcons = false;

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

  void rebuildRowItems();

  int listCount() const override { return static_cast<int>(files.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  // Long-press BACK goes to root; short Back goes up a directory (home/cancel at
  // root), and Confirm activates on RELEASE (a hold is "delete").
  bool handleCustomInput() override;
  bool handleButtons() override;
  // Header shows the current folder name (battery indicator via GUI.drawHeader);
  // footer labels depend on path depth and picker mode.
  void drawChrome() override;
  void drawFooter() override;
  // forceDelete routes the touch long-press to the delete branch; button
  // navigation leaves it false and relies on getHeldTime() instead.
  void activateSelected(bool forceDelete = false);

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
