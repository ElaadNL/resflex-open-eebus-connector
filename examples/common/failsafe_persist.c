// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * examples/common/failsafe_persist.c
 */

#define _GNU_SOURCE   /* snprintf */

#include "failsafe_persist.h"
#include "tls_persist.h"
#include "cjson/cJSON.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define FAILSAFE_FILE "failsafe.json"

/* ── path / dir helpers ──────────────────────────────────────────────────── */

static bool build_path(const char* name, char* out, size_t outsz) {
    char dir[PATH_MAX];
    if (tls_persist_dir(name, dir, sizeof(dir)) != EEBUS_OK) return false;
    int n = snprintf(out, outsz, "%s/" FAILSAFE_FILE, dir);
    return n > 0 && (size_t)n < outsz;
}

/** Create ~/.eebus/<name>/ (and parents) with mode 0700, like mkdir -p. */
static bool mkdirs(const char* path) {
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (len && tmp[len - 1] == '/') tmp[len - 1] = '\0';
    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0700) != 0 && errno != EEXIST) return false;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0700) != 0 && errno != EEXIST) return false;
    return true;
}

static bool ensure_dir(const char* name) {
    char dir[PATH_MAX];
    if (tls_persist_dir(name, dir, sizeof(dir)) != EEBUS_OK) return false;
    return mkdirs(dir);
}

/* ── JSON serialization ──────────────────────────────────────────────────── */

static bool fs_from_json(const cJSON* obj, uc_lc_failsafe_t* out) {
    if (!cJSON_IsObject(obj)) return false;
    const cJSON* pw   = cJSON_GetObjectItemCaseSensitive(obj, "power_limit_w");
    const cJSON* dur  = cJSON_GetObjectItemCaseSensitive(obj, "duration_s");
    const cJSON* pwc  = cJSON_GetObjectItemCaseSensitive(obj, "power_limit_changeable");
    const cJSON* durc = cJSON_GetObjectItemCaseSensitive(obj, "duration_changeable");
    if (!cJSON_IsNumber(pw) || !cJSON_IsNumber(dur)) return false;
    memset(out, 0, sizeof(*out));
    out->power_limit_w          = pw->valuedouble;
    out->duration_s             = (uint32_t)dur->valuedouble;
    out->power_limit_changeable = cJSON_IsBool(pwc) ? cJSON_IsTrue(pwc) : false;
    out->duration_changeable    = cJSON_IsBool(durc) ? cJSON_IsTrue(durc) : false;
    return true;
}

static cJSON* fs_to_json(const uc_lc_failsafe_t* fs) {
    cJSON* o = cJSON_CreateObject();
    if (!o) return NULL;
    cJSON_AddNumberToObject(o, "power_limit_w", fs->power_limit_w);
    cJSON_AddNumberToObject(o, "duration_s",    (double)fs->duration_s);
    cJSON_AddBoolToObject(o, "power_limit_changeable", fs->power_limit_changeable);
    cJSON_AddBoolToObject(o, "duration_changeable",    fs->duration_changeable);
    return o;
}

/* ── file I/O ───────────────────────────────────────────────────────────── */

static cJSON* read_file_as_cjson(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n <= 0 || n > 64 * 1024) { fclose(f); return NULL; }
    rewind(f);
    char* buf = (char*)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = '\0';
    cJSON* root = cJSON_Parse(buf);
    free(buf);
    return root;
}

static bool write_atomic(const char* path, const cJSON* root) {
    char tmp[PATH_MAX];
    int n = snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (n <= 0 || (size_t)n >= sizeof(tmp)) return false;
    char* json = cJSON_PrintUnformatted(root);
    if (!json) return false;
    FILE* f = fopen(tmp, "wb");
    if (!f) { free(json); return false; }
    size_t len = strlen(json);
    bool ok = (fwrite(json, 1, len, f) == len) && (fflush(f) == 0);
    fclose(f);
    free(json);
    if (!ok)                     { unlink(tmp); return false; }
    if (rename(tmp, path) != 0)  { unlink(tmp); return false; }
    return true;
}

/* ── public API ──────────────────────────────────────────────────────────── */

bool failsafe_persist_load(const char* name, failsafe_persist_t* out) {
    if (!name || !out) return false;
    memset(out, 0, sizeof(*out));

    char path[PATH_MAX];
    if (!build_path(name, path, sizeof(path))) return false;

    cJSON* root = read_file_as_cjson(path);
    if (!root) return false;

    const cJSON* lpc = cJSON_GetObjectItemCaseSensitive(root, "lpc");
    const cJSON* lpp = cJSON_GetObjectItemCaseSensitive(root, "lpp");
    if (lpc && fs_from_json(lpc, &out->lpc)) out->lpc_present = true;
    if (lpp && fs_from_json(lpp, &out->lpp)) out->lpp_present = true;

    cJSON_Delete(root);
    return out->lpc_present || out->lpp_present;
}

bool failsafe_persist_save(const char* name, const failsafe_persist_t* in) {
    if (!name || !in) return false;
    if (!ensure_dir(name)) return false;

    char path[PATH_MAX];
    if (!build_path(name, path, sizeof(path))) return false;

    cJSON* root = cJSON_CreateObject();
    if (!root) return false;

    if (in->lpc_present) {
        cJSON* o = fs_to_json(&in->lpc);
        if (o) cJSON_AddItemToObject(root, "lpc", o);
    }
    if (in->lpp_present) {
        cJSON* o = fs_to_json(&in->lpp);
        if (o) cJSON_AddItemToObject(root, "lpp", o);
    }

    bool ok = write_atomic(path, root);
    cJSON_Delete(root);
    return ok;
}

bool failsafe_persist_update_lpc(const char* name,
                                  const uc_lc_failsafe_t* lpc) {
    if (!name || !lpc) return false;
    failsafe_persist_t fp = {0};
    (void)failsafe_persist_load(name, &fp);   /* missing file = empty start */
    fp.lpc          = *lpc;
    fp.lpc_present  = true;
    return failsafe_persist_save(name, &fp);
}

bool failsafe_persist_update_lpp(const char* name,
                                  const uc_lc_failsafe_t* lpp) {
    if (!name || !lpp) return false;
    failsafe_persist_t fp = {0};
    (void)failsafe_persist_load(name, &fp);
    fp.lpp          = *lpp;
    fp.lpp_present  = true;
    return failsafe_persist_save(name, &fp);
}

bool failsafe_persist_clear(const char* name) {
    if (!name) return false;
    char path[PATH_MAX];
    if (!build_path(name, path, sizeof(path))) return false;
    if (unlink(path) == 0)   return true;
    if (errno == ENOENT)     return true;
    return false;
}
