#ifndef ES_H
#define ES_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <ctype.h>
#include <assert.h>

/* ---- dynamic array ---- */
#define DA_INIT_CAP 8
#define da_push(da, item) do {                                      \
    if ((da).count >= (da).cap) {                                   \
        (da).cap = (da).cap ? (da).cap * 2 : DA_INIT_CAP;          \
        (da).items = realloc((da).items,                            \
                             (da).cap * sizeof(*(da).items));       \
    }                                                               \
    (da).items[(da).count++] = (item);                              \
} while(0)

/* ---- error helpers ---- */
#define es_fatal(fmt, ...) do {                                     \
    fprintf(stderr, "error: " fmt "\n", ##__VA_ARGS__);             \
    exit(1);                                                        \
} while(0)

#define es_error_at(file, line, col, fmt, ...) do {                 \
    fprintf(stderr, "%s:%d:%d: error: " fmt "\n",                   \
            (file), (line), (col), ##__VA_ARGS__);                  \
    exit(1);                                                        \
} while(0)

/* ---- string helpers ---- */
static inline char *es_strdup(const char *s) {
    size_t len = strlen(s);
    char *d = (char *)malloc(len + 1);
    memcpy(d, s, len + 1);
    return d;
}

static inline char *es_strndup(const char *s, size_t n) {
    char *d = (char *)malloc(n + 1);
    memcpy(d, s, n);
    d[n] = '\0';
    return d;
}

/* ---- file I/O ---- */
static inline char *es_read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { es_fatal("cannot open '%s'", path); }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc(len + 1);
    if (fread(buf, 1, len, f) != (size_t)len) {
        es_fatal("failed to read '%s'", path);
    }
    buf[len] = '\0';
    fclose(f);
    return buf;
}

#endif /* ES_H */
