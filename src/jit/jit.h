/* Copy-patch JIT for x86-64 (System V ABI).
 * Guarded by XSC_ENABLE_JIT; falls back if mmap fails. */
#ifndef XS_JIT_H
#define XS_JIT_H

#ifdef XSC_ENABLE_JIT

#include "vm/bytecode.h"
#include <stdint.h>
#include <stddef.h>

#define XS_JIT_THRESHOLD    100
#define XS_JIT_CODE_SIZE    (4 * 1024 * 1024)  /* 4 MiB code buffer */
#define XS_JIT_MAX_PROTOS   1024

typedef Value *(*JitFn)(Value **stack_base, Value **constants, Value **locals, XSMap *globals);

typedef struct XSJIT {
    uint8_t *code;          /* mmap'd executable code buffer */
    size_t   code_size;     /* allocated size */
    size_t   code_used;     /* bytes emitted so far */
    void   **compiled;      /* compiled[proto_index] = JitFn or NULL */
    int     *call_counts;   /* per-proto invocation counts */
    int      n_protos;      /* number of proto slots */
    int      available;     /* 1 if mmap succeeded, 0 if JIT unavailable */
} XSJIT;

XSJIT *jit_new(void);
void   jit_free(XSJIT *j);
void  *jit_compile(XSJIT *j, XSProto *proto);
void  *jit_maybe_compile(XSJIT *j, int proto_index, XSProto *proto);
JitFn  jit_get_compiled(XSJIT *j, int proto_index);
int    jit_available(void);

#else

typedef struct XSJIT XSJIT;
typedef void *(*JitFn)(void);

static inline int    jit_available(void) { return 0; }
static inline XSJIT *jit_new(void) { return NULL; }
static inline void   jit_free(XSJIT *j) { (void)j; }
static inline void  *jit_compile(XSJIT *j, void *proto) { (void)j; (void)proto; return NULL; }
static inline void  *jit_maybe_compile(XSJIT *j, int proto_index, void *proto) { (void)j; (void)proto_index; (void)proto; return NULL; }
static inline JitFn  jit_get_compiled(XSJIT *j, int proto_index) { (void)j; (void)proto_index; return NULL; }

#endif
#endif
