"""Exercise production selection control flow without display/SD hardware."""

from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/activities/reader/DictionaryWordSelectActivity.cpp").read_text()


def method(name, next_marker):
    start = source.index(f"void DictionaryWordSelectActivity::{name}(")
    return source[start:source.index(next_marker, start)]


harness = r"""
#include <cassert>
#include <functional>
#include <vector>
#include "src/util/WordSelectionState.h"
enum class StrId { STR_LOOKUP, STR_HIGHLIGHT, STR_EXTEND_SELECTION, STR_CANCEL,
                   STR_TEXT_SELECTION, STR_SELECT_RANGE_END };
unsigned long now = 0;
unsigned long millis() { return now; }
constexpr unsigned long POPUP_DURATION_MS = 1500;
constexpr unsigned long WORD_REPEAT_START_MS = 500;
constexpr unsigned long WORD_REPEAT_INTERVAL_MS = 500;
namespace haptic_feedback { void touchAction() {} }
struct MappedInputManager {
  enum class Button { Back, Confirm, ScreenLeft, ScreenRight, ScreenUp, ScreenDown };
  bool back = false, confirm = false, tap = false;
  bool wasReleased(Button b) { return b == Button::Back ? back : b == Button::Confirm && confirm; }
  bool wasPressed(Button) { return false; }
  bool isPressed(Button) { return false; }
  unsigned long getHeldTime() { return 0; }
  bool wasScreenTouchDown(int&, int&) { return false; }
  bool wasScreenTapped(int&, int&) { return tap; }
  bool wasAnyPressed() { return false; }
  bool wasAnyReleased() { return back || confirm; }
};
// Model OptionPopup's contract: dismissal does not invoke the selection callback.
struct OptionPopup {
  bool active = false, dismiss = false;
  int action = -1;
  std::function<void(int)> callback;
  void show(StrId, const StrId*, int count, int, std::function<void(int)> cb) {
    assert(count == 4);
    active = true;
    callback = cb;
  }
  bool isActive() { return active; }
  bool handleInput(MappedInputManager& input, std::function<void()> update) {
    if (!active) return false;
    if (action >= 0) {
      active = false;
      callback(action);
      update();
    } else if (dismiss || input.back) {
      active = false;
      update();
    }
    return true;
  }
};
struct DictionaryWordSelectActivity {
  enum class Popup { None, Hint, NotFound, Error };
  MappedInputManager mappedInput;
  OptionPopup selectionActions;
  WordSelectionState selectionState;
  struct { bool save = true; } selectionContext;
  bool selectionActionChosen = false;
  Popup popup = Popup::None;
  StrId popupMsg{};
  unsigned long popupTime = 0, lastHorizontalMoveTime = 0;
  int selected = 0, snapshotIdx = -1, hit = -1;
  int finishes = 0, lookups = 0, saves = 0;
  std::vector<int> words{1, 2};
  void requestUpdate() {}
  void finish() { ++finishes; }
  void performLookup() { ++lookups; }
  void saveSelection() { ++saves; finish(); }
  int wordAt(int, int) { return hit; }
  void selectWord(int i) { selected = i; selectionState.select(i); }
  void moveVertical(int) {}
  void loop();
  void showSelectionActions();
  void closeDefinition();
};
"""
harness += method("showSelectionActions", "void DictionaryWordSelectActivity::saveSelection")
harness += method("loop", "// Saves the pixels under")
# Compile the production definition-result callback with the same activity stub.
callback = source.split("startActivityForResult(std::move(activity), [this](const ActivityResult&) {", 1)[1]
harness += "void DictionaryWordSelectActivity::closeDefinition() {" + callback.split("\n    });", 1)[0] + "\n}\n"
harness += r"""
int main() {
  // Closing a touch lookup returns to reading; button mode allows repeated lookup.
  for (bool touch : {false, true}) {
    DictionaryWordSelectActivity a;
    a.selectionContext.save = touch;
    a.closeDefinition();
    assert(a.finishes == touch);
  }
  // Back and outside taps must exit the picker, not leave word navigation active.
  for (bool outside : {false, true}) {
    DictionaryWordSelectActivity a;
    a.showSelectionActions();
    a.mappedInput.back = !outside;
    a.selectionActions.dismiss = outside;
    a.loop();
    assert(a.finishes == 1);
  }
  // A chosen action must not be mistaken for popup cancellation.
  for (int action = 0; action < 4; ++action) {
    DictionaryWordSelectActivity a;
    a.selectWord(0);
    a.showSelectionActions();
    a.selectionActions.action = action;
    a.loop();
    assert(a.lookups == (action == 0));
    assert(a.saves == (action == 1));
    assert(a.finishes == (action == 1 || action == 3));
    assert(a.selectionState.extending() == (action == 2));
  }
  // An open menu with no action still owns input and does not exit.
  {
    DictionaryWordSelectActivity a;
    a.showSelectionActions();
    a.loop();
    assert(a.finishes == 0 && a.selectionActions.isActive());
  }
  // Back is available even while a lookup/save error is visible.
  for (auto popup : {DictionaryWordSelectActivity::Popup::Error,
                     DictionaryWordSelectActivity::Popup::NotFound}) {
    DictionaryWordSelectActivity a;
    a.popup = popup;
    a.mappedInput.back = true;
    a.loop();
    assert(a.finishes == 1);
  }
  // Errors restore an explicit action menu on touch, not an invisible picker.
  for (bool touch : {false, true}) {
    DictionaryWordSelectActivity a;
    a.selectionContext.save = touch;
    a.popup = DictionaryWordSelectActivity::Popup::Error;
    now = POPUP_DURATION_MS;
    a.loop();
    assert(a.selectionActions.isActive() == touch);
    assert(a.finishes == 0);
  }
  // Blank taps exit extension; word taps retain the range and reopen actions.
  for (int hit : {-1, 1}) {
    DictionaryWordSelectActivity a;
    a.selectWord(0);
    a.selectionState.beginExtension();
    a.hit = hit;
    a.mappedInput.tap = true;
    a.loop();
    assert(a.finishes == (hit < 0));
    assert(a.selectionActions.isActive() == (hit >= 0));
    assert(a.saves == 0);
    if (hit >= 0) assert(a.selectionState.first() == 0 && a.selectionState.last() == 1);
  }
}
"""
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / "selection.cpp"
    executable = Path(directory) / "selection"
    cpp.write_text(harness)
    subprocess.run([
        "c++", "-std=c++20", "-Wall", "-Wextra", "-I", str(ROOT),
        str(cpp), "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True)
print("Word selection exit regression checks passed")
