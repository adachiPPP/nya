#include "nya.h"
#include <pwd.h>
#include <ftw.h>

static char *url_encode(const char *s) {
	static const char hex[] = "0123456789ABCDEF";
	char *out = xmalloc(strlen(s) * 3 + 1);
	char *p = out;
	while (*s) {
		unsigned char c = *s;
		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') *p++ = c;
		else {
			*p++ = '%';
			*p++ = hex[c >> 4];
			*p++ = hex[c & 15];
		}
		s++;
	}
	*p = '\0';
	return out;
}

static int aur_enabled(config *c) {
	if (!c->aur) {
		error("AUR support is disabled (set 'aur = true' in %s)", c->path);
		return 0;
	}
	return 1;
}

#define MALWARE_URL "https://raw.githubusercontent.com/lenucksi/aur-malware-check/master/data/campaigns/aur-infected/packages.txt"

static hmap *g_malware = NULL;
static int g_malware_state = 0; /* 0 = not tried, 1 = loaded, 2 = failed */

static void malware_load(config *c) {
	char *data;
	long len;
	if (dl_url_quick(c, MALWARE_URL, &data, &len, 15) != 0) {
		g_malware_state = 2;
		return;
	}
	g_malware = hmap_new(2048);
	char *line = data;
	while (line && *line) {
		char *nl = strchr(line, '\n');
		if (nl) *nl = '\0';
		trim(line);
		if (*line) hmap_put(g_malware, line, (void *)1);
		if (!nl) break;
		line = nl + 1;
	}
	free(data);
	g_malware_state = 1;
}

int aur_malware_check(config *c, const char *name) {
	if (g_malware_state == 0) malware_load(c);
	if (g_malware_state == 1 && hmap_has(g_malware, name)) return 1;
	return 0;
}

static void print_aur_pkg(config *c, json *r) {
	const char *name = json_str(json_get(r, "Name"));
	if (!name) return;
	const char *ver = json_str(json_get(r, "Version"));
	const char *desc = json_str(json_get(r, "Description"));
	json *ood = json_get(r, "OutOfDate");
	json *votes = json_get(r, "NumVotes");
	json *pop = json_get(r, "Popularity");
	int installed = db_find_local(name) != NULL;
	int bad = aur_malware_check(c, name);
	printf("%saur/%s %s%s%s%s%s", col_magenta(), name, col_reset(), col_bold(), ver ? ver : "", col_reset(),
	       installed ? " [installed]" : "");
	if (bad) printf(" %s[MALWARE]%s", col_red(), col_reset());
	if (ood && ood->type != J_NULL) printf(" (out-of-date)");
	if (votes && votes->type == J_NUM) printf(" [%lld votes]", (long long)votes->num);
	printf("\n");
	if (desc) printf("    %s\n", desc);
	(void)pop;
}

static hmap *repo_name_set(void) {
	hmap *m = hmap_new(1 << 16);
	int i;
	for (i = 0; i < g_nsync; i++) {
		if (g_sync[i]->name) hmap_put(m, g_sync[i]->name, g_sync[i]);
	}
	return m;
}

int aur_search(config *c, const char *term, int quiet) {
	if (!aur_enabled(c)) return -1;
	char *enc = url_encode(term);
	char url[4600];
	snprintf(url, sizeof url, "%s/rpc/v5/search/%s", c->aurbase, enc);
	free(enc);
	char *data;
	long len;
	if (dl_url(c, url, &data, &len) != 0) {
		error("failed to query the AUR");
		return -1;
	}
	json *root = json_parse(data, len);
	free(data);
	if (!root) {
		error("invalid response from the AUR");
		return -1;
	}
	hmap *repos = repo_name_set();
	json *results = json_get(root, "results");
	int count = 0;
	json *r;
	for (r = results ? results->child : NULL; r; r = r->next) {
		const char *name = json_str(json_get(r, "Name"));
		if (name && hmap_has(repos, name)) continue; /* configured repos take priority */
		print_aur_pkg(c, r);
		count++;
	}
	hmap_free(repos);
	json_free(root);
	if (count == 0 && !quiet) msg("no AUR packages found matching '%s'", term);
	return count;
}

static int aur_search_multi_core(config *c, const char **terms, int n, int check) {
	if (check && !aur_enabled(c)) return -1;
	if (check && n == 1) return aur_search(c, terms[0], 0);
	json **roots = xcalloc(n, sizeof *roots);
	strs *sets = xcalloc(n, sizeof *sets);
	int i;
	int ok = 1;
	for (i = 0; i < n; i++) {
		char *enc = url_encode(terms[i]);
		char url[4600];
		snprintf(url, sizeof url, "%s/rpc/v5/search/%s", c->aurbase, enc);
		free(enc);
		char *data;
		long len;
		if (dl_url(c, url, &data, &len) != 0) {
			error("failed to query the AUR");
			ok = 0;
			break;
		}
		json *root = json_parse(data, len);
		free(data);
		if (!root) {
			error("invalid response from the AUR");
			ok = 0;
			break;
		}
		roots[i] = root;
		json *results = json_get(root, "results");
		json *r;
		for (r = results ? results->child : NULL; r; r = r->next) {
			const char *nm = json_str(json_get(r, "Name"));
			if (nm) strs_add(&sets[i], nm);
		}
	}
	int count = 0;
	if (ok) {
		hmap *repos = repo_name_set();
		json *results0 = json_get(roots[0], "results");
		json *r;
		for (r = results0 ? results0->child : NULL; r; r = r->next) {
			const char *nm = json_str(json_get(r, "Name"));
			if (!nm) continue;
			if (hmap_has(repos, nm)) continue; /* configured repos take priority */
			int inall = 1;
			for (i = 1; i < n && inall; i++) {
				if (!strs_has(&sets[i], nm)) inall = 0;
			}
			if (inall) {
				print_aur_pkg(c, r);
				count++;
			}
		}
		hmap_free(repos);
		if (count == 0) msg("no AUR packages found matching the search terms");
	}
	for (i = 0; i < n; i++) {
		if (roots[i]) json_free(roots[i]);
		strs_free(&sets[i]);
	}
	free(roots);
	free(sets);
	return ok ? count : -1;
}

int aur_search_multi(config *c, const char **terms, int n) {
	return aur_search_multi_core(c, terms, n, 1);
}

int aur_search_any(config *c, const char **terms, int n) {
	return aur_search_multi_core(c, terms, n, 0);
}

int aur_info(config *c, const char *name) {
	if (!aur_enabled(c)) return -1;
	char *enc = url_encode(name);
	char url[4600];
	snprintf(url, sizeof url, "%s/rpc/v5/info/%s", c->aurbase, enc);
	free(enc);
	char *data;
	long len;
	if (dl_url(c, url, &data, &len) != 0) {
		error("failed to query the AUR");
		return -1;
	}
	json *root = json_parse(data, len);
	free(data);
	if (!root) {
		error("invalid response from the AUR");
		return -1;
	}
	json *results = json_get(root, "results");
	int found = 0;
	json *r;
	for (r = results ? results->child : NULL; r; r = r->next) {
		const char *n = json_str(json_get(r, "Name"));
		if (!n) continue;
		int bad = aur_malware_check(c, n);
		printf("%saur/%s%s %s%s%s", col_magenta(), n, col_reset(), col_bold(),
		       json_str(json_get(r, "Version")) ? json_str(json_get(r, "Version")) : "", col_reset());
		if (bad) printf(" %s[MALWARE]%s", col_red(), col_reset());
		printf("\n");
		const char *v = json_str(json_get(r, "Version"));
		const char *d = json_str(json_get(r, "Description"));
		const char *u = json_str(json_get(r, "URL"));
		const char *m = json_str(json_get(r, "Maintainer"));
		json *votes = json_get(r, "NumVotes");
		json *pop = json_get(r, "Popularity");
		json *ood = json_get(r, "OutOfDate");
		if (v) printf("  Version         : %s\n", v);
		if (d) printf("  Description     : %s\n", d);
		if (u) printf("  URL             : %s\n", u);
		if (m) printf("  Maintainer      : %s\n", m);
		if (votes && votes->type == J_NUM) printf("  Votes           : %lld\n", (long long)votes->num);
		if (pop && pop->type == J_NUM) printf("  Popularity      : %.2f\n", pop->num);
		if (ood && ood->type != J_NULL) printf("  Out-of-date     : yes\n");
		pkg *l = db_find_local(n);
		if (l) printf("  Installed       : %s\n", l->version);
		printf("\n");
		found = 1;
	}
	json_free(root);
	if (!found) {
		error("AUR package '%s' was not found", name);
		return -1;
	}
	return 0;
}

static int extract_snapshot(const char *tarball, const char *dir) {
	rd *r = rd_open_compressed(tarball);
	if (!r) return -1;
	tar_it t;
	tar_init(&t, r);
	tar_entry e;
	while (tar_next(&t, &e) > 0) {
		char clean[4096];
		if (tar_safe_path(e.name, clean, sizeof clean) != 0) {
			tar_skip(&t);
			continue;
		}
		char *slash = strchr(clean, '/');
		const char *sub = slash ? slash + 1 : clean;
		if (!*sub) {
			tar_skip(&t);
			continue;
		}
		char dest[4600];
		snprintf(dest, sizeof dest, "%s/%s", dir, sub);
		if (e.type == '5') {
			mkdir_p(dest, e.mode & 07777);
			tar_skip(&t);
		} else if (e.type == '0') {
			int fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, e.mode & 0777);
			if (fd < 0) {
				tar_skip(&t);
				continue;
			}
			char buf[65536];
			long long remain = e.size;
			while (remain > 0) {
				long n = remain > (long long)sizeof buf ? (long long)sizeof buf : remain;
				long got = tar_read(&t, buf, n);
				if (got <= 0) break;
				long off = 0;
				while (off < got) {
					ssize_t w = write(fd, buf + off, got - off);
					if (w < 0) {
						if (errno == EINTR) continue;
						break;
					}
					off += w;
				}
				remain -= got;
			}
			close(fd);
		} else {
			tar_skip(&t);
		}
	}
	rd_close(r);
	return 0;
}

static int makepkg_available(void) {
	return access("/usr/bin/makepkg", X_OK) == 0 || access("/bin/makepkg", X_OK) == 0 ||
	       access("/usr/local/bin/makepkg", X_OK) == 0;
}

static void build_dir_for(const char *user, char *out, size_t n) {
	if (user) {
		struct passwd *pw = getpwnam(user);
		if (pw && pw->pw_dir && *pw->pw_dir) {
			snprintf(out, n, "%s/.cache/nya/aur-build", pw->pw_dir);
			return;
		}
	}
	const char *home = getenv("HOME");
	if (home && *home) {
		snprintf(out, n, "%s/.cache/nya/aur-build", home);
		return;
	}
	snprintf(out, n, "/tmp/nya-aur-%ld", (long)geteuid());
}

static uid_t g_cuid;
static gid_t g_cgid;

static int chown_cb(const char *path, const struct stat *st, int type, struct FTW *ftw) {
	(void)st;
	(void)type;
	(void)ftw;
	chown(path, g_cuid, g_cgid);
	return 0;
}

static void chown_r(const char *path, uid_t uid, gid_t gid) {
	g_cuid = uid;
	g_cgid = gid;
	nftw(path, chown_cb, 32, FTW_PHYS);
}

int aur_pkg_exists(config *c, const char *name) {
	if (!aur_enabled(c)) return 0;
	char *enc = url_encode(name);
	char url[4600];
	snprintf(url, sizeof url, "%s/rpc/v5/info/%s", c->aurbase, enc);
	free(enc);
	char *data;
	long len;
	if (dl_url(c, url, &data, &len) != 0) return 1; /* network issue: be optimistic, let download retries handle it */
	json *root = json_parse(data, len);
	free(data);
	if (!root) return 1;
	json *results = json_get(root, "results");
	int found = 0;
	json *r;
	for (r = results ? results->child : NULL; r; r = r->next) {
		const char *n = json_str(json_get(r, "Name"));
		if (n && strcmp(n, name) == 0) {
			found = 1;
			break;
		}
	}
	json_free(root);
	return found;
}

int aur_build_install(config *c, const char *name, txn *t) {
	if (!aur_enabled(c)) return -1;
	if (aur_malware_check(c, name)) {
		error("refusing to install '%s': listed as malware in aur-malware-check (https://github.com/lenucksi/aur-malware-check)", name);
		return -1;
	}
	if (!aur_pkg_exists(c, name)) {
		warn("%s: not found in the AUR", name);
		return -1;
	}
	if (!makepkg_available()) {
		error("makepkg not found; install base-devel to build AUR packages");
		return -1;
	}
	int root = geteuid() == 0;
	const char *buser = root ? invoking_user_name() : NULL;
	if (root && !buser) {
		error("cannot run makepkg as root; run 'nya aur install %s' without sudo/doas", name);
		return -1;
	}
	char build[4096];
	build_dir_for(buser, build, sizeof build);
	mkdir_p(build, 0755);
	if (root && buser) {
		struct passwd *pw = getpwnam(buser);
		if (pw) chown(build, pw->pw_uid, pw->pw_gid);
	}
	char dir[4300];
	snprintf(dir, sizeof dir, "%s/%s", build, name);
	rm_rf(dir);
	mkdir_p(dir, 0755);
	char url[4600];
	snprintf(url, sizeof url, "%s/cgit/aur.git/snapshot/%s.tar.gz", c->aurbase, name);
	char tarball[4400];
	snprintf(tarball, sizeof tarball, "%s/%s.tar.gz", dir, name);
	info("Downloading AUR snapshot for %s...", name);
	int dlrc = -1;
	int attempt;
	for (attempt = 0; attempt < 3; attempt++) {
		dlrc = download_url(c, url, tarball);
		if (dlrc == 0) break;
		if (attempt < 2) {
			warn("AUR snapshot download failed, retrying...");
			sleep(2);
		}
	}
	if (dlrc != 0) {
		error("failed to download AUR snapshot for %s", name);
		return -1;
	}
	if (extract_snapshot(tarball, dir) != 0) {
		error("failed to extract AUR snapshot for %s", name);
		return -1;
	}
	char pkgbuild[4400];
	snprintf(pkgbuild, sizeof pkgbuild, "%s/PKGBUILD", dir);
	if (!is_file(pkgbuild)) {
		error("no PKGBUILD found in AUR snapshot for %s", name);
		return -1;
	}
	if (root && buser) {
		struct passwd *pw = getpwnam(buser);
		if (!pw) {
			error("cannot drop to user '%s' to run makepkg", buser);
			return -1;
		}
		chown_r(dir, pw->pw_uid, pw->pw_gid);
	}
	info("Building %s with makepkg...", name);
	pid_t pid = fork();
	if (pid < 0) return -1;
	if (pid == 0) {
		chdir(dir);
		if (root && buser) {
			execlp("runuser", "runuser", "-u", buser, "--", "makepkg", "--syncdeps", "--noconfirm", (char *)NULL);
		} else {
			execlp("makepkg", "makepkg", "--syncdeps", "--noconfirm", (char *)NULL);
		}
		_exit(127);
	}
	int st;
	while (waitpid(pid, &st, 0) < 0) {
		if (errno != EINTR) break;
	}
	if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
		error("failed to build %s (check for missing dependencies, possibly other AUR packages)", name);
		return -1;
	}
	DIR *d = opendir(dir);
	if (!d) return -1;
	struct dirent *de;
	char found[4400] = "";
	char fallback[4400] = "";
	char pf[96];
	snprintf(pf, sizeof pf, "%s-", name);
	char dbg[128];
	snprintf(dbg, sizeof dbg, "%s-debug-", name);
	while ((de = readdir(d)) != NULL) {
		if (!(endswith(de->d_name, ".pkg.tar.zst") || endswith(de->d_name, ".pkg.tar.xz") ||
		      endswith(de->d_name, ".pkg.tar.gz") || endswith(de->d_name, ".pkg.tar"))) {
			continue;
		}
		if (fallback[0] == '\0') snprintf(fallback, sizeof fallback, "%s/%s", dir, de->d_name);
		if (startswith(de->d_name, dbg)) continue;
		if (startswith(de->d_name, pf) && found[0] == '\0') {
			snprintf(found, sizeof found, "%s/%s", dir, de->d_name);
		}
	}
	closedir(d);
	if (found[0] == '\0') snprintf(found, sizeof found, "%s", fallback);
	if (found[0] == '\0') {
		error("makepkg finished but no package file was produced for %s", name);
		return -1;
	}
	pkg *p = pkg_new("aur");
	p->filename = xstrdup(found);
	if (pkg_scan_archive(c, found, p) != 0) {
		pkg_free(p);
		return -1;
	}
	info("Built %s-%s", p->name, p->version);
	txn_add_add(t, p);
	return 0;
}

static int aur_name_ignored(config *c, const char *name) {
	int i;
	for (i = 0; i < c->ignorepkg.n; i++) {
		if (fnmatch(c->ignorepkg.v[i], name, 0) == 0) return 1;
	}
	return 0;
}

int aur_update(config *c, txn *t) {
	if (!c->aur) return 0;
	strs foreign;
	memset(&foreign, 0, sizeof foreign);
	int i;
	for (i = 0; i < g_nlocal; i++) {
		pkg *l = g_local[i];
		if (!l->name) continue;
		if (l->is_host) continue;
		if (db_find_sync(l->name)) continue;
		strs_add(&foreign, l->name);
	}
	if (foreign.n == 0) {
		strs_free(&foreign);
		return 0;
	}
	info("checking for AUR updates for %d foreign package(s)", foreign.n);
	int updated = 0;
	int nfound = 0;
	const int batch = 100;
	int b;
	for (b = 0; b < foreign.n; b += batch) {
		int hi = b + batch;
		if (hi > foreign.n) hi = foreign.n;
		char url[24000];
		int off = sprintf(url, "%s/rpc/v5/info?", c->aurbase);
		int j;
		for (j = b; j < hi && off < (int)sizeof url - 64; j++) {
			char *enc = url_encode(foreign.v[j]);
			off += sprintf(url + off, "%sarg[]=%s", j == b ? "" : "&", enc);
			free(enc);
		}
		char *data;
		long len;
		if (dl_url(c, url, &data, &len) != 0) {
			warn("failed to query the AUR for updates");
			break;
		}
		json *root = json_parse(data, len);
		free(data);
		if (!root) {
			warn("invalid response from the AUR");
			break;
		}
		json *results = json_get(root, "results");
		json *r;
		for (r = results ? results->child : NULL; r; r = r->next) {
			const char *n = json_str(json_get(r, "Name"));
			const char *v = json_str(json_get(r, "Version"));
			if (!n || !v) continue;
			nfound++;
			pkg *l = db_find_local(n);
			if (!l || !l->version) continue;
			if (vercmp(v, l->version) <= 0) continue;
			if (aur_name_ignored(c, n)) {
				warn("%s: ignoring AUR upgrade (%s => %s)", n, l->version, v);
				continue;
			}
			if (strs_has(&c->holdpkg, n)) {
				if (!yesno("%s-%s: upgrade? ", n, v)) {
					warn("%s: ignoring AUR upgrade (holdpkg)", n);
					continue;
				}
			}
			if (aur_malware_check(c, n)) {
				warn("%s: skipping upgrade: listed as malware in aur-malware-check", n);
				continue;
			}
			printf("%saur/%s%s %s%s%s -> %s%s%s\n", col_magenta(), n, col_reset(),
			       col_bold(), l->version, col_reset(), col_green(), v, col_reset());
			if (aur_build_install(c, n, t) != 0) {
				json_free(root);
				strs_free(&foreign);
				return -1;
			}
			updated++;
		}
		json_free(root);
	}
	strs_free(&foreign);
	if (updated == 0 && nfound > 0) msg("no AUR updates available");
	return 0;
}
