/**
 * @file dns_table.h
 * @brief DNS record store backed by a separately-chained hash table.
 *
 * Domain names are case-insensitive (stored lower-case). IPv4 addresses are
 * stored in dotted-quad text form. The table grows automatically so that the
 * load factor stays at or below 0.75, giving O(1) average add/resolve/
 * update/delete.
 *
 * Not thread-safe.
 */
#ifndef DNS_TABLE_H
#define DNS_TABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define DNS_MAX_DOMAIN_LEN   (253U)   /* RFC 1035 limit (without NUL)   */
#define DNS_MAX_LABEL_LEN    (63U)    /* RFC 1035 limit per label       */
#define DNS_IP_BUF_LEN       (16U)    /* "255.255.255.255" + NUL        */
#define DNS_INITIAL_BUCKETS  (16U)    /* must be a power of two         */

typedef enum
{
    DNS_OK = 0,
    DNS_ERR_NULL,            /* NULL table / output pointer             */
    DNS_ERR_INVALID_DOMAIN,  /* malformed domain name                   */
    DNS_ERR_INVALID_IP,      /* malformed IPv4 address                  */
    DNS_ERR_DUPLICATE,       /* domain already present                  */
    DNS_ERR_NOT_FOUND,       /* domain not present                      */
    DNS_ERR_NOMEM,           /* allocation failure                      */
    DNS_ERR_BUFFER           /* caller buffer too small                 */
} dns_status_t;

typedef struct dns_record
{
    char domain[DNS_MAX_DOMAIN_LEN + 1U];
    char ip[DNS_IP_BUF_LEN];
    time_t created_at;              /* UTC Unix timestamp */
    time_t updated_at;              /* UTC Unix timestamp */
    struct dns_record *next;
} dns_record_t;

/* Opaque table handle. */
typedef struct dns_table dns_table_t;

/* Lifecycle */
dns_table_t *dns_table_create(void);
void         dns_table_destroy(dns_table_t *table);

/* Core operations */
dns_status_t dns_add(dns_table_t *table, const char *domain, const char *ip);
dns_status_t dns_add_at(dns_table_t *table, const char *domain, const char *ip,
                        time_t created_at, time_t updated_at);
dns_status_t dns_resolve(const dns_table_t *table, const char *domain,
                         char *ip_out, size_t ip_out_len);
dns_status_t dns_update(dns_table_t *table, const char *domain, const char *ip);
dns_status_t dns_update_at(dns_table_t *table, const char *domain, const char *ip,
                           time_t updated_at);
dns_status_t dns_delete(dns_table_t *table, const char *domain);

/* Queries */
size_t       dns_count(const dns_table_t *table);
size_t       dns_bucket_count(const dns_table_t *table);

/**
 * Fill @p out with pointers to every record, sorted alphabetically by domain.
 * @p capacity must be >= dns_count(); otherwise DNS_ERR_BUFFER is returned.
 * Pointers stay valid until the table is modified or destroyed.
 */
dns_status_t dns_get_sorted(const dns_table_t *table, const dns_record_t **out,
                            size_t capacity, size_t *written);

/* Validation helpers */
bool         dns_is_valid_domain(const char *domain);
bool         dns_is_valid_ip(const char *ip);

/* Diagnostics */
const char  *dns_status_string(dns_status_t status);
size_t       dns_live_allocations(void);  /* outstanding heap blocks (leak check) */

#endif /* DNS_TABLE_H */
