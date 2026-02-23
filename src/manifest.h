#ifndef MANIFEST_H
#define MANIFEST_H

/*  Decision manifest compiler — the core of el-stupido's pivot.
 *
 *  Pipeline:
 *    1. LLM emits ~30-80 tokens of constrained JSON (decision manifest)
 *    2. manifest_parse() reads JSON into Manifest struct
 *    3. manifest_expand() deterministically generates el-stupido source
 *    4. Existing compiler pipeline compiles it to native binary
 *
 *  Or, for file-based output:
 *    3. manifest_expand_files() writes multiple files to output dir
 */

#define MF_MAX_MODELS  8
#define MF_MAX_FIELDS  16
#define MF_MAX_ROUTES  32
#define MF_MAX_FLAGS   16
#define MF_MAX_ARGS    16
#define MF_MAX_TESTS   32
#define MF_MAX_ASSERTS 16

typedef enum { MF_CRUD, MF_REST, MF_CLI, MF_TEST } MfDomain;
typedef enum { MF_STRING, MF_INT, MF_BOOL, MF_TEXT } MfFieldType;
typedef enum { MF_GET, MF_POST, MF_DELETE } MfMethod;
typedef enum { MF_LIST, MF_CREATE, MF_DEL, MF_STATIC, MF_HEALTH } MfAction;

typedef struct {
    char name[64];
    MfFieldType type;
    int required;
} MfField;

typedef struct {
    char name[64];
    MfField fields[MF_MAX_FIELDS];
    int field_count;
} MfModel;

typedef struct {
    MfMethod method;
    char path[128];
    MfAction action;
    char model[64];     /* for list/create/delete */
    char body[256];     /* for static */
} MfRoute;

typedef struct {
    char name[64];
    char shortf[4];
    char help[128];
} MfFlag;

typedef struct {
    char name[64];
    char help[128];
} MfArg;

typedef struct {
    char name[128];
    char assertions[MF_MAX_ASSERTS][256];
    int assert_count;
} MfTest;

typedef struct {
    MfDomain domain;
    char app_name[64];
    int port;

    MfModel models[MF_MAX_MODELS];
    int model_count;

    MfRoute routes[MF_MAX_ROUTES];
    int route_count;

    MfFlag flags[MF_MAX_FLAGS];
    int flag_count;

    MfArg args[MF_MAX_ARGS];
    int arg_count;

    MfTest tests[MF_MAX_TESTS];
    int test_count;
} Manifest;

/* Parse JSON string into Manifest struct. Returns 0 on success, -1 on error. */
int manifest_parse(const char *json, Manifest *mf);

/* Expand manifest into el-stupido source code (single string, malloc'd).
   Caller frees. Returns NULL on error. */
char *manifest_expand(const Manifest *mf);

/* Return the GBNF grammar string for constrained manifest generation. */
const char *manifest_grammar(void);

#endif /* MANIFEST_H */
