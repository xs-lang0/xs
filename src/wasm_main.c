/*
 * WASM entry point for XS. Handles file execution, transpilation,
 * type checking - everything the native binary does except REPL/LSP/DAP.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/xs.h"
#include "core/value.h"
#include "core/ast.h"
#include "core/lexer.h"
#include "core/parser.h"
#include "runtime/interp.h"
#include "runtime/error.h"
#include "semantic/sema.h"
#include "semantic/cache.h"
#include "types/inference.h"

#ifdef XSC_ENABLE_TRANSPILER
extern char *transpile_js(Node *program, const char *filename);
extern char *transpile_c(Node *program, const char *filename);
#endif

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("xs (wasm) - XS programming language\n");
        printf("usage: xs [options] <file.xs>\n");
        printf("  --emit js    transpile to JavaScript\n");
        printf("  --emit c     transpile to C\n");
        printf("  --check      type check only\n");
        printf("  --strict     require type annotations\n");
        return 0;
    }

    const char *filename = NULL;
    const char *emit_target = NULL;
    const char *stdin_file = NULL;
    int check_only = 0;
    int strict = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--emit") == 0 && i + 1 < argc) {
            emit_target = argv[++i];
        } else if (strcmp(argv[i], "--stdin") == 0 && i + 1 < argc) {
            stdin_file = argv[++i];
        } else if (strcmp(argv[i], "--check") == 0) {
            check_only = 1;
        } else if (strcmp(argv[i], "--strict") == 0) {
            strict = 1;
        } else {
            filename = argv[i];
        }
    }

    if (!filename) {
        fprintf(stderr, "error: no input file\n");
        return 1;
    }

    /* redirect input() reads from file if --stdin given */
    extern FILE *g_xs_stdin_override;
    if (stdin_file) {
        g_xs_stdin_override = fopen(stdin_file, "r");
    }

    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "error: could not open %s\n", filename);
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *src = malloc(sz + 1);
    if (!src) { fclose(f); return 1; }
    fread(src, 1, sz, f);
    src[sz] = '\0';
    fclose(f);

    Lexer lex;
    lexer_init(&lex, src, filename);
    lexer_tokenize(&lex);

    Parser p;
    parser_init(&p, &lex.tokens, filename);
    p.literals = lex.literals;
    Node *program = parser_parse(&p);

    if (p.had_error) {
        free(src);
        return 1;
    }

    if (check_only) {
        /* semantic analysis */
        SemaCtx sema;
        sema_init(&sema, 0, strict);
        sema_analyze(&sema, program, filename);
        int sema_errors = sema.n_errors;
        sema_free(&sema);
        free(src);
        return sema_errors > 0 ? 1 : 0;
    }

#ifdef XSC_ENABLE_TRANSPILER
    if (emit_target) {
        if (strcmp(emit_target, "js") == 0) {
            char *js = transpile_js(program, filename);
            if (js) { printf("%s", js); free(js); }
        } else if (strcmp(emit_target, "c") == 0) {
            char *c_code = transpile_c(program, filename);
            if (c_code) { printf("%s", c_code); free(c_code); }
        } else {
            fprintf(stderr, "error: unknown target '%s'\n", emit_target);
            free(src);
            return 1;
        }
        free(src);
        return 0;
    }
#endif

    Interp *interp = interp_new(filename);
    interp_run(interp, program);

    int had_error = (interp->cf.signal != 0);
    interp_free(interp);
    free(src);
    return had_error ? 1 : 0;
}
