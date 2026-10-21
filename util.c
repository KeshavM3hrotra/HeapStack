#include "heaptrace.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* ---- formatting ---------------------------------------------------------- */

static char *vformat(const char *fmt, va_list ap) {
    va_list ap2;
    int n;
    char *buf;
    va_copy(ap2, ap);
    n = vsnprintf(NULL, 0, fmt, ap);
    if (n < 0) {
        va_end(ap2);
        return NULL;
    }
    buf = (char *)malloc((size_t)n + 1);
    if (buf != NULL) vsnprintf(buf, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    return buf;
}

char *ht_asprintf(const char *fmt, ...) {
    va_list ap;
    char *s;
    va_start(ap, fmt);
    s = vformat(fmt, ap);
    va_end(ap);
    return s;
}

char *ht_strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *copy = (char *)malloc(n);
    if (copy != NULL) memcpy(copy, s, n);
    return copy;
}

/* ---- StrList ------------------------------------------------------------- */

static int strlist_adopt(StrList *l, char *owned) {
    if (owned == NULL) return 0;
    if (l->count == l->cap) {
        size_t ncap = l->cap == 0 ? 16 : l->cap * 2;
        char **grown = (char **)realloc(l->items, ncap * sizeof(char *));
        if (grown == NULL) {
            free(owned);
            return 0;
        }
        l->items = grown;
        l->cap = ncap;
    }
    l->items[l->count++] = owned;
    return 1;
}

int strlist_push(StrList *l, const char *s) { return strlist_adopt(l, ht_strdup(s)); }

int strlist_pushf(StrList *l, const char *fmt, ...) {
    va_list ap;
    char *s;
    va_start(ap, fmt);
    s = vformat(fmt, ap);
    va_end(ap);
    return strlist_adopt(l, s);
}

void strlist_clear(StrList *l) {
    size_t i;
    for (i = 0; i < l->count; ++i) free(l->items[i]);
    free(l->items);
    l->items = NULL;
    l->count = 0;
    l->cap = 0;
}

void strlist_free(StrList *l) { strlist_clear(l); }

/* ---- StrBuf -------------------------------------------------------------- */

int strbuf_append(StrBuf *b, const char *s) {
    size_t n = strlen(s);
    if (b->len + n + 1 > b->cap) {
        size_t ncap = b->cap == 0 ? 64 : b->cap;
        char *grown;
        while (ncap < b->len + n + 1) ncap *= 2;
        grown = (char *)realloc(b->data, ncap);
        if (grown == NULL) return 0;
        b->data = grown;
        b->cap = ncap;
    }
    memcpy(b->data + b->len, s, n + 1);
    b->len += n;
    return 1;
}

void strbuf_free(StrBuf *b) {
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

/* ---- string helpers ------------------------------------------------------ */

void ht_to_upper(char *s) {
    for (; *s != '\0'; ++s) {
        if (*s >= 'a' && *s <= 'z') *s = (char)(*s - 'a' + 'A');
    }
}

void ht_to_lower(char *s) {
    for (; *s != '\0'; ++s) {
        if (*s >= 'A' && *s <= 'Z') *s = (char)(*s - 'A' + 'a');
    }
}

static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

char *ht_trim(char *s) {
    size_t len;
    while (*s != '\0' && is_space(*s)) ++s;
    len = strlen(s);
    while (len > 0 && is_space(s[len - 1])) --len;
    s[len] = '\0';
    return s;
}

int ht_parse_integer(const char *token, long long lo, long long hi, long long *out) {
    long long value = 0;
    size_t i, len = strlen(token);
    if (len == 0 || len > 18) return 0; /* 18 digits always fit in long long */
    for (i = 0; i < len; ++i) {
        if (token[i] < '0' || token[i] > '9') return 0;
        value = value * 10 + (token[i] - '0');
    }
    if (value < lo || value > hi) return 0;
    *out = value;
    return 1;
}

int ht_parse_size(const char *token, MemSize *out) {
    long long v = 0;
    if (!ht_parse_integer(token, 1, HT_MAX_MEMORY_MB, &v)) return 0;
    *out = v;
    return 1;
}

int ht_valid_vm_name(const char *name) {
    size_t i, len = strlen(name);
    if (len == 0 || len > HT_MAX_NAME) return 0;
    for (i = 0; i < len; ++i) {
        char c = name[i];
        int alnum = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (i == 0) {
            if (!alnum && c != '_') return 0;
        } else if (!alnum && c != '_' && c != '-' && c != '.') {
            return 0;
        }
    }
    return 1;
}

/* ---- metrics ------------------------------------------------------------- */

double metrics_average_fragmentation(const RunMetrics *m) {
    return m->samples > 0 ? m->frag_sum / m->samples : 0.0;
}

void metrics_free(RunMetrics *m) {
    strlist_free(&m->rejected_vms);
    strlist_free(&m->error_messages);
}

void strategy_run_free(StrategyRun *r) { metrics_free(&r->metrics); }

/* ---- session log --------------------------------------------------------- */

int log_open(SessionLog *log, const char *path) {
    FILE *fp;
    char *copy;
    log_close(log);
    fp = fopen(path, "w");
    if (fp == NULL) return 0;
    copy = ht_strdup(path);
    if (copy == NULL) {
        fclose(fp);
        return 0;
    }
    log->fp = fp;
    log->path = copy;
    return 1;
}

void log_write(SessionLog *log, const char *line) {
    if (log == NULL || log->fp == NULL) return;
    fputs(line, log->fp);
    fputc('\n', log->fp);
    fflush(log->fp);
}

void log_close(SessionLog *log) {
    if (log->fp != NULL) fclose(log->fp);
    free(log->path);
    log->fp = NULL;
    log->path = NULL;
}
