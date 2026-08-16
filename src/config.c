#include "nya.h"

void repo_free(repo *r) {
	int i;
	if (!r) return;
	for (i = 0; i < r->servers.n; i++) free(r->servers.v[i]);
	free(r->servers.v);
	free(r->name);
	free(r);
}

void config_free(config *c) {
	int i;
	if (!c) return;
	free(c->path);
	free(c->rootdir);
	free(c->dbpath);
	free(c->logfile);
	free(c->arch);
	free(c->nyacache);
	free(c->nixchannel);
	free(c->aurbase);
	free(c->sudobin);
	free(c->hostsrepo);
	strs_free(&c->cachedirs);
	strs_free(&c->holdpkg);
	strs_free(&c->ignorepkg);
	strs_free(&c->ignoregrp);
	strs_free(&c->siglevel);
	strs_free(&c->noupgrade);
	for (i = 0; i < c->nrepos; i++) repo_free(c->repos[i]);
	free(c->repos);
	free(c);
}

config *config_alloc(void) {
	config *c = xcalloc(1, sizeof *c);
	c->rootdir = xstrdup("/");
	c->dbpath = xstrdup("/var/lib/pacman");
	c->logfile = xstrdup("/var/log/nya.log");
	c->arch = xstrdup("auto");
	c->nyacache = xstrdup("/var/cache/nya");
	strs_add(&c->cachedirs, "/var/cache/pacman/pkg");
	c->nixchannel = xstrdup("nixpkgs-unstable");
	c->aurbase = xstrdup("https://aur.archlinux.org");
	c->sudobin = xstrdup("sudo");
	c->hostsrepo = xstrdup("https://adachippp.github.io/nya-hosts");
	c->parallel = 5;
	c->color = 1;
	c->searchaur = c->searchnix = c->searchflatpak = c->searchhost = -1;
	return c;
}

static repo *config_repo(config *c, const char *name) {
	int i;
	for (i = 0; i < c->nrepos; i++) {
		if (strcmp(c->repos[i]->name, name) == 0) return c->repos[i];
	}
	return NULL;
}

repo *config_find_repo(config *c, const char *name) {
	return config_repo(c, name);
}

static void config_add_repo(config *c, const char *name) {
	if (config_repo(c, name)) return;
	c->repos = xrealloc(c->repos, (c->nrepos + 1) * sizeof(repo *));
	repo *r = xcalloc(1, sizeof *r);
	r->name = xstrdup(name);
	c->repos[c->nrepos++] = r;
}

static void cfg_add_str(strs *s, const char *val) {
	strs tmp;
	memset(&tmp, 0, sizeof tmp);
	strs_split_ws(val, &tmp);
	int i;
	for (i = 0; i < tmp.n; i++) {
		if (!strs_has(s, tmp.v[i])) strs_add(s, tmp.v[i]);
	}
	strs_free(&tmp);
}

static void config_apply_key(config *c, repo *r, const char *key, const char *val) {
	if (!r) {
		if (str_ieq(key, "RootDir")) {
			free(c->rootdir);
			c->rootdir = xstrdup(val);
		} else if (str_ieq(key, "DBPath")) {
			free(c->dbpath);
			c->dbpath = xstrdup(val);
		} else if (str_ieq(key, "LogFile")) {
			free(c->logfile);
			c->logfile = xstrdup(val);
		} else if (str_ieq(key, "CacheDir")) {
			if (!strs_has(&c->cachedirs, val)) strs_add(&c->cachedirs, val);
		} else if (str_ieq(key, "Architecture")) {
			free(c->arch);
			c->arch = xstrdup(val);
		} else if (str_ieq(key, "HoldPkg") || str_ieq(key, "IgnorePkg") || str_ieq(key, "IgnoreGroup") ||
		           str_ieq(key, "NoUpgrade") || str_ieq(key, "SigLevel")) {
			strs *dst = NULL;
			if (str_ieq(key, "HoldPkg")) dst = &c->holdpkg;
			else if (str_ieq(key, "IgnorePkg")) dst = &c->ignorepkg;
			else if (str_ieq(key, "IgnoreGroup")) dst = &c->ignoregrp;
			else if (str_ieq(key, "NoUpgrade")) dst = &c->noupgrade;
			else dst = &c->siglevel;
			cfg_add_str(dst, val);
		} else if (str_ieq(key, "ParallelDownloads")) {
			c->parallel = atoi(val);
		} else if (str_ieq(key, "aur")) {
			c->aur = str_ieq(val, "true") || str_ieq(val, "yes") || atoi(val) > 0;
		} else if (str_ieq(key, "nix")) {
			c->nix = str_ieq(val, "true") || str_ieq(val, "yes") || atoi(val) > 0;
		} else if (str_ieq(key, "searchaur")) {
			c->searchaur = str_ieq(val, "true") || str_ieq(val, "yes") || atoi(val) > 0;
		} else if (str_ieq(key, "searchnix")) {
			c->searchnix = str_ieq(val, "true") || str_ieq(val, "yes") || atoi(val) > 0;
	} else if (str_ieq(key, "searchflatpak")) {
		c->searchflatpak = str_ieq(val, "true") || str_ieq(val, "yes") || atoi(val) > 0;
	} else if (str_ieq(key, "searchhost")) {
		c->searchhost = str_ieq(val, "true") || str_ieq(val, "yes") || atoi(val) > 0;
	} else if (str_ieq(key, "aurfirst")) {
		c->aurfirst = str_ieq(val, "true") || str_ieq(val, "yes") || atoi(val) > 0;
	} else if (str_ieq(key, "NixChannel")) {
			free(c->nixchannel);
			c->nixchannel = xstrdup(val);
		} else if (str_ieq(key, "AurBase")) {
			free(c->aurbase);
			c->aurbase = xstrdup(val);
	} else if (str_ieq(key, "sudobin")) {
		free(c->sudobin);
		c->sudobin = xstrdup(val);
	} else if (str_ieq(key, "hostsrepo")) {
		free(c->hostsrepo);
		c->hostsrepo = xstrdup(val);
	} else if (str_ieq(key, "NyaCacheDir")) {
			free(c->nyacache);
			c->nyacache = xstrdup(val);
		}
	} else if (str_ieq(key, "Server")) {
		if (!strs_has(&r->servers, val)) strs_add(&r->servers, val);
	}
}

static void config_apply_flag(config *c, repo *r, const char *name) {
	if (r) return;
	if (str_ieq(name, "Color")) c->color = 1;
	else if (str_ieq(name, "NoColor")) c->color = 0;
	else if (str_ieq(name, "VerbosePkgLists")) c->verbosepkglists = 1;
	else if (str_ieq(name, "CheckSpace")) {
	} else if (str_ieq(name, "ILoveCandy")) {
	}
}

static int parse_config_file(config *c, const char *path, int depth, repo *cur) {
	if (depth > 8) {
		error("config include depth exceeded at %s", path);
		return -1;
	}
	FILE *f = fopen(path, "r");
	if (!f) return -1;
	char line[4096];
	while (fgets(line, sizeof line, f)) {
		char *s = trim(line);
		if (*s == '\0') continue;
		if (*s == '#') continue;
		if (*s == '[') {
			char *end = strchr(s, ']');
			if (!end) continue;
			*end = '\0';
			char *name = trim(s + 1);
			if (str_ieq(name, "options")) {
				cur = NULL;
			} else {
				config_add_repo(c, name);
				cur = config_repo(c, name);
			}
			continue;
		}
		char *eq = strchr(s, '=');
		if (eq) {
			*eq = '\0';
			char *key = trim(s);
			char *val = trim(eq + 1);
			if (str_ieq(key, "Include")) {
				char inc[4096];
				if (val[0] == '/') snprintf(inc, sizeof inc, "%s", val);
				else {
					const char *slash = strrchr(path, '/');
					if (slash) {
						size_t dirlen = slash - path;
						snprintf(inc, sizeof inc, "%.*s/%s", (int)dirlen, path, val);
					} else snprintf(inc, sizeof inc, "%s", val);
				}
				parse_config_file(c, inc, depth + 1, cur);
				continue;
			}
			config_apply_key(c, cur, key, val);
		} else {
			config_apply_flag(c, cur, s);
		}
	}
	fclose(f);
	return 0;
}

int config_resolve_arch(config *c) {
	char buf[256];
	strs ws;
	memset(&ws, 0, sizeof ws);
	strs_split_ws(c->arch, &ws);
	if (ws.n == 0) {
		strs_free(&ws);
		return -1;
	}
	const char *a = ws.v[0];
	if (str_ieq(a, "auto")) {
		FILE *p = fopen("/proc/sys/kernel/arch", "r");
		if (p) {
			if (fgets(buf, sizeof buf, p)) {
				trim(buf);
				if (str_ieq(buf, "x86_64")) snprintf(buf, sizeof buf, "x86_64");
				else if (str_ieq(buf, "i386") || str_ieq(buf, "i486") || str_ieq(buf, "i586") || str_ieq(buf, "i686"))
					snprintf(buf, sizeof buf, "i686");
				else if (str_ieq(buf, "aarch64")) snprintf(buf, sizeof buf, "aarch64");
				else if (str_ieq(buf, "armv6l")) snprintf(buf, sizeof buf, "armv6h");
				else if (str_ieq(buf, "armv7l")) snprintf(buf, sizeof buf, "armv7h");
				else if (str_ieq(buf, "ppc64le")) snprintf(buf, sizeof buf, "ppc64le");
				else if (str_ieq(buf, "riscv64")) snprintf(buf, sizeof buf, "riscv64");
				else if (str_ieq(buf, "loongarch64")) snprintf(buf, sizeof buf, "loongarch64");
				else if (str_ieq(buf, "s390x")) snprintf(buf, sizeof buf, "s390x");
			}
			fclose(p);
		} else {
			snprintf(buf, sizeof buf, "x86_64");
		}
		free(c->arch);
		c->arch = xstrdup(buf);
	} else {
		free(c->arch);
		c->arch = xstrdup(a);
	}
	strs_free(&ws);
	return 0;
}

static char *subst_server(const char *s, const char *repo, const char *arch) {
	char out[4096];
	size_t o = 0;
	const char *p = s;
	while (*p && o + 1 < sizeof out) {
		if (*p == '$') {
			if (strncmp(p, "$repo", 5) == 0) {
				o += snprintf(out + o, sizeof out - o, "%s", repo);
				p += 5;
				continue;
			}
			if (strncmp(p, "$arch", 5) == 0) {
				o += snprintf(out + o, sizeof out - o, "%s", arch);
				p += 5;
				continue;
			}
		}
		out[o++] = *p++;
	}
	out[o] = '\0';
	return xstrdup(out);
}

static void config_finalize(config *c) {
	int i;
	for (i = 0; i < c->nrepos; i++) {
		repo *r = c->repos[i];
		strs resolved;
		memset(&resolved, 0, sizeof resolved);
		int j;
		for (j = 0; j < r->servers.n; j++) {
			strs_add_own(&resolved, subst_server(r->servers.v[j], r->name, c->arch));
		}
		strs_free(&r->servers);
		r->servers = resolved;
	}
}

config *config_load(const char *path, int *generated) {
	config *c = config_alloc();
	if (generated) *generated = 0;
	if (!path) path = "/etc/nya.conf";
	c->path = xstrdup(path);
	if (is_file(path)) {
		if (parse_config_file(c, path, 0, NULL) != 0) {
			error("could not read config file %s", path);
			config_free(c);
			return NULL;
		}
	} else {
		config *pac = config_alloc();
		if (config_read_pacman(pac, "/etc/pacman.conf") == 0) {
			config_write(path, pac, 1);
			if (generated) *generated = 1;
			msg("%sgenerated %s from /etc/pacman.conf%s", col_green(), path, col_reset());
		} else {
			config_write(path, pac, 1);
			if (generated) *generated = 1;
			warn("no /etc/pacman.conf found; wrote default config to %s", path);
		}
		config_free(pac);
		parse_config_file(c, path, 0, NULL);
	}
	config_resolve_arch(c);
	if (c->cachedirs.n == 0) strs_add(&c->cachedirs, "/var/cache/pacman/pkg");
	config_finalize(c);
	return c;
}

config *config_discover(void) {
	const char *env = getenv("NYA_CONF");
	char path[4096];
	const char *home = getenv("HOME");
	if (env && *env) {
		return config_load(env, NULL);
	}
	if (is_file("./nya.conf")) {
		return config_load("./nya.conf", NULL);
	}
	if (home) {
		snprintf(path, sizeof path, "%s/.config/nya/nya.conf", home);
		if (is_file(path)) return config_load(path, NULL);
	}
	return config_load("/etc/nya.conf", NULL);
}

const char *cfg_rooted(const config *c, const char *path) {
	static __thread char buf[4096];
	if (!path) return NULL;
	if (strcmp(c->rootdir, "/") == 0) return path;
	if (startswith(path, c->rootdir) && c->rootdir[1] != '\0') {
		snprintf(buf, sizeof buf, "%s", path);
		return buf;
	}
	if (path[0] == '/' && c->rootdir[1] == '\0') {
		snprintf(buf, sizeof buf, "%s", path);
		return buf;
	}
	snprintf(buf, sizeof buf, "%s/%s", c->rootdir, path[0] == '/' ? path + 1 : path);
	return buf;
}

static void write_options(FILE *f, config *c, int from_pac) {
	int i;
	fprintf(f, "[options]\n");
	fprintf(f, "RootDir = %s\n", c->rootdir);
	fprintf(f, "DBPath = %s\n", c->dbpath);
	for (i = 0; i < c->cachedirs.n; i++) fprintf(f, "CacheDir = %s\n", c->cachedirs.v[i]);
	if (c->logfile && *c->logfile) fprintf(f, "LogFile = %s\n", c->logfile);
	fprintf(f, "Architecture = %s\n", c->arch);
	if (c->siglevel.n) {
		fprintf(f, "SigLevel =");
		for (i = 0; i < c->siglevel.n; i++) fprintf(f, " %s", c->siglevel.v[i]);
		fprintf(f, "\n");
	}
	if (c->holdpkg.n) {
		fprintf(f, "HoldPkg =");
		for (i = 0; i < c->holdpkg.n; i++) fprintf(f, " %s", c->holdpkg.v[i]);
		fprintf(f, "\n");
	}
	if (c->ignorepkg.n) {
		fprintf(f, "IgnorePkg =");
		for (i = 0; i < c->ignorepkg.n; i++) fprintf(f, " %s", c->ignorepkg.v[i]);
		fprintf(f, "\n");
	}
	if (c->ignoregrp.n) {
		fprintf(f, "IgnoreGroup =");
		for (i = 0; i < c->ignoregrp.n; i++) fprintf(f, " %s", c->ignoregrp.v[i]);
		fprintf(f, "\n");
	}
	if (c->noupgrade.n) {
		fprintf(f, "NoUpgrade =");
		for (i = 0; i < c->noupgrade.n; i++) fprintf(f, " %s", c->noupgrade.v[i]);
		fprintf(f, "\n");
	}
	fprintf(f, "ParallelDownloads = %d\n", c->parallel);
	fprintf(f, "Color\n");
	if (c->verbosepkglists) fprintf(f, "VerbosePkgLists\n");
	fprintf(f, "NyaCacheDir = %s\n", c->nyacache);
	fprintf(f, "NixChannel = %s\n", c->nixchannel);
	fprintf(f, "AurBase = %s\n", c->aurbase);
	fprintf(f, "#aur = true\n");
	fprintf(f, "#nix = true\n");
	fprintf(f, "#searchaur = true\n");
	fprintf(f, "#searchnix = true\n");
	fprintf(f, "#searchflatpak = true\n");
	fprintf(f, "#searchhost = true\n");
	fprintf(f, "#sudobin = sudo\n");
	fprintf(f, "#hostsrepo = https://adachippp.github.io/nya-hosts\n");
	fprintf(f, "#install priority: repos -> hosts -> aur -> flatpak (set 'aurfirst = true' for aur -> hosts)\n");
	fprintf(f, "#aurfirst = true\n");
	(void)from_pac;
}

int config_write(const char *path, config *c, int from_pac) {
	char tmp[4096];
	snprintf(tmp, sizeof tmp, "%s.nya-tmp", path);
	FILE *f = fopen(tmp, "w");
	if (!f) return -1;
	fprintf(f, "#\n# nya.conf\n");
	fprintf(f, "# pacman-compatible package manager configuration\n");
	if (from_pac) fprintf(f, "# generated from /etc/pacman.conf; rerun 'nya --read-paconfig' to resync\n");
	fprintf(f, "#\n");
	write_options(f, c, from_pac);
	int i, j;
	for (i = 0; i < c->nrepos; i++) {
		repo *r = c->repos[i];
		fprintf(f, "\n[%s]\n", r->name);
		for (j = 0; j < r->servers.n; j++) {
			fprintf(f, "Server = %s\n", r->servers.v[j]);
		}
	}
	fclose(f);
	rename(tmp, path);
	return 0;
}

int config_read_pacman(config *c, const char *pacpath) {
	if (!is_file(pacpath)) return -1;
	parse_config_file(c, pacpath, 0, NULL);
	return 0;
}
