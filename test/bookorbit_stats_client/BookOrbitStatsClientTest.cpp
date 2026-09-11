#include <ArduinoJson.h>
#include <BookOrbitStatsClient.h>
#include <ReadingStatsRecorder.h>
#include <gtest/gtest.h>

#include <string>

std::string responseBody, requestBody, requestPath;

namespace {
constexpr char HASH[] = "0123456789abcdef0123456789abcdef";

class BookOrbitStatsClientTest : public testing::Test {
 protected:
  ReadingStatsEvent event{};
  void SetUp() override {
    event.startEpochSeconds = 1700000000;
    event.durationSeconds = 60;
    event.page = 1250;
    event.totalPages = 10000;
    responseBody =
        R"({"results":[{"hash":"0123456789abcdef0123456789abcdef","accepted":1,"duplicates":0,"watermark":1700000000}],"unmatched":[]})";
    requestBody.clear();
    requestPath.clear();
  }
};

TEST_F(BookOrbitStatsClientTest, SendsMeasuredTimeAndLogicalProgressWithStableDeviceIdentity) {
  ASSERT_TRUE(BookOrbitStatsClient::upload(HASH, &event, 1));
  EXPECT_EQ(requestPath, "/plugin/page-stats");
  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, requestBody));
  EXPECT_EQ(doc["books"][0]["events"][0]["page"].as<int>(), 1250);
  EXPECT_EQ(doc["books"][0]["events"][0]["totalPages"].as<int>(), 10000);
  EXPECT_EQ(doc["books"][0]["events"][0]["durationSeconds"].as<int>(), 60);
  EXPECT_EQ(doc["books"][0]["events"][0]["startTime"].as<int64_t>(), 1700000000);
  EXPECT_EQ(doc["deviceId"].as<std::string>(), "060504030201");
}

TEST_F(BookOrbitStatsClientTest, AcceptsDuplicateRetryAcknowledgement) {
  responseBody =
      R"({"results":[{"hash":"0123456789abcdef0123456789abcdef","accepted":0,"duplicates":1,"watermark":1700000000}],"unmatched":[]})";
  EXPECT_TRUE(BookOrbitStatsClient::upload(HASH, &event, 1));
}

TEST_F(BookOrbitStatsClientTest, RetainsIncompleteAcknowledgement) {
  responseBody =
      R"({"results":[{"hash":"0123456789abcdef0123456789abcdef","accepted":0,"duplicates":0,"watermark":1700000000}],"unmatched":[]})";
  EXPECT_FALSE(BookOrbitStatsClient::upload(HASH, &event, 1));
}

TEST_F(BookOrbitStatsClientTest, RetainsUnmatchedAndMalformedResponses) {
  responseBody = R"({"results":[],"unmatched":["0123456789abcdef0123456789abcdef"]})";
  EXPECT_FALSE(BookOrbitStatsClient::upload(HASH, &event, 1));
  responseBody = "{}";
  EXPECT_FALSE(BookOrbitStatsClient::upload(HASH, &event, 1));
  responseBody = std::string(2049, ' ');
  EXPECT_FALSE(BookOrbitStatsClient::upload(HASH, &event, 1));
}

TEST_F(BookOrbitStatsClientTest, DistinguishesExplicitUnmatchedBookAndResetsForNextUpload) {
  responseBody = R"({"results":[],"unmatched":["0123456789abcdef0123456789abcdef"]})";
  EXPECT_FALSE(BookOrbitStatsClient::upload(HASH, &event, 1));
  EXPECT_TRUE(BookOrbitStatsClient::lastUploadWasUnmatched());
  responseBody = R"({"results":[],"unmatched":["ffffffffffffffffffffffffffffffff"]})";
  EXPECT_FALSE(BookOrbitStatsClient::upload(HASH, &event, 1));
  EXPECT_FALSE(BookOrbitStatsClient::lastUploadWasUnmatched());
  responseBody = R"({"results":[],"unmatched":["0123456789abcdef0123456789abcdef"]})";
  EXPECT_FALSE(BookOrbitStatsClient::upload(HASH, &event, 1));
  EXPECT_TRUE(BookOrbitStatsClient::lastUploadWasUnmatched());
  EXPECT_FALSE(BookOrbitStatsClient::upload(HASH, &event, 9));
  EXPECT_FALSE(BookOrbitStatsClient::lastUploadWasUnmatched());
}

TEST_F(BookOrbitStatsClientTest, RejectsWrongWatermarkAndHash) {
  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, responseBody));
  doc["results"][0]["watermark"] = 1700000001;
  responseBody.clear();
  serializeJson(doc, responseBody);
  EXPECT_FALSE(BookOrbitStatsClient::upload(HASH, &event, 1));
  doc["results"][0]["watermark"] = 1700000000;
  doc["results"][0]["hash"] = "ffffffffffffffffffffffffffffffff";
  responseBody.clear();
  serializeJson(doc, responseBody);
  EXPECT_FALSE(BookOrbitStatsClient::upload(HASH, &event, 1));
}

TEST_F(BookOrbitStatsClientTest, RejectsUndatedEventsAndOversizedBatchesBeforeNetwork) {
  EXPECT_FALSE(BookOrbitStatsClient::upload(HASH, &event, 9));
  event.startEpochSeconds = 0;
  EXPECT_FALSE(BookOrbitStatsClient::upload(HASH, &event, 1));
  EXPECT_TRUE(requestPath.empty());
}

TEST_F(BookOrbitStatsClientTest, SendsSweepCountsAndRequiresBooleanAcknowledgement) {
  responseBody = R"({"ok":true})";
  ASSERT_TRUE(BookOrbitStatsClient::completeSweep(8, 2));
  EXPECT_EQ(requestPath, "/plugin/sweeps");
  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, requestBody));
  EXPECT_EQ(doc["booksMatched"].as<int>(), 2);
  EXPECT_EQ(doc["pageStatsUploaded"].as<int>(), 8);
  responseBody = R"({"ok":1})";
  EXPECT_FALSE(BookOrbitStatsClient::completeSweep(8, 2));
}
}  // namespace
