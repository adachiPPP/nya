#include "nya.h"

#define WORLD_DIR "/nya/world"

static const char *world_dir_for(config *c) {
	static char buf[4096];
	if (c && c->rootdir && strcmp(c->rootdir, "/") == 0) {
		return WORLD_DIR;
	}
	snprintf(buf, sizeof buf, "%s/nya/world", c ? c->rootdir : "/");
	return buf;
}

int do_read_pkg(config *c) {
	const char *world_dir = world_dir_for(c);

	mkdir_p(world_dir, 0755);

	char path[4096];
	snprintf(path, sizeof path, "%s/packages", world_dir);
	FILE *f = fopen(path, "w");
	if (!f) {
		error("could not write to %s", path);
		return 1;
	}

	int i;
	for (i = 0; i < g_nlocal; i++) {
		pkg *p = g_local[i];
		if (!p->name) continue;
		fprintf(f, "%s", p->name);
		if (p->reason == 1) fprintf(f, " (dep)");
		fprintf(f, "\n");
	}
	fclose(f);
	msg("saved %d installed packages to %s/packages", g_nlocal, world_dir);

	snprintf(path, sizeof path, "%s/depends", world_dir);
	f = fopen(path, "w");
	if (!f) {
		error("could not write to %s", path);
		return 1;
	}

	for (i = 0; i < g_nlocal; i++) {
		pkg *p = g_local[i];
		if (!p->name) continue;
		int j;
		for (j = 0; j < p->depends.n; j++) {
			fprintf(f, "%s: %s\n", p->name, p->depends.v[j]);
		}
	}
	fclose(f);
	msg("saved dependency info to %s/depends", world_dir);

	return 0;
}

static int is_in_world(config *c, const char *name) {
	const char *wd = world_dir_for(c);
	char path[4096];
	snprintf(path, sizeof path, "%s/packages", wd);
	FILE *f = fopen(path, "r");
	if (!f) return 0;

	char line[4096];
	int found = 0;
	while (fgets(line, sizeof line, f)) {
		char *nl = strchr(line, '\n');
		if (nl) *nl = '\0';
		char *space = strchr(line, ' ');
		if (space) *space = '\0';
		if (strcmp(line, name) == 0) {
			found = 1;
			break;
		}
	}
	fclose(f);
	return found;
}

static strs *load_world_deps(config *c) {
	const char *wd = world_dir_for(c);
	char path[4096];
	snprintf(path, sizeof path, "%s/depends", wd);
	FILE *f = fopen(path, "r");
	if (!f) return NULL;

	strs *deps = xcalloc(1, sizeof *deps);
	char line[4096];
	while (fgets(line, sizeof line, f)) {
		char *nl = strchr(line, '\n');
		if (nl) *nl = '\0';
		char *colon = strchr(line, ':');
		if (colon) {
			*colon = '\0';
			char *depname = colon + 1;
			while (*depname == ' ') depname++;
			if (*depname && !strs_has(deps, depname)) {
				strs_add(deps, depname);
			}
		}
	}
	fclose(f);
	return deps;
}

int do_remove_unneeded(config *c) {
	const char *world_dir = world_dir_for(c);
	if (!is_dir(world_dir)) {
		error("world directory %s does not exist - run 'nya --readpkg' first", world_dir);
		return 1;
	}

	strs *world_deps = load_world_deps(c);
	if (!world_deps) {
		warn("could not load world dependencies");
	}

	txn t;
	txn_init(&t, c);

	int i;
	int count = 0;
	for (i = 0; i < g_nlocal; i++) {
		pkg *p = g_local[i];
		if (!p->name) continue;
		if (p->is_host) continue;

		if (is_in_world(c, p->name)) continue;

		int needed = 0;
		if (world_deps) {
			int j;
			for (j = 0; j < world_deps->n; j++) {
				depspec ds;
				if (depspec_parse(world_deps->v[j], &ds) == 0) {
					if (pkg_matches_dep(p, &ds)) {
						needed = 1;
					}
					depspec_free(&ds);
					if (needed) break;
				}
			}
		}

		if (!needed && !required_by_any(c, &t, p)) {
			txn_add_rm(&t, p);
			count++;
		}
	}

	if (world_deps) strs_free(world_deps);

	if (count == 0) {
		msg("no unneeded packages found");
		txn_free(&t);
		return 0;
	}

	msg("found %d unneeded package(s):", count);
	for (i = 0; i < t.nrm; i++) {
		printf("  - %s", t.rm[i]->name);
		if (t.rm[i]->reason == 1) printf(" (was auto-installed as dep)");
		printf("\n");
	}

	if (!yesno("remove these unneeded packages? ")) {
		msg("operation cancelled >.<");
		txn_free(&t);
		return 1;
	}

	t.unneeded = 1;
	return txn_run(c, &t, 2);
}
