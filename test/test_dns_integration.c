/**
 * @file test_dns_integration.c
 * @brief Integration tests: several operations combined in realistic sequences.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dns_table.h"
#include "test_util.h"

/* ======================= INTEGRATION TESTS ======================== */
static void test_int_insert_resolve(void)
{
    dns_table_t *t = dns_table_create();
    CHECK(dns_add(t, "openai.com", "104.18.1.1") == DNS_OK);
    CHECK(resolves_to(t, "openai.com", "104.18.1.1"));
    dns_table_destroy(t);
}

static void test_int_insert_update_verify(void)
{
    dns_table_t *t = dns_table_create();
    CHECK(dns_add(t, "svc.local", "192.168.0.10") == DNS_OK);
    CHECK(resolves_to(t, "svc.local", "192.168.0.10"));
    CHECK(dns_update(t, "svc.local", "192.168.0.99") == DNS_OK);
    CHECK(resolves_to(t, "svc.local", "192.168.0.99"));
    CHECK(dns_count(t) == 1U);
    dns_table_destroy(t);
}

static void test_int_multiple_sorted_display(void)
{
    static const char *const names[] = {
        "zebra.com", "apple.com", "Mango.org", "banana.net", "apple.co",
        "cherry.io", "a.com", "10.example.com", "zz.com", "middle.com"
    };
    const size_t n = sizeof(names) / sizeof(names[0]);
    const dns_record_t *list[16];
    size_t written = 0U;
    size_t i;
    dns_table_t *t = dns_table_create();

    for (i = 0U; i < n; i++)
    {
        CHECK(dns_add(t, names[i], "1.1.1.1") == DNS_OK);
    }
    CHECK(dns_count(t) == n);
    CHECK(dns_get_sorted(t, list, 16U, &written) == DNS_OK);
    CHECK(written == n);
    CHECK(is_sorted_list(list, written));
    CHECK(strcmp(list[0]->domain, "10.example.com") == 0);
    CHECK(strcmp(list[written - 1U]->domain, "zz.com") == 0);
    dns_table_destroy(t);
}

static void test_int_insert_delete_unavailable(void)
{
    char buf[DNS_IP_BUF_LEN];
    dns_table_t *t = dns_table_create();
    CHECK(dns_add(t, "temp.com", "8.8.8.8") == DNS_OK);
    CHECK(dns_delete(t, "temp.com") == DNS_OK);
    CHECK(dns_resolve(t, "temp.com", buf, sizeof(buf)) == DNS_ERR_NOT_FOUND);
    CHECK(dns_count(t) == 0U);
    dns_table_destroy(t);
}

static void test_int_consecutive_operations(void)
{
    const uint32_t N = 2000U;
    const dns_record_t **list;
    static const dns_record_t *storage[2000];
    char name[64];
    char ip[DNS_IP_BUF_LEN];
    size_t written = 0U;
    uint32_t i;
    dns_table_t *t = dns_table_create();

    list = storage;

    for (i = 0U; i < N; i++)                                    /* insert all */
    {
        make_name(name, sizeof(name), i);
        CHECK(dns_add(t, name, "10.0.0.1") == DNS_OK);
    }
    CHECK(dns_count(t) == N);

    for (i = 0U; i < N; i += 2U)                                /* delete evens */
    {
        make_name(name, sizeof(name), i);
        CHECK(dns_delete(t, name) == DNS_OK);
    }
    CHECK(dns_count(t) == (N / 2U));

    for (i = 1U; i < N; i += 2U)                                /* update odds */
    {
        make_name(name, sizeof(name), i);
        (void)snprintf(ip, sizeof(ip), "172.16.%u.%u",
                       (unsigned int)(i / 256U), (unsigned int)(i % 256U));
        CHECK(dns_update(t, name, ip) == DNS_OK);
    }

    for (i = 0U; i < N; i++)                                    /* verify state */
    {
        make_name(name, sizeof(name), i);
        if ((i % 2U) == 0U)
        {
            CHECK(dns_resolve(t, name, ip, sizeof(ip)) == DNS_ERR_NOT_FOUND);
        }
        else
        {
            char expect[DNS_IP_BUF_LEN];
            (void)snprintf(expect, sizeof(expect), "172.16.%u.%u",
                           (unsigned int)(i / 256U), (unsigned int)(i % 256U));
            CHECK(resolves_to(t, name, expect));
        }
    }

    for (i = 0U; i < N; i += 2U)                                /* re-add evens */
    {
        make_name(name, sizeof(name), i);
        CHECK(dns_add(t, name, "10.9.9.9") == DNS_OK);
    }
    CHECK(dns_count(t) == N);
    CHECK(dns_get_sorted(t, list, N, &written) == DNS_OK);
    CHECK(written == N);
    CHECK(is_sorted_list(list, written));

    dns_table_destroy(t);
}

/* ============================ RUNNER ============================== */
int main(void)
{
    static const test_case_t integration_tests[] = {
        { "integration: insert -> resolve", test_int_insert_resolve },
        { "integration: insert -> update -> verify", test_int_insert_update_verify },
        { "integration: multiple inserts -> sorted list", test_int_multiple_sorted_display },
        { "integration: insert -> delete -> unavailable", test_int_insert_delete_unavailable },
        { "integration: 2000 consecutive operations", test_int_consecutive_operations }
    };
    size_t i;

    for (i = 0U; i < (sizeof(integration_tests) / sizeof(integration_tests[0])); i++)
    {
        run_test(&integration_tests[i]);
    }
    return test_summary();
}
