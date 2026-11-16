// SPDX-FileCopyrightText: 2026 ElaadNL
// SPDX-FileContributor: Developed by enjoyelec
// SPDX-License-Identifier: Apache-2.0

/*
 * examples/common/cs_config.c
 */

#define _GNU_SOURCE

#include "cs_config.h"
#include "tls_persist.h"
#include "cjson/cJSON.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CFG_FILE "cs_config.json"

/* ── path / dir helpers ──────────────────────────────────────────────────── */

static bool build_path(const char* name, char* out, size_t outsz) {
    char dir[PATH_MAX];
    if (tls_persist_dir(name, dir, sizeof(dir)) != EEBUS_OK) return false;
    int n = snprintf(out, outsz, "%s/" CFG_FILE, dir);
    return n > 0 && (size_t)n < outsz;
}

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

/* ── JSON ────────────────────────────────────────────────────────────────── */

static bool dir_from_json(const cJSON* o, cs_config_dir_t* d) {
    if (!cJSON_IsObject(o)) return false;
    memset(d, 0, sizeof(*d));
    const cJSON* v;
    #define GET_DBL(field, key) do {                                       \
        v = cJSON_GetObjectItemCaseSensitive(o, key);                      \
        if (cJSON_IsNumber(v)) (field) = v->valuedouble;                   \
    } while (0)
    #define GET_U32(field, key) do {                                       \
        v = cJSON_GetObjectItemCaseSensitive(o, key);                      \
        if (cJSON_IsNumber(v)) (field) = (uint32_t)v->valuedouble;         \
    } while (0)
    GET_DBL(d->apl_max_w,     "apl_max_w");
    GET_DBL(d->apl_min_w,     "apl_min_w");
    GET_DBL(d->fapl_max_w,    "fapl_max_w");
    GET_DBL(d->fapl_min_w,    "fapl_min_w");
    GET_DBL(d->pfapl_w,       "pfapl_w");
    GET_U32(d->mfsdm_s,       "mfsdm_s");
    GET_U32(d->pfsdm_s,       "pfsdm_s");
    GET_DBL(d->nominal_max_w, "nominal_max_w");
    #undef GET_DBL
    #undef GET_U32
    return true;
}

static cJSON* dir_to_json(const cs_config_dir_t* d) {
    cJSON* o = cJSON_CreateObject();
    if (!o) return NULL;
    cJSON_AddNumberToObject(o, "apl_max_w",     d->apl_max_w);
    cJSON_AddNumberToObject(o, "apl_min_w",     d->apl_min_w);
    cJSON_AddNumberToObject(o, "fapl_max_w",    d->fapl_max_w);
    cJSON_AddNumberToObject(o, "fapl_min_w",    d->fapl_min_w);
    cJSON_AddNumberToObject(o, "pfapl_w",       d->pfapl_w);
    cJSON_AddNumberToObject(o, "mfsdm_s",       (double)d->mfsdm_s);
    cJSON_AddNumberToObject(o, "pfsdm_s",       (double)d->pfsdm_s);
    cJSON_AddNumberToObject(o, "nominal_max_w", d->nominal_max_w);
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

/* ── public load/save/update ─────────────────────────────────────────────── */

bool cs_config_load(const char* name, cs_config_t* out) {
    if (!name || !out) return false;
    memset(out, 0, sizeof(*out));
    char path[PATH_MAX];
    if (!build_path(name, path, sizeof(path))) return false;
    cJSON* root = read_file_as_cjson(path);
    if (!root) return false;
    const cJSON* lpc = cJSON_GetObjectItemCaseSensitive(root, "lpc");
    const cJSON* lpp = cJSON_GetObjectItemCaseSensitive(root, "lpp");
    if (lpc) out->lpc_present = dir_from_json(lpc, &out->lpc);
    if (lpp) out->lpp_present = dir_from_json(lpp, &out->lpp);
    cJSON_Delete(root);
    return out->lpc_present || out->lpp_present;
}

bool cs_config_save(const char* name, const cs_config_t* in) {
    if (!name || !in) return false;
    if (!ensure_dir(name)) return false;
    char path[PATH_MAX];
    if (!build_path(name, path, sizeof(path))) return false;
    cJSON* root = cJSON_CreateObject();
    if (!root) return false;
    if (in->lpc_present) {
        cJSON* o = dir_to_json(&in->lpc);
        if (o) cJSON_AddItemToObject(root, "lpc", o);
    }
    if (in->lpp_present) {
        cJSON* o = dir_to_json(&in->lpp);
        if (o) cJSON_AddItemToObject(root, "lpp", o);
    }
    bool ok = write_atomic(path, root);
    cJSON_Delete(root);
    return ok;
}

bool cs_config_update_lpc(const char* name, const cs_config_dir_t* lpc) {
    if (!name || !lpc) return false;
    cs_config_t cfg = {0};
    (void)cs_config_load(name, &cfg);
    cfg.lpc          = *lpc;
    cfg.lpc_present  = true;
    return cs_config_save(name, &cfg);
}

bool cs_config_update_lpp(const char* name, const cs_config_dir_t* lpp) {
    if (!name || !lpp) return false;
    cs_config_t cfg = {0};
    (void)cs_config_load(name, &cfg);
    cfg.lpp          = *lpp;
    cfg.lpp_present  = true;
    return cs_config_save(name, &cfg);
}

bool cs_config_init_if_absent(const char* name, const cs_config_t* defaults) {
    if (!name || !defaults) return false;
    char path[PATH_MAX];
    if (!build_path(name, path, sizeof(path))) return false;

    struct stat st;
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode))
        return true;   /* already present — leave it alone */

    return cs_config_save(name, defaults);
}

/* ── CLI ─────────────────────────────────────────────────────────────────── */

typedef enum {
    F_APL_MAX, F_APL_MIN,
    F_FAPL_MAX, F_FAPL_MIN,
    F_PFAPL,
    F_MFSDM, F_PFSDM,
    F_NOMINAL_MAX,
    F_INVALID,
} field_id_t;

static field_id_t parse_field(const char* s) {
    if (strcmp(s, "apl_max")     == 0) return F_APL_MAX;
    if (strcmp(s, "apl_min")     == 0) return F_APL_MIN;
    if (strcmp(s, "fapl_max")    == 0) return F_FAPL_MAX;
    if (strcmp(s, "fapl_min")    == 0) return F_FAPL_MIN;
    if (strcmp(s, "pfapl")       == 0) return F_PFAPL;
    if (strcmp(s, "mfsdm")       == 0) return F_MFSDM;
    if (strcmp(s, "pfsdm")       == 0) return F_PFSDM;
    if (strcmp(s, "nominal_max") == 0) return F_NOMINAL_MAX;
    return F_INVALID;
}

static bool dir_set(cs_config_dir_t* d, field_id_t f, const char* val) {
    char* end = NULL;
    if (f == F_MFSDM || f == F_PFSDM) {
        unsigned long n = strtoul(val, &end, 10);
        if (!end || *end != '\0') return false;
        if (f == F_MFSDM) d->mfsdm_s = (uint32_t)n;
        else              d->pfsdm_s = (uint32_t)n;
        return true;
    }
    double v = strtod(val, &end);
    if (!end || *end != '\0') return false;
    switch (f) {
    case F_APL_MAX:     d->apl_max_w     = v; break;
    case F_APL_MIN:     d->apl_min_w     = v; break;
    case F_FAPL_MAX:    d->fapl_max_w    = v; break;
    case F_FAPL_MIN:    d->fapl_min_w    = v; break;
    case F_PFAPL:       d->pfapl_w       = v; break;
    case F_NOMINAL_MAX: d->nominal_max_w = v; break;
    default: return false;
    }
    return true;
}

/* Full-name display, indexed by field_id_t. */
static const char* const FULL_NAME_LPC[] = {
    "Active Power Consumption Limit Maximum",
    "Active Power Consumption Limit Minimum",
    "Failsafe Consumption Active Power Limit Maximum",
    "Failsafe Consumption Active Power Limit Minimum",
    "Pre-configured Failsafe Consumption Active Power Limit",
    "Maximum Failsafe Duration Minimum",
    "Pre-configured Failsafe Duration Minimum",
    "Consumption Nominal Maximum",
};
static const char* const FULL_NAME_LPP[] = {
    "Active Power Production Limit Maximum",
    "Active Power Production Limit Minimum",
    "Failsafe Production Active Power Limit Maximum",
    "Failsafe Production Active Power Limit Minimum",
    "Pre-configured Failsafe Production Active Power Limit",
    "Maximum Failsafe Duration Minimum",
    "Pre-configured Failsafe Duration Minimum",
    "Production Nominal Maximum",
};

static void print_dir(const char* hdr, const cs_config_dir_t* d, bool is_lpc) {
    printf("%s:\n", hdr);
    const char* const* names = is_lpc ? FULL_NAME_LPC : FULL_NAME_LPP;
    int w = 0;
    for (int i = 0; i < 8; i++) {
        int n = (int)strlen(names[i]);
        if (n > w) w = n;
    }
    printf("  %-*s : %8.0f W\n", w, names[F_APL_MAX],     d->apl_max_w);
    printf("  %-*s : %8.0f W\n", w, names[F_APL_MIN],     d->apl_min_w);
    printf("  %-*s : %8.0f W\n", w, names[F_FAPL_MAX],    d->fapl_max_w);
    printf("  %-*s : %8.0f W\n", w, names[F_FAPL_MIN],    d->fapl_min_w);
    printf("  %-*s : %8.0f W\n", w, names[F_PFAPL],       d->pfapl_w);
    printf("  %-*s : %8u s\n",   w, names[F_MFSDM],       d->mfsdm_s);
    printf("  %-*s : %8u s\n",   w, names[F_PFSDM],       d->pfsdm_s);
    printf("  %-*s : %8.0f W\n", w, names[F_NOMINAL_MAX], d->nominal_max_w);
}

static void cmd_show(const char* name, int argc, char** argv) {
    cs_config_t cfg = {0};
    (void)cs_config_load(name, &cfg);

    const char* which = (argc >= 3) ? argv[2] : NULL;
    bool show_lpc = (!which || strcmp(which, "lpc") == 0);
    bool show_lpp = (!which || strcmp(which, "lpp") == 0);
    if (which && !show_lpc && !show_lpp) {
        printf("Usage: cfg show [lpc|lpp]\n");
        return;
    }
    if (show_lpc) {
        if (cfg.lpc_present) print_dir("LPC", &cfg.lpc, true);
        else                 printf("LPC:\n  (not configured)\n");
    }
    if (show_lpp) {
        if (cfg.lpp_present) print_dir("LPP", &cfg.lpp, false);
        else                 printf("LPP:\n  (not configured)\n");
    }
}

static void cmd_set(const char* name, int argc, char** argv) {
    if (argc < 5) {
        printf("Usage: cfg set <lpc|lpp> <field> <value>\n");
        return;
    }
    const char* dir_str = argv[2];
    const char* field   = argv[3];
    const char* value   = argv[4];

    bool is_lpc;
    if      (strcmp(dir_str, "lpc") == 0) is_lpc = true;
    else if (strcmp(dir_str, "lpp") == 0) is_lpc = false;
    else { printf("Invalid direction '%s' (use lpc or lpp)\n", dir_str); return; }

    field_id_t fid = parse_field(field);
    if (fid == F_INVALID) {
        printf("Invalid field '%s'\n", field);
        printf("Valid: apl_max apl_min fapl_max fapl_min pfapl mfsdm pfsdm nominal_max\n");
        return;
    }

    cs_config_t cfg = {0};
    (void)cs_config_load(name, &cfg);

    cs_config_dir_t* d = is_lpc ? &cfg.lpc : &cfg.lpp;
    if (!dir_set(d, fid, value)) {
        printf("Invalid value '%s' for field '%s'\n", value, field);
        return;
    }
    if (is_lpc) cfg.lpc_present = true;
    else        cfg.lpp_present = true;

    /* SPINE [LPC-022]: failsafe duration is shared between LPC and LPP.
     * Mirror mfsdm/pfsdm to the other direction when both are configured. */
    if ((fid == F_MFSDM || fid == F_PFSDM)
            && cfg.lpc_present && cfg.lpp_present) {
        cs_config_dir_t* other = is_lpc ? &cfg.lpp : &cfg.lpc;
        if (fid == F_MFSDM) other->mfsdm_s = d->mfsdm_s;
        else                other->pfsdm_s = d->pfsdm_s;
    }

    if (!cs_config_save(name, &cfg)) {
        printf("Failed to save cs_config.json\n");
        return;
    }
    print_dir(is_lpc ? "LPC" : "LPP", d, is_lpc);
}

void cs_config_cli_handler(int argc, char** argv, void* ud) {
    const char* name = (const char*)ud;
    if (!name) { printf("[cfg] no demo name configured\n"); return; }

    if (argc < 2 || strcmp(argv[1], "show") == 0) {
        cmd_show(name, argc, argv);
        return;
    }
    if (strcmp(argv[1], "set") == 0) {
        cmd_set(name, argc, argv);
        return;
    }
    printf("Usage: cfg [show [lpc|lpp] | set <lpc|lpp> <field> <value>]\n");
    printf("Fields: apl_max apl_min fapl_max fapl_min pfapl mfsdm pfsdm nominal_max\n");
}
