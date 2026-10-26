/**
 * @file main.c
 * @brief Interactive menu front-end for the DNS hash table.
 *
 * MISRA C:2012 deviations (user-interface layer only): Rule 21.6 (stdio) and
 * Rule 21.3 (malloc, used once for display/export snapshots).
 *
 * File persistence
 * ----------------
 * All records are stored in DNS_TXT_FILE ("dns_records.txt").
 * Format: one record per line -> "domain ip created_at updated_at".
 * Timestamp fields are UTC Unix seconds and are optional for compatibility.
 * Lines beginning with '#' and blank lines are ignored, so you can annotate
 * the file by hand.  Any manual edit is picked up via menu option 8 (Import).
 *
 * Export
 * ------
 * Menu option 7 writes DNS_CSV_FILE ("dns_records.csv") with a header row
 * and one domain/IP/timestamp row per record, sorted alphabetically.
 */
#include <stdbool.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dns_table.h"

#define INPUT_BUF_LEN (300U)
#define DNS_TXT_FILE "dns_records.txt"
#define DNS_CSV_FILE "dns_records.csv"

/* ------------------------------------------------------------------ */
/* Read one line, strip newline, discard any overflow.                 */
/* ------------------------------------------------------------------ */
static bool read_line(const char *prompt, char *buf, size_t size) {
  bool ok = false;

  (void)printf("%s", prompt);
  (void)fflush(stdout);

  if (fgets(buf, (int)size, stdin) != NULL) {
    size_t len = strlen(buf);

    if ((len > 0U) && (buf[len - 1U] == '\n')) {
      len--;
      buf[len] = '\0';
    } else {
      int c = getc(stdin);
      while ((c != '\n') && (c != EOF)) {
        c = getc(stdin);
      }
    }
    if ((len > 0U) && (buf[len - 1U] == '\r')) {
      buf[len - 1U] = '\0';
    }
    ok = true;
  }
  return ok;
}

static void report(dns_status_t status, const char *success_msg) {
  if (status == DNS_OK) {
    (void)printf("OK: %s\n", success_msg);
  } else {
    (void)printf("Error: %s\n", dns_status_string(status));
  }
}

/* ------------------------------------------------------------------ */
/* parse_txt_line                                                       */
/* Parse one non-empty, non-comment line from the .txt store.         */
/* Format: "domain<whitespace>ip"                                      */
/* Returns true and fills domain/ip on success.                       */
/* ------------------------------------------------------------------ */
static bool parse_txt_line(const char *line, char *domain, size_t dom_len,
                           char *ip, size_t ip_len, time_t *created_at,
                           time_t *updated_at) {
  size_t i = 0U;
  size_t di = 0U;
  size_t ii = 0U;
  bool timestamps_ok = true;

  *created_at = (time_t)0;
  *updated_at = (time_t)0;

  /* Skip leading whitespace */
  while ((line[i] == ' ') || (line[i] == '\t')) {
    i++;
  }

  /* Ignore comment lines and blank lines */
  if ((line[i] == '#') || (line[i] == '\0') || (line[i] == '\n')) {
    return false;
  }

  /* Read domain token (up to first whitespace) */
  while ((line[i] != '\0') && (line[i] != ' ') && (line[i] != '\t') &&
         (line[i] != '\n') && (di < (dom_len - 1U))) {
    domain[di] = line[i];
    di++;
    i++;
  }
  domain[di] = '\0';

  /* Skip whitespace separator */
  while ((line[i] == ' ') || (line[i] == '\t')) {
    i++;
  }

  /* Read IP token */
  while ((line[i] != '\0') && (line[i] != ' ') && (line[i] != '\t') &&
         (line[i] != '\n') && (line[i] != '\r') && (ii < (ip_len - 1U))) {
    ip[ii] = line[i];
    ii++;
    i++;
  }
  ip[ii] = '\0';

  while ((line[i] == ' ') || (line[i] == '\t')) {
    i++;
  }
  if ((line[i] != '\0') && (line[i] != '\n') && (line[i] != '\r')) {
    char *end = NULL;
    long long parsed_created;
    long long parsed_updated;

    errno = 0;
    parsed_created = strtoll(&line[i], &end, 10);
    if ((end == &line[i]) || (errno != 0) || (parsed_created <= 0LL)) {
      timestamps_ok = false;
    } else {
      *created_at = (time_t)parsed_created;
      i = (size_t)(end - line);
      while ((line[i] == ' ') || (line[i] == '\t')) {
        i++;
      }
      errno = 0;
      parsed_updated = strtoll(&line[i], &end, 10);
      if ((end == &line[i]) || (errno != 0) || (parsed_updated <= 0LL)) {
        timestamps_ok = false;
      } else {
        *updated_at = (time_t)parsed_updated;
        i = (size_t)(end - line);
      }
    }
    while ((line[i] == ' ') || (line[i] == '\t') || (line[i] == '\n') ||
           (line[i] == '\r')) {
      i++;
    }
    timestamps_ok = timestamps_ok && (line[i] == '\0');
  }

  return ((di > 0U) && (ii > 0U) && timestamps_ok);
}

/* ------------------------------------------------------------------ */
/* load_from_file                                                       */
/* Populate the hash table from DNS_TXT_FILE. Called once on startup. */
/* ------------------------------------------------------------------ */
static void load_from_file(dns_table_t *table) {
  FILE *fp = fopen(DNS_TXT_FILE, "r");

  if (fp == NULL) {
    (void)printf("[file] '%s' not found -- starting with an empty table.\n"
                 "[file] Records you add will be saved there automatically.\n",
                 DNS_TXT_FILE);
  } else {
    char line[INPUT_BUF_LEN];
    char domain[DNS_MAX_DOMAIN_LEN + 1U];
    char ip[DNS_IP_BUF_LEN];
    time_t created_at;
    time_t updated_at;
    size_t loaded = 0U;
    size_t skipped = 0U;

    while (fgets(line, (int)sizeof(line), fp) != NULL) {
      if (parse_txt_line(line, domain, sizeof(domain), ip, sizeof(ip),
                         &created_at, &updated_at)) {
        if (dns_add_at(table, domain, ip, created_at, updated_at) == DNS_OK) {
          loaded++;
        } else {
          skipped++;
        }
      }
    }
    (void)fclose(fp);

    (void)printf("[file] Loaded %lu record(s) from '%s'", (unsigned long)loaded,
                 DNS_TXT_FILE);
    if (skipped > 0U) {
      (void)printf(" (%lu skipped -- duplicate/invalid)",
                   (unsigned long)skipped);
    }
    (void)printf(".\n");
  }
}

/* ------------------------------------------------------------------ */
/* save_to_file                                                         */
/* Overwrite DNS_TXT_FILE with the current sorted table.              */
/* Called automatically after every add / update / delete.            */
/* ------------------------------------------------------------------ */
static bool save_to_file(const dns_table_t *table) {
  bool ok = false;
  const size_t n = dns_count(table);
  FILE *fp = fopen(DNS_TXT_FILE, "w");

  if (fp == NULL) {
    (void)printf("[file] ERROR: cannot write to '%s'.\n", DNS_TXT_FILE);
  } else {
    /* Human-readable header -- these lines are skipped on re-import */
    (void)fprintf(
        fp, "# DNS Records -- managed by dns_app\n"
            "# Format  : domain ip created_at updated_at (UTC Unix seconds)\n"
            "# Comments: lines starting with '#' are ignored on import\n"
            "# Tip     : edit this file by hand, then use menu option 8 to "
            "reload\n\n");

    if (n > 0U) {
      const dns_record_t **list =
          (const dns_record_t **)malloc(n * sizeof(const dns_record_t *));

      if (list == NULL) {
        (void)printf("[file] ERROR: out of memory while saving.\n");
      } else {
        size_t written = 0U;

        if (dns_get_sorted(table, list, n, &written) == DNS_OK) {
          size_t i;
          for (i = 0U; i < written; i++) {
            (void)fprintf(fp, "%s %s %lld %lld\n", list[i]->domain, list[i]->ip,
                         (long long)list[i]->created_at,
                         (long long)list[i]->updated_at);
          }
          ok = true;
        }
        free(list);
      }
    } else {
      ok = true; /* empty table -- file is still valid */
    }
    (void)fclose(fp);
  }
  return ok;
}

/* ------------------------------------------------------------------ */
/* handle_export_csv                                                    */
/* Write DNS_CSV_FILE (RFC 4180 CSV with header + sorted rows).       */
/* ------------------------------------------------------------------ */
static void handle_export_csv(const dns_table_t *table) {
  const size_t n = dns_count(table);
  FILE *fp = fopen(DNS_CSV_FILE, "w");

  if (fp == NULL) {
    (void)printf("Error: cannot open '%s' for writing.\n", DNS_CSV_FILE);
  } else {
    (void)fprintf(fp, "Domain,IP Address,Created At (UTC),Updated At (UTC)\n");

    if (n > 0U) {
      const dns_record_t **list =
          (const dns_record_t **)malloc(n * sizeof(const dns_record_t *));

      if (list == NULL) {
        (void)printf("Error: %s\n", dns_status_string(DNS_ERR_NOMEM));
      } else {
        size_t written = 0U;

        if (dns_get_sorted(table, list, n, &written) == DNS_OK) {
          size_t i;
          for (i = 0U; i < written; i++) {
            (void)fprintf(fp, "%s,%s,%lld,%lld\n", list[i]->domain, list[i]->ip,
                         (long long)list[i]->created_at,
                         (long long)list[i]->updated_at);
          }
          (void)printf("OK: %lu record(s) exported to '%s'.\n",
                       (unsigned long)written, DNS_CSV_FILE);
        }
        free(list);
      }
    } else {
      (void)printf("OK: table is empty -- header-only CSV written to '%s'.\n",
                   DNS_CSV_FILE);
    }
    (void)fclose(fp);
  }
}

/* ------------------------------------------------------------------ */
/* handle_import                                                        */
/* Merge DNS_TXT_FILE into the live table (non-destructive).          */
/*   New domain        -> dns_add()                                   */
/*   Same domain + new IP -> dns_update()  (hand-edited file)         */
/*   Same domain + same IP -> skipped silently                        */
/* Persists the merged result back to the file when done.             */
/* ------------------------------------------------------------------ */
static void handle_import(dns_table_t *table) {
  FILE *fp = fopen(DNS_TXT_FILE, "r");

  if (fp == NULL) {
    (void)printf("Error: '%s' not found. Nothing imported.\n", DNS_TXT_FILE);
  } else {
    char line[INPUT_BUF_LEN];
    char domain[DNS_MAX_DOMAIN_LEN + 1U];
    char ip[DNS_IP_BUF_LEN];
    time_t created_at;
    time_t updated_at;
    size_t added = 0U;
    size_t updated = 0U;
    size_t skipped = 0U;

    while (fgets(line, (int)sizeof(line), fp) != NULL) {
      if (parse_txt_line(line, domain, sizeof(domain), ip, sizeof(ip),
                         &created_at, &updated_at)) {
        dns_status_t st = dns_add_at(table, domain, ip, created_at, updated_at);

        if (st == DNS_OK) {
          (void)printf("  + added   %-42s -> %s\n", domain, ip);
          added++;
        } else if (st == DNS_ERR_DUPLICATE) {
          /* IP may have been changed in the file -- try update */
          st = dns_update_at(table, domain, ip, updated_at);
          if (st == DNS_OK) {
            (void)printf("  ~ updated %-42s -> %s\n", domain, ip);
            updated++;
          } else {
            (void)printf("  ! skipped %-42s (%s)\n", domain,
                         dns_status_string(st));
            skipped++;
          }
        } else {
          (void)printf("  ! skipped %-42s (%s)\n", domain,
                       dns_status_string(st));
          skipped++;
        }
      }
    }
    (void)fclose(fp);

    (void)printf("Import complete: %lu added, %lu updated, %lu skipped.\n",
                 (unsigned long)added, (unsigned long)updated,
                 (unsigned long)skipped);

    /* Persist the merged state */
    (void)save_to_file(table);
  }
}

/* ------------------------------------------------------------------ */
/* CRUD handlers -- same logic as before, save_to_file on success.    */
/* ------------------------------------------------------------------ */
static void handle_add(dns_table_t *table) {
  char domain[INPUT_BUF_LEN];
  char ip[INPUT_BUF_LEN];

  if (read_line("Domain name : ", domain, sizeof(domain)) &&
      read_line("IPv4 address: ", ip, sizeof(ip))) {
    const dns_status_t st = dns_add(table, domain, ip);
    report(st, "record added.");
    if (st == DNS_OK) {
      (void)save_to_file(table);
    }
  }
}

static void handle_resolve(const dns_table_t *table) {
  char domain[INPUT_BUF_LEN];
  char ip[DNS_IP_BUF_LEN];

  if (read_line("Domain name : ", domain, sizeof(domain))) {
    const dns_status_t st = dns_resolve(table, domain, ip, sizeof(ip));
    if (st == DNS_OK) {
      (void)printf("%s -> %s\n", domain, ip);
    } else {
      report(st, "");
    }
  }
}

static void handle_update(dns_table_t *table) {
  char domain[INPUT_BUF_LEN];
  char ip[INPUT_BUF_LEN];

  if (read_line("Domain name : ", domain, sizeof(domain)) &&
      read_line("New IPv4    : ", ip, sizeof(ip))) {
    const dns_status_t st = dns_update(table, domain, ip);
    report(st, "record updated.");
    if (st == DNS_OK) {
      (void)save_to_file(table);
    }
  }
}

static void handle_delete(dns_table_t *table) {
  char domain[INPUT_BUF_LEN];

  if (read_line("Domain name : ", domain, sizeof(domain))) {
    const dns_status_t st = dns_delete(table, domain);
    report(st, "record deleted.");
    if (st == DNS_OK) {
      (void)save_to_file(table);
    }
  }
}

static void handle_display(const dns_table_t *table) {
  const size_t n = dns_count(table);

  if (n == 0U) {
    (void)printf("No records stored.\n");
  } else {
    const dns_record_t **list =
        (const dns_record_t **)malloc(n * sizeof(const dns_record_t *));

    if (list == NULL) {
      (void)printf("Error: %s\n", dns_status_string(DNS_ERR_NOMEM));
    } else {
      size_t written = 0U;

      if (dns_get_sorted(table, list, n, &written) == DNS_OK) {
        size_t i;
        (void)printf("%-45s %-15s %-12s %s\n", "DOMAIN", "IP ADDRESS",
                     "CREATED", "UPDATED");
        (void)printf("%-45s %-15s %-12s %s\n",
                     "---------------------------------------------",
                     "---------------", "------------", "------------");
        for (i = 0U; i < written; i++) {
          (void)printf("%-45s %-15s %lld %lld\n", list[i]->domain, list[i]->ip,
                       (long long)list[i]->created_at,
                       (long long)list[i]->updated_at);
        }
      }
      free(list);
    }
  }
}

static void print_menu(void) {
  (void)printf("\n=== DNS Resolver  [store: " DNS_TXT_FILE "] ===\n"
               "1. Add record\n"
               "2. Resolve domain\n"
               "3. Update record\n"
               "4. Delete record\n"
               "5. Display all (alphabetical)\n"
               "6. Record count\n"
               "7. Export to CSV  (" DNS_CSV_FILE ")\n"
               "8. Import / reload from " DNS_TXT_FILE "\n"
               "0. Exit\n");
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
int main(void) {
  int exit_code = 0;
  bool running = true;
  dns_table_t *table = dns_table_create();

  if (table == NULL) {
    (void)printf("Error: %s\n", dns_status_string(DNS_ERR_NOMEM));
    exit_code = 1;
  } else {
    /* Restore persisted records on startup */
    load_from_file(table);

    char choice[INPUT_BUF_LEN];

    while (running) {
      print_menu();
      if (!read_line("Choice: ", choice, sizeof(choice))) {
        running = false; /* EOF / Ctrl-D */
      } else if (strlen(choice) != 1U) {
        (void)printf("Invalid choice.\n");
      } else {
        switch (choice[0]) {
        case '1':
          handle_add(table);
          break;
        case '2':
          handle_resolve(table);
          break;
        case '3':
          handle_update(table);
          break;
        case '4':
          handle_delete(table);
          break;
        case '5':
          handle_display(table);
          break;
        case '6':
          (void)printf("Total records: %lu  |  Buckets: %lu\n",
                       (unsigned long)dns_count(table),
                       (unsigned long)dns_bucket_count(table));
          break;
        case '7':
          handle_export_csv(table);
          break;
        case '8':
          handle_import(table);
          break;
        case '0':
          running = false;
          break;
        default:
          (void)printf("Invalid choice.\n");
          break;
        }
      }
    }
    dns_table_destroy(table);
  }
  return exit_code;
}
