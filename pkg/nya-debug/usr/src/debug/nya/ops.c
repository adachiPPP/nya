#include "nya.h"
#include <regex.h>

typedef struct {
	regex_t re;
	int ok;
	const char *term;
} needle;

int do_search(config *c, const char **terms, int n, int local) {
	(void)c;
	int i;
	int hits = 0;
	needle *nd = xcalloc(n, sizeof *nd);
	for (i = 0; i < n; i++) {
		nd[i].term = terms[i];
		nd[i].ok = regcomp(&nd[i].re, terms[i], REG_EXTENDED | REG_NOSUB) == 0;
	}
	pkg **pkgs = local ? g_local : g_sync;
	int npkgs = local ? g_nlocal : g_nsync;
	for (i = 0; i < npkgs; i++) {
		pkg *p = pkgs[i];
		const char *name = p->name ? p->name : "";
		int matched = 1;
		int j;
		for (j = 0; j < n; j++) {
			int m = (nd[j].ok && (regexec(&nd[j].re, name, 0, NULL, 0) == 0 ||
			                     (p->desc && regexec(&nd[j].re, p->desc, 0, NULL, 0) == 0))) ||
			        strcasestr(name, nd[j].term) || (p->desc && strcasestr(p->desc, nd[j].term));
			if (!m) {
				matched = 0;
				break;
			}
		}
		if (!matched) continue;
		char line[1024];
		const char *repo = p->repo ? p->repo : "local";
		int installed = !local && db_find_local(p->name) != NULL;
		snprintf(line, sizeof line, "%s%s/%s%s%s %s%s", col_cyan(), repo, col_bold(), p->name, col_reset(),
		         col_green(), p->version ? p->version : "");
		printf("%s%s%s%s\n", line, col_reset(), installed ? col_green() : "", installed ? " [installed]" : "");
		hits++;
		if (p->desc) {
			const char *d = p->desc;
			while (*d) {
				const char *nl = strchr(d, '\n');
				size_t l = nl ? (size_t)(nl - d) : strlen(d);
				printf("    %.*s\n", (int)l, d);
				if (!nl) break;
				d = nl + 1;
			}
		}
	}
	for (i = 0; i < n; i++) {
		if (nd[i].ok) regfree(&nd[i].re);
	}
	free(nd);
	return hits;
}

static const char *field_join(strs *s) {
	static char buf[4096];
	buf[0] = '\0';
	int i;
	for (i = 0; i < s->n; i++) {
		if (buf[0]) strncat(buf, "  ", sizeof buf - strlen(buf) - 1);
		strncat(buf, s->v[i], sizeof buf - strlen(buf) - 1);
	}
	if (buf[0] == '\0') snprintf(buf, sizeof buf, "None");
	return buf;
}

static void print_info_field(const char *key, const char *val) {
	printf("%-16s: %s\n", key, val);
}

int do_info(config *c, const char **targets, int n, int local, int verbose) {
	(void)c;
	int i;
	for (i = 0; i < n; i++) {
		const char *name = targets[i];
		const char *repofilter = NULL;
		char repobuf[256];
		if (name[0] != '/' && strchr(name, '/')) {
			const char *slash = strchr(name, '/');
			size_t rl = slash - name;
			if (rl >= sizeof repobuf) rl = sizeof repobuf - 1;
			memcpy(repobuf, name, rl);
			repobuf[rl] = '\0';
			repofilter = repobuf;
			name = slash + 1;
		}
		pkg *p = local ? db_find_local(name) : db_find_sync_exact(repofilter, name);
		if (!p && !local) p = db_find_local(name);
		if (!p) {
			error("package '%s' was not found", name);
			return -1;
		}
		if (p->repo) print_info_field("Repository", p->repo);
		print_info_field("Name", p->name);
		print_info_field("Version", p->version);
		if (p->base) print_info_field("Base", p->base);
		if (p->desc) print_info_field("Description", p->desc);
		if (p->url) print_info_field("URL", p->url);
		if (p->arch) print_info_field("Architecture", p->arch);
		if (p->licenses.n) print_info_field("Licenses", field_join(&p->licenses));
		if (p->groups.n) print_info_field("Groups", field_join(&p->groups));
		if (p->provides.n) print_info_field("Provides", field_join(&p->provides));
		if (p->depends.n) print_info_field("Depends On", field_join(&p->depends));
		if (p->optdepends.n) print_info_field("Optional Deps", field_join(&p->optdepends));
		if (p->conflicts.n) print_info_field("Conflicts With", field_join(&p->conflicts));
		if (p->replaces.n) print_info_field("Replaces", field_join(&p->replaces));
		if (!local) {
			char sz[64];
			if (p->csize > 0) {
				fmt_size(p->csize, sz, sizeof sz);
				print_info_field("Download Size", sz);
			}
			if (p->isize > 0) {
				fmt_size(p->isize, sz, sizeof sz);
				print_info_field("Installed Size", sz);
			}
			if (p->packager) print_info_field("Packager", p->packager);
			if (p->builddate_ts > 0) {
				char buf[64];
				struct tm tm;
				time_t t = p->builddate_ts;
				gmtime_r(&t, &tm);
				strftime(buf, sizeof buf, "%a %d %b %Y %I:%M:%S %p UTC", &tm);
				print_info_field("Build Date", buf);
			}
			const char *val = p->validation;
			if (!val || !*val) val = p->sha256sum ? "SHA-256 Sum" : "Unknown";
			print_info_field("Validated By", val);
		}
		pkg *l = db_find_local(p->name);
		if (l) {
			if (l->installdate_ts > 0) {
				char buf[64];
				struct tm tm;
				time_t t = l->installdate_ts;
				localtime_r(&t, &tm);
				strftime(buf, sizeof buf, "%a %d %b %Y %I:%M:%S %p %Z", &tm);
				print_info_field("Installed On", buf);
			}
			print_info_field("Install Reason", l->reason == 1 ? "Dependency" : "Explicitly installed");
		}
		if (verbose && p->backup.n) {
			printf("\nBackup Files:\n");
			int j;
			for (j = 0; j < p->backup.n; j++) {
				printf("    %s\n", p->backup.v[j]);
			}
		}
		printf("\n");
	}
	return 0;
}

static int local_required_by(pkg *p) {
	int i, j;
	for (i = 0; i < g_nlocal; i++) {
		if (g_local[i] == p) continue;
		for (j = 0; j < g_local[i]->depends.n; j++) {
			depspec dep;
			depspec_parse(g_local[i]->depends.v[j], &dep);
			if (dep.mod && strchr(dep.name, ':')) {
				char *colon = strchr(dep.name, ':');
				*colon = '\0';
			}
			int m = pkg_matches_dep(p, &dep);
			depspec_free(&dep);
			if (m) return 1;
		}
	}
	return 0;
}

int do_list(config *c, int foreign, int deps, int explicit, int orphans, int upgradable) {
	(void)c;
	int i;
	for (i = 0; i < g_nlocal; i++) {
		pkg *p = g_local[i];
		if (foreign && db_find_sync(p->name)) continue;
		if (deps && p->reason != 1) continue;
		if (explicit && p->reason != 0) continue;
		if (orphans && (p->reason != 1 || local_required_by(p))) continue;
		if (upgradable) {
			pkg *s = db_find_sync(p->name);
			if (!s || vercmp(s->version, p->version) <= 0) continue;
			printf("%s %s\n", p->name, s->version);
			continue;
		}
		printf("%s %s\n", p->name, p->version);
	}
	return 0;
}

int do_files(config *c, const char **targets, int n) {
	int i;
	for (i = 0; i < n; i++) {
		pkg *p = db_find_local(targets[i]);
		if (!p) {
			error("package '%s' was not found", targets[i]);
			return -1;
		}
		int j;
		for (j = 0; j < p->files.n; j++) {
			printf("%s %s/%s\n", p->name, c->rootdir[1] ? c->rootdir : "", p->files.v[j]);
		}
	}
	return 0;
}

int do_check(config *c, const char **targets, int n, int deep) {
	int i, failures = 0;
	for (i = 0; i < n; i++) {
		pkg *p = db_find_local(targets[i]);
		if (!p) {
			error("package '%s' was not found", targets[i]);
			return -1;
		}
		hmap *sha = NULL;
		if (deep && p->mtree_data) sha = mtree_sha_map(p->mtree_data, p->mtree_len);
		int j;
		for (j = 0; j < p->files.n; j++) {
			const char *f = p->files.v[j];
			size_t fl = strlen(f);
			char rooted[4300];
			snprintf(rooted, sizeof rooted, "%s/%s", c->rootdir, f);
			if (fl > 0 && f[fl - 1] == '/') {
				if (!is_dir(rooted)) {
					error("%s: missing directory %s", p->name, rooted);
					failures++;
				}
				continue;
			}
			struct stat st;
			if (lstat(rooted, &st) != 0) {
				error("%s: missing %s", p->name, rooted);
				failures++;
				continue;
			}
			if (deep && S_ISREG(st.st_mode) && sha) {
				const char *orig = hmap_get(sha, f);
				if (orig) {
					char hex[65];
					if (sha256_file(rooted, hex) == 0 && strcasecmp(hex, orig) != 0) {
						error("%s: %s (Checksum mismatch)", p->name, rooted);
						failures++;
					}
				}
			}
		}
		if (sha) hmap_free(sha);
	}
	if (failures > 0) {
		error("%d file(s) failed integrity check", failures);
		return 1;
	}
	msg("no problems found");
	return 0;
}

int do_owns(config *c, const char *path) {
	char resolved[4096];
	if (path[0] != '/') {
		char *cwd = getcwd(NULL, 0);
		snprintf(resolved, sizeof resolved, "%s/%s", cwd, path);
		free(cwd);
	} else {
		snprintf(resolved, sizeof resolved, "%s", path);
	}
	char *real = realpath(resolved, NULL);
	if (real) snprintf(resolved, sizeof resolved, "%s", real);
	size_t rl = strlen(c->rootdir);
	const char *rel = resolved;
	if (rl > 1 && startswith(resolved, c->rootdir)) rel = resolved + rl;
	while (*rel) {
		const char *f = rel[0] == '/' ? rel + 1 : rel;
		const char *owner = db_owner(f);
		if (owner) {
			pkg *p = db_find_local(owner);
			printf("%s is owned by %s %s\n", resolved, owner, p ? p->version : "");
			if (real) free(real);
			return 0;
		}
		char *slash = strrchr(rel, '/');
		if (!slash) break;
		*slash = '\0';
		if (rel[0] == '\0') break;
	}
	if (real) free(real);
	error("no package owns %s", path);
	return 1;
}

int refresh_dbs(config *c, int force) {
	if (c->nrepos == 0) {
		error("no usable package repositories configured");
		return -1;
	}
	mkdir_p(c->dbpath, 0755);
	char syncpath[4096];
	snprintf(syncpath, sizeof syncpath, "%s/sync", c->dbpath);
	mkdir_p(syncpath, 0755);
	dl *jobs = xcalloc(c->nrepos, sizeof *jobs);
	repo **reps = xcalloc(c->nrepos, sizeof *reps);
	char **tmps = xcalloc(c->nrepos, sizeof *tmps);
	char **finals = xcalloc(c->nrepos, sizeof *finals);
	int i, n = 0;
	for (i = 0; i < c->nrepos; i++) {
		repo *r = c->repos[i];
		if (r->servers.n == 0) {
			warn("no servers configured for repository '%s'", r->name);
			continue;
		}
		reps[n] = r;
		char rel[512];
		snprintf(rel, sizeof rel, "%s.db", r->name);
		jobs[n].urls = xcalloc(r->servers.n, sizeof(char *));
		int j;
		for (j = 0; j < r->servers.n; j++) {
			size_t sl = strlen(r->servers.v[j]);
			int need = sl > 0 && r->servers.v[j][sl - 1] != '/';
			char *u = xmalloc(sl + need + strlen(rel) + 1);
			sprintf(u, "%s%s%s", r->servers.v[j], need ? "/" : "", rel);
			jobs[n].urls[j] = u;
			jobs[n].nurls++;
		}
		char tmp[4200];
		snprintf(tmp, sizeof tmp, "%s/.nya-%s-%ld", syncpath, rel, (long)getpid());
		char final[4200];
		snprintf(final, sizeof final, "%s/%s", syncpath, rel);
		jobs[n].dest = xstrdup(tmp);
		tmps[n] = xstrdup(tmp);
		finals[n] = xstrdup(final);
		n++;
	}
	if (n == 0) {
		error("no servers configured for any repository");
		free(jobs);
		free(reps);
		free(tmps);
		free(finals);
		return -1;
	}
	int n_stale = 0;
	int *stale = xcalloc(n, sizeof *stale);
	long now = (long)time(NULL);
	for (i = 0; i < n; i++) {
		struct stat st;
		if (!force && c->refreshage > 0 && stat(finals[i], &st) == 0) {
			long age = now - (long)st.st_mtime;
			if (age < c->refreshage) {
				msg("%s%s%s is up to date (age %ldh%ldm < %ldh)", col_green(), reps[i]->name, col_reset(),
				     age / 3600, (age % 3600) / 60, c->refreshage / 3600);
				stale[i] = 0;
				continue;
			}
		}
		stale[i] = 1;
		n_stale++;
	}
	if (n_stale == 0) {
		info("All package databases are up to date");
		free(stale);
		for (i = 0; i < n; i++) {
			free(tmps[i]);
			free(finals[i]);
			free((char *)jobs[i].dest);
			int j;
			for (j = 0; j < jobs[i].nurls; j++) free(jobs[i].urls[j]);
			free(jobs[i].urls);
		}
		free(jobs);
		free(reps);
		free(tmps);
		free(finals);
		return 0;
	}
	info("Synchronizing package databases (%d stale)...", n_stale);
	dl_parallel(c, jobs, n);
	int rc = 0;
	for (i = 0; i < n; i++) {
		if (!stale[i]) continue;
		if (jobs[i].ok) {
			rename(tmps[i], finals[i]);
			msg("%s%s%s is up to date", col_green(), reps[i]->name, col_reset());
		} else {
			unlink(tmps[i]);
			if (jobs[i].err[0]) {
				if (strstr(jobs[i].err, "Permission denied"))
					error("failed to sync %s: %s (run as root, e.g. 'sudo nya update')", reps[i]->name, jobs[i].err);
				else error("failed to sync %s: %s", reps[i]->name, jobs[i].err);
			}
			rc = -1;
		}
	}
	free(stale);
	for (i = 0; i < n; i++) {
		free(tmps[i]);
		free(finals[i]);
		free((char *)jobs[i].dest);
		int j;
		for (j = 0; j < jobs[i].nurls; j++) free(jobs[i].urls[j]);
		free(jobs[i].urls);
	}
	free(jobs);
	free(reps);
	free(tmps);
	free(finals);
	return rc;
}

int do_clean(config *c, int all) {
	int i, j;
	int removed = 0;
	for (i = 0; i < c->cachedirs.n; i++) {
		DIR *d = opendir(c->cachedirs.v[i]);
		if (!d) continue;
		struct dirent *de;
		while ((de = readdir(d)) != NULL) {
			const char *fn = de->d_name;
			if (!endswith(fn, ".pkg.tar.zst") && !endswith(fn, ".pkg.tar.xz") &&
			    !endswith(fn, ".pkg.tar.gz") && !endswith(fn, ".pkg.tar"))
				continue;
			int keep = 0;
			if (!all) {
				for (j = 0; j < g_nlocal; j++) {
					char expect[512];
					if (!g_local[j]->arch) continue;
					snprintf(expect, sizeof expect, "%s-%s-%s.pkg.tar.zst", g_local[j]->name,
					         g_local[j]->version, g_local[j]->arch);
					if (strcmp(expect, fn) == 0) {
						keep = 1;
						break;
					}
				}
			}
			if (!keep) {
				char full[4300];
				snprintf(full, sizeof full, "%s/%s", c->cachedirs.v[i], fn);
				unlink(full);
				removed++;
			}
		}
		closedir(d);
	}
	msg("removed %d cached package(s)", removed);
	return 0;
}
