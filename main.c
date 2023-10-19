#include <stdlib.h>

#include "runtime.h"


int
main(int argc, char *argv[])
{
	init_runtime();
	if (argc == 1) return EXIT_FAILURE;
	char *file_name = argv[1];
	char *sexp_str = read_file(file_name);
	struct obj *root = gen_obj_list();
	struct obj *begin = gen_obj_symb("begin");
	sexp_append_obj_inplace(root, begin);
	parse(&root, &sexp_str);
	// print_obj(root);
	emit(file_name, root);
	build(file_name);
    deinit_runtime();
    return EXIT_SUCCESS;
}
