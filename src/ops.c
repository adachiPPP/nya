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
		if (deep) {
			if (pkg_load_mtree(p) != 0) continue;
			sha = mtree_sha_map(p->mtree_data, p->mtree_len);
		}
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
	msg("no problems found mreow~");
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
		info("All package databases are up to date mreow~");
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

static char *str_tolower_dup(const char *s) {
	char *out = xstrdup(s ? s : "");
	str_lower(out);
	return out;
}

static const char *decl_config_path(config *c) {
	static char buf[4096];
	if (c && c->rootdir && strcmp(c->rootdir, "/") == 0) return "/nya/config.nya";
	snprintf(buf, sizeof buf, "%s/nya/config.nya", c ? c->rootdir : "/");
	return buf;
}

static const char *decl_world_dir(config *c) {
	static char buf[4096];
	if (c && c->rootdir && strcmp(c->rootdir, "/") == 0) return "/nya/world";
	snprintf(buf, sizeof buf, "%s/nya/world", c ? c->rootdir : "/");
	return buf;
}

static int decl_is_pkg_key(const char *key) {
	char *l = str_tolower_dup(key);
	int ok = (strcmp(l, "packages") == 0 || strcmp(l, "pkgs") == 0 ||
	          strcmp(l, "systempackages") == 0 || strcmp(l, "world") == 0 ||
	          strcmp(l, "installed") == 0 || strcmp(l, "units") == 0);
	free(l);
	return ok;
}

static int decl_is_keyword(const char *tok) {
	char *l = str_tolower_dup(tok);
	int ok = (strcmp(l, "packages") == 0 || strcmp(l, "pkgs") == 0 ||
	          strcmp(l, "systempackages") == 0 || strcmp(l, "options") == 0 ||
	          strcmp(l, "true") == 0 || strcmp(l, "false") == 0 ||
	          strcmp(l, "yes") == 0 || strcmp(l, "no") == 0 ||
	          strcmp(l, "on") == 0 || strcmp(l, "off") == 0);
	free(l);
	return ok;
}

static void decl_parse_tokens(const char *text, strs *desired) {
	const char *p = text;
	while (*p) {
		while (*p && (isspace((unsigned char)*p) || *p == ',' || *p == '[' ||
		              *p == ']' || *p == ';' || *p == '{' || *p == '}'))
			p++;
		if (!*p) break;
		const char *start = p;
		while (*p && !isspace((unsigned char)*p) && *p != ',' && *p != '[' &&
		       *p != ']' && *p != ';' && *p != '{' && *p != '}')
			p++;
		char tok[512];
		size_t l = (size_t)(p - start);
		if (l >= sizeof tok) l = sizeof tok - 1;
		memcpy(tok, start, l);
		tok[l] = '\0';
		char *s = trim(tok);
		/* strip leading list markers */
		while (*s == '-' || *s == '*' || *s == '+') {
			s++;
			while (*s == ' ') s++;
		}
		if (*s == '\0') continue;
		/* strip version comparators */
		char *comp = strpbrk(s, "=<>");
		if (comp && comp > s) *comp = '\0';
		char *t = trim(s);
		if (!*t || decl_is_keyword(t)) continue;
		if (!strs_has(desired, t)) strs_add(desired, t);
	}
}

static int parse_decl_config(config *c, strs *desired) {
	const char *path = decl_config_path(c);
	FILE *f = fopen(path, "r");
	if (!f) {
		error("could not open declarative config %s", path);
		return -1;
	}
	char line[8192];
	int in_pkgs = 0;
	while (fgets(line, sizeof line, f)) {
		char *hash = strchr(line, '#');
		/* keep # inside urls */
		if (hash && (hash == line || (hash > line && isspace((unsigned char)hash[-1])))) *hash = '\0';
		char *s = trim(line);
		if (*s == '\0') continue;
		if (*s == '[') {
			char *end = strchr(s, ']');
			if (end) {
				char section[256];
				size_t len = (size_t)(end - s - 1);
				if (len >= sizeof section) len = sizeof section - 1;
				memcpy(section, s + 1, len);
				section[len] = '\0';
				str_lower(section);
				in_pkgs = (strcmp(section, "packages") == 0 || strcmp(section, "pkgs") == 0 ||
				           strcmp(section, "systempackages") == 0 || strcmp(section, "installed") == 0 ||
				           strcmp(section, "world") == 0);
			}
			continue;
		}
		char *eq = strchr(s, '=');
		char *colon = strchr(s, ':');
		char *sep = NULL;
		if (eq && (!colon || eq < colon)) sep = eq;
		else if (colon) sep = colon;
		if (sep) {
			*sep = '\0';
			char *key = trim(s);
			char *val = trim(sep + 1);
			if (decl_is_pkg_key(key)) {
				in_pkgs = 1;
				if (*val) decl_parse_tokens(val, desired);
			} else {
				in_pkgs = 0;
			}
			continue;
		}
		if (in_pkgs) {
			if (s[0] == '-' || s[0] == '*' || s[0] == '+') decl_parse_tokens(s + 1, desired);
			else decl_parse_tokens(s, desired);
		} else {
			/* flat list of package names */
			decl_parse_tokens(s, desired);
		}
	}
	fclose(f);
	return 0;
}

static void systemctl_cmd(config *c, const char *svc, int user, int enable) {
	char bin[48];
	snprintf(bin, sizeof bin, "systemctl");
	char *argv[8];
	int ai = 0;
	argv[ai++] = bin;
	if (user) argv[ai++] = (char *)"--user";
	argv[ai++] = enable ? (char *)"enable" : (char *)"disable";
	argv[ai++] = (char *)"--now";
	argv[ai++] = (char *)svc;
	argv[ai] = NULL;
	(void)c;
	char *out = NULL;
	run_capture_quiet(argv, &out);
	if (out) free(out);
}

static void apply_service_list(config *c, const char *path, int user) {
	FILE *f = fopen(path, "r");
	if (!f) return;
	char line[8192];
	while (fgets(line, sizeof line, f)) {
		char *hash = strchr(line, '#');
		if (hash) *hash = '\0';
		char *s = trim(line);
		if (!*s) continue;
		char *tok = strtok(s, " \t,;");
		while (tok) {
			if (strstr(tok, "\\n")) {
				char buf[8192];
				snprintf(buf, sizeof buf, "%s", tok);
				char *p = buf;
				while ((p = strstr(p, "\\n")) != NULL) { p[0] = ' '; p[1] = ' '; p += 2; }
				char *t2 = strtok(buf, " \t,;");
				while (t2) {
					if (strstr(t2, ".service")) systemctl_cmd(c, t2, user, 1);
					t2 = strtok(NULL, " \t,;");
				}
			} else if (strstr(tok, ".service")) {
				systemctl_cmd(c, tok, user, 1);
			}
			tok = strtok(NULL, " \t,;");
		}
	}
	fclose(f);
}

static void process_decconfig_file(config *c, const char *path) {
	FILE *f = fopen(path, "r");
	if (!f) return;
	char line[8192];
	char target[4096] = "";
	char mode[64] = "";
	/* content accumulator */
	char *content = NULL;
	size_t clen = 0, ccap = 0;
	int collecting = 0;
	char delim[128] = "<<";
	/* We support:
	   target <path>
	   mode <octal>
	   content <<
	   ...
	   <<
	*/
	while (fgets(line, sizeof line, f)) {
		char *s = trim(line);
		if (!collecting) {
			if (*s == '\0' || *s == '#') continue;
			if (strncmp(s, "target", 6) == 0) {
				char *p = s + 6;
				while (*p == ' ' || *p == '=') p++;
				snprintf(target, sizeof target, "%s", trim(p));
				continue;
			}
			if (strncmp(s, "mode", 4) == 0) {
				char *p = s + 4;
				while (*p == ' ' || *p == '=') p++;
				snprintf(mode, sizeof mode, "%s", trim(p));
				continue;
			}
			if (strncmp(s, "content", 7) == 0) {
				char *p = s + 7;
				while (*p == ' ' || *p == '=') p++;
				if (strncmp(p, "<<", 2) == 0) {
					collecting = 1;
					clen = 0;
					if (content) { free(content); content = NULL; }
					continue;
				}
			}
			continue;
		} else {
			if (strcmp(s, "<<") == 0 || strcmp(s, "<<EOF") == 0) {
				collecting = 0;
				/* write accumulated content to target */
				if (target[0]) {
					char rooted[4096];
					if (target[0] == '~') {
						const char *home = getenv("HOME");
						if (!home || !*home) home = "/";
						if (target[1] == '/')
							snprintf(rooted, sizeof rooted, "%s%s", home, target + 1);
						else
							snprintf(rooted, sizeof rooted, "%s/%s", home, target + 1);
					} else if (c && c->rootdir && strcmp(c->rootdir, "/") != 0) {
						snprintf(rooted, sizeof rooted, "%s/%s", c->rootdir, target[0] == '/' ? target + 1 : target);
					} else {
						snprintf(rooted, sizeof rooted, "%s", target);
					}
					char parent[4096];
					snprintf(parent, sizeof parent, "%s", rooted);
					char *slash = strrchr(parent, '/');
					if (slash) { *slash = '\0'; mkdir_p(parent, 0755); }
					FILE *out = fopen(rooted, "w");
					if (out) {
						if (content) fwrite(content, 1, clen, out);
						fclose(out);
						if (mode[0]) {
							mode_t m = (mode_t)strtol(mode, NULL, 8);
							chmod(rooted, m);
						} else {
							chmod(rooted, 0444);
						}
						msg("wrote %s", rooted);
					} else {
						error("could not write %s", rooted);
					}
				}
				continue;
			}
			/* accumulate line */
			size_t ll = strlen(line);
			if (clen + ll + 2 > ccap) {
				ccap = ccap ? ccap * 2 : 4096;
				while (clen + ll + 2 > ccap) ccap *= 2;
				content = xrealloc(content, ccap);
			}
			memcpy(content + clen, line, ll);
			clen += ll;
			content[clen++] = '\n';
			content[clen] = '\0';
		}
	}
	fclose(f);
	if (content) free(content);
}

static void ensure_service_list(config *c, const char *path, int user) {
	if (is_file(path)) return;
	char *out = NULL;
	char *argv[9];
	int ai = 0;
	argv[ai++] = (char *)"systemctl";
	if (user) argv[ai++] = (char *)"--user";
	argv[ai++] = (char *)"list-unit-files";
	argv[ai++] = (char *)"--type=service";
	argv[ai++] = (char *)"--state=enabled";
	argv[ai++] = (char *)"--no-pager";
	argv[ai++] = (char *)"--no-legend";
	argv[ai] = NULL;
	if (run_capture_quiet(argv, &out) != 0 || !out) return;
	FILE *f = fopen(path, "w");
	if (!f) { free(out); return; }
	char *p = out;
	while (*p) {
		char *nl = strchr(p, '\n');
		size_t l = nl ? (size_t)(nl - p) : strlen(p);
		char line[1024];
		if (l >= sizeof line) l = sizeof line - 1;
		memcpy(line, p, l);
		line[l] = '\0';
		char *tok = strtok(line, " \t");
		if (tok && strstr(tok, ".service")) fprintf(f, "%s\n", tok);
		if (!nl) break;
		p = nl + 1;
	}
	fclose(f);
	free(out);
}

static void apply_decconfig_services(config *c) {
	char svcd[4096];
	if (c && c->rootdir && strcmp(c->rootdir, "/") != 0) snprintf(svcd, sizeof svcd, "%s/nya/services", c->rootdir);
	else snprintf(svcd, sizeof svcd, "/nya/services");
	mkdir_p(svcd, 0755);
	char path[4096];
	snprintf(path, sizeof path, "%s/system.nya", svcd);
	ensure_service_list(c, path, 0);
	snprintf(path, sizeof path, "%s/user.nya", svcd);
	ensure_service_list(c, path, 1);
	char decdir[4096];
	if (c && c->rootdir && strcmp(c->rootdir, "/") != 0) snprintf(decdir, sizeof decdir, "%s/nya/decconfig", c->rootdir);
	else snprintf(decdir, sizeof decdir, "/nya/decconfig");
	mkdir_p(decdir, 0755);
	DIR *d = opendir(decdir);
	if (d) {
		struct dirent *de;
		while ((de = readdir(d)) != NULL) {
			size_t dl = strlen(de->d_name);
			if (dl < 5 || strcmp(de->d_name + dl - 4, ".nya") != 0) continue;
			char p[4096];
			snprintf(p, sizeof p, "%s/%s", decdir, de->d_name);
			process_decconfig_file(c, p);
		}
		closedir(d);
	}
	char subs[2][4096];
	snprintf(subs[0], sizeof subs[0], "%s/system", decdir);
	snprintf(subs[1], sizeof subs[1], "%s/user", decdir);
	int si;
	for (si = 0; si < 2; si++) {
		mkdir_p(subs[si], 0755);
		DIR *sd = opendir(subs[si]);
		if (!sd) continue;
		struct dirent *de;
		while ((de = readdir(sd)) != NULL) {
			size_t dl = strlen(de->d_name);
			if (dl < 5 || strcmp(de->d_name + dl - 4, ".nya") != 0) continue;
			char p[4096];
			snprintf(p, sizeof p, "%s/%s", subs[si], de->d_name);
			process_decconfig_file(c, p);
		}
		closedir(sd);
	}

	char svcdir[4096];
	if (c && c->rootdir && strcmp(c->rootdir, "/") != 0) snprintf(svcdir, sizeof svcdir, "%s/nya/services", c->rootdir);
	else snprintf(svcdir, sizeof svcdir, "/nya/services");
	mkdir_p(svcdir, 0755);
	char p[4096];
	snprintf(p, sizeof p, "%s/user.nya", svcdir);
	apply_service_list(c, p, 1);
	snprintf(p, sizeof p, "%s/system.nya", svcdir);
	apply_service_list(c, p, 0);

	/* well known daemons */
	const char *known[][2] = {
		{"tailscale", "tailscaled"},
		{"docker", "docker"},
		{"openssh", "sshd"},
		{"nginx", "nginx"},
		{"httpd", "httpd"},
		{"mariadb", "mariadb"},
		{"postgresql", "postgresql"},
		{NULL, NULL}
	};
	int i;
	for (i = 0; known[i][0]; i++) {
		char binpath[4096];
		if (c && c->rootdir && strcmp(c->rootdir, "/") != 0)
			snprintf(binpath, sizeof binpath, "%s/usr/bin/%s", c->rootdir, known[i][0]);
		else
			snprintf(binpath, sizeof binpath, "/usr/bin/%s", known[i][0]);
		if (is_file(binpath) || is_file(known[i][0])) {
			systemctl_cmd(c, known[i][1], 0, 1);
		}
	}
}

int do_declarative(config *c) {
	const char *cfgpath = decl_config_path(c);
	if (!is_file(cfgpath)) {
		const char *wd = decl_world_dir(c);
		char wpath[4096];
		snprintf(wpath, sizeof wpath, "%s/packages", wd);
		if (!is_file(wpath)) {
			error("declarative config %s not found (create it, or run 'nya --readpkg' first)", cfgpath);
			return 1;
		}
		mkdir_p(c->rootdir, 0755);
		char parent[4096];
		snprintf(parent, sizeof parent, "%s", cfgpath);
		char *slash = strrchr(parent, '/');
		if (slash) { *slash = '\0'; mkdir_p(parent, 0755); }
		FILE *src = fopen(wpath, "r");
		FILE *dst = fopen(cfgpath, "w");
		if (!src || !dst) {
			if (src) fclose(src);
			if (dst) fclose(dst);
			error("could not create %s", cfgpath);
			return 1;
		}
		char buf[8192];
		size_t n;
		while ((n = fread(buf, 1, sizeof buf, src)) > 0) fwrite(buf, 1, n, dst);
		fclose(src);
		fclose(dst);
		msg("generated %s from world packages", cfgpath);
	}
	strs desired;
	memset(&desired, 0, sizeof desired);
	if (parse_decl_config(c, &desired) != 0) {
		strs_free(&desired);
		return 1;
	}
	if (desired.n == 0) {
		msg("no packages declared; running --makeconf to seed config.nya from this system");
		do_makeconf(c);
		strs_free(&desired);
		memset(&desired, 0, sizeof desired);
		parse_decl_config(c, &desired);
	}
	if (desired.n == 0) {
		warn("no packages declared in %s; skipping install/removal", cfgpath);
		strs_free(&desired);
		return 0;
	}

	const char *wd = decl_world_dir(c);
	mkdir_p(wd, 0755);
	char wpath[4096];
	snprintf(wpath, sizeof wpath, "%s/packages", wd);
	FILE *wf = fopen(wpath, "w");
	if (wf) {
		int i;
		for (i = 0; i < desired.n; i++) fprintf(wf, "%s\n", desired.v[i]);
		fclose(wf);
	}
	/* keep existing dependency info; do not truncate /nya/world/depends */

	/* install missing desired packages */
	strs missing;
	memset(&missing, 0, sizeof missing);
	int i;
	for (i = 0; i < desired.n; i++) {
		pkg *l = db_find_local(desired.v[i]);
		if (!l) {
			int found = 0;
			int j;
			for (j = 0; j < g_nlocal; j++) {
				if (g_local[j]->base && strcmp(g_local[j]->base, desired.v[i]) == 0) {
					found = 1;
					break;
				}
			}
			if (!found) strs_add(&missing, desired.v[i]);
		}
	}
	int rc = 0;
	strs saved_order = c->pkgorder;
	memset(&c->pkgorder, 0, sizeof c->pkgorder);
	strs_add(&c->pkgorder, "repos");
	if (missing.n > 0) {
		txn t;
		txn_init(&t, c);
		if (txn_build_install(c, (const char **)missing.v, missing.n, &t, NULL) != 0) {
			txn_free(&t);
			strs_free(&missing);
			strs_free(&desired);
			strs_free(&c->pkgorder);
			c->pkgorder = saved_order;
			return 1;
		}
		if (t.nadd > 0) {
			rc = txn_run(c, &t, 0);
		}
		txn_free(&t);
	}
	strs_free(&c->pkgorder);
	c->pkgorder = saved_order;
	strs_free(&missing);

	if (rc == 0 && g_prune) {
		int rc2 = do_remove_unneeded(c);
		if (rc2 != 0) rc = rc2;
	} else if (rc == 0) {
		msg("skipping removal of unlisted packages (use 'nya rebuild --prune' to prune)");
	}

	strs_free(&desired);
	apply_decconfig_services(c);
	msg("declarative apply complete mreow~");
	return rc;
}

int do_makeconf(config *c) {
	char nyadir[4096];
	if (c && c->rootdir && strcmp(c->rootdir, "/") != 0) snprintf(nyadir, sizeof nyadir, "%s/nya", c->rootdir);
	else snprintf(nyadir, sizeof nyadir, "/nya");
	mkdir_p(nyadir, 0755);
	mkdir_p(decl_world_dir(c), 0755);
	if (do_read_pkg(c) != 0) {
		warn("could not read installed packages; continuing");
	}

	/* convert installed (non-core) packages to config.nya */
	char cfgpath[4096];
	snprintf(cfgpath, sizeof cfgpath, "%s/config.nya", nyadir);
	strs seen;
	memset(&seen, 0, sizeof seen);
	FILE *dst = fopen(cfgpath, "w");
	if (dst) {
		fprintf(dst, "# Declarative package list (generated by nya --makeconf)\n");
		fprintf(dst, "# Core/system packages are intentionally omitted\n");
		fprintf(dst, "packages:\n");
		int i;
		for (i = 0; i < g_nlocal; i++) {
			pkg *p = g_local[i];
			if (!p || !p->name) continue;
			if (p->repo) {
				if (str_ieq(p->repo, "core") || str_ieq(p->repo, "core-testing") ||
				    str_ieq(p->repo, "core-debug") || str_ieq(p->repo, "core-staging"))
					continue;
			}
					/* include every installed package outside the core repos so --makeconf never trims a live system */
			const char *nm = (p->base && *p->base) ? p->base : p->name;
			if (strs_has(&seen, nm)) continue;
			strs_add(&seen, nm);
			fprintf(dst, "  - %s\n", nm);
		}
		fclose(dst);
		msg("wrote %s", cfgpath);
	}
	strs_free(&seen);

	/* service lists */
	char svcdir[4096];
	snprintf(svcdir, sizeof svcdir, "%s/services", nyadir);
	mkdir_p(svcdir, 0755);
	char spath[4096];
	char *out = NULL;
	char *argv_sys[] = { "systemctl", "list-unit-files", "--type=service", "--state=enabled", "--no-pager", "--no-legend", NULL };
	if (run_capture_quiet(argv_sys, &out) == 0 && out) {
		snprintf(spath, sizeof spath, "%s/system.nya", svcdir);
		FILE *f = fopen(spath, "w");
		if (f) {
			char *p = out;
			while (*p) {
				char *nl = strchr(p, '\n');
				size_t l = nl ? (size_t)(nl - p) : strlen(p);
				char line[1024];
				if (l >= sizeof line) l = sizeof line - 1;
				memcpy(line, p, l);
				line[l] = '\0';
				char *tok = strtok(line, " \\t");
				if (tok && strstr(tok, ".service")) {
					fprintf(f, "%s\n", tok);
				}
				if (!nl) break;
				p = nl + 1;
			}
			fclose(f);
			msg("wrote %s", spath);
		}
		free(out);
		out = NULL;
	}
	char *argv_user[] = { "systemctl", "--user", "list-unit-files", "--type=service", "--state=enabled", "--no-pager", "--no-legend", NULL };
	if (run_capture_quiet(argv_user, &out) == 0 && out) {
		snprintf(spath, sizeof spath, "%s/user.nya", svcdir);
		FILE *f = fopen(spath, "w");
		if (f) {
			char *p = out;
			while (*p) {
				char *nl = strchr(p, '\n');
				size_t l = nl ? (size_t)(nl - p) : strlen(p);
				char line[1024];
				if (l >= sizeof line) l = sizeof line - 1;
				memcpy(line, p, l);
				line[l] = '\0';
				char *tok = strtok(line, " \\t");
				if (tok && strstr(tok, ".service")) {
					fprintf(f, "%s\n", tok);
				}
				if (!nl) break;
				p = nl + 1;
			}
			fclose(f);
			msg("wrote %s", spath);
		}
		free(out);
		out = NULL;
	}

	/* example decconfig template */
	char decdir[4096];
	snprintf(decdir, sizeof decdir, "%s/decconfig", nyadir);
	mkdir_p(decdir, 0755);
	char example[4096];
	snprintf(example, sizeof example, "%s/example.nya", decdir);
	if (!is_file(example)) {
		FILE *e = fopen(example, "w");
		if (e) {
			fprintf(e, "# declarative config example\\n");
			fprintf(e, "target /etc/example.conf\\n");
			fprintf(e, "content <<\\n");
			fprintf(e, "example content\\n");
			fprintf(e, "<<\\n");
			fprintf(e, "mode 0644\\n");
			fclose(e);
		}
	}

	msg("makeconf complete mreow~: review %s", nyadir);
	return 0;
}
