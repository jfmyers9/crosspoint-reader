#pragma once

#include <string>

struct LibraryBookDetails {
  std::string title;
  std::string author;
  std::string coverPath;
};

// Call serially on the browser worker. Empty fields retain the filename/cover placeholder.
bool loadLibraryBookDetails(const std::string& path, int coverHeight, LibraryBookDetails& out);
