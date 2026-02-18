#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "core/xs_compat.h"
#include "runtime/interp.h"
#include "runtime/error.h"
#include "core/lexer.h"
#include "core/parser.h"
#include "core/xs_bigint.h"
#include "core/xs_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <time.h>
#include <limits.h>
#include <sys/stat.h>

Interp *g_current_interp = NULL;

/* forward decl for plugin system */
Value *call_value(Interp *i, Value *callee, Value **args, int argc,
                  const char *call_site);

/* ── plugin system globals ── */

#define MAX_PLUGIN_METHODS 256
typedef struct {
    char  *type_name;
    char  *method_name;
    Value *fn;
} PluginMethod;

static PluginMethod g_plugin_methods[MAX_PLUGIN_METHODS];
static int g_plugin_method_count = 0;

#define MAX_TEARDOWN_FNS 64
static Value *g_teardown_fns[MAX_TEARDOWN_FNS];
static int g_teardown_count = 0;

#define MAX_LOADED_XS_PLUGINS 64
typedef struct {
    char *name;
    char *version;
    char *source;    /* kept alive so AST nodes remain valid */
    char *filepath;  /* kept alive for span references */
    Node *program;   /* kept alive so function bodies remain valid */
} LoadedXSPlugin;
static LoadedXSPlugin g_xs_plugins[MAX_LOADED_XS_PLUGINS];
static int g_xs_plugin_count = 0;

static void plugin_register_method(const char *type_name, const char *method_name, Value *fn) {
    if (g_plugin_method_count >= MAX_PLUGIN_METHODS) return;
    PluginMethod *pm = &g_plugin_methods[g_plugin_method_count++];
    pm->type_name = xs_strdup(type_name);
    pm->method_name = xs_strdup(method_name);
    pm->fn = value_incref(fn);
}

static Value *plugin_lookup_method(const char *type_name, const char *method_name) {
    for (int j = 0; j < g_plugin_method_count; j++) {
        if (strcmp(g_plugin_methods[j].type_name, type_name) == 0 &&
            strcmp(g_plugin_methods[j].method_name, method_name) == 0) {
            return g_plugin_methods[j].fn;
        }
    }
    return NULL;
}

static void plugin_run_teardowns(void) {
    for (int j = 0; j < g_teardown_count; j++) {
        if (g_teardown_fns[j] && g_current_interp) {
            Value *r = call_value(g_current_interp, g_teardown_fns[j], NULL, 0, "teardown");
            if (r) value_decref(r);
        }
    }
}

static void plugin_register_loaded(const char *name, const char *version) {
    if (g_xs_plugin_count >= MAX_LOADED_XS_PLUGINS) return;
    g_xs_plugins[g_xs_plugin_count].name = xs_strdup(name);
    g_xs_plugins[g_xs_plugin_count].version = version ? xs_strdup(version) : NULL;
    g_xs_plugin_count++;
}

static int plugin_is_loaded(const char *name) {
    for (int j = 0; j < g_xs_plugin_count; j++) {
        if (g_xs_plugins[j].name && strcmp(g_xs_plugins[j].name, name) == 0)
            return 1;
    }
    return 0;
}

/* ── phase 2: eval hooks ── */

typedef struct {
    Value *callback;
    int    tag_filter; /* -1 = all tags, otherwise specific NodeTag */
} EvalHook;

static EvalHook g_before_eval[64];
static int g_n_before_eval = 0;
static EvalHook g_after_eval[64];
static int g_n_after_eval = 0;
static int g_has_eval_hooks = 0;
static int g_in_eval_hook = 0; /* recursion guard */

/* ── phase 2: syntax extension globals ── */

static Value *g_syntax_handlers[16];
static int g_n_syntax_handlers = 0;
static Value *g_syntax_expr_handlers[16];
static int g_n_syntax_expr_handlers = 0;
static Value *g_postfix_handlers[16];
static int g_n_postfix_handlers = 0;

/* plugin keyword registry for lexer */
#define MAX_PLUGIN_KEYWORDS 64
static char *g_plugin_keywords[MAX_PLUGIN_KEYWORDS];
static int g_n_plugin_keywords = 0;

/* global interp pointer for use by parser-invoked plugin callbacks */
static Interp *g_plugin_interp = NULL;

/* forward decls for parser access from plugin callbacks */
struct Parser; /* already defined in parser.h but we need it here */

/* forward decls for phase 2 functions used in interp_eval */
static Value *node_to_xs_map(Node *n);
static Node  *node_from_xs_map(Value *map);
static const char *node_tag_to_string(NodeTag tag);
static int plugin_is_keyword_impl(const char *word);
static Node *plugin_try_syntax_handler_impl(Parser *p, Token *tok);
static Node *plugin_try_syntax_expr_handler_impl(Parser *p, Token *tok);

/* ── end plugin globals ── */

/* enum variant constructor registry */
typedef struct {
    char *type_name;
    char *variant_name;
} EnumCtorInfo;

static EnumCtorInfo enum_ctor_registry[256];
static int enum_ctor_count = 0;

static Value *enum_ctor_fn(Interp *interp, Value **args, int argc, int slot) {
    (void)interp;
    EnumCtorInfo *info = &enum_ctor_registry[slot];
    XSEnum *en = xs_calloc(1, sizeof(XSEnum));
    en->type_name = xs_strdup(info->type_name);
    en->variant   = xs_strdup(info->variant_name);
    en->arr_data  = array_new();
    en->refcount  = 1;
    for (int k = 0; k < argc; k++)
        array_push(en->arr_data, value_incref(args[k]));
    Value *ev = xs_calloc(1, sizeof(Value));
    ev->tag = XS_ENUM_VAL; ev->refcount = 1; ev->en = en;
    return ev;
}

/*
 * Each enum variant needs a unique C function pointer so we can wrap it
 * as a NativeFn. We use an X-macro to stamp them out — ugly but it's the
 * only way to get distinct function addresses without runtime codegen.
 */
#define ENUM_SLOTS \
    X(0) X(1) X(2) X(3) X(4) X(5) X(6) X(7) \
    X(8) X(9) X(10) X(11) X(12) X(13) X(14) X(15) \
    X(16) X(17) X(18) X(19) X(20) X(21) X(22) X(23) \
    X(24) X(25) X(26) X(27) X(28) X(29) X(30) X(31) \
    X(32) X(33) X(34) X(35) X(36) X(37) X(38) X(39) \
    X(40) X(41) X(42) X(43) X(44) X(45) X(46) X(47) \
    X(48) X(49) X(50) X(51) X(52) X(53) X(54) X(55) \
    X(56) X(57) X(58) X(59) X(60) X(61) X(62) X(63) \
    X(64) X(65) X(66) X(67) X(68) X(69) X(70) X(71) \
    X(72) X(73) X(74) X(75) X(76) X(77) X(78) X(79) \
    X(80) X(81) X(82) X(83) X(84) X(85) X(86) X(87) \
    X(88) X(89) X(90) X(91) X(92) X(93) X(94) X(95) \
    X(96) X(97) X(98) X(99) X(100) X(101) X(102) X(103) \
    X(104) X(105) X(106) X(107) X(108) X(109) X(110) X(111) \
    X(112) X(113) X(114) X(115) X(116) X(117) X(118) X(119) \
    X(120) X(121) X(122) X(123) X(124) X(125) X(126) X(127) \
    X(128) X(129) X(130) X(131) X(132) X(133) X(134) X(135) \
    X(136) X(137) X(138) X(139) X(140) X(141) X(142) X(143) \
    X(144) X(145) X(146) X(147) X(148) X(149) X(150) X(151) \
    X(152) X(153) X(154) X(155) X(156) X(157) X(158) X(159) \
    X(160) X(161) X(162) X(163) X(164) X(165) X(166) X(167) \
    X(168) X(169) X(170) X(171) X(172) X(173) X(174) X(175) \
    X(176) X(177) X(178) X(179) X(180) X(181) X(182) X(183) \
    X(184) X(185) X(186) X(187) X(188) X(189) X(190) X(191) \
    X(192) X(193) X(194) X(195) X(196) X(197) X(198) X(199) \
    X(200) X(201) X(202) X(203) X(204) X(205) X(206) X(207) \
    X(208) X(209) X(210) X(211) X(212) X(213) X(214) X(215) \
    X(216) X(217) X(218) X(219) X(220) X(221) X(222) X(223) \
    X(224) X(225) X(226) X(227) X(228) X(229) X(230) X(231) \
    X(232) X(233) X(234) X(235) X(236) X(237) X(238) X(239) \
    X(240) X(241) X(242) X(243) X(244) X(245) X(246) X(247) \
    X(248) X(249) X(250) X(251) X(252) X(253) X(254) X(255)

#define X(N) static Value *enum_ctor_##N(Interp *i, Value **a, int c) \
    { return enum_ctor_fn(i, a, c, N); }
ENUM_SLOTS
#undef X

#define X(N) enum_ctor_##N,
static NativeFn enum_ctor_table[256] = { ENUM_SLOTS };
#undef X

static Value *make_enum_ctor_native(const char *type_name, const char *variant_name) {
    if (enum_ctor_count >= 256) {
        fprintf(stderr, "xs: too many enum ctors (max 256)\n");
        return xs_native(enum_ctor_table[0]);
    }
    int slot = enum_ctor_count++;
    enum_ctor_registry[slot].type_name    = xs_strdup(type_name);
    enum_ctor_registry[slot].variant_name = xs_strdup(variant_name);
    return xs_native(enum_ctor_table[slot]);
}

/* derive builtins (Debug, Display, etc) */

static Value *builtin_debug_to_string(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1) return xs_str("<debug>");
    Value *self = args[0];
    char *repr = value_repr(self);
    Value *result = xs_str(repr);
    free(repr);
    return result;
}

static Value *builtin_clone(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1 || args[0]->tag != XS_INST) return value_incref(XS_NULL_VAL);
    Value *self = args[0];
    XSInst *inst = xs_calloc(1, sizeof(XSInst));
    inst->class_ = self->inst->class_;
    if (inst->class_) inst->class_->refcount++;
    inst->fields = map_new();
    inst->methods = map_new();
    inst->refcount = 1;
    if (self->inst->fields) {
        int nk = 0; char **ks = map_keys(self->inst->fields, &nk);
        for (int j = 0; j < nk; j++) {
            Value *fv = map_get(self->inst->fields, ks[j]);
            if (fv) map_set(inst->fields, ks[j], value_incref(fv));
            free(ks[j]);
        }
        free(ks);
    }
    if (self->inst->methods) {
        int nk = 0; char **ks = map_keys(self->inst->methods, &nk);
        for (int j = 0; j < nk; j++) {
            Value *mv = map_get(self->inst->methods, ks[j]);
            if (mv) map_set(inst->methods, ks[j], value_incref(mv));
            free(ks[j]);
        }
        free(ks);
    }
    Value *result = xs_calloc(1, sizeof(Value));
    result->tag = XS_INST; result->refcount = 1;
    result->inst = inst;
    return result;
}

static Value *builtin_struct_eq(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 2) return value_incref(XS_FALSE_VAL);
    Value *self = args[0], *other = args[1];
    if (self->tag != XS_INST || other->tag != XS_INST) return value_incref(XS_FALSE_VAL);
    if (self->inst->class_ != other->inst->class_) return value_incref(XS_FALSE_VAL);
    if (!self->inst->fields || !other->inst->fields) return value_incref(XS_FALSE_VAL);
    int nk = 0; char **ks = map_keys(self->inst->fields, &nk);
    int eq = 1;
    for (int j = 0; j < nk; j++) {
        Value *a = map_get(self->inst->fields, ks[j]);
        Value *b = map_get(other->inst->fields, ks[j]);
        if (!b || !value_equal(a, b)) { eq = 0; }
        free(ks[j]);
    }
    free(ks);
    return eq ? value_incref(XS_TRUE_VAL) : value_incref(XS_FALSE_VAL);
}

#define CF_CLEAR(i)  do { (i)->cf.signal=0; value_decref((i)->cf.value); (i)->cf.value=NULL; free((i)->cf.label); (i)->cf.label=NULL; } while(0)

static void hoist_functions(Interp *i, NodeList *stmts);
static Value *EVAL(Interp *i, Node *n) {
    if (!n || i->cf.signal) return value_incref(XS_NULL_VAL);
    return interp_eval(i, n);
}

static void push_env(Interp *i) {
    Env *child = env_new(i->env);
    i->env = child;
}

static void pop_env(Interp *i) {
    Env *old = i->env;
    i->env = old->parent ? env_incref(old->parent) : NULL;
    env_decref(old);
}

static const char *find_similar_name(Env *env, const char *name) {
    const char *best = NULL;
    int best_dist = 3;
    for (Env *e = env; e != NULL; e = e->parent) {
        for (int j = 0; j < e->len; j++) {
            if (!e->bindings[j].name) continue;
            int d = xs_edit_distance(name, e->bindings[j].name);
            if (d > 0 && d < best_dist) {
                best_dist = d;
                best = e->bindings[j].name;
            }
        }
    }
    return best;
}

/* runtime type annotation checking */

static int value_matches_type(Value *v, const char *type_name) {
    if (!type_name || !v) return 1;
    if (strcmp(type_name, "any") == 0) return 1;
    if (strcmp(type_name, "int") == 0 || strcmp(type_name, "i64") == 0)
        return v->tag == XS_INT;
    if (strcmp(type_name, "float") == 0 || strcmp(type_name, "f64") == 0)
        return v->tag == XS_FLOAT;
    if (strcmp(type_name, "str") == 0 || strcmp(type_name, "string") == 0)
        return v->tag == XS_STR;
    if (strcmp(type_name, "bool") == 0)
        return v->tag == XS_BOOL;
    if (strcmp(type_name, "array") == 0)
        return v->tag == XS_ARRAY;
    if (strcmp(type_name, "map") == 0)
        return v->tag == XS_MAP;
    if (strcmp(type_name, "null") == 0)
        return v->tag == XS_NULL;
    if (strcmp(type_name, "fn") == 0 || strcmp(type_name, "function") == 0)
        return v->tag == XS_FUNC || v->tag == XS_NATIVE;
    if (strcmp(type_name, "tuple") == 0)
        return v->tag == XS_TUPLE;
    if (v->tag == XS_STRUCT_VAL && v->st)
        return strcmp(v->st->type_name, type_name) == 0;
    if (v->tag == XS_INST && v->inst && v->inst->class_)
        return strcmp(v->inst->class_->name, type_name) == 0;
    if (v->tag == XS_ENUM_VAL && v->en)
        return strcmp(v->en->type_name, type_name) == 0;
    return 1; /* unknown type = pass */
}

static const char *value_type_str(Value *v) {
    if (!v) return "null";
    switch (v->tag) {
        case XS_INT:   return "int";
        case XS_FLOAT: return "float";
        case XS_STR:   return "str";
        case XS_BOOL:  return "bool";
        case XS_ARRAY: return "array";
        case XS_MAP:   return "map";
        case XS_NULL:  return "null";
        case XS_FUNC: case XS_NATIVE: return "fn";
        case XS_TUPLE: return "tuple";
        case XS_STRUCT_VAL: return v->st ? v->st->type_name : "struct";
        case XS_INST:  return (v->inst && v->inst->class_) ? v->inst->class_->name : "instance";
        case XS_ENUM_VAL: return v->en ? v->en->type_name : "enum";
        default: return "unknown";
    }
}

Interp *interp_new(const char *filename) {
    value_init_singletons();
    Interp *i = xs_calloc(1, sizeof(Interp));
    i->globals   = env_new(NULL);
    i->env       = env_incref(i->globals);
    i->max_depth = 1000;
    i->filename  = filename ? filename : "<stdin>";
    i->call_stack     = NULL;
    i->call_stack_len = 0;
    i->call_stack_cap = 0;
    i->diag           = NULL;
    i->n_tasks        = 0;
    i->source         = NULL;
    i->needs_reparse  = 0;
    /* phase 2: register plugin parser bridge functions */
    g_plugin_is_keyword = plugin_is_keyword_impl;
    g_plugin_try_syntax_handler = plugin_try_syntax_handler_impl;
    g_plugin_try_syntax_expr_handler = plugin_try_syntax_expr_handler_impl;
    stdlib_register(i);
    return i;
}

void interp_free(Interp *i) {
    if (!i) return;
    /* run plugin teardown callbacks only for the main interpreter */
    if (i == g_current_interp) {
        plugin_run_teardowns();
    }
    CF_CLEAR(i);
    env_decref(i->env);
    env_decref(i->globals);
    free(i->defers.items);
    while (i->effect_stack) {
        EffectFrame *frame = i->effect_stack;
        i->effect_stack = frame->prev;
        free(frame->effect_name);
        free(frame->op_name);
        env_decref(frame->handler_env);
        free(frame);
    }
    if (i->resume_value) value_decref(i->resume_value);
    for (int t = 0; t < i->n_tasks; t++) {
        if (i->task_queue[t].fn)     value_decref(i->task_queue[t].fn);
        if (i->task_queue[t].result) value_decref(i->task_queue[t].result);
    }
    i->n_tasks = 0;
    free(i->call_stack);
    free(i);
}

void interp_define_native(Interp *i, const char *name, NativeFn fn) {
    Value *v = xs_native(fn);
    env_define(i->globals, name, v, 1);
    value_decref(v);
}

static Value *eval_binop(Interp *i, Node *n);
Value *call_value(Interp *i, Value *callee, Value **args, int argc,
                          const char *call_site);
static int match_pattern(Interp *i, Node *pat, Value *val, Env *env);
static void hoist_functions(Interp *i, NodeList *stmts);

static int bind_pattern(Interp *i, Node *pat, Value *val, Env *env, int mutable) {
    if (!pat) return 1;
    switch (pat->tag) {
    case NODE_PAT_WILD: return 1;
    case NODE_PAT_IDENT:
        env_define(env, pat->pat_ident.name, val,
                   pat->pat_ident.mutable || mutable);
        return 1;
    case NODE_PAT_LIT: return 1;
    case NODE_PAT_TUPLE: {
        if (val->tag != XS_ARRAY && val->tag != XS_TUPLE) return 0;
        XSArray *arr = val->arr;
        if (pat->pat_tuple.elems.len != arr->len) return 0;
        for (int j = 0; j < pat->pat_tuple.elems.len; j++) {
            if (!bind_pattern(i, pat->pat_tuple.elems.items[j],
                              arr->items[j], env, mutable)) return 0;
        }
        return 1;
    }
    case NODE_PAT_STRUCT: {
        XSMap *m = NULL;
        if (val->tag == XS_MAP) m = val->map;
        else if (val->tag == XS_STRUCT_VAL) m = val->st->fields;
        else if (val->tag == XS_ENUM_VAL) m = val->en->map_data;
        else if (val->tag == XS_INST) m = val->inst->fields;
        if (!m) return 0;
        for (int j = 0; j < pat->pat_struct.fields.len; j++) {
            char *fname = pat->pat_struct.fields.items[j].key;
            Node *sub   = pat->pat_struct.fields.items[j].val;
            Value *fv   = map_get(m, fname);
            if ((!fv || fv->tag == XS_NULL) &&
                j < pat->pat_struct.defaults.len &&
                pat->pat_struct.defaults.items[j]) {
                fv = EVAL(i, pat->pat_struct.defaults.items[j]);
                if (sub) {
                    int ok = bind_pattern(i, sub, fv, env, mutable);
                    value_decref(fv);
                    if (!ok) return 0;
                } else {
                    env_define(env, fname, fv, mutable);
                    value_decref(fv);
                }
                continue;
            }
            if (!fv) fv = XS_NULL_VAL;
            if (sub) { if (!bind_pattern(i, sub, fv, env, mutable)) return 0; }
            else env_define(env, fname, fv, mutable);
        }
        return 1;
    }
    case NODE_PAT_CAPTURE: {
        if (!bind_pattern(i, pat->pat_capture.pattern, val, env, mutable)) return 0;
        env_define(env, pat->pat_capture.name, val, mutable);
        return 1;
    }
    case NODE_PAT_ENUM: {
        if (val->tag != XS_ENUM_VAL && val->tag != XS_ARRAY) return 0;
        XSArray *arr = NULL;
        if (val->tag == XS_ENUM_VAL && val->en->arr_data) arr = val->en->arr_data;
        else if (val->tag == XS_ARRAY) arr = val->arr;
        if (!arr) {
                return (pat->pat_enum.args.len == 0);
        }
        if (pat->pat_enum.args.len != arr->len) return 0;
        for (int j = 0; j < pat->pat_enum.args.len; j++) {
            if (!bind_pattern(i, pat->pat_enum.args.items[j],
                              arr->items[j], env, mutable)) return 0;
        }
        return 1;
    }
    case NODE_PAT_SLICE: {
        if (val->tag != XS_ARRAY && val->tag != XS_TUPLE) return 0;
        XSArray *arr = val->arr;
        int nfixed = pat->pat_slice.elems.len;
        if (!pat->pat_slice.rest && arr->len != nfixed) return 0;
        if (pat->pat_slice.rest  && arr->len < nfixed)  return 0;
        for (int j = 0; j < nfixed; j++) {
            if (!bind_pattern(i, pat->pat_slice.elems.items[j],
                              arr->items[j], env, mutable)) return 0;
        }
        if (pat->pat_slice.rest) {
            Value *rest_arr = xs_array_new();
            for (int j = nfixed; j < arr->len; j++)
                array_push(rest_arr->arr, value_incref(arr->items[j]));
            env_define(env, pat->pat_slice.rest, rest_arr, 1);
            value_decref(rest_arr);
        }
        return 1;
    }
    default: return 1;
    }
}

static int match_pattern(Interp *i, Node *pat, Value *val, Env *env) {
    if (!pat) return 1;
    switch (pat->tag) {
    case NODE_PAT_WILD: return 1;
    case NODE_PAT_IDENT:
        if (env) env_define(env, pat->pat_ident.name, val,
                             pat->pat_ident.mutable);
        return 1;
    case NODE_PAT_LIT: {
        switch (pat->pat_lit.tag) {
        case 0: return val->tag == XS_INT && val->i == pat->pat_lit.ival;
        case 1: return val->tag == XS_FLOAT && val->f == pat->pat_lit.fval;
        case 2: return val->tag == XS_STR && strcmp(val->s, pat->pat_lit.sval) == 0;
        case 3: return val->tag == XS_BOOL && (int)val->i == pat->pat_lit.bval;
        case 4: return val->tag == XS_NULL;
        default: return 0;
        }
    }
    case NODE_PAT_TUPLE: {
        if (val->tag != XS_ARRAY && val->tag != XS_TUPLE) return 0;
        if (val->arr->len != pat->pat_tuple.elems.len) return 0;
        for (int j = 0; j < pat->pat_tuple.elems.len; j++) {
            if (!match_pattern(i, pat->pat_tuple.elems.items[j],
                               val->arr->items[j], env)) return 0;
        }
        return 1;
    }
    case NODE_PAT_ENUM: {
        const char *path = pat->pat_enum.path;
        const char *variant = path;
        const char *sep = strstr(path, "::");
        while (sep) { variant = sep + 2; sep = strstr(variant, "::"); }

        if (val->tag == XS_ENUM_VAL) {
            if (strcmp(val->en->variant, variant) != 0) return 0;
            XSArray *arr = val->en->arr_data;
            if (pat->pat_enum.args.len == 0) return 1;
            if (!arr || arr->len != pat->pat_enum.args.len) return 0;
            for (int j = 0; j < pat->pat_enum.args.len; j++) {
                if (!match_pattern(i, pat->pat_enum.args.items[j],
                                   arr->items[j], env)) return 0;
            }
            return 1;
        }
        if (val->tag == XS_STR && strcmp(val->s, variant) == 0)
            return pat->pat_enum.args.len == 0;
        return 0;
    }
    case NODE_PAT_STRUCT: {
        XSMap *m = NULL;
        const char *type_name = pat->pat_struct.path;
        if (val->tag == XS_MAP) m = val->map;
        else if (val->tag == XS_STRUCT_VAL) {
            if (type_name && strcmp(val->st->type_name, type_name) != 0) return 0;
            m = val->st->fields;
        } else if (val->tag == XS_ENUM_VAL) {
            if (type_name) {
                const char *variant = type_name;
                const char *sep = strstr(type_name, "::");
                while (sep) { variant = sep+2; sep = strstr(variant,"::"); }
                if (strcmp(val->en->variant, variant) != 0) return 0;
            }
            m = val->en->map_data;
        } else if (val->tag == XS_INST) {
            m = val->inst->fields;
        }
        if (!m) return 0;
        for (int j = 0; j < pat->pat_struct.fields.len; j++) {
            char *fname = pat->pat_struct.fields.items[j].key;
            Node *sub   = pat->pat_struct.fields.items[j].val;
            Value *fv   = map_get(m, fname);
            /* Apply default value if field is missing/null */
            if ((!fv || fv->tag == XS_NULL) &&
                j < pat->pat_struct.defaults.len &&
                pat->pat_struct.defaults.items[j]) {
                fv = EVAL(i, pat->pat_struct.defaults.items[j]);
                if (sub) {
                    int ok = match_pattern(i, sub, fv, env);
                    value_decref(fv);
                    if (!ok) return 0;
                } else if (env) {
                    env_define(env, fname, fv, 1);
                    value_decref(fv);
                }
                continue;
            }
            if (!fv) fv = XS_NULL_VAL;
            if (sub) { if (!match_pattern(i, sub, fv, env)) return 0; }
            else if (env) env_define(env, fname, fv, 1);
        }
        return 1;
    }
    case NODE_PAT_OR: {
        Env *tmp = env_new(NULL);
        int lm = match_pattern(i, pat->pat_or.left, val, tmp);
        if (lm) {
            /* copy bindings */
            for (int j = 0; j < tmp->len; j++)
                if (env) env_define(env, tmp->bindings[j].name,
                                    tmp->bindings[j].value, 1);
            env_decref(tmp);
            return 1;
        }
        /* reset and try right */
        env_decref(tmp);
        return match_pattern(i, pat->pat_or.right, val, env);
    }
    case NODE_PAT_RANGE: {
        Value *start = pat->pat_range.start ? EVAL(i, pat->pat_range.start) : NULL;
        Value *end   = pat->pat_range.end   ? EVAL(i, pat->pat_range.end)   : NULL;
        int ok = 1;
        if (start) {
            int cmp = value_cmp(val, start);
            ok = ok && (cmp >= 0);
            value_decref(start);
        }
        if (end) {
            int cmp = value_cmp(val, end);
            ok = ok && (pat->pat_range.inclusive ? cmp <= 0 : cmp < 0);
            value_decref(end);
        }
        return ok;
    }
    case NODE_PAT_SLICE: {
        if (val->tag != XS_ARRAY && val->tag != XS_TUPLE) return 0;
        XSArray *arr = val->arr;
        int nfixed = pat->pat_slice.elems.len;
        if (!pat->pat_slice.rest && arr->len != nfixed) return 0;
        if (pat->pat_slice.rest  && arr->len < nfixed)  return 0;
        for (int j = 0; j < nfixed; j++) {
            if (!match_pattern(i, pat->pat_slice.elems.items[j],
                               arr->items[j], env)) return 0;
        }
        if (env && pat->pat_slice.rest) {
            Value *rest_arr = xs_array_new();
            for (int j = nfixed; j < arr->len; j++)
                array_push(rest_arr->arr, value_incref(arr->items[j]));
            env_define(env, pat->pat_slice.rest, rest_arr, 1);
            value_decref(rest_arr);
        }
        return 1;
    }
    case NODE_PAT_EXPR: {
        Value *ev = EVAL(i, pat->pat_expr.expr);
        int eq = value_equal(ev, val);
        value_decref(ev);
        return eq;
    }
    case NODE_PAT_CAPTURE: {
        if (!match_pattern(i, pat->pat_capture.pattern, val, env)) return 0;
        if (env) env_define(env, pat->pat_capture.name, val, 0);
        return 1;
    }
    case NODE_PAT_GUARD: {
        if (!match_pattern(i, pat->pat_guard.pattern, val, env)) return 0;
        Value *g = EVAL(i, pat->pat_guard.guard);
        int ok = value_truthy(g);
        value_decref(g);
        return ok;
    }
    case NODE_PAT_STRING_CONCAT: {
        if (val->tag != XS_STR) return 0;
        const char *prefix = pat->pat_str_concat.prefix;
        size_t plen = strlen(prefix);
        if (strncmp(val->s, prefix, plen) != 0) return 0;
        /* Bind the rest of the string to the sub-pattern */
        Value *rest_val = xs_str(val->s + plen);
        int ok = match_pattern(i, pat->pat_str_concat.rest, rest_val, env);
        value_decref(rest_val);
        return ok;
    }
    default: return 1;
    }
}

static void interp_push_frame(Interp *i, const char *func_name, Span span) {
    if (i->call_stack_len >= i->call_stack_cap) {
        int newcap = i->call_stack_cap ? i->call_stack_cap * 2 : 16;
        i->call_stack = (InterpFrame *)xs_realloc(
            i->call_stack, (size_t)newcap * sizeof(InterpFrame));
        i->call_stack_cap = newcap;
    }
    InterpFrame *f = &i->call_stack[i->call_stack_len++];
    f->func_name = func_name;
    f->call_span = span;
}

static void interp_pop_frame(Interp *i) {
    if (i->call_stack_len > 0) i->call_stack_len--;
}

Value *call_value(Interp *i, Value *callee, Value **args, int argc,
                          const char *call_site) {
    if (!callee) return value_incref(XS_NULL_VAL);

    const char *frame_name = call_site;
    if (!frame_name && callee->tag == XS_FUNC && callee->fn->name)
        frame_name = callee->fn->name;
    interp_push_frame(i, frame_name, i->current_span);

    if (callee->tag == XS_NATIVE) {
        Value *result = callee->native(i, args, argc);
        interp_pop_frame(i);
        return result ? result : value_incref(XS_NULL_VAL);
    }

    if (callee->tag == XS_FUNC) {
        XSFunc *fn = callee->fn;
        if (fn->deprecated_msg) {
            fprintf(stderr, "xs: warning: function '%s' is deprecated: %s\n",
                    fn->name ? fn->name : "<anonymous>", fn->deprecated_msg);
        }
        if (i->depth >= i->max_depth) {
            xs_runtime_error(i->current_span,
                    "maximum call depth exceeded here",
                    "consider using iteration instead of deep recursion",
                    "stack overflow in '%s'",
                    fn->name ? fn->name : "<anonymous>");
            exit(1);
        }
        i->depth++;

        Env *saved_env = i->env;
        env_incref(saved_env);

        Value **tc_args = NULL;
        int     tc_argc = 0;
        int     owns_args = 0;

tail_call_entry: ;
        {
        Env *call_env  = env_new(fn->closure);
        i->env = call_env;

        {
            Value **cur_args = owns_args ? tc_args : args;
            int     cur_argc = owns_args ? tc_argc : argc;
            int arg_idx = 0;
            for (int j = 0; j < fn->nparams; j++) {
                Node *param = fn->params[j];
                int is_variadic = fn->variadic_flags && fn->variadic_flags[j];
                if (is_variadic) {
                    Value *rest = xs_array_new();
                    for (int k = arg_idx; k < cur_argc; k++)
                        array_push(rest->arr, value_incref(cur_args[k]));
                    if (param->tag == NODE_PAT_IDENT)
                        env_define(call_env, param->pat_ident.name, rest, 1);
                    else
                        bind_pattern(i, param, rest, call_env, 1);
                    value_decref(rest);
                    arg_idx = cur_argc;
                } else if (arg_idx < cur_argc) {
                    if (param->tag == NODE_PAT_IDENT)
                        env_define(call_env, param->pat_ident.name, cur_args[arg_idx], 1);
                    else
                        bind_pattern(i, param, cur_args[arg_idx], call_env, 1);
                    arg_idx++;
                } else {
                    Node *def = fn->default_vals ? fn->default_vals[j] : NULL;
                    if (def) {
                        Value *dv = interp_eval(i, def);
                        if (param->tag == NODE_PAT_IDENT)
                            env_define(call_env, param->pat_ident.name, dv, 1);
                        else
                            bind_pattern(i, param, dv, call_env, 1);
                        value_decref(dv);
                    } else {
                        if (param->tag == NODE_PAT_IDENT)
                            env_define(call_env, param->pat_ident.name, XS_NULL_VAL, 1);
                    }
                }
            }
        }

        /* wire up 'super' proxy so subclass methods can call base */
        {
            Value **cur_args2 = owns_args ? tc_args : args;
            int     cur_argc2 = owns_args ? tc_argc : argc;
            if (fn->nparams > 0 && cur_argc2 > 0 &&
                fn->params[0]->tag == NODE_PAT_IDENT &&
                strcmp(fn->params[0]->pat_ident.name, "self") == 0 &&
                cur_args2[0]->tag == XS_INST &&
                cur_args2[0]->inst->class_ &&
                cur_args2[0]->inst->class_->nbases > 0 &&
                cur_args2[0]->inst->class_->bases[0]) {
                XSClass *base = cur_args2[0]->inst->class_->bases[0];
                XSInst *si = xs_calloc(1, sizeof(XSInst));
                si->class_  = base;
                base->refcount++;
                si->fields  = map_new(); /* own empty fields map */
                si->methods = map_new();
                if (base->methods) {
                    int nk = 0; char **ks = map_keys(base->methods, &nk);
                    for (int sk = 0; sk < nk; sk++) {
                        Value *mv = map_get(base->methods, ks[sk]);
                        if (mv) map_set(si->methods, ks[sk], value_incref(mv));
                        free(ks[sk]);
                    }
                    free(ks);
                }
                map_set(si->fields, "__super_self__", value_incref(cur_args2[0]));
                si->refcount = 1;
                Value *super_val = xs_calloc(1, sizeof(Value));
                super_val->tag = XS_INST; super_val->refcount = 1;
                super_val->inst = si;
                env_define(call_env, "super", super_val, 1);
                value_decref(super_val);
            }
        }

        if (fn->param_type_names) {
            Value **cur_args = owns_args ? tc_args : args;
            int     cur_argc = owns_args ? tc_argc : argc;
            int arg_idx2 = 0;
            for (int j = 0; j < fn->nparams && arg_idx2 < cur_argc; j++) {
                int is_variadic = fn->variadic_flags && fn->variadic_flags[j];
                if (is_variadic) break;
                if (fn->param_type_names[j] && !value_matches_type(cur_args[arg_idx2], fn->param_type_names[j])) {
                    xs_runtime_error(i->current_span, "type mismatch", NULL,
                        "argument %d of '%s' expected '%s', got '%s'",
                        j + 1, fn->name ? fn->name : "<anonymous>",
                        fn->param_type_names[j], value_type_str(cur_args[arg_idx2]));
                    i->cf.signal = CF_PANIC;
                    i->cf.value = xs_str("type error");
                    break;
                }
                arg_idx2++;
            }
        }

        if (owns_args) {
            for (int j = 0; j < tc_argc; j++) value_decref(tc_args[j]);
            free(tc_args);
            tc_args = NULL; owns_args = 0;
        }

        if (i->cf.signal) {
            pop_env(i);
            i->depth--;
            interp_pop_frame(i);
            return value_incref(XS_NULL_VAL);
        }

        int defer_base = i->defers.len;
        Value *result = NULL;
        if (fn->body) {
            if (fn->is_generator) {
                Value *collector = xs_array_new();
                Value *saved_collector = i->yield_collect;
                i->yield_collect = collector;

                Value *body_val = interp_eval(i, fn->body);
                value_decref(body_val);

                i->yield_collect = saved_collector;

                if (i->cf.signal == CF_RETURN || i->cf.signal == CF_YIELD)
                    CF_CLEAR(i);

                Value *gen = xs_map_new();
                Value *type_v = xs_str("generator");
                map_set(gen->map, "__type", type_v); value_decref(type_v);
                map_set(gen->map, "_yields", collector); value_decref(collector);
                Value *idx_v = xs_int(0);
                map_set(gen->map, "_index", idx_v); value_decref(idx_v);
                Value *done_v = value_incref(XS_FALSE_VAL);
                map_set(gen->map, "_done", done_v); value_decref(done_v);
                result = gen;
            } else {
                Value *body_val = interp_eval(i, fn->body);
                if (i->cf.signal == CF_TAIL_CALL) {
                    value_decref(body_val);
                    for (int di = i->defers.len - 1; di >= defer_base; di--) {
                        Node *db = i->defers.items[di];
                        int saved_sig = i->cf.signal;
                        i->cf.signal = 0; i->cf.value = NULL; i->cf.label = NULL;
                        interp_exec(i, db);
                        if (!i->cf.signal) {
                            i->cf.signal = saved_sig;
                        }
                    }
                    i->defers.len = defer_base;

                    Value *tc_callee = i->tc_callee; i->tc_callee = NULL;
                    tc_args = i->tc_args;   i->tc_args = NULL;
                    tc_argc = i->tc_argc;   i->tc_argc = 0;
                    owns_args = 1;
                    i->cf.signal = 0;

                    env_decref(i->env);
                    if (tc_callee->tag == XS_FUNC) {
                        fn = tc_callee->fn;
                        value_decref(tc_callee);
                        goto tail_call_entry;
                    }

                    i->env = saved_env;
                    i->depth--;
                    Value *r = call_value(i, tc_callee, tc_args, tc_argc, NULL);
                    value_decref(tc_callee);
                    for (int j = 0; j < tc_argc; j++) value_decref(tc_args[j]);
                    free(tc_args);
                    interp_pop_frame(i);
                    return r;
                } else if (i->cf.signal == CF_RETURN) {
                    if (i->cf.value) result = value_incref(i->cf.value);
                    CF_CLEAR(i);
                    value_decref(body_val);
                } else if (!i->cf.signal) {
                    result = body_val;
                } else {
                    value_decref(body_val);
                }
            }
        }
        if (!result) result = value_incref(XS_NULL_VAL);

        for (int di = i->defers.len - 1; di >= defer_base; di--) {
            Node *db = i->defers.items[di];
            int saved_sig = i->cf.signal;
            Value *saved_val = i->cf.value;
            char *saved_lbl = i->cf.label;
            i->cf.signal = 0; i->cf.value = NULL; i->cf.label = NULL;
            interp_exec(i, db);
            if (!i->cf.signal) {
                i->cf.signal = saved_sig;
                i->cf.value  = saved_val;
                i->cf.label  = saved_lbl;
            } else {
                value_decref(saved_val);
                free(saved_lbl);
            }
        }
        i->defers.len = defer_base;

        if (fn->ret_type_name && !value_matches_type(result, fn->ret_type_name)) {
            xs_runtime_error(i->current_span, "type mismatch", NULL,
                "function '%s' expected return type '%s', got '%s'",
                fn->name ? fn->name : "<anonymous>",
                fn->ret_type_name, value_type_str(result));
            i->cf.signal = CF_PANIC;
            i->cf.value = xs_str("type error");
        }

        env_decref(i->env);
        i->env = saved_env;
        i->depth--;
        interp_pop_frame(i);
        return result;
        } /* end of tail_call_entry block */
    }

    if (callee->tag == XS_CLASS_VAL) {
        XSClass *cls = callee->cls;
        XSInst  *inst = xs_calloc(1, sizeof(XSInst));
        inst->class_  = cls; cls->refcount++;
        inst->fields  = map_new();
        inst->methods = map_new();
        inst->refcount = 1;

        if (cls->fields) {
            int nkeys = 0;
            char **keys = map_keys(cls->fields, &nkeys);
            for (int j = 0; j < nkeys; j++) {
                Value *fv = map_get(cls->fields, keys[j]);
                if (fv) map_set(inst->fields, keys[j], value_incref(fv));
                free(keys[j]);
            }
            free(keys);
        }
        if (cls->methods) {
            int nkeys = 0;
            char **keys = map_keys(cls->methods, &nkeys);
            for (int j = 0; j < nkeys; j++) {
                Value *mv = map_get(cls->methods, keys[j]);
                if (mv) map_set(inst->methods, keys[j], value_incref(mv));
                free(keys[j]);
            }
            free(keys);
        }

        Value *inst_val = xs_calloc(1, sizeof(Value));
        inst_val->tag = XS_INST; inst_val->refcount = 1;
        inst_val->inst = inst;

        Value *init_fn = map_get(inst->methods, "__init__");
        if (!init_fn) init_fn = map_get(inst->methods, "init");
        if (init_fn && init_fn->tag == XS_FUNC) {
            Value **new_args = xs_malloc((argc+1)*sizeof(Value*));
            new_args[0] = value_incref(inst_val);
            for (int j = 0; j < argc; j++) new_args[j+1] = args[j];
            Value *r = call_value(i, init_fn, new_args, argc+1, "__init__");
            value_decref(new_args[0]);
            free(new_args);
            value_decref(r);
        }
        interp_pop_frame(i);
        return inst_val;
    }

    if (callee->tag == XS_SIGNAL) {
        XSSignal *sig = callee->signal;
        if (argc > 0) {
            value_decref(sig->value);
            sig->value = value_incref(args[0]);
            if (!sig->notifying) {
                sig->notifying = 1;
                for (int j = 0; j < sig->nsubs; j++) {
                    Value *r = call_value(i, sig->subscribers[j], args, 1, "subscriber");
                    value_decref(r);
                }
                sig->notifying = 0;
            }
        }
        if (sig->compute) {
            Value *r = call_value(i, sig->compute, NULL, 0, "derived");
            interp_pop_frame(i);
            return r;
        }
        interp_pop_frame(i);
        return value_incref(sig->value);
    }

    if (callee->tag == XS_ACTOR && callee->actor) {
        XSActor *src = callee->actor;
        XSActor *actor = xs_calloc(1, sizeof(XSActor));
        actor->name     = xs_strdup(src->name);
        actor->state    = map_new();
        actor->methods  = map_new();
        actor->refcount = 1;
        if (src->state) {
            int nk = 0; char **ks = map_keys(src->state, &nk);
            for (int j = 0; j < nk; j++) {
                Value *sv = map_get(src->state, ks[j]);
                if (sv) map_set(actor->state, ks[j], value_incref(sv));
                free(ks[j]);
            }
            free(ks);
        }
        if (src->methods) {
            int nk = 0; char **ks = map_keys(src->methods, &nk);
            for (int j = 0; j < nk; j++) {
                Value *mv = map_get(src->methods, ks[j]);
                if (mv) map_set(actor->methods, ks[j], value_incref(mv));
                free(ks[j]);
            }
            free(ks);
        }
        if (src->handle_fn) {
            actor->handle_fn = src->handle_fn;
            actor->handle_fn->refcount++;
        }
        actor->closure = src->closure ? env_incref(src->closure) : NULL;
        Value *init_fn = map_get(actor->methods, "init");
        if (!init_fn) init_fn = map_get(actor->methods, "__init__");
        Value *actor_val = xs_calloc(1, sizeof(Value));
        actor_val->tag      = XS_ACTOR;
        actor_val->refcount = 1;
        actor_val->actor    = actor;
        if (init_fn && init_fn->tag == XS_FUNC) {
            Env *wrapper = env_new(init_fn->fn->closure ? init_fn->fn->closure : actor->closure);
            env_define(wrapper, "self", value_incref(actor_val), 0);
            if (actor->state) {
                int nk2 = 0; char **ks2 = map_keys(actor->state, &nk2);
                for (int j = 0; j < nk2; j++) {
                    Value *sv = map_get(actor->state, ks2[j]);
                    if (sv) env_define(wrapper, ks2[j], value_incref(sv), 1);
                    free(ks2[j]);
                }
                free(ks2);
            }
            Env *orig = init_fn->fn->closure;
            env_incref(wrapper);
            init_fn->fn->closure = wrapper;
            Value *r = call_value(i, init_fn, args, argc, "init");
            env_decref(init_fn->fn->closure);
            init_fn->fn->closure = orig;
            if (actor->state) {
                int nk2 = 0; char **ks2 = map_keys(actor->state, &nk2);
                for (int j = 0; j < nk2; j++) {
                    Value *upd = env_get(wrapper, ks2[j]);
                    if (upd) map_set(actor->state, ks2[j], value_incref(upd));
                    free(ks2[j]);
                }
                free(ks2);
            }
            env_decref(wrapper);
            value_decref(r);
        }
        interp_pop_frame(i);
        return actor_val;
    }

    char *repr = value_repr(callee);
    const char *type_name = "unknown";
    switch (callee->tag) {
        case XS_INT: type_name = "int"; break;
        case XS_FLOAT: type_name = "float"; break;
        case XS_STR: type_name = "str"; break;
        case XS_BOOL: type_name = "bool"; break;
        case XS_ARRAY: type_name = "array"; break;
        case XS_MAP: type_name = "map"; break;
        case XS_NULL: type_name = "null"; break;
        default: type_name = "value"; break;
    }
    xs_runtime_error(i->current_span,
            "not a function — cannot call this",
            NULL,
            "value of type '%s' is not callable (got %s)",
            type_name, repr);
    free(repr);
    i->cf.signal = CF_ERROR;
    interp_pop_frame(i);
    return value_incref(XS_NULL_VAL);
}

/* method dispatch */

static Value *eval_method(Interp *i, Value *obj, const char *method,
                           Value **args, int argc) {
    if (obj->tag == XS_INST) {
        if (strcmp(method, "is_a") == 0) {
            if (argc >= 1 && args[0]->tag == XS_STR && obj->inst->class_) {
                int found = 0;
                XSClass *stack[64];
                int sp = 0;
                stack[sp++] = obj->inst->class_;
                while (sp > 0 && !found) {
                    XSClass *c = stack[--sp];
                    if (strcmp(c->name, args[0]->s) == 0) { found = 1; break; }
                    for (int ti = 0; ti < c->ntraits; ti++) {
                        if (c->traits[ti] && strcmp(c->traits[ti], args[0]->s) == 0)
                            { found = 1; break; }
                    }
                    if (found) break;
                    for (int bi = 0; bi < c->nbases && sp < 64; bi++) {
                        if (c->bases[bi]) stack[sp++] = c->bases[bi];
                    }
                }
                return found ? value_incref(XS_TRUE_VAL) : value_incref(XS_FALSE_VAL);
            }
            return value_incref(XS_FALSE_VAL);
        }
        Value *fn = map_get(obj->inst->methods, method);
        if (!fn && obj->inst->class_ && obj->inst->class_->methods)
            fn = map_get(obj->inst->class_->methods, method);
        if (fn) {
            int has_self_param = 0;
            if (fn->tag == XS_FUNC && fn->fn->nparams > 0) {
                Node *p0 = fn->fn->params[0];
                if (p0->tag == NODE_PAT_IDENT && strcmp(p0->pat_ident.name, "self") == 0)
                    has_self_param = 1;
            }
            if (fn->tag == XS_NATIVE) has_self_param = 1;
            Value *r;
            if (has_self_param) {
                Value *real_self = obj;
                Value *super_self = map_get(obj->inst->fields, "__super_self__");
                if (super_self && super_self->tag == XS_INST)
                    real_self = super_self;
                Value **new_args = xs_malloc((argc+1)*sizeof(Value*));
                new_args[0] = value_incref(real_self);
                for (int j = 0; j < argc; j++) new_args[j+1] = args[j];
                r = call_value(i, fn, new_args, argc+1, method);
                value_decref(new_args[0]);
                free(new_args);
            } else {
                Env *saved = i->env;
                env_incref(saved);
                Env *wrapper = env_new(fn->tag==XS_FUNC ? fn->fn->closure : saved);
                env_define(wrapper, "self", obj, 1);
                if (obj->inst && obj->inst->fields) {
                    int nk = 0; char **ks = map_keys(obj->inst->fields, &nk);
                    for (int j = 0; j < nk; j++) {
                        Value *fv = map_get(obj->inst->fields, ks[j]);
                        if (fv) env_define(wrapper, ks[j], fv, 1);
                        free(ks[j]);
                    }
                    free(ks);
                }
                Env *orig_closure = NULL;
                if (fn->tag == XS_FUNC) {
                    orig_closure = fn->fn->closure;
                    env_incref(wrapper);
                    fn->fn->closure = wrapper;
                }
                r = call_value(i, fn, args, argc, method);
                if (fn->tag == XS_FUNC) {
                    env_decref(fn->fn->closure);
                    fn->fn->closure = orig_closure;
                }
                if (obj->inst && obj->inst->fields) {
                    int nk = 0; char **ks = map_keys(obj->inst->fields, &nk);
                    for (int j = 0; j < nk; j++) {
                        Value *new_fv = env_get(wrapper, ks[j]);
                        if (new_fv) map_set(obj->inst->fields, ks[j], value_incref(new_fv));
                        free(ks[j]);
                    }
                    free(ks);
                }
                i->env = saved;
                env_decref(wrapper);
                env_decref(saved);
            }
            return r;
        }
        Value *fv = map_get(obj->inst->fields, method);
        if (fv) return call_value(i, fv, args, argc, method);
    }

    if (obj->tag == XS_ACTOR && obj->actor) {
        XSActor *actor = obj->actor;
        if (strcmp(method, "send") == 0 && argc >= 1) {
            if (actor->handle_fn) {
                Env *wrapper = env_new(actor->handle_fn->closure ? actor->handle_fn->closure : actor->closure);
                env_define(wrapper, "self", value_incref(obj), 0);
                if (actor->state) {
                    int nk = 0; char **ks = map_keys(actor->state, &nk);
                    for (int j = 0; j < nk; j++) {
                        Value *sv = map_get(actor->state, ks[j]);
                        if (sv) env_define(wrapper, ks[j], value_incref(sv), 1);
                        free(ks[j]);
                    }
                    free(ks);
                }
                Env *orig_closure = actor->handle_fn->closure;
                env_incref(wrapper);
                actor->handle_fn->closure = wrapper;
                Value *fn_val = xs_func_new(actor->handle_fn);
                Value *r = call_value(i, fn_val, args, 1, "handle");
                value_decref(fn_val);
                env_decref(actor->handle_fn->closure);
                actor->handle_fn->closure = orig_closure;
                if (actor->state) {
                    int nk = 0; char **ks = map_keys(actor->state, &nk);
                    for (int j = 0; j < nk; j++) {
                        Value *updated = env_get(wrapper, ks[j]);
                        if (updated) map_set(actor->state, ks[j], value_incref(updated));
                        free(ks[j]);
                    }
                    free(ks);
                }
                env_decref(wrapper);
                return r;
            }
            return value_incref(XS_NULL_VAL);
        }
        Value *mfn = map_get(actor->methods, method);
        if (mfn) {
            Env *wrapper = env_new(mfn->tag == XS_FUNC ? mfn->fn->closure : actor->closure);
            env_define(wrapper, "self", value_incref(obj), 0);
            if (actor->state) {
                int nk = 0; char **ks = map_keys(actor->state, &nk);
                for (int j = 0; j < nk; j++) {
                    Value *sv = map_get(actor->state, ks[j]);
                    if (sv) env_define(wrapper, ks[j], value_incref(sv), 1);
                    free(ks[j]);
                }
                free(ks);
            }
            Env *orig_closure = NULL;
            if (mfn->tag == XS_FUNC) {
                orig_closure = mfn->fn->closure;
                env_incref(wrapper);
                mfn->fn->closure = wrapper;
            }
            Value *r = call_value(i, mfn, args, argc, method);
            if (mfn->tag == XS_FUNC) {
                env_decref(mfn->fn->closure);
                mfn->fn->closure = orig_closure;
            }
            if (actor->state) {
                int nk = 0; char **ks = map_keys(actor->state, &nk);
                for (int j = 0; j < nk; j++) {
                    Value *updated = env_get(wrapper, ks[j]);
                    if (updated) map_set(actor->state, ks[j], value_incref(updated));
                    free(ks[j]);
                }
                free(ks);
            }
            env_decref(wrapper);
            return r;
        }
        if (actor->state) {
            Value *sv = map_get(actor->state, method);
            if (sv) return value_incref(sv);
        }
        return value_incref(XS_NULL_VAL);
    }

    // --- string methods
    if (obj->tag == XS_STR) {
        const char *s = obj->s;
        int slen = (int)strlen(s);
        if (strcmp(method, "len") == 0 || strcmp(method, "length") == 0) return xs_int(slen);
        if (strcmp(method, "upper") == 0 || strcmp(method, "to_upper") == 0) {
            char *r = xs_strdup(s);
            for (int j=0; r[j]; j++) r[j] = toupper((unsigned char)r[j]);
            Value *v = xs_str(r); free(r); return v;
        }
        if (strcmp(method, "lower") == 0 || strcmp(method, "to_lower") == 0) {
            char *r = xs_strdup(s);
            for (int j=0; r[j]; j++) r[j] = tolower((unsigned char)r[j]);
            Value *v = xs_str(r); free(r); return v;
        }
        if (strcmp(method, "trim") == 0) {
            int start=0, end=slen-1;
            while (start<=end && isspace((unsigned char)s[start])) start++;
            while (end>=start && isspace((unsigned char)s[end]))   end--;
            return xs_str_n(s+start, end-start+1);
        }
        if (strcmp(method, "starts_with") == 0 || strcmp(method, "startswith") == 0) {
            if (argc < 1 || args[0]->tag != XS_STR) return value_incref(XS_FALSE_VAL);
            return strncmp(s, args[0]->s, strlen(args[0]->s)) == 0 ?
                   value_incref(XS_TRUE_VAL) : value_incref(XS_FALSE_VAL);
        }
        if (strcmp(method, "ends_with") == 0 || strcmp(method, "endswith") == 0) {
            if (argc < 1 || args[0]->tag != XS_STR) return value_incref(XS_FALSE_VAL);
            int plen = (int)strlen(args[0]->s);
            if (plen > slen) return value_incref(XS_FALSE_VAL);
            return strcmp(s + slen - plen, args[0]->s) == 0 ?
                   value_incref(XS_TRUE_VAL) : value_incref(XS_FALSE_VAL);
        }
        if (strcmp(method, "contains") == 0 || strcmp(method, "includes") == 0) {
            if (argc < 1 || args[0]->tag != XS_STR) return value_incref(XS_FALSE_VAL);
            return strstr(s, args[0]->s) ?
                   value_incref(XS_TRUE_VAL) : value_incref(XS_FALSE_VAL);
        }
        if (strcmp(method, "split") == 0) {
            Value *arr = xs_array_new();
            const char *sep = (argc > 0 && args[0]->tag == XS_STR) ? args[0]->s : " ";
            int seplen = (int)strlen(sep);
            const char *cur = s;
            if (seplen == 0) {
                for (int j = 0; j < slen; j++)
                    array_push(arr->arr, xs_str_n(s+j, 1));
            } else {
                while (1) {
                    const char *pos = strstr(cur, sep);
                    if (!pos) { array_push(arr->arr, xs_str(cur)); break; }
                    array_push(arr->arr, xs_str_n(cur, pos-cur));
                    cur = pos + seplen;
                }
            }
            return arr;
        }
        if (strcmp(method, "replace") == 0) {
            if (argc < 2) return value_incref(obj);
            const char *from = (args[0]->tag==XS_STR)?args[0]->s:"";
            const char *to   = (args[1]->tag==XS_STR)?args[1]->s:"";
            int flen=(int)strlen(from), tlen=(int)strlen(to);
            if (flen == 0) return value_incref(obj);
            int max_replace = -1; /* -1 means replace all */
            if (argc >= 3 && args[2]->tag == XS_INT) max_replace = (int)args[2]->i;
            /* count occurrences (up to max_replace if set) */
            int count=0; const char *p=s;
            while ((p=strstr(p,from))) { count++; p+=flen; if (max_replace>=0 && count>=max_replace) break; }
            int newlen = slen + count*(tlen-flen);
            char *res = xs_malloc(newlen+1); int ri=0; int replaced=0;
            p=s;
            while (1) {
                const char *q=strstr(p,from);
                if (!q || (max_replace>=0 && replaced>=max_replace)) { strcpy(res+ri,p); break; }
                int part=(int)(q-p); memcpy(res+ri,p,part); ri+=part;
                memcpy(res+ri,to,tlen); ri+=tlen;
                p=q+flen; replaced++;
            }
            Value *v=xs_str(res); free(res); return v;
        }
        if (strcmp(method, "chars") == 0) {
            Value *arr = xs_array_new();
            for (int j=0;j<slen;j++) array_push(arr->arr, xs_str_n(s+j,1));
            return arr;
        }
        if (strcmp(method, "bytes") == 0 || strcmp(method, "to_bytes") == 0) {
            Value *arr = xs_array_new();
            for (int j=0;j<slen;j++) array_push(arr->arr, xs_int((unsigned char)s[j]));
            return arr;
        }
        if (strcmp(method, "parse_int") == 0 || strcmp(method, "parse") == 0) {
            int base = (argc > 0 && args[0]->tag == XS_INT) ? (int)args[0]->i : 10;
            char *endptr = NULL;
            long long val = strtoll(s, &endptr, base);
            if (endptr == s) return value_incref(XS_NULL_VAL);
            return xs_int(val);
        }
        if (strcmp(method, "parse_float") == 0) { return xs_float(atof(s)); }
        if (strcmp(method, "is_empty") == 0) {
            return slen==0?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
        }
        if (strcmp(method, "repeat") == 0) {
            int n2 = (argc>0&&args[0]->tag==XS_INT)?(int)args[0]->i:0;
            if (n2 < 0) n2 = 0;
            if (n2 > 0 && slen > (int)(INT_MAX - 1) / n2) return xs_str("");
            char *r = xs_malloc(slen*n2+1); int pos = 0;
            for (int j=0;j<n2;j++) { memcpy(r+pos,s,slen); pos+=slen; }
            r[pos]='\0';
            Value *v=xs_str(r); free(r); return v;
        }
        if (strcmp(method, "join") == 0) {
            if (argc<1||args[0]->tag!=XS_ARRAY) return value_incref(obj);
            /* s.join(arr) → join arr elements with sep=s */
            XSArray *arr2 = args[0]->arr;
            int total=0;
            for (int j=0;j<arr2->len;j++) {
                char *rs=value_str(arr2->items[j]);
                total+=(int)strlen(rs); free(rs);
                if (j<arr2->len-1) total+=slen;
            }
            char *res=xs_malloc(total+1); res[0]='\0';
            for (int j=0;j<arr2->len;j++) {
                char *rs=value_str(arr2->items[j]);
                strcat(res,rs); free(rs);
                if (j<arr2->len-1) strcat(res,s);
            }
            Value *v=xs_str(res); free(res); return v;
        }
        /* substr/slice: str.slice(start, end) or str[i..j] */
        if (strcmp(method, "slice") == 0 || strcmp(method, "substr") == 0 || strcmp(method, "substring") == 0) {
            int start2=0, end2=slen;
            if (argc>0&&args[0]->tag==XS_INT) start2=(int)args[0]->i;
            if (argc>1&&args[1]->tag==XS_INT) end2=(int)args[1]->i;
            if (start2<0) start2=slen+start2;
            if (end2<0)   end2=slen+end2;
            if (start2<0) start2=0;
            if (end2>slen) end2=slen;
            return (start2>=end2)?xs_str(""):xs_str_n(s+start2,end2-start2);
        }
        if (strcmp(method, "find") == 0 || strcmp(method, "index_of") == 0) {
            if (argc<1||args[0]->tag!=XS_STR) return xs_int(-1);
            const char *pos=strstr(s,args[0]->s);
            return xs_int(pos?pos-s:-1);
        }
        /* to_str / to_string */
        if (strcmp(method, "to_str") == 0 || strcmp(method, "to_string") == 0)
            return value_incref(obj);
        /* trim_start / ltrim */
        if (strcmp(method, "trim_start") == 0 || strcmp(method, "ltrim") == 0) {
            int start=0;
            while (start<slen && isspace((unsigned char)s[start])) start++;
            return xs_str(s+start);
        }
        /* trim_end / rtrim */
        if (strcmp(method, "trim_end") == 0 || strcmp(method, "rtrim") == 0) {
            int end=slen-1;
            while (end>=0 && isspace((unsigned char)s[end])) end--;
            return xs_str_n(s, end+1);
        }
        /* lines — split by \n */
        if (strcmp(method, "lines") == 0) {
            Value *arr = xs_array_new();
            const char *cur = s;
            while (1) {
                const char *pos = strchr(cur, '\n');
                if (!pos) { array_push(arr->arr, xs_str(cur)); break; }
                array_push(arr->arr, xs_str_n(cur, pos-cur));
                cur = pos+1;
            }
            return arr;
        }
        /* count(substr) */
        if (strcmp(method, "count") == 0) {
            if (argc<1||args[0]->tag!=XS_STR) return xs_int(0);
            const char *sub=args[0]->s; int sublen=(int)strlen(sub);
            if (sublen==0) return xs_int(0);
            int cnt=0; const char *p=s;
            while ((p=strstr(p,sub))) { cnt++; p+=sublen; }
            return xs_int(cnt);
        }
        /* title */
        if (strcmp(method, "title") == 0) {
            char *r=xs_strdup(s); int after_space=1;
            for (int j=0;r[j];j++) {
                if (isspace((unsigned char)r[j])) { after_space=1; }
                else if (after_space) { r[j]=toupper((unsigned char)r[j]); after_space=0; }
                else { r[j]=tolower((unsigned char)r[j]); }
            }
            Value *v=xs_str(r); free(r); return v;
        }
        /* capitalize */
        if (strcmp(method, "capitalize") == 0) {
            char *r=xs_strdup(s);
            for (int j=0;r[j];j++) r[j]=(j==0)?toupper((unsigned char)r[j]):tolower((unsigned char)r[j]);
            Value *v=xs_str(r); free(r); return v;
        }
        /* center(width, ch) */
        if (strcmp(method, "center") == 0) {
            int width=(argc>0&&args[0]->tag==XS_INT)?(int)args[0]->i:slen;
            char ch=(argc>1&&args[1]->tag==XS_STR&&args[1]->s[0])?args[1]->s[0]:' ';
            if (width<=slen) return value_incref(obj);
            int total=width-slen; int left=total/2; int right=total-left;
            char *r=xs_malloc(width+1);
            for(int j=0;j<left;j++) r[j]=ch;
            memcpy(r+left,s,slen);
            for(int j=0;j<right;j++) r[left+slen+j]=ch;
            r[width]='\0';
            Value *v=xs_str(r); free(r); return v;
        }
        /* pad_left / lpad */
        if (strcmp(method, "pad_left") == 0 || strcmp(method, "lpad") == 0 || strcmp(method, "pad_start") == 0) {
            int width=(argc>0&&args[0]->tag==XS_INT)?(int)args[0]->i:slen;
            char ch=(argc>1&&args[1]->tag==XS_STR&&args[1]->s[0])?args[1]->s[0]:' ';
            if (width<=slen) return value_incref(obj);
            int pad=width-slen;
            char *r=xs_malloc(width+1);
            for(int j=0;j<pad;j++) r[j]=ch;
            memcpy(r+pad,s,slen); r[width]='\0';
            Value *v=xs_str(r); free(r); return v;
        }
        /* pad_right / rpad */
        if (strcmp(method, "pad_right") == 0 || strcmp(method, "rpad") == 0 || strcmp(method, "pad_end") == 0) {
            int width=(argc>0&&args[0]->tag==XS_INT)?(int)args[0]->i:slen;
            char ch=(argc>1&&args[1]->tag==XS_STR&&args[1]->s[0])?args[1]->s[0]:' ';
            if (width<=slen) return value_incref(obj);
            int pad=width-slen;
            char *r=xs_malloc(width+1);
            memcpy(r,s,slen);
            for(int j=0;j<pad;j++) r[slen+j]=ch;
            r[width]='\0';
            Value *v=xs_str(r); free(r); return v;
        }
        /* remove_prefix */
        if (strcmp(method, "remove_prefix") == 0) {
            if (argc<1||args[0]->tag!=XS_STR) return value_incref(obj);
            int plen=(int)strlen(args[0]->s);
            if (plen<=slen && strncmp(s,args[0]->s,plen)==0) return xs_str(s+plen);
            return value_incref(obj);
        }
        /* remove_suffix */
        if (strcmp(method, "remove_suffix") == 0) {
            if (argc<1||args[0]->tag!=XS_STR) return value_incref(obj);
            int plen=(int)strlen(args[0]->s);
            if (plen<=slen && strcmp(s+slen-plen,args[0]->s)==0) return xs_str_n(s,slen-plen);
            return value_incref(obj);
        }
        /* is_ascii */
        if (strcmp(method, "is_ascii") == 0) {
            for (int j=0;j<slen;j++) if ((unsigned char)s[j]>=128) return value_incref(XS_FALSE_VAL);
            return value_incref(XS_TRUE_VAL);
        }
        /* is_digit / is_numeric */
        if (strcmp(method, "is_digit") == 0 || strcmp(method, "is_numeric") == 0) {
            if (slen==0) return value_incref(XS_FALSE_VAL);
            for (int j=0;j<slen;j++) if (!isdigit((unsigned char)s[j])) return value_incref(XS_FALSE_VAL);
            return value_incref(XS_TRUE_VAL);
        }
        /* is_alpha */
        if (strcmp(method, "is_alpha") == 0) {
            if (slen==0) return value_incref(XS_FALSE_VAL);
            for (int j=0;j<slen;j++) if (!isalpha((unsigned char)s[j])) return value_incref(XS_FALSE_VAL);
            return value_incref(XS_TRUE_VAL);
        }
        /* is_alnum */
        if (strcmp(method, "is_alnum") == 0) {
            if (slen==0) return value_incref(XS_FALSE_VAL);
            for (int j=0;j<slen;j++) if (!isalnum((unsigned char)s[j])) return value_incref(XS_FALSE_VAL);
            return value_incref(XS_TRUE_VAL);
        }
        /* is_upper */
        if (strcmp(method, "is_upper") == 0) {
            if (slen==0) return value_incref(XS_FALSE_VAL);
            int has_alpha=0;
            for (int j=0;j<slen;j++) {
                if (isalpha((unsigned char)s[j])) {
                    has_alpha=1;
                    if (islower((unsigned char)s[j])) return value_incref(XS_FALSE_VAL);
                }
            }
            return has_alpha?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
        }
        /* is_lower */
        if (strcmp(method, "is_lower") == 0) {
            if (slen==0) return value_incref(XS_FALSE_VAL);
            int has_alpha=0;
            for (int j=0;j<slen;j++) {
                if (isalpha((unsigned char)s[j])) {
                    has_alpha=1;
                    if (isupper((unsigned char)s[j])) return value_incref(XS_FALSE_VAL);
                }
            }
            return has_alpha?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
        }
        /* to_int / as_int */
        if (strcmp(method, "to_int") == 0 || strcmp(method, "as_int") == 0) {
            return xs_int(atoll(s));
        }
        /* to_float / as_float */
        if (strcmp(method, "to_float") == 0 || strcmp(method, "as_float") == 0) {
            return xs_float(atof(s));
        }
        /* char_at */
        if (strcmp(method, "char_at") == 0) {
            int idx=(argc>0&&args[0]->tag==XS_INT)?(int)args[0]->i:0;
            if (idx<0) idx=slen+idx;
            if (idx<0||idx>=slen) return value_incref(XS_NULL_VAL);
            return xs_str_n(s+idx,1);
        }
        /* reverse / reversed */
        if (strcmp(method, "reverse") == 0 || strcmp(method, "reversed") == 0) {
            char *r=xs_strdup(s);
            for (int l2=0,r2=slen-1;l2<r2;l2++,r2--) { char t=r[l2]; r[l2]=r[r2]; r[r2]=t; }
            Value *v=xs_str(r); free(r); return v;
        }
        /* truncate(max_len, suffix) */
        if (strcmp(method, "truncate") == 0) {
            int maxlen=(argc>0&&args[0]->tag==XS_INT)?(int)args[0]->i:slen;
            const char *suf=(argc>1&&args[1]->tag==XS_STR)?args[1]->s:"...";
            if (slen<=maxlen) return value_incref(obj);
            int suflen=(int)strlen(suf);
            int cutlen=maxlen-suflen; if (cutlen<0) cutlen=0;
            char *r=xs_malloc(maxlen+1);
            memcpy(r,s,cutlen); memcpy(r+cutlen,suf,suflen); r[maxlen]='\0';
            Value *v=xs_str(r); free(r); return v;
        }
        /* rfind */
        if (strcmp(method, "rfind") == 0) {
            if (argc<1||args[0]->tag!=XS_STR) return xs_int(-1);
            int sublen=(int)strlen(args[0]->s);
            int last=-1;
            const char *p=s;
            while ((p=strstr(p,args[0]->s))) { last=(int)(p-s); p+=sublen; }
            return xs_int(last);
        }
        /* split_at */
        if (strcmp(method, "split_at") == 0) {
            int idx=(argc>0&&args[0]->tag==XS_INT)?(int)args[0]->i:0;
            if (idx<0) idx=slen+idx;
            if (idx<0) idx=0;
            if (idx>slen) idx=slen;
            Value *tup=xs_tuple_new();
            array_push(tup->arr, xs_str_n(s,idx));
            array_push(tup->arr, xs_str(s+idx));
            return tup;
        }
        /* to_chars (alias for chars) */
        if (strcmp(method, "to_chars") == 0) {
            Value *arr = xs_array_new();
            for (int j=0;j<slen;j++) array_push(arr->arr, xs_str_n(s+j,1));
            return arr;
        }
        /* format: "Hello {} and {}".format(a, b) — substitute {} placeholders */
        if (strcmp(method, "format") == 0) {
            /* Build result by replacing {} with successive args */
            int cap = slen + 256;
            char *res = xs_malloc(cap); int ri = 0; int ai = 0;
            for (int j = 0; j < slen; j++) {
                if (s[j] == '{' && j+1 < slen && s[j+1] == '}') {
                    char *rep = NULL;
                    if (ai < argc) { rep = value_str(args[ai++]); }
                    else { rep = xs_strdup(""); }
                    int rlen = (int)strlen(rep);
                    while (ri + rlen + 1 >= cap) { cap *= 2; char *tmp = realloc(res, cap); if (!tmp) { free(res); free(rep); return xs_str("<oom>"); } res = tmp; }
                    memcpy(res + ri, rep, rlen); ri += rlen;
                    free(rep);
                    j++; /* skip '}' */
                } else {
                    if (ri + 2 >= cap) { cap *= 2; char *tmp = realloc(res, cap); if (!tmp) { free(res); return xs_str("<oom>"); } res = tmp; }
                    res[ri++] = s[j];
                }
            }
            res[ri] = '\0';
            Value *v = xs_str(res); free(res); return v;
        }
        /* from_chars: "".from_chars([...]) — build string from char array */
        if (strcmp(method, "from_chars") == 0) {
            if (argc < 1 || args[0]->tag != XS_ARRAY) return xs_str("");
            XSArray *arr2 = args[0]->arr;
            int total = 0;
            for (int j = 0; j < arr2->len; j++) {
                if (arr2->items[j]->tag == XS_STR) total += (int)strlen(arr2->items[j]->s);
                else total += 1;
            }
            char *res = xs_malloc(total + 1); int ri = 0;
            for (int j = 0; j < arr2->len; j++) {
                if (arr2->items[j]->tag == XS_STR) {
                    int l = (int)strlen(arr2->items[j]->s);
                    memcpy(res + ri, arr2->items[j]->s, l); ri += l;
                } else if (arr2->items[j]->tag == XS_INT) {
                    res[ri++] = (char)arr2->items[j]->i;
                }
            }
            res[ri] = '\0';
            Value *v = xs_str(res); free(res); return v;
        }
    }

    // --- array methods
    if (obj->tag == XS_ARRAY || obj->tag == XS_TUPLE) {
        XSArray *arr = obj->arr;
        if (strcmp(method, "len") == 0) return xs_int(arr->len);
        if (strcmp(method, "push") == 0 || strcmp(method, "append") == 0) {
            for (int j=0;j<argc;j++) array_push(arr, value_incref(args[j]));
            return value_incref(XS_NULL_VAL);
        }
        if (strcmp(method, "pop") == 0) {
            if (arr->len == 0) return value_incref(XS_NULL_VAL);
            Value *v = arr->items[--arr->len]; /* consume refcount */
            return v;
        }
        if (strcmp(method, "first") == 0 || strcmp(method, "head") == 0) {
            return arr->len>0?value_incref(arr->items[0]):value_incref(XS_NULL_VAL);
        }
        if (strcmp(method, "last") == 0) {
            return arr->len>0?value_incref(arr->items[arr->len-1]):value_incref(XS_NULL_VAL);
        }
        if (strcmp(method, "is_empty") == 0) {
            return arr->len==0?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
        }
        if (strcmp(method, "contains") == 0 || strcmp(method, "includes") == 0) {
            if (argc<1) return value_incref(XS_FALSE_VAL);
            for (int j=0;j<arr->len;j++)
                if (value_equal(arr->items[j],args[0]))
                    return value_incref(XS_TRUE_VAL);
            return value_incref(XS_FALSE_VAL);
        }
        if (strcmp(method, "map") == 0) {
            if (argc<1) return value_incref(obj);
            Value *res = xs_array_new();
            for (int j=0;j<arr->len;j++) {
                Value *a[1] = {arr->items[j]};
                Value *r = call_value(i, args[0], a, 1, "map");
                if (!i->cf.signal) array_push(res->arr, r);
                else { value_decref(r); break; }
            }
            return res;
        }
        if (strcmp(method, "filter") == 0) {
            if (argc<1) return value_incref(obj);
            Value *res = xs_array_new();
            for (int j=0;j<arr->len;j++) {
                Value *a[1] = {arr->items[j]};
                Value *r = call_value(i, args[0], a, 1, "filter");
                int keep = value_truthy(r); value_decref(r);
                if (!i->cf.signal && keep) array_push(res->arr, value_incref(arr->items[j]));
                if (i->cf.signal) break;
            }
            return res;
        }
        if (strcmp(method, "reduce") == 0 || strcmp(method, "fold") == 0) {
            /* reduce(fn) — use first element as init, reduce remaining
               reduce(fn, init) — args[0]=fn, args[1]=init
               fold(init, fn)   — args[0]=init, args[1]=fn  */
            if (argc<1) return value_incref(XS_NULL_VAL);
            Value *fn_val, *init;
            int start_idx = 0;
            if (strcmp(method, "fold") == 0) {
                if (argc < 2) return value_incref(XS_NULL_VAL);
                init = args[0]; fn_val = args[1];
            } else if (argc >= 2) {
                fn_val = args[0]; init = args[1];
            } else {
                /* reduce(fn) — 1 arg: use first element as accumulator */
                fn_val = args[0];
                if (arr->len == 0) return value_incref(XS_NULL_VAL);
                init = arr->items[0];
                start_idx = 1;
            }
            Value *acc = value_incref(init);
            for (int j=start_idx;j<arr->len;j++) {
                Value *a[2] = {acc, arr->items[j]};
                Value *r = call_value(i, fn_val, a, 2, "fold");
                value_decref(acc); acc = r;
                if (i->cf.signal) break;
            }
            return acc;
        }
        if (strcmp(method, "find") == 0) {
            if (argc<1) return value_incref(XS_NULL_VAL);
            for (int j=0;j<arr->len;j++) {
                Value *a[1] = {arr->items[j]};
                Value *r = call_value(i, args[0], a, 1, "find");
                int ok = value_truthy(r); value_decref(r);
                if (ok) return value_incref(arr->items[j]);
            }
            return value_incref(XS_NULL_VAL);
        }
        if (strcmp(method, "any") == 0) {
            if (argc<1) return value_incref(XS_FALSE_VAL);
            for (int j=0;j<arr->len;j++) {
                Value *a[1] = {arr->items[j]};
                Value *r = call_value(i, args[0], a, 1, "any");
                int ok = value_truthy(r); value_decref(r);
                if (ok) return value_incref(XS_TRUE_VAL);
            }
            return value_incref(XS_FALSE_VAL);
        }
        if (strcmp(method, "all") == 0) {
            if (argc<1) return value_incref(XS_TRUE_VAL);
            for (int j=0;j<arr->len;j++) {
                Value *a[1] = {arr->items[j]};
                Value *r = call_value(i, args[0], a, 1, "all");
                int ok = value_truthy(r); value_decref(r);
                if (!ok) return value_incref(XS_FALSE_VAL);
            }
            return value_incref(XS_TRUE_VAL);
        }
        if (strcmp(method, "sort") == 0) {
            /* simple insertion sort */
            Value *cmp_fn = (argc>0)?args[0]:NULL;
            for (int a2=1; a2<arr->len; a2++) {
                Value *key = arr->items[a2]; int b2 = a2-1;
                while (b2 >= 0) {
                    int cmp;
                    if (cmp_fn) {
                        Value *ca[2] = {arr->items[b2], key};
                        Value *r = call_value(i, cmp_fn, ca, 2, "sort");
                        cmp = (r->tag==XS_INT)?(int)r->i:0;
                        value_decref(r);
                    } else {
                        cmp = value_cmp(arr->items[b2], key);
                    }
                    if (cmp <= 0) break;
                    arr->items[b2+1] = arr->items[b2]; b2--;
                }
                arr->items[b2+1] = key;
            }
            return value_incref(obj);
        }
        if (strcmp(method, "sorted") == 0) {
            Value *copy = xs_array_new();
            for (int j=0;j<arr->len;j++) array_push(copy->arr, value_incref(arr->items[j]));
            /* sort the copy */
            Value *sort_args[1] = {argc>0?args[0]:NULL};
            return eval_method(i, copy, "sort", sort_args, argc>0?1:0);
        }
        if (strcmp(method, "sort_by") == 0) {
            /* sort_by(key_fn) — sort a copy by extracted key */
            if (argc < 1) return value_incref(XS_NULL_VAL);
            Value *key_fn = args[0];
            Value *copy = xs_array_new();
            for (int j=0;j<arr->len;j++) array_push(copy->arr, value_incref(arr->items[j]));
            XSArray *ca = copy->arr;
            /* extract keys */
            Value **keys = xs_malloc(ca->len * sizeof(Value*));
            for (int j=0;j<ca->len;j++) {
                Value *arg[1] = {ca->items[j]};
                keys[j] = call_value(i, key_fn, arg, 1, "sort_by");
            }
            /* insertion sort by keys */
            for (int a2=1; a2<ca->len; a2++) {
                Value *kv = keys[a2]; Value *item = ca->items[a2]; int b2 = a2-1;
                while (b2 >= 0 && value_cmp(keys[b2], kv) > 0) {
                    ca->items[b2+1] = ca->items[b2];
                    keys[b2+1] = keys[b2];
                    b2--;
                }
                ca->items[b2+1] = item;
                keys[b2+1] = kv;
            }
            for (int j=0;j<ca->len;j++) value_decref(keys[j]);
            free(keys);
            return copy;
        }
        if (strcmp(method, "reverse") == 0) {
            for (int l2=0,r2=arr->len-1; l2<r2; l2++,r2--) {
                Value *tmp = arr->items[l2];
                arr->items[l2]=arr->items[r2]; arr->items[r2]=tmp;
            }
            return value_incref(obj);
        }
        if (strcmp(method, "reversed") == 0) {
            Value *copy = xs_array_new();
            for (int j=arr->len-1;j>=0;j--) array_push(copy->arr, value_incref(arr->items[j]));
            return copy;
        }
        if (strcmp(method, "join") == 0) {
            const char *sep = (argc>0&&args[0]->tag==XS_STR)?args[0]->s:"";
            int seplen=(int)strlen(sep), total=0;
            char **strs = xs_malloc(arr->len*sizeof(char*));
            for (int j=0;j<arr->len;j++) {
                strs[j]=value_str(arr->items[j]);
                total+=(int)strlen(strs[j]);
                if (j<arr->len-1) total+=seplen;
            }
            char *res=xs_malloc(total+1); res[0]='\0';
            for (int j=0;j<arr->len;j++) {
                strcat(res,strs[j]); free(strs[j]);
                if (j<arr->len-1) strcat(res,sep);
            }
            free(strs);
            Value *v=xs_str(res); free(res); return v;
        }
        if (strcmp(method, "slice") == 0) {
            int start2=0, end2=arr->len;
            if (argc>0&&args[0]->tag==XS_INT) start2=(int)args[0]->i;
            if (argc>1&&args[1]->tag==XS_INT) end2=(int)args[1]->i;
            if (start2<0) start2=arr->len+start2;
            if (end2<0)   end2=arr->len+end2;
            if (start2<0) start2=0;
            if (end2>arr->len) end2=arr->len;
            Value *res=xs_array_new();
            for (int j=start2;j<end2;j++) array_push(res->arr, value_incref(arr->items[j]));
            return res;
        }
        if (strcmp(method, "index_of") == 0 || strcmp(method, "find_index") == 0) {
            if (argc<1) return xs_int(-1);
            for (int j=0;j<arr->len;j++)
                if (value_equal(arr->items[j],args[0])) return xs_int(j);
            return xs_int(-1);
        }
        if (strcmp(method, "flatten") == 0) {
            Value *res=xs_array_new();
            for (int j=0;j<arr->len;j++) {
                if (arr->items[j]->tag==XS_ARRAY) {
                    XSArray *inner=arr->items[j]->arr;
                    for (int k=0;k<inner->len;k++) array_push(res->arr, value_incref(inner->items[k]));
                } else {
                    array_push(res->arr, value_incref(arr->items[j]));
                }
            }
            return res;
        }
        if (strcmp(method, "zip") == 0) {
            if (argc<1||args[0]->tag!=XS_ARRAY) return value_incref(obj);
            XSArray *other=args[0]->arr;
            int n2=arr->len<other->len?arr->len:other->len;
            Value *res=xs_array_new();
            for (int j=0;j<n2;j++) {
                Value *tup=xs_tuple_new();
                array_push(tup->arr, value_incref(arr->items[j]));
                array_push(tup->arr, value_incref(other->items[j]));
                array_push(res->arr, tup);
            }
            return res;
        }
        if (strcmp(method, "enumerate") == 0) {
            int64_t start_idx = 0;
            if (argc >= 1 && args[0]->tag == XS_INT) start_idx = args[0]->i;
            else if (argc >= 1 && args[0]->tag == XS_FLOAT) start_idx = (int64_t)args[0]->f;
            Value *res=xs_array_new();
            for (int j=0;j<arr->len;j++) {
                Value *tup=xs_tuple_new();
                array_push(tup->arr, xs_int(start_idx + j));
                array_push(tup->arr, value_incref(arr->items[j]));
                array_push(res->arr, tup);
            }
            return res;
        }
        if (strcmp(method, "sum") == 0) {
            int64_t si=0; double sf=0; int is_f=0;
            for (int j=0;j<arr->len;j++) {
                if (arr->items[j]->tag==XS_FLOAT){is_f=1;sf+=arr->items[j]->f;}
                else if(arr->items[j]->tag==XS_INT) si+=arr->items[j]->i;
            }
            return is_f?xs_float(sf+(double)si):xs_int(si);
        }
        if (strcmp(method, "min") == 0) {
            if (arr->len==0) return value_incref(XS_NULL_VAL);
            Value *m=arr->items[0];
            for (int j=1;j<arr->len;j++) if (value_cmp(arr->items[j],m)<0) m=arr->items[j];
            return value_incref(m);
        }
        if (strcmp(method, "max") == 0) {
            if (arr->len==0) return value_incref(XS_NULL_VAL);
            Value *m=arr->items[0];
            for (int j=1;j<arr->len;j++) if (value_cmp(arr->items[j],m)>0) m=arr->items[j];
            return value_incref(m);
        }
        if (strcmp(method, "remove") == 0) {
            if (argc<1||args[0]->tag!=XS_INT) return value_incref(XS_NULL_VAL);
            int idx=(int)args[0]->i;
            if (idx<0) idx=arr->len+idx;
            if (idx<0||idx>=arr->len) return value_incref(XS_NULL_VAL);
            Value *v=arr->items[idx];
            for (int j=idx;j<arr->len-1;j++) arr->items[j]=arr->items[j+1];
            arr->len--;
            return v; /* consume refcount */
        }
        if (strcmp(method, "insert") == 0) {
            if (argc<2) return value_incref(XS_NULL_VAL);
            int idx=(int)((args[0]->tag==XS_INT)?args[0]->i:0);
            if (idx<0) idx=arr->len+idx;
            if (idx<0) idx=0;
            if (idx>arr->len) idx=arr->len;
            /* grow */
            if (arr->len>=arr->cap) {
                arr->cap=arr->cap?arr->cap*2:4;
                arr->items=xs_realloc(arr->items,arr->cap*sizeof(Value*));
            }
            for (int j=arr->len;j>idx;j--) arr->items[j]=arr->items[j-1];
            arr->items[idx]=value_incref(args[1]);
            arr->len++;
            return value_incref(XS_NULL_VAL);
        }
        if (strcmp(method, "concat") == 0 || strcmp(method, "extend") == 0) {
            if (argc<1) return value_incref(obj);
            Value *res=xs_array_new();
            for (int j=0;j<arr->len;j++) array_push(res->arr, value_incref(arr->items[j]));
            for (int j=0;j<argc;j++) {
                if (args[j]->tag==XS_ARRAY) {
                    XSArray *other=args[j]->arr;
                    for (int k=0;k<other->len;k++) array_push(res->arr, value_incref(other->items[k]));
                }
            }
            return res;
        }
        if (strcmp(method, "clone") == 0 || strcmp(method, "copy") == 0) {
            Value *copy=xs_array_new();
            for (int j=0;j<arr->len;j++) array_push(copy->arr, value_incref(arr->items[j]));
            return copy;
        }
        /* for_each(fn) */
        if (strcmp(method, "for_each") == 0 || strcmp(method, "each") == 0) {
            if (argc<1) return value_incref(XS_NULL_VAL);
            for (int j=0;j<arr->len;j++) {
                Value *a[1]={arr->items[j]};
                Value *r=call_value(i, args[0], a, 1, "for_each");
                value_decref(r);
                if (i->cf.signal) break;
            }
            return value_incref(XS_NULL_VAL);
        }
        /* count(fn_or_val) */
        if (strcmp(method, "count") == 0) {
            if (argc<1) return xs_int(arr->len);
            int64_t cnt=0;
            for (int j=0;j<arr->len;j++) {
                Value *a[1]={arr->items[j]};
                if (args[0]->tag==XS_FUNC||args[0]->tag==XS_NATIVE) {
                    Value *r=call_value(i, args[0], a, 1, "count");
                    int ok=value_truthy(r); value_decref(r);
                    if (!i->cf.signal && ok) cnt++;
                } else {
                    if (value_equal(arr->items[j],args[0])) cnt++;
                }
                if (i->cf.signal) break;
            }
            return xs_int(cnt);
        }
        /* take(n) */
        if (strcmp(method, "take") == 0) {
            int n2=(argc>0&&args[0]->tag==XS_INT)?(int)args[0]->i:0;
            if (n2>arr->len) n2=arr->len;
            Value *res=xs_array_new();
            for (int j=0;j<n2;j++) array_push(res->arr, value_incref(arr->items[j]));
            return res;
        }
        /* skip(n) / drop(n) */
        if (strcmp(method, "skip") == 0 || strcmp(method, "drop") == 0) {
            int n2=(argc>0&&args[0]->tag==XS_INT)?(int)args[0]->i:0;
            if (n2<0) n2=0;
            if (n2>arr->len) n2=arr->len;
            Value *res=xs_array_new();
            for (int j=n2;j<arr->len;j++) array_push(res->arr, value_incref(arr->items[j]));
            return res;
        }
        /* chunk(n) */
        if (strcmp(method, "chunk") == 0) {
            int n2=(argc>0&&args[0]->tag==XS_INT)?(int)args[0]->i:1;
            if (n2<1) n2=1;
            Value *res=xs_array_new();
            for (int j=0;j<arr->len;j+=n2) {
                Value *chunk=xs_array_new();
                for (int k=j;k<j+n2&&k<arr->len;k++) array_push(chunk->arr, value_incref(arr->items[k]));
                array_push(res->arr, chunk);
            }
            return res;
        }
        /* group_by(key_fn) */
        if (strcmp(method, "group_by") == 0) {
            if (argc<1) return value_incref(XS_NULL_VAL);
            Value *res=xs_map_new();
            for (int j=0;j<arr->len;j++) {
                Value *a[1]={arr->items[j]};
                Value *k=call_value(i, args[0], a, 1, "group_by");
                if (i->cf.signal) { value_decref(k); break; }
                char *ks=value_str(k); value_decref(k);
                Value *bucket=map_get(res->map, ks);
                if (!bucket) {
                    bucket=xs_array_new();
                    map_set(res->map, ks, bucket);
                    value_decref(bucket);
                    bucket=map_get(res->map, ks);
                }
                array_push(bucket->arr, value_incref(arr->items[j]));
                free(ks);
            }
            return res;
        }
        /* partition(pred) */
        if (strcmp(method, "partition") == 0) {
            if (argc<1) return value_incref(XS_NULL_VAL);
            Value *trues=xs_array_new(), *falses=xs_array_new();
            for (int j=0;j<arr->len;j++) {
                Value *a[1]={arr->items[j]};
                Value *r=call_value(i, args[0], a, 1, "partition");
                int ok=value_truthy(r); value_decref(r);
                if (!i->cf.signal) {
                    if (ok) array_push(trues->arr, value_incref(arr->items[j]));
                    else    array_push(falses->arr, value_incref(arr->items[j]));
                }
                if (i->cf.signal) break;
            }
            Value *res=xs_tuple_new();
            array_push(res->arr, trues); array_push(res->arr, falses);
            return res;
        }
        /* window(n) */
        if (strcmp(method, "window") == 0) {
            int n2=(argc>0&&args[0]->tag==XS_INT)?(int)args[0]->i:1;
            if (n2<1) n2=1;
            Value *res=xs_array_new();
            for (int j=0;j+n2<=arr->len;j++) {
                Value *win=xs_array_new();
                for (int k=j;k<j+n2;k++) array_push(win->arr, value_incref(arr->items[k]));
                array_push(res->arr, win);
            }
            return res;
        }
        /* sum_by(fn) */
        if (strcmp(method, "sum_by") == 0) {
            if (argc<1) return xs_int(0);
            double sf=0; int64_t si=0; int is_f=0;
            for (int j=0;j<arr->len;j++) {
                Value *a[1]={arr->items[j]};
                Value *r=call_value(i, args[0], a, 1, "sum_by");
                if (!i->cf.signal) {
                    if (r->tag==XS_FLOAT){is_f=1;sf+=r->f;}
                    else if(r->tag==XS_INT) si+=r->i;
                }
                value_decref(r);
                if (i->cf.signal) break;
            }
            return is_f?xs_float(sf+(double)si):xs_int(si);
        }
        /* min_by(fn) */
        if (strcmp(method, "min_by") == 0) {
            if (argc<1||arr->len==0) return value_incref(XS_NULL_VAL);
            Value *best=arr->items[0];
            Value *best_key; { Value *a[1]={best}; best_key=call_value(i, args[0], a, 1, "min_by"); }
            for (int j=1;j<arr->len;j++) {
                Value *a[1]={arr->items[j]};
                Value *k=call_value(i, args[0], a, 1, "min_by");
                if (!i->cf.signal && value_cmp(k, best_key)<0) {
                    value_decref(best_key); best_key=k; best=arr->items[j];
                } else value_decref(k);
                if (i->cf.signal) break;
            }
            value_decref(best_key);
            return value_incref(best);
        }
        /* max_by(fn) */
        if (strcmp(method, "max_by") == 0) {
            if (argc<1||arr->len==0) return value_incref(XS_NULL_VAL);
            Value *best=arr->items[0];
            Value *best_key; { Value *a[1]={best}; best_key=call_value(i, args[0], a, 1, "max_by"); }
            for (int j=1;j<arr->len;j++) {
                Value *a[1]={arr->items[j]};
                Value *k=call_value(i, args[0], a, 1, "max_by");
                if (!i->cf.signal && value_cmp(k, best_key)>0) {
                    value_decref(best_key); best_key=k; best=arr->items[j];
                } else value_decref(k);
                if (i->cf.signal) break;
            }
            value_decref(best_key);
            return value_incref(best);
        }
        /* zip_with(other, fn) */
        if (strcmp(method, "zip_with") == 0) {
            if (argc<2||args[0]->tag!=XS_ARRAY) return value_incref(XS_NULL_VAL);
            XSArray *other=args[0]->arr;
            int n2=arr->len<other->len?arr->len:other->len;
            Value *res=xs_array_new();
            for (int j=0;j<n2;j++) {
                Value *a[2]={arr->items[j], other->items[j]};
                Value *r=call_value(i, args[1], a, 2, "zip_with");
                if (!i->cf.signal) array_push(res->arr, r); else { value_decref(r); break; }
            }
            return res;
        }
        /* intersperse(val) */
        if (strcmp(method, "intersperse") == 0) {
            if (argc<1) return value_incref(obj);
            Value *res=xs_array_new();
            for (int j=0;j<arr->len;j++) {
                array_push(res->arr, value_incref(arr->items[j]));
                if (j<arr->len-1) array_push(res->arr, value_incref(args[0]));
            }
            return res;
        }
        /* rotate(n) */
        if (strcmp(method, "rotate") == 0) {
            int n2=(argc>0&&args[0]->tag==XS_INT)?(int)args[0]->i:0;
            if (arr->len==0) return xs_array_new();
            n2=((n2%arr->len)+arr->len)%arr->len;
            Value *res=xs_array_new();
            for (int j=n2;j<arr->len;j++) array_push(res->arr, value_incref(arr->items[j]));
            for (int j=0;j<n2;j++) array_push(res->arr, value_incref(arr->items[j]));
            return res;
        }
        /* take_while(pred) */
        if (strcmp(method, "take_while") == 0) {
            if (argc<1) return value_incref(obj);
            Value *res=xs_array_new();
            for (int j=0;j<arr->len;j++) {
                Value *a[1]={arr->items[j]};
                Value *r=call_value(i, args[0], a, 1, "take_while");
                int ok=value_truthy(r); value_decref(r);
                if (!ok || i->cf.signal) break;
                array_push(res->arr, value_incref(arr->items[j]));
            }
            return res;
        }
        /* drop_while(pred) */
        if (strcmp(method, "drop_while") == 0) {
            if (argc<1) return value_incref(obj);
            Value *res=xs_array_new();
            int dropping=1;
            for (int j=0;j<arr->len;j++) {
                if (dropping) {
                    Value *a[1]={arr->items[j]};
                    Value *r=call_value(i, args[0], a, 1, "drop_while");
                    int ok=value_truthy(r); value_decref(r);
                    if (!ok || i->cf.signal) dropping=0;
                    if (i->cf.signal) break;
                    if (dropping) continue;
                }
                array_push(res->arr, value_incref(arr->items[j]));
            }
            return res;
        }
        /* scan(init, fn) */
        if (strcmp(method, "scan") == 0) {
            if (argc<2) return value_incref(XS_NULL_VAL);
            Value *res=xs_array_new();
            Value *acc=value_incref(args[0]);
            array_push(res->arr, value_incref(acc));
            for (int j=0;j<arr->len;j++) {
                Value *a[2]={acc, arr->items[j]};
                Value *r=call_value(i, args[1], a, 2, "scan");
                value_decref(acc); acc=r;
                if (!i->cf.signal) array_push(res->arr, value_incref(acc));
                if (i->cf.signal) break;
            }
            value_decref(acc);
            return res;
        }
        /* product */
        if (strcmp(method, "product") == 0) {
            if (arr->len==0) return xs_int(1);
            int64_t pi=1; double pf=1.0; int is_f=0;
            for (int j=0;j<arr->len;j++) {
                if (arr->items[j]->tag==XS_FLOAT){is_f=1;pf*=arr->items[j]->f;}
                else if(arr->items[j]->tag==XS_INT) pi*=arr->items[j]->i;
            }
            return is_f?xs_float(pf*(double)pi):xs_int(pi);
        }
        /* prepend(val) */
        if (strcmp(method, "prepend") == 0) {
            if (argc<1) return value_incref(XS_NULL_VAL);
            if (arr->len>=arr->cap) {
                arr->cap=arr->cap?arr->cap*2:4;
                arr->items=xs_realloc(arr->items,arr->cap*sizeof(Value*));
            }
            for (int j=arr->len;j>0;j--) arr->items[j]=arr->items[j-1];
            arr->items[0]=value_incref(args[0]);
            arr->len++;
            return value_incref(XS_NULL_VAL);
        }
        /* clear */
        if (strcmp(method, "clear") == 0) {
            for (int j=0;j<arr->len;j++) value_decref(arr->items[j]);
            arr->len=0;
            return value_incref(XS_NULL_VAL);
        }
        /* shuffle */
        if (strcmp(method, "shuffle") == 0) {
            for (int j=arr->len-1;j>0;j--) {
                int k=rand()%(j+1);
                Value *tmp=arr->items[j]; arr->items[j]=arr->items[k]; arr->items[k]=tmp;
            }
            return value_incref(obj);
        }
        /* sample(n) */
        if (strcmp(method, "sample") == 0) {
            int n2=(argc>0&&args[0]->tag==XS_INT)?(int)args[0]->i:1;
            if (n2>arr->len) n2=arr->len;
            /* build index array and partial Fisher-Yates */
            int *idx=xs_malloc(arr->len*sizeof(int));
            for (int j=0;j<arr->len;j++) idx[j]=j;
            Value *res=xs_array_new();
            for (int j=0;j<n2;j++) {
                int k=j+rand()%(arr->len-j);
                int tmp=idx[j]; idx[j]=idx[k]; idx[k]=tmp;
                array_push(res->arr, value_incref(arr->items[idx[j]]));
            }
            free(idx);
            return res;
        }
        /* flat_map(fn) */
        if (strcmp(method, "flat_map") == 0) {
            if (argc<1) return value_incref(obj);
            Value *res=xs_array_new();
            for (int j=0;j<arr->len;j++) {
                Value *a[1]={arr->items[j]};
                Value *r=call_value(i, args[0], a, 1, "flat_map");
                if (!i->cf.signal) {
                    if (r->tag==XS_ARRAY) {
                        for (int k=0;k<r->arr->len;k++) array_push(res->arr, value_incref(r->arr->items[k]));
                        value_decref(r);
                    } else {
                        array_push(res->arr, r);
                    }
                } else { value_decref(r); break; }
            }
            return res;
        }
        /* dedup / unique */
        if (strcmp(method, "dedup") == 0 || strcmp(method, "unique") == 0) {
            Value *res=xs_array_new();
            for (int j=0;j<arr->len;j++) {
                int found=0;
                for (int k=0;k<res->arr->len;k++) {
                    if (value_equal(res->arr->items[k], arr->items[j])) { found=1; break; }
                }
                if (!found) array_push(res->arr, value_incref(arr->items[j]));
            }
            return res;
        }
        /* frequencies */
        if (strcmp(method, "frequencies") == 0) {
            Value *res=xs_map_new();
            for (int j=0;j<arr->len;j++) {
                char *ks=value_str(arr->items[j]);
                Value *cur=map_get(res->map, ks);
                int64_t cnt2=cur&&cur->tag==XS_INT?cur->i:0;
                Value *nv=xs_int(cnt2+1);
                map_set(res->map, ks, nv);
                value_decref(nv); free(ks);
            }
            return res;
        }
        if (strcmp(method, "to_str") == 0 || strcmp(method, "to_string") == 0) {
            char *r = value_repr(obj); Value *v = xs_str(r); free(r); return v;
        }
    }

    // --- map methods
    if (obj->tag == XS_MAP || obj->tag == XS_MODULE) {
        XSMap *m = obj->map;
        /* Generator iterator protocol */
        {
            Value *ct = map_get(m, "__type");
            if (ct && ct->tag == XS_STR && strcmp(ct->s, "generator") == 0) {
                if (strcmp(method, "next") == 0) {
                    Value *yields = map_get(m, "_yields");
                    Value *idx_v  = map_get(m, "_index");
                    int idx = idx_v && idx_v->tag == XS_INT ? (int)idx_v->i : 0;
                    Value *result = xs_map_new();
                    if (yields && yields->tag == XS_ARRAY && idx < yields->arr->len) {
                        map_set(result->map, "value", value_incref(yields->arr->items[idx]));
                        Value *dv = value_incref(XS_FALSE_VAL);
                        map_set(result->map, "done", dv); value_decref(dv);
                        Value *new_idx = xs_int(idx + 1);
                        map_set(m, "_index", new_idx); value_decref(new_idx);
                    } else {
                        map_set(result->map, "value", value_incref(XS_NULL_VAL));
                        Value *dv = value_incref(XS_TRUE_VAL);
                        map_set(result->map, "done", dv); value_decref(dv);
                        Value *done_v = value_incref(XS_TRUE_VAL);
                        map_set(m, "_done", done_v); value_decref(done_v);
                    }
                    return result;
                }
            }
        }
        /* User-defined callables stored in map take priority over built-in methods
         * (e.g. {get: fn, set: fn} state accessor objects) */
        {
            Value *fn = map_get(m, method);
            if (fn && (fn->tag == XS_FUNC || fn->tag == XS_NATIVE)) {
                return call_value(i, fn, args, argc, method);
            }
        }
        /* Stopwatch object: has _start field */
        if ((strcmp(method,"elapsed")==0||strcmp(method,"elapsed_ms")==0) &&
            map_has(m,"_start")) {
            Value *sv = map_get(m, "_start");
            double start = sv ? (sv->tag==XS_FLOAT ? sv->f : (double)sv->i) : 0.0;
            struct timespec ts2; clock_gettime(CLOCK_REALTIME, &ts2);
            double now2 = (double)ts2.tv_sec + (double)ts2.tv_nsec/1e9;
            double elapsed = now2 - start;
            if (strcmp(method,"elapsed_ms")==0) elapsed *= 1000.0;
            return xs_float(elapsed);
        }
        if (strcmp(method, "reset") == 0 && map_has(m, "_start")) {
            struct timespec ts2; clock_gettime(CLOCK_REALTIME, &ts2);
            double now2 = (double)ts2.tv_sec + (double)ts2.tv_nsec/1e9;
            Value *nv = xs_float(now2);
            map_set(m, "_start", nv); value_decref(nv);
            return value_incref(XS_NULL_VAL);
        }
        /* Collections early dispatch (must be before generic len/is_empty/get) */
        {
            Value *ct = map_get(m, "_type");
            if (ct && ct->tag == XS_STR) {
                /* All collection types: route to full dispatch for any method */
                if (strcmp(ct->s,"Stack")==0||strcmp(ct->s,"PriorityQueue")==0||
                    strcmp(ct->s,"Deque")==0||strcmp(ct->s,"Set")==0||
                    strcmp(ct->s,"OrderedMap")==0||strcmp(ct->s,"Counter")==0) {
                    goto collections_full_dispatch;
                }
                /* Signal reactive primitive */
                if (strcmp(ct->s,"Signal")==0) {
                    if (strcmp(method,"get")==0) {
                        Value *v=map_get(m,"_val");
                        return v?value_incref(v):value_incref(XS_NULL_VAL);
                    }
                    if (strcmp(method,"set")==0) {
                        if (argc<1) return value_incref(XS_NULL_VAL);
                        map_set(m,"_val",value_incref(args[0]));
                        /* call subscribers */
                        Value *subs=map_get(m,"_subs");
                        if (subs&&subs->tag==XS_ARRAY) {
                            for (int si=0;si<subs->arr->len;si++)
                                { Value *r=call_value(i, subs->arr->items[si], args, 1, "signal_sub"); if(r) value_decref(r); }
                        }
                        return value_incref(XS_NULL_VAL);
                    }
                    if (strcmp(method,"subscribe")==0) {
                        if (argc<1) return value_incref(XS_NULL_VAL);
                        Value *subs=map_get(m,"_subs");
                        if (subs&&subs->tag==XS_ARRAY)
                            array_push(subs->arr, value_incref(args[0]));
                        return value_incref(XS_NULL_VAL);
                    }
                }
                /* Derived reactive primitive */
                if (strcmp(ct->s,"Derived")==0) {
                    if (strcmp(method,"get")==0) {
                        Value *fn=map_get(m,"_fn");
                        if (!fn) return value_incref(XS_NULL_VAL);
                        return call_value(i, fn, NULL, 0, "derived_get");
                    }
                }
                /* Channel: send/recv/len */
                if (strcmp(ct->s,"Channel")==0) {
                    Value *data = map_get(m, "_data");
                    if (!data || data->tag != XS_ARRAY)
                        return value_incref(XS_NULL_VAL);
                    XSArray *da = data->arr;
                    if (strcmp(method,"send")==0) {
                        if (argc < 1) return value_incref(XS_NULL_VAL);
                        /* Check capacity */
                        Value *cap_v = map_get(m, "_cap");
                        int cap = cap_v ? (int)cap_v->i : 0;
                        if (cap > 0 && da->len >= cap) {
                            fprintf(stderr, "xs: error at %s:%d:%d: channel: buffer full (capacity %d)\n",
                                    i->current_span.file ? i->current_span.file : "<unknown>",
                                    i->current_span.line, i->current_span.col, cap);
                            return value_incref(XS_NULL_VAL);
                        }
                        array_push(da, value_incref(args[0]));
                        return value_incref(XS_NULL_VAL);
                    }
                    if (strcmp(method,"recv")==0) {
                        if (da->len == 0) {
                            fprintf(stderr, "xs: error at %s:%d:%d: channel: recv on empty channel\n",
                                    i->current_span.file ? i->current_span.file : "<unknown>",
                                    i->current_span.line, i->current_span.col);
                            return value_incref(XS_NULL_VAL);
                        }
                        /* Dequeue from front */
                        Value *v = da->items[0];
                        for (int idx2 = 1; idx2 < da->len; idx2++)
                            da->items[idx2-1] = da->items[idx2];
                        da->len--;
                        /* v is already ref-counted (owned by array slot) */
                        return v; /* transfer ownership */
                    }
                    if (strcmp(method,"len")==0) return xs_int(da->len);
                    if (strcmp(method,"is_empty")==0)
                        return da->len==0?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
                    if (strcmp(method,"is_full")==0) {
                        Value *cap_v = map_get(m, "_cap");
                        int cap = cap_v ? (int)cap_v->i : 0;
                        return (cap > 0 && da->len >= cap)?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
                    }
                }
                /* Counter is now routed via the early goto above */
            }
        }
        if (strcmp(method, "len") == 0 || strcmp(method, "length") == 0 ||
            strcmp(method, "count") == 0 || strcmp(method, "size") == 0) return xs_int(m->len);
        if (strcmp(method, "keys") == 0) {
            int nk=0; char **ks=map_keys(m,&nk);
            Value *arr=xs_array_new();
            for (int j=0;j<nk;j++) { array_push(arr->arr, xs_str(ks[j])); free(ks[j]); }
            free(ks); return arr;
        }
        if (strcmp(method, "values") == 0) {
            int nk=0; char **ks=map_keys(m,&nk);
            Value *arr=xs_array_new();
            for (int j=0;j<nk;j++) {
                Value *v=map_get(m,ks[j]);
                if (v) array_push(arr->arr,value_incref(v));
                free(ks[j]);
            }
            free(ks); return arr;
        }
        if (strcmp(method, "entries") == 0 || strcmp(method, "items") == 0) {
            int nk=0; char **ks=map_keys(m,&nk);
            Value *arr=xs_array_new();
            for (int j=0;j<nk;j++) {
                Value *v=map_get(m,ks[j]);
                Value *tup=xs_tuple_new();
                array_push(tup->arr, xs_str(ks[j]));
                array_push(tup->arr, v?value_incref(v):value_incref(XS_NULL_VAL));
                array_push(arr->arr, tup);
                free(ks[j]);
            }
            free(ks); return arr;
        }
        if (strcmp(method, "has") == 0 || strcmp(method, "contains_key") == 0 ||
            strcmp(method, "has_key") == 0) {
            if (argc<1||args[0]->tag!=XS_STR) return value_incref(XS_FALSE_VAL);
            return map_has(m,args[0]->s)?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
        }
        if (strcmp(method, "get") == 0) {
            if (argc<1||args[0]->tag!=XS_STR) return value_incref(XS_NULL_VAL);
            Value *v=map_get(m,args[0]->s);
            if (v) return value_incref(v);
            return argc>1?value_incref(args[1]):value_incref(XS_NULL_VAL);
        }
        if (strcmp(method, "set") == 0 || strcmp(method, "insert") == 0) {
            if (argc<2||args[0]->tag!=XS_STR) return value_incref(XS_NULL_VAL);
            map_set(m, args[0]->s, value_incref(args[1]));
            return value_incref(XS_NULL_VAL);
        }
        if (strcmp(method, "remove") == 0 || strcmp(method, "delete") == 0) {
            if (argc<1||args[0]->tag!=XS_STR) return value_incref(XS_NULL_VAL);
            map_del(m, args[0]->s);
            return value_incref(XS_NULL_VAL);
        }
        if (strcmp(method, "is_empty") == 0) {
            return m->len==0?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
        }
        /* clone / copy */
        if (strcmp(method, "clone") == 0 || strcmp(method, "copy") == 0) {
            Value *res=xs_map_new();
            int nk=0; char **ks=map_keys(m,&nk);
            for (int j=0;j<nk;j++) {
                Value *v=map_get(m,ks[j]);
                if (v) map_set(res->map, ks[j], value_incref(v));
                free(ks[j]);
            }
            free(ks); return res;
        }
        /* map(fn) — apply fn(key, val) to each entry, return new map */
        if (strcmp(method, "map") == 0) {
            if (argc<1) return value_incref(obj);
            Value *res=xs_map_new();
            int nk=0; char **ks=map_keys(m,&nk);
            for (int j=0;j<nk;j++) {
                Value *v=map_get(m,ks[j]);
                Value *kv=xs_str(ks[j]);
                Value *a[2]={kv, v?v:XS_NULL_VAL};
                Value *r=call_value(i, args[0], a, 2, "map");
                value_decref(kv);
                if (!i->cf.signal) { map_set(res->map, ks[j], r); value_decref(r); }
                else { value_decref(r); }
                free(ks[j]);
                if (i->cf.signal) {
                    for (int j2=j+1; j2<nk; j2++) free(ks[j2]);
                    break;
                }
            }
            free(ks); return res;
        }
        /* filter(fn) — filter entries by fn(key, val), return new map */
        if (strcmp(method, "filter") == 0) {
            if (argc<1) return value_incref(obj);
            Value *res=xs_map_new();
            int nk=0; char **ks=map_keys(m,&nk);
            for (int j=0;j<nk;j++) {
                Value *v=map_get(m,ks[j]);
                Value *kv=xs_str(ks[j]);
                Value *a[2]={kv, v?v:XS_NULL_VAL};
                Value *r=call_value(i, args[0], a, 2, "filter");
                value_decref(kv);
                int ok=value_truthy(r); value_decref(r);
                if (!i->cf.signal && ok && v) map_set(res->map, ks[j], value_incref(v));
                free(ks[j]);
                if (i->cf.signal) break;
            }
            free(ks); return res;
        }
        /* merge(other) — merge another map, return new map */
        if (strcmp(method, "merge") == 0) {
            Value *res=xs_map_new();
            int nk=0; char **ks=map_keys(m,&nk);
            for (int j=0;j<nk;j++) {
                Value *v=map_get(m,ks[j]);
                if (v) map_set(res->map, ks[j], value_incref(v));
                free(ks[j]);
            }
            free(ks);
            if (argc>0 && (args[0]->tag==XS_MAP||args[0]->tag==XS_MODULE)) {
                int nk2=0; char **ks2=map_keys(args[0]->map,&nk2);
                for (int j=0;j<nk2;j++) {
                    Value *v=map_get(args[0]->map,ks2[j]);
                    if (v) map_set(res->map, ks2[j], value_incref(v));
                    free(ks2[j]);
                }
                free(ks2);
            }
            return res;
        }
        /* Collections: Stack / PriorityQueue / Counter dispatch */
        collections_full_dispatch:;
        {
            Value *type_val = map_get(m, "_type");
            if (type_val && type_val->tag == XS_STR) {
                const char *ctype = type_val->s;
                Value *data = map_get(m, "_data");
                /* Stack */
                if (strcmp(ctype, "Stack") == 0 && data && data->tag == XS_ARRAY) {
                    XSArray *arr = data->arr;
                    if (strcmp(method, "push") == 0) {
                        if (argc >= 1) array_push(arr, value_incref(args[0]));
                        return value_incref(XS_NULL_VAL);
                    }
                    if (strcmp(method, "pop") == 0) {
                        if (arr->len == 0) return value_incref(XS_NULL_VAL);
                        Value *v = arr->items[arr->len-1];
                        arr->len--;
                        return v; /* transfer ownership */
                    }
                    if (strcmp(method, "peek") == 0) {
                        if (arr->len == 0) return value_incref(XS_NULL_VAL);
                        return value_incref(arr->items[arr->len-1]);
                    }
                    if (strcmp(method, "len") == 0) return xs_int(arr->len);
                    if (strcmp(method, "is_empty") == 0)
                        return arr->len==0?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
                    if (strcmp(method, "to_array") == 0) {
                        Value *res = xs_array_new();
                        for (int j = 0; j < arr->len; j++)
                            array_push(res->arr, value_incref(arr->items[j]));
                        return res;
                    }
                    if (strcmp(method, "clear") == 0) {
                        for (int j = 0; j < arr->len; j++)
                            value_decref(arr->items[j]);
                        arr->len = 0;
                        return value_incref(XS_NULL_VAL);
                    }
                }
                /* Deque */
                if (strcmp(ctype, "Deque") == 0 && data && data->tag == XS_ARRAY) {
                    XSArray *arr = data->arr;
                    if (strcmp(method, "push_back") == 0) {
                        if (argc >= 1) array_push(arr, value_incref(args[0]));
                        return value_incref(XS_NULL_VAL);
                    }
                    if (strcmp(method, "push_front") == 0) {
                        if (argc < 1) return value_incref(XS_NULL_VAL);
                        /* Extend array by one slot using a dummy value */
                        Value *dummy = value_incref(XS_NULL_VAL);
                        array_push(arr, dummy);
                        /* Now arr->len increased by 1. items[len-1] = dummy.
                           Shift existing items right: items[len-1..1] = items[len-2..0] */
                        for (int j = arr->len - 1; j > 0; j--)
                            arr->items[j] = arr->items[j-1];
                        /* items[0] still holds old items[0] (now also at items[1]).
                           Overwrite items[0] with the new value. The dummy NULL_VAL
                           that was at items[len-1] was overwritten by items[len-2] in
                           the shift, so we already lost that ref — decref it. */
                        value_decref(dummy);
                        arr->items[0] = value_incref(args[0]);
                        return value_incref(XS_NULL_VAL);
                    }
                    if (strcmp(method, "pop_back") == 0) {
                        if (arr->len == 0) return value_incref(XS_NULL_VAL);
                        Value *v = arr->items[arr->len-1];
                        arr->len--;
                        return v;
                    }
                    if (strcmp(method, "pop_front") == 0) {
                        if (arr->len == 0) return value_incref(XS_NULL_VAL);
                        Value *v = arr->items[0];
                        for (int j = 0; j < arr->len - 1; j++)
                            arr->items[j] = arr->items[j+1];
                        arr->len--;
                        return v;
                    }
                    if (strcmp(method, "len") == 0) return xs_int(arr->len);
                    if (strcmp(method, "is_empty") == 0)
                        return arr->len==0?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
                    if (strcmp(method, "to_array") == 0) {
                        Value *res = xs_array_new();
                        for (int j = 0; j < arr->len; j++)
                            array_push(res->arr, value_incref(arr->items[j]));
                        return res;
                    }
                }
                /* Set */
                if (strcmp(ctype, "Set") == 0) {
                    Value *sdata = map_get(m, "_data");
                    if (sdata && sdata->tag == XS_MAP) {
                        XSMap *sd = sdata->map;
                        if (strcmp(method, "add") == 0) {
                            if (argc >= 1) {
                                char *k = value_str(args[0]);
                                Value *tv = value_incref(XS_TRUE_VAL);
                                map_set(sd, k, tv); value_decref(tv);
                                free(k);
                            }
                            return value_incref(XS_NULL_VAL);
                        }
                        if (strcmp(method, "remove") == 0) {
                            if (argc >= 1) {
                                char *k = value_str(args[0]);
                                map_del(sd, k);
                                free(k);
                            }
                            return value_incref(XS_NULL_VAL);
                        }
                        if (strcmp(method, "contains") == 0) {
                            if (argc < 1) return value_incref(XS_FALSE_VAL);
                            char *k = value_str(args[0]);
                            int found = map_has(sd, k);
                            free(k);
                            return found ? value_incref(XS_TRUE_VAL) : value_incref(XS_FALSE_VAL);
                        }
                        if (strcmp(method, "len") == 0) return xs_int(sd->len);
                        if (strcmp(method, "is_empty") == 0)
                            return sd->len==0?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
                        if (strcmp(method, "to_array") == 0) {
                            Value *res = xs_array_new();
                            int nk = 0; char **ks = map_keys(sd, &nk);
                            for (int j = 0; j < nk; j++) {
                                array_push(res->arr, xs_str(ks[j]));
                                free(ks[j]);
                            }
                            free(ks);
                            return res;
                        }
                        if (strcmp(method, "union") == 0) {
                            Value *ns = xs_map_new();
                            Value *nt = xs_str("Set"); map_set(ns->map, "_type", nt); value_decref(nt);
                            Value *nd = xs_map_new(); map_set(ns->map, "_data", nd); value_decref(nd);
                            int nk = 0; char **ks = map_keys(sd, &nk);
                            for (int j = 0; j < nk; j++) {
                                Value *tv = value_incref(XS_TRUE_VAL);
                                map_set(nd->map, ks[j], tv); value_decref(tv);
                                free(ks[j]);
                            }
                            free(ks);
                            if (argc >= 1 && args[0]->tag == XS_MAP) {
                                Value *odata = map_get(args[0]->map, "_data");
                                if (odata && odata->tag == XS_MAP) {
                                    int nk2 = 0; char **ks2 = map_keys(odata->map, &nk2);
                                    for (int j = 0; j < nk2; j++) {
                                        Value *tv = value_incref(XS_TRUE_VAL);
                                        map_set(nd->map, ks2[j], tv); value_decref(tv);
                                        free(ks2[j]);
                                    }
                                    free(ks2);
                                }
                            }
                            return ns;
                        }
                        if (strcmp(method, "intersection") == 0) {
                            Value *ns = xs_map_new();
                            Value *nt = xs_str("Set"); map_set(ns->map, "_type", nt); value_decref(nt);
                            Value *nd = xs_map_new(); map_set(ns->map, "_data", nd); value_decref(nd);
                            if (argc >= 1 && args[0]->tag == XS_MAP) {
                                Value *odata = map_get(args[0]->map, "_data");
                                if (odata && odata->tag == XS_MAP) {
                                    int nk = 0; char **ks = map_keys(sd, &nk);
                                    for (int j = 0; j < nk; j++) {
                                        if (map_has(odata->map, ks[j])) {
                                            Value *tv = value_incref(XS_TRUE_VAL);
                                            map_set(nd->map, ks[j], tv); value_decref(tv);
                                        }
                                        free(ks[j]);
                                    }
                                    free(ks);
                                }
                            }
                            return ns;
                        }
                        if (strcmp(method, "difference") == 0) {
                            Value *ns = xs_map_new();
                            Value *nt = xs_str("Set"); map_set(ns->map, "_type", nt); value_decref(nt);
                            Value *nd = xs_map_new(); map_set(ns->map, "_data", nd); value_decref(nd);
                            int nk = 0; char **ks = map_keys(sd, &nk);
                            if (argc >= 1 && args[0]->tag == XS_MAP) {
                                Value *odata = map_get(args[0]->map, "_data");
                                if (odata && odata->tag == XS_MAP) {
                                    for (int j = 0; j < nk; j++) {
                                        if (!map_has(odata->map, ks[j])) {
                                            Value *tv = value_incref(XS_TRUE_VAL);
                                            map_set(nd->map, ks[j], tv); value_decref(tv);
                                        }
                                        free(ks[j]);
                                    }
                                } else {
                                    for (int j = 0; j < nk; j++) {
                                        Value *tv = value_incref(XS_TRUE_VAL);
                                        map_set(nd->map, ks[j], tv); value_decref(tv);
                                        free(ks[j]);
                                    }
                                }
                            } else {
                                for (int j = 0; j < nk; j++) {
                                    Value *tv = value_incref(XS_TRUE_VAL);
                                    map_set(nd->map, ks[j], tv); value_decref(tv);
                                    free(ks[j]);
                                }
                            }
                            free(ks);
                            return ns;
                        }
                    }
                }
                /* OrderedMap */
                if (strcmp(ctype, "OrderedMap") == 0) {
                    Value *om_keys = map_get(m, "_keys");
                    Value *om_data = map_get(m, "_data");
                    if (om_keys && om_keys->tag == XS_ARRAY && om_data && om_data->tag == XS_MAP) {
                        XSArray *ka = om_keys->arr;
                        XSMap *dm = om_data->map;
                        if (strcmp(method, "set") == 0) {
                            if (argc < 2 || args[0]->tag != XS_STR) return value_incref(XS_NULL_VAL);
                            const char *key = args[0]->s;
                            if (!map_has(dm, key))
                                array_push(ka, value_incref(args[0]));
                            map_set(dm, key, value_incref(args[1]));
                            return value_incref(XS_NULL_VAL);
                        }
                        if (strcmp(method, "get") == 0) {
                            if (argc < 1 || args[0]->tag != XS_STR) return value_incref(XS_NULL_VAL);
                            Value *v = map_get(dm, args[0]->s);
                            if (v) return value_incref(v);
                            return argc > 1 ? value_incref(args[1]) : value_incref(XS_NULL_VAL);
                        }
                        if (strcmp(method, "delete") == 0) {
                            if (argc < 1 || args[0]->tag != XS_STR) return value_incref(XS_NULL_VAL);
                            const char *key = args[0]->s;
                            map_del(dm, key);
                            for (int j = 0; j < ka->len; j++) {
                                if (ka->items[j]->tag == XS_STR && strcmp(ka->items[j]->s, key) == 0) {
                                    value_decref(ka->items[j]);
                                    for (int k = j; k < ka->len - 1; k++)
                                        ka->items[k] = ka->items[k+1];
                                    ka->len--;
                                    break;
                                }
                            }
                            return value_incref(XS_NULL_VAL);
                        }
                        if (strcmp(method, "has") == 0) {
                            if (argc < 1 || args[0]->tag != XS_STR) return value_incref(XS_FALSE_VAL);
                            return map_has(dm, args[0]->s) ? value_incref(XS_TRUE_VAL) : value_incref(XS_FALSE_VAL);
                        }
                        if (strcmp(method, "keys") == 0) {
                            Value *res = xs_array_new();
                            for (int j = 0; j < ka->len; j++)
                                array_push(res->arr, value_incref(ka->items[j]));
                            return res;
                        }
                        if (strcmp(method, "values") == 0) {
                            Value *res = xs_array_new();
                            for (int j = 0; j < ka->len; j++) {
                                if (ka->items[j]->tag == XS_STR) {
                                    Value *v = map_get(dm, ka->items[j]->s);
                                    array_push(res->arr, v ? value_incref(v) : value_incref(XS_NULL_VAL));
                                }
                            }
                            return res;
                        }
                        if (strcmp(method, "entries") == 0) {
                            Value *res = xs_array_new();
                            for (int j = 0; j < ka->len; j++) {
                                if (ka->items[j]->tag == XS_STR) {
                                    Value *tup = xs_tuple_new();
                                    array_push(tup->arr, value_incref(ka->items[j]));
                                    Value *v = map_get(dm, ka->items[j]->s);
                                    array_push(tup->arr, v ? value_incref(v) : value_incref(XS_NULL_VAL));
                                    array_push(res->arr, tup);
                                }
                            }
                            return res;
                        }
                        if (strcmp(method, "len") == 0) return xs_int(ka->len);
                    }
                }
                /* PriorityQueue */
                if (strcmp(ctype, "PriorityQueue") == 0 && data && data->tag == XS_ARRAY) {
                    XSArray *arr = data->arr;
                    if (strcmp(method, "push") == 0) {
                        /* push(item, priority): store as [item, priority] tuple */
                        if (argc < 1) return value_incref(XS_NULL_VAL);
                        double prio = (argc >= 2) ? (args[1]->tag==XS_INT?(double)args[1]->i:
                                      args[1]->tag==XS_FLOAT?args[1]->f:0.0) : 0.0;
                        Value *entry = xs_array_new();
                        array_push(entry->arr, value_incref(args[0]));
                        array_push(entry->arr, xs_float(prio));
                        /* insert sorted descending by priority */
                        int pos = arr->len;
                        for (int j = 0; j < arr->len; j++) {
                            Value *ej = arr->items[j];
                            if (ej->tag == XS_ARRAY && ej->arr->len >= 2) {
                                double ep = ej->arr->items[1]->tag==XS_FLOAT?ej->arr->items[1]->f:
                                            (double)ej->arr->items[1]->i;
                                if (prio > ep) { pos = j; break; }
                            }
                        }
                        /* shift right and insert */
                        array_push(arr, value_incref(XS_NULL_VAL)); /* extend */
                        for (int j = arr->len-1; j > pos; j--)
                            arr->items[j] = arr->items[j-1];
                        arr->items[pos] = entry;
                        return value_incref(XS_NULL_VAL);
                    }
                    if (strcmp(method, "pop") == 0) {
                        if (arr->len == 0) return value_incref(XS_NULL_VAL);
                        Value *entry = arr->items[0];
                        for (int j = 0; j < arr->len-1; j++) arr->items[j] = arr->items[j+1];
                        arr->len--;
                        Value *item = (entry->tag==XS_ARRAY&&entry->arr->len>=1) ?
                                      value_incref(entry->arr->items[0]) : value_incref(XS_NULL_VAL);
                        value_decref(entry);
                        return item;
                    }
                    if (strcmp(method, "peek") == 0) {
                        if (arr->len == 0) return value_incref(XS_NULL_VAL);
                        Value *entry = arr->items[0];
                        return (entry->tag==XS_ARRAY&&entry->arr->len>=1) ?
                               value_incref(entry->arr->items[0]) : value_incref(XS_NULL_VAL);
                    }
                    if (strcmp(method, "len") == 0) return xs_int(arr->len);
                    if (strcmp(method, "is_empty") == 0)
                        return arr->len==0?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
                }
                /* Counter */
                if (strcmp(ctype, "Counter") == 0) {
                    if (strcmp(method, "get") == 0) {
                        if (argc < 1) return xs_int(0);
                        char *key = value_str(args[0]);
                        Value *v = map_get(m, key); free(key);
                        return v ? value_incref(v) : xs_int(0);
                    }
                    if (strcmp(method, "add") == 0) {
                        if (argc < 1) return value_incref(XS_NULL_VAL);
                        char *key = value_str(args[0]);
                        int64_t inc = (argc >= 2 && args[1]->tag == XS_INT) ? args[1]->i : 1;
                        Value *cur = map_get(m, key);
                        int64_t val = cur && cur->tag == XS_INT ? cur->i + inc : inc;
                        Value *nv = xs_int(val);
                        map_set(m, key, nv); value_decref(nv);
                        free(key);
                        return value_incref(XS_NULL_VAL);
                    }
                    if (strcmp(method, "total") == 0) {
                        int nk = 0; char **ks = map_keys(m, &nk);
                        int64_t total = 0;
                        for (int j = 0; j < nk; j++) {
                            if (strcmp(ks[j], "_type") != 0) {
                                Value *v = map_get(m, ks[j]);
                                if (v && v->tag == XS_INT) total += v->i;
                            }
                            free(ks[j]);
                        }
                        free(ks);
                        return xs_int(total);
                    }
                    if (strcmp(method, "to_map") == 0) {
                        Value *res = xs_map_new();
                        int nk = 0; char **ks = map_keys(m, &nk);
                        for (int j = 0; j < nk; j++) {
                            if (strcmp(ks[j], "_type") != 0) {
                                Value *v = map_get(m, ks[j]);
                                if (v) map_set(res->map, ks[j], value_incref(v));
                            }
                            free(ks[j]);
                        }
                        free(ks);
                        return res;
                    }
                    if (strcmp(method, "keys") == 0) {
                        Value *res = xs_array_new();
                        int nk = 0; char **ks = map_keys(m, &nk);
                        for (int j = 0; j < nk; j++) {
                            if (strcmp(ks[j], "_type") != 0)
                                array_push(res->arr, xs_str(ks[j]));
                            free(ks[j]);
                        }
                        free(ks);
                        return res;
                    }
                    if (strcmp(method, "most_common") == 0) {
                        int topn = (argc>=1&&args[0]->tag==XS_INT)?(int)args[0]->i:m->len;
                        /* collect non-meta entries */
                        int nk=0; char **ks=map_keys(m,&nk);
                        /* simple selection sort for top-N */
                        /* build pairs array */
                        Value *result = xs_array_new();
                        /* We'll do a simple approach: collect all, sort, return top n */
                        /* Use a small struct on stack for sorting */
                        int valid = 0;
                        for (int j=0;j<nk;j++) if (strcmp(ks[j],"_type")!=0) valid++;
                        if (valid > 0) {
                            /* create sortable entries */
                            char **keys2 = xs_malloc(valid*sizeof(char*));
                            int64_t *counts = xs_malloc(valid*sizeof(int64_t));
                            int vi=0;
                            for (int j=0;j<nk;j++) {
                                if (strcmp(ks[j],"_type")==0) { free(ks[j]); continue; }
                                Value *v=map_get(m,ks[j]);
                                counts[vi] = v&&v->tag==XS_INT?v->i:0;
                                keys2[vi] = ks[j]; vi++;
                            }
                            free(ks);
                            /* bubble sort descending */
                            for (int a2=0;a2<valid-1;a2++) for (int b2=a2+1;b2<valid;b2++)
                                if (counts[b2]>counts[a2]) {
                                    int64_t tc=counts[a2]; counts[a2]=counts[b2]; counts[b2]=tc;
                                    char *tk=keys2[a2]; keys2[a2]=keys2[b2]; keys2[b2]=tk;
                                }
                            int lim = topn < valid ? topn : valid;
                            for (int j=0;j<lim;j++) {
                                Value *tup=xs_tuple_new();
                                array_push(tup->arr, xs_str(keys2[j]));
                                array_push(tup->arr, xs_int(counts[j]));
                                array_push(result->arr, tup);
                            }
                            for (int j=0;j<valid;j++) free(keys2[j]);
                            free(keys2); free(counts);
                        } else { free(ks); }
                        return result;
                    }
                }
            }
        }
        /* Fall back: look up method in the map itself (e.g. module functions) */
        {
            Value *fn = map_get(m, method);
            if (fn) {
                return call_value(i, fn, args, argc, method);
            }
        }
    }

    if (obj->tag == XS_INT || obj->tag == XS_FLOAT) {
        double num_f = (obj->tag==XS_FLOAT)?obj->f:(double)obj->i;
        int64_t num_i = (obj->tag==XS_INT)?obj->i:(int64_t)obj->f;
        if (strcmp(method, "abs") == 0) {
            if (obj->tag==XS_FLOAT) return xs_float(fabs(obj->f));
            return xs_int(num_i<0?-num_i:num_i);
        }
        if (strcmp(method, "pow") == 0) {
            double exp_v=(argc>0)?(args[0]->tag==XS_INT?(double)args[0]->i:args[0]->f):1.0;
            double r=pow(num_f, exp_v);
            if (obj->tag==XS_INT && args[0]->tag==XS_INT && exp_v>=0)
                return xs_int((int64_t)r);
            return xs_float(r);
        }
        if (strcmp(method, "sqrt") == 0) return xs_float(sqrt(num_f));
        if (strcmp(method, "min") == 0) {
            if (argc<1) return value_incref(obj);
            if (value_cmp(obj, args[0])<=0) return value_incref(obj);
            return value_incref(args[0]);
        }
        if (strcmp(method, "max") == 0) {
            if (argc<1) return value_incref(obj);
            if (value_cmp(obj, args[0])>=0) return value_incref(obj);
            return value_incref(args[0]);
        }
        if (strcmp(method, "clamp") == 0) {
            if (argc<2) return value_incref(obj);
            if (value_cmp(obj, args[0])<0) return value_incref(args[0]);
            if (value_cmp(obj, args[1])>0) return value_incref(args[1]);
            return value_incref(obj);
        }
        if (strcmp(method, "floor") == 0) {
            if (obj->tag==XS_INT) return value_incref(obj);
            return xs_int((int64_t)floor(obj->f));
        }
        if (strcmp(method, "ceil") == 0) {
            if (obj->tag==XS_INT) return value_incref(obj);
            return xs_int((int64_t)ceil(obj->f));
        }
        if (strcmp(method, "round") == 0) {
            if (obj->tag==XS_INT) return value_incref(obj);
            return xs_int((int64_t)round(obj->f));
        }
        if (strcmp(method, "to_int") == 0 || strcmp(method, "as_int") == 0) {
            return xs_int(num_i);
        }
        if (strcmp(method, "to_float") == 0 || strcmp(method, "as_float") == 0) {
            return xs_float(num_f);
        }
        if (strcmp(method, "to_str") == 0 || strcmp(method, "as_str") == 0 ||
            strcmp(method, "to_string") == 0) {
            char *r=value_str(obj); Value *v=xs_str(r); free(r); return v;
        }
        if (strcmp(method, "is_nan") == 0) {
            if (obj->tag!=XS_FLOAT) return value_incref(XS_FALSE_VAL);
            return isnan(obj->f)?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
        }
        if (strcmp(method, "is_inf") == 0) {
            if (obj->tag!=XS_FLOAT) return value_incref(XS_FALSE_VAL);
            return isinf(obj->f)?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
        }
        if (strcmp(method, "is_even") == 0) {
            return (num_i%2==0)?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
        }
        if (strcmp(method, "is_odd") == 0) {
            return (num_i%2!=0)?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
        }
        if (strcmp(method, "sign") == 0) {
            if (obj->tag==XS_FLOAT) {
                if (obj->f>0.0) return xs_int(1);
                if (obj->f<0.0) return xs_int(-1);
                return xs_int(0);
            }
            return xs_int(num_i>0?1:num_i<0?-1:0);
        }
        if (strcmp(method, "to_char") == 0 || strcmp(method, "chr") == 0) {
            char buf[5]={0}; /* up to 4 UTF-8 bytes */
            uint32_t cp=(uint32_t)num_i;
            if (cp<0x80) { buf[0]=(char)cp; }
            else if (cp<0x800) {
                buf[0]=(char)(0xC0|(cp>>6));
                buf[1]=(char)(0x80|(cp&0x3F));
            } else if (cp<0x10000) {
                buf[0]=(char)(0xE0|(cp>>12));
                buf[1]=(char)(0x80|((cp>>6)&0x3F));
                buf[2]=(char)(0x80|(cp&0x3F));
            } else {
                buf[0]=(char)(0xF0|(cp>>18));
                buf[1]=(char)(0x80|((cp>>12)&0x3F));
                buf[2]=(char)(0x80|((cp>>6)&0x3F));
                buf[3]=(char)(0x80|(cp&0x3F));
            }
            return xs_str(buf);
        }
        if (strcmp(method, "digits") == 0) {
            Value *arr = xs_array_new();
            int64_t n = num_i < 0 ? -num_i : num_i;
            if (n == 0) {
                array_push(arr->arr, xs_int(0));
                return arr;
            }
            /* collect digits in reverse, then reverse the array */
            int count = 0;
            while (n > 0) {
                array_push(arr->arr, xs_int(n % 10));
                n /= 10;
                count++;
            }
            /* reverse in place */
            for (int j = 0; j < count / 2; j++) {
                Value *tmp = arr->arr->items[j];
                arr->arr->items[j] = arr->arr->items[count - 1 - j];
                arr->arr->items[count - 1 - j] = tmp;
            }
            return arr;
        }
        if (strcmp(method, "to_hex") == 0) {
            char buf[32];
            if (num_i < 0)
                snprintf(buf, sizeof(buf), "-0x%llx", (unsigned long long)(-num_i));
            else
                snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)num_i);
            return xs_str(buf);
        }
        if (strcmp(method, "to_bin") == 0) {
            char buf[80]; /* enough for 64-bit + "0b" + sign */
            uint64_t val = (uint64_t)(num_i < 0 ? -num_i : num_i);
            int pos = 0;
            if (num_i < 0) buf[pos++] = '-';
            buf[pos++] = '0'; buf[pos++] = 'b';
            if (val == 0) { buf[pos++] = '0'; buf[pos] = '\0'; }
            else {
                char tmp[65]; int tlen = 0;
                while (val > 0) { tmp[tlen++] = '0' + (val & 1); val >>= 1; }
                for (int j = tlen - 1; j >= 0; j--) buf[pos++] = tmp[j];
                buf[pos] = '\0';
            }
            return xs_str(buf);
        }
        if (strcmp(method, "to_oct") == 0) {
            char buf[32];
            if (num_i < 0)
                snprintf(buf, sizeof(buf), "-0o%llo", (unsigned long long)(-num_i));
            else
                snprintf(buf, sizeof(buf), "0o%llo", (unsigned long long)num_i);
            return xs_str(buf);
        }
        (void)num_f; (void)num_i;
    }

    if (obj->tag == XS_ENUM_VAL) {
        const char *variant = obj->en->variant;
        Value *inner = (obj->en->arr_data && obj->en->arr_data->len>0)
                       ? obj->en->arr_data->items[0] : XS_NULL_VAL;
        if (strcmp(method, "is_some") == 0) {
            return strcmp(variant,"Some")==0?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
        }
        if (strcmp(method, "is_none") == 0) {
            return strcmp(variant,"None")==0?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
        }
        if (strcmp(method, "is_ok") == 0) {
            return strcmp(variant,"Ok")==0?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
        }
        if (strcmp(method, "is_err") == 0) {
            return strcmp(variant,"Err")==0?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
        }
        if (strcmp(method, "unwrap") == 0) {
            if (strcmp(variant,"Some")==0 || strcmp(variant,"Ok")==0)
                return value_incref(inner);
            fprintf(stderr, "xs: error at %s:%d:%d: unwrap called on %s\n",
                    i->current_span.file ? i->current_span.file : "<unknown>",
                    i->current_span.line, i->current_span.col, variant);
            return value_incref(XS_NULL_VAL);
        }
        if (strcmp(method, "unwrap_or") == 0) {
            if (strcmp(variant,"Some")==0 || strcmp(variant,"Ok")==0)
                return value_incref(inner);
            return argc>0?value_incref(args[0]):value_incref(XS_NULL_VAL);
        }
        if (strcmp(method, "map") == 0) {
            if (argc<1) return value_incref(obj);
            if (strcmp(variant,"Some")==0) {
                Value *a[1]={inner};
                Value *r=call_value(i, args[0], a, 1, "option_map");
                if (i->cf.signal) return r;
                /* wrap in Some */
                XSEnum *en=xs_calloc(1,sizeof(XSEnum));
                en->type_name=xs_strdup(obj->en->type_name);
                en->variant=xs_strdup("Some");
                en->arr_data=xs_calloc(1,sizeof(XSArray));
                en->arr_data->len=1; en->arr_data->cap=1;
                en->arr_data->items=xs_malloc(sizeof(Value*));
                en->arr_data->items[0]=r;
                en->refcount=1;
                Value *ev=xs_calloc(1,sizeof(Value));
                ev->tag=XS_ENUM_VAL; ev->refcount=1; ev->en=en;
                return ev;
            }
            return value_incref(obj); /* None/Err pass through */
        }
        if (strcmp(method, "or_else") == 0) {
            if (strcmp(variant,"None")==0) {
                if (argc<1) return value_incref(XS_NULL_VAL);
                return call_value(i, args[0], NULL, 0, "or_else");
            }
            return value_incref(obj);
        }
        if (strcmp(method, "map_err") == 0) {
            if (argc<1) return value_incref(obj);
            if (strcmp(variant,"Err")==0) {
                Value *a[1]={inner};
                Value *r=call_value(i, args[0], a, 1, "map_err");
                if (i->cf.signal) return r;
                XSEnum *en=xs_calloc(1,sizeof(XSEnum));
                en->type_name=xs_strdup(obj->en->type_name);
                en->variant=xs_strdup("Err");
                en->arr_data=xs_calloc(1,sizeof(XSArray));
                en->arr_data->len=1; en->arr_data->cap=1;
                en->arr_data->items=xs_malloc(sizeof(Value*));
                en->arr_data->items[0]=r;
                en->refcount=1;
                Value *ev=xs_calloc(1,sizeof(Value));
                ev->tag=XS_ENUM_VAL; ev->refcount=1; ev->en=en;
                return ev;
            }
            return value_incref(obj); /* Ok passes through */
        }
        if (strcmp(method, "ok") == 0) {
            if (strcmp(variant,"Ok")==0) {
                /* wrap inner in Some */
                XSEnum *en=xs_calloc(1,sizeof(XSEnum));
                en->type_name=xs_strdup("Option");
                en->variant=xs_strdup("Some");
                en->arr_data=xs_calloc(1,sizeof(XSArray));
                en->arr_data->len=1; en->arr_data->cap=1;
                en->arr_data->items=xs_malloc(sizeof(Value*));
                en->arr_data->items[0]=value_incref(inner);
                en->refcount=1;
                Value *ev=xs_calloc(1,sizeof(Value));
                ev->tag=XS_ENUM_VAL; ev->refcount=1; ev->en=en;
                return ev;
            }
            /* Err → None */
            XSEnum *en=xs_calloc(1,sizeof(XSEnum));
            en->type_name=xs_strdup("Option");
            en->variant=xs_strdup("None");
            en->refcount=1;
            Value *ev=xs_calloc(1,sizeof(Value));
            ev->tag=XS_ENUM_VAL; ev->refcount=1; ev->en=en;
            return ev;
        }
    }

    if (obj->tag == XS_SIGNAL) {
        XSSignal *sig = obj->signal;
        if (strcmp(method, "get") == 0) {
            if (sig->compute) {
                return call_value(i, sig->compute, NULL, 0, "derived_compute");
            }
            return value_incref(sig->value);
        }
        if (strcmp(method, "set") == 0 && argc >= 1) {
            value_decref(sig->value);
            sig->value = value_incref(args[0]);
            if (!sig->notifying) {
                sig->notifying = 1;
                for (int j = 0; j < sig->nsubs; j++) {
                    Value *r = call_value(i, sig->subscribers[j], args, 1, "subscriber");
                    value_decref(r);
                }
                sig->notifying = 0;
            }
            return value_incref(XS_NULL_VAL);
        }
        if (strcmp(method, "subscribe") == 0 && argc >= 1) {
            if (sig->nsubs >= sig->subcap) {
                sig->subcap = sig->subcap ? sig->subcap * 2 : 4;
                sig->subscribers = xs_realloc(sig->subscribers, sig->subcap * sizeof(Value*));
            }
            sig->subscribers[sig->nsubs++] = value_incref(args[0]);
            return value_incref(XS_NULL_VAL);
        }
        if (strcmp(method, "value") == 0) {
            if (sig->compute) {
                return call_value(i, sig->compute, NULL, 0, "derived_compute");
            }
            return value_incref(sig->value);
        }
        return value_incref(XS_NULL_VAL);
    }

    if (obj->tag == XS_RANGE) {
        if (strcmp(method, "len") == 0) {
            int64_t span = obj->range->end - obj->range->start;
            if (obj->range->inclusive) span += (span >= 0) ? 1 : -1;
            int64_t step = obj->range->step ? obj->range->step : 1;
            int64_t n2;
            if (step > 0) n2 = (span > 0) ? (span + step - 1) / step : 0;
            else           n2 = (span < 0) ? (-span + (-step) - 1) / (-step) : 0;
            return xs_int(n2);
        }
        if (strcmp(method, "step") == 0) {
            return xs_int(obj->range->step ? obj->range->step : 1);
        }
        if (strcmp(method, "to_array") == 0) {
            Value *arr = xs_array_new();
            int64_t step = obj->range->step ? obj->range->step : 1;
            int64_t end2 = obj->range->inclusive ? obj->range->end + (step > 0 ? 1 : -1) : obj->range->end;
            for (int64_t j = obj->range->start; step > 0 ? j < end2 : j > end2; j += step)
                array_push(arr->arr, xs_int(j));
            return arr;
        }
        if (strcmp(method, "filter") == 0 || strcmp(method, "map") == 0 ||
            strcmp(method, "for_each") == 0 || strcmp(method, "fold") == 0 ||
            strcmp(method, "reduce") == 0 || strcmp(method, "any") == 0 ||
            strcmp(method, "all") == 0 || strcmp(method, "find") == 0 ||
            strcmp(method, "sum") == 0 || strcmp(method, "min") == 0 ||
            strcmp(method, "max") == 0 || strcmp(method, "count") == 0) {
            Value *arr = xs_array_new();
            int64_t step = obj->range->step ? obj->range->step : 1;
            int64_t end2 = obj->range->inclusive ? obj->range->end + (step > 0 ? 1 : -1) : obj->range->end;
            for (int64_t j2 = obj->range->start; step > 0 ? j2 < end2 : j2 > end2; j2 += step)
                array_push(arr->arr, xs_int(j2));
            Value *res2 = eval_method(i, arr, method, args, argc);
            value_decref(arr);
            return res2;
        }
        if (strcmp(method, "contains") == 0) {
            if (argc<1||args[0]->tag!=XS_INT) return value_incref(XS_FALSE_VAL);
            int64_t v2=args[0]->i;
            int64_t step = obj->range->step ? obj->range->step : 1;
            int ok;
            if (step > 0) {
                ok = v2 >= obj->range->start &&
                     (obj->range->inclusive ? v2 <= obj->range->end : v2 < obj->range->end) &&
                     ((v2 - obj->range->start) % step == 0);
            } else {
                ok = v2 <= obj->range->start &&
                     (obj->range->inclusive ? v2 >= obj->range->end : v2 > obj->range->end) &&
                     ((obj->range->start - v2) % (-step) == 0);
            }
            return ok?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
        }
    }

    if (obj->tag == XS_ENUM_VAL && obj->en->type_name) {
        Value *type_mod = env_get(i->env, obj->en->type_name);
        if (type_mod && (type_mod->tag == XS_MODULE || type_mod->tag == XS_MAP)) {
            Value *impl_val = map_get(type_mod->map, "__impl__");
            if (impl_val && (impl_val->tag == XS_MAP || impl_val->tag == XS_MODULE)) {
                Value *fn = map_get(impl_val->map, method);
                if (fn && (fn->tag == XS_FUNC || fn->tag == XS_NATIVE)) {
                    Value **new_args = xs_malloc((argc+1)*sizeof(Value*));
                    new_args[0] = obj;
                    for (int j = 0; j < argc; j++) new_args[j+1] = args[j];
                    Value *r = call_value(i, fn, new_args, argc+1, method);
                    free(new_args);
                    return r;
                }
            }
        }
    }

    if (obj->tag == XS_CLASS_VAL && obj->cls) {
        Value *sfn = NULL;
        if (obj->cls->static_methods) sfn = map_get(obj->cls->static_methods, method);
        if (!sfn && obj->cls->methods) sfn = map_get(obj->cls->methods, method);
        if (sfn && (sfn->tag == XS_FUNC || sfn->tag == XS_NATIVE))
            return call_value(i, sfn, args, argc, method);
    }

    Value *fn = env_get(i->env, method);
    if (fn) return call_value(i, fn, args, argc, method);

    /* check plugin method registry */
    {
        const char *tname = value_type_str(obj);
        Value *pfn = plugin_lookup_method(tname, method);
        if (pfn) {
            Value **new_args = xs_malloc((argc + 1) * sizeof(Value*));
            new_args[0] = value_incref(obj);
            for (int j = 0; j < argc; j++) new_args[j + 1] = args[j];
            Value *r = call_value(i, pfn, new_args, argc + 1, method);
            value_decref(new_args[0]);
            free(new_args);
            return r;
        }
    }

    char *repr = value_repr(obj);
    static const char *str_methods[] = {"len","trim","upper","lower","split","contains","replace","starts_with","ends_with","chars","to_str","bytes","repeat","join","slice","index_of","parse_int","parse_float","is_empty","reverse","capitalize","pad_left","pad_right","count",NULL};
    static const char *arr_methods[] = {"len","push","pop","map","filter","reduce","sort","reverse","contains","join","slice","flatten","enumerate","zip","any","all","find","index_of",NULL};
    static const char *map_methods[] = {"len","keys","values","contains","remove","clear","entries","merge","filter","map","clone",NULL};
    static const char *num_methods[] = {"abs","to_str","floor","ceil","round","clamp",NULL};
    const char **methods_list = NULL;
    switch (obj->tag) {
        case XS_STR: methods_list = str_methods; break;
        case XS_ARRAY: case XS_TUPLE: methods_list = arr_methods; break;
        case XS_MAP: methods_list = map_methods; break;
        case XS_INT: case XS_FLOAT: methods_list = num_methods; break;
        default: break;
    }
    char hint_buf[256] = {0};
    if (methods_list) {
        const char *best = NULL;
        int best_dist = 4;
        for (const char **m = methods_list; *m; m++) {
            int d = xs_edit_distance(method, *m);
            if (d > 0 && d < best_dist) { best_dist = d; best = *m; }
        }
        if (best)
            snprintf(hint_buf, sizeof hint_buf, "did you mean '%s'?", best);
        else {
            /* List a few available methods */
            int pos = 0;
            pos += snprintf(hint_buf + pos, sizeof(hint_buf) - pos, "available: ");
            for (const char **m = methods_list; *m && pos < (int)sizeof(hint_buf) - 20; m++) {
                if (m != methods_list) pos += snprintf(hint_buf + pos, sizeof(hint_buf) - pos, ", ");
                pos += snprintf(hint_buf + pos, sizeof(hint_buf) - pos, "%s", *m);
            }
        }
    }
    char label_buf[128];
    snprintf(label_buf, sizeof label_buf, "no method '%s' on type '%s'", method, repr);
    xs_runtime_error(i->current_span, label_buf,
            hint_buf[0] ? hint_buf : NULL,
            "value of type '%s' has no method '%s'", repr, method);
    free(repr);
    i->cf.signal = CF_ERROR;
    return value_incref(XS_NULL_VAL);
}

static Value *eval_binop(Interp *i, Node *n) {
    const char *op = n->binop.op;

    if (strcmp(op,"&&")==0 || strcmp(op,"and")==0) {
        Value *l = EVAL(i, n->binop.left);
        if (!value_truthy(l)) return l;
        value_decref(l);
        return EVAL(i, n->binop.right);
    }
    if (strcmp(op,"||")==0 || strcmp(op,"or")==0) {
        Value *l = EVAL(i, n->binop.left);
        if (value_truthy(l)) return l;
        value_decref(l);
        return EVAL(i, n->binop.right);
    }
    if (strcmp(op,"??")==0) {
        Value *l = EVAL(i, n->binop.left);
        if (l->tag != XS_NULL) return l;
        value_decref(l);
        return EVAL(i, n->binop.right);
    }

    Value *left  = EVAL(i, n->binop.left);
    Value *right = EVAL(i, n->binop.right);
    Value *result = NULL;

    if (strcmp(op, "<=>") == 0) {
        if (left->tag == XS_INT && right->tag == XS_INT) {
            int64_t cmp = (left->i > right->i) - (left->i < right->i);
            result = xs_int(cmp);
        } else if ((left->tag == XS_INT || left->tag == XS_FLOAT) &&
                   (right->tag == XS_INT || right->tag == XS_FLOAT)) {
            double a = (left->tag == XS_INT) ? (double)left->i : left->f;
            double b = (right->tag == XS_INT) ? (double)right->i : right->f;
            result = xs_int((a > b) - (a < b));
        } else if (left->tag == XS_STR && right->tag == XS_STR) {
            int cmp = strcmp(left->s, right->s);
            result = xs_int((cmp > 0) - (cmp < 0));
        } else {
            result = xs_int(0);
        }
        goto done;
    }

    if (left->tag == XS_INT && right->tag == XS_INT) {
        int64_t a = left->i, b = right->i;
        if (op[0]=='=' && op[1]=='=') { result=a==b?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL); goto done; }
        if (op[0]=='<' && op[1]=='\0') { result=a<b?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL); goto done; }
        if (op[0]=='>' && op[1]=='\0') { result=a>b?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL); goto done; }
        if (op[0]=='<' && op[1]=='=') { result=a<=b?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL); goto done; }
        if (op[0]=='>' && op[1]=='=') { result=a>=b?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL); goto done; }
        if (op[0]=='!' && op[1]=='=') { result=a!=b?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL); goto done; }
        if (op[0]=='+' && op[1]=='\0') {
            result = xs_safe_add(a, b); goto done;
        }
        if (op[0]=='-' && op[1]=='\0') {
            result = xs_safe_sub(a, b); goto done;
        }
        if (op[0]=='*' && op[1]=='\0') {
            result = xs_safe_mul(a, b); goto done;
        }
        if (op[0]=='/' && op[1]=='\0') {
            if (!b) { xs_runtime_error(n->span, "division by zero", NULL, "cannot divide by zero"); result=value_incref(XS_NULL_VAL); goto done; }
            result=xs_int(a/b); goto done;
        }
        if (op[0]=='%' && op[1]=='\0') {
            if (!b) { xs_runtime_error(n->span, "modulo by zero", NULL, "cannot take remainder with divisor zero"); result=value_incref(XS_NULL_VAL); goto done; }
            int64_t r = a % b;
            if (r != 0 && ((r ^ b) < 0)) r += b; /* math modulo */
            result=xs_int(r); goto done;
        }
        if (op[0]=='*' && op[1]=='*') {
            if (b < 0) { result=xs_float(pow((double)a,(double)b)); goto done; }
            result = xs_safe_pow(a, b); goto done;
        }
        if (op[0]=='/' && op[1]=='/') {
            if (!b) { xs_runtime_error(n->span, "division by zero", NULL, "cannot floor-divide by zero"); result=value_incref(XS_NULL_VAL); goto done; }
            int64_t q = a / b;
            if ((a ^ b) < 0 && a % b != 0) q--; /* floor toward -inf */
            result=xs_int(q); goto done;
        }
        if (op[0]=='&' && op[1]=='\0') { result=xs_int(a&b); goto done; }
        if (op[0]=='|' && op[1]=='\0') { result=xs_int(a|b); goto done; }
        if (op[0]=='^' && op[1]=='\0') { result=xs_int(a^b); goto done; }
        if (op[0]=='<' && op[1]=='<') { result=xs_int(a<<b); goto done; }
        if (op[0]=='>' && op[1]=='>') { result=xs_int(a>>b); goto done; }
    }

    if ((left->tag == XS_INT || left->tag == XS_BIGINT) &&
        (right->tag == XS_INT || right->tag == XS_BIGINT) &&
        (left->tag == XS_BIGINT || right->tag == XS_BIGINT)) {
        if (op[0]=='+' && op[1]=='\0') { result=xs_numeric_add(left,right); goto done; }
        if (op[0]=='-' && op[1]=='\0') { result=xs_numeric_sub(left,right); goto done; }
        if (op[0]=='*' && op[1]=='\0') { result=xs_numeric_mul(left,right); goto done; }
        if (op[0]=='/' && op[1]=='\0') { result=xs_numeric_div(left,right); goto done; }
        if (op[0]=='%' && op[1]=='\0') { result=xs_numeric_mod(left,right); goto done; }
        if (op[0]=='*' && op[1]=='*') { result=xs_numeric_pow(left,right); goto done; }
        if (op[0]=='/' && op[1]=='/') { result=xs_numeric_floordiv(left,right); goto done; }
        if (op[0]=='=' && op[1]=='=') { result=value_equal(left,right)?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL); goto done; }
        if (op[0]=='!' && op[1]=='=') { result=!value_equal(left,right)?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL); goto done; }
        int cmp = value_cmp(left, right);
        if (op[0]=='<' && op[1]=='\0') { result=cmp<0?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL); goto done; }
        if (op[0]=='>' && op[1]=='\0') { result=cmp>0?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL); goto done; }
        if (op[0]=='<' && op[1]=='=') { result=cmp<=0?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL); goto done; }
        if (op[0]=='>' && op[1]=='=') { result=cmp>=0?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL); goto done; }
    }

    if ((left->tag==XS_INT||left->tag==XS_FLOAT||left->tag==XS_BIGINT) &&
        (right->tag==XS_INT||right->tag==XS_FLOAT||right->tag==XS_BIGINT)) {
        double a = left->tag==XS_FLOAT ? left->f : (left->tag==XS_BIGINT ? bigint_to_double(left->bigint) : (double)left->i);
        double b2 = right->tag==XS_FLOAT ? right->f : (right->tag==XS_BIGINT ? bigint_to_double(right->bigint) : (double)right->i);
        if (op[0]=='+') { result=xs_float(a+b2); goto done; }
        if (op[0]=='-') { result=xs_float(a-b2); goto done; }
        if (op[0]=='*' && op[1]=='\0') { result=xs_float(a*b2); goto done; }
        if (op[0]=='/' && op[1]=='\0') {
            if (b2==0.0) { xs_runtime_error(n->span, "division by zero", NULL, "cannot divide by zero"); result=value_incref(XS_NULL_VAL); goto done; }
            result=xs_float(a/b2); goto done;
        }
        if (op[0]=='%') { result=xs_float(fmod(a,b2)); goto done; }
        if (op[0]=='*' && op[1]=='*') { result=xs_float(pow(a,b2)); goto done; }
        if (op[0]=='/' && op[1]=='/') { result=xs_float(floor(a/b2)); goto done; }
        if (op[0]=='=' && op[1]=='=') { result=a==b2?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL); goto done; }
        if (op[0]=='!' && op[1]=='=') { result=a!=b2?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL); goto done; }
        if (op[0]=='<' && op[1]=='\0') { result=a<b2?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL); goto done; }
        if (op[0]=='>' && op[1]=='\0') { result=a>b2?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL); goto done; }
        if (op[0]=='<' && op[1]=='=') { result=a<=b2?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL); goto done; }
        if (op[0]=='>' && op[1]=='=') { result=a>=b2?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL); goto done; }
    }

    if ((left->tag == XS_STR || right->tag == XS_STR) && op[0]=='+' && op[1]=='\0') {
        char *ls = value_str(left);
        char *rs = value_str(right);
        int la=(int)strlen(ls), lb=(int)strlen(rs);
        char *buf=xs_malloc(la+lb+1);
        memcpy(buf,ls,la); memcpy(buf+la,rs,lb+1);
        free(ls); free(rs);
        result=xs_str(buf); free(buf); goto done;
    }
    if (left->tag == XS_STR) {
        if (op[0]=='+') {
            char *rs = value_str(right);
            int la = (int)strlen(left->s), lb = (int)strlen(rs);
            char *buf = xs_malloc(la+lb+1);
            memcpy(buf, left->s, la);
            memcpy(buf+la, rs, lb+1);
            free(rs);
            result = xs_str(buf); free(buf); goto done;
        }
        if (op[0]=='+' && op[1]=='+') {
            char *rs = value_str(right);
            int la=(int)strlen(left->s), lb=(int)strlen(rs);
            char *buf=xs_malloc(la+lb+1);
            memcpy(buf,left->s,la); memcpy(buf+la,rs,lb+1);
            free(rs); result=xs_str(buf); free(buf); goto done;
        }
        if (op[0]=='=' && op[1]=='=') {
            result = (right->tag==XS_STR && strcmp(left->s,right->s)==0) ?
                     value_incref(XS_TRUE_VAL) : value_incref(XS_FALSE_VAL);
            goto done;
        }
        if (op[0]=='!' && op[1]=='=') {
            result = (right->tag!=XS_STR || strcmp(left->s,right->s)!=0) ?
                     value_incref(XS_TRUE_VAL) : value_incref(XS_FALSE_VAL);
            goto done;
        }
        if (op[0]=='<') { result=value_cmp(left,right)<0?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL); goto done; }
        if (op[0]=='>') { result=value_cmp(left,right)>0?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL); goto done; }
        if (op[0]=='*' && right->tag==XS_INT) {
            int n2 = (int)right->i; if (n2<0) n2=0;
            int slen=(int)strlen(left->s);
            char *buf=xs_malloc(slen*n2+1); buf[0]='\0';
            for (int j=0;j<n2;j++) strcat(buf,left->s);
            result=xs_str(buf); free(buf); goto done;
        }
    }

    if (op[0]=='+' && op[1]=='+') {
        if (left->tag==XS_ARRAY) {
            Value *res=xs_array_new();
            XSArray *la=left->arr;
            for (int j=0;j<la->len;j++) array_push(res->arr, value_incref(la->items[j]));
            if (right->tag==XS_ARRAY) {
                XSArray *ra=right->arr;
                for (int j=0;j<ra->len;j++) array_push(res->arr, value_incref(ra->items[j]));
            }
            result=res; goto done;
        }
    }

    if (strcmp(op, "is") == 0) {
        const char *tname = (right->tag == XS_STR) ? right->s : "";
        int match = 0;
        if (strcmp(tname, "int") == 0 || strcmp(tname, "i64") == 0)
            match = (left->tag == XS_INT);
        else if (strcmp(tname, "float") == 0 || strcmp(tname, "f64") == 0)
            match = (left->tag == XS_FLOAT);
        else if (strcmp(tname, "str") == 0 || strcmp(tname, "string") == 0)
            match = (left->tag == XS_STR);
        else if (strcmp(tname, "bool") == 0)
            match = (left->tag == XS_BOOL);
        else if (strcmp(tname, "array") == 0)
            match = (left->tag == XS_ARRAY);
        else if (strcmp(tname, "map") == 0)
            match = (left->tag == XS_MAP);
        else if (strcmp(tname, "null") == 0)
            match = (left->tag == XS_NULL);
        else if (strcmp(tname, "fn") == 0 || strcmp(tname, "function") == 0)
            match = (left->tag == XS_FUNC || left->tag == XS_NATIVE);
        else if (strcmp(tname, "tuple") == 0)
            match = (left->tag == XS_TUPLE);
        else if (left->tag == XS_STRUCT_VAL && left->st)
            match = (strcmp(left->st->type_name, tname) == 0);
        else if (left->tag == XS_ENUM_VAL && left->en)
            match = (strcmp(left->en->type_name, tname) == 0);
        else if (left->tag == XS_INST && left->inst && left->inst->class_)
            match = (strcmp(left->inst->class_->name, tname) == 0);
        result = match ? value_incref(XS_TRUE_VAL) : value_incref(XS_FALSE_VAL);
        goto done;
    }

    if (strcmp(op,"in")==0) {
        if (right->tag==XS_ARRAY) {
            for (int j=0;j<right->arr->len;j++)
                if (value_equal(left,right->arr->items[j])) { result=value_incref(XS_TRUE_VAL); goto done; }
            result=value_incref(XS_FALSE_VAL); goto done;
        }
        if (right->tag==XS_MAP || right->tag==XS_MODULE) {
            if (left->tag==XS_STR) result=map_has(right->map,left->s)?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
            else result=value_incref(XS_FALSE_VAL);
            goto done;
        }
        if (right->tag==XS_STR && left->tag==XS_STR) {
            result=strstr(right->s,left->s)?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
            goto done;
        }
        if (right->tag==XS_RANGE) {
            if (left->tag==XS_INT) {
                int64_t v2=left->i;
                int ok=v2>=right->range->start &&
                       (right->range->inclusive?v2<=right->range->end:v2<right->range->end);
                result=ok?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
            } else result=value_incref(XS_FALSE_VAL);
            goto done;
        }
        result=value_incref(XS_FALSE_VAL); goto done;
    }

    if (strcmp(op,"not in")==0) {
        Node fakeop = *n;
        char tmp[8]; strcpy(tmp,"in");
        memcpy(fakeop.binop.op, tmp, 8);
        Value *inr = eval_binop(i, &fakeop);
        result = value_truthy(inr) ? value_incref(XS_FALSE_VAL) : value_incref(XS_TRUE_VAL);
        value_decref(inr); goto done;
    }

    /* operator overloading via dunder methods */
    if (left->tag == XS_INST) {
        const char *dunder = NULL;
        if (op[0]=='+' && op[1]=='\0') dunder = "__add__";
        else if (op[0]=='-' && op[1]=='\0') dunder = "__sub__";
        else if (op[0]=='*' && op[1]=='\0') dunder = "__mul__";
        else if (op[0]=='/' && op[1]=='\0') dunder = "__div__";
        else if (op[0]=='%' && op[1]=='\0') dunder = "__mod__";
        else if (op[0]=='=' && op[1]=='=') dunder = "__eq__";
        else if (op[0]=='!' && op[1]=='=') dunder = "__ne__";
        else if (op[0]=='<' && op[1]=='\0') dunder = "__lt__";
        else if (op[0]=='>' && op[1]=='\0') dunder = "__gt__";
        else if (op[0]=='<' && op[1]=='=') dunder = "__le__";
        else if (op[0]=='>' && op[1]=='=') dunder = "__ge__";

        Value *op_fn = NULL;
        if (dunder) op_fn = map_get(left->inst->methods, dunder);
        if (!op_fn) op_fn = map_get(left->inst->methods, op);
        if (!op_fn && left->inst->class_) {
            if (dunder) op_fn = map_get(left->inst->class_->methods, dunder);
            if (!op_fn) op_fn = map_get(left->inst->class_->methods, op);
        }
        if (!op_fn && op[0]=='!' && op[1]=='=') {
            op_fn = map_get(left->inst->methods, "__eq__");
            if (!op_fn && left->inst->class_)
                op_fn = map_get(left->inst->class_->methods, "__eq__");
            if (op_fn && (op_fn->tag == XS_FUNC || op_fn->tag == XS_NATIVE)) {
                int has_self = 0;
                if (op_fn->tag == XS_FUNC && op_fn->fn->nparams > 0) {
                    Node *p0 = op_fn->fn->params[0];
                    if (p0->tag == NODE_PAT_IDENT && strcmp(p0->pat_ident.name, "self") == 0)
                        has_self = 1;
                }
                Value *call_args[2] = { left, right };
                Value *eq_result = call_value(i, op_fn, call_args, has_self ? 2 : 2,
                                              "__eq__");
                result = value_truthy(eq_result) ?
                    value_incref(XS_FALSE_VAL) : value_incref(XS_TRUE_VAL);
                value_decref(eq_result);
                goto done;
            }
            op_fn = NULL;
        }
        if (!op_fn && op[0]=='>' && op[1]=='\0') {
            op_fn = map_get(left->inst->methods, "__lt__");
            if (!op_fn && left->inst->class_)
                op_fn = map_get(left->inst->class_->methods, "__lt__");
            if (op_fn && right->tag == XS_INST) {
                Value *call_args[2] = { right, left };
                result = call_value(i, op_fn, call_args, 2, "__lt__");
                goto done;
            }
            op_fn = NULL;
        }
        if (op_fn && (op_fn->tag == XS_FUNC || op_fn->tag == XS_NATIVE)) {
            Value *call_args[2] = { left, right };
            result = call_value(i, op_fn, call_args, 2, dunder ? dunder : op);
            goto done;
        }
    }

    if (op[0]=='=' && op[1]=='=') {
        result=value_equal(left,right)?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
        goto done;
    }
    if (op[0]=='!' && op[1]=='=') {
        result=value_equal(left,right)?value_incref(XS_FALSE_VAL):value_incref(XS_TRUE_VAL);
        goto done;
    }

    result = value_incref(XS_NULL_VAL);

done:
    value_decref(left);
    value_decref(right);
    return result ? result : value_incref(XS_NULL_VAL);
}

static void interp_for_each(Interp *i, Value *iter,
                              Node *pat, Node *body) {
    if (iter->tag == XS_ARRAY || iter->tag == XS_TUPLE) {
        XSArray *arr = iter->arr;
        for (int j = 0; j < arr->len; j++) {
            push_env(i);
            bind_pattern(i, pat, arr->items[j], i->env, 1);
            interp_exec(i, body);
            pop_env(i);
            if (i->cf.signal == CF_BREAK) { CF_CLEAR(i); break; }
            if (i->cf.signal == CF_CONTINUE) { CF_CLEAR(i); continue; }
            if (i->cf.signal) break;
        }
    } else if (iter->tag == XS_RANGE) {
        XSRange *r = iter->range;
        int64_t step = r->step ? r->step : 1;
        int64_t end2 = r->inclusive ? r->end + (step > 0 ? 1 : -1) : r->end;
        for (int64_t j = r->start; step > 0 ? j < end2 : j > end2; j += step) {
            Value *v = xs_int(j);
            push_env(i);
            bind_pattern(i, pat, v, i->env, 1);
            value_decref(v);
            interp_exec(i, body);
            pop_env(i);
            if (i->cf.signal == CF_BREAK) { CF_CLEAR(i); break; }
            if (i->cf.signal == CF_CONTINUE) { CF_CLEAR(i); continue; }
            if (i->cf.signal) break;
        }
    } else if (iter->tag == XS_STR) {
        const char *s = iter->s;
        for (int j = 0; s[j]; j++) {
            Value *v = xs_str_n(s+j, 1);
            push_env(i);
            bind_pattern(i, pat, v, i->env, 1);
            value_decref(v);
            interp_exec(i, body);
            pop_env(i);
            if (i->cf.signal == CF_BREAK) { CF_CLEAR(i); break; }
            if (i->cf.signal == CF_CONTINUE) { CF_CLEAR(i); continue; }
            if (i->cf.signal) break;
        }
    } else if (iter->tag == XS_MAP || iter->tag == XS_MODULE) {
        int nkeys = 0;
        char **keys = map_keys(iter->map, &nkeys);
        for (int j = 0; j < nkeys; j++) {
            Value *v = xs_str(keys[j]);
            push_env(i);
            bind_pattern(i, pat, v, i->env, 1);
            value_decref(v);
            interp_exec(i, body);
            pop_env(i);
            free(keys[j]);
            if (i->cf.signal == CF_BREAK) { CF_CLEAR(i); break; }
            if (i->cf.signal == CF_CONTINUE) { CF_CLEAR(i); continue; }
            if (i->cf.signal) { /* free remaining */ for(int k=j+1;k<nkeys;k++) free(keys[k]); break; }
        }
        free(keys);
    }
}

Value *interp_eval(Interp *i, Node *n) {
    if (!n) return value_incref(XS_NULL_VAL);
    if (i->cf.signal) return value_incref(XS_NULL_VAL);
    i->current_span = n->span;
    if (i->coverage && n->span.line > 0)
        coverage_record_line(i->coverage, n->span.line);

    /* phase 2: before_eval hooks (skip if already inside a hook to prevent recursion) */
    if (g_has_eval_hooks && g_n_before_eval > 0 && !g_in_eval_hook) {
        g_in_eval_hook = 1;
        for (int _h = 0; _h < g_n_before_eval; _h++) {
            EvalHook *hook = &g_before_eval[_h];
            if (!hook->callback) continue;
            if (hook->tag_filter >= 0 && hook->tag_filter != (int)n->tag) continue;
            Value *node_map = node_to_xs_map(n);
            Value *args[1] = { node_map };
            Value *result = call_value(i, hook->callback, args, 1, "before_eval");
            value_decref(node_map);
            if (!result || result->tag == XS_NULL) {
                if (result) value_decref(result);
                g_in_eval_hook = 0;
                return value_incref(XS_NULL_VAL);
            }
            value_decref(result);
            if (i->cf.signal) { g_in_eval_hook = 0; return value_incref(XS_NULL_VAL); }
        }
        g_in_eval_hook = 0;
    }

    switch (n->tag) {
    case NODE_LIT_INT:   return xs_int(n->lit_int.ival);
    case NODE_LIT_BIGINT: {
        XSBigInt *b = bigint_from_str(n->lit_bigint.bigint_str, 10);
        return xs_bigint_val(b);
    }
    case NODE_LIT_FLOAT: return xs_float(n->lit_float.fval);
    case NODE_LIT_BOOL:  return n->lit_bool.bval?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
    case NODE_LIT_NULL:  return value_incref(XS_NULL_VAL);
    case NODE_LIT_CHAR:  return xs_char(n->lit_char.cval);
    case NODE_LIT_STRING:
        return xs_str(n->lit_string.sval ? n->lit_string.sval : "");

    case NODE_INTERP_STRING: {
        char *result = xs_strdup("");
        int result_len = 0;
        for (int j = 0; j < n->lit_string.parts.len; j++) {
            Node *part = n->lit_string.parts.items[j];
            char *piece;
            if (part->tag == NODE_LIT_STRING) {
                piece = xs_strdup(part->lit_string.sval ? part->lit_string.sval : "");
            } else {
                Value *v = EVAL(i, part);
                piece = value_str(v);
                value_decref(v);
            }
            int plen = (int)strlen(piece);
            result = xs_realloc(result, result_len + plen + 1);
            memcpy(result + result_len, piece, plen + 1);
            result_len += plen;
            free(piece);
        }
        Value *v = xs_str(result);
        free(result);
        return v;
    }

    case NODE_LIT_ARRAY: {
        Value *arr = xs_array_new();
        /* [expr; N] repeat literal */
        if (n->lit_array.repeat_val && n->lit_array.repeat_cnt > 0) {
            Value *fill = EVAL(i, n->lit_array.repeat_val);
            for (int64_t j = 0; j < n->lit_array.repeat_cnt; j++)
                array_push(arr->arr, value_incref(fill));
            value_decref(fill);
            return arr;
        }
        for (int j = 0; j < n->lit_array.elems.len; j++) {
            Node *elem = n->lit_array.elems.items[j];
            if (elem->tag == NODE_SPREAD) {
                Value *sv = EVAL(i, elem->spread.expr);
                if (sv->tag == XS_ARRAY) {
                    for (int k=0;k<sv->arr->len;k++) array_push(arr->arr, value_incref(sv->arr->items[k]));
                }
                value_decref(sv);
            } else {
                Value *ev = EVAL(i, elem);
                if (!i->cf.signal) array_push(arr->arr, ev);
                else { value_decref(ev); break; }
            }
        }
        return arr;
    }

    case NODE_LIST_COMP: {
        /* [expr for pat in iter] */
        Value *result = xs_array_new();
        for (int cl = 0; cl < n->list_comp.clause_pats.len; cl++) {
            Node *pat  = n->list_comp.clause_pats.items[cl];
            Node *iter_expr = n->list_comp.clause_iters.items[cl];
            Node *cond = (cl < n->list_comp.clause_conds.len)
                         ? n->list_comp.clause_conds.items[cl] : NULL;
            Value *iter_val = EVAL(i, iter_expr);
            if (i->cf.signal) { value_decref(iter_val); return result; }

            /* Iterate over the value */
            int iter_len = 0;
            Value **iter_items = NULL;
            Value *range_arr = NULL; /* temp if range */
            if (iter_val->tag == XS_ARRAY || iter_val->tag == XS_TUPLE) {
                iter_len = iter_val->arr->len;
                iter_items = iter_val->arr->items;
            } else if (iter_val->tag == XS_RANGE) {
                /* Expand range to array */
                range_arr = xs_array_new();
                int64_t start = iter_val->range->start;
                int64_t end   = iter_val->range->end;
                if (iter_val->range->inclusive) end++;
                for (int64_t ri = start; ri < end; ri++)
                    array_push(range_arr->arr, xs_int(ri));
                iter_len = range_arr->arr->len;
                iter_items = range_arr->arr->items;
            }

            push_env(i);
            for (int idx = 0; idx < iter_len && !i->cf.signal; idx++) {
                /* Bind pattern */
                bind_pattern(i, pat, iter_items[idx], i->env, 1);
                /* Check guard */
                if (cond) {
                    Value *cv = EVAL(i, cond);
                    int ok = value_truthy(cv);
                    value_decref(cv);
                    if (!ok) continue;
                }
                /* Evaluate element expression */
                Value *elem = EVAL(i, n->list_comp.element);
                if (!i->cf.signal) array_push(result->arr, elem);
                else value_decref(elem);
            }
            pop_env(i);
            if (range_arr) value_decref(range_arr);
            value_decref(iter_val);
        }
        return result;
    }

    case NODE_MAP_COMP: {
        /* {key_expr: val_expr for pat in iter if cond} */
        Value *result = xs_map_new();
        for (int cl = 0; cl < n->map_comp.clause_pats.len; cl++) {
            Node *pat  = n->map_comp.clause_pats.items[cl];
            Node *iter_expr = n->map_comp.clause_iters.items[cl];
            Node *cond = (cl < n->map_comp.clause_conds.len)
                         ? n->map_comp.clause_conds.items[cl] : NULL;
            Value *iter_val = EVAL(i, iter_expr);
            if (i->cf.signal) { value_decref(iter_val); return result; }

            /* Iterate over the value */
            int iter_len = 0;
            Value **iter_items = NULL;
            Value *range_arr = NULL;
            if (iter_val->tag == XS_ARRAY || iter_val->tag == XS_TUPLE) {
                iter_len = iter_val->arr->len;
                iter_items = iter_val->arr->items;
            } else if (iter_val->tag == XS_RANGE) {
                range_arr = xs_array_new();
                int64_t start = iter_val->range->start;
                int64_t end   = iter_val->range->end;
                if (iter_val->range->inclusive) end++;
                for (int64_t ri = start; ri < end; ri++)
                    array_push(range_arr->arr, xs_int(ri));
                iter_len = range_arr->arr->len;
                iter_items = range_arr->arr->items;
            }

            push_env(i);
            for (int idx = 0; idx < iter_len && !i->cf.signal; idx++) {
                bind_pattern(i, pat, iter_items[idx], i->env, 1);
                if (cond) {
                    Value *cv = EVAL(i, cond);
                    int ok = value_truthy(cv);
                    value_decref(cv);
                    if (!ok) continue;
                }
                Value *k = EVAL(i, n->map_comp.key);
                Value *v = EVAL(i, n->map_comp.value);
                if (!i->cf.signal) {
                    if (k->tag == XS_STR) {
                        map_set(result->map, k->s, value_incref(v));
                    } else if (k->tag == XS_INT) {
                        char buf[32];
                        snprintf(buf, sizeof(buf), "%lld", (long long)k->i);
                        map_set(result->map, buf, value_incref(v));
                    }
                }
                value_decref(k);
                value_decref(v);
            }
            pop_env(i);
            if (range_arr) value_decref(range_arr);
            value_decref(iter_val);
        }
        return result;
    }

    case NODE_LIT_TUPLE: {
        Value *tup = xs_tuple_new();
        for (int j = 0; j < n->lit_array.elems.len; j++) {
            Value *ev = EVAL(i, n->lit_array.elems.items[j]);
            if (!i->cf.signal) array_push(tup->arr, ev);
            else { value_decref(ev); break; }
        }
        return tup;
    }

    case NODE_LIT_MAP: {
        Value *map = xs_map_new();
        for (int j = 0; j < n->lit_map.keys.len && j < n->lit_map.vals.len; j++) {
            Node *kn = n->lit_map.keys.items[j];
            if (kn->tag == NODE_SPREAD) {
                /* Spread: merge source map into target */
                Value *src = EVAL(i, kn->spread.expr);
                if (src && src->tag == XS_MAP) {
                    int nk = 0;
                    char **keys = map_keys(src->map, &nk);
                    for (int ki = 0; ki < nk; ki++) {
                        Value *v = map_get(src->map, keys[ki]);
                        if (v) { value_incref(v); map_set(map->map, keys[ki], v); value_decref(v); }
                        free(keys[ki]);
                    }
                    free(keys);
                }
                if (src) value_decref(src);
                continue;
            }
            Value *vv = EVAL(i, n->lit_map.vals.items[j]);
            /* Identifier keys (e.g. { x: 5 }) are treated as string keys "x" */
            if (kn->tag == NODE_IDENT) {
                map_set(map->map, kn->ident.name, value_incref(vv));
            } else {
                Value *kv = EVAL(i, kn);
                if (kv->tag == XS_STR) {
                    map_set(map->map, kv->s, value_incref(vv));
                }
                value_decref(kv);
            }
            value_decref(vv);
        }
        return map;
    }

    case NODE_IDENT: {
        Value *v = env_get(i->env, n->ident.name);
        if (!v) {
            const char *suggestion = find_similar_name(i->env, n->ident.name);
            char hint_buf[128];
            if (suggestion) {
                snprintf(hint_buf, sizeof hint_buf, "did you mean '%s'?", suggestion);
                xs_runtime_error(n->span, "not found in this scope", hint_buf,
                        "name '%s' is not defined", n->ident.name);
            } else {
                xs_runtime_error(n->span, "not found in this scope", NULL,
                        "name '%s' is not defined", n->ident.name);
            }
            i->cf.signal = CF_ERROR;
            return value_incref(XS_NULL_VAL);
        }
        return value_incref(v);
    }

    case NODE_SCOPE: {
        /* A::B::C — look up the chain */
        if (n->scope.nparts == 0) return value_incref(XS_NULL_VAL);
        Value *v = env_get(i->env, n->scope.parts[0]);
        if (!v) return value_incref(XS_NULL_VAL);
        v = value_incref(v);
        for (int j = 1; j < n->scope.nparts; j++) {
            Value *next = NULL;
            if (v->tag == XS_MODULE || v->tag == XS_MAP)
                next = map_get(v->map, n->scope.parts[j]);
            else if (v->tag == XS_INST)
                next = map_get(v->inst->fields, n->scope.parts[j]);
            else if (v->tag == XS_CLASS_VAL)
                next = map_get(v->cls->methods, n->scope.parts[j]);
            value_decref(v);
            v = next ? value_incref(next) : value_incref(XS_NULL_VAL);
            if (!next) break;
        }
        return v;
    }

    case NODE_BINOP: return eval_binop(i, n);

    case NODE_UNARY: {
        if (n->unary.op[0] == '?') {
            Value *v = EVAL(i, n->unary.expr);
            if (v->tag == XS_ENUM_VAL && strcmp(v->en->variant, "Err") == 0) {
                if (i->cf.value) value_decref(i->cf.value);
                i->cf.signal = CF_RETURN;
                i->cf.value  = v; /* transfer ownership */
                return value_incref(XS_NULL_VAL);
            }
            if (v->tag == XS_ENUM_VAL && strcmp(v->en->variant, "Ok") == 0) {
                Value *inner = (v->en->arr_data && v->en->arr_data->len > 0)
                               ? value_incref(v->en->arr_data->items[0])
                               : value_incref(XS_NULL_VAL);
                value_decref(v);
                return inner;
            }
            return v;
        }
        Value *v = EVAL(i, n->unary.expr);
        Value *result = NULL;
        if (n->unary.op[0] == '-') {
            if (v->tag == XS_INT)   result = xs_safe_neg(v->i);
            else if (v->tag == XS_BIGINT) result = xs_numeric_neg(v);
            else if (v->tag==XS_FLOAT) result = xs_float(-v->f);
            else result = value_incref(XS_NULL_VAL);
        } else if (n->unary.op[0] == '!') {
            result = value_truthy(v) ? value_incref(XS_FALSE_VAL) : value_incref(XS_TRUE_VAL);
        } else if (n->unary.op[0] == '~') {
            result = (v->tag==XS_INT) ? xs_int(~v->i) : value_incref(XS_NULL_VAL);
        } else {
            result = value_incref(v);
        }
        value_decref(v);
        return result;
    }

    case NODE_ASSIGN: {
        Value *val = EVAL(i, n->assign.value);
        Node *target = n->assign.target;
        Value *result = val;
        if (n->assign.op[0] != '=' || n->assign.op[1] != '\0') {
            Value *old = EVAL(i, target);
            char opbuf[8] = {0};
            const char *aop = n->assign.op;
            if (strcmp(aop,"+=") == 0)   strcpy(opbuf,"+");
            else if (strcmp(aop,"-=") == 0) strcpy(opbuf,"-");
            else if (strcmp(aop,"*=") == 0) strcpy(opbuf,"*");
            else if (strcmp(aop,"/=") == 0) strcpy(opbuf,"/");
            else if (strcmp(aop,"%=") == 0) strcpy(opbuf,"%");
            else if (strcmp(aop,"&=") == 0) strcpy(opbuf,"&");
            else if (strcmp(aop,"|=") == 0) strcpy(opbuf,"|");
            else if (strcmp(aop,"^=") == 0) strcpy(opbuf,"^");
            else if (strcmp(aop,"**=") == 0) strcpy(opbuf,"**");
            else strcpy(opbuf,"=");
            Value *computed = NULL;
            if (old->tag==XS_INT && val->tag==XS_INT) {
                int64_t a=old->i, b=val->i;
                if (opbuf[0]=='+') computed=xs_int(a+b);
                else if(opbuf[0]=='-') computed=xs_int(a-b);
                else if(opbuf[0]=='*'&&opbuf[1]=='\0') computed=xs_int(a*b);
                else if(opbuf[0]=='/'&&opbuf[1]=='\0') { if(!b){xs_runtime_error(n->span,"division by zero",NULL,"cannot divide by zero");computed=value_incref(XS_NULL_VAL);}else computed=xs_int(a/b); }
                else if(opbuf[0]=='%') { if(!b){xs_runtime_error(n->span,"modulo by zero",NULL,"cannot take remainder with divisor zero");computed=value_incref(XS_NULL_VAL);}else computed=xs_int(a%b); }
                else if(opbuf[0]=='&') computed=xs_int(a&b);
                else if(opbuf[0]=='|') computed=xs_int(a|b);
                else if(opbuf[0]=='^') computed=xs_int(a^b);
                else computed=value_incref(val);
            } else if ((old->tag==XS_FLOAT||old->tag==XS_INT) &&
                       (val->tag==XS_FLOAT||val->tag==XS_INT)) {
                double a2=(old->tag==XS_FLOAT)?old->f:(double)old->i;
                double b3=(val->tag==XS_FLOAT)?val->f:(double)val->i;
                if (opbuf[0]=='+') computed=xs_float(a2+b3);
                else if(opbuf[0]=='-') computed=xs_float(a2-b3);
                else if(opbuf[0]=='*') computed=xs_float(a2*b3);
                else if(opbuf[0]=='/') computed=b3?xs_float(a2/b3):xs_float(0);
                else computed=value_incref(val);
            } else if (old->tag==XS_STR && opbuf[0]=='+') {
                char *rs=value_str(val);
                int la=(int)strlen(old->s),lb=(int)strlen(rs);
                char *buf=xs_malloc(la+lb+1);
                memcpy(buf,old->s,la); memcpy(buf+la,rs,lb+1);
                free(rs); computed=xs_str(buf); free(buf);
            } else {
                computed=value_incref(val);
            }
            value_decref(old);
            value_decref(val);
            result=computed; val=computed;
        }

        if (target->tag == NODE_IDENT) {
            int r = env_set(i->env, target->ident.name, result);
            if (r == -1) {
                env_define(i->env, target->ident.name, result, 1);
            } else if (r == -2) {
                xs_runtime_error(target->span,
                        "cannot assign — declared with 'let'",
                        "use 'var' instead of 'let' to allow mutation",
                        "cannot assign to immutable variable '%s'",
                        target->ident.name);
                i->cf.signal = CF_ERROR;
                value_decref(result);
                return value_incref(XS_NULL_VAL);
            }
        } else if (target->tag == NODE_INDEX) {
            Value *obj = EVAL(i, target->index.obj);
            Value *idx = EVAL(i, target->index.index);
            if (obj->tag == XS_ARRAY || obj->tag == XS_TUPLE) {
                int ai = (int)((idx->tag==XS_INT)?idx->i:0);
                if (ai < 0) ai = obj->arr->len + ai;
                if (ai >= 0 && ai < obj->arr->len) {
                    value_decref(obj->arr->items[ai]);
                    obj->arr->items[ai] = value_incref(result);
                }
            } else if (obj->tag == XS_MAP || obj->tag == XS_MODULE) {
                if (idx->tag == XS_STR) {
                    map_set(obj->map, idx->s, value_incref(result));
                } else {
                    char *ks = value_str(idx);
                    map_set(obj->map, ks, value_incref(result));
                    free(ks);
                }
            }
            value_decref(obj); value_decref(idx);
        } else if (target->tag == NODE_FIELD) {
            Value *obj = EVAL(i, target->field.obj);
            if (obj->tag == XS_INST) {
                map_set(obj->inst->fields, target->field.name, value_incref(result));
            } else if (obj->tag == XS_MAP || obj->tag == XS_MODULE) {
                map_set(obj->map, target->field.name, value_incref(result));
            } else if (obj->tag == XS_STRUCT_VAL) {
                map_set(obj->st->fields, target->field.name, value_incref(result));
            } else if (obj->tag == XS_ACTOR && obj->actor) {
                map_set(obj->actor->state, target->field.name, value_incref(result));
            }
            value_decref(obj);
        }

        Value *ret = value_incref(result);
        if (val != result) value_decref(val);
        else value_decref(result);
        return ret;
    }

    case NODE_CALL: {
        if (n->call.callee->tag == NODE_IDENT &&
            strcmp(n->call.callee->ident.name, "dbg") == 0) {
            int argc = n->call.args.len;
            Value *last = NULL;
            for (int j = 0; j < argc; j++) {
                Node *arg_node = n->call.args.items[j];
                Value *val = EVAL(i, arg_node);
                if (i->cf.signal) { value_decref(val); return value_incref(XS_NULL_VAL); }
                char *repr = value_repr(val);
                if (arg_node->tag == NODE_IDENT) {
                    fprintf(stderr, "[dbg] %s = %s\n", arg_node->ident.name, repr);
                } else {
                    fprintf(stderr, "[dbg] %s\n", repr);
                }
                free(repr);
                if (last) value_decref(last);
                last = val;
            }
            return last ? last : value_incref(XS_NULL_VAL);
        }

        Value *callee = EVAL(i, n->call.callee);
        if (i->cf.signal) {
            value_decref(callee);
            return value_incref(XS_NULL_VAL);
        }
        int argc = n->call.args.len;
        Value **args = argc ? xs_malloc(argc * sizeof(Value*)) : NULL;
        for (int j = 0; j < argc; j++) {
            Node *an = n->call.args.items[j];
            if (an->tag == NODE_SPREAD) {
                Value *sv = EVAL(i, an->spread.expr);
                if (sv->tag == XS_ARRAY) {
                    int extra = sv->arr->len;
                    int new_argc = j + extra + (argc - j - 1);
                    Value **new_args = new_argc ? xs_malloc(new_argc*sizeof(Value*)) : NULL;
                    for (int k=0;k<j;k++) new_args[k]=args[k];
                    for (int k=0;k<extra;k++) new_args[j+k]=value_incref(sv->arr->items[k]);
                    for (int k=j+1;k<argc;k++) {
                        new_args[j+extra+(k-j-1)] = EVAL(i, n->call.args.items[k]);
                    }
                    value_decref(sv); free(args);
                    args = new_args; argc = new_argc;
                    goto do_call;
                }
                value_decref(sv);
                args[j] = value_incref(XS_NULL_VAL);
            } else {
                args[j] = EVAL(i, an);
            }
        }
do_call: ;
        i->current_span = n->span;
        Value *result = call_value(i, callee, args, argc, NULL);
        value_decref(callee);
        for (int j=0;j<argc;j++) value_decref(args[j]);
        if (args) free(args);
        /* phase 2: after_eval hooks for call nodes */
        if (g_has_eval_hooks && g_n_after_eval > 0 && !g_in_eval_hook) {
            g_in_eval_hook = 1;
            for (int _h = 0; _h < g_n_after_eval; _h++) {
                EvalHook *hook = &g_after_eval[_h];
                if (!hook->callback) continue;
                if (hook->tag_filter >= 0 && hook->tag_filter != NODE_CALL) continue;
                Value *node_map = node_to_xs_map(n);
                Value *hargs[2] = { node_map, result };
                Value *hresult = call_value(i, hook->callback, hargs, 2, "after_eval");
                value_decref(node_map);
                if (hresult && hresult->tag != XS_NULL) {
                    value_decref(result);
                    result = hresult;
                } else if (hresult) {
                    value_decref(hresult);
                }
            }
            g_in_eval_hook = 0;
        }
        return result;
    }

    case NODE_METHOD_CALL: {
        Value *obj = EVAL(i, n->method_call.obj);
        if (n->method_call.optional && obj->tag == XS_NULL) {
            return obj;
        }
        if (!obj || obj->tag == XS_NULL) {
            if (obj) value_decref(obj);
            return value_incref(XS_NULL_VAL);
        }
        int argc = n->method_call.args.len;
        Value **args = argc ? xs_malloc(argc*sizeof(Value*)) : NULL;
        for (int j=0;j<argc;j++) args[j] = EVAL(i, n->method_call.args.items[j]);
        i->current_span = n->span;
        Value *result = eval_method(i, obj, n->method_call.method, args, argc);
        value_decref(obj);
        for (int j=0;j<argc;j++) value_decref(args[j]);
        if (args) free(args);
        return result;
    }

    case NODE_INDEX: {
        Value *obj = EVAL(i, n->index.obj);
        Value *idx = EVAL(i, n->index.index);
        Value *result = NULL;

        if (obj->tag == XS_ARRAY || obj->tag == XS_TUPLE) {
            if (idx->tag == XS_INT) {
                result = value_incref(array_get(obj->arr, (int)idx->i));
            } else if (idx->tag == XS_RANGE) {
                XSRange *r = idx->range;
                Value *slice = xs_array_new();
                int64_t step = r->step ? r->step : 1;
                int64_t end2 = r->inclusive ? r->end + (step > 0 ? 1 : -1) : r->end;
                int len = obj->arr->len;
                for (int64_t j=r->start; step > 0 ? (j<end2 && j<len) : (j>end2 && j>=-1); j += step) {
                    if (j>=0) array_push(slice->arr, value_incref(array_get(obj->arr,(int)j)));
                }
                result = slice;
            } else result = value_incref(XS_NULL_VAL);
        } else if (obj->tag == XS_MAP || obj->tag == XS_MODULE) {
            if (idx->tag == XS_STR) {
                Value *v = map_get(obj->map, idx->s);
                result = v ? value_incref(v) : value_incref(XS_NULL_VAL);
            } else {
                char *ks = value_str(idx);
                Value *v = map_get(obj->map, ks); free(ks);
                result = v ? value_incref(v) : value_incref(XS_NULL_VAL);
            }
        } else if (obj->tag == XS_STR) {
            if (idx->tag == XS_INT) {
                int ai=(int)idx->i; int slen=(int)strlen(obj->s);
                if (ai<0) ai=slen+ai;
                result=(ai>=0&&ai<slen)?xs_str_n(obj->s+ai,1):value_incref(XS_NULL_VAL);
            } else result = value_incref(XS_NULL_VAL);
        } else if (obj->tag == XS_RANGE) {
            /* range[int] → range.start + int */
            if (idx->tag == XS_INT) {
                int64_t ri = obj->range->start + idx->i;
                int64_t rend = obj->range->inclusive ? obj->range->end : obj->range->end - 1;
                result = (ri <= rend) ? xs_int(ri) : value_incref(XS_NULL_VAL);
            } else result = value_incref(XS_NULL_VAL);
        } else if (obj->tag == XS_INST && obj->inst) {
            /* __index__ dunder method on instances */
            Value *fn = map_get(obj->inst->methods, "__index__");
            if (!fn && obj->inst->class_ && obj->inst->class_->methods)
                fn = map_get(obj->inst->class_->methods, "__index__");
            if (fn && (fn->tag == XS_FUNC || fn->tag == XS_NATIVE)) {
                int has_self = 0;
                if (fn->tag == XS_FUNC && fn->fn->nparams > 0) {
                    Node *p0 = fn->fn->params[0];
                    if (p0->tag == NODE_PAT_IDENT && strcmp(p0->pat_ident.name, "self") == 0)
                        has_self = 1;
                }
                if (has_self) {
                    Value *call_args[2] = { obj, idx };
                    result = call_value(i, fn, call_args, 2, "__index__");
                } else {
                    Value *call_args[1] = { idx };
                    result = call_value(i, fn, call_args, 1, "__index__");
                }
            } else {
                result = value_incref(XS_NULL_VAL);
            }
        } else {
            result = value_incref(XS_NULL_VAL);
        }

        value_decref(obj); value_decref(idx);
        return result;
    }

    case NODE_FIELD: {
        Value *obj = EVAL(i, n->field.obj);
        if (n->field.optional && obj->tag == XS_NULL) return obj;
        if (!obj || obj->tag == XS_NULL) {
            if (obj) value_decref(obj);
            return value_incref(XS_NULL_VAL);
        }
        Value *result = NULL;
        const char *name = n->field.name;
        if (obj->tag == XS_INST) {
            Value *v = map_get(obj->inst->fields, name);
            if (!v) v = map_get(obj->inst->methods, name);
            if (v) result = value_incref(v);
        }
        if (!result && (obj->tag == XS_MAP || obj->tag == XS_MODULE)) {
            Value *v = map_get(obj->map, name);
            if (v) result = value_incref(v);
        }
        if (!result && obj->tag == XS_STRUCT_VAL) {
            Value *v = map_get(obj->st->fields, name);
            if (v) result = value_incref(v);
        }
        if (!result && obj->tag == XS_ENUM_VAL) {
            if (obj->en->map_data) {
                Value *v = map_get(obj->en->map_data, name);
                if (v) result = value_incref(v);
            }
        }
        if (!result && obj->tag == XS_CLASS_VAL) {
            /* Static methods first, then regular methods, then fields */
            Value *v = NULL;
            if (obj->cls->static_methods) v = map_get(obj->cls->static_methods, name);
            if (!v) v = map_get(obj->cls->methods, name);
            if (!v) v = map_get(obj->cls->fields, name);
            if (v) result = value_incref(v);
        }
        if (!result && obj->tag == XS_ACTOR && obj->actor) {
            Value *v = map_get(obj->actor->state, name);
            if (!v && obj->actor->methods) v = map_get(obj->actor->methods, name);
            if (v) result = value_incref(v);
        }
        /* Numeric field access for tuples/arrays: tup.0, tup.1, etc. */
        if (!result && (obj->tag == XS_TUPLE || obj->tag == XS_ARRAY)) {
            char *endp;
            long idx = strtol(name, &endp, 10);
            if (*endp == '\0' && endp != name) {
                if (idx >= 0 && idx < obj->arr->len)
                    result = value_incref(obj->arr->items[idx]);
                else
                    result = value_incref(XS_NULL_VAL);
            }
        }
        /* Property-style: .len, .is_empty, etc. */
        if (!result) {
            if (strcmp(name,"len")==0) {
                if (obj->tag==XS_ARRAY||obj->tag==XS_TUPLE) result=xs_int(obj->arr->len);
                else if (obj->tag==XS_STR) result=xs_int((int64_t)strlen(obj->s));
                else if (obj->tag==XS_MAP||obj->tag==XS_MODULE) result=xs_int(obj->map->len);
            } else if (strcmp(name,"is_empty")==0) {
                int empty=0;
                if (obj->tag==XS_ARRAY||obj->tag==XS_TUPLE) empty=obj->arr->len==0;
                else if (obj->tag==XS_STR) empty=!obj->s||!obj->s[0];
                else if (obj->tag==XS_MAP) empty=obj->map->len==0;
                result=empty?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
            }
        }
        if (!result) result = value_incref(XS_NULL_VAL);
        value_decref(obj);
        return result;
    }

    case NODE_IF: {
        Value *cond = EVAL(i, n->if_expr.cond);
        int ok = value_truthy(cond);
        value_decref(cond);
        if (i->coverage && n->if_expr.cond->span.line > 0)
            coverage_record_branch(i->coverage, n->if_expr.cond->span.line, ok);
        if (ok) {
            Value *r = EVAL(i, n->if_expr.then);
            return r;
        }
        for (int j = 0; j < n->if_expr.elif_conds.len; j++) {
            Value *ec = EVAL(i, n->if_expr.elif_conds.items[j]);
            int eok = value_truthy(ec);
            value_decref(ec);
            if (i->coverage && n->if_expr.elif_conds.items[j]->span.line > 0)
                coverage_record_branch(i->coverage, n->if_expr.elif_conds.items[j]->span.line, eok);
            if (eok) {
                return EVAL(i, n->if_expr.elif_thens.items[j]);
            }
        }
        if (n->if_expr.else_branch) return EVAL(i, n->if_expr.else_branch);
        return value_incref(XS_NULL_VAL);
    }

    case NODE_MATCH: {
        Value *subject = EVAL(i, n->match.subject);
        Value *result = value_incref(XS_NULL_VAL);
        for (int j = 0; j < n->match.arms.len; j++) {
            MatchArm *arm = &n->match.arms.items[j];
            Env *arm_env = env_new(i->env);
            if (match_pattern(i, arm->pattern, subject, arm_env)) {
                /* check guard */
                if (arm->guard) {
                    Env *saved = i->env; i->env = env_incref(arm_env);
                    Value *g = EVAL(i, arm->guard);
                    int gok = value_truthy(g); value_decref(g);
                    env_decref(i->env); i->env = saved;
                    if (!gok) { env_decref(arm_env); continue; }
                }
                Env *saved = i->env; i->env = env_incref(arm_env);
                value_decref(result);
                result = EVAL(i, arm->body);
                env_decref(i->env); i->env = saved;
                env_decref(arm_env);
                /* if CF_RETURN was set inside, propagate */
                break;
            }
            env_decref(arm_env);
        }
        value_decref(subject);
        return result;
    }

    case NODE_WHILE: {
        const char *my_label = n->while_loop.label;
        Value *result = value_incref(XS_NULL_VAL);
        while (1) {
            Value *cond = EVAL(i, n->while_loop.cond);
            int ok = value_truthy(cond); value_decref(cond);
            if (i->coverage && n->while_loop.cond->span.line > 0)
                coverage_record_branch(i->coverage, n->while_loop.cond->span.line, ok);
            if (!ok) break;
            value_decref(result);
            result = EVAL(i, n->while_loop.body);
            if (i->cf.signal == CF_BREAK) {
                if (!i->cf.label || (my_label && strcmp(i->cf.label, my_label)==0))
                    { CF_CLEAR(i); break; }
                break; /* labeled break for outer loop */
            }
            if (i->cf.signal == CF_CONTINUE) {
                if (!i->cf.label || (my_label && strcmp(i->cf.label, my_label)==0))
                    { CF_CLEAR(i); continue; }
                break; /* labeled continue for outer loop */
            }
            if (i->cf.signal) break;
        }
        return result;
    }

    case NODE_FOR: {
        const char *my_label = n->for_loop.label;
        Value *iter = EVAL(i, n->for_loop.iter);
        /* Macro to handle break/continue with label awareness */
#define FOR_BREAK_CHECK \
    if (i->cf.signal == CF_BREAK) { \
        if (!i->cf.label || (my_label && strcmp(i->cf.label, my_label)==0)) \
            { CF_CLEAR(i); break; } \
        break; /* labeled break for an outer loop */ \
    } \
    if (i->cf.signal == CF_CONTINUE) { \
        if (!i->cf.label || (my_label && strcmp(i->cf.label, my_label)==0)) \
            { CF_CLEAR(i); continue; } \
        break; /* labeled continue for an outer loop */ \
    } \
    if (i->cf.signal) break;

        if (iter->tag == XS_ARRAY || iter->tag == XS_TUPLE) {
            XSArray *arr = iter->arr;
            for (int fi = 0; fi < arr->len; fi++) {
                push_env(i);
                bind_pattern(i, n->for_loop.pattern, arr->items[fi], i->env, 1);
                interp_exec(i, n->for_loop.body);
                pop_env(i);
                FOR_BREAK_CHECK
            }
        } else if (iter->tag == XS_RANGE) {
            XSRange *r = iter->range;
            int64_t step = r->step ? r->step : 1;
            int64_t end2 = r->inclusive ? r->end + (step > 0 ? 1 : -1) : r->end;
            for (int64_t fi = r->start; step > 0 ? fi < end2 : fi > end2; fi += step) {
                Value *v = xs_int(fi);
                push_env(i);
                bind_pattern(i, n->for_loop.pattern, v, i->env, 1);
                value_decref(v);
                interp_exec(i, n->for_loop.body);
                pop_env(i);
                FOR_BREAK_CHECK
            }
        } else if (iter->tag == XS_STR) {
            const char *s = iter->s;
            for (int fi = 0; s[fi]; fi++) {
                Value *v = xs_str_n(s+fi, 1);
                push_env(i);
                bind_pattern(i, n->for_loop.pattern, v, i->env, 1);
                value_decref(v);
                interp_exec(i, n->for_loop.body);
                pop_env(i);
                FOR_BREAK_CHECK
            }
        } else if (iter->tag == XS_MAP && map_get(iter->map, "__type") &&
                   map_get(iter->map, "__type")->tag == XS_STR &&
                   strcmp(map_get(iter->map, "__type")->s, "generator") == 0) {
            /* Generator iterator: iterate over yielded values via next() */
            Value *yields = map_get(iter->map, "_yields");
            if (yields && yields->tag == XS_ARRAY) {
                for (int fi = 0; fi < yields->arr->len; fi++) {
                    push_env(i);
                    bind_pattern(i, n->for_loop.pattern, yields->arr->items[fi], i->env, 1);
                    interp_exec(i, n->for_loop.body);
                    pop_env(i);
                    FOR_BREAK_CHECK
                }
            }
        } else if ((iter->tag == XS_INST &&
                    (map_has(iter->inst->methods, "next") ||
                     map_has(iter->inst->fields, "next"))) ||
                   (iter->tag == XS_MAP && map_has(iter->map, "next"))) {
            /* Iterator protocol: call .iter() if present, then loop .next() */
            Value *iter_obj = NULL;
            /* Check for .iter() method first */
            int has_iter = 0;
            if (iter->tag == XS_INST)
                has_iter = map_has(iter->inst->methods, "iter") ||
                            map_has(iter->inst->fields, "iter");
            else if (iter->tag == XS_MAP)
                has_iter = map_has(iter->map, "iter");
            if (has_iter) {
                iter_obj = eval_method(i, iter, "iter", NULL, 0);
                if (!iter_obj || iter_obj->tag == XS_NULL) {
                    if (iter_obj) value_decref(iter_obj);
                    iter_obj = value_incref(iter); /* .iter() returned null — use iter directly */
                }
            } else {
                iter_obj = value_incref(iter); /* no .iter() — use iter directly */
            }
            while (1) {
                Value *result = eval_method(i, iter_obj, "next", NULL, 0);
                if (!result) break;
                /* Stop when not Some(v) — None returns XS_NULL or a non-Some enum */
                if (result->tag != XS_ENUM_VAL ||
                    !result->en->variant ||
                    strcmp(result->en->variant, "Some") != 0) {
                    value_decref(result);
                    break;
                }
                /* Unwrap Some(v) */
                Value *item = (result->en->arr_data && result->en->arr_data->len > 0)
                    ? value_incref(result->en->arr_data->items[0])
                    : value_incref(XS_NULL_VAL);
                value_decref(result);
                push_env(i);
                bind_pattern(i, n->for_loop.pattern, item, i->env, 1);
                value_decref(item);
                interp_exec(i, n->for_loop.body);
                pop_env(i);
                FOR_BREAK_CHECK
            }
            value_decref(iter_obj);
        } else {
            /* fallback for other types */
            interp_for_each(i, iter, n->for_loop.pattern, n->for_loop.body);
            /* interp_for_each clears unlabeled breaks; if a labeled break bubbled up,
               check if it matches this loop's label */
            if (i->cf.signal == CF_BREAK) {
                if (!i->cf.label || (my_label && strcmp(i->cf.label, my_label)==0))
                    CF_CLEAR(i);
            } else if (i->cf.signal == CF_CONTINUE) {
                if (!i->cf.label || (my_label && strcmp(i->cf.label, my_label)==0))
                    CF_CLEAR(i);
            }
        }
#undef FOR_BREAK_CHECK
        value_decref(iter);
        return value_incref(XS_NULL_VAL);
    }

    case NODE_LOOP: {
        const char *my_label = n->loop.label;
        Value *loop_result = NULL;
        while (1) {
            interp_exec(i, n->loop.body);
            if (i->cf.signal == CF_BREAK) {
                if (!i->cf.label || (my_label && strcmp(i->cf.label, my_label)==0)) {
                    /* Save break value before CF_CLEAR destroys it */
                    loop_result = i->cf.value ? value_incref(i->cf.value) : NULL;
                    CF_CLEAR(i); break;
                }
                break;
            }
            if (i->cf.signal == CF_CONTINUE) {
                if (!i->cf.label || (my_label && strcmp(i->cf.label, my_label)==0))
                    { CF_CLEAR(i); continue; }
                break;
            }
            if (i->cf.signal) break;
        }
        if (loop_result) return loop_result;
        return value_incref(XS_NULL_VAL);
    }

    case NODE_BREAK: {
        Value *val = n->brk.value ? EVAL(i, n->brk.value) : value_incref(XS_NULL_VAL);
        if (i->cf.value) value_decref(i->cf.value);
        free(i->cf.label); i->cf.label = n->brk.label ? xs_strdup(n->brk.label) : NULL;
        i->cf.signal = CF_BREAK;
        i->cf.value  = val;
        return value_incref(XS_NULL_VAL);
    }

    case NODE_CONTINUE: {
        free(i->cf.label); i->cf.label = n->cont.label ? xs_strdup(n->cont.label) : NULL;
        i->cf.signal = CF_CONTINUE;
        return value_incref(XS_NULL_VAL);
    }

    case NODE_RETURN: {
        /* Tail call optimization: if returning a plain call, signal CF_TAIL_CALL
           so call_value can loop instead of recursing */
        if (n->ret.value && n->ret.value->tag == NODE_CALL) {
            Node *cn = n->ret.value;
            Value *callee = EVAL(i, cn->call.callee);
            if (i->cf.signal) { value_decref(callee); return value_incref(XS_NULL_VAL); }
            /* Only trampoline for XS_FUNC calls (not natives, classes, etc.) */
            if (callee->tag == XS_FUNC) {
                int argc = cn->call.args.len;
                Value **args = argc ? xs_malloc(argc * sizeof(Value*)) : NULL;
                for (int j = 0; j < argc; j++) {
                    args[j] = EVAL(i, cn->call.args.items[j]);
                    if (i->cf.signal) {
                        for (int k = 0; k <= j; k++) value_decref(args[k]);
                        free(args); value_decref(callee);
                        return value_incref(XS_NULL_VAL);
                    }
                }
                /* Stash tail call info */
                i->tc_callee = callee; /* transfer ownership */
                i->tc_args   = args;
                i->tc_argc   = argc;
                i->cf.signal = CF_TAIL_CALL;
                if (i->cf.value) value_decref(i->cf.value);
                i->cf.value  = NULL;
                return value_incref(XS_NULL_VAL);
            }
            value_decref(callee);
        }
        Value *val = n->ret.value ? EVAL(i, n->ret.value) : value_incref(XS_NULL_VAL);
        if (i->cf.value) value_decref(i->cf.value);
        i->cf.signal = CF_RETURN;
        i->cf.value  = val;
        return value_incref(XS_NULL_VAL);
    }

    case NODE_YIELD: {
        Value *val = n->yield_.value ? EVAL(i, n->yield_.value) : value_incref(XS_NULL_VAL);
        if (i->cf.signal) { value_decref(val); return value_incref(XS_NULL_VAL); }
        if (i->yield_collect) {
            /* generator collect mode: push value into collector array */
            array_push(i->yield_collect->arr, val); /* array_push takes ownership */
        } else {
            /* standalone yield outside generator context: set CF_YIELD */
            if (i->cf.value) value_decref(i->cf.value);
            i->cf.signal = CF_YIELD;
            i->cf.value  = val;
        }
        return value_incref(XS_NULL_VAL);
    }

    case NODE_THROW: {
        Value *val = EVAL(i, n->throw_.value);
        if (i->cf.value) value_decref(i->cf.value);
        i->cf.signal = CF_THROW;
        i->cf.value  = val;
        return value_incref(XS_NULL_VAL);
    }

    case NODE_TRY: {
        /* Execute body and capture result */
        Value *result = EVAL(i, n->try_.body);
        if (i->cf.signal == CF_THROW) {
            value_decref(result);
            result = value_incref(XS_NULL_VAL);
            Value *exc = i->cf.value;
            if (exc) value_incref(exc);
            CF_CLEAR(i);
            /* Find matching catch arm */
            int caught = 0;
            for (int j = 0; j < n->try_.catch_arms.len; j++) {
                MatchArm *arm = &n->try_.catch_arms.items[j];
                Env *arm_env = env_new(i->env);
                int matches = !arm->pattern ||
                              match_pattern(i, arm->pattern, exc ? exc : XS_NULL_VAL, arm_env);
                if (matches) {
                    Env *saved = i->env; i->env = env_incref(arm_env);
                    value_decref(result);
                    result = EVAL(i, arm->body);
                    env_decref(i->env); i->env = saved;
                    env_decref(arm_env);
                    caught = 1;
                    break;
                }
                env_decref(arm_env);
            }
            if (exc) value_decref(exc);
            if (!caught && exc) {
                /* re-throw */
                if (i->cf.value) value_decref(i->cf.value);
                i->cf.signal = CF_THROW;
                i->cf.value  = value_incref(exc);
            }
        }
        if (n->try_.finally_block && !i->cf.signal)
            interp_exec(i, n->try_.finally_block);
        return result;
    }

    case NODE_LAMBDA: {
        int nparams = n->lambda.params.len;
        Node **params   = nparams ? xs_malloc(nparams * sizeof(Node*)) : NULL;
        Node **defaults = nparams ? xs_calloc(nparams, sizeof(Node*)) : NULL;
        int  *varflags  = nparams ? xs_calloc(nparams, sizeof(int)) : NULL;
        for (int j = 0; j < nparams; j++) {
            Param *pm = &n->lambda.params.items[j];
            if (pm->pattern) {
                if (pm->pattern->tag == NODE_IDENT) {
                    Node *pn = node_new(NODE_PAT_IDENT, pm->pattern->span);
                    pn->pat_ident.name    = xs_strdup(pm->pattern->ident.name);
                    pn->pat_ident.mutable = 0;
                    params[j] = pn;
                } else {
                    params[j] = pm->pattern;
                }
            } else {
                Node *pn = node_new(NODE_PAT_IDENT, pm->span);
                pn->pat_ident.name    = xs_strdup(pm->name ? pm->name : "_");
                pn->pat_ident.mutable = 0;
                params[j] = pn;
            }
            defaults[j] = pm->default_val;
            varflags[j]  = pm->variadic;
        }
        XSFunc *fn = func_new_ex("<lambda>", params, nparams,
                               n->lambda.body, i->env, defaults, varflags);
        fn->is_generator = n->lambda.is_generator;
        if (nparams > 0) {
            int has_types = 0;
            for (int j = 0; j < nparams; j++) {
                Param *pm = &n->lambda.params.items[j];
                if (pm->type_ann && pm->type_ann->name) { has_types = 1; break; }
            }
            if (has_types) {
                fn->param_type_names = xs_calloc(nparams, sizeof(char*));
                for (int j = 0; j < nparams; j++) {
                    Param *pm = &n->lambda.params.items[j];
                    if (pm->type_ann && pm->type_ann->name)
                        fn->param_type_names[j] = xs_strdup(pm->type_ann->name);
                }
            }
        }
        Value *v = xs_func_new(fn);
        return v;
    }

    case NODE_BLOCK: {
        int has_decls = n->block.has_decls;
        if (has_decls == -1) {
            has_decls = 0;
            for (int j = 0; j < n->block.stmts.len; j++) {
                NodeTag t = n->block.stmts.items[j]->tag;
                if (t==NODE_LET||t==NODE_VAR||t==NODE_CONST||
                    t==NODE_FN_DECL||t==NODE_CLASS_DECL||
                    t==NODE_STRUCT_DECL||t==NODE_ENUM_DECL) {
                    has_decls = 1; break;
                }
            }
            n->block.has_decls = has_decls;
        }
        Env *saved = NULL;
        if (has_decls) {
            saved = i->env;
            push_env(i);
        }
        Value *result = value_incref(XS_NULL_VAL);
        for (int j = 0; j < n->block.stmts.len; j++) {
            interp_exec(i, n->block.stmts.items[j]);
            if (i->cf.signal) break;
        }
        if (!i->cf.signal && n->block.expr) {
            value_decref(result);
            result = EVAL(i, n->block.expr);
        }
        if (has_decls) {
            pop_env(i);
            i->env = saved;
        }
        return result;
    }

    case NODE_RANGE: {
        Value *start = n->range.start ? EVAL(i, n->range.start) : xs_int(0);
        Value *end   = n->range.end   ? EVAL(i, n->range.end)   : xs_int(0);
        int64_t sv = (start->tag==XS_INT)?start->i:(int64_t)start->f;
        int64_t ev = (end->tag==XS_INT)?end->i:(int64_t)end->f;
        value_decref(start); value_decref(end);
        return xs_range(sv, ev, n->range.inclusive);
    }

    case NODE_STRUCT_INIT: {
        const char *path = n->struct_init.path ? n->struct_init.path : "";
        const char *type_name = path;
        const char *sep = strstr(path, "::");
        while (sep) { type_name = sep+2; sep = strstr(type_name,"::"); }

        /* Check if it's an enum variant */
        Value *cls = env_get(i->env, type_name);
        if (!cls) {
            char first[128] = {0};
            const char *c = path;
            int fi=0;
            while (*c && *c!=':' && fi<127) first[fi++]=*c++;
            cls = env_get(i->env, first);
        }

        if (cls && cls->tag == XS_CLASS_VAL) {
            Value *args_empty[1];
            Value *inst = call_value(i, cls, args_empty, 0, path);
            if (inst && inst->tag == XS_INST) {
                for (int j=0;j<n->struct_init.fields.len;j++) {
                    char *fname = n->struct_init.fields.items[j].key;
                    if (cls->cls->fields && !map_has(cls->cls->fields, fname)) {
                        fprintf(stderr, "xs: error at %s:%d:%d: unknown field '%s' in struct '%s'\n",
                                n->span.file ? n->span.file : "<unknown>",
                                n->span.line, n->span.col, fname, path);
                        return value_incref(XS_NULL_VAL);
                    }
                }
                /* Apply spread/rest base first (explicit fields override) */
                if (n->struct_init.rest) {
                    Value *base = EVAL(i, n->struct_init.rest);
                    if (base && base->tag == XS_INST) {
                        int nk=0; char **ks=map_keys(base->inst->fields,&nk);
                        for (int j=0;j<nk;j++) {
                            Value *v=map_get(base->inst->fields,ks[j]);
                            if (v) map_set(inst->inst->fields,ks[j],value_incref(v));
                            free(ks[j]);
                        }
                        free(ks);
                    } else if (base && (base->tag == XS_MAP || base->tag == XS_MODULE)) {
                        int nk=0; char **ks=map_keys(base->map,&nk);
                        for (int j=0;j<nk;j++) {
                            Value *v=map_get(base->map,ks[j]);
                            if (v) map_set(inst->inst->fields,ks[j],value_incref(v));
                            free(ks[j]);
                        }
                        free(ks);
                    }
                    value_decref(base);
                }
                /* Set explicit fields (override spread values) */
                for (int j=0;j<n->struct_init.fields.len;j++) {
                    char *fname = n->struct_init.fields.items[j].key;
                    Value *fv = EVAL(i, n->struct_init.fields.items[j].val);
                    map_set(inst->inst->fields, fname, value_incref(fv));
                    value_decref(fv);
                }
                /* Validate: check for missing required fields */
                if (cls->cls->fields) {
                    int nkeys = 0;
                    char **keys = map_keys(cls->cls->fields, &nkeys);
                    for (int j = 0; j < nkeys; j++) {
                        /* Check instance fields (includes both spread and explicit) */
                        int found = map_has(inst->inst->fields, keys[j]);
                        if (!found) {
                            /* Check if the field has a non-null default */
                            Value *def = map_get(cls->cls->fields, keys[j]);
                            if (!def || def->tag == XS_NULL) {
                                fprintf(stderr, "xs: error at %s:%d:%d: missing field '%s' in struct '%s'\n",
                                        n->span.file ? n->span.file : "<unknown>",
                                        n->span.line, n->span.col, keys[j], path);
                                for (int f = j; f < nkeys; f++) free(keys[f]);
                                free(keys);
                                value_decref(inst);
                                return value_incref(XS_NULL_VAL);
                            }
                        }
                        free(keys[j]);
                    }
                    free(keys);
                }
            }
            return inst;
        }

        /* Build map-based struct */
        Value *m = xs_map_new();
        for (int j=0;j<n->struct_init.fields.len;j++) {
            char *fname = n->struct_init.fields.items[j].key;
            Value *fv = EVAL(i, n->struct_init.fields.items[j].val);
            map_set(m->map, fname, value_incref(fv));
            value_decref(fv);
        }
        if (n->struct_init.rest) {
            Value *base = EVAL(i, n->struct_init.rest);
            if (base->tag == XS_MAP || base->tag == XS_MODULE) {
                int nk=0; char **ks=map_keys(base->map,&nk);
                for (int j=0;j<nk;j++) {
                    if (!map_has(m->map, ks[j])) {
                        Value *v=map_get(base->map,ks[j]);
                        if (v) map_set(m->map,ks[j],value_incref(v));
                    }
                    free(ks[j]);
                }
                free(ks);
            }
            value_decref(base);
        }
        return m;
    }

    case NODE_CAST: {
        Value *v = EVAL(i, n->cast.expr);
        /* Basic type coercions */
        const char *t = n->cast.type_name ? n->cast.type_name : "";
        Value *result = NULL;
        if (strcmp(t,"i64")==0||strcmp(t,"int")==0||strcmp(t,"i32")==0||
            strcmp(t,"i128")==0||strcmp(t,"isize")==0||
            strcmp(t,"u64")==0||strcmp(t,"u32")==0||strcmp(t,"u8")==0||
            strcmp(t,"u16")==0||strcmp(t,"u128")==0||strcmp(t,"usize")==0) {
            if (v->tag==XS_INT)   result=value_incref(v);
            else if(v->tag==XS_FLOAT) result=xs_int((int64_t)v->f);
            else if(v->tag==XS_CHAR)  result=xs_int((unsigned char)(v->s?v->s[0]:0));
            else if(v->tag==XS_STR) {
                /* single-char string → ASCII value; otherwise parse as number */
                if (v->s && v->s[0] && !v->s[1]) result=xs_int((unsigned char)v->s[0]);
                else result=xs_int(atoll(v->s));
            }
            else if(v->tag==XS_BOOL)  result=xs_int(v->i);
            else result=xs_int(0);
        } else if (strcmp(t,"f64")==0||strcmp(t,"float")==0||strcmp(t,"f32")==0) {
            if (v->tag==XS_FLOAT) result=value_incref(v);
            else if(v->tag==XS_INT)   result=xs_float((double)v->i);
            else if(v->tag==XS_STR)   result=xs_float(atof(v->s));
            else result=xs_float(0.0);
        } else if (strcmp(t,"str")==0||strcmp(t,"String")==0) {
            char *s=value_str(v); result=xs_str(s); free(s);
        } else if (strcmp(t,"bool")==0) {
            result=value_truthy(v)?value_incref(XS_TRUE_VAL):value_incref(XS_FALSE_VAL);
        } else if (strcmp(t,"char")==0) {
            if (v->tag==XS_INT) result=xs_char((char)v->i);
            else if(v->tag==XS_STR&&v->s[0]) result=xs_char(v->s[0]);
            else result=xs_char(0);
        } else {
            result = value_incref(v);
        }
        value_decref(v);
        return result;
    }

    case NODE_EXPR_STMT:
        /* Should be handled by exec, but just in case */
        return EVAL(i, n->expr_stmt.expr);

    case NODE_PROGRAM:
        interp_run(i, n);
        return value_incref(XS_NULL_VAL);

    case NODE_EFFECT_DECL:
        return value_incref(XS_NULL_VAL);

    case NODE_HANDLE: {
        int arms_pushed = 0;
        for (int j = 0; j < n->handle.arms.len; j++) {
            EffectArm *arm = &n->handle.arms.items[j];
            EffectFrame *frame = xs_calloc(1, sizeof(EffectFrame));
            frame->effect_name  = xs_strdup(arm->effect_name);
            frame->op_name      = xs_strdup(arm->op_name);
            frame->params       = arm->params; /* borrow — do not free here */
            frame->handler_body = arm->body;
            frame->handler_env  = env_incref(i->env);
            frame->prev         = i->effect_stack;
            i->effect_stack     = frame;
            arms_pushed++;
        }

        Value *result = EVAL(i, n->handle.expr);
        for (int j = 0; j < arms_pushed; j++) {
            EffectFrame *frame = i->effect_stack;
            i->effect_stack = frame->prev;
            free(frame->effect_name);
            free(frame->op_name);
            env_decref(frame->handler_env);
            free(frame);
        }

        return result;
    }

    case NODE_PERFORM: {
        const char *eff = n->perform.effect_name;
        const char *op  = n->perform.op_name;
        EffectFrame *frame = i->effect_stack;
        while (frame) {
            if (strcmp(frame->effect_name, eff) == 0 &&
                strcmp(frame->op_name, op) == 0)
                break;
            frame = frame->prev;
        }
        if (!frame) {
            fprintf(stderr, "xs: unhandled effect %s.%s at %s:%d:%d\n", eff, op,
                    n->span.file ? n->span.file : "<unknown>",
                    n->span.line, n->span.col);
            return value_incref(XS_NULL_VAL);
        }

        int argc = n->perform.args.len;
        Value **args = argc ? xs_malloc(argc * sizeof(Value*)) : NULL;
        for (int j = 0; j < argc; j++)
            args[j] = EVAL(i, n->perform.args.items[j]);

        Env *handler_call_env = env_new(frame->handler_env);
        for (int j = 0; j < frame->params.len && j < argc; j++) {
            Param *pm = &frame->params.items[j];
            const char *pname = pm->name ? pm->name :
                (pm->pattern && pm->pattern->tag == NODE_PAT_IDENT ?
                 pm->pattern->pat_ident.name : NULL);
            if (pname)
                env_define(handler_call_env, pname, args[j], 1);
        }
        for (int j = 0; j < argc; j++) value_decref(args[j]);
        free(args);

        Env *saved_env = i->env;
        env_incref(saved_env);
        i->env = env_incref(handler_call_env);

        Value *saved_resume = i->resume_value;
        i->resume_value = value_incref(XS_NULL_VAL);

        int saved_in_handler = i->in_handler;
        i->in_handler = 1;

        Value *_ = EVAL(i, frame->handler_body);
        value_decref(_);

        if (i->cf.signal == CF_RESUME)
            CF_CLEAR(i);

        Value *resume_val = i->resume_value;
        i->resume_value   = saved_resume;
        i->in_handler     = saved_in_handler;

        env_decref(i->env);
        i->env = saved_env;
        env_decref(handler_call_env);

        return resume_val;
    }

    case NODE_RESUME: {
        Value *val = n->resume_.value ? EVAL(i, n->resume_.value) : value_incref(XS_NULL_VAL);
        if (i->resume_value) value_decref(i->resume_value);
        i->resume_value = val;
        if (i->cf.value) value_decref(i->cf.value);
        i->cf.signal = CF_RESUME;
        i->cf.value  = NULL;
        return value_incref(XS_NULL_VAL);
    }

    case NODE_AWAIT: {
        /* Cooperative await: evaluate expression, then check if it's a task handle */
        Value *v = EVAL(i, n->await_.expr);
        if (i->cf.signal) { value_decref(v); return value_incref(XS_NULL_VAL); }
        if (v->tag == XS_MAP) {
            Value *tid_val = map_get(v->map, "_task_id");
            if (tid_val && tid_val->tag == XS_INT) {
                /* Task queue entry — run all pending tasks up to this one */
                int tid = (int)tid_val->i;
                for (int t = 0; t <= tid && t < i->n_tasks; t++) {
                    if (i->task_queue[t].done) continue;
                    Value *tfn = i->task_queue[t].fn;
                    Value *tres = NULL;
                    if (tfn) tres = call_value(i, tfn, NULL, 0, "spawn_task");
                    i->task_queue[t].result = tres ? tres : value_incref(XS_NULL_VAL);
                    i->task_queue[t].done = 1;
                }
                /* Return the awaited task's result */
                Value *result = value_incref(XS_NULL_VAL);
                if (tid >= 0 && tid < i->n_tasks && i->task_queue[tid].done) {
                    value_decref(result);
                    result = i->task_queue[tid].result
                        ? value_incref(i->task_queue[tid].result)
                        : value_incref(XS_NULL_VAL);
                    /* Update task handle */
                    { Value *sv = xs_str("done"); map_set(v->map, "_status", sv); value_decref(sv); }
                    map_set(v->map, "_result", i->task_queue[tid].result);
                }
                value_decref(v);
                return result;
            }
            /* Check for legacy _status / _result maps */
            Value *status = map_get(v->map, "_status");
            if (status && status->tag == XS_STR && strcmp(status->s, "done") == 0) {
                Value *res = map_get(v->map, "_result");
                Value *ret = res ? value_incref(res) : value_incref(XS_NULL_VAL);
                value_decref(v);
                return ret;
            }
        }
        if (v->tag == XS_FUNC || v->tag == XS_NATIVE) {
            Value *result = call_value(i, v, NULL, 0, "await");
            value_decref(v);
            return result;
        }
        return v; /* Already a concrete value */
    }

    case NODE_NURSERY: {
        /* Execute body; any NODE_SPAWN inside will queue tasks.
         * Set up task queue, run body, then execute all queued tasks. */
        Value *saved_queue = i->nursery_queue;
        Value *queue = xs_array_new();
        i->nursery_queue = queue;

        /* Run the nursery body */
        Value *body_val = EVAL(i, n->nursery_.body);
        value_decref(body_val);

        /* Restore nursery queue pointer */
        i->nursery_queue = saved_queue;

        /* Execute all spawned tasks in FIFO order */
        XSArray *tasks = queue->arr;
        for (int j = 0; j < tasks->len; j++) {
            if (i->cf.signal) break;
            Value *task = tasks->items[j];
            if (task->tag == XS_FUNC || task->tag == XS_NATIVE) {
                Value *r = call_value(i, task, NULL, 0, "nursery_task");
                /* If the task returned a callable (e.g. spawn fn() { ... }),
                   call it too so the user's function body actually runs. */
                if (!i->cf.signal && r &&
                    (r->tag == XS_FUNC || r->tag == XS_NATIVE)) {
                    Value *r2 = call_value(i, r, NULL, 0, "nursery_task");
                    value_decref(r2);
                }
                value_decref(r);
            } else {
                /* Already evaluated value — discard */
            }
        }
        value_decref(queue);
        return value_incref(XS_NULL_VAL);
    }

    case NODE_SPAWN: {
        /* Queue or execute a task expression.
         * When a nursery is active, wrap the expression as a zero-param function
         * and add it to the nursery queue for deferred execution.
         * Otherwise defer into cooperative task queue. */
        if (i->nursery_queue) {
            /* Wrap the expression body as a zero-param closure */
            XSFunc *fn = func_new_ex("__spawn__", NULL, 0,
                                     n->spawn_.expr, i->env, NULL, NULL);
            Value *task = xs_func_new(fn);
            array_push(i->nursery_queue->arr, task); /* queue takes ownership */
            return value_incref(XS_NULL_VAL);
        } else {
            /* Wrap the spawn expression as a zero-param closure for deferred execution */
            XSFunc *spawn_fn = func_new_ex("__spawn__", NULL, 0,
                                           n->spawn_.expr, i->env, NULL, NULL);
            Value *v = xs_func_new(spawn_fn);

            /* Check for actors first */
            if (n->spawn_.expr->tag == NODE_IDENT) {
                Value *check = EVAL(i, n->spawn_.expr);
                if (!i->cf.signal && check && check->tag == XS_ACTOR) {
                    value_decref(v);
                    Value *inst = call_value(i, check, NULL, 0, "spawn");
                    value_decref(check);
                    return inst;
                }
                if (check) value_decref(check);
                i->cf.signal = 0; /* clear any error from check */
            }

            /* Execute immediately (no real threads yet) and return task handle */
            Value *task = xs_map_new();
            Value *r = call_value(i, v, NULL, 0, "spawn");
            { Value *sv = xs_str("done"); map_set(task->map, "_status", sv); value_decref(sv); }
            map_set(task->map, "_result", r ? r : value_incref(XS_NULL_VAL));
            if (r) value_decref(r);
            if (i->n_tasks < 64) {
                int tid = i->n_tasks++;
                i->task_queue[tid].fn     = value_incref(v);
                i->task_queue[tid].result = map_get(task->map, "_result") ? value_incref(map_get(task->map, "_result")) : value_incref(XS_NULL_VAL);
                i->task_queue[tid].done   = 1;
                { Value *iv = xs_int(tid); map_set(task->map, "_task_id", iv); value_decref(iv); }
            }
            value_decref(v);
            return task;
        }
    }

    case NODE_SEND_EXPR: {
        /* actor ! message — synchronous dispatch */
        Value *target = EVAL(i, n->send_expr.target);
        Value *msg = EVAL(i, n->send_expr.message);
        Value *result = value_incref(XS_NULL_VAL);
        if (target->tag == XS_ACTOR && target->actor && target->actor->handle_fn) {
            XSActor *actor = target->actor;
            /* Create env with actor state */
            Env *wrapper = env_new(actor->handle_fn->closure ? actor->handle_fn->closure : actor->closure);
            env_define(wrapper, "self", value_incref(target), 0);
            /* Import actor state into env */
            if (actor->state) {
                int nkeys = 0;
                char **keys = map_keys(actor->state, &nkeys);
                for (int j = 0; j < nkeys; j++) {
                    Value *sv = map_get(actor->state, keys[j]);
                    if (sv) env_define(wrapper, keys[j], value_incref(sv), 1);
                    free(keys[j]);
                }
                free(keys);
            }
            /* Temporarily swap closure of handle_fn to wrapper */
            Env *orig_closure = actor->handle_fn->closure;
            env_incref(wrapper);
            actor->handle_fn->closure = wrapper;
            /* Call handle(msg) */
            Value *fn_val = xs_func_new(actor->handle_fn);
            value_decref(result);
            result = call_value(i, fn_val, &msg, 1, "handle");
            value_decref(fn_val);
            /* Restore closure */
            env_decref(actor->handle_fn->closure);
            actor->handle_fn->closure = orig_closure;
            /* Flush state back from wrapper env */
            if (actor->state) {
                int nkeys = 0;
                char **keys = map_keys(actor->state, &nkeys);
                for (int j = 0; j < nkeys; j++) {
                    Value *updated = env_get(wrapper, keys[j]);
                    if (updated) map_set(actor->state, keys[j], value_incref(updated));
                    free(keys[j]);
                }
                free(keys);
            }
            env_decref(wrapper);
        } else if (target->tag == XS_INST) {
            /* Fallback: treat ! on instances as calling .handle(msg) */
            Value *r = eval_method(i, target, "handle", &msg, 1);
            value_decref(result);
            result = r;
        }
        value_decref(target);
        value_decref(msg);
        return result;
    }

    default:
        /* Delegate to exec for statement nodes used in expression context */
        interp_exec(i, n);
        return value_incref(XS_NULL_VAL);
    }
}

static Value *load_xs_module_file(Interp *i, const char *filepath) {
    FILE *f = fopen(filepath, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *src = xs_malloc((size_t)(sz + 1));
    if (fread(src, 1, (size_t)sz, f) != (size_t)sz) {
        free(src);
        fclose(f);
        return NULL;
    }
    src[sz] = '\0';
    fclose(f);

    char *filepath_owned = xs_strdup(filepath);
    Lexer lex;
    lexer_init(&lex, src, filepath_owned);
    TokenArray ta = lexer_tokenize(&lex);
    Parser p;
    parser_init(&p, &ta, filepath_owned);
    Node *prog = parser_parse(&p);
    token_array_free(&ta);
    if (!prog || p.had_error) {
        free(src);
        free(filepath_owned);
        if (prog) node_free(prog);
        return NULL;
    }

    Env *saved = i->env;
    i->env = env_new(i->globals);
    for (int j = 0; j < prog->program.stmts.len; j++) {
        interp_exec(i, prog->program.stmts.items[j]);
        if (i->cf.signal == CF_RETURN) CF_CLEAR(i);
        else if (i->cf.signal) break;
    }
    if (i->cf.signal == CF_ERROR || i->cf.signal == CF_PANIC || i->cf.signal == CF_THROW)
        CF_CLEAR(i);

    XSMap *m = map_new();
    for (int j = 0; j < i->env->len; j++)
        map_set(m, i->env->bindings[j].name, value_incref(i->env->bindings[j].value));

    env_decref(i->env);
    i->env = saved;
    /* prog and src intentionally kept alive: function bodies
       reference AST nodes and the source buffer */
    (void)prog;
    (void)src;

    return xs_module(m);
}

static Value *try_load_xs_module(Interp *i, const char *modname) {
    char path[2048];
    struct stat st;

    /* try xs_modules/<name>/main.xs */
    snprintf(path, sizeof(path), "xs_modules/%s/main.xs", modname);
    if (stat(path, &st) == 0) return load_xs_module_file(i, path);

    /* try xs_modules/<name>/lib.xs */
    snprintf(path, sizeof(path), "xs_modules/%s/lib.xs", modname);
    if (stat(path, &st) == 0) return load_xs_module_file(i, path);

    /* try xs_modules/<name>/src/lib.xs */
    snprintf(path, sizeof(path), "xs_modules/%s/src/lib.xs", modname);
    if (stat(path, &st) == 0) return load_xs_module_file(i, path);

    /* try xs_modules/<name>/<name>.xs */
    snprintf(path, sizeof(path), "xs_modules/%s/%s.xs", modname, modname);
    if (stat(path, &st) == 0) return load_xs_module_file(i, path);

    return NULL;
}

/* ── XS plugin system native functions ── */

static Env *s_plugin_host_globals = NULL;

static Value *native_plugin_global_set(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 2 || !args[0] || args[0]->tag != XS_STR || !s_plugin_host_globals)
        return value_incref(XS_NULL_VAL);
    env_define(s_plugin_host_globals, args[0]->s, value_incref(args[1]), 1);
    return value_incref(XS_NULL_VAL);
}

static Value *native_plugin_global_get(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1 || !args[0] || args[0]->tag != XS_STR || !s_plugin_host_globals)
        return value_incref(XS_NULL_VAL);
    Value *v = env_get(s_plugin_host_globals, args[0]->s);
    return v ? value_incref(v) : value_incref(XS_NULL_VAL);
}

static Value *native_plugin_global_names(Interp *interp, Value **args, int argc) {
    (void)interp; (void)args; (void)argc;
    Value *arr = xs_array_new();
    if (!s_plugin_host_globals) return arr;
    for (int j = 0; j < s_plugin_host_globals->len; j++)
        array_push(arr->arr, xs_str(s_plugin_host_globals->bindings[j].name));
    return arr;
}

static Value *native_plugin_add_method(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 3 || !args[0] || args[0]->tag != XS_STR ||
        !args[1] || args[1]->tag != XS_STR ||
        !args[2] || (args[2]->tag != XS_FUNC && args[2]->tag != XS_NATIVE))
        return value_incref(XS_NULL_VAL);
    plugin_register_method(args[0]->s, args[1]->s, args[2]);
    return value_incref(XS_NULL_VAL);
}

static Value *native_plugin_teardown(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1 || !args[0] || (args[0]->tag != XS_FUNC && args[0]->tag != XS_NATIVE))
        return value_incref(XS_NULL_VAL);
    if (g_teardown_count < MAX_TEARDOWN_FNS) {
        g_teardown_fns[g_teardown_count++] = value_incref(args[0]);
    }
    return value_incref(XS_NULL_VAL);
}

static Value *native_plugin_requires(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1 || !args[0] || args[0]->tag != XS_STR)
        return value_incref(XS_NULL_VAL);
    if (!plugin_is_loaded(args[0]->s)) {
        char buf[256];
        snprintf(buf, sizeof(buf), "required plugin '%s' not loaded", args[0]->s);
        fprintf(stderr, "xs: error: %s\n", buf);
        if (g_current_interp) {
            g_current_interp->cf.signal = CF_PANIC;
            g_current_interp->cf.value = xs_str(buf);
        }
    }
    return value_incref(XS_NULL_VAL);
}

/* AST constructors */
static Value *native_ast_int_node(Interp *interp, Value **args, int argc) {
    (void)interp;
    Value *m = xs_map_new();
    map_set(m->map, "tag", xs_str("int"));
    map_set(m->map, "value", (argc > 0 && args[0]) ? value_incref(args[0]) : xs_int(0));
    return m;
}
static Value *native_ast_float_node(Interp *interp, Value **args, int argc) {
    (void)interp;
    Value *m = xs_map_new();
    map_set(m->map, "tag", xs_str("float"));
    map_set(m->map, "value", (argc > 0 && args[0]) ? value_incref(args[0]) : xs_float(0.0));
    return m;
}
static Value *native_ast_str_node(Interp *interp, Value **args, int argc) {
    (void)interp;
    Value *m = xs_map_new();
    map_set(m->map, "tag", xs_str("str"));
    map_set(m->map, "value", (argc > 0 && args[0]) ? value_incref(args[0]) : xs_str(""));
    return m;
}
static Value *native_ast_bool_node(Interp *interp, Value **args, int argc) {
    (void)interp;
    Value *m = xs_map_new();
    map_set(m->map, "tag", xs_str("bool"));
    map_set(m->map, "value", (argc > 0 && args[0]) ? value_incref(args[0]) : value_incref(XS_FALSE_VAL));
    return m;
}
static Value *native_ast_null_node(Interp *interp, Value **args, int argc) {
    (void)interp; (void)args; (void)argc;
    Value *m = xs_map_new();
    map_set(m->map, "tag", xs_str("null"));
    return m;
}
static Value *native_ast_ident(Interp *interp, Value **args, int argc) {
    (void)interp;
    Value *m = xs_map_new();
    map_set(m->map, "tag", xs_str("ident"));
    map_set(m->map, "name", (argc > 0 && args[0]) ? value_incref(args[0]) : xs_str(""));
    return m;
}
static Value *native_ast_binop(Interp *interp, Value **args, int argc) {
    (void)interp;
    Value *m = xs_map_new();
    map_set(m->map, "tag", xs_str("binop"));
    map_set(m->map, "op", (argc > 0 && args[0]) ? value_incref(args[0]) : xs_str("+"));
    map_set(m->map, "left", (argc > 1 && args[1]) ? value_incref(args[1]) : value_incref(XS_NULL_VAL));
    map_set(m->map, "right", (argc > 2 && args[2]) ? value_incref(args[2]) : value_incref(XS_NULL_VAL));
    return m;
}
static Value *native_ast_unary(Interp *interp, Value **args, int argc) {
    (void)interp;
    Value *m = xs_map_new();
    map_set(m->map, "tag", xs_str("unary"));
    map_set(m->map, "op", (argc > 0 && args[0]) ? value_incref(args[0]) : xs_str("-"));
    map_set(m->map, "expr", (argc > 1 && args[1]) ? value_incref(args[1]) : value_incref(XS_NULL_VAL));
    return m;
}
static Value *native_ast_call(Interp *interp, Value **args, int argc) {
    (void)interp;
    Value *m = xs_map_new();
    map_set(m->map, "tag", xs_str("call"));
    map_set(m->map, "callee", (argc > 0 && args[0]) ? value_incref(args[0]) : value_incref(XS_NULL_VAL));
    map_set(m->map, "args", (argc > 1 && args[1]) ? value_incref(args[1]) : xs_array_new());
    return m;
}
static Value *native_ast_method_call(Interp *interp, Value **args, int argc) {
    (void)interp;
    Value *m = xs_map_new();
    map_set(m->map, "tag", xs_str("method_call"));
    map_set(m->map, "obj", (argc > 0 && args[0]) ? value_incref(args[0]) : value_incref(XS_NULL_VAL));
    map_set(m->map, "method", (argc > 1 && args[1]) ? value_incref(args[1]) : xs_str(""));
    map_set(m->map, "args", (argc > 2 && args[2]) ? value_incref(args[2]) : xs_array_new());
    return m;
}
static Value *native_ast_if_expr(Interp *interp, Value **args, int argc) {
    (void)interp;
    Value *m = xs_map_new();
    map_set(m->map, "tag", xs_str("if"));
    map_set(m->map, "cond", (argc > 0 && args[0]) ? value_incref(args[0]) : value_incref(XS_NULL_VAL));
    map_set(m->map, "then", (argc > 1 && args[1]) ? value_incref(args[1]) : value_incref(XS_NULL_VAL));
    return m;
}
static Value *native_ast_if_else(Interp *interp, Value **args, int argc) {
    (void)interp;
    Value *m = xs_map_new();
    map_set(m->map, "tag", xs_str("if"));
    map_set(m->map, "cond", (argc > 0 && args[0]) ? value_incref(args[0]) : value_incref(XS_NULL_VAL));
    map_set(m->map, "then", (argc > 1 && args[1]) ? value_incref(args[1]) : value_incref(XS_NULL_VAL));
    map_set(m->map, "else", (argc > 2 && args[2]) ? value_incref(args[2]) : value_incref(XS_NULL_VAL));
    return m;
}
static Value *native_ast_block(Interp *interp, Value **args, int argc) {
    (void)interp;
    Value *m = xs_map_new();
    map_set(m->map, "tag", xs_str("block"));
    map_set(m->map, "stmts", (argc > 0 && args[0]) ? value_incref(args[0]) : xs_array_new());
    map_set(m->map, "expr", (argc > 1 && args[1]) ? value_incref(args[1]) : value_incref(XS_NULL_VAL));
    return m;
}
static Value *native_ast_let_decl(Interp *interp, Value **args, int argc) {
    (void)interp;
    Value *m = xs_map_new();
    map_set(m->map, "tag", xs_str("let"));
    map_set(m->map, "name", (argc > 0 && args[0]) ? value_incref(args[0]) : xs_str(""));
    map_set(m->map, "value", (argc > 1 && args[1]) ? value_incref(args[1]) : value_incref(XS_NULL_VAL));
    map_set(m->map, "type_ann", value_incref(XS_NULL_VAL));
    return m;
}
static Value *native_ast_var_decl(Interp *interp, Value **args, int argc) {
    (void)interp;
    Value *m = xs_map_new();
    map_set(m->map, "tag", xs_str("var"));
    map_set(m->map, "name", (argc > 0 && args[0]) ? value_incref(args[0]) : xs_str(""));
    map_set(m->map, "value", (argc > 1 && args[1]) ? value_incref(args[1]) : value_incref(XS_NULL_VAL));
    map_set(m->map, "type_ann", value_incref(XS_NULL_VAL));
    return m;
}
static Value *native_ast_fn_decl(Interp *interp, Value **args, int argc) {
    (void)interp;
    Value *m = xs_map_new();
    map_set(m->map, "tag", xs_str("fn_decl"));
    map_set(m->map, "name", (argc > 0 && args[0]) ? value_incref(args[0]) : xs_str(""));
    map_set(m->map, "params", (argc > 1 && args[1]) ? value_incref(args[1]) : xs_array_new());
    map_set(m->map, "body", (argc > 2 && args[2]) ? value_incref(args[2]) : value_incref(XS_NULL_VAL));
    map_set(m->map, "ret_type", value_incref(XS_NULL_VAL));
    return m;
}
static Value *native_ast_lambda(Interp *interp, Value **args, int argc) {
    (void)interp;
    Value *m = xs_map_new();
    map_set(m->map, "tag", xs_str("lambda"));
    map_set(m->map, "params", (argc > 0 && args[0]) ? value_incref(args[0]) : xs_array_new());
    map_set(m->map, "body", (argc > 1 && args[1]) ? value_incref(args[1]) : value_incref(XS_NULL_VAL));
    return m;
}
static Value *native_ast_return_node(Interp *interp, Value **args, int argc) {
    (void)interp;
    Value *m = xs_map_new();
    map_set(m->map, "tag", xs_str("return"));
    map_set(m->map, "value", (argc > 0 && args[0]) ? value_incref(args[0]) : value_incref(XS_NULL_VAL));
    return m;
}
static Value *native_ast_assign(Interp *interp, Value **args, int argc) {
    (void)interp;
    Value *m = xs_map_new();
    map_set(m->map, "tag", xs_str("assign"));
    map_set(m->map, "target", (argc > 0 && args[0]) ? value_incref(args[0]) : value_incref(XS_NULL_VAL));
    map_set(m->map, "value", (argc > 1 && args[1]) ? value_incref(args[1]) : value_incref(XS_NULL_VAL));
    return m;
}
static Value *native_ast_for_loop(Interp *interp, Value **args, int argc) {
    (void)interp;
    Value *m = xs_map_new();
    map_set(m->map, "tag", xs_str("for"));
    map_set(m->map, "pattern", (argc > 0 && args[0]) ? value_incref(args[0]) : value_incref(XS_NULL_VAL));
    map_set(m->map, "iter", (argc > 1 && args[1]) ? value_incref(args[1]) : value_incref(XS_NULL_VAL));
    map_set(m->map, "body", (argc > 2 && args[2]) ? value_incref(args[2]) : value_incref(XS_NULL_VAL));
    return m;
}
static Value *native_ast_while_loop(Interp *interp, Value **args, int argc) {
    (void)interp;
    Value *m = xs_map_new();
    map_set(m->map, "tag", xs_str("while"));
    map_set(m->map, "cond", (argc > 0 && args[0]) ? value_incref(args[0]) : value_incref(XS_NULL_VAL));
    map_set(m->map, "body", (argc > 1 && args[1]) ? value_incref(args[1]) : value_incref(XS_NULL_VAL));
    return m;
}
static Value *native_ast_array(Interp *interp, Value **args, int argc) {
    (void)interp;
    Value *m = xs_map_new();
    map_set(m->map, "tag", xs_str("array"));
    map_set(m->map, "elements", (argc > 0 && args[0]) ? value_incref(args[0]) : xs_array_new());
    return m;
}
static Value *native_ast_map_node(Interp *interp, Value **args, int argc) {
    (void)interp;
    Value *m = xs_map_new();
    map_set(m->map, "tag", xs_str("map"));
    map_set(m->map, "keys", (argc > 0 && args[0]) ? value_incref(args[0]) : xs_array_new());
    map_set(m->map, "values", (argc > 1 && args[1]) ? value_incref(args[1]) : xs_array_new());
    return m;
}

/* ── phase 2: node tag string conversion ── */

static const char *node_tag_to_string(NodeTag tag) {
    switch (tag) {
    case NODE_LIT_INT:    return "int";
    case NODE_LIT_FLOAT:  return "float";
    case NODE_LIT_STRING: return "str";
    case NODE_LIT_BOOL:   return "bool";
    case NODE_LIT_NULL:   return "null";
    case NODE_IDENT:      return "ident";
    case NODE_BINOP:      return "binop";
    case NODE_UNARY:      return "unary";
    case NODE_CALL:       return "call";
    case NODE_METHOD_CALL:return "method_call";
    case NODE_IF:         return "if";
    case NODE_BLOCK:      return "block";
    case NODE_LET:        return "let";
    case NODE_VAR:        return "var";
    case NODE_FN_DECL:    return "fn_decl";
    case NODE_LAMBDA:     return "lambda";
    case NODE_RETURN:     return "return";
    case NODE_ASSIGN:     return "assign";
    case NODE_FOR:        return "for";
    case NODE_WHILE:      return "while";
    case NODE_LOOP:       return "loop";
    case NODE_BREAK:      return "break";
    case NODE_CONTINUE:   return "continue";
    case NODE_INDEX:      return "index";
    case NODE_FIELD:      return "field";
    case NODE_LIT_ARRAY:  return "array";
    case NODE_LIT_MAP:    return "map";
    case NODE_RANGE:      return "range";
    case NODE_MATCH:      return "match";
    case NODE_THROW:      return "throw";
    case NODE_TRY:        return "try";
    case NODE_DEFER:      return "defer";
    case NODE_SPAWN:      return "spawn";
    case NODE_YIELD:      return "yield";
    case NODE_SPREAD:     return "spread";
    case NODE_STRUCT_INIT:return "struct_init";
    case NODE_EXPR_STMT:  return "expr_stmt";
    default:              return "unknown";
    }
}

static int node_tag_from_string(const char *s) {
    if (!s) return -1;
    if (strcmp(s, "int") == 0)    return NODE_LIT_INT;
    if (strcmp(s, "float") == 0)  return NODE_LIT_FLOAT;
    if (strcmp(s, "str") == 0)    return NODE_LIT_STRING;
    if (strcmp(s, "bool") == 0)   return NODE_LIT_BOOL;
    if (strcmp(s, "null") == 0)   return NODE_LIT_NULL;
    if (strcmp(s, "ident") == 0)  return NODE_IDENT;
    if (strcmp(s, "binop") == 0)  return NODE_BINOP;
    if (strcmp(s, "unary") == 0)  return NODE_UNARY;
    if (strcmp(s, "call") == 0)   return NODE_CALL;
    if (strcmp(s, "method_call") == 0) return NODE_METHOD_CALL;
    if (strcmp(s, "if") == 0)     return NODE_IF;
    if (strcmp(s, "block") == 0)  return NODE_BLOCK;
    if (strcmp(s, "let") == 0)    return NODE_LET;
    if (strcmp(s, "var") == 0)    return NODE_VAR;
    if (strcmp(s, "fn_decl") == 0) return NODE_FN_DECL;
    if (strcmp(s, "lambda") == 0) return NODE_LAMBDA;
    if (strcmp(s, "return") == 0) return NODE_RETURN;
    if (strcmp(s, "assign") == 0) return NODE_ASSIGN;
    if (strcmp(s, "for") == 0)    return NODE_FOR;
    if (strcmp(s, "while") == 0)  return NODE_WHILE;
    if (strcmp(s, "loop") == 0)   return NODE_LOOP;
    if (strcmp(s, "break") == 0)  return NODE_BREAK;
    if (strcmp(s, "continue") == 0) return NODE_CONTINUE;
    if (strcmp(s, "index") == 0)  return NODE_INDEX;
    if (strcmp(s, "field") == 0)  return NODE_FIELD;
    if (strcmp(s, "array") == 0)  return NODE_LIT_ARRAY;
    if (strcmp(s, "map") == 0)    return NODE_LIT_MAP;
    if (strcmp(s, "range") == 0)  return NODE_RANGE;
    if (strcmp(s, "match") == 0)  return NODE_MATCH;
    if (strcmp(s, "throw") == 0)  return NODE_THROW;
    if (strcmp(s, "try") == 0)    return NODE_TRY;
    if (strcmp(s, "defer") == 0)  return NODE_DEFER;
    if (strcmp(s, "spawn") == 0)  return NODE_SPAWN;
    if (strcmp(s, "yield") == 0)  return NODE_YIELD;
    if (strcmp(s, "spread") == 0) return NODE_SPREAD;
    if (strcmp(s, "struct_init") == 0) return NODE_STRUCT_INIT;
    if (strcmp(s, "expr_stmt") == 0) return NODE_EXPR_STMT;
    return -1;
}

/* Convert a C Node* to an XS map for plugin consumption */
static Value *node_to_xs_map(Node *n) {
    if (!n) return value_incref(XS_NULL_VAL);
    Value *m = xs_map_new();
    map_set(m->map, "tag", xs_str(node_tag_to_string(n->tag)));

    switch (n->tag) {
    case NODE_LIT_INT:
        map_set(m->map, "value", xs_int(n->lit_int.ival));
        break;
    case NODE_LIT_FLOAT:
        map_set(m->map, "value", xs_float(n->lit_float.fval));
        break;
    case NODE_LIT_STRING:
    case NODE_INTERP_STRING:
        map_set(m->map, "value", xs_str(n->lit_string.sval ? n->lit_string.sval : ""));
        break;
    case NODE_LIT_BOOL:
        map_set(m->map, "value", n->lit_bool.bval ? value_incref(XS_TRUE_VAL) : value_incref(XS_FALSE_VAL));
        break;
    case NODE_LIT_NULL:
        break;
    case NODE_IDENT:
        map_set(m->map, "name", xs_str(n->ident.name ? n->ident.name : ""));
        break;
    case NODE_BINOP:
        map_set(m->map, "op", xs_str(n->binop.op));
        map_set(m->map, "left", node_to_xs_map(n->binop.left));
        map_set(m->map, "right", node_to_xs_map(n->binop.right));
        break;
    case NODE_UNARY:
        map_set(m->map, "op", xs_str(n->unary.op));
        map_set(m->map, "expr", node_to_xs_map(n->unary.expr));
        break;
    case NODE_CALL: {
        map_set(m->map, "callee", node_to_xs_map(n->call.callee));
        Value *args = xs_array_new();
        for (int j = 0; j < n->call.args.len; j++)
            array_push(args->arr, node_to_xs_map(n->call.args.items[j]));
        map_set(m->map, "args", args);
        break;
    }
    case NODE_IF:
        map_set(m->map, "cond", node_to_xs_map(n->if_expr.cond));
        map_set(m->map, "then", node_to_xs_map(n->if_expr.then));
        if (n->if_expr.else_branch)
            map_set(m->map, "else", node_to_xs_map(n->if_expr.else_branch));
        break;
    case NODE_BLOCK: {
        Value *stmts = xs_array_new();
        for (int j = 0; j < n->block.stmts.len; j++)
            array_push(stmts->arr, node_to_xs_map(n->block.stmts.items[j]));
        map_set(m->map, "stmts", stmts);
        if (n->block.expr)
            map_set(m->map, "expr", node_to_xs_map(n->block.expr));
        break;
    }
    case NODE_LET: case NODE_VAR:
        map_set(m->map, "name", xs_str(n->let.name ? n->let.name : ""));
        if (n->let.value) map_set(m->map, "value", node_to_xs_map(n->let.value));
        break;
    case NODE_RETURN:
        if (n->ret.value) map_set(m->map, "value", node_to_xs_map(n->ret.value));
        break;
    case NODE_ASSIGN:
        map_set(m->map, "target", node_to_xs_map(n->assign.target));
        map_set(m->map, "value", node_to_xs_map(n->assign.value));
        break;
    case NODE_FOR:
        map_set(m->map, "pattern", node_to_xs_map(n->for_loop.pattern));
        map_set(m->map, "iter", node_to_xs_map(n->for_loop.iter));
        map_set(m->map, "body", node_to_xs_map(n->for_loop.body));
        break;
    case NODE_WHILE:
        map_set(m->map, "cond", node_to_xs_map(n->while_loop.cond));
        map_set(m->map, "body", node_to_xs_map(n->while_loop.body));
        break;
    default:
        break;
    }
    return m;
}

/* Convert an XS map back to a C Node* */
static Node *node_from_xs_map(Value *map) {
    if (!map || map->tag != XS_MAP) return NULL;
    Value *tag_v = map_get(map->map, "tag");
    if (!tag_v || tag_v->tag != XS_STR) return NULL;
    const char *tag_s = tag_v->s;
    int tag_i = node_tag_from_string(tag_s);
    Span sp = span_zero();

    if (tag_i == NODE_LIT_INT) {
        Node *n = node_new(NODE_LIT_INT, sp);
        Value *v = map_get(map->map, "value");
        n->lit_int.ival = (v && v->tag == XS_INT) ? v->i : 0;
        return n;
    }
    if (tag_i == NODE_LIT_FLOAT) {
        Node *n = node_new(NODE_LIT_FLOAT, sp);
        Value *v = map_get(map->map, "value");
        n->lit_float.fval = (v && v->tag == XS_FLOAT) ? v->f : 0.0;
        return n;
    }
    if (tag_i == NODE_LIT_STRING) {
        Node *n = node_new(NODE_LIT_STRING, sp);
        Value *v = map_get(map->map, "value");
        n->lit_string.sval = xs_strdup((v && v->tag == XS_STR) ? v->s : "");
        n->lit_string.interpolated = 0;
        n->lit_string.parts = nodelist_new();
        return n;
    }
    if (tag_i == NODE_LIT_BOOL) {
        Node *n = node_new(NODE_LIT_BOOL, sp);
        Value *v = map_get(map->map, "value");
        n->lit_bool.bval = (v && value_truthy(v)) ? 1 : 0;
        return n;
    }
    if (tag_i == NODE_LIT_NULL) {
        return node_new(NODE_LIT_NULL, sp);
    }
    if (tag_i == NODE_IDENT) {
        Node *n = node_new(NODE_IDENT, sp);
        Value *v = map_get(map->map, "name");
        n->ident.name = xs_strdup((v && v->tag == XS_STR) ? v->s : "");
        return n;
    }
    if (tag_i == NODE_BINOP) {
        Node *n = node_new(NODE_BINOP, sp);
        Value *op = map_get(map->map, "op");
        strncpy(n->binop.op, (op && op->tag == XS_STR) ? op->s : "+", sizeof(n->binop.op)-1);
        Value *l = map_get(map->map, "left");
        Value *r = map_get(map->map, "right");
        n->binop.left = node_from_xs_map(l);
        n->binop.right = node_from_xs_map(r);
        if (!n->binop.left) n->binop.left = node_new(NODE_LIT_NULL, sp);
        if (!n->binop.right) n->binop.right = node_new(NODE_LIT_NULL, sp);
        return n;
    }
    if (tag_i == NODE_UNARY) {
        Node *n = node_new(NODE_UNARY, sp);
        Value *op = map_get(map->map, "op");
        const char *ops = (op && op->tag == XS_STR) ? op->s : "-";
        if (strcmp(ops, "not") == 0) ops = "!";
        strncpy(n->unary.op, ops, sizeof(n->unary.op)-1);
        Value *e = map_get(map->map, "expr");
        n->unary.expr = node_from_xs_map(e);
        if (!n->unary.expr) n->unary.expr = node_new(NODE_LIT_NULL, sp);
        n->unary.prefix = 1;
        return n;
    }
    if (tag_i == NODE_CALL) {
        Node *n = node_new(NODE_CALL, sp);
        Value *callee = map_get(map->map, "callee");
        n->call.callee = node_from_xs_map(callee);
        if (!n->call.callee) n->call.callee = node_new(NODE_LIT_NULL, sp);
        n->call.args = nodelist_new();
        n->call.kwargs = nodepairlist_new();
        Value *args = map_get(map->map, "args");
        if (args && args->tag == XS_ARRAY) {
            for (int j = 0; j < args->arr->len; j++) {
                Node *an = node_from_xs_map(args->arr->items[j]);
                if (an) nodelist_push(&n->call.args, an);
            }
        }
        return n;
    }
    if (tag_i == NODE_IF) {
        Node *n = node_new(NODE_IF, sp);
        Value *cond = map_get(map->map, "cond");
        Value *then = map_get(map->map, "then");
        Value *els = map_get(map->map, "else");
        n->if_expr.cond = node_from_xs_map(cond);
        n->if_expr.then = node_from_xs_map(then);
        n->if_expr.elif_conds = nodelist_new();
        n->if_expr.elif_thens = nodelist_new();
        n->if_expr.else_branch = els ? node_from_xs_map(els) : NULL;
        if (!n->if_expr.cond) n->if_expr.cond = node_new(NODE_LIT_NULL, sp);
        if (!n->if_expr.then) n->if_expr.then = node_new(NODE_LIT_NULL, sp);
        return n;
    }
    if (tag_i == NODE_BLOCK) {
        Node *n = node_new(NODE_BLOCK, sp);
        n->block.stmts = nodelist_new();
        n->block.expr = NULL;
        n->block.has_decls = -1;
        n->block.is_unsafe = 0;
        Value *stmts = map_get(map->map, "stmts");
        if (stmts && stmts->tag == XS_ARRAY) {
            for (int j = 0; j < stmts->arr->len; j++) {
                Node *sn = node_from_xs_map(stmts->arr->items[j]);
                if (sn) nodelist_push(&n->block.stmts, sn);
            }
        }
        Value *expr = map_get(map->map, "expr");
        if (expr && expr->tag == XS_MAP)
            n->block.expr = node_from_xs_map(expr);
        return n;
    }
    if (tag_i == NODE_LET || tag_i == NODE_VAR) {
        Node *n = node_new((NodeTag)tag_i, sp);
        Value *name = map_get(map->map, "name");
        Value *val = map_get(map->map, "value");
        const char *nm = (name && name->tag == XS_STR) ? name->s : "";
        Node *pat = node_new(NODE_PAT_IDENT, sp);
        pat->pat_ident.name = xs_strdup(nm);
        pat->pat_ident.mutable = (tag_i == NODE_VAR) ? 1 : 0;
        n->let.pattern = pat;
        n->let.name = xs_strdup(nm);
        n->let.value = (val && val->tag == XS_MAP) ? node_from_xs_map(val) : NULL;
        n->let.mutable = (tag_i == NODE_VAR) ? 1 : 0;
        n->let.type_ann = NULL;
        return n;
    }
    if (tag_i == NODE_RETURN) {
        Node *n = node_new(NODE_RETURN, sp);
        Value *val = map_get(map->map, "value");
        n->ret.value = (val && val->tag == XS_MAP) ? node_from_xs_map(val) : NULL;
        return n;
    }
    if (tag_i == NODE_ASSIGN) {
        Node *n = node_new(NODE_ASSIGN, sp);
        Value *tgt = map_get(map->map, "target");
        Value *val = map_get(map->map, "value");
        n->assign.target = node_from_xs_map(tgt);
        n->assign.value = node_from_xs_map(val);
        strncpy(n->assign.op, "=", sizeof(n->assign.op)-1);
        if (!n->assign.target) n->assign.target = node_new(NODE_LIT_NULL, sp);
        if (!n->assign.value) n->assign.value = node_new(NODE_LIT_NULL, sp);
        return n;
    }
    if (tag_i == NODE_FOR) {
        Node *n = node_new(NODE_FOR, sp);
        Value *pat = map_get(map->map, "pattern");
        Value *iter = map_get(map->map, "iter");
        Value *body = map_get(map->map, "body");
        n->for_loop.pattern = node_from_xs_map(pat);
        n->for_loop.iter = node_from_xs_map(iter);
        n->for_loop.body = node_from_xs_map(body);
        n->for_loop.label = NULL;
        if (!n->for_loop.pattern) n->for_loop.pattern = node_new(NODE_LIT_NULL, sp);
        if (!n->for_loop.iter) n->for_loop.iter = node_new(NODE_LIT_NULL, sp);
        if (!n->for_loop.body) n->for_loop.body = node_new(NODE_LIT_NULL, sp);
        return n;
    }
    if (tag_i == NODE_WHILE) {
        Node *n = node_new(NODE_WHILE, sp);
        Value *cond = map_get(map->map, "cond");
        Value *body = map_get(map->map, "body");
        n->while_loop.cond = node_from_xs_map(cond);
        n->while_loop.body = node_from_xs_map(body);
        n->while_loop.label = NULL;
        if (!n->while_loop.cond) n->while_loop.cond = node_new(NODE_LIT_NULL, sp);
        if (!n->while_loop.body) n->while_loop.body = node_new(NODE_LIT_NULL, sp);
        return n;
    }
    if (tag_i == NODE_EXPR_STMT) {
        Value *expr = map_get(map->map, "expr");
        if (expr && expr->tag == XS_MAP) {
            Node *n = node_new(NODE_EXPR_STMT, sp);
            n->expr_stmt.expr = node_from_xs_map(expr);
            n->expr_stmt.has_semicolon = 0;
            return n;
        }
    }

    /* Unsupported tag: wrap as a plugin_eval node that calls the XS value directly.
       We store the XS map itself as a literal that interp_eval can handle. */
    return NULL;
}

/* ── phase 2: eval hook natives ── */

static Value *native_plugin_before_eval(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1) return value_incref(XS_NULL_VAL);

    int tag_filter = -1;
    Value *callback = NULL;

    if (argc >= 2 && args[0] && args[0]->tag == XS_STR &&
        args[1] && (args[1]->tag == XS_FUNC || args[1]->tag == XS_NATIVE)) {
        tag_filter = node_tag_from_string(args[0]->s);
        callback = args[1];
    } else if (args[0] && (args[0]->tag == XS_FUNC || args[0]->tag == XS_NATIVE)) {
        callback = args[0];
    } else {
        return value_incref(XS_NULL_VAL);
    }

    if (g_n_before_eval >= 64) return value_incref(XS_NULL_VAL);
    int idx = g_n_before_eval++;
    g_before_eval[idx].callback = value_incref(callback);
    g_before_eval[idx].tag_filter = tag_filter;
    g_has_eval_hooks = 1;

    Value *handle = xs_map_new();
    map_set(handle->map, "index", xs_int(idx));
    map_set(handle->map, "type", xs_str("before_eval"));
    return handle;
}

static Value *native_plugin_after_eval(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1) return value_incref(XS_NULL_VAL);

    int tag_filter = -1;
    Value *callback = NULL;

    if (argc >= 2 && args[0] && args[0]->tag == XS_STR &&
        args[1] && (args[1]->tag == XS_FUNC || args[1]->tag == XS_NATIVE)) {
        tag_filter = node_tag_from_string(args[0]->s);
        callback = args[1];
    } else if (args[0] && (args[0]->tag == XS_FUNC || args[0]->tag == XS_NATIVE)) {
        callback = args[0];
    } else {
        return value_incref(XS_NULL_VAL);
    }

    if (g_n_after_eval >= 64) return value_incref(XS_NULL_VAL);
    int idx = g_n_after_eval++;
    g_after_eval[idx].callback = value_incref(callback);
    g_after_eval[idx].tag_filter = tag_filter;
    g_has_eval_hooks = 1;

    Value *handle = xs_map_new();
    map_set(handle->map, "index", xs_int(idx));
    map_set(handle->map, "type", xs_str("after_eval"));
    return handle;
}

/* ── phase 2: syntax handler natives ── */

static Value *native_plugin_on_unknown(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1 || !args[0] || (args[0]->tag != XS_FUNC && args[0]->tag != XS_NATIVE))
        return value_incref(XS_NULL_VAL);
    if (g_n_syntax_handlers >= 16) return value_incref(XS_NULL_VAL);
    g_syntax_handlers[g_n_syntax_handlers++] = value_incref(args[0]);

    Value *handle = xs_map_new();
    map_set(handle->map, "type", xs_str("on_unknown"));
    return handle;
}

static Value *native_plugin_on_unknown_expr(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1 || !args[0] || (args[0]->tag != XS_FUNC && args[0]->tag != XS_NATIVE))
        return value_incref(XS_NULL_VAL);
    if (g_n_syntax_expr_handlers >= 16) return value_incref(XS_NULL_VAL);
    g_syntax_expr_handlers[g_n_syntax_expr_handlers++] = value_incref(args[0]);

    Value *handle = xs_map_new();
    map_set(handle->map, "type", xs_str("on_unknown_expr"));
    return handle;
}

static Value *native_plugin_on_postfix(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1 || !args[0] || (args[0]->tag != XS_FUNC && args[0]->tag != XS_NATIVE))
        return value_incref(XS_NULL_VAL);
    if (g_n_postfix_handlers >= 16) return value_incref(XS_NULL_VAL);
    g_postfix_handlers[g_n_postfix_handlers++] = value_incref(args[0]);

    Value *handle = xs_map_new();
    map_set(handle->map, "type", xs_str("on_postfix"));
    return handle;
}

/* ── phase 2: lexer extension natives ── */

static Value *native_plugin_add_keyword(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (argc < 1 || !args[0] || args[0]->tag != XS_STR)
        return value_incref(XS_NULL_VAL);
    if (g_n_plugin_keywords >= MAX_PLUGIN_KEYWORDS) return value_incref(XS_NULL_VAL);
    /* check for duplicates */
    for (int j = 0; j < g_n_plugin_keywords; j++) {
        if (strcmp(g_plugin_keywords[j], args[0]->s) == 0)
            return value_incref(XS_NULL_VAL);
    }
    g_plugin_keywords[g_n_plugin_keywords++] = xs_strdup(args[0]->s);
    return value_incref(XS_NULL_VAL);
}

/* ── phase 2: parser method natives (require active parser) ── */

/* These are declared here but implemented in parser.c through a callback mechanism.
   The parser sets a global pointer, and these natives use it. */

/* Active parser pointer - set/cleared during plugin handler invocation */
static void *g_active_parser = NULL;

static Node *parse_expr_from_parser(void *parser, int min_prec) {
    return parser_parse_expr((Parser *)parser, min_prec);
}
static Node *parse_block_from_parser(void *parser) {
    return parser_parse_block((Parser *)parser);
}
static Token *parser_peek_token(void *parser, int offset) {
    return parser_peek((Parser *)parser, offset);
}
static Token *parser_advance_token(void *parser) {
    return parser_advance((Parser *)parser);
}
static Token *parser_expect_kind(void *parser, int kind, const char *msg) {
    return parser_expect((Parser *)parser, (TokenKind)kind, msg);
}

static Value *native_parser_expr(Interp *interp, Value **args, int argc) {
    (void)args; (void)argc;
    if (!g_active_parser) {
        fprintf(stderr, "xs: error: parser methods can only be called during parsing\n");
        return value_incref(XS_NULL_VAL);
    }
    Node *n = parse_expr_from_parser(g_active_parser, 0);
    Value *result = node_to_xs_map(n);
    return result;
}

static Value *native_parser_block(Interp *interp, Value **args, int argc) {
    (void)args; (void)argc;
    if (!g_active_parser) {
        fprintf(stderr, "xs: error: parser methods can only be called during parsing\n");
        return value_incref(XS_NULL_VAL);
    }
    Node *n = parse_block_from_parser(g_active_parser);
    Value *result = node_to_xs_map(n);
    return result;
}

static Value *native_parser_ident(Interp *interp, Value **args, int argc) {
    (void)args; (void)argc;
    if (!g_active_parser) {
        fprintf(stderr, "xs: error: parser methods can only be called during parsing\n");
        return value_incref(XS_NULL_VAL);
    }
    Token *tok = parser_peek_token(g_active_parser, 0);
    if (tok->kind == TK_IDENT) {
        parser_advance_token(g_active_parser);
        return xs_str(tok->sval ? tok->sval : "");
    }
    return value_incref(XS_NULL_VAL);
}

static Value *native_parser_expect(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (!g_active_parser || argc < 1 || !args[0] || args[0]->tag != XS_STR) {
        fprintf(stderr, "xs: error: parser methods can only be called during parsing\n");
        return value_incref(XS_NULL_VAL);
    }
    const char *kind_str = args[0]->s;
    int kind = -1;
    if (strcmp(kind_str, "{") == 0) kind = TK_LBRACE;
    else if (strcmp(kind_str, "}") == 0) kind = TK_RBRACE;
    else if (strcmp(kind_str, "(") == 0) kind = TK_LPAREN;
    else if (strcmp(kind_str, ")") == 0) kind = TK_RPAREN;
    else if (strcmp(kind_str, "[") == 0) kind = TK_LBRACKET;
    else if (strcmp(kind_str, "]") == 0) kind = TK_RBRACKET;
    else if (strcmp(kind_str, ";") == 0) kind = TK_SEMICOLON;
    else if (strcmp(kind_str, ",") == 0) kind = TK_COMMA;
    else if (strcmp(kind_str, ":") == 0) kind = TK_COLON;
    else if (strcmp(kind_str, "=") == 0) kind = TK_ASSIGN;

    if (kind >= 0) {
        parser_expect_kind(g_active_parser, kind, "expected token");
    }
    return value_incref(XS_NULL_VAL);
}

static Value *native_parser_at(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (!g_active_parser || argc < 1 || !args[0] || args[0]->tag != XS_STR)
        return value_incref(XS_FALSE_VAL);
    Token *tok = parser_peek_token(g_active_parser, 0);
    const char *kind_str = args[0]->s;
    /* check if the current token matches */
    if (strcmp(kind_str, "{") == 0) return xs_bool(tok->kind == TK_LBRACE);
    if (strcmp(kind_str, "}") == 0) return xs_bool(tok->kind == TK_RBRACE);
    if (strcmp(kind_str, "(") == 0) return xs_bool(tok->kind == TK_LPAREN);
    if (strcmp(kind_str, ")") == 0) return xs_bool(tok->kind == TK_RPAREN);
    if (strcmp(kind_str, "IDENT") == 0) return xs_bool(tok->kind == TK_IDENT);
    if (strcmp(kind_str, "EOF") == 0) return xs_bool(tok->kind == TK_EOF);
    /* check against the token value for identifiers/keywords */
    if (tok->kind == TK_IDENT && tok->sval && strcmp(tok->sval, kind_str) == 0)
        return value_incref(XS_TRUE_VAL);
    return value_incref(XS_FALSE_VAL);
}

static Value *native_parser_peek(Interp *interp, Value **args, int argc) {
    (void)interp;
    if (!g_active_parser) return value_incref(XS_NULL_VAL);
    int offset = 0;
    if (argc > 0 && args[0] && args[0]->tag == XS_INT)
        offset = (int)args[0]->i;
    Token *tok = parser_peek_token(g_active_parser, offset);
    Value *m = xs_map_new();
    map_set(m->map, "kind", xs_str(token_kind_name(tok->kind)));
    map_set(m->map, "value", xs_str(tok->sval ? tok->sval : token_kind_name(tok->kind)));
    map_set(m->map, "line", xs_int(tok->span.line));
    map_set(m->map, "col", xs_int(tok->span.col));
    return m;
}

/* ── phase 2: plugin-parser bridge functions (called from parser.c via fn ptrs) ── */

static int plugin_is_keyword_impl(const char *word) {
    if (!word) return 0;
    for (int j = 0; j < g_n_plugin_keywords; j++) {
        if (strcmp(g_plugin_keywords[j], word) == 0)
            return 1;
    }
    return 0;
}

static Node *plugin_try_syntax_handler_impl(Parser *p, Token *tok) {
    if (g_n_syntax_handlers == 0 || !g_plugin_interp) return NULL;

    /* build token map: #{ kind: "...", value: "...", line: N, col: N } */
    Value *token_map = xs_map_new();
    map_set(token_map->map, "kind", xs_str(token_kind_name(tok->kind)));
    map_set(token_map->map, "value", xs_str(tok->sval ? tok->sval : token_kind_name(tok->kind)));
    map_set(token_map->map, "line", xs_int(tok->span.line));
    map_set(token_map->map, "col", xs_int(tok->span.col));

    void *saved_parser = g_active_parser;
    g_active_parser = p;

    Node *result = NULL;
    for (int j = 0; j < g_n_syntax_handlers; j++) {
        Value *handler = g_syntax_handlers[j];
        if (!handler) continue;
        Value *args[1] = { token_map };
        Value *ret = call_value(g_plugin_interp, handler, args, 1, "on_unknown");
        if (ret && ret->tag == XS_MAP) {
            result = node_from_xs_map(ret);
            value_decref(ret);
            if (result) break;
        } else if (ret) {
            value_decref(ret);
        }
        if (g_plugin_interp->cf.signal) break;
    }

    g_active_parser = saved_parser;
    value_decref(token_map);
    return result;
}

static Node *plugin_try_syntax_expr_handler_impl(Parser *p, Token *tok) {
    if (g_n_syntax_expr_handlers == 0 || !g_plugin_interp) return NULL;

    Value *token_map = xs_map_new();
    map_set(token_map->map, "kind", xs_str(token_kind_name(tok->kind)));
    map_set(token_map->map, "value", xs_str(tok->sval ? tok->sval : token_kind_name(tok->kind)));
    map_set(token_map->map, "line", xs_int(tok->span.line));
    map_set(token_map->map, "col", xs_int(tok->span.col));

    void *saved_parser = g_active_parser;
    g_active_parser = p;

    Node *result = NULL;
    for (int j = 0; j < g_n_syntax_expr_handlers; j++) {
        Value *handler = g_syntax_expr_handlers[j];
        if (!handler) continue;
        Value *args[1] = { token_map };
        Value *ret = call_value(g_plugin_interp, handler, args, 1, "on_unknown_expr");
        if (ret && ret->tag == XS_MAP) {
            result = node_from_xs_map(ret);
            value_decref(ret);
            if (result) break;
        } else if (ret) {
            value_decref(ret);
        }
        if (g_plugin_interp->cf.signal) break;
    }

    g_active_parser = saved_parser;
    value_decref(token_map);
    return result;
}

/* ── end phase 2 natives ── */

static void build_plugin_map(Value *plugin_map) {
    /* plugin.lexer */
    Value *lexer_map = xs_map_new();
    map_set(lexer_map->map, "add_keyword", xs_native(native_plugin_add_keyword));
    map_set(plugin_map->map, "lexer", lexer_map);
    value_decref(lexer_map);

    /* plugin.parser */
    Value *parser_map = xs_map_new();
    map_set(parser_map->map, "on_unknown", xs_native(native_plugin_on_unknown));
    map_set(parser_map->map, "on_unknown_expr", xs_native(native_plugin_on_unknown_expr));
    map_set(parser_map->map, "on_postfix", xs_native(native_plugin_on_postfix));
    map_set(parser_map->map, "expr", xs_native(native_parser_expr));
    map_set(parser_map->map, "block", xs_native(native_parser_block));
    map_set(parser_map->map, "ident", xs_native(native_parser_ident));
    map_set(parser_map->map, "expect", xs_native(native_parser_expect));
    map_set(parser_map->map, "at", xs_native(native_parser_at));
    map_set(parser_map->map, "peek", xs_native(native_parser_peek));
    map_set(plugin_map->map, "parser", parser_map);
    value_decref(parser_map);

    /* plugin.hooks — empty map */
    Value *hooks_map = xs_map_new();
    map_set(plugin_map->map, "hooks", hooks_map);
    value_decref(hooks_map);

    /* plugin.runtime */
    Value *runtime_map = xs_map_new();

    /* plugin.runtime.global with .set/.get/.names */
    Value *global_map = xs_map_new();
    map_set(global_map->map, "set", xs_native(native_plugin_global_set));
    map_set(global_map->map, "get", xs_native(native_plugin_global_get));
    map_set(global_map->map, "names", xs_native(native_plugin_global_names));
    map_set(runtime_map->map, "global", global_map);
    value_decref(global_map);

    /* plugin.runtime.add_method */
    map_set(runtime_map->map, "add_method", xs_native(native_plugin_add_method));

    /* plugin.runtime.before_eval / after_eval */
    map_set(runtime_map->map, "before_eval", xs_native(native_plugin_before_eval));
    map_set(runtime_map->map, "after_eval", xs_native(native_plugin_after_eval));

    map_set(plugin_map->map, "runtime", runtime_map);
    value_decref(runtime_map);

    /* plugin.teardown */
    map_set(plugin_map->map, "teardown", xs_native(native_plugin_teardown));

    /* plugin.requires */
    map_set(plugin_map->map, "requires", xs_native(native_plugin_requires));

    /* plugin.ast constructors */
    Value *ast_map = xs_map_new();
    map_set(ast_map->map, "int_node", xs_native(native_ast_int_node));
    map_set(ast_map->map, "float_node", xs_native(native_ast_float_node));
    map_set(ast_map->map, "str_node", xs_native(native_ast_str_node));
    map_set(ast_map->map, "bool_node", xs_native(native_ast_bool_node));
    map_set(ast_map->map, "null_node", xs_native(native_ast_null_node));
    map_set(ast_map->map, "ident", xs_native(native_ast_ident));
    map_set(ast_map->map, "binop", xs_native(native_ast_binop));
    map_set(ast_map->map, "unary", xs_native(native_ast_unary));
    map_set(ast_map->map, "call", xs_native(native_ast_call));
    map_set(ast_map->map, "method_call", xs_native(native_ast_method_call));
    map_set(ast_map->map, "if_expr", xs_native(native_ast_if_expr));
    map_set(ast_map->map, "if_else", xs_native(native_ast_if_else));
    map_set(ast_map->map, "block", xs_native(native_ast_block));
    map_set(ast_map->map, "let_decl", xs_native(native_ast_let_decl));
    map_set(ast_map->map, "var_decl", xs_native(native_ast_var_decl));
    map_set(ast_map->map, "fn_decl", xs_native(native_ast_fn_decl));
    map_set(ast_map->map, "lambda", xs_native(native_ast_lambda));
    map_set(ast_map->map, "return_node", xs_native(native_ast_return_node));
    map_set(ast_map->map, "assign", xs_native(native_ast_assign));
    map_set(ast_map->map, "for_loop", xs_native(native_ast_for_loop));
    map_set(ast_map->map, "while_loop", xs_native(native_ast_while_loop));
    map_set(ast_map->map, "array", xs_native(native_ast_array));
    map_set(ast_map->map, "map", xs_native(native_ast_map_node));
    map_set(plugin_map->map, "ast", ast_map);
    value_decref(ast_map);
}

static void exec_plugin_load(Interp *i, Node *stmt, const char *resolved) {
    const char *use_path = stmt->use_.path;

    FILE *pf = fopen(resolved, "rb");
    if (!pf) {
        char errbuf[256];
        snprintf(errbuf, sizeof(errbuf), "plugin \"%s\" failed to load", use_path);
        xs_runtime_error(stmt->span, errbuf, NULL,
            "could not open '%s'", resolved);
        i->cf.signal = CF_PANIC;
        i->cf.value = xs_str("plugin load error");
        return;
    }
    fseek(pf, 0, SEEK_END);
    long psz = ftell(pf);
    fseek(pf, 0, SEEK_SET);
    char *psrc = xs_malloc((size_t)(psz + 1));
    if (fread(psrc, 1, (size_t)psz, pf) != (size_t)psz) {
        free(psrc); fclose(pf);
        i->cf.signal = CF_PANIC;
        i->cf.value = xs_str("plugin read error");
        return;
    }
    psrc[psz] = '\0';
    fclose(pf);

    char *pfpath = xs_strdup(resolved);
    Lexer plex;
    lexer_init(&plex, psrc, pfpath);
    TokenArray pta = lexer_tokenize(&plex);
    Parser pp;
    parser_init(&pp, &pta, pfpath);
    Node *pprog = parser_parse(&pp);
    token_array_free(&pta);
    if (!pprog || pp.had_error) {
        char errbuf[256];
        snprintf(errbuf, sizeof(errbuf), "plugin \"%s\" failed to load", use_path);
        xs_runtime_error(stmt->span, errbuf, NULL, "parse error in '%s'", resolved);
        free(psrc); free(pfpath);
        if (pprog) node_free(pprog);
        i->cf.signal = CF_PANIC;
        i->cf.value = xs_str("plugin parse error");
        return;
    }

    /* save state for rollback on error */
    int saved_method_count = g_plugin_method_count;
    int saved_teardown_count = g_teardown_count;
    int saved_xs_plugin_count = g_xs_plugin_count;
    int saved_before_eval = g_n_before_eval;
    int saved_after_eval = g_n_after_eval;
    int saved_syntax_handlers = g_n_syntax_handlers;
    int saved_syntax_expr_handlers = g_n_syntax_expr_handlers;
    int saved_postfix_handlers = g_n_postfix_handlers;
    int saved_plugin_keywords = g_n_plugin_keywords;

    /* set the host globals pointer for native functions */
    s_plugin_host_globals = i->globals;
    g_plugin_interp = i;

    /* build the plugin map */
    Value *plugin_map = xs_map_new();
    build_plugin_map(plugin_map);

    /* create temp interpreter and inject plugin */
    Interp *tmp = interp_new(pfpath);
    env_define(tmp->globals, "plugin", value_incref(plugin_map), 1);

    /* save and restore the main interp pointer */
    Interp *saved_interp = g_current_interp;
    Interp *saved_plugin_interp = g_plugin_interp;

    /* run the plugin file in the temp interpreter */
    interp_run(tmp, pprog);

    /* restore main interp */
    g_current_interp = saved_interp;
    g_plugin_interp = saved_plugin_interp;

    int had_error = (tmp->cf.signal == CF_ERROR || tmp->cf.signal == CF_PANIC ||
                     tmp->cf.signal == CF_THROW);

    if (had_error) {
        /* rollback: undo any methods/teardowns/plugins registered */
        for (int j = saved_method_count; j < g_plugin_method_count; j++) {
            free(g_plugin_methods[j].type_name);
            free(g_plugin_methods[j].method_name);
            value_decref(g_plugin_methods[j].fn);
        }
        g_plugin_method_count = saved_method_count;

        for (int j = saved_teardown_count; j < g_teardown_count; j++)
            value_decref(g_teardown_fns[j]);
        g_teardown_count = saved_teardown_count;

        for (int j = saved_xs_plugin_count; j < g_xs_plugin_count; j++) {
            free(g_xs_plugins[j].name);
            free(g_xs_plugins[j].version);
        }
        g_xs_plugin_count = saved_xs_plugin_count;

        /* rollback phase 2 hooks */
        for (int j = saved_before_eval; j < g_n_before_eval; j++)
            value_decref(g_before_eval[j].callback);
        g_n_before_eval = saved_before_eval;
        for (int j = saved_after_eval; j < g_n_after_eval; j++)
            value_decref(g_after_eval[j].callback);
        g_n_after_eval = saved_after_eval;
        for (int j = saved_syntax_handlers; j < g_n_syntax_handlers; j++)
            value_decref(g_syntax_handlers[j]);
        g_n_syntax_handlers = saved_syntax_handlers;
        for (int j = saved_syntax_expr_handlers; j < g_n_syntax_expr_handlers; j++)
            value_decref(g_syntax_expr_handlers[j]);
        g_n_syntax_expr_handlers = saved_syntax_expr_handlers;
        for (int j = saved_postfix_handlers; j < g_n_postfix_handlers; j++)
            value_decref(g_postfix_handlers[j]);
        g_n_postfix_handlers = saved_postfix_handlers;
        for (int j = saved_plugin_keywords; j < g_n_plugin_keywords; j++)
            free(g_plugin_keywords[j]);
        g_n_plugin_keywords = saved_plugin_keywords;
        g_has_eval_hooks = (g_n_before_eval > 0 || g_n_after_eval > 0);

        char errbuf[256];
        snprintf(errbuf, sizeof(errbuf), "plugin \"%s\" failed to load", use_path);
        xs_runtime_error(stmt->span, errbuf, NULL,
            "error while executing plugin '%s'", resolved);
        i->cf.signal = CF_PANIC;
        i->cf.value = xs_str("plugin execution error");
    } else {
        /* success: read plugin.meta to register the loaded plugin */
        const char *pname = use_path;
        const char *pver = NULL;
        Value *pval = env_get(tmp->globals, "plugin");
        if (pval && pval->tag == XS_MAP) {
            Value *meta = map_get(pval->map, "meta");
            if (meta && meta->tag == XS_MAP) {
                Value *name_v = map_get(meta->map, "name");
                Value *ver_v = map_get(meta->map, "version");
                if (name_v && name_v->tag == XS_STR) pname = name_v->s;
                if (ver_v && ver_v->tag == XS_STR) pver = ver_v->s;
            }
        }
        plugin_register_loaded(pname, pver);
        /* flag re-parse if syntax handlers were newly registered */
        if (g_n_syntax_handlers > saved_syntax_handlers ||
            g_n_plugin_keywords > saved_plugin_keywords)
            i->needs_reparse = 1;
        /* store source, filepath, and AST so closures remain valid */
        int idx = g_xs_plugin_count - 1;
        if (idx >= 0) {
            g_xs_plugins[idx].source = psrc;
            g_xs_plugins[idx].filepath = pfpath;
            g_xs_plugins[idx].program = pprog;
            psrc = NULL;
            pfpath = NULL;
            pprog = NULL;
        }
    }

    value_decref(plugin_map);
    interp_free(tmp);
    /* if error, free psrc/pprog/pfpath; if success, they were moved into g_xs_plugins */
    free(psrc);
    free(pfpath);
    if (pprog) node_free(pprog);
}

/* ── end plugin system ── */

void interp_exec(Interp *i, Node *stmt) {
    if (!stmt || i->cf.signal) return;
    i->current_span = stmt->span;
    if (i->coverage && stmt->span.line > 0)
        coverage_record_line(i->coverage, stmt->span.line);

    switch (stmt->tag) {
    case NODE_EXPR_STMT: {
        Value *v = EVAL(i, stmt->expr_stmt.expr);
        value_decref(v);
        break;
    }

    case NODE_LET:
    case NODE_VAR: {
        Value *val = stmt->let.value ? EVAL(i, stmt->let.value) : value_incref(XS_NULL_VAL);
        if (stmt->let.type_ann && stmt->let.type_ann->name) {
            if (!value_matches_type(val, stmt->let.type_ann->name)) {
                xs_runtime_error(stmt->span, "type mismatch", NULL,
                    "expected '%s', got '%s'",
                    stmt->let.type_ann->name, value_type_str(val));
                i->cf.signal = CF_PANIC;
                i->cf.value = xs_str("type error");
            }
        }
        int mutable = (stmt->tag == NODE_VAR) || stmt->let.mutable;
        if (stmt->let.name) {
            env_define(i->env, stmt->let.name, val, mutable);
        } else if (stmt->let.pattern) {
            bind_pattern(i, stmt->let.pattern, val, i->env, mutable);
        }
        value_decref(val);
        break;
    }

    case NODE_CONST: {
        Value *val = EVAL(i, stmt->const_.value);
        if (stmt->const_.type_ann && stmt->const_.type_ann->name) {
            if (!value_matches_type(val, stmt->const_.type_ann->name)) {
                xs_runtime_error(stmt->span, "type mismatch", NULL,
                    "expected '%s', got '%s'",
                    stmt->const_.type_ann->name, value_type_str(val));
                i->cf.signal = CF_PANIC;
                i->cf.value = xs_str("type error");
            }
        }
        env_define(i->env, stmt->const_.name, val, 0);
        value_decref(val);
        break;
    }

    case NODE_FN_DECL: {
        if (!stmt->fn_decl.body) break;
        int nparams = stmt->fn_decl.params.len;
        Node **params = nparams ? xs_malloc(nparams * sizeof(Node*)) : NULL;
        Node **defaults = nparams ? xs_calloc(nparams, sizeof(Node*)) : NULL;
        int  *varflags  = nparams ? xs_calloc(nparams, sizeof(int)) : NULL;
        for (int j = 0; j < nparams; j++) {
            Param *pm = &stmt->fn_decl.params.items[j];
            if (pm->pattern) {
                params[j] = pm->pattern;
            } else {
                Node *pn = node_new(NODE_PAT_IDENT, pm->span);
                pn->pat_ident.name    = xs_strdup(pm->name ? pm->name : "_");
                pn->pat_ident.mutable = 0;
                params[j] = pn;
            }
            defaults[j]  = pm->default_val;
            varflags[j]   = pm->variadic;
        }
        XSFunc *fn = func_new_ex(stmt->fn_decl.name, params, nparams,
                               stmt->fn_decl.body, i->env, defaults, varflags);
        fn->is_generator = stmt->fn_decl.is_generator;
        fn->is_async     = stmt->fn_decl.is_async;
        if (stmt->fn_decl.deprecated_msg)
            fn->deprecated_msg = xs_strdup(stmt->fn_decl.deprecated_msg);
        if (nparams > 0) {
            fn->param_type_names = xs_calloc(nparams, sizeof(char*));
            for (int j = 0; j < nparams; j++) {
                Param *pm = &stmt->fn_decl.params.items[j];
                if (pm->type_ann && pm->type_ann->name)
                    fn->param_type_names[j] = xs_strdup(pm->type_ann->name);
            }
        }
        if (stmt->fn_decl.ret_type && stmt->fn_decl.ret_type->name)
            fn->ret_type_name = xs_strdup(stmt->fn_decl.ret_type->name);
        Value *v = xs_func_new(fn);
        if (stmt->fn_decl.name)
            env_define(i->env, stmt->fn_decl.name, v, 1);
        value_decref(v);
        break;
    }

    case NODE_CLASS_DECL: {
        XSClass *cls = xs_calloc(1, sizeof(XSClass));
        cls->name           = xs_strdup(stmt->class_decl.name);
        cls->fields         = map_new();
        cls->methods        = map_new();
        cls->static_methods = map_new();
        cls->refcount       = 1;

        int nbases = stmt->class_decl.nbases;
        cls->nbases = nbases;
        cls->bases  = nbases ? xs_malloc(nbases * sizeof(XSClass*)) : NULL;
        for (int j = 0; j < nbases; j++) {
            Value *base_val = env_get(i->env, stmt->class_decl.bases[j]);
            if (base_val && base_val->tag == XS_CLASS_VAL) {
                cls->bases[j] = base_val->cls;
            } else {
                const char *base_sugg = find_similar_name(i->env, stmt->class_decl.bases[j]);
                if (base_sugg)
                    fprintf(stderr, "xs: error at %s:%d:%d: base class '%s' not found for class '%s' (did you mean '%s'?)\n",
                            stmt->span.file ? stmt->span.file : "<unknown>",
                            stmt->span.line, stmt->span.col,
                            stmt->class_decl.bases[j], stmt->class_decl.name, base_sugg);
                else
                    fprintf(stderr, "xs: error at %s:%d:%d: base class '%s' not found for class '%s'\n",
                            stmt->span.file ? stmt->span.file : "<unknown>",
                            stmt->span.line, stmt->span.col,
                            stmt->class_decl.bases[j], stmt->class_decl.name);
                cls->bases[j] = NULL;
            }
        }

        for (int j = 0; j < nbases; j++) {
            XSClass *base = cls->bases[j];
            if (!base) continue;
            if (base->fields) {
                int nk = 0; char **ks = map_keys(base->fields, &nk);
                for (int k = 0; k < nk; k++) {
                    Value *fv = map_get(base->fields, ks[k]);
                    if (fv) map_set(cls->fields, ks[k], fv);
                    free(ks[k]);
                }
                free(ks);
            }
            if (base->methods) {
                int nk = 0; char **ks = map_keys(base->methods, &nk);
                for (int k = 0; k < nk; k++) {
                    Value *mv = map_get(base->methods, ks[k]);
                    if (mv) map_set(cls->methods, ks[k], mv);
                    free(ks[k]);
                }
                free(ks);
            }
            if (base->static_methods) {
                int nk = 0; char **ks = map_keys(base->static_methods, &nk);
                for (int k = 0; k < nk; k++) {
                    Value *mv = map_get(base->static_methods, ks[k]);
                    if (mv) map_set(cls->static_methods, ks[k], mv);
                    free(ks[k]);
                }
                free(ks);
            }
        }

        for (int j = 0; j < stmt->class_decl.members.len; j++) {
            Node *mem = stmt->class_decl.members.items[j];
            if (mem->tag == NODE_FN_DECL) {
                int np = mem->fn_decl.params.len;
                Node **params = np ? xs_malloc(np*sizeof(Node*)) : NULL;
                for (int k=0;k<np;k++) {
                    Param *pm=&mem->fn_decl.params.items[k];
                    params[k] = pm->pattern ? pm->pattern :
                        ({ Node *pn=node_new(NODE_PAT_IDENT,pm->span);
                           pn->pat_ident.name=xs_strdup(pm->name?pm->name:"_");
                           pn->pat_ident.mutable=0; pn; });
                }
                XSFunc *fn = func_new(mem->fn_decl.name, params, np,
                                       mem->fn_decl.body, i->env);
                Value *fv = xs_func_new(fn);
                if (mem->fn_decl.name) {
                    if (mem->fn_decl.is_static)
                        map_set(cls->static_methods, mem->fn_decl.name, fv);
                    else
                        map_set(cls->methods, mem->fn_decl.name, fv);
                }
                else value_decref(fv);
            } else if (mem->tag == NODE_LET || mem->tag == NODE_VAR) {
                Value *def = mem->let.value ? EVAL(i, mem->let.value) : value_incref(XS_NULL_VAL);
                if (mem->let.name) map_set(cls->fields, mem->let.name, def);
                else value_decref(def);
            }
        }

        Value *cls_val = xs_calloc(1, sizeof(Value));
        cls_val->tag = XS_CLASS_VAL; cls_val->refcount = 1;
        cls_val->cls = cls;
        env_define(i->env, stmt->class_decl.name, cls_val, 1);
        value_decref(cls_val);
        break;
    }

    case NODE_ACTOR_DECL: {
        XSActor *actor = xs_calloc(1, sizeof(XSActor));
        actor->name     = xs_strdup(stmt->actor_decl.name);
        actor->state    = map_new();
        actor->methods  = map_new();
        actor->refcount = 1;

        /* Evaluate default state field values */
        for (int j = 0; j < stmt->actor_decl.state_fields.len; j++) {
            char *fname = stmt->actor_decl.state_fields.items[j].key;
            Node *def   = stmt->actor_decl.state_fields.items[j].val;
            Value *dv   = def ? EVAL(i, def) : value_incref(XS_NULL_VAL);
            map_set(actor->state, fname, dv);
            value_decref(dv);
        }

        /* Process methods — find handle and other methods */
        for (int j = 0; j < stmt->actor_decl.methods.len; j++) {
            Node *m = stmt->actor_decl.methods.items[j];
            if (m->tag != NODE_FN_DECL) continue;
            int np = m->fn_decl.params.len;
            Node **params = np ? xs_malloc(np * sizeof(Node*)) : NULL;
            for (int k = 0; k < np; k++) {
                Param *pm = &m->fn_decl.params.items[k];
                if (pm->pattern) {
                    params[k] = pm->pattern;
                } else {
                    Node *pn = node_new(NODE_PAT_IDENT, pm->span);
                    pn->pat_ident.name    = xs_strdup(pm->name ? pm->name : "_");
                    pn->pat_ident.mutable = 0;
                    params[k] = pn;
                }
            }
            XSFunc *fn = func_new(m->fn_decl.name, params, np, m->fn_decl.body, i->env);
            if (m->fn_decl.name && strcmp(m->fn_decl.name, "handle") == 0) {
                actor->handle_fn = fn;
                fn->refcount++;
            }
            /* Store all methods (including handle) in methods map */
            if (m->fn_decl.name) {
                Value *fv = xs_func_new(fn);
                map_set(actor->methods, m->fn_decl.name, fv);
                value_decref(fv);
            }
        }

        actor->closure = env_incref(i->env);

        Value *actor_val = xs_calloc(1, sizeof(Value));
        actor_val->tag      = XS_ACTOR;
        actor_val->refcount = 1;
        actor_val->actor    = actor;
        env_define(i->env, stmt->actor_decl.name, actor_val, 1);
        value_decref(actor_val);
        break;
    }

    case NODE_STRUCT_DECL: {
        XSClass *cls = xs_calloc(1, sizeof(XSClass));
        cls->name           = xs_strdup(stmt->struct_decl.name);
        cls->fields         = map_new();
        cls->methods        = map_new();
        cls->static_methods = map_new();
        cls->refcount       = 1;
        for (int j=0;j<stmt->struct_decl.fields.len;j++) {
            char *fn2 = stmt->struct_decl.fields.items[j].key;
            Value *def = stmt->struct_decl.fields.items[j].val ?
                         EVAL(i, stmt->struct_decl.fields.items[j].val) :
                         value_incref(XS_NULL_VAL);
            if (fn2) map_set(cls->fields, fn2, def);
            else value_decref(def);
        }
        for (int j = 0; j < stmt->struct_decl.n_derives; j++) {
            const char *tname = stmt->struct_decl.derives[j];
            int idx = cls->ntraits++;
            cls->traits = xs_realloc(cls->traits, sizeof(char*) * cls->ntraits);
            cls->traits[idx] = xs_strdup(tname);
            if (strcmp(tname, "Debug") == 0) {
                Value *dbg_fn = xs_native(builtin_debug_to_string);
                map_set(cls->methods, "to_string", dbg_fn);
                map_set(cls->methods, "debug", value_incref(dbg_fn));
            } else if (strcmp(tname, "Clone") == 0) {
                Value *clone_fn = xs_native(builtin_clone);
                map_set(cls->methods, "clone", clone_fn);
            } else if (strcmp(tname, "PartialEq") == 0 || strcmp(tname, "Eq") == 0) {
                Value *eq_fn = xs_native(builtin_struct_eq);
                map_set(cls->methods, "eq", eq_fn);
            }
        }
        Value *cls_val = xs_calloc(1, sizeof(Value));
        cls_val->tag = XS_CLASS_VAL; cls_val->refcount = 1;
        cls_val->cls = cls;
        env_define(i->env, stmt->struct_decl.name, cls_val, 1);
        value_decref(cls_val);
        break;
    }

    case NODE_ENUM_DECL: {
        XSMap *enum_map = map_new();

        for (int j = 0; j < stmt->enum_decl.variants.len; j++) {
            EnumVariant *v = &stmt->enum_decl.variants.items[j];
            if (v->fields.len == 0) {
                Value *vv = xs_str(v->name);
                XSEnum *en = xs_calloc(1, sizeof(XSEnum));
                en->type_name = xs_strdup(stmt->enum_decl.name);
                en->variant   = xs_strdup(v->name);
                en->refcount  = 1;
                Value *ev = xs_calloc(1, sizeof(Value));
                ev->tag = XS_ENUM_VAL; ev->refcount = 1; ev->en = en;
                map_set(enum_map, v->name, ev);
                value_decref(vv);
            } else {
                Value *ctor = make_enum_ctor_native(stmt->enum_decl.name, v->name);
                map_set(enum_map, v->name, ctor);
            }
        }

        Value *mod = xs_module(enum_map);
        env_define(i->env, stmt->enum_decl.name, mod, 1);
        value_decref(mod);
        break;
    }

    case NODE_IMPL_DECL: {
        Value *cls_val = env_get(i->env, stmt->impl_decl.type_name);
        XSClass *cls = NULL;
        if (cls_val && cls_val->tag == XS_CLASS_VAL) {
            cls = cls_val->cls;
        }
        if (cls && stmt->impl_decl.trait_name) {
            int idx = cls->ntraits++;
            cls->traits = xs_realloc(cls->traits, sizeof(char*) * cls->ntraits);
            cls->traits[idx] = xs_strdup(stmt->impl_decl.trait_name);
            Value *trait_val = env_get(i->env, stmt->impl_decl.trait_name);
            if (trait_val && trait_val->tag == XS_MAP) {
                Value *defaults = map_get(trait_val->map, "__defaults__");
                if (defaults && defaults->tag == XS_MAP) {
                    int nk = 0; char **ks = map_keys(defaults->map, &nk);
                    for (int j = 0; j < nk; j++) {
                        int found = 0;
                        for (int m = 0; m < stmt->impl_decl.members.len; m++) {
                            Node *mem = stmt->impl_decl.members.items[m];
                            if (mem->tag == NODE_FN_DECL && mem->fn_decl.name &&
                                strcmp(mem->fn_decl.name, ks[j]) == 0) { found = 1; break; }
                        }
                        if (!found) {
                            Value *dfn = map_get(defaults->map, ks[j]);
                            if (dfn && dfn->tag == XS_FUNC) {
                                map_set(cls->methods, ks[j], value_incref(dfn));
                            }
                        }
                        free(ks[j]);
                    }
                    free(ks);
                }
            }
        }
        XSMap *enum_impl = NULL;
        if (cls_val && (cls_val->tag == XS_MODULE || cls_val->tag == XS_MAP)) {
            Value *impl_val = map_get(cls_val->map, "__impl__");
            if (!impl_val) {
                Value *new_impl = xs_map_new();
                map_set(cls_val->map, "__impl__", new_impl);
                enum_impl = new_impl->map;
                value_decref(new_impl);
            } else {
                enum_impl = impl_val->map;
            }
        }
        for (int j = 0; j < stmt->impl_decl.members.len; j++) {
            Node *mem = stmt->impl_decl.members.items[j];
            if (mem->tag == NODE_FN_DECL && mem->fn_decl.body) {
                int np = mem->fn_decl.params.len;
                Node **params = np ? xs_malloc(np*sizeof(Node*)) : NULL;
                for (int k=0;k<np;k++) {
                    Param *pm=&mem->fn_decl.params.items[k];
                    params[k] = pm->pattern ? pm->pattern :
                        ({ Node *pn=node_new(NODE_PAT_IDENT,pm->span);
                           pn->pat_ident.name=xs_strdup(pm->name?pm->name:"_");
                           pn->pat_ident.mutable=0; pn; });
                }
                XSFunc *fn = func_new(mem->fn_decl.name, params, np,
                                       mem->fn_decl.body, i->env);
                Value *fv = xs_func_new(fn);
                if (cls && mem->fn_decl.name)
                    map_set(cls->methods, mem->fn_decl.name, value_incref(fv));
                if (enum_impl && mem->fn_decl.name)
                    map_set(enum_impl, mem->fn_decl.name, value_incref(fv));
                if (mem->fn_decl.name)
                    env_define(i->env, mem->fn_decl.name, fv, 1);
                value_decref(fv);
            }
        }
        break;
    }

    case NODE_IMPORT: {
        if (stmt->import.nparts == 0) break;
        char *modname = stmt->import.path[0];
        Value *mod = env_get(i->env, modname);
        if (!mod) {
            mod = try_load_xs_module(i, modname);
            if (mod) {
                env_define(i->globals, modname, mod, 1);
                value_decref(mod);
                mod = env_get(i->env, modname);
            }
        }
        if (mod) {
            if (stmt->import.nitems > 0) {
                for (int j=0;j<stmt->import.nitems;j++) {
                    Value *item = NULL;
                    if (mod->tag==XS_MODULE||mod->tag==XS_MAP)
                        item = map_get(mod->map, stmt->import.items[j]);
                    if (item) env_define(i->env, stmt->import.items[j], item, 1);
                }
            } else if (stmt->import.alias) {
                env_define(i->env, stmt->import.alias, mod, 1);
            } else {
                env_define(i->env, modname, mod, 1);
            }
        }
        break;
    }

    case NODE_USE: {
        char resolved[2048];
        const char *use_path = stmt->use_.path;
        struct stat st2;

        /* resolve relative to current file's directory */
        if (use_path[0] != '/') {
            const char *fn = i->filename ? i->filename : "";
            const char *last_slash = strrchr(fn, '/');
            if (last_slash) {
                int dirlen = (int)(last_slash - fn);
                snprintf(resolved, sizeof(resolved), "%.*s/%s", dirlen, fn, use_path);
            } else {
                snprintf(resolved, sizeof(resolved), "%s", use_path);
            }
        } else {
            snprintf(resolved, sizeof(resolved), "%s", use_path);
        }

        if (stmt->use_.is_plugin) {
            exec_plugin_load(i, stmt, resolved);
            break;
        }

        /* directory import: look for mod.xs or index.xs inside */
        size_t rlen = strlen(resolved);
        if (rlen > 0 && resolved[rlen - 1] == '/') {
            char dir_try[2048];
            snprintf(dir_try, sizeof(dir_try), "%smod.xs", resolved);
            if (stat(dir_try, &st2) == 0) {
                snprintf(resolved, sizeof(resolved), "%s", dir_try);
            } else {
                snprintf(dir_try, sizeof(dir_try), "%sindex.xs", resolved);
                if (stat(dir_try, &st2) == 0)
                    snprintf(resolved, sizeof(resolved), "%s", dir_try);
            }
        } else if (stat(resolved, &st2) == 0 && S_ISDIR(st2.st_mode)) {
            char dir_try[2048];
            snprintf(dir_try, sizeof(dir_try), "%s/mod.xs", resolved);
            if (stat(dir_try, &st2) == 0) {
                snprintf(resolved, sizeof(resolved), "%s", dir_try);
            } else {
                snprintf(dir_try, sizeof(dir_try), "%s/index.xs", resolved);
                if (stat(dir_try, &st2) == 0)
                    snprintf(resolved, sizeof(resolved), "%s", dir_try);
            }
        }

        Value *mod = load_xs_module_file(i, resolved);
        if (!mod) {
            xs_runtime_error(stmt->span, "failed to load module", NULL,
                "could not load '%s'", resolved);
            i->cf.signal = CF_PANIC;
            i->cf.value = xs_str("module load error");
            break;
        }

        if (stmt->use_.import_all) {
            env_define(i->env, stmt->use_.alias, mod, 1);
        } else {
            for (int j = 0; j < stmt->use_.nnames; j++) {
                Value *item = NULL;
                if (mod->tag == XS_MODULE || mod->tag == XS_MAP)
                    item = map_get(mod->map, stmt->use_.names[j]);
                if (item)
                    env_define(i->env, stmt->use_.name_aliases[j], item, 1);
            }
        }
        value_decref(mod);
        break;
    }

    case NODE_MODULE_DECL: {
        Env *saved = i->env;
        push_env(i);
        for (int j=0;j<stmt->module_decl.body.len;j++) {
            interp_exec(i, stmt->module_decl.body.items[j]);
            if (i->cf.signal) break;
        }
        XSMap *m = map_new();
        for (int j=0;j<i->env->len;j++)
            map_set(m, i->env->bindings[j].name, value_incref(i->env->bindings[j].value));
        pop_env(i);
        i->env = saved;
        Value *mod = xs_module(m);
        env_define(i->env, stmt->module_decl.name, mod, 1);
        value_decref(mod);
        break;
    }

    case NODE_TYPE_ALIAS: break;
    case NODE_TRAIT_DECL: {
        Value *trait_map = xs_map_new();
        Value *methods_arr = xs_array_new();
        for (int j = 0; j < stmt->trait_decl.n_methods; j++) {
            array_push(methods_arr->arr, xs_str(stmt->trait_decl.method_names[j]));
        }
        map_set(trait_map->map, "__methods__", methods_arr);
        Value *defaults_map = xs_map_new();
        for (int j = 0; j < stmt->trait_decl.methods.len; j++) {
            Node *meth = stmt->trait_decl.methods.items[j];
            if (meth->tag == NODE_FN_DECL && meth->fn_decl.body) {
                int np = meth->fn_decl.params.len;
                Node **params = np ? xs_malloc(np*sizeof(Node*)) : NULL;
                for (int k = 0; k < np; k++) {
                    Param *pm = &meth->fn_decl.params.items[k];
                    params[k] = pm->pattern ? pm->pattern :
                        ({ Node *pn = node_new(NODE_PAT_IDENT, pm->span);
                           pn->pat_ident.name = xs_strdup(pm->name ? pm->name : "_");
                           pn->pat_ident.mutable = 0; pn; });
                }
                XSFunc *fn = func_new(meth->fn_decl.name, params, np,
                                       meth->fn_decl.body, i->env);
                Value *fv = xs_func_new(fn);
                map_set(defaults_map->map, meth->fn_decl.name, fv);
            }
        }
        map_set(trait_map->map, "__defaults__", defaults_map);
        if (stmt->trait_decl.super_trait) {
            map_set(trait_map->map, "__super__", xs_str(stmt->trait_decl.super_trait));
        }
        if (stmt->trait_decl.n_assoc_types > 0) {
            Value *assoc = xs_array_new();
            for (int j = 0; j < stmt->trait_decl.n_assoc_types; j++)
                array_push(assoc->arr, xs_str(stmt->trait_decl.assoc_types[j]));
            map_set(trait_map->map, "__assoc_types__", assoc);
        }
        map_set(trait_map->map, "__name__", xs_str(stmt->trait_decl.name));
        env_define(i->env, stmt->trait_decl.name, trait_map, 1);
        value_decref(trait_map);
        break;
    }
    case NODE_EFFECT_DECL: break;

    case NODE_HANDLE:
    case NODE_PERFORM:
    case NODE_RESUME: {
        Value *v = interp_eval(i, stmt);
        value_decref(v);
        break;
    }

    case NODE_RETURN: {
        if (stmt->ret.value && stmt->ret.value->tag == NODE_CALL) {
            Node *cn = stmt->ret.value;
            Value *callee = EVAL(i, cn->call.callee);
            if (i->cf.signal) { value_decref(callee); break; }
            if (callee->tag == XS_FUNC) {
                int argc = cn->call.args.len;
                Value **args = argc ? xs_malloc(argc * sizeof(Value*)) : NULL;
                int ok = 1;
                for (int j = 0; j < argc; j++) {
                    args[j] = EVAL(i, cn->call.args.items[j]);
                    if (i->cf.signal) {
                        for (int k = 0; k <= j; k++) value_decref(args[k]);
                        free(args); value_decref(callee);
                        ok = 0; break;
                    }
                }
                if (ok) {
                    i->tc_callee = callee;
                    i->tc_args   = args;
                    i->tc_argc   = argc;
                    i->cf.signal = CF_TAIL_CALL;
                    if (i->cf.value) value_decref(i->cf.value);
                    i->cf.value  = NULL;
                    break;
                }
                break;
            }
            value_decref(callee);
        }
        Value *val = stmt->ret.value ? EVAL(i, stmt->ret.value) : value_incref(XS_NULL_VAL);
        if (i->cf.value) value_decref(i->cf.value);
        i->cf.signal = CF_RETURN;
        i->cf.value  = val;
        break;
    }

    case NODE_YIELD: {
        /* yield statement (e.g. yield expr;) */
        Value *val = stmt->yield_.value ? EVAL(i, stmt->yield_.value) : value_incref(XS_NULL_VAL);
        if (i->cf.signal) { value_decref(val); break; }
        if (i->yield_collect) {
            array_push(i->yield_collect->arr, val);
        } else {
            if (i->cf.value) value_decref(i->cf.value);
            i->cf.signal = CF_YIELD;
            i->cf.value  = val;
        }
        break;
    }

    case NODE_BREAK: {
        Value *val = stmt->brk.value ? EVAL(i, stmt->brk.value) : value_incref(XS_NULL_VAL);
        if (i->cf.value) value_decref(i->cf.value);
        free(i->cf.label); i->cf.label = stmt->brk.label ? xs_strdup(stmt->brk.label) : NULL;
        i->cf.signal = CF_BREAK;
        i->cf.value  = val;
        break;
    }

    case NODE_CONTINUE:
        free(i->cf.label); i->cf.label = stmt->cont.label ? xs_strdup(stmt->cont.label) : NULL;
        i->cf.signal = CF_CONTINUE;
        break;

    case NODE_THROW: {
        Value *val = EVAL(i, stmt->throw_.value);
        if (i->cf.value) value_decref(i->cf.value);
        i->cf.signal = CF_THROW;
        i->cf.value  = val;
        break;
    }

    case NODE_DEFER: {
        if (i->defers.len >= i->defers.cap) {
            i->defers.cap = i->defers.cap ? i->defers.cap * 2 : 8;
            i->defers.items = xs_realloc(i->defers.items, i->defers.cap * sizeof(Node*));
        }
        i->defers.items[i->defers.len++] = stmt->defer_.body;
        break;
    }

    case NODE_BLOCK: {
        Value *v = EVAL(i, stmt);
        value_decref(v);
        break;
    }

    case NODE_IF:
    case NODE_WHILE:
    case NODE_FOR:
    case NODE_LOOP:
    case NODE_MATCH:
    case NODE_TRY: {
        Value *v = interp_eval(i, stmt);
        value_decref(v);
        break;
    }

    default: {
            Value *v = interp_eval(i, stmt);
            value_decref(v);
        }
        break;
    }
}

static void hoist_functions(Interp *i, NodeList *stmts) {
    for (int j = 0; j < stmts->len; j++) {
        Node *stmt = stmts->items[j];
        if (!stmt || stmt->tag != NODE_FN_DECL) continue;
        if (!stmt->fn_decl.body || !stmt->fn_decl.name) continue;

        int nparams = stmt->fn_decl.params.len;
        Node **params = nparams ? xs_malloc(nparams * sizeof(Node*)) : NULL;
        Node **defaults = nparams ? xs_calloc(nparams, sizeof(Node*)) : NULL;
        int  *varflags  = nparams ? xs_calloc(nparams, sizeof(int)) : NULL;
        for (int k = 0; k < nparams; k++) {
            Param *pm = &stmt->fn_decl.params.items[k];
            if (pm->pattern) {
                params[k] = pm->pattern;
            } else {
                Node *pn = node_new(NODE_PAT_IDENT, pm->span);
                pn->pat_ident.name    = xs_strdup(pm->name ? pm->name : "_");
                pn->pat_ident.mutable = 0;
                params[k] = pn;
            }
            defaults[k]  = pm->default_val;
            varflags[k]   = pm->variadic;
        }
        XSFunc *fn = func_new_ex(stmt->fn_decl.name, params, nparams,
                                 stmt->fn_decl.body, i->env, defaults, varflags);
        fn->is_generator = stmt->fn_decl.is_generator;
        fn->is_async     = stmt->fn_decl.is_async;
        if (stmt->fn_decl.deprecated_msg)
            fn->deprecated_msg = xs_strdup(stmt->fn_decl.deprecated_msg);
        Value *v = xs_func_new(fn);
        env_define(i->env, stmt->fn_decl.name, v, 1);
        value_decref(v);
    }
}

void interp_run(Interp *i, Node *program) {
    if (!program || program->tag != NODE_PROGRAM) return;
    g_current_interp = i;
    g_plugin_interp = i;
    hoist_functions(i, &program->program.stmts);
    for (int j = 0; j < program->program.stmts.len; j++) {
        interp_exec(i, program->program.stmts.items[j]);

        /* phase 2: if a plugin registered syntax handlers, re-parse remaining source */
        if (i->needs_reparse) {
        }
        if (i->needs_reparse && i->source && i->filename &&
            (g_n_syntax_handlers > 0 || g_n_plugin_keywords > 0)) {
            i->needs_reparse = 0;
            if (i->cf.signal) CF_CLEAR(i);
            Lexer rlex;
            lexer_init(&rlex, i->source, i->filename);
            TokenArray rta = lexer_tokenize(&rlex);
            Parser rp;
            parser_init(&rp, &rta, i->filename);
            Node *reparsed = parser_parse(&rp);
            token_array_free(&rta);
            if (reparsed && !rp.had_error) {
                hoist_functions(i, &reparsed->program.stmts);
                for (int k = j + 1; k < reparsed->program.stmts.len; k++) {
                    interp_exec(i, reparsed->program.stmts.items[k]);
                    if (i->cf.signal == CF_RETURN) { CF_CLEAR(i); }
                    else if (i->cf.signal == CF_ERROR || i->cf.signal == CF_PANIC) {
                        goto run_done;
                    } else if (i->cf.signal == CF_THROW) {
                        Value *exc = i->cf.value;
                        char *s = exc ? value_repr(exc) : xs_strdup("<error>");
                        Node *sn = reparsed->program.stmts.items[k];
                        fprintf(stderr, "xs: error at %s:%d:%d: unhandled exception: %s\n",
                                sn->span.file ? sn->span.file : "<unknown>",
                                sn->span.line, sn->span.col, s);
                        free(s);
                        CF_CLEAR(i);
                    } else if (i->cf.signal) {
                        CF_CLEAR(i);
                    }
                }
                /* don't free reparsed -- AST nodes may be referenced by closures */
                goto run_done;
            }
            if (reparsed) node_free(reparsed);
        }

        if (i->cf.signal == CF_RETURN) { CF_CLEAR(i); }
        else if (i->cf.signal == CF_ERROR || i->cf.signal == CF_PANIC) {
            break;
        } else if (i->cf.signal == CF_THROW) {
            Value *exc = i->cf.value;
            char *s = exc ? value_repr(exc) : xs_strdup("<error>");
            Node *sn = program->program.stmts.items[j];
            fprintf(stderr, "xs: error at %s:%d:%d: unhandled exception: %s\n",
                    sn->span.file ? sn->span.file : "<unknown>",
                    sn->span.line, sn->span.col, s);
            free(s);
            CF_CLEAR(i);
        } else if (i->cf.signal) {
            CF_CLEAR(i);
        }
    }
run_done:;
    Value *main_fn = env_get(i->globals, "main");
    if (main_fn && main_fn->tag == XS_FUNC) {
        Value *res = call_value(i, main_fn, NULL, 0, "main");
        if (res) value_decref(res);
    }
}
