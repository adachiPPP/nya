#include "nya.h"

int fp_available(void) {
	if (access("/usr/bin/flatpak", X_OK) == 0 || access("/bin/flatpak", X_OK) == 0 ||
	    access("/usr/local/bin/flatpak", X_OK) == 0)
		return 1;
	const char *path = getenv("PATH");
	if (!path) return 0;
	char *copy = xstrdup(path);
	char *save = NULL;
	int found = 0;
	for (char *dir = strtok_r(copy, ":", &save); dir; dir = strtok_r(NULL, ":", &save)) {
		if (!dir[0]) continue;
		char buf[4096];
		snprintf(buf, sizeof buf, "%s/flatpak", dir);
		if (access(buf, X_OK) == 0) {
			found = 1;
			break;
		}
	}
	free(copy);
	return found;
}

/* when running as root (elevated via sudo/doas), run flatpak as the invoking
 * user so it manages the user install instead of root's. */
static char **fp_user_argv(char *const argv[]) {
	if (geteuid() != 0) return NULL;
	const char *user = invoking_user_name();
	if (!user) return NULL;
	int n = 0;
	while (argv[n]) n++;
	char **args = xcalloc(n + 5, sizeof *args);
	args[0] = "runuser";
	args[1] = "-u";
	args[2] = (char *)user;
	args[3] = "--";
	int i;
	for (i = 0; i < n; i++) args[4 + i] = argv[i];
	args[n + 4] = NULL;
	return args;
}

static int fp_cmd(char *const argv[]) {
	char **wrap = fp_user_argv(argv);
	int rc = run_cmd(wrap ? wrap : argv);
	free(wrap);
	return rc;
}

static int fp_capture(char *const argv[], char **out) {
	char **wrap = fp_user_argv(argv);
	int rc = run_capture_quiet(wrap ? wrap : argv, out);
	free(wrap);
	return rc;
}

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
	if (!fp_available()) {
		error("flatpak is not installed");
		return -1;
	}
	char **args = xcalloc(argc + 2, sizeof *args);
	args[0] = "flatpak";
	args[1] = (char *)map;
	int i;
	for (i = 1; i < argc; i++) args[i + 1] = argv[i];
	args[argc + 1] = NULL;
	int rc = fp_cmd(args);
	free(args);
	return rc;
}

int fp_search(config *c, const char **terms, int n) {
	(void)c;
	if (!fp_available()) return 0;
	char **argv = xcalloc(n + 4, sizeof *argv);
	argv[0] = "flatpak";
	argv[1] = "search";
	argv[2] = "--columns=name,description,application,version,remotes";
	int i;
	for (i = 0; i < n; i++) argv[i + 3] = (char *)terms[i];
	argv[n + 3] = NULL;
	char *out;
	if (fp_capture(argv, &out) != 0) {
		free(argv);
		free(out);
		return 0;
	}
	free(argv);
	int count = 0, first = 1;
	char *line = out;
	while (line && *line) {
		char *nl = strchr(line, '\n');
		if (nl) *nl = '\0';
		int header = first && (strncmp(line, "Name\t", 5) == 0 || strstr(line, "Application ID") != NULL);
		if (!header) {
			char *f[5] = {0};
			int fi = 0;
			char *p = line;
			while (p && fi < 5) {
				f[fi++] = p;
				char *t = strchr(p, '\t');
				if (!t) break;
				*t = '\0';
				p = t + 1;
			}
			const char *name = f[0] ? f[0] : "";
			const char *desc = f[1] ? f[1] : "";
			const char *app = f[2] ? f[2] : "";
			const char *ver = f[3] ? f[3] : "";
			if (app[0]) {
				printf("%sflatpak/%s %s%s%s%s\n", col_yellow(), app, col_reset(), col_bold(), ver, col_reset());
				if (desc[0]) printf("    %s\n", desc);
				count++;
			}
			(void)name;
		}
		first = 0;
		if (!nl) break;
		line = nl + 1;
	}
	free(out);
	return count;
}

int fp_update(config *c) {
	(void)c;
	if (!fp_available()) {
		warn("flatpak is not installed, skipping flatpak update");
		return -1;
	}
	info("Updating flatpak applications...");
	char *argv[] = {"flatpak", "update", "-y", NULL};
	int rc = fp_cmd(argv);
	if (rc != 0) warn("flatpak update failed");
	return rc;
}
