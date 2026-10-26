/**
 * @file test_dns_gtest.cpp
 * @brief GoogleTest runner for the C DNS table API.
 */
#include <gtest/gtest.h>

#include <string>

extern "C" {
#include "dns_table.h"
}

namespace {

class DnsTableTest : public ::testing::Test {
 protected:
  void SetUp() override {
    baseline_ = dns_live_allocations();
    table_ = dns_table_create();
    ASSERT_NE(table_, nullptr);
  }

  void TearDown() override {
    dns_table_destroy(table_);
    EXPECT_EQ(dns_live_allocations(), baseline_);
  }

  dns_table_t* table_ = nullptr;
  size_t baseline_ = 0U;
};

TEST(DnsValidationTest, ValidatesDomainsAndIPv4Addresses) {
  EXPECT_TRUE(dns_is_valid_domain("api.example.com"));
  EXPECT_TRUE(dns_is_valid_domain("a-1.example"));
  EXPECT_FALSE(dns_is_valid_domain("-api.example"));
  EXPECT_FALSE(dns_is_valid_domain("api..example"));
  EXPECT_TRUE(dns_is_valid_ip("0.0.0.0"));
  EXPECT_TRUE(dns_is_valid_ip("255.255.255.255"));
  EXPECT_FALSE(dns_is_valid_ip("01.2.3.4"));
  EXPECT_FALSE(dns_is_valid_ip("256.2.3.4"));
}

TEST_F(DnsTableTest, AddAndResolveAreCaseInsensitive) {
  ASSERT_EQ(dns_add(table_, "Example.COM", "192.0.2.10"), DNS_OK);
  char ip[DNS_IP_BUF_LEN] = {};
  ASSERT_EQ(dns_resolve(table_, "EXAMPLE.com", ip, sizeof(ip)), DNS_OK);
  EXPECT_STREQ(ip, "192.0.2.10");
  EXPECT_EQ(dns_count(table_), 1U);
}

TEST_F(DnsTableTest, StoresCreationAndUpdateTimestamps) {
  ASSERT_EQ(dns_add_at(table_, "time.test", "192.0.2.1", 100, 150), DNS_OK);
  const dns_record_t* records[1] = {};
  size_t written = 0U;
  ASSERT_EQ(dns_get_sorted(table_, records, 1U, &written), DNS_OK);
  ASSERT_EQ(written, 1U);
  EXPECT_EQ(records[0]->created_at, 100);
  EXPECT_EQ(records[0]->updated_at, 150);
  ASSERT_EQ(dns_update_at(table_, "time.test", "192.0.2.2", 200), DNS_OK);
  EXPECT_EQ(records[0]->updated_at, 200);
}

TEST_F(DnsTableTest, DuplicateUpdateAndDelete) {
  ASSERT_EQ(dns_add(table_, "service.test", "198.51.100.1"), DNS_OK);
  EXPECT_EQ(dns_add(table_, "SERVICE.TEST", "198.51.100.2"), DNS_ERR_DUPLICATE);
  EXPECT_EQ(dns_update(table_, "service.test", "198.51.100.2"), DNS_OK);
  EXPECT_EQ(dns_delete(table_, "SERVICE.TEST"), DNS_OK);
  EXPECT_EQ(dns_delete(table_, "service.test"), DNS_ERR_NOT_FOUND);
  EXPECT_EQ(dns_count(table_), 0U);
}

TEST_F(DnsTableTest, InvalidOperationsReturnErrors) {
  char ip[DNS_IP_BUF_LEN] = {};
  EXPECT_EQ(dns_add(nullptr, "a.test", "192.0.2.1"), DNS_ERR_NULL);
  EXPECT_EQ(dns_add(table_, "a.test", "192.0.2.999"), DNS_ERR_INVALID_IP);
  EXPECT_EQ(dns_resolve(table_, "bad..test", ip, sizeof(ip)),
            DNS_ERR_INVALID_DOMAIN);
  EXPECT_EQ(dns_resolve(table_, "a.test", ip, 4U), DNS_ERR_BUFFER);
  EXPECT_EQ(dns_update(table_, "missing.test", "192.0.2.1"), DNS_ERR_NOT_FOUND);
}

TEST_F(DnsTableTest, SortedListingAndCapacity) {
  ASSERT_EQ(dns_add(table_, "z.test", "192.0.2.3"), DNS_OK);
  ASSERT_EQ(dns_add(table_, "a.test", "192.0.2.1"), DNS_OK);
  ASSERT_EQ(dns_add(table_, "m.test", "192.0.2.2"), DNS_OK);

  const dns_record_t* records[3] = {};
  size_t written = 0U;
  EXPECT_EQ(dns_get_sorted(table_, records, 2U, &written), DNS_ERR_BUFFER);
  EXPECT_EQ(dns_get_sorted(table_, records, 3U, &written), DNS_OK);
  ASSERT_EQ(written, 3U);
  EXPECT_STREQ(records[0]->domain, "a.test");
  EXPECT_STREQ(records[1]->domain, "m.test");
  EXPECT_STREQ(records[2]->domain, "z.test");
}

TEST_F(DnsTableTest, GrowthPreservesResolution) {
  for (unsigned int i = 0U; i < 100U; ++i) {
    const std::string domain = "host" + std::to_string(i) + ".test";
    ASSERT_EQ(dns_add(table_, domain.c_str(), "203.0.113.1"), DNS_OK);
  }
  EXPECT_GT(dns_bucket_count(table_), DNS_INITIAL_BUCKETS);
  EXPECT_EQ(dns_count(table_), 100U);
  char ip[DNS_IP_BUF_LEN] = {};
  EXPECT_EQ(dns_resolve(table_, "host99.test", ip, sizeof(ip)), DNS_OK);
  EXPECT_STREQ(ip, "203.0.113.1");
}

TEST(DnsStatusTest, EveryStatusHasText) {
  for (int value = static_cast<int>(DNS_OK);
       value <= static_cast<int>(DNS_ERR_BUFFER); ++value) {
    const char* text = dns_status_string(static_cast<dns_status_t>(value));
    ASSERT_NE(text, nullptr);
    EXPECT_STRNE(text, "Unknown error");
  }
}

}  // namespace
