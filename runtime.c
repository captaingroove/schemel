#include <stdbool.h>
#include <stdio.h>
#include <gmp.h>
#include <ctype.h>
#include <string.h>

#define STB_DS_IMPLEMENTATION
#include <stb/stb_ds.h>

#include "runtime.h"

#define MAX_STACK   (128)
#define MAX_ENV     (128)
#define MAX_VALLEN  (128)
#define MAX_STMTLEN (256)
#define FILE_SEP    ('/')

#define panic(...) { fprintf(stderr, __VA_ARGS__); exit(EXIT_FAILURE); }

void eval(char ***out, struct obj* ast);

/// Tree of environment hash tables implemented as an array
struct envht { char *key; struct obj *value; };
struct envt { struct envht *e; int pidx; };
struct envt env[MAX_ENV] = {0};
static int envmax = 0, envcur_sp = 0;
static int envcur[MAX_ENV] = {0};
/// Stack
static struct obj *stack[MAX_STACK] = {0};
static int sp = 0;
/// Lambda
static int label_idx = 1;
/// Code
static char **mainc = NULL;
static char **funcs = NULL;
static char **func_decls = NULL;
struct func_def {
	struct obj*parms;
	struct obj*body;
	char *name;
	int lambda_idx;
};
struct func_def *func_defs = NULL;
static int *parent_env = NULL;


/// Functions operating on objects/s-expressions
void
sexp_append_obj_inplace(struct obj *list, struct obj *obj)
{
	if (list == NULL || list->type != TLIST) panic("Can't append object to nil or non-list\n");
	struct obj **darr = list->pval;
	arrput(darr, obj);
	list->pval = darr;
}


void
sexp_append_or_set(struct obj **out, struct obj *obj)
{
	if (*out == NULL) *out = obj;
	else if ((*out)->type == TLIST) sexp_append_obj_inplace(*out, obj);
	/// Otherwise *out is a literal and we don't mutate it
}


/// String operations, lexer, parser
void
str_from_strview(char *s, struct strview sv)
{
	snprintf(s, sv.end - sv.beg + 1 + 1, "%s", sv.beg);
}


char *
read_file(char *file_name)
{
	FILE *f = fopen(file_name, "r");
	if (!f) panic("Could not open file '%s'\n", file_name);
	fseek(f, 0, SEEK_END);
	long fsize = ftell(f);
	rewind(f);
	char *ret = malloc(sizeof(char) * fsize + 1);
	size_t ir = fread(ret, fsize, sizeof(char), f);
	if (!ir) panic("Error while reading '%s'\n", file_name);
	ret[fsize] = 0;
	fclose(f);
	return ret;
}


void
tok_str(char *s, struct token t)
{
	size_t blen = 3 + t.s.end - t.s.beg + 1 + 1;
	snprintf(s, blen, "%d: %s", t.type, t.s.beg);
}


void
skip_space(char **ss)
{
	while (**ss && isspace(**ss)) (*ss)++;
}


struct token
next_tok(char **ss)
{
	struct token t;
	skip_space(ss);
	char c = **ss;
	if (c == 0) {
		t.type = TOKEOS;
		t.s = (struct strview){ .beg = *ss, .end = *ss };
	} else if (c == '(') {
		t.type = TOKPARL;
		t.s = (struct strview){ .beg = *ss, .end = *ss };
		(*ss)++;
	} else if (c == ')') {
		t.type = TOKPARR;
		t.s = (struct strview){ .beg = *ss, .end = *ss };
		(*ss)++;
	} else if (isdigit(c)
		|| (*(*ss + 1) && isdigit(*(*ss + 1)) && (c == '+' || c == '-'))) {
		t.type = TOKNUM;
		t.s = (struct strview){ .beg = *ss };
		if (c == '+' || c == '-') (*ss)++;
		while (isdigit(**ss)) (*ss)++;
		t.s.end = *ss - 1;
	} else {
		t.type = TOKSYMB;
		t.s = (struct strview){ .beg = *ss };
		while (!isspace(**ss) && **ss != '(' && **ss != ')') (*ss)++;
		t.s.end = *ss - 1;
	}
	return t;
}


int
parse(struct obj **ast, char **sexpr_str)
{
	struct token t;
	char buf[MAX_VALLEN];
	t = next_tok(sexpr_str);
	int t_type = t.type;
	if (t_type == TOKEOS) {
	}
	else if (t_type == TOKPARL) {
		struct obj *o = gen_obj_list();
		int t_type;
		do {
			t_type = parse(&o, sexpr_str);
		} while (t_type != TOKPARR);
		sexp_append_or_set(ast, o);
	}
	else if (t_type == TOKPARR) {
	}
	else if (t_type == TOKNUM) {
		struct obj *o = gen_obj_int_strview(t.s);
		sexp_append_or_set(ast, o);
	}
	else if (t_type == TOKSYMB) {
		str_from_strview(buf, t.s);
		struct obj *o = gen_obj_symb(buf);
		sexp_append_or_set(ast, o);
	}
	else {
		panic("unknown token\n");
	}
	return t_type;
}


static void
emit_incl(char ***out)
{
	char **outarr = *out;
	arrput(outarr,
		"#include <stdlib.h>\n"
		"#include \"runtime.h\"\n\n"
	);
	*out = outarr;
}


static void
emit_main_top(char ***out)
{
	char **outarr = *out;
	arrput(outarr,
		"int\n"
		"main(int argc, char *argv[])\n"
		"{\n"
		"	int ret = EXIT_FAILURE;\n"
		"	init_runtime();\n"
	);
	*out = outarr;
}


static void
emit_main_bottom(char ***out)
{
	char **outarr = *out;
	arrput(outarr,
		"	ret = EXIT_SUCCESS;\n"
		"	deinit_runtime();\n"
		"	return ret;\n"
		"}\n"
	);
	*out = outarr;
}


static void
emit_display(char ***out)
{
	char **outarr = *out;
	arrput(outarr,
		"	print_obj(pop());\n"
		"	push(NULL);\n"
	);
	*out = outarr;
}


static void
emit_literal(char ***out, struct obj *obj)
{
	char **outarr = *out;
	char s[MAX_VALLEN];
	obj_tostr(s, obj);
	char *so = malloc(MAX_STMTLEN);
	sprintf(so, "	push(gen_obj_int(%s));\n", s);
	arrput(outarr, so);
	*out = outarr;
}


static void
emit_retrieve(char ***out, struct obj *obj)
{
	char **outarr = *out;
	char s[MAX_VALLEN];
	obj_tostr(s, obj);
	char *so = malloc(MAX_STMTLEN);
	sprintf(so, "	push(retrieve_symbol(\"%s\"));\n", (char *)obj->pval);
	arrput(outarr, so);
	*out = outarr;
}


static void
emit_call(char ***out, struct obj *obj, size_t narg)
{
	char **outarr = *out;
	char s[MAX_VALLEN];
	obj_tostr(s, obj);
	char *so = malloc(MAX_STMTLEN);
	sprintf(so, "	call_obj(retrieve_symbol(\"%s\"), %ld);\n", (char *)obj->pval, narg);
	arrput(outarr, so);
	*out = outarr;
}


static void
emit_call_obj(char ***out, int argc)
{
	char **outarr = *out;
	char *so = malloc(MAX_STMTLEN);
	sprintf(so, "	call_obj(pop(), %d);\n", argc);
	arrput(outarr, so);
	*out = outarr;
}


static void
emit_if(char ***out, struct obj *conseq, struct obj *alter)
{
	char **outarr = *out;
	arrput(outarr, "	if (is_true(pop())) {\n");
	*out = outarr;
	eval(out, conseq);
	outarr = *out;
	arrput(outarr, "	} else {\n");
	*out = outarr;
	eval(out, alter);
	outarr = *out;
	arrput(outarr, "	}\n");
	*out = outarr;
}


static void
emit_define(char ***out, struct obj *obj)
{
	char **outarr = *out;
	char s[MAX_VALLEN];
	obj_tostr(s, obj);
	char *so = malloc(MAX_STMTLEN);
	sprintf(so, "	define_local(pop(), \"%s\");", (char *)obj->pval);
	arrput(outarr, so);
	arrput(outarr, "	push(NULL);\n");
	*out = outarr;
}


static void
emit_set(char ***out, struct obj *obj)
{
	char **outarr = *out;
	char s[MAX_VALLEN];
	obj_tostr(s, obj);
	char *so = malloc(MAX_STMTLEN);
	sprintf(so, "	define_global(pop(), \"%s\");", (char *)obj->pval);
	arrput(outarr, so);
	arrput(outarr, "	push(NULL);\n");
	*out = outarr;
}


static void
emit_pop(char ***out)
{
	char **outarr = *out;
	arrput(outarr, "	pop();\n");
	*out = outarr;
}


static void
emit_lambda_obj(char ***out, char *name, int env_idx)
{
	char **outarr = *out;
	char *so = malloc(MAX_STMTLEN);
	sprintf(so, "	push(gen_obj_fn(%s, %d));\n", name, env_idx);
	arrput(outarr, so);
	*out = outarr;
}


static void
emit_env(char ***out, int env_idx, int parent_env_idx)
{
	char **outarr = *out;
	char *so = malloc(MAX_STMTLEN);
	sprintf(so, "	new_env(%d, %d);\n", env_idx, parent_env_idx);
	arrput(outarr, so);
	*out = outarr;
}


static void
emit_lambda_decl(char ***out, char *name)
{
	char **outarr = *out;
	char *so = malloc(MAX_STMTLEN);
	sprintf(so, "void %s(int nargs);\n", name);
	arrput(outarr, so);
	*out = outarr;
}


static void
emit_lambda_def(char ***out, struct func_def *fd)
{
	/// Generate function definition outside of main() by
	/// generating code to add the parms to the runtime environment of the function
	char **outarr = *out;
	char *so = malloc(MAX_STMTLEN);
	sprintf(so, "void %s(int nargs)\n", fd->name);
	arrput(outarr, so);
	arrput(outarr, "{\n");
	struct obj **parr = fd->parms->pval;
	for (ptrdiff_t i = arrlen(parr) - 1; i >= 0; i--) {
		struct obj *parm = parr[i];
		char *so = malloc(MAX_STMTLEN);
		sprintf(so, "	define_local(pop(), \"%s\");\n", (char *)parm->pval);
		arrput(outarr, so);
	}
	/// Generate code for the function body
	arrput(parent_env, fd->lambda_idx);
	*out = outarr;
	eval(out, fd->body);
	outarr = *out;
	arrpop(parent_env);
	arrput(outarr, "}\n");
	*out = outarr;
}


static void
emit_quote(char ***out, struct obj *obj)
{
	char **outarr = *out;
	/// FIXME don't open a scope or allocate a lh value for obj
	// arrput(outarr, "	{\n");
	arrput(outarr, "		struct obj *o = NULL;\n");
	char *so = malloc(MAX_STMTLEN);
	char val_s[MAX_VALLEN] = {0};
	obj_tostr(val_s, obj);
	sprintf(so, "		char *sexpr_str = \"%s\";\n", val_s);
	arrput(outarr, so);
	arrput(outarr, "		parse(&o, &sexpr_str);\n");
	arrput(outarr, "		push(o);\n");
	// arrput(outarr, "	}\n");
	*out = outarr;
}


void
eval_list(char ***out, struct obj **list, ptrdiff_t beg, ptrdiff_t end)
{
	if (end == -1) end = arrlen(list) - 1;
	else if (end > arrlen(list) - 1) return;
	for (ptrdiff_t i = beg; i <= end; i++) {
		eval(out, list[i]);
	}
}


void
eval(char ***out, struct obj* ast)
{
	if (ast->type == TLIST) {
		struct obj **x = ast->pval;
		struct obj *fo = x[0];
		if (fo->type == TSYMB) {
			char *symb = fo->pval;
			if (strcmp(symb, "quote") == 0) {
				// /// TODO implement quote
				// /// push the list without the first object which is symbol "quote"
				// /// => push CDR(list)
				// /// quote seems to expect only one argument
				emit_quote(out, x[1]);
			} else if (strcmp(symb, "if") == 0) {
			// if (strcmp(symb, "if") == 0) {
				eval(out, x[1]);
				emit_if(out, x[2], x[3]);
			} else if (strcmp(symb, "define") == 0) {
				eval(out, x[2]);
				emit_define(out, x[1]);
			} else if (strcmp(symb, "set!") == 0) {
				/// FIXME maybe incorrect, could also be set in an enclosing scope,
				/// not necessarily the global scope
				eval(out, x[2]);
				emit_set(out, x[1]);
			} else if (strcmp(symb, "begin") == 0) {
				size_t i;
				for (i = 1; i < arrlenu(x) - 1; i++) {
					eval(out, x[i]);
					emit_pop(out);
				}
				eval(out, x[i]);
			} else if (strcmp(symb, "display") == 0) {
				eval(out, x[1]);
				emit_display(out);
			} else if (strcmp(symb, "lambda") == 0) {
				char *lambda_name = malloc(MAX_VALLEN);
				sprintf(lambda_name, "lambda_%d", label_idx);
				int env_idx = label_idx;
				label_idx++;
				struct func_def fd = {
					.parms = x[1],
					.body = x[2],
					.name = lambda_name,
					.lambda_idx = env_idx
				};
				arrput(func_defs, fd);
				emit_lambda_obj(out, lambda_name, env_idx);
				/// Generate lambda object with its newly created environment index
				/// and also a new environment with the env_idx 
				size_t last_parenv = parent_env[arrlenu(parent_env) - 1];
				emit_env(out, env_idx, last_parenv);
			} else {  /// Function call (proc arg ...)
				eval_list(out, x, 1, -1);
				emit_call(out, fo, arrlenu(x) - 1);
			}
		} else if (fo->type == TLIST) {  /// Function call ((proc ...) arg ...)
			eval_list(out, x, 1, -1);
			eval(out, x[0]);
			emit_call_obj(out, arrlenu(x) - 1);
		}
	} else if (ast->type == TSYMB) {  /// Variable reference
		emit_retrieve(out, ast);
	} else if (ast->type == TNUM) {  /// Constant literal
		/// TODO differ between integer and float (use multiprecision library?)
		///      ... and character literals ...?
		emit_literal(out, ast);
	}
}


static char *
chop_file_ext(char *file_name)
{
	char *dot = strrchr(file_name, '.');
	char *slash = strrchr(file_name, FILE_SEP);
	if (!dot || dot < slash) return file_name;
	char *ret = malloc(dot - file_name + 1);
	memcpy(ret, file_name, dot - file_name);
	ret[dot - file_name] = '\0';
	return ret;
}


static char *
add_suffix(char *file_base, const char *suffix)
{
	size_t file_base_len = strlen(file_base);
    char *out_file = realloc(file_base, file_base_len + 2);
    sprintf(out_file, "%s%s", file_base, suffix);
    return out_file;
}


void
emit(char *file_name, struct obj* ast)
{
	char *file_base = chop_file_ext(file_name);
	if (!file_base) return;
    FILE *f = fopen(add_suffix(file_base, ".c"), "w");
	emit_incl(&funcs);
	emit_main_top(&mainc);
	eval(&mainc, ast);
	emit_main_bottom(&mainc);
	for (size_t i = 0; i < arrlenu(func_defs); i++) {
		emit_lambda_def(&funcs, &func_defs[i]);
	}
	for (size_t i = 0; i < arrlenu(func_defs); i++) {
		emit_lambda_decl(&func_decls, func_defs[i].name);
	}
	for (size_t i = 0; i < arrlenu(func_decls); i++) {
		fputs(func_decls[i], f);
	}
	for (size_t i = 0; i < arrlenu(funcs); i++) {
		fputs(funcs[i], f);
	}
	for (size_t i = 0; i < arrlenu(mainc); i++) {
		fputs(mainc[i], f);
	}
	fclose(f);
}


void
build(char *file_name)
{
	char *file_base = chop_file_ext(file_name);
	char cmd[MAX_STMTLEN];
	sprintf(cmd, "cc -g -I. -o %s %s.c runtime.o -lgmp", file_base, file_base);
	system("make");
	system(cmd);
}


/// Functions for generating objects

struct obj *
gen_obj_bool(bool op)
{
	struct obj *res = malloc(sizeof(struct obj));
	res->type = TBOOL;
	res->pval = malloc(sizeof(bool));
	res->envidx = 0;
	*(bool *)res->pval = op;
	return res;
}


struct obj *
gen_obj_int(long int op)
{
	struct obj *res = malloc(sizeof(struct obj));
	res->type = TNUM;
	res->pval = malloc(sizeof(mpz_t));
	res->envidx = 0;
	if (op >= 0) {
		mpz_init_set_ui(res->pval, op);
	} else {
		mpz_init_set_si(res->pval, op);
	}
	return res;
}


struct obj *
gen_obj_int_strview(struct strview op)
{
	struct obj *res = malloc(sizeof(struct obj));
	res->type = TNUM;
	res->pval = malloc(sizeof(mpz_t));
	res->envidx = 0;
	char opstr[MAX_VALLEN];
	str_from_strview(opstr, op);
	mpz_init_set_str(res->pval, opstr, 10);
	return res;
}


struct obj *
gen_obj_symb(char *symb)
{
	struct obj *res = malloc(sizeof(struct obj));
	res->type = TSYMB;
	size_t symb_len = strlen(symb) + 1;
	res->pval = malloc(symb_len);
	memcpy(res->pval, symb, symb_len);
	res->envidx = 0;
	return res;
}


struct obj *
gen_obj_list(void)
{
	struct obj *res = malloc(sizeof(struct obj));
	res->type = TLIST;
	res->pval = NULL;
	res->envidx = 0;
	return res;
}


struct obj *
gen_obj_fn(func fn, int idx)
{
	struct obj *res = malloc(sizeof(struct obj));
	res->type = TFUNC;
	res->pval = (func*)fn;
	// res->envidx = envcur;
	res->envidx = idx;
	// new_env();
	return res;
}


/// Builtin functions called by the runtime/VM
static void
add(int nargs)
{
	(void)nargs;
	struct obj *res = gen_obj_int(0);
	mpz_add(res->pval, pop()->pval, pop()->pval);
	push(res);
}


static void
sub(int nargs)
{
	(void)nargs;
	struct obj *res = gen_obj_int(0);
	mpz_sub(res->pval, pop()->pval, pop()->pval);
	push(res);
}


static void
mul(int nargs)
{
	(void)nargs;
	struct obj *res = gen_obj_int(0);
	struct obj *o2 = pop();
	struct obj *o1 = pop();
	if (o1 == NULL || o2 == NULL) {
		fprintf(stderr, "arguments for '*' are NULL\n");
	}
	mpz_mul(res->pval, o1->pval, o2->pval);
	// mpz_mul(res->pval, pop()->pval, pop()->pval);
	push(res);
}


static void
gt(int nargs)
{
	(void)nargs;
	struct obj *res = gen_obj_bool(false);
	int r = mpz_cmp(pop()->pval, pop()->pval);
	if (r > 0) *(bool *)res->pval = true;
	push(res);
}


static void
lt(int nargs)
{
	(void)nargs;
	struct obj *res = gen_obj_bool(false);
	int r = mpz_cmp(pop()->pval, pop()->pval);
	if (r < 0) *(bool *)res->pval = true;
	push(res);
}


static void
ge(int nargs)
{
	(void)nargs;
	struct obj *res = gen_obj_bool(false);
	int r = mpz_cmp(pop()->pval, pop()->pval);
	if (r >= 0) *(bool *)res->pval = true;
	push(res);
}


static void
le(int nargs)
{
	(void)nargs;
	struct obj *res = gen_obj_bool(false);
	int r = mpz_cmp(pop()->pval, pop()->pval);
	if (r <= 0) *(bool *)res->pval = true;
	push(res);
}


static void
eq(int nargs)
{
	(void)nargs;
	struct obj *res = gen_obj_bool(false);
	int r = mpz_cmp(pop()->pval, pop()->pval);
	if (r == 0) *(bool *)res->pval = true;
	push(res);
}


static void
list(int nargs)
{
	struct obj *res = gen_obj_list();
	struct obj **darr = NULL;
	darr = arraddnptr(darr, nargs);
	for (int i = nargs - 1; i >= 0; i--) {
		darr[i] = pop();
	}
	res->pval = darr;
	push(res);
}


static void
car(int nargs)
{
	(void)nargs;
	struct obj **darr = pop()->pval;
	struct obj *res = darr[0];
	push(res);
}


static void
cdr(int nargs)
{
	(void)nargs;
	struct obj *res = gen_obj_list();
	struct obj **iarr = pop()->pval;
	struct obj **oarr = NULL;
	size_t oarrlen = arrlenu(iarr) - 1;
	oarr = arraddnptr(oarr, oarrlen);
	for (size_t i = 0; i < oarrlen; i++) {
		oarr[i] = iarr[i + 1];
	}
	res->pval = oarr;
	push(res);
}


static void
cons(int nargs)
{
	(void)nargs;
	struct obj *res = gen_obj_list();
	struct obj **iarr = pop()->pval;
	struct obj *iel = pop();
	struct obj **oarr = NULL;
	size_t oarrlen = arrlenu(iarr) + 1;
	oarr = arraddnptr(oarr, oarrlen);
	oarr[0] = iel;
	for (size_t i = 1; i < oarrlen; i++) {
		oarr[i] = iarr[i - 1];
	}
	res->pval = oarr;
	push(res);
}


static void
null_pred(int nargs)
{
	(void)nargs;
	struct obj *o = pop();
	bool resval = false;
	if (o->type == TLIST) {
		struct obj **iarr = o->pval;
		resval = arrlenu(iarr) == 0;
	}
	struct obj *res = gen_obj_bool(resval);
	push(res);
}


static void
length(int nargs)
{
	(void)nargs;
	struct obj **iarr = pop()->pval;
	struct obj *res = gen_obj_int(arrlenu(iarr));
	push(res);
}


static void
append(int nargs)
{
	(void)nargs;
	struct obj *res = gen_obj_list();
	struct obj **iarr2 = pop()->pval;
	struct obj **iarr1 = pop()->pval;
	struct obj **oarr = NULL;
	size_t oarrlen = arrlenu(iarr1) + arrlenu(iarr2);
	oarr = arraddnptr(oarr, oarrlen);
	for (size_t i = 0; i < arrlenu(iarr1); i++) {
		oarr[i] = iarr1[i];
	}
	for (size_t i = 0; i < arrlenu(iarr2); i++) {
		oarr[i + arrlenu(iarr1)] = iarr2[i];
	}
	res->pval = oarr;
	push(res);
}


// static void
// quote(int nargs)
// {
	// (void)nargs;
	// // push(pop());
// }


bool
init_builtins()
{
    shput(env[0].e, "+", gen_obj_fn(add, 0));
    shput(env[0].e, "-", gen_obj_fn(sub, 0));
    shput(env[0].e, "*", gen_obj_fn(mul, 0));
    shput(env[0].e, ">", gen_obj_fn(gt, 0));
    shput(env[0].e, ">=", gen_obj_fn(ge, 0));
    shput(env[0].e, "<", gen_obj_fn(lt, 0));
    shput(env[0].e, "<=", gen_obj_fn(le, 0));
    shput(env[0].e, "=", gen_obj_fn(eq, 0));
    shput(env[0].e, "list", gen_obj_fn(list, 0));
    shput(env[0].e, "car", gen_obj_fn(car, 0));
    shput(env[0].e, "cdr", gen_obj_fn(cdr, 0));
    shput(env[0].e, "cons", gen_obj_fn(cons, 0));
    shput(env[0].e, "null?", gen_obj_fn(null_pred, 0));
    shput(env[0].e, "length", gen_obj_fn(length, 0));
    shput(env[0].e, "append", gen_obj_fn(append, 0));
    // shput(env[0].e, "quote", gen_obj_fn(quote, 0));
    return true;
}


bool
init_runtime()
{
	for (int eidx = 0; eidx < MAX_ENV; eidx++) {
	    env[eidx].e = NULL;
	    env[eidx].pidx = -1;
	}
	arrput(parent_env, 0);
	init_builtins();
	return true;
}


bool
deinit_runtime()
{
	/// TODO we should destroy the whole environment tree
	shfree(env[0].e);
    return true;
}


int
is_true(struct obj *obj)
{
	/// TODO need a proper way to handle errors from the runtime
	///      then is_true() should return a bool
	if (obj->type != TBOOL) return -1;
	return *(bool *)obj->pval == true;
}


void
push(struct obj *obj)
{
	if (sp == MAX_STACK - 1) {
		fprintf(stderr, "stack overflow\n");
		return;
	}
	sp++;
	stack[sp] = obj;
}


struct obj *
pop(void)
{
	if (sp <= 0) {
		fprintf(stderr, "stack underflow\n");
		return NULL;
	}
	struct obj *ret = stack[sp];
	stack[sp] = 0;
	sp--;
	return ret;
}


void
new_env(int eidx, int pidx)
{
	envmax = eidx > envmax ? eidx : envmax;
	env[eidx].e = NULL;
	env[eidx].pidx = pidx;
}


void
define_local(struct obj *obj, char *name)
{
    shput(env[envcur[envcur_sp]].e, name, obj);
}


void
define_global(struct obj *obj, char *name)
{
    shput(env[0].e, name, obj);
}


struct obj *
retrieve_symbol(char *name)
{
	// print_env();
	/// Search tree for symbol, starting with envcur
	///       and iterating over all parents until global env is reached
	int eidx = envcur[envcur_sp];
	// fprintf(stderr, "retrieving %s from env %d\n", name, eidx);
	while (eidx != -1) {
		struct obj *ret = shget(env[eidx].e, name);
		if (ret) return ret;
		eidx = env[eidx].pidx;
	}
	panic("could not retrieve symbol '%s'\n", name);
	return NULL;
}


void
call_obj(struct obj *obj, int nargs)
{
	(void)nargs;
	if (!obj) panic("cannot call nil");
	if (obj->type != TFUNC) panic("attempt to call non-function object");
	// fprintf(stderr, "calling %p with env %d\n", obj->pval, obj->envidx);
	func *fn = (func*)(obj->pval);
	envcur_sp++;
	envcur[envcur_sp] = obj->envidx;
	fn(nargs);
	envcur[envcur_sp] = 0;
	envcur_sp--;
}


int
obj_tostr(char *str, struct obj *obj)
{
	int ret = 0;
	if (!obj) {
		return ret;
	}
	struct obj **darr = obj->pval;
	struct obj *olist = obj->pval;
	int l = 0;
	switch(obj->type) {
	case TBOOL:
		ret = sprintf(str, "%s", *(bool *)obj->pval ? "#t" : "#f");
		break;
	case TNUM:
		mpz_get_str(str, 10, obj->pval);
		ret = strlen(str);
		break;
	case TSYMB:
		l = strlen((char *)obj->pval);
		memcpy(str, obj->pval, l);
		ret = l;
		break;
	case TLIST:
		*str++ = '(';
		ret++;
		for (size_t i = 0; i < arrlenu(olist); i++) {
			l = obj_tostr(str, darr[i]);
			str += l;
			ret += l;
			if (i < arrlenu(olist) - 1) {
				*str++ = ' ';
				ret++;
			}
		}
		*str++ = ')';
		ret++;
		break;
	case TFUNC:
		ret = sprintf(str, "func %p", obj->pval);
		break;
	}
	return ret;
}


void
print_obj(struct obj *obj)
{
	if (!obj) {
		fprintf(stderr, "attempt to print NULL object\n");
		return;
	}
	char val_s[MAX_VALLEN] = {0};
	if (obj_tostr(val_s, obj) > 0) puts(val_s);
}


void
print_symbol(char *name)
{
	struct obj *obj = retrieve_symbol(name);
	if (obj) print_obj(obj);
}


void
print_stack()
{
	fprintf(stderr, "STACK BOTTOM:\n");
	for (int i = 0; i <= sp; i++) {
		print_obj(stack[i]);
	}
	fprintf(stderr, "STACK TOP^\n");
}


void
print_env()
{
	int eidx = envcur[envcur_sp];
	while (eidx != -1) {
		fprintf(stderr, "ENV[%d]:\n", eidx);
		for (int j = 0; j < shlen(env[eidx].e); j++) {
			fprintf(stderr, "  %s:", env[eidx].e[j].key);
			print_obj(env[eidx].e[j].value);
		}
		eidx = env[eidx].pidx;
	}
}

