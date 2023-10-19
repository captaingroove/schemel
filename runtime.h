#ifndef __RUNTIME_DEF__
#define __RUNTIME_DEF__

#include <stdbool.h>


struct strview {
	char *beg, *end;
};

typedef enum token_type {
	TOKEOS = 0,
	TOKPARL,
	TOKPARR,
	TOKNUM,
	TOKSYMB,
	TOKLAST
} token_type;

struct token {
	token_type type;
	struct strview s;
};

enum types {
	TBOOL = 0,
	TNUM,
	TSYMB,
	TLIST,
	TFUNC,
	TLAST
};

struct obj {
	int type;
	void *pval;
	int envidx;
};


typedef void (func) (int);

bool init_runtime();
bool deinit_runtime();
char *read_file(char *file_name);
void skip_space(char **ss);
struct token next_tok(char **ss);
void tok_str(char *s, struct token t);
int parse(struct obj **ast, char **sexpr_str);
void emit(char *file_name, struct obj* ast);
void build(char *file_name);
void new_env(int eidx, int pidx);
/// Operations on objects and s-expressions
struct obj *gen_obj_bool(bool op);
struct obj *gen_obj_int(long int op);
struct obj *gen_obj_int_strview(struct strview op);
struct obj *gen_obj_symb(char *symb);
struct obj *gen_obj_fn(func fn, int envidx);
struct obj *gen_obj_list(void);
int is_true(struct obj *obj);
void sexp_append_obj_inplace(struct obj *list, struct obj *obj);
/// Runtime functions
void push(struct obj *obj);
struct obj *pop(void);
struct obj *retrieve_symbol(char *name);
void define_global(struct obj *obj, char *name);
void define_local(struct obj *obj, char *name);
void call_obj(struct obj *obj, int nargs);
int obj_tostr(char *str, struct obj *obj);
void print_obj(struct obj *obj);
void print_stack();
void print_env();
/// VM operations
void QUOTE(char *sexp);

#endif
