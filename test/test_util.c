/**
 * @file test_util.c
 * @brief Shared test helpers: check counter, runner and string builders.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dns_table.h"
#include "test_util.h"

static uint32_t g_checks = 0U;
static uint32_t g_failures = 0U;

void check(bool cond, const char *expr, const char *file, int line)
{
    g_checks++;
    if (!cond)
    {
        g_failures++;
        (void)printf("    FAIL %s:%d: %s\n", file, line, expr);
    }
}

/* ---------------------------- helpers ---------------------------- */
void make_name(char *buf, size_t n, uint32_t i)
{
    (void)snprintf(buf, n, "host%u.example.com", (unsigned int)i);
}

void build_long_domain(char *buf, size_t last_len)
{
    size_t   pos = 0U;
    uint32_t l;

    for (l = 0U; l < 3U; l++)
    {
        (void)memset(&buf[pos], 'a', 63U);
        pos += 63U;
        buf[pos] = '.';
        pos++;
    }
    (void)memset(&buf[pos], 'b', last_len);
    pos += last_len;
    buf[pos] = '\0';
}

bool is_sorted_list(const dns_record_t **a, size_t n)
{
    bool   ok = true;
    size_t i;
    for (i = 1U; (i < n) && ok; i++)
    {
        if (strcmp(a[i - 1U]->domain, a[i]->domain) > 0)
        {
            ok = false;
        }
    }
    return ok;
}

bool resolves_to(const dns_table_t *t, const char *domain, const char *ip)
{
    char buf[DNS_IP_BUF_LEN];
    return (dns_resolve(t, domain, buf, sizeof(buf)) == DNS_OK) &&
           (strcmp(buf, ip) == 0);
}

void run_test(const test_case_t *tc)
{
    const uint32_t fail_before = g_failures;
    const size_t   allocs_before = dns_live_allocations();

    tc->fn();
    CHECK(dns_live_allocations() == allocs_before);              /* no leaks per test */
    (void)printf("[%s] %s\n", (g_failures == fail_before) ? "PASS" : "FAIL", tc->name);
}

int test_summary(void)
{
    (void)printf("\n%u checks, %u failures\n", (unsigned int)g_checks, (unsigned int)g_failures);
    return (g_failures == 0U) ? 0 : 1;
}
