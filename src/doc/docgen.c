#include "doc/docgen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "core/strbuf.h"

static void type_to_str(SB *s, TypeExpr *te) {
    if (!te) return;
    switch (te->kind) {
    case TEXPR_NAMED:
        if (te->name) sb_add(s, te->name);
        if (te->nargs > 0) {
            sb_addc(s, '<');
            for (int i = 0; i < te->nargs; i++) {
                if (i > 0) sb_add(s, ", ");
                type_to_str(s, te->args[i]);
            }
            sb_addc(s, '>');
        }
        break;
    case TEXPR_ARRAY:
        sb_addc(s, '[');
        type_to_str(s, te->inner);
        sb_addc(s, ']');
        break;
    case TEXPR_TUPLE:
        sb_addc(s, '(');
        for (int i = 0; i < te->nelems; i++) {
            if (i > 0) sb_add(s, ", ");
            type_to_str(s, te->elems[i]);
        }
        sb_addc(s, ')');
        break;
    case TEXPR_FN:
        sb_add(s, "fn(");
        for (int i = 0; i < te->nparams; i++) {
            if (i > 0) sb_add(s, ", ");
            type_to_str(s, te->params[i]);
        }
        sb_addc(s, ')');
        if (te->ret) {
            sb_add(s, " -> ");
            type_to_str(s, te->ret);
        }
        break;
    case TEXPR_OPTION:
        type_to_str(s, te->inner);
        sb_addc(s, '?');
        break;
    case TEXPR_INFER:
        sb_addc(s, '_');
        break;
    }
}

typedef struct { Node **items; int len; int cap; } DeclList;
static void dl_init(DeclList *d) { d->items = NULL; d->len = 0; d->cap = 0; }
static void dl_push(DeclList *d, Node *n) {
    if (d->len >= d->cap) {
        d->cap = d->cap ? d->cap * 2 : 8;
        Node **tmp = (Node**)realloc(d->items, (size_t)d->cap * sizeof(Node*));
        if (!tmp) return;
        d->items = tmp;
    }
    d->items[d->len++] = n;
}
static void dl_free(DeclList *d) { free(d->items); }

static void collect_decls(Node *program,
                          DeclList *fns, DeclList *structs, DeclList *enums,
                          DeclList *traits, DeclList *impls, DeclList *consts,
                          DeclList *aliases, DeclList *effects) {
    if (!program || program->tag != NODE_PROGRAM) return;
    for (int i = 0; i < program->program.stmts.len; i++) {
        Node *n = program->program.stmts.items[i];
        if (!n) continue;
        switch (n->tag) {
        case NODE_FN_DECL:     dl_push(fns, n); break;
        case NODE_STRUCT_DECL: dl_push(structs, n); break;
        case NODE_ENUM_DECL:   dl_push(enums, n); break;
        case NODE_TRAIT_DECL:  dl_push(traits, n); break;
        case NODE_IMPL_DECL:   dl_push(impls, n); break;
        case NODE_CONST:       dl_push(consts, n); break;
        case NODE_TYPE_ALIAS:  dl_push(aliases, n); break;
        case NODE_EFFECT_DECL: dl_push(effects, n); break;
        default: break;
        }
    }
}

/* markdown output */
static void gen_markdown(SB *s, Node *program, const char *filename,
                         DeclList *fns, DeclList *structs, DeclList *enums,
                         DeclList *traits, DeclList *impls, DeclList *consts,
                         DeclList *aliases, DeclList *effects) {
    (void)program;
    sb_printf(s, "# Module: %s\n\n", filename ? filename : "<unknown>");

    /* Functions */
    if (fns->len > 0) {
        sb_add(s, "## Functions\n\n");
        for (int i = 0; i < fns->len; i++) {
            Node *n = fns->items[i];
            sb_add(s, "### `");
            if (n->fn_decl.is_pub) sb_add(s, "pub ");
            if (n->fn_decl.is_async) sb_add(s, "async ");
            sb_add(s, "fn ");
            sb_add(s, n->fn_decl.name);
            sb_addc(s, '(');
            for (int j = 0; j < n->fn_decl.params.len; j++) {
                if (j > 0) sb_add(s, ", ");
                Param *p = &n->fn_decl.params.items[j];
                if (p->name) sb_add(s, p->name);
                if (p->type_ann) {
                    sb_add(s, ": ");
                    type_to_str(s, p->type_ann);
                }
            }
            sb_addc(s, ')');
            if (n->fn_decl.ret_type) {
                sb_add(s, " -> ");
                type_to_str(s, n->fn_decl.ret_type);
            }
            sb_add(s, "`\n\n");
        }
    }

    /* Structs */
    if (structs->len > 0) {
        sb_add(s, "## Structs\n\n");
        for (int i = 0; i < structs->len; i++) {
            Node *n = structs->items[i];
            sb_printf(s, "### `struct %s`\n\n", n->struct_decl.name);
            if (n->struct_decl.fields.len > 0) {
                sb_add(s, "**Fields:**\n");
                for (int j = 0; j < n->struct_decl.fields.len; j++) {
                    sb_printf(s, "- `%s`\n", n->struct_decl.fields.items[j].key);
                }
                sb_addc(s, '\n');
            }
        }
    }

    /* Enums */
    if (enums->len > 0) {
        sb_add(s, "## Enums\n\n");
        for (int i = 0; i < enums->len; i++) {
            Node *n = enums->items[i];
            sb_printf(s, "### `enum %s`\n\n", n->enum_decl.name);
            if (n->enum_decl.variants.len > 0) {
                sb_add(s, "**Variants:**\n");
                for (int j = 0; j < n->enum_decl.variants.len; j++) {
                    sb_printf(s, "- `%s`\n", n->enum_decl.variants.items[j].name);
                }
                sb_addc(s, '\n');
            }
        }
    }

    /* Traits */
    if (traits->len > 0) {
        sb_add(s, "## Traits\n\n");
        for (int i = 0; i < traits->len; i++) {
            Node *n = traits->items[i];
            sb_printf(s, "### `trait %s`\n\n", n->trait_decl.name);
            if (n->trait_decl.n_methods > 0) {
                sb_add(s, "**Methods:**\n");
                for (int j = 0; j < n->trait_decl.n_methods; j++) {
                    sb_printf(s, "- `%s`\n", n->trait_decl.method_names[j]);
                }
                sb_addc(s, '\n');
            }
        }
    }

    /* Implementations */
    if (impls->len > 0) {
        sb_add(s, "## Implementations\n\n");
        for (int i = 0; i < impls->len; i++) {
            Node *n = impls->items[i];
            sb_add(s, "### `impl ");
            if (n->impl_decl.trait_name) {
                sb_add(s, n->impl_decl.trait_name);
                sb_add(s, " for ");
            }
            sb_add(s, n->impl_decl.type_name);
            sb_add(s, "`\n\n");
            if (n->impl_decl.members.len > 0) {
                sb_add(s, "**Members:**\n");
                for (int j = 0; j < n->impl_decl.members.len; j++) {
                    Node *m = n->impl_decl.members.items[j];
                    if (m && m->tag == NODE_FN_DECL) {
                        sb_printf(s, "- `fn %s()`\n", m->fn_decl.name);
                    }
                }
                sb_addc(s, '\n');
            }
        }
    }

    /* Constants */
    if (consts->len > 0) {
        sb_add(s, "## Constants\n\n");
        for (int i = 0; i < consts->len; i++) {
            Node *n = consts->items[i];
            sb_printf(s, "### `const %s`\n\n", n->const_.name);
        }
    }

    /* Type Aliases */
    if (aliases->len > 0) {
        sb_add(s, "## Type Aliases\n\n");
        for (int i = 0; i < aliases->len; i++) {
            Node *n = aliases->items[i];
            sb_printf(s, "### `type %s = %s`\n\n",
                      n->type_alias.name, n->type_alias.target);
        }
    }

    /* Effects */
    if (effects->len > 0) {
        sb_add(s, "## Effects\n\n");
        for (int i = 0; i < effects->len; i++) {
            Node *n = effects->items[i];
            sb_printf(s, "### `effect %s`\n\n", n->effect_decl.name);
            if (n->effect_decl.ops.len > 0) {
                sb_add(s, "**Operations:**\n");
                for (int j = 0; j < n->effect_decl.ops.len; j++) {
                    Node *op = n->effect_decl.ops.items[j];
                    if (op && op->tag == NODE_FN_DECL) {
                        sb_printf(s, "- `fn %s()`\n", op->fn_decl.name);
                    }
                }
                sb_addc(s, '\n');
            }
        }
    }
}

static void json_escape(SB *s, const char *str) {
    if (!str) { sb_add(s, "null"); return; }
    sb_addc(s, '"');
    for (const char *p = str; *p; p++) {
        switch (*p) {
        case '"':  sb_add(s, "\\\""); break;
        case '\\': sb_add(s, "\\\\"); break;
        case '\n': sb_add(s, "\\n"); break;
        case '\r': sb_add(s, "\\r"); break;
        case '\t': sb_add(s, "\\t"); break;
        default:   sb_addc(s, *p); break;
        }
    }
    sb_addc(s, '"');
}

static char *type_to_alloc_str(TypeExpr *te) {
    if (!te) return NULL;
    SB tmp; sb_init(&tmp);
    type_to_str(&tmp, te);
    return tmp.data;
}

static void gen_json(SB *s, Node *program, const char *filename,
                     DeclList *fns, DeclList *structs, DeclList *enums,
                     DeclList *traits, DeclList *impls, DeclList *consts,
                     DeclList *aliases, DeclList *effects) {
    (void)program;
    sb_add(s, "{\n");
    sb_add(s, "  \"module\": ");
    json_escape(s, filename ? filename : "<unknown>");
    sb_add(s, ",\n");

    /* Functions */
    sb_add(s, "  \"functions\": [");
    for (int i = 0; i < fns->len; i++) {
        Node *n = fns->items[i];
        if (i > 0) sb_addc(s, ',');
        sb_add(s, "\n    {\n");
        sb_add(s, "      \"name\": ");
        json_escape(s, n->fn_decl.name);
        sb_add(s, ",\n      \"is_pub\": ");
        sb_add(s, n->fn_decl.is_pub ? "true" : "false");
        sb_add(s, ",\n      \"is_async\": ");
        sb_add(s, n->fn_decl.is_async ? "true" : "false");
        sb_add(s, ",\n      \"parameters\": [");
        for (int j = 0; j < n->fn_decl.params.len; j++) {
            Param *p = &n->fn_decl.params.items[j];
            if (j > 0) sb_addc(s, ',');
            sb_add(s, "\n        {\"name\": ");
            json_escape(s, p->name);
            sb_add(s, ", \"type\": ");
            char *tstr = type_to_alloc_str(p->type_ann);
            json_escape(s, tstr);
            free(tstr);
            sb_addc(s, '}');
        }
        if (n->fn_decl.params.len > 0) sb_add(s, "\n      ");
        sb_add(s, "],\n      \"return_type\": ");
        char *ret = type_to_alloc_str(n->fn_decl.ret_type);
        json_escape(s, ret);
        free(ret);
        sb_add(s, "\n    }");
    }
    if (fns->len > 0) sb_add(s, "\n  ");
    sb_add(s, "],\n");

    /* Types (structs) */
    sb_add(s, "  \"types\": [");
    for (int i = 0; i < structs->len; i++) {
        Node *n = structs->items[i];
        if (i > 0) sb_addc(s, ',');
        sb_add(s, "\n    {\n");
        sb_add(s, "      \"kind\": \"struct\",\n");
        sb_add(s, "      \"name\": ");
        json_escape(s, n->struct_decl.name);
        sb_add(s, ",\n      \"fields\": [");
        for (int j = 0; j < n->struct_decl.fields.len; j++) {
            if (j > 0) sb_addc(s, ',');
            sb_add(s, "\n        ");
            json_escape(s, n->struct_decl.fields.items[j].key);
        }
        if (n->struct_decl.fields.len > 0) sb_add(s, "\n      ");
        sb_add(s, "]\n    }");
    }
    for (int i = 0; i < enums->len; i++) {
        if (i > 0 || structs->len > 0) sb_addc(s, ',');
        Node *n = enums->items[i];
        sb_add(s, "\n    {\n");
        sb_add(s, "      \"kind\": \"enum\",\n");
        sb_add(s, "      \"name\": ");
        json_escape(s, n->enum_decl.name);
        sb_add(s, ",\n      \"variants\": [");
        for (int j = 0; j < n->enum_decl.variants.len; j++) {
            if (j > 0) sb_addc(s, ',');
            sb_add(s, "\n        ");
            json_escape(s, n->enum_decl.variants.items[j].name);
        }
        if (n->enum_decl.variants.len > 0) sb_add(s, "\n      ");
        sb_add(s, "]\n    }");
    }
    for (int i = 0; i < aliases->len; i++) {
        if (i > 0 || structs->len > 0 || enums->len > 0) sb_addc(s, ',');
        Node *n = aliases->items[i];
        sb_add(s, "\n    {\n");
        sb_add(s, "      \"kind\": \"alias\",\n");
        sb_add(s, "      \"name\": ");
        json_escape(s, n->type_alias.name);
        sb_add(s, ",\n      \"target\": ");
        json_escape(s, n->type_alias.target);
        sb_add(s, "\n    }");
    }
    if (structs->len > 0 || enums->len > 0 || aliases->len > 0)
        sb_add(s, "\n  ");
    sb_add(s, "],\n");

    /* Modules (traits + impls) */
    sb_add(s, "  \"modules\": [");
    for (int i = 0; i < traits->len; i++) {
        Node *n = traits->items[i];
        if (i > 0) sb_addc(s, ',');
        sb_add(s, "\n    {\n");
        sb_add(s, "      \"kind\": \"trait\",\n");
        sb_add(s, "      \"name\": ");
        json_escape(s, n->trait_decl.name);
        sb_add(s, ",\n      \"methods\": [");
        for (int j = 0; j < n->trait_decl.n_methods; j++) {
            if (j > 0) sb_addc(s, ',');
            sb_add(s, "\n        ");
            json_escape(s, n->trait_decl.method_names[j]);
        }
        if (n->trait_decl.n_methods > 0) sb_add(s, "\n      ");
        sb_add(s, "]\n    }");
    }
    for (int i = 0; i < impls->len; i++) {
        if (i > 0 || traits->len > 0) sb_addc(s, ',');
        Node *n = impls->items[i];
        sb_add(s, "\n    {\n");
        sb_add(s, "      \"kind\": \"impl\",\n");
        sb_add(s, "      \"type_name\": ");
        json_escape(s, n->impl_decl.type_name);
        if (n->impl_decl.trait_name) {
            sb_add(s, ",\n      \"trait_name\": ");
            json_escape(s, n->impl_decl.trait_name);
        }
        sb_add(s, ",\n      \"members\": [");
        int first = 1;
        for (int j = 0; j < n->impl_decl.members.len; j++) {
            Node *m = n->impl_decl.members.items[j];
            if (m && m->tag == NODE_FN_DECL) {
                if (!first) sb_addc(s, ',');
                sb_add(s, "\n        ");
                json_escape(s, m->fn_decl.name);
                first = 0;
            }
        }
        if (!first) sb_add(s, "\n      ");
        sb_add(s, "]\n    }");
    }
    for (int i = 0; i < effects->len; i++) {
        if (i > 0 || traits->len > 0 || impls->len > 0) sb_addc(s, ',');
        Node *n = effects->items[i];
        sb_add(s, "\n    {\n");
        sb_add(s, "      \"kind\": \"effect\",\n");
        sb_add(s, "      \"name\": ");
        json_escape(s, n->effect_decl.name);
        sb_add(s, ",\n      \"operations\": [");
        int first = 1;
        for (int j = 0; j < n->effect_decl.ops.len; j++) {
            Node *op = n->effect_decl.ops.items[j];
            if (op && op->tag == NODE_FN_DECL) {
                if (!first) sb_addc(s, ',');
                sb_add(s, "\n        ");
                json_escape(s, op->fn_decl.name);
                first = 0;
            }
        }
        if (!first) sb_add(s, "\n      ");
        sb_add(s, "]\n    }");
    }
    if (traits->len > 0 || impls->len > 0 || effects->len > 0)
        sb_add(s, "\n  ");
    sb_add(s, "],\n");

    /* Constants */
    sb_add(s, "  \"constants\": [");
    for (int i = 0; i < consts->len; i++) {
        if (i > 0) sb_addc(s, ',');
        sb_add(s, "\n    ");
        json_escape(s, consts->items[i]->const_.name);
    }
    if (consts->len > 0) sb_add(s, "\n  ");
    sb_add(s, "]\n}\n");
}

/* HTML */
static void gen_html(SB *s, Node *program, const char *filename,
                     DeclList *fns, DeclList *structs, DeclList *enums,
                     DeclList *traits, DeclList *impls, DeclList *consts,
                     DeclList *aliases, DeclList *effects) {
    (void)program;
    sb_add(s, "<html>\n<head>\n<style>\n");
    sb_add(s, "body { font-family: sans-serif; max-width: 800px; margin: 0 auto; padding: 20px; }\n");
    sb_add(s, "code { background: #f4f4f4; padding: 2px 6px; border-radius: 3px; }\n");
    sb_add(s, "h1 { border-bottom: 2px solid #333; }\n");
    sb_add(s, "h2 { color: #444; }\n");
    sb_add(s, "h3 { color: #666; }\n");
    sb_add(s, "</style>\n</head>\n<body>\n");

    sb_printf(s, "<h1>Module: %s</h1>\n", filename ? filename : "<unknown>");

    if (fns->len > 0) {
        sb_add(s, "<h2>Functions</h2>\n");
        for (int i = 0; i < fns->len; i++) {
            Node *n = fns->items[i];
            sb_add(s, "<h3><code>");
            if (n->fn_decl.is_pub) sb_add(s, "pub ");
            sb_printf(s, "fn %s(", n->fn_decl.name);
            for (int j = 0; j < n->fn_decl.params.len; j++) {
                if (j > 0) sb_add(s, ", ");
                Param *p = &n->fn_decl.params.items[j];
                if (p->name) sb_add(s, p->name);
            }
            sb_add(s, ")");
            if (n->fn_decl.ret_type) {
                sb_add(s, " -&gt; ");
                type_to_str(s, n->fn_decl.ret_type);
            }
            sb_add(s, "</code></h3>\n");
        }
    }

    if (structs->len > 0) {
        sb_add(s, "<h2>Structs</h2>\n");
        for (int i = 0; i < structs->len; i++) {
            Node *n = structs->items[i];
            sb_printf(s, "<h3><code>struct %s</code></h3>\n", n->struct_decl.name);
            if (n->struct_decl.fields.len > 0) {
                sb_add(s, "<p><strong>Fields:</strong></p>\n<ul>\n");
                for (int j = 0; j < n->struct_decl.fields.len; j++) {
                    sb_printf(s, "<li><code>%s</code></li>\n", n->struct_decl.fields.items[j].key);
                }
                sb_add(s, "</ul>\n");
            }
        }
    }

    if (enums->len > 0) {
        sb_add(s, "<h2>Enums</h2>\n");
        for (int i = 0; i < enums->len; i++) {
            Node *n = enums->items[i];
            sb_printf(s, "<h3><code>enum %s</code></h3>\n", n->enum_decl.name);
            if (n->enum_decl.variants.len > 0) {
                sb_add(s, "<p><strong>Variants:</strong></p>\n<ul>\n");
                for (int j = 0; j < n->enum_decl.variants.len; j++) {
                    sb_printf(s, "<li><code>%s</code></li>\n", n->enum_decl.variants.items[j].name);
                }
                sb_add(s, "</ul>\n");
            }
        }
    }

    if (traits->len > 0) {
        sb_add(s, "<h2>Traits</h2>\n");
        for (int i = 0; i < traits->len; i++) {
            sb_printf(s, "<h3><code>trait %s</code></h3>\n", traits->items[i]->trait_decl.name);
        }
    }

    if (impls->len > 0) {
        sb_add(s, "<h2>Implementations</h2>\n");
        for (int i = 0; i < impls->len; i++) {
            Node *n = impls->items[i];
            sb_add(s, "<h3><code>impl ");
            if (n->impl_decl.trait_name) {
                sb_add(s, n->impl_decl.trait_name);
                sb_add(s, " for ");
            }
            sb_add(s, n->impl_decl.type_name);
            sb_add(s, "</code></h3>\n");
            if (n->impl_decl.members.len > 0) {
                sb_add(s, "<p><strong>Methods:</strong></p>\n<ul>\n");
                for (int j = 0; j < n->impl_decl.members.len; j++) {
                    Node *m = n->impl_decl.members.items[j];
                    if (m && m->tag == NODE_FN_DECL) {
                        sb_printf(s, "<li><code>fn %s(", m->fn_decl.name);
                        for (int k = 0; k < m->fn_decl.params.len; k++) {
                            if (k > 0) sb_add(s, ", ");
                            Param *p = &m->fn_decl.params.items[k];
                            if (p->name) sb_add(s, p->name);
                        }
                        sb_add(s, ")");
                        if (m->fn_decl.ret_type) {
                            sb_add(s, " -&gt; ");
                            type_to_str(s, m->fn_decl.ret_type);
                        }
                        sb_add(s, "</code></li>\n");
                    }
                }
                sb_add(s, "</ul>\n");
            }
        }
    }

    if (consts->len > 0) {
        sb_add(s, "<h2>Constants</h2>\n");
        for (int i = 0; i < consts->len; i++) {
            sb_printf(s, "<h3><code>const %s</code></h3>\n", consts->items[i]->const_.name);
        }
    }

    if (aliases->len > 0) {
        sb_add(s, "<h2>Type Aliases</h2>\n");
        for (int i = 0; i < aliases->len; i++) {
            Node *n = aliases->items[i];
            sb_printf(s, "<h3><code>type %s = %s</code></h3>\n",
                      n->type_alias.name, n->type_alias.target);
        }
    }

    if (effects->len > 0) {
        sb_add(s, "<h2>Effects</h2>\n");
        for (int i = 0; i < effects->len; i++) {
            sb_printf(s, "<h3><code>effect %s</code></h3>\n", effects->items[i]->effect_decl.name);
        }
    }

    sb_add(s, "</body>\n</html>\n");
}

char *docgen_generate(Node *program, const char *filename, const char *format) {
    if (!program || program->tag != NODE_PROGRAM) {
        return xs_strdup("<!-- empty module -->\n");
    }

    DeclList fns, structs, enums, traits, impls, consts, aliases, effects;
    dl_init(&fns); dl_init(&structs); dl_init(&enums); dl_init(&traits);
    dl_init(&impls); dl_init(&consts); dl_init(&aliases); dl_init(&effects);

    collect_decls(program, &fns, &structs, &enums, &traits, &impls,
                  &consts, &aliases, &effects);

    SB sb;
    sb_init(&sb);

    if (format && strcmp(format, "html") == 0) {
        gen_html(&sb, program, filename, &fns, &structs, &enums,
                 &traits, &impls, &consts, &aliases, &effects);
    } else if (format && strcmp(format, "json") == 0) {
        gen_json(&sb, program, filename, &fns, &structs, &enums,
                 &traits, &impls, &consts, &aliases, &effects);
    } else {
        gen_markdown(&sb, program, filename, &fns, &structs, &enums,
                     &traits, &impls, &consts, &aliases, &effects);
    }

    dl_free(&fns); dl_free(&structs); dl_free(&enums); dl_free(&traits);
    dl_free(&impls); dl_free(&consts); dl_free(&aliases); dl_free(&effects);

    if (!sb.data) return xs_strdup("");
    return sb.data;
}
