#include "nya.h"

int fp_run(int argc, char **argv) {
	if (argc < 1) {
		error("flatpak operation required: nya -fp search|install|remove|list|update|info <app>...");
		return -1;
	}
	const char *verb = argv[0];
	const char *map = verb;
	if (strcmp(verb, "remove") == 0 || strcmp(verb, "uninstall") == 0) map = "uninstall";
	if (strcmp(verb, "install") != 0 && strcmp(verb, "search") != 0 && strcmp(verb, "uninstall") != 0 &&
	    strcmp(verb, "list") != 0 && strcmp(verb, "update") != 0 && strcmp(verb, "info") != 0) {
		error("unknown flatpak operation '%s'", verb);
		return -1;
	}
	if (access("/usr/bin/flatpak", X_OK) != 0 && access("/bin/flatpak", X_OK) != 0 &&
	    access("/usr/local/bin/flatpak", X_OK) != 0) {
		error("flatpak is not installed");
		return -1;
	}
	char **args = xcalloc(argc + 2, sizeof *args);
	args[0] = "flatpak";
	args[1] = (char *)map;
	int i;
	for (i = 1; i < argc; i++) args[i + 1] = argv[i];
	args[argc + 1] = NULL;
	return run_cmd(args);
}
