// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * examples/common/tls_persist.c
 */

#define _GNU_SOURCE   /* strdup, snprintf */

#include "tls_persist.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>

/* ── helpers ─────────────────────────────────────────────────────────────── */

/** Create a directory (and parents) with mode 0700, like mkdir -p. */
static eebus_err_t mkdirs(const char* path) {
  char tmp[PATH_MAX];
  snprintf(tmp, sizeof(tmp), "%s", path);
  size_t len = strlen(tmp);
  if (len && tmp[len - 1] == '/') tmp[len - 1] = '\0';

  for (char* p = tmp + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0';
      if (mkdir(tmp, 0700) != 0 && errno != EEXIST) return EEBUS_ERR_IO;
      *p = '/';
    }
  }
  if (mkdir(tmp, 0700) != 0 && errno != EEXIST) return EEBUS_ERR_IO;
  return EEBUS_OK;
}

/** Return 1 if a regular file exists at path. */
static int file_exists(const char* path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/** Write ski to ~/.eebus/<n>/ski.txt (best-effort, no error on failure). */
static void write_ski_file(const char* dir, const char* ski) {
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "%s/ski.txt", dir);
  FILE* f = fopen(path, "w");
  if (!f) return;
  fprintf(f, "%s\n", ski);
  fclose(f);
}

/* ── public API ──────────────────────────────────────────────────────────── */

eebus_err_t tls_persist_dir(const char* name, char* buf, size_t bufsz) {
  const char* home = getenv("HOME");
  if (!home || !home[0]) return EEBUS_ERR_IO;
  int n = snprintf(buf, bufsz, "%s/.eebus/%s", home, name);
  if (n < 0 || (size_t)n >= bufsz) return EEBUS_ERR_IO;
  return EEBUS_OK;
}

eebus_err_t tls_persist_load(const char* name, ship_tls_cred_t* cred) {
  if (!name || !cred) return EEBUS_ERR_INVALID;

  /* dir <= 490 chars, leaving room for "/cert.pem" + NUL within 512 */
  char dir[490];
  eebus_err_t rc = tls_persist_dir(name, dir, sizeof(dir));
  if (rc != EEBUS_OK) return rc;

  char cert_path[512];
  char key_path[512];
  snprintf(cert_path, sizeof(cert_path), "%s/cert.pem", dir);
  snprintf(key_path,  sizeof(key_path),  "%s/key.pem",  dir);

  memset(cred, 0, sizeof(*cred));

  if (file_exists(cert_path) && file_exists(key_path)) {
    /* Load existing credentials */
    rc = ship_tls_load(cred, cert_path, key_path);
    if (rc != EEBUS_OK) return rc;
    printf("[TLS] Loaded certificate from %s (SKI: %s)\n",
           dir, cred->ski);
  } else {
    /* Generate new credentials */
    rc = mkdirs(dir);
    if (rc != EEBUS_OK) {
      fprintf(stderr, "[TLS] Cannot create directory %s: %s\n",
              dir, strerror(errno));
      return rc;
    }

    rc = ship_tls_generate(cred, name);
    if (rc != EEBUS_OK) {
      fprintf(stderr, "[TLS] Certificate generation failed\n");
      return rc;
    }

    rc = ship_tls_save(cred, cert_path, key_path);
    if (rc != EEBUS_OK) {
      fprintf(stderr, "[TLS] Cannot save certificate to %s\n", dir);
      ship_tls_cred_free(cred);
      return rc;
    }

    write_ski_file(dir, cred->ski);
    printf("[TLS] Generated new certificate (SKI: %s)\n", cred->ski);
    printf("[TLS] Saved to %s\n", dir);
  }

  return EEBUS_OK;
}
