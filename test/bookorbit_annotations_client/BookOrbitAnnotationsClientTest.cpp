#include <ArduinoJson.h>
#include <BookOrbitAnnotationsClient.h>
#include <BookOrbitStatsClient.h>
#include <gtest/gtest.h>

#include <cstring>
#include <string>

std::string responseBody, requestBody, requestPath;
bool transportFails = false;
int postCount = 0;
std::string deviceIdentity = "060504030201";
const char* BookOrbitStatsClient::deviceId() { return deviceIdentity.c_str(); }

namespace {
constexpr char HASH[] = "0123456789abcdef0123456789abcdef";
constexpr char DATE[] = "2026-10-04 10:20:30";
constexpr char POS0[] = "/body/DocFragment[1]/body/p[1]/text()[1].0";
constexpr char POS1[] = "/body/DocFragment[1]/body/p[1]/text()[1].5";
std::string ack(const char* count = "1") {
  return std::string("{\"results\":[{\"hash\":\"") + HASH + "\",\"upserted\":" + count + "}],\"unmatched\":[]}";
}
bool upload(const char* hash = HASH, const char* date = DATE, const char* updated = DATE, const char* pos0 = POS0,
            const char* pos1 = POS1, const char* text = "hello", const char* note = "") {
  return BookOrbitAnnotationsClient::upload(hash, date, updated, pos0, pos1, text, note);
}
class BookOrbitAnnotationsClientTest : public testing::Test {
 protected:
  void SetUp() override {
    responseBody = ack();
    requestBody.clear();
    requestPath.clear();
    transportFails = false;
    postCount = 0;
    deviceIdentity = "060504030201";
  }
};

TEST_F(BookOrbitAnnotationsClientTest, SendsExactSingleAnnotationSchemaAndExplicitEmptyNote) {
  ASSERT_TRUE(upload());
  EXPECT_EQ(requestPath, "/plugin/annotations");
  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, requestBody));
  EXPECT_EQ(doc.size(), 4u);
  EXPECT_EQ(doc["deviceId"].as<std::string>(), deviceIdentity);
  EXPECT_EQ(doc["deviceModel"].as<std::string>(), "CrossPoint X4 Pro");
  EXPECT_EQ(doc["pluginVersion"].as<std::string>(), "crosspoint-1");
  ASSERT_EQ(doc["books"].size(), 1u);
  EXPECT_EQ(doc["books"][0].size(), 2u);
  EXPECT_EQ(doc["books"][0]["hash"].as<std::string>(), HASH);
  ASSERT_EQ(doc["books"][0]["annotations"].size(), 1u);
  auto row = doc["books"][0]["annotations"][0];
  EXPECT_EQ(row.size(), 8u);
  EXPECT_EQ(row["datetime"].as<std::string>(), DATE);
  EXPECT_EQ(row["datetimeUpdated"].as<std::string>(), DATE);
  EXPECT_EQ(row["drawer"].as<std::string>(), "underscore");
  EXPECT_EQ(row["posFormat"].as<std::string>(), "xpointer");
  EXPECT_EQ(row["pos0"].as<std::string>(), POS0);
  EXPECT_EQ(row["pos1"].as<std::string>(), POS1);
  EXPECT_EQ(row["text"].as<std::string>(), "hello");
  EXPECT_TRUE(row["note"].is<const char*>());
  EXPECT_EQ(row["note"].as<std::string>(), "");
}

TEST_F(BookOrbitAnnotationsClientTest, RetryZeroIsAcknowledgedWithIdenticalPayload) {
  ASSERT_TRUE(upload());
  const auto first = requestBody;
  responseBody = ack("0");
  EXPECT_TRUE(upload());
  EXPECT_EQ(first, requestBody);
  EXPECT_FALSE(BookOrbitAnnotationsClient::lastUploadWasUnmatched());
}

TEST_F(BookOrbitAnnotationsClientTest, RejectsMalformedAcknowledgements) {
  for (const auto* count : {"-1", "2", "true", "null", "\"1\"", "1.0", "4294967296"}) {
    responseBody = ack(count);
    EXPECT_FALSE(upload()) << count;
  }
  for (const auto* body :
       {"{}", "null", "[]", "{", "{\"results\":[],\"unmatched\":[]}", "{\"results\":[{}],\"unmatched\":[]}",
        "{\"results\":[{\"upserted\":1}],\"unmatched\":[]}"}) {
    responseBody = body;
    EXPECT_FALSE(upload()) << body;
  }
  responseBody = ack();
  responseBody.replace(responseBody.find(HASH), 32, 32, 'f');
  EXPECT_FALSE(upload());
  responseBody = ack();
  responseBody.replace(responseBody.find("\"unmatched\":[]"), 14, "\"unmatched\":null");
  EXPECT_FALSE(upload());
}

TEST_F(BookOrbitAnnotationsClientTest, ExplicitUnmatchedIsRecognizedAndReset) {
  responseBody = std::string("{\"results\":[],\"unmatched\":[\"") + HASH + "\"]}";
  EXPECT_FALSE(upload());
  EXPECT_TRUE(BookOrbitAnnotationsClient::lastUploadWasUnmatched());
  EXPECT_FALSE(upload(nullptr));
  EXPECT_FALSE(BookOrbitAnnotationsClient::lastUploadWasUnmatched());
  responseBody = "{\"results\":[],\"unmatched\":[\"ffffffffffffffffffffffffffffffff\"]}";
  EXPECT_FALSE(upload());
  EXPECT_FALSE(BookOrbitAnnotationsClient::lastUploadWasUnmatched());
}

TEST_F(BookOrbitAnnotationsClientTest, RejectsTransportOversizeAndExcessNesting) {
  transportFails = true;
  EXPECT_FALSE(upload());
  transportFails = false;
  responseBody = ack() + std::string(2048, ' ');
  EXPECT_FALSE(upload());
  responseBody = ack();
  responseBody.insert(1, "\"extra\":[[[[[[[]]]]]]],");
  EXPECT_FALSE(upload());
}

TEST_F(BookOrbitAnnotationsClientTest, RejectsInvalidDatesAndHashesWithoutPosting) {
  for (const auto* date : {"", "2026-02-29 00:00:00", "2026-04-31 00:00:00", "2026-10-04T10:20:30",
                           "2026-10-04 24:20:30", "0000-01-01 00:00:00", "2026-10-04 10:60:00"}) {
    EXPECT_FALSE(upload(HASH, date));
    EXPECT_FALSE(upload(HASH, DATE, date));
  }
  EXPECT_FALSE(upload(nullptr));
  EXPECT_FALSE(upload(""));
  EXPECT_FALSE(upload("0123456789ABCDEF0123456789abcdef"));
  EXPECT_EQ(postCount, 0);
  EXPECT_TRUE(upload(HASH, "2024-02-29 00:00:00"));
}

TEST_F(BookOrbitAnnotationsClientTest, RejectsNullEmptyAndOverflowInputsWithoutPosting) {
  for (const auto* bad : {static_cast<const char*>(nullptr), "", "page:3"}) {
    EXPECT_FALSE(upload(HASH, DATE, DATE, bad));
    EXPECT_FALSE(upload(HASH, DATE, DATE, POS0, bad));
  }
  EXPECT_FALSE(upload(HASH, DATE, DATE, POS0, POS1, ""));
  EXPECT_FALSE(upload(HASH, DATE, DATE, POS0, POS1, nullptr));
  EXPECT_FALSE(upload(HASH, DATE, DATE, POS0, POS1, "hello", nullptr));
  EXPECT_FALSE(upload(HASH, DATE, DATE, std::string(4001, '/').c_str()));
  EXPECT_FALSE(upload(HASH, DATE, DATE, POS0, POS1, std::string(2049, 'x').c_str()));
  EXPECT_FALSE(upload(HASH, DATE, DATE, POS0, POS1, "hello", std::string(129, 'x').c_str()));
  deviceIdentity.clear();
  EXPECT_FALSE(upload());
  EXPECT_EQ(postCount, 0);
}

TEST_F(BookOrbitAnnotationsClientTest, AcceptsBoundedEscapedTextAndNote) {
  const std::string text(2048, '\n'), note(128, '"');
  ASSERT_TRUE(upload(HASH, DATE, DATE, POS0, POS1, text.c_str(), note.c_str()));
  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, requestBody));
  EXPECT_EQ(doc["books"][0]["annotations"][0]["text"].as<std::string>(), text);
  EXPECT_EQ(doc["books"][0]["annotations"][0]["note"].as<std::string>(), note);
}

TEST_F(BookOrbitAnnotationsClientTest, RejectsTrailingDataAndMultipleResults) {
  for (const auto& suffix : {std::string("x"), std::string("{}"), std::string(1, '\0')}) {
    responseBody = ack() + suffix;
    EXPECT_FALSE(upload());
  }
  responseBody = ack() + " \t\r\n";
  EXPECT_TRUE(upload());
  JsonDocument response;
  ASSERT_FALSE(deserializeJson(response, ack()));
  auto duplicate = response["results"].as<JsonArray>().add<JsonObject>();
  duplicate["hash"] = HASH;
  duplicate["upserted"] = 0;
  responseBody.clear();
  serializeJson(response, responseBody);
  EXPECT_FALSE(upload());
}

TEST_F(BookOrbitAnnotationsClientTest, AcceptsPositionLimitAndRejectsEitherOverflow) {
  std::string position = "/body/DocFragment[1]/body/";
  position += std::string(4000 - position.size() - strlen("/text()[1].0"), 'a');
  position += "/text()[1].0";
  EXPECT_TRUE(upload(HASH, DATE, DATE, position.c_str(), position.c_str()));
  position += "0";
  EXPECT_FALSE(upload(HASH, DATE, DATE, position.c_str()));
  EXPECT_FALSE(upload(HASH, DATE, DATE, POS0, position.c_str()));
}
}  // namespace
