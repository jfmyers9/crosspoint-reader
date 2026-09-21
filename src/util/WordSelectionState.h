#pragma once

// Word indices are inclusive endpoints in display/reading order. The caller
// validates upper bounds against its word collection; negative indices clear
// the selection. Input source (touch or buttons) does not affect transitions.
class WordSelectionState {
 public:
  constexpr void select(const int index) {
    if (index < 0 || !extending_) {
      reset(index);
      return;
    }
    selected_ = index;
  }

  constexpr void beginExtension() {
    if (valid()) extending_ = true;
  }

  // Also ends extension. reset(selected()) collapses to the current endpoint;
  // reset(anchor()) collapses to the original endpoint.
  constexpr void reset(const int index) {
    anchor_ = selected_ = index < 0 ? -1 : index;
    extending_ = false;
  }

  constexpr int anchor() const { return anchor_; }
  constexpr int selected() const { return selected_; }
  constexpr int first() const { return anchor_ < selected_ ? anchor_ : selected_; }
  constexpr int last() const { return anchor_ > selected_ ? anchor_ : selected_; }
  constexpr bool extending() const { return extending_; }
  constexpr bool valid() const { return anchor_ >= 0 && selected_ >= 0; }

 private:
  int anchor_ = -1;
  int selected_ = -1;
  bool extending_ = false;
};
