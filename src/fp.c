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

static int fp_row_matches(const char *name, const char *desc, const char *app,
                          const char **terms, int n);
static char *fp_search_cache(void);

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

#define FP_CACHE_TTL 1800

static char *fp_search_cache(void) {
	const char *home = getenv("HOME");
	char cachef[4600];
	if (home && *home) snprintf(cachef, sizeof cachef, "%s/.cache/nya/flatpak-apps.tsv", home);
	else snprintf(cachef, sizeof cachef, "/tmp/nya-flatpak-apps-%ld.tsv", (long)geteuid());
	struct stat st;
	if (stat(cachef, &st) == 0 &&
	    (long long)time(NULL) - (long long)st.st_mtime < FP_CACHE_TTL) {
		char *data = read_file(cachef, NULL);
		if (data) return data;
	}
	char *argv[] = { (char *)"flatpak", (char *)"remote-ls", (char *)"--app",
	                 (char *)"--columns=application,name,description,version", NULL };
	char *out = NULL;
	if (fp_capture(argv, &out) != 0 || !out) {
		free(out);
		return read_file(cachef, NULL);
	}
	char dir[4600];
	if (home && *home) snprintf(dir, sizeof dir, "%s/.cache/nya", home);
	else snprintf(dir, sizeof dir, "/tmp");
	mkdir_p(dir, 0755);
	write_file(cachef, out, (long)strlen(out), 0644);
	return out;
}

static int fp_row_matches(const char *name, const char *desc, const char *app,
                          const char **terms, int n) {
	int i;
	for (i = 0; i < n; i++) {
		if (strcasestr(name, terms[i]) || strcasestr(desc, terms[i]) || strcasestr(app, terms[i]))
			continue;
		int hit = 0;
		const char *sep = " -.";
		char *copy = xstrdup(app);
		char *save = NULL;
		for (char *tok = strtok_r(copy, sep, &save); tok; tok = strtok_r(NULL, sep, &save)) {
			if (strcasestr(tok, terms[i]) || strcasestr(terms[i], tok)) {
				hit = 1;
				break;
			}
		}
		free(copy);
		if (!hit) return 0;
	}
	return 1;
}

static int fp_row_score(const char *name, const char *desc, const char *app,
                        const char **terms, int n) {
	int i;
	int best = 0;
	for (i = 0; i < n; i++) {
		int s;
		const char *seg = strrchr(app, '.');
		seg = seg ? seg + 1 : app;
		if (strncasecmp(seg, terms[i], strlen(terms[i])) == 0) s = 0;
		else if (strcasestr(app, terms[i]) || strcasestr(name, terms[i])) s = 1;
		else if (strcasestr(desc, terms[i])) s = 2;
		else s = 3;
		if (s > best) best = s;
	}
	return best;
}

typedef struct {
	const char *app;
	const char *name;
	const char *desc;
	const char *ver;
	int score;
} fp_row;

static int fp_row_cmp(const void *a, const void *b) {
	const fp_row *x = a, *y = b;
	if (x->score != y->score) return x->score - y->score;
	size_t xl = strlen(x->name), yl = strlen(y->name);
	if (xl != yl) return (int)xl - (int)yl;
	return strcmp(x->app, y->app);
}

int fp_search(config *c, const char **terms, int n) {
	(void)c;
	if (!fp_available()) return 0;
	char *out = fp_search_cache();
	if (!out) return 0;
	fp_row *rows = NULL;
	int nrows = 0, cap = 0;
	hmap *seen = hmap_new(1 << 14);
	char *line = out;
	while (line && *line) {
		char *nl = strchr(line, '\n');
		if (nl) *nl = '\0';
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
		const char *app = f[0] ? f[0] : "";
		const char *name = f[1] ? f[1] : "";
		const char *desc = f[2] ? f[2] : "";
		const char *ver = f[3] ? f[3] : "";
		if (app[0] && !hmap_has(seen, app) && fp_row_matches(name, desc, app, terms, n)) {
			hmap_put(seen, app, (void *)1);
			if (nrows == cap) {
				cap = cap ? cap * 2 : 32;
				rows = xrealloc(rows, cap * sizeof *rows);
			}
			rows[nrows].app = app;
			rows[nrows].name = name;
			rows[nrows].desc = desc;
			rows[nrows].ver = ver;
			rows[nrows].score = fp_row_score(name, desc, app, terms, n);
			nrows++;
		}
		if (!nl) break;
		line = nl + 1;
	}
	qsort(rows, nrows, sizeof *rows, fp_row_cmp);
	int i;
	for (i = 0; i < nrows; i++) {
		printf("%sflatpak/%s %s%s%s%s\n", col_yellow(), rows[i].app, col_reset(), col_bold(), rows[i].ver, col_reset());
		if (rows[i].desc[0]) printf("    %s\n", rows[i].desc);
	}
	free(rows);
	hmap_free(seen);
	free(out);
	return nrows;
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
