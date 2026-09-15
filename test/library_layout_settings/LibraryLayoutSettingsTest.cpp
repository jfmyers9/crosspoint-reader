#include <gtest/gtest.h>

#include "LibraryLayoutSettings.h"

TEST(LibraryLayoutSettings, DefaultsToCompact) {
  JsonDocument doc;
  bool resave = false;
  EXPECT_EQ(readLibraryLayout(doc.as<JsonVariantConst>(), resave), 0);
  EXPECT_FALSE(resave);
}

TEST(LibraryLayoutSettings, PreservesValidNewLayout) {
  for (uint8_t layout : {0, 1}) {
    JsonDocument doc;
    doc["libraryLayout"] = layout;
    bool resave = false;
    EXPECT_EQ(readLibraryLayout(doc.as<JsonVariantConst>(), resave), layout);
    EXPECT_FALSE(resave);
  }
}

TEST(LibraryLayoutSettings, MigratesBooleanAndNumericLegacyValues) {
  for (const char* json : {"{\"libraryCoverView\":true}", "{\"libraryCoverView\":1}"}) {
    JsonDocument doc;
    ASSERT_FALSE(deserializeJson(doc, json));
    bool resave = false;
    EXPECT_EQ(readLibraryLayout(doc.as<JsonVariantConst>(), resave), 1);
    EXPECT_TRUE(resave);
  }
  for (const char* json : {"{\"libraryCoverView\":false}", "{\"libraryCoverView\":0}"}) {
    JsonDocument doc;
    ASSERT_FALSE(deserializeJson(doc, json));
    bool resave = false;
    EXPECT_EQ(readLibraryLayout(doc.as<JsonVariantConst>(), resave), 0);
    EXPECT_TRUE(resave);
  }
}

TEST(LibraryLayoutSettings, DoesNotMigrateFileBrowserPreference) {
  JsonDocument doc;
  doc["libraryView"] = 1;
  bool resave = false;
  EXPECT_EQ(readLibraryLayout(doc.as<JsonVariantConst>(), resave), 0);
  EXPECT_TRUE(resave);
}

TEST(LibraryLayoutSettings, NewKeyWinsOverLegacyEvenWhenInvalid) {
  for (const char* json : {"{\"libraryLayout\":0}", "{\"libraryLayout\":null}",
                           "{\"libraryLayout\":true}", "{\"libraryLayout\":256}",
                           "{\"libraryLayout\":-1}", "{\"libraryLayout\":2}",
                           "{\"libraryLayout\":\"1\"}"}) {
    JsonDocument doc;
    ASSERT_FALSE(deserializeJson(doc, json));
    doc["libraryCoverView"] = true;
    bool resave = false;
    EXPECT_EQ(readLibraryLayout(doc.as<JsonVariantConst>(), resave), 0) << json;
    EXPECT_TRUE(resave);
  }
}

TEST(LibraryLayoutSettings, RejectsInvalidLegacyValues) {
  for (const char* json : {"{\"libraryCoverView\":null}", "{\"libraryCoverView\":256}",
                           "{\"libraryCoverView\":-1}", "{\"libraryCoverView\":2}",
                           "{\"libraryCoverView\":\"1\"}"}) {
    JsonDocument doc;
    ASSERT_FALSE(deserializeJson(doc, json));
    bool resave = false;
    EXPECT_EQ(readLibraryLayout(doc.as<JsonVariantConst>(), resave), 0) << json;
    EXPECT_TRUE(resave);
  }
}

TEST(LibraryLayoutSettings, PreservesExistingResaveRequest) {
  JsonDocument doc;
  bool resave = true;
  EXPECT_EQ(readLibraryLayout(doc.as<JsonVariantConst>(), resave), 0);
  EXPECT_TRUE(resave);
}
