/**
 * @file test_dns_unit.c
 * @brief Unit tests: each function of the DNS table tested on its own.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "dns_table.h"
#include "test_util.h"

/* =========================== UNIT TESTS =========================== */
static void test_create_destroy(void)
{
    dns_table_t *t = dns_table_create();
    CHECK(t != NULL);
    CHECK(dns_count(t) == 0U);
    CHECK(dns_bucket_count(t) == DNS_INITIAL_BUCKETS);
    dns_table_destroy(t);
    dns_table_destroy(NULL);               /* must be safe */
    CHECK(dns_count(NULL) == 0U);
}

static void test_insert(void)
{
    dns_table_t *t = dns_table_create();
    CHECK(dns_add(t, "example.com", "93.184.216.34") == DNS_OK);
    CHECK(dns_count(t) == 1U);
    CHECK(dns_add(t, "localhost", "127.0.0.1") == DNS_OK);
    CHECK(dns_count(t) == 2U);
    CHECK(dns_add(NULL, "a.com", "1.1.1.1") == DNS_ERR_NULL);
    dns_table_destroy(t);
}

static void test_search_resolution(void)
{
    char buf[DNS_IP_BUF_LEN];
    char tiny[4];
    dns_table_t *t = dns_table_create();

    CHECK(dns_add(t, "Example.COM", "10.0.0.1") == DNS_OK);
    CHECK(dns_resolve(t, "example.com", buf, sizeof(buf)) == DNS_OK);
    CHECK(strcmp(buf, "10.0.0.1") == 0);
    CHECK(resolves_to(t, "EXAMPLE.com", "10.0.0.1"));           /* case-insensitive */
    CHECK(dns_resolve(t, "missing.com", buf, sizeof(buf)) == DNS_ERR_NOT_FOUND);
    CHECK(dns_resolve(t, "bad..com", buf, sizeof(buf)) == DNS_ERR_INVALID_DOMAIN);
    CHECK(dns_resolve(t, NULL, buf, sizeof(buf)) == DNS_ERR_INVALID_DOMAIN);
    CHECK(dns_resolve(t, "example.com", NULL, 16U) == DNS_ERR_NULL);
    CHECK(dns_resolve(NULL, "example.com", buf, sizeof(buf)) == DNS_ERR_NULL);
    CHECK(dns_resolve(t, "example.com", tiny, sizeof(tiny)) == DNS_ERR_BUFFER);
    dns_table_destroy(t);
}

static void test_ip_update(void)
{
    dns_table_t *t = dns_table_create();
    CHECK(dns_add(t, "site.org", "1.1.1.1") == DNS_OK);
    CHECK(dns_update(t, "site.org", "2.2.2.2") == DNS_OK);
    CHECK(resolves_to(t, "site.org", "2.2.2.2"));
    CHECK(dns_count(t) == 1U);                                   /* no new record */
    CHECK(dns_update(t, "nope.org", "3.3.3.3") == DNS_ERR_NOT_FOUND);
    CHECK(dns_update(t, "site.org", "999.1.1.1") == DNS_ERR_INVALID_IP);
    CHECK(resolves_to(t, "site.org", "2.2.2.2"));                /* unchanged on error */
    CHECK(dns_update(t, "-bad.org", "3.3.3.3") == DNS_ERR_INVALID_DOMAIN);
    CHECK(dns_update(NULL, "site.org", "3.3.3.3") == DNS_ERR_NULL);
    dns_table_destroy(t);
}

static void test_deletion(void)
{
    char buf[DNS_IP_BUF_LEN];
    dns_table_t *t = dns_table_create();

    CHECK(dns_add(t, "a.com", "1.0.0.1") == DNS_OK);
    CHECK(dns_add(t, "b.com", "1.0.0.2") == DNS_OK);
    CHECK(dns_delete(t, "a.com") == DNS_OK);
    CHECK(dns_count(t) == 1U);
    CHECK(dns_resolve(t, "a.com", buf, sizeof(buf)) == DNS_ERR_NOT_FOUND);
    CHECK(resolves_to(t, "b.com", "1.0.0.2"));
    CHECK(dns_delete(t, "a.com") == DNS_ERR_NOT_FOUND);          /* second delete */
    CHECK(dns_delete(t, "a b.com") == DNS_ERR_INVALID_DOMAIN);
    CHECK(dns_delete(NULL, "b.com") == DNS_ERR_NULL);
    CHECK(dns_add(t, "a.com", "9.9.9.9") == DNS_OK);             /* re-add works */
    CHECK(resolves_to(t, "a.com", "9.9.9.9"));
    dns_table_destroy(t);
}

static void test_duplicate_handling(void)
{
    dns_table_t *t = dns_table_create();
    CHECK(dns_add(t, "dup.com", "1.2.3.4") == DNS_OK);
    CHECK(dns_add(t, "dup.com", "5.6.7.8") == DNS_ERR_DUPLICATE);
    CHECK(dns_add(t, "DUP.COM", "5.6.7.8") == DNS_ERR_DUPLICATE);
    CHECK(dns_count(t) == 1U);
    CHECK(resolves_to(t, "dup.com", "1.2.3.4"));                 /* original kept */
    dns_table_destroy(t);
}

static void test_invalid_inputs(void)
{
    static char big[400];
    dns_table_t *t = dns_table_create();

    /* invalid domains */
    CHECK(dns_add(t, NULL, "1.1.1.1") == DNS_ERR_INVALID_DOMAIN);
    CHECK(dns_add(t, "", "1.1.1.1") == DNS_ERR_INVALID_DOMAIN);
    CHECK(dns_add(t, ".com", "1.1.1.1") == DNS_ERR_INVALID_DOMAIN);
    CHECK(dns_add(t, "a..com", "1.1.1.1") == DNS_ERR_INVALID_DOMAIN);
    CHECK(dns_add(t, "a.com.", "1.1.1.1") == DNS_ERR_INVALID_DOMAIN);
    CHECK(dns_add(t, "-a.com", "1.1.1.1") == DNS_ERR_INVALID_DOMAIN);
    CHECK(dns_add(t, "a-.com", "1.1.1.1") == DNS_ERR_INVALID_DOMAIN);
    CHECK(dns_add(t, "a.com-", "1.1.1.1") == DNS_ERR_INVALID_DOMAIN);
    CHECK(dns_add(t, "exa mple.com", "1.1.1.1") == DNS_ERR_INVALID_DOMAIN);
    CHECK(dns_add(t, "exa_mple.com", "1.1.1.1") == DNS_ERR_INVALID_DOMAIN);

    (void)memset(big, 'a', 64U);                                 /* 64-char label */
    (void)strcpy(&big[64], ".com");
    CHECK(dns_add(t, big, "1.1.1.1") == DNS_ERR_INVALID_DOMAIN);

    build_long_domain(big, 61U);                                 /* 253 chars: valid */
    CHECK(strlen(big) == 253U);
    CHECK(dns_add(t, big, "1.1.1.1") == DNS_OK);
    build_long_domain(big, 62U);                                 /* 254 chars: invalid */
    CHECK(dns_add(t, big, "1.1.1.1") == DNS_ERR_INVALID_DOMAIN);

    /* invalid IPs */
    CHECK(dns_add(t, "ok.com", NULL) == DNS_ERR_INVALID_IP);
    CHECK(dns_add(t, "ok.com", "") == DNS_ERR_INVALID_IP);
    CHECK(dns_add(t, "ok.com", "256.1.1.1") == DNS_ERR_INVALID_IP);
    CHECK(dns_add(t, "ok.com", "1.2.3") == DNS_ERR_INVALID_IP);
    CHECK(dns_add(t, "ok.com", "1.2.3.4.5") == DNS_ERR_INVALID_IP);
    CHECK(dns_add(t, "ok.com", "1.2.3.") == DNS_ERR_INVALID_IP);
    CHECK(dns_add(t, "ok.com", "1..3.4") == DNS_ERR_INVALID_IP);
    CHECK(dns_add(t, "ok.com", "a.b.c.d") == DNS_ERR_INVALID_IP);
    CHECK(dns_add(t, "ok.com", "01.2.3.4") == DNS_ERR_INVALID_IP);
    CHECK(dns_add(t, "ok.com", "1.2.3.1234") == DNS_ERR_INVALID_IP);
    CHECK(dns_add(t, "ok.com", " 1.2.3.4") == DNS_ERR_INVALID_IP);
    CHECK(dns_add(t, "ok.com", "0.0.0.0") == DNS_OK);
    CHECK(dns_add(t, "ok2.com", "255.255.255.255") == DNS_OK);

    CHECK(dns_count(t) == 3U);                                   /* only valid ones stored */
    dns_table_destroy(t);
}

static void test_nonexistent_domain(void)
{
    char buf[DNS_IP_BUF_LEN];
    dns_table_t *t = dns_table_create();
    CHECK(dns_resolve(t, "ghost.net", buf, sizeof(buf)) == DNS_ERR_NOT_FOUND);
    CHECK(dns_update(t, "ghost.net", "1.1.1.1") == DNS_ERR_NOT_FOUND);
    CHECK(dns_delete(t, "ghost.net") == DNS_ERR_NOT_FOUND);
    CHECK(dns_count(t) == 0U);
    dns_table_destroy(t);
}

static void test_memory_cleanup(void)
{
    const size_t base = dns_live_allocations();
    char name[64];
    uint32_t i;
    dns_table_t *t = dns_table_create();

    CHECK(dns_live_allocations() == (base + 2U));                /* table + bucket array */
    for (i = 0U; i < 500U; i++)
    {
        make_name(name, sizeof(name), i);
        CHECK(dns_add(t, name, "1.1.1.1") == DNS_OK);
    }
    for (i = 0U; i < 500U; i++)
    {
        make_name(name, sizeof(name), i);
        CHECK(dns_delete(t, name) == DNS_OK);
    }
    CHECK(dns_live_allocations() == (base + 2U));                /* nodes all freed */
    CHECK(dns_add(t, "x.com", "1.1.1.1") == DNS_OK);
    dns_table_destroy(t);
    CHECK(dns_live_allocations() == base);                       /* everything freed */
}

static void test_resize_growth(void)
{
    char name[64];
    uint32_t i;
    dns_table_t *t = dns_table_create();

    for (i = 0U; i < 1000U; i++)
    {
        make_name(name, sizeof(name), i);
        CHECK(dns_add(t, name, "10.1.1.1") == DNS_OK);
    }
    CHECK(dns_count(t) == 1000U);
    CHECK(dns_bucket_count(t) > DNS_INITIAL_BUCKETS);
    CHECK((dns_count(t) * 4U) <= (dns_bucket_count(t) * 3U));    /* load factor <= .75 */
    for (i = 0U; i < 1000U; i++)
    {
        make_name(name, sizeof(name), i);
        CHECK(resolves_to(t, name, "10.1.1.1"));
    }
    dns_table_destroy(t);
}

static void test_sorted_buffer_errors(void)
{
    const dns_record_t *list[4];
    size_t written = 99U;
    dns_table_t *t = dns_table_create();

    CHECK(dns_get_sorted(t, list, 4U, &written) == DNS_OK);      /* empty table */
    CHECK(written == 0U);
    CHECK(dns_add(t, "b.com", "1.1.1.1") == DNS_OK);
    CHECK(dns_add(t, "a.com", "1.1.1.2") == DNS_OK);
    CHECK(dns_get_sorted(t, list, 1U, &written) == DNS_ERR_BUFFER);
    CHECK(dns_get_sorted(NULL, list, 4U, &written) == DNS_ERR_NULL);
    CHECK(dns_get_sorted(t, NULL, 4U, &written) == DNS_ERR_NULL);
    CHECK(dns_get_sorted(t, list, 4U, NULL) == DNS_ERR_NULL);
    dns_table_destroy(t);
}

static void test_status_strings(void)
{
    int s;
    for (s = (int)DNS_OK; s <= (int)DNS_ERR_BUFFER; s++)
    {
        CHECK(dns_status_string((dns_status_t)s) != NULL);
        CHECK(strcmp(dns_status_string((dns_status_t)s), "Unknown error") != 0);
    }
}

static void test_timestamps(void)
{
    const dns_record_t *records[1];
    size_t written = 0U;
    dns_table_t *t = dns_table_create();

    CHECK(dns_add_at(t, "time.test", "192.0.2.1", (time_t)100U,
                     (time_t)150U) == DNS_OK);
    CHECK(dns_get_sorted(t, records, 1U, &written) == DNS_OK);
    CHECK(written == 1U);
    CHECK(records[0]->created_at == (time_t)100U);
    CHECK(records[0]->updated_at == (time_t)150U);
    CHECK(dns_update_at(t, "time.test", "192.0.2.2", (time_t)200U) == DNS_OK);
    CHECK(records[0]->updated_at == (time_t)200U);
    dns_table_destroy(t);
}

/* ============================ RUNNER ============================== */
int main(void)
{
    static const test_case_t unit_tests[] = {
        { "unit: create/destroy", test_create_destroy },
        { "unit: record insertion", test_insert },
        { "unit: domain search/resolution", test_search_resolution },
        { "unit: IP address update", test_ip_update },
        { "unit: record deletion", test_deletion },
        { "unit: duplicate domain handling", test_duplicate_handling },
        { "unit: invalid input handling", test_invalid_inputs },
        { "unit: non-existent domain handling", test_nonexistent_domain },
        { "unit: memory cleanup", test_memory_cleanup },
        { "unit: automatic resize / load factor", test_resize_growth },
        { "unit: sorted listing error paths", test_sorted_buffer_errors },
        { "unit: status strings", test_status_strings },
        { "unit: record timestamps", test_timestamps }
    };
    size_t i;

    for (i = 0U; i < (sizeof(unit_tests) / sizeof(unit_tests[0])); i++)
    {
        run_test(&unit_tests[i]);
    }
    return test_summary();
}
