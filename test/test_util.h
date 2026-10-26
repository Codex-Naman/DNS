/**
 * @file test_util.h
 * @brief Minimal shared test helpers (no external framework).
 */
#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dns_table.h"

typedef struct
{
    const char *name;
    void (*fn)(void);
} test_case_t;

void check(bool cond, const char *expr, const char *file, int line);
#define CHECK(c) check((c), #c, __FILE__, __LINE__)

void make_name(char *buf, size_t n, uint32_t i);
void build_long_domain(char *buf, size_t last_len);
bool is_sorted_list(const dns_record_t **a, size_t n);
bool resolves_to(const dns_table_t *t, const char *domain, const char *ip);

/** Run one test and verify it left no heap blocks allocated. */
void run_test(const test_case_t *tc);
/** Print totals; returns 0 when every check passed, otherwise 1. */
int  test_summary(void);

#endif /* TEST_UTIL_H */
