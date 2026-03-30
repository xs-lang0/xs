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

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: xs <file.xs>\n");
        return 1;
    }

    const char *filename = argv[1];
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "could not open %s\n", filename);
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *src = malloc(sz + 1);
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

    Interp *interp = interp_new(filename);
    interp_run(interp, program);
    interp_free(interp);

    free(src);
    return 0;
}
