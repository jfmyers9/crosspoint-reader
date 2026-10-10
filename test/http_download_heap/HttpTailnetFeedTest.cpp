#include <HTTPClient.h>
#include <TailscaleManager.h>
#include <gtest/gtest.h>

#include "HttpDownloader.h"

namespace {
class HttpTailnetFeedTest : public testing::Test {
 protected:
  void SetUp() override {
    tailnet_test::requests.clear();
    tailnet_test::responses.clear();
    tailnet_test::beginSucceeds = true;
    TAILSCALE.ready = true;
    options.username = "reader";
    options.password = "secret";
    options.headers = {{"X-Token", "token"}};
    options.accept = "application/opds+json";
    options.acceptLanguage = "en";
    options.statusOut = &status;
  }
  bool fetch(const std::string& url = "http://books.ts.net/feed") {
    return HttpDownloader::fetchUrl(
        url,
        [&](const uint8_t* data, size_t len) {
          body.append(reinterpret_cast<const char*>(data), len);
          return acceptBody;
        },
        options);
  }
  HttpDownloader::FetchOptions options;
  int status = -1;
  std::string body;
  bool acceptBody = true;
};

TEST_F(HttpTailnetFeedTest, OpdsOptionsUseTailnetAndPreserveHeaders) {
  ASSERT_TRUE(fetch());
  ASSERT_EQ(tailnet_test::requests.size(), 1u);
  const auto& request = tailnet_test::requests.front();
  EXPECT_EQ(request.basicAuth, "reader:secret");
  EXPECT_EQ(request.headers.at("X-Token"), "token");
  EXPECT_EQ(request.headers.at("Accept"), "application/opds+json");
  EXPECT_EQ(request.headers.at("Accept-Language"), "en");
  EXPECT_EQ(body, "feed");
  EXPECT_EQ(status, 200);
}

TEST_F(HttpTailnetFeedTest, BearerOverridesBasicAndStaysWithOrigin) {
  options.bearer = "access-token";
  tailnet_test::responses = {{302, "", "http://cdn.ts.net/feed"}, {200, "catalog", ""}};
  ASSERT_TRUE(fetch());
  ASSERT_EQ(tailnet_test::requests.size(), 2u);
  EXPECT_TRUE(tailnet_test::requests[0].basicAuth.empty());
  EXPECT_EQ(tailnet_test::requests[0].headers.at("Authorization"), "Bearer access-token");
  EXPECT_EQ(tailnet_test::requests[1].headers.count("Authorization"), 0u);
  EXPECT_EQ(tailnet_test::requests[1].headers.count("X-Token"), 0u);
  EXPECT_TRUE(tailnet_test::requests[1].basicAuth.empty());
  EXPECT_EQ(tailnet_test::requests[1].headers.at("Accept"), options.accept);
  EXPECT_TRUE(HttpDownloader::lastAnswerRedirected());
}

TEST_F(HttpTailnetFeedTest, CapturesAuthDocumentWithoutReportingSuccess) {
  options.captureErrorBody = true;
  tailnet_test::responses = {{401, "auth-document", ""}};
  EXPECT_FALSE(fetch());
  EXPECT_EQ(body, "auth-document");
  EXPECT_EQ(status, 401);
}

TEST_F(HttpTailnetFeedTest, DoesNotCaptureUnrequestedErrorBody) {
  tailnet_test::responses = {{401, "auth-document", ""}};
  EXPECT_FALSE(fetch());
  EXPECT_TRUE(body.empty());
  EXPECT_EQ(status, 401);
}

TEST_F(HttpTailnetFeedTest, SinkFailureIsNotSuccess) {
  acceptBody = false;
  EXPECT_FALSE(fetch());
  EXPECT_EQ(status, 200);
}

TEST_F(HttpTailnetFeedTest, ClearsPreviousStatusWhenNextRequestCannotStart) {
  ASSERT_TRUE(fetch());
  tailnet_test::beginSucceeds = false;
  EXPECT_FALSE(fetch());
  EXPECT_EQ(status, 0);
  EXPECT_EQ(HttpDownloader::lastStatus(), 0);
}

TEST_F(HttpTailnetFeedTest, RejectsUnsupportedMagicDnsHttpsWithoutConnecting) {
  EXPECT_FALSE(fetch("https://books.ts.net/feed"));
  EXPECT_TRUE(tailnet_test::requests.empty());
  EXPECT_EQ(status, 0);
}

TEST_F(HttpTailnetFeedTest, TailnetIpRequiresTunnelPreparation) {
  TAILSCALE.ready = false;
  EXPECT_FALSE(fetch("http://100.64.0.1/feed"));
  EXPECT_EQ(status, 0);
}
}  // namespace
