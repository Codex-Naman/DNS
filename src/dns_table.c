/**
 * @file dns_table.c
 * @brief Implementation of the DNS hash table (see dns_table.h).
 *
 * MISRA C:2012 notes: single exit point per function, no recursion, no goto,
 * fixed-width types for hashing, bounded string handling (no strcpy/strcat).
 * Documented deviation: Rule 21.3 (dynamic memory) - required by the project
 * specification; all heap use is confined to dns_calloc()/dns_free() below.
 */
#include "dns_table.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DNS_MAX_BUCKETS  ((size_t)1U << 24U)

struct dns_table
{
    dns_record_t **buckets;
    size_t         bucket_count;   /* always a power of two */
    size_t         count;
};

/* ------------------------------------------------------------------ */
/* Allocation wrappers (single place for heap use, with leak counter)  */
/* ------------------------------------------------------------------ */
static size_t g_live_allocations = 0U;

static void *dns_calloc(size_t n, size_t size)
{
    void *p = calloc(n, size);
    if (p != NULL)
    {
        g_live_allocations++;
    }
    return p;
}

static void dns_free(void *p)
{
    if (p != NULL)
    {
        free(p);
        g_live_allocations--;
    }
}

// cppcheck-suppress unusedFunction
size_t dns_live_allocations(void)
{
    return g_live_allocations;
}

/* ------------------------------------------------------------------ */
/* Character / string helpers                                          */
/* ------------------------------------------------------------------ */
static bool is_alnum_char(char c)
{
    return ((c >= 'a') && (c <= 'z')) ||
           ((c >= 'A') && (c <= 'Z')) ||
           ((c >= '0') && (c <= '9'));
}

static char to_lower_char(char c)
{
    char r = c;
    if ((c >= 'A') && (c <= 'Z'))
    {
        r = (char)(c + ('a' - 'A'));
    }
    return r;
}

static void copy_str(char *dst, size_t dst_size, const char *src)
{
    if ((dst != NULL) && (src != NULL) && (dst_size > 0U))
    {
        size_t i = 0u;
        while ((i < (dst_size - 1U)) && (src[i] != '\0'))
        {
            dst[i] = src[i];
            i++;
        }
        dst[i] = '\0';
    }
}

static time_t current_timestamp(void)
{
    const time_t now = time(NULL);
    return (now == (time_t)-1) ? (time_t)0 : now;
}


/* ------------------------------------------------------------------ */
/* Validation                                                          */
/* ------------------------------------------------------------------ */
bool dns_is_valid_domain(const char *domain)
{
    bool ok = (domain != NULL);

    if (ok)
    {
        size_t i = 0U;
        size_t label_len = 0U;
        char prev = '.';

        while (ok && (domain[i] != '\0'))
        {
            const char c = domain[i];

            if (i >= DNS_MAX_DOMAIN_LEN)
            {
                ok = false;
            }
            else if (c == '.')
            {
                /* empty label (leading dot, "..") or label ending in '-' */
                if ((label_len == 0U) || (prev == '-'))
                {
                    ok = false;
                }
                label_len = 0U;
            }
            else if (c == '-')
            {
                if (label_len == 0U)   /* label may not start with '-' */
                {
                    ok = false;
                }
                label_len++;
            }
            else if (is_alnum_char(c))
            {
                label_len++;
            }
            else
            {
                ok = false;
            }

            if (label_len > DNS_MAX_LABEL_LEN)
            {
                ok = false;
            }
            prev = c;
            i++;
        }

        /* empty string, trailing dot, or trailing '-' */
        if (ok && ((i == 0U) || (label_len == 0U) || (prev == '-')))
        {
            ok = false;
        }
    }
    return ok;
}

bool dns_is_valid_ip(const char *ip)
{
    bool ok = (ip != NULL);

    if (ok)
    {
        size_t i = 0U;
        uint32_t value = 0U;
        uint32_t digits = 0U;
        uint32_t dots = 0U;

        while (ok && (ip[i] != '\0'))
        {
            const char c = ip[i];

            if ((c >= '0') && (c <= '9'))
            {
                if ((digits == 1U) && (value == 0U))
                {
                    ok = false;            /* leading zero, e.g. "01" */
                }
                value = (value * 10U) + (uint32_t)(c - '0');
                digits++;
                if ((digits > 3U) || (value > 255U))
                {
                    ok = false;
                }
            }
            else if (c == '.')
            {
                if (digits == 0U)
                {
                    ok = false;            /* empty octet */
                }
                dots++;
                if (dots > 3U)
                {
                    ok = false;
                }
                value = 0U;
                digits = 0U;
            }
            else
            {
                ok = false;
            }
            i++;
        }

        if (ok && ((digits == 0U) || (dots != 3U)))
        {
            ok = false;
        }
    }
    return ok;
}

/* Validate and convert to canonical lower-case key. key >= 254 bytes. */
static bool make_key(const char *domain, char *key)
{
    bool ok = dns_is_valid_domain(domain);

    if (ok)
    {
        size_t i = 0U;

        while (domain[i] != '\0')
        {
            key[i] = to_lower_char(domain[i]);
            i++;
        }
        key[i] = '\0';
    }
    return ok;
}

/* ------------------------------------------------------------------ */
/* Hashing / lookup                                                    */
/* ------------------------------------------------------------------ */
static uint32_t hash_key(const char *key)   /* FNV-1a, 32 bit */
{
    uint32_t h = 2166136261U;

    for (size_t i = 0U; key[i] != '\0'; i++)
    {
        h ^= (uint32_t)(unsigned char)key[i];
        h *= 16777619U;
    }
    return h;
}

static size_t bucket_index(size_t bucket_count, const char *key)
{
    return (size_t)hash_key(key) & (bucket_count - 1U);
}

static dns_record_t *find_node(const dns_table_t *table, const char *key)
{
    dns_record_t *node = table->buckets[bucket_index(table->bucket_count, key)];

    while ((node != NULL) && (strcmp(node->domain, key) != 0))
    {
        node = node->next;
    }
    return node;
}

/* Double the bucket array when load factor would exceed 0.75. Failure to
 * grow is harmless: the table keeps working with longer chains. */
static void grow_if_needed(dns_table_t *table)
{
    const bool need = ((table->count + 1U) * 4U) > (table->bucket_count * 3U);

    if (need && (table->bucket_count < DNS_MAX_BUCKETS))
    {
        const size_t   new_count = table->bucket_count * 2U;
        dns_record_t **nb = (dns_record_t **)dns_calloc(new_count, sizeof(dns_record_t *));

        if (nb != NULL)
        {
            size_t b;
            for (b = 0U; b < table->bucket_count; b++)
            {
                dns_record_t *node = table->buckets[b];
                while (node != NULL)
                {
                    dns_record_t *const next = node->next;
                    const size_t idx = bucket_index(new_count, node->domain);
                    node->next = nb[idx];
                    nb[idx] = node;
                    node = next;
                }
            }
            dns_free(table->buckets);
            table->buckets = nb;
            table->bucket_count = new_count;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */
dns_table_t *dns_table_create(void)
{
    dns_table_t *table = (dns_table_t *)dns_calloc(1U, sizeof(dns_table_t));

    if (table != NULL)
    {
        table->buckets = (dns_record_t **)dns_calloc(DNS_INITIAL_BUCKETS,
                                                     sizeof(dns_record_t *));
        if (table->buckets == NULL)
        {
            dns_free(table);
            table = NULL;
        }
        else
        {
            table->bucket_count = DNS_INITIAL_BUCKETS;
            table->count = 0U;
        }
    }
    return table;
}

void dns_table_destroy(dns_table_t *table)
{
    if (table != NULL)
    {
        size_t b;
        for (b = 0U; b < table->bucket_count; b++)
        {
            dns_record_t *node = table->buckets[b];
            while (node != NULL)
            {
                dns_record_t *const next = node->next;
                dns_free(node);
                node = next;
            }
        }
        dns_free(table->buckets);
        dns_free(table);
    }
}

/* ------------------------------------------------------------------ */
/* Operations                                                          */
/* ------------------------------------------------------------------ */
dns_status_t dns_add_at(dns_table_t *table, const char *domain, const char *ip,
                        time_t created_at, time_t updated_at)
{
    dns_status_t status = DNS_OK;
    char         key[DNS_MAX_DOMAIN_LEN + 1U] = { '\0' };

    if (table == NULL)
    {
        status = DNS_ERR_NULL;
    }
    else if (!make_key(domain, key))
    {
        status = DNS_ERR_INVALID_DOMAIN;
    }
    else if (!dns_is_valid_ip(ip))
    {
        status = DNS_ERR_INVALID_IP;
    }
    else if (find_node(table, key) != NULL)
    {
        status = DNS_ERR_DUPLICATE;
    }
    else
    {
        dns_record_t *node = (dns_record_t *)dns_calloc(1U, sizeof(dns_record_t));

        if (node == NULL)
        {
            status = DNS_ERR_NOMEM;
        }
        else
        {
            size_t idx;

            grow_if_needed(table);
            idx = bucket_index(table->bucket_count, key);

            copy_str(node->domain, sizeof(node->domain), key);
            copy_str(node->ip, sizeof(node->ip), ip);
            node->created_at = (created_at == (time_t)0) ? current_timestamp() : created_at;
            node->updated_at = (updated_at == (time_t)0) ? node->created_at : updated_at;
            node->next = table->buckets[idx];
            table->buckets[idx] = node;
            table->count++;
        }
    }
    return status;
}

dns_status_t dns_add(dns_table_t *table, const char *domain, const char *ip)
{
    const time_t now = current_timestamp();
    return dns_add_at(table, domain, ip, now, now);
}

dns_status_t dns_resolve(const dns_table_t *table, const char *domain,
                         char *ip_out, size_t ip_out_len)
{
    dns_status_t status = DNS_OK;
    char         key[DNS_MAX_DOMAIN_LEN + 1U] = { '\0' };

    if ((table == NULL) || (ip_out == NULL))
    {
        status = DNS_ERR_NULL;
    }
    else if (ip_out_len < DNS_IP_BUF_LEN)
    {
        status = DNS_ERR_BUFFER;
    }
    else if (!make_key(domain, key))
    {
        status = DNS_ERR_INVALID_DOMAIN;
    }
    else
    {
        const dns_record_t *const node = find_node(table, key);

        if (node == NULL)
        {
            status = DNS_ERR_NOT_FOUND;
        }
        else
        {
            copy_str(ip_out, ip_out_len, node->ip);
        }
    }
    return status;
}

dns_status_t dns_update_at(const dns_table_t *table, const char *domain, const char *ip,
                           time_t updated_at)
{
    dns_status_t status = DNS_OK;
    char         key[DNS_MAX_DOMAIN_LEN + 1U] = { '\0' };

    if (table == NULL)
    {
        status = DNS_ERR_NULL;
    }
    else if (!make_key(domain, key))
    {
        status = DNS_ERR_INVALID_DOMAIN;
    }
    else if (!dns_is_valid_ip(ip))
    {
        status = DNS_ERR_INVALID_IP;
    }
    else
    {
        dns_record_t *const node = find_node(table, key);

        if (node == NULL)
        {
            status = DNS_ERR_NOT_FOUND;
        }
        else
        {
            copy_str(node->ip, sizeof(node->ip), ip);
            node->updated_at = (updated_at == (time_t)0) ? current_timestamp() : updated_at;
        }
    }
    return status;
}

dns_status_t dns_update(const dns_table_t *table, const char *domain, const char *ip)
{
    return dns_update_at(table, domain, ip, current_timestamp());
}

dns_status_t dns_delete(dns_table_t *table, const char *domain)
{
    dns_status_t status = DNS_OK;
    char         key[DNS_MAX_DOMAIN_LEN + 1U] = { '\0' };

    if (table == NULL)
    {
        status = DNS_ERR_NULL;
    }
    else if (!make_key(domain, key))
    {
        status = DNS_ERR_INVALID_DOMAIN;
    }
    else
    {
        dns_record_t **link = &table->buckets[bucket_index(table->bucket_count, key)];

        while ((*link != NULL) && (strcmp((*link)->domain, key) != 0))
        {
            link = &(*link)->next;
        }

        if (*link == NULL)
        {
            status = DNS_ERR_NOT_FOUND;
        }
        else
        {
            dns_record_t *const victim = *link;
            *link = victim->next;
            dns_free(victim);
            table->count--;
        }
    }
    return status;
}

size_t dns_count(const dns_table_t *table)
{
    size_t n = 0U;
    if (table != NULL)
    {
        n = table->count;
    }
    return n;
}

size_t dns_bucket_count(const dns_table_t *table)
{
    size_t n = 0U;
    if (table != NULL)
    {
        n = table->bucket_count;
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* Sorted listing (iterative heap sort: O(n log n), no recursion)      */
/* ------------------------------------------------------------------ */
static bool rec_less(const dns_record_t *a, const dns_record_t *b)
{
    return strcmp(a->domain, b->domain) < 0;
}

static void swap_recs(const dns_record_t **a, size_t i, size_t j)
{
    const dns_record_t *const tmp = a[i];
    a[i] = a[j];
    a[j] = tmp;
}

static void sift_down(const dns_record_t **a, size_t start, size_t end)
{
    size_t root = start;
    bool   done = false;

    while (!done)
    {
        size_t child = (2U * root) + 1U;

        if (child >= end)
        {
            done = true;
        }
        else
        {
            if (((child + 1U) < end) && rec_less(a[child], a[child + 1U]))
            {
                child++;
            }
            if (rec_less(a[root], a[child]))
            {
                swap_recs(a, root, child);
                root = child;
            }
            else
            {
                done = true;
            }
        }
    }
}

static void sort_records(const dns_record_t **a, size_t n)
{
    if (n > 1U)
    {
        size_t i = n / 2U;
        while (i > 0U)
        {
            i--;
            sift_down(a, i, n);
        }
        i = n;
        while (i > 1U)
        {
            i--;
            swap_recs(a, 0U, i);
            sift_down(a, 0U, i);
        }
    }
}

dns_status_t dns_get_sorted(const dns_table_t *table, const dns_record_t **out,
                            size_t capacity, size_t *written)
{
    dns_status_t status = DNS_OK;

    if ((table == NULL) || (out == NULL) || (written == NULL))
    {
        status = DNS_ERR_NULL;
    }
    else if (capacity < table->count)
    {
        *written = 0U;
        status = DNS_ERR_BUFFER;
    }
    else
    {
        size_t n = 0U;
        size_t b;

        for (b = 0U; b < table->bucket_count; b++)
        {
            const dns_record_t *node = table->buckets[b];
            while (node != NULL)
            {
                out[n] = node;
                n++;
                node = node->next;
            }
        }
        sort_records(out, n);
        *written = n;
    }
    return status;
}

/* ------------------------------------------------------------------ */
const char *dns_status_string(dns_status_t status)
{
    const char *msg = "Unknown error";

    switch (status)
    {
        case DNS_OK:                 msg = "Success";                          break;
        case DNS_ERR_NULL:           msg = "Null argument";                    break;
        case DNS_ERR_INVALID_DOMAIN: msg = "Invalid domain name";              break;
        case DNS_ERR_INVALID_IP:     msg = "Invalid IPv4 address";             break;
        case DNS_ERR_DUPLICATE:      msg = "Domain already exists";            break;
        case DNS_ERR_NOT_FOUND:      msg = "Domain not found";                 break;
        case DNS_ERR_NOMEM:          msg = "Out of memory";                    break;
        case DNS_ERR_BUFFER:         msg = "Buffer too small";                 break;
        default:                     break;
    }
    return msg;
}
