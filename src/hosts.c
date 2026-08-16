#include "nya.h"
#include <pwd.h>

typedef struct host_recipe {
	char *repo;
	char *folder;
	strs instructions;
	char *binary;
	char *version;
	char *desc;
	char *app;
} host_recipe;

static void host_recipe_free(host_recipe *r) {
	free(r->repo);
	free(r->folder);
	free(r->binary);
	free(r->version);
	free(r->desc);
	free(r->app);
	strs_free(&r->instructions);
	memset(r, 0, sizeof *r);
}

static void host_recipe_set(host_recipe *r, const char *sec, const char *line) {
	if (strcmp(sec, "repo") == 0) {
		free(r->repo);
		r->repo = xstrdup(line);
	} else if (strcmp(sec, "folder") == 0) {
		free(r->folder);
		r->folder = xstrdup(line);
	} else if (strcmp(sec, "binary") == 0) {
		free(r->binary);
		r->binary = xstrdup(line);
	} else if (strcmp(sec, "version") == 0) {
		free(r->version);
		r->version = xstrdup(line);
	} else if (strcmp(sec, "desc") == 0) {
		free(r->desc);
		r->desc = xstrdup(line);
	} else if (strcmp(sec, "instructions") == 0) {
		if (*line) strs_add(&r->instructions, line);
	} else if (strcmp(sec, "app") == 0) {
		free(r->app);
		r->app = xstrdup(line);
	}
}

static int host_recipe_parse(const char *data, host_recipe *r) {
	char *copy = xstrdup(data);
	char *line = copy;
	char *sec = NULL;
	while (line && *line) {
		char *nl = strchr(line, '\n');
		if (nl) *nl = '\0';
		char *s = trim(line);
		if (*s && *s != '#') {
			if (*s == '[') {
				char *end = strchr(s + 1, ']');
				if (end) {
					*end = '\0';
					sec = s + 1;
				}
			} else if (sec) {
				host_recipe_set(r, sec, s);
			}
		}
		line = nl ? nl + 1 : NULL;
	}
	free(copy);
	return r->repo ? 0 : -1;
}

static int host_fetch_recipe(config *c, const char *name, host_recipe *r, int quiet) {
	if (!c->hostsrepo || !*c->hostsrepo) {
		if (!quiet) error("no hosts repo configured (set 'hostsrepo = <url>' in %s)", c->path);
		return -1;
	}
	if (is_dir(c->hostsrepo)) {
		char path[4600];
		snprintf(path, sizeof path, "%s/%s", c->hostsrepo, name);
		if (!is_file(path)) {
			if (!quiet) error("host '%s' not found in %s", name, c->hostsrepo);
			return -1;
		}
		char *data = read_file(path, NULL);
		if (!data) {
			if (!quiet) error("failed to read recipe for '%s'", name);
			return -1;
		}
		int rc = host_recipe_parse(data, r);
		free(data);
		if (rc != 0 && !quiet) error("invalid recipe for host '%s' (missing [repo])", name);
		return rc;
	}
	char url[4600];
	snprintf(url, sizeof url, "%s/%s", c->hostsrepo, name);
	char tmp[4400];
	snprintf(tmp, sizeof tmp, "/tmp/nya-host-%ld-%s", (long)getpid(), name);
	if (!quiet) info("Fetching recipe for %s...", name);
	if (download_url(c, url, tmp) != 0) {
		unlink(tmp);
		if (!quiet) error("failed to fetch recipe for host '%s'", name);
		return -1;
	}
	char *data = read_file(tmp, NULL);
	unlink(tmp);
	if (!data) {
		if (!quiet) error("failed to read recipe for '%s'", name);
		return -1;
	}
	int rc = host_recipe_parse(data, r);
	free(data);
	if (rc != 0 && !quiet) error("invalid recipe for host '%s' (missing [repo])", name);
	return rc;
}

static int host_name_ok(const char *s) {
	if (!s || !*s || s[0] == '.' || strchr(s, '/')) return 0;
	const char *p;
	for (p = s; *p; p++) {
		unsigned char ch = *p;
		if (!(isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '+' || ch == '@')) return 0;
	}
	return 1;
}

static void folder_from_url(const char *url, char *out, size_t n) {
	const char *base = strrchr(url, '/');
	base = base ? base + 1 : url;
	snprintf(out, n, "%s", base);
	char *q = strchr(out, '?');
	if (q) *q = '\0';
	q = strchr(out, '#');
	if (q) *q = '\0';
	size_t l = strlen(out);
	if (l > 4 && strcmp(out + l - 4, ".git") == 0) out[l - 4] = '\0';
}

static int run_argv_as(const char *user, const char *cwd, char *const argv[]) {
	pid_t pid = fork();
	if (pid < 0) return -1;
	if (pid == 0) {
		if (cwd && chdir(cwd) != 0) _exit(127);
		if (user) {
			int n = 0;
			while (argv[n]) n++;
			char **args = xcalloc(n + 5, sizeof *args);
			args[0] = (char *)"runuser";
			args[1] = (char *)"-u";
			args[2] = (char *)user;
			args[3] = (char *)"--";
			int i;
			for (i = 0; i <= n; i++) args[i + 4] = argv[i];
			execvp("runuser", args);
			free(args);
		} else {
			execvp(argv[0], argv);
		}
		_exit(127);
	}
	int st;
	while (waitpid(pid, &st, 0) < 0) {
		if (errno != EINTR) return -1;
	}
	if (WIFEXITED(st)) return WEXITSTATUS(st);
	return -1;
}

static char *shq(const char *s) {
	size_t n = strlen(s);
	char *out = xmalloc(n * 2 + 3);
	char *p = out;
	*p++ = '\'';
	const char *c;
	for (c = s; *c; c++) {
		if (*c == '\'') {
			*p++ = '\'';
			*p++ = '\\';
			*p++ = '\'';
			*p++ = '\'';
		} else {
			*p++ = *c;
		}
	}
	*p++ = '\'';
	*p = '\0';
	return out;
}

static int run_sh_as(const char *user, const char *cwd, const char *cmd, int quiet) {
	pid_t pid = fork();
	if (pid < 0) return -1;
	if (pid == 0) {
		if (cwd && chdir(cwd) != 0) _exit(127);
		if (quiet) {
			int devnull = open("/dev/null", O_WRONLY);
			if (devnull >= 0) {
				dup2(devnull, STDOUT_FILENO);
				dup2(devnull, STDERR_FILENO);
				close(devnull);
			}
		}
		if (user) execlp("runuser", "runuser", "-u", user, "--", "sh", "-c", cmd, (char *)NULL);
		else execlp("sh", "sh", "-c", cmd, (char *)NULL);
		_exit(127);
	}
	int st;
	while (waitpid(pid, &st, 0) < 0) {
		if (errno != EINTR) return -1;
	}
	if (WIFEXITED(st)) return WEXITSTATUS(st);
	return -1;
}

static void host_build_dir(const char *user, char *out, size_t n) {
	const char *home = NULL;
	if (user) {
		struct passwd *pw = getpwnam(user);
		if (pw && pw->pw_dir && *pw->pw_dir) home = pw->pw_dir;
	}
	if (!home) home = getenv("HOME");
	if (home && *home) {
		snprintf(out, n, "%s/.cache/nya/hosts-build", home);
		return;
	}
	snprintf(out, n, "/tmp/nya-hosts-%ld", (long)geteuid());
}

static int is_elf(const char *path) {
	struct stat st;
	if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || !(st.st_mode & 0111)) return 0;
	int fd = open(path, O_RDONLY);
	if (fd < 0) return 0;
	char magic[4];
	ssize_t n = read(fd, magic, 4);
	close(fd);
	return n == 4 && memcmp(magic, "\x7f" "ELF", 4) == 0;
}

static const char *host_scan_dirs[] = {
	"build", "builddir", "release", "debug", "dist", "bin", "out",
	"target/release", "target/debug", "", NULL
};

static int host_detect_entry(const char *proj, const char *name, const char *folder,
                             char *out, size_t n) {
	strs found;
	memset(&found, 0, sizeof found);
	int i;
	for (i = 0; host_scan_dirs[i]; i++) {
		char d[4400];
		if (host_scan_dirs[i][0]) snprintf(d, sizeof d, "%s/%s", proj, host_scan_dirs[i]);
		else snprintf(d, sizeof d, "%s", proj);
		if (!is_dir(d)) continue;
		DIR *dp = opendir(d);
		if (!dp) continue;
		struct dirent *de;
		while ((de = readdir(dp)) != NULL) {
			if (de->d_name[0] == '.') continue;
			char p[4600];
			snprintf(p, sizeof p, "%s/%s", d, de->d_name);
			if (is_elf(p)) {
				char rel[4600];
				if (host_scan_dirs[i][0]) snprintf(rel, sizeof rel, "%s/%s", host_scan_dirs[i], de->d_name);
				else snprintf(rel, sizeof rel, "%s", de->d_name);
				strs_add(&found, rel);
			}
		}
		closedir(dp);
	}
	const char *pref[2];
	pref[0] = name;
	pref[1] = folder;
	int pi, k;
	for (pi = 0; pi < 2; pi++) {
		for (k = 0; k < found.n; k++) {
			const char *base = strrchr(found.v[k], '/');
			base = base ? base + 1 : found.v[k];
			if (strcmp(base, pref[pi]) == 0) {
				snprintf(out, n, "%s", found.v[k]);
				strs_free(&found);
				return 0;
			}
		}
	}
	if (found.n == 1) {
		snprintf(out, n, "%s", found.v[0]);
		strs_free(&found);
		return 0;
	}
	if (found.n > 1) error("multiple executables found in %s; add [binary] to the recipe", proj);
	else error("no executable found in %s; add [binary] to the recipe", proj);
	strs_free(&found);
	return -1;
}

static int has_runtime_artifacts(const char *dir, const char *entryname) {
	DIR *dp = opendir(dir);
	if (!dp) return 0;
	struct dirent *de;
	int has = 0;
	while ((de = readdir(dp)) != NULL) {
		if (de->d_name[0] == '.') continue;
		if (strcmp(de->d_name, entryname) == 0) continue;
		if (endswith(de->d_name, ".so")) {
			has = 1;
			break;
		}
		char p[4600];
		snprintf(p, sizeof p, "%s/%s", dir, de->d_name);
		if (is_elf(p)) {
			has = 1;
			break;
		}
	}
	closedir(dp);
	return has;
}

static int host_is_junk(const char *base) {
	static const char *names[] = {
		"CMakeFiles", "CMakeCache.txt", "cmake_install.cmake",
		"compile_commands.json", "Makefile", "makefile", "GNUmakefile",
		"build.ninja", ".ninja_log", ".ninja_deps", ".git", ".github",
		".gitignore", "__pycache__", "CPackConfig.cmake", "CPackSourceConfig.cmake",
		NULL
	};
	static const char *exts[] = { ".o", ".a", ".cmake", ".ninja", ".pyc", ".pyo", ".so.debug", NULL };
	int i;
	for (i = 0; names[i]; i++) {
		if (strcmp(base, names[i]) == 0) return 1;
	}
	for (i = 0; exts[i]; i++) {
		if (endswith(base, exts[i])) return 1;
	}
	return 0;
}

static int copy_file(const char *src, const char *dst, mode_t mode) {
	int in = open(src, O_RDONLY);
	if (in < 0) return -1;
	int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, mode);
	if (out < 0) {
		close(in);
		return -1;
	}
	char buf[65536];
	ssize_t got;
	while ((got = read(in, buf, sizeof buf)) > 0) {
		ssize_t off = 0;
		while (off < got) {
			ssize_t w = write(out, buf + off, got - off);
			if (w < 0) {
				if (errno == EINTR) continue;
				close(in);
				close(out);
				return -1;
			}
			off += w;
		}
	}
	close(in);
	close(out);
	return got < 0 ? -1 : 0;
}

static int merge_tree(config *c, const char *src, const char *root, pkg *p,
                      const char *relprefix, long long *total) {
	(void)c;
	DIR *dp = opendir(src);
	if (!dp) return -1;
	struct dirent *de;
	while ((de = readdir(dp)) != NULL) {
		if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
		if (host_is_junk(de->d_name)) continue;
		char spath[4600];
		snprintf(spath, sizeof spath, "%s/%s", src, de->d_name);
		char rel[4600];
		if (relprefix[0]) snprintf(rel, sizeof rel, "%s/%s", relprefix, de->d_name);
		else snprintf(rel, sizeof rel, "%s", de->d_name);
		char dst[4600];
		snprintf(dst, sizeof dst, "%s/%s", root, rel);
		struct stat st;
		if (lstat(spath, &st) != 0) continue;
		if (S_ISDIR(st.st_mode)) {
			mkdir_p(dst, st.st_mode & 07777);
			char rp[4600];
			snprintf(rp, sizeof rp, "%s/", rel);
			if (!strs_has(&p->files, rp)) strs_add(&p->files, rp);
			merge_tree(c, spath, root, p, rel, total);
		} else if (S_ISLNK(st.st_mode)) {
			char target[4096];
			ssize_t tl = readlink(spath, target, sizeof target - 1);
			if (tl < 0) continue;
			target[tl] = '\0';
			unlink(dst);
			symlink(target, dst);
			if (!strs_has(&p->files, rel)) strs_add(&p->files, rel);
		} else if (S_ISREG(st.st_mode)) {
			char parent[4600];
			snprintf(parent, sizeof parent, "%s", dst);
			char *sl = strrchr(parent, '/');
			if (sl) {
				*sl = '\0';
				if (*parent) mkdir_p(parent, 0755);
			}
			if (copy_file(spath, dst, st.st_mode & 07777) != 0) continue;
			*total += st.st_size;
			if (!strs_has(&p->files, rel)) strs_add(&p->files, rel);
		}
	}
	closedir(dp);
	return 0;
}

static int stage_dir_empty(const char *stage) {
	DIR *dp = opendir(stage);
	if (!dp) return 1;
	struct dirent *de;
	int n = 0;
	while ((de = readdir(dp)) != NULL) {
		if (strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..") != 0) {
			n = 1;
			break;
		}
	}
	closedir(dp);
	return !n;
}

static int cmake_available(void) {
	return access("/usr/bin/cmake", X_OK) == 0 || access("/bin/cmake", X_OK) == 0 ||
	       access("/usr/local/bin/cmake", X_OK) == 0;
}

static int host_try_staged(const char *buildroot, const char *proj, const char *folder,
                           const char *stage, const char *user) {
	char makefile[4400];
	snprintf(makefile, sizeof makefile, "%s/Makefile", proj);
	if (is_file(makefile)) {
		char *qstage = shq(stage);
		char *qf = shq(folder);
		char cmd[16384];
		snprintf(cmd, sizeof cmd, "cd %s && make -n install DESTDIR=%s >/dev/null 2>&1", qf, qstage);
		if (run_sh_as(user, buildroot, cmd, 1) == 0) {
			snprintf(cmd, sizeof cmd, "cd %s && DESTDIR=%s make install DESTDIR=%s >/dev/null 2>&1",
			         qf, qstage, qstage);
			if (run_sh_as(user, buildroot, cmd, 1) == 0 && !stage_dir_empty(stage)) {
				free(qstage);
				free(qf);
				return 1;
			}
			rm_rf(stage);
			mkdir_p(stage, 0755);
		}
		free(qstage);
		free(qf);
	}
	if (cmake_available()) {
		static const char *dirs[] = { "build", "builddir", "cmake-build-release",
		                              "cmake-build-debug", "release", "debug", NULL };
		int i;
		for (i = 0; dirs[i]; i++) {
			char cache[4400];
			snprintf(cache, sizeof cache, "%s/%s/CMakeCache.txt", proj, dirs[i]);
			if (!is_file(cache)) continue;
			char *qstage = shq(stage);
			char *qf = shq(folder);
			char cmd[16384];
			snprintf(cmd, sizeof cmd, "cd %s && DESTDIR=%s cmake --install %s --prefix /usr >/dev/null 2>&1",
			         qf, qstage, dirs[i]);
			int rc = run_sh_as(user, buildroot, cmd, 1);
			free(qstage);
			free(qf);
			if (rc == 0 && !stage_dir_empty(stage)) return 1;
			rm_rf(stage);
			mkdir_p(stage, 0755);
		}
	}
	return 0;
}

static int path_inside(const char *dir, const char *path) {
	size_t dl = strlen(dir);
	if (strncmp(dir, path, dl) != 0) return 0;
	return path[dl] == '/' || path[dl] == '\0';
}

static int size_dir_name(const char *name) {
	if (strcmp(name, "scalable") == 0) return 1;
	if (isdigit((unsigned char)name[0]) && strchr(name, 'x') && atoi(name) > 0) return 1;
	return 0;
}

static int host_install_icon_file(config *c, pkg *p, const char *src, long long *total) {
	/* pick the size from the file's directory chain (hicolor/<N>x<N>|<scalable>/apps/...) */
	char parent[4700];
	snprintf(parent, sizeof parent, "%s", src);
	char size[64] = "256x256";
	char *sl = strrchr(parent, '/');
	if (sl) {
		*sl = '\0';
		const char *pdir = strrchr(parent, '/');
		pdir = pdir ? pdir + 1 : parent;
		if (size_dir_name(pdir)) {
			snprintf(size, sizeof size, "%s", pdir);
		} else {
			char *sl2 = strrchr(parent, '/');
			if (sl2) {
				*sl2 = '\0';
				const char *gp = strrchr(parent, '/');
				gp = gp ? gp + 1 : parent;
				if (size_dir_name(gp)) snprintf(size, sizeof size, "%s", gp);
				else if (endswith(src, ".svg")) snprintf(size, sizeof size, "scalable");
			} else if (endswith(src, ".svg")) {
				snprintf(size, sizeof size, "scalable");
			}
		}
	}
	const char *base = strrchr(src, '/');
	base = base ? base + 1 : src;
	char dstdir[4400];
	snprintf(dstdir, sizeof dstdir, "%s/usr/share/icons/hicolor/%s/apps", c->rootdir, size);
	mkdir_p(dstdir, 0755);
	char dst[4600];
	snprintf(dst, sizeof dst, "%s/%s", dstdir, base);
	if (is_file(dst)) return 0;
	if (copy_file(src, dst, 0644) != 0) return -1;
	struct stat st;
	if (stat(src, &st) == 0) *total += st.st_size;
	char rel[4600];
	snprintf(rel, sizeof rel, "usr/share/icons/hicolor/%s/apps/%s", size, base);
	if (!strs_has(&p->files, rel)) strs_add(&p->files, rel);
	return 0;
}

static void host_find_icon_name(config *c, pkg *p, const char *appdir, const char *iconname,
                                long long *total, int depth) {
	if (depth > 8) return;
	DIR *dp = opendir(appdir);
	if (!dp) return;
	struct dirent *de;
	while ((de = readdir(dp)) != NULL) {
		if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
		if (host_is_junk(de->d_name)) continue;
		char p2[4600];
		snprintf(p2, sizeof p2, "%s/%s", appdir, de->d_name);
		struct stat st;
		if (lstat(p2, &st) != 0) continue;
		if (S_ISDIR(st.st_mode)) {
			host_find_icon_name(c, p, p2, iconname, total, depth + 1);
			continue;
		}
		if (!S_ISREG(st.st_mode)) continue;
		char *dot = strrchr(de->d_name, '.');
		if (!dot) continue;
		size_t bl = dot - de->d_name;
		if (bl >= 512) bl = 511;
		char base[512];
		memcpy(base, de->d_name, bl);
		base[bl] = '\0';
		if (str_ieq(base, iconname)) host_install_icon_file(c, p, p2, total);
	}
	closedir(dp);
}

static void host_scan_logos(config *c, pkg *p, const char *dir, const char *name,
                            const char *folder, int in_icon, long long *total, int depth) {
	if (depth > 8) return;
	static const char *namedirs[] = { "icons", "logo", "logos", "branding", NULL };
	static const char *imgexts[] = { ".png", ".svg", ".xpm", NULL };
	DIR *dp = opendir(dir);
	if (!dp) return;
	struct dirent *de;
	while ((de = readdir(dp)) != NULL) {
		if (de->d_name[0] == '.') continue;
		if (host_is_junk(de->d_name)) continue;
		if (strcmp(de->d_name, "third_party") == 0 || strcmp(de->d_name, "node_modules") == 0 ||
		    strcmp(de->d_name, "vendor") == 0) continue;
		char p2[4600];
		snprintf(p2, sizeof p2, "%s/%s", dir, de->d_name);
		struct stat st;
		if (lstat(p2, &st) != 0) continue;
		if (S_ISDIR(st.st_mode)) {
			int sub = in_icon;
			int i;
			for (i = 0; namedirs[i]; i++) {
				if (str_ieq(de->d_name, namedirs[i])) sub = 1;
			}
			host_scan_logos(c, p, p2, name, folder, sub, total, depth + 1);
			continue;
		}
		if (!S_ISREG(st.st_mode)) continue;
		const char *ext = strrchr(de->d_name, '.');
		if (!ext) continue;
		int is_img = 0;
		int i;
		for (i = 0; imgexts[i]; i++) {
			if (str_ieq(ext, imgexts[i])) is_img = 1;
		}
		if (!is_img) continue;
		size_t bl = ext - de->d_name;
		if (bl >= 512) bl = 511;
		char base[512];
		memcpy(base, de->d_name, bl);
		base[bl] = '\0';
		if (in_icon || str_ieq(base, name) || str_ieq(base, folder) ||
		    str_ieq(base, "logo") || str_ieq(base, "icon")) {
			host_install_icon_file(c, p, p2, total);
		}
	}
	closedir(dp);
}

static void host_auto_logos(config *c, pkg *p, const char *proj, const char *name,
                             const char *folder, long long *total) {
	host_scan_logos(c, p, proj, name, folder, 0, total, 0);
}

static int host_app_menu(config *c, pkg *p, const char *proj, const char *name,
                         const char *appval, const char *bundlesrc, long long *total) {
	char val[512];
	snprintf(val, sizeof val, "%s", appval);
	while (startswith(val, "./")) memmove(val, val + 2, strlen(val + 2) + 1);
	char appfull[4700];
	if (val[0] == '/') snprintf(appfull, sizeof appfull, "%s", val);
	else snprintf(appfull, sizeof appfull, "%s/%s", proj, val);
	char desktop[4700] = "";
	if (is_file(appfull)) {
		snprintf(desktop, sizeof desktop, "%s", appfull);
	} else if (is_dir(appfull)) {
		DIR *dp = opendir(appfull);
		if (!dp) {
			warn("[app] '%s' not found for host '%s'", appval, name);
			return 0;
		}
		struct dirent *de;
		char want[600];
		snprintf(want, sizeof want, "%s.desktop", name);
		while ((de = readdir(dp)) != NULL) {
			if (!endswith(de->d_name, ".desktop")) continue;
			if (!desktop[0]) snprintf(desktop, sizeof desktop, "%s/%s", appfull, de->d_name);
			if (strcmp(de->d_name, want) == 0) {
				snprintf(desktop, sizeof desktop, "%s/%s", appfull, de->d_name);
				break;
			}
		}
		closedir(dp);
		if (!desktop[0]) {
			warn("no .desktop file found in [app] folder '%s' for host '%s'", appval, name);
			return 0;
		}
	} else {
		warn("[app] '%s' not found for host '%s'", appval, name);
		return 0;
	}
	char *slash = strrchr(desktop, '/');
	const char *base = slash ? slash + 1 : desktop;
	char appdir[4700];
	if (slash) snprintf(appdir, sizeof appdir, "%.*s", (int)(slash - desktop), desktop);
	else snprintf(appdir, sizeof appdir, "%s", desktop);
	char bundlebase[4400];
	snprintf(bundlebase, sizeof bundlebase, "%s/usr/lib/nya/%s", c->rootdir, name);
	char deskbundle[4700] = "";
	int merged = 0;
	if (bundlesrc[0] && path_inside(bundlesrc, appdir)) {
		/* app folder is already part of the copied bundle */
		snprintf(deskbundle, sizeof deskbundle, "%s/%s", bundlebase, desktop + strlen(bundlesrc) + 1);
		merged = 1;
	} else if (bundlesrc[0]) {
		/* bundle exists but the app folder lives elsewhere: merge it in so the link stays valid */
		info("Merging app folder into %s...", bundlebase);
		char relprefix[4600];
		snprintf(relprefix, sizeof relprefix, "usr/lib/nya/%s", name);
		if (merge_tree(c, appdir, c->rootdir, p, relprefix, total) == 0) {
			snprintf(deskbundle, sizeof deskbundle, "%s/%s", bundlebase, base);
			merged = 1;
		}
	}
	char appsdir[4400];
	snprintf(appsdir, sizeof appsdir, "%s/usr/share/applications", c->rootdir);
	mkdir_p(appsdir, 0755);
	char linkpath[4600];
	snprintf(linkpath, sizeof linkpath, "%s/%s", appsdir, base);
	unlink(linkpath);
	char rel[4600];
	snprintf(rel, sizeof rel, "usr/share/applications/%s", base);
	if (merged) {
		if (symlink(deskbundle, linkpath) == 0) {
			if (!strs_has(&p->files, rel)) strs_add(&p->files, rel);
			info("Linked %s into the application menu", base);
		} else {
			warn("failed to link %s -> %s", linkpath, deskbundle);
		}
	} else if (copy_file(desktop, linkpath, 0644) == 0) {
		if (!strs_has(&p->files, rel)) strs_add(&p->files, rel);
		info("Installed %s into the application menu", base);
	}
	char *ddata = read_file(desktop, NULL);
	if (!ddata) return 0;
	char *line = ddata;
	while (line && *line) {
		char *nl = strchr(line, '\n');
		if (nl) *nl = '\0';
		char *s = trim(line);
		if (startswith(s, "Icon=")) {
			char *iconname = s + 5;
			trim(iconname);
			if (*iconname) {
				if (strchr(iconname, '/') || strstr(iconname, ".png") || strstr(iconname, ".svg")) {
					char ipath[4700];
					if (iconname[0] == '/') snprintf(ipath, sizeof ipath, "%s", iconname);
					else snprintf(ipath, sizeof ipath, "%s/%s", appdir, iconname);
					while (startswith(ipath, "./")) memmove(ipath, ipath + 2, strlen(ipath + 2) + 1);
					if (is_file(ipath)) host_install_icon_file(c, p, ipath, total);
					else warn("icon '%s' not found for host '%s'", iconname, name);
				} else {
					host_find_icon_name(c, p, appdir, iconname, total, 0);
				}
			}
			break;
		}
		line = nl ? nl + 1 : NULL;
	}
	free(ddata);
	return 0;
}

static void host_uninstall_local(config *c, pkg *p) {
	int i;
	for (i = 0; i < p->files.n; i++) {
		const char *f = p->files.v[i];
		size_t fl = strlen(f);
		if (fl > 0 && f[fl - 1] == '/') continue;
		char rooted[4300];
		snprintf(rooted, sizeof rooted, "%s/%s", c->rootdir, f);
		unlink(rooted);
	}
	for (i = p->files.n - 1; i >= 0; i--) {
		const char *f = p->files.v[i];
		size_t fl = strlen(f);
		if (fl == 0 || f[fl - 1] != '/') continue;
		char rooted[4300];
		snprintf(rooted, sizeof rooted, "%s/%s", c->rootdir, f);
		rmdir(rooted);
	}
	db_remove_local(c, p->name, p->version);
}

static int host_do_install(config *c, const char *name, host_recipe *r) {
	char folder[512];
	if (r->folder) snprintf(folder, sizeof folder, "%s", r->folder);
	else folder_from_url(r->repo, folder, sizeof folder);
	if (!host_name_ok(folder)) {
		error("invalid folder name '%s' in recipe for '%s'", folder, name);
		host_recipe_free(r);
		return -1;
	}
	const char *version = (r->version && *r->version) ? r->version : "1";

	int root = geteuid() == 0;
	const char *buser = root ? invoking_user_name() : NULL;
	if (root && !buser) {
		error("cannot run host builds as root; run 'nya host install %s' without sudo/doas", name);
		host_recipe_free(r);
		return -1;
	}

	char build[4096];
	host_build_dir(buser, build, sizeof build);
	if (mkdir_p(build, 0755) != 0 && !root) {
		/* HOME cache not writable (e.g. root-owned parent): fall back to /tmp */
		snprintf(build, sizeof build, "/tmp/nya-hosts-%ld", (long)geteuid());
		mkdir_p(build, 0755);
	}
	if (root && buser) {
		struct passwd *pw = getpwnam(buser);
		if (pw) chown(build, pw->pw_uid, pw->pw_gid);
	}

	pkg *old = db_find_local(name);
	if (old) {
		info("upgrading %s %s -> %s", name, old->version, version);
		host_uninstall_local(c, old);
	}

	char dir[4300];
	snprintf(dir, sizeof dir, "%s/%s", build, name);
	rm_rf(dir);
	mkdir_p(dir, 0755);
	if (root && buser) {
		struct passwd *pw = getpwnam(buser);
		if (pw) chown(dir, pw->pw_uid, pw->pw_gid);
	}

	info("Cloning %s...", r->repo);
	char *gitav[] = { (char *)"git", (char *)"clone", (char *)"--depth", (char *)"1",
	                  r->repo, folder, NULL };
	if (run_argv_as(buser, dir, gitav) != 0) {
		error("failed to clone %s (is git installed?)", r->repo);
		host_recipe_free(r);
		return -1;
	}
	char proj[4400];
	snprintf(proj, sizeof proj, "%s/%s", dir, folder);

	if (r->instructions.n > 0) {
		info("Running instructions for %s...", name);
		strs cmd;
		memset(&cmd, 0, sizeof cmd);
		strs_add(&cmd, "set -e");
		char cdline[600];
		snprintf(cdline, sizeof cdline, "cd '%s'", folder);
		strs_add(&cmd, cdline);
		int i;
		for (i = 0; i < r->instructions.n; i++) strs_add(&cmd, r->instructions.v[i]);
		long total = 1;
		for (i = 0; i < cmd.n; i++) total += strlen(cmd.v[i]) + 1;
		char *script = xmalloc(total);
		script[0] = '\0';
		for (i = 0; i < cmd.n; i++) {
			strcat(script, cmd.v[i]);
			strcat(script, "\n");
		}
		int rc = run_sh_as(buser, dir, script, 0);
		free(script);
		strs_free(&cmd);
		if (rc != 0) {
			error("instructions failed for host '%s' (exit %d)", name, rc);
			host_recipe_free(r);
			return -1;
		}
	}

	pkg *p = pkg_new("local");
	p->name = xstrdup(name);
	p->version = xstrdup(version);
	if (r->desc && *r->desc) p->desc = xstrdup(r->desc);
	p->url = xstrdup(r->repo);
	p->reason = 0;
	p->validation = xstrdup("none");
	p->is_local = 1;
	long long total_size = 0;

	char stage[4400];
	snprintf(stage, sizeof stage, "%s/%s.stage", dir, name);
	rm_rf(stage);
	mkdir_p(stage, 0755);
	char bundlesrc[4700] = "";

	int rc2 = -1;
	if (host_try_staged(dir, proj, folder, stage, buser)) {
		info("Installing %s (project install rules)...", name);
		if (merge_tree(c, stage, c->rootdir, p, "", &total_size) == 0) rc2 = 0;
		else error("failed to merge staged install for '%s'", name);
	} else {
		char entry[4600] = "";
		int have_entry = 0;
		if (r->binary) {
			snprintf(entry, sizeof entry, "%s", r->binary);
			while (startswith(entry, "./")) memmove(entry, entry + 2, strlen(entry + 2) + 1);
			have_entry = 1;
		} else if (host_detect_entry(proj, name, folder, entry, sizeof entry) == 0) {
			have_entry = 1;
		}
		if (!have_entry) {
			error("no [binary] in recipe and none could be detected for host '%s'", name);
			pkg_free(p);
			host_recipe_free(r);
			rm_rf(stage);
			return -1;
		}
		char epath[4700];
		snprintf(epath, sizeof epath, "%s/%s", proj, entry);
		if (is_dir(epath)) {
			char sub[4600] = "";
			if (host_detect_entry(epath, name, folder, sub, sizeof sub) != 0) {
				pkg_free(p);
				host_recipe_free(r);
				rm_rf(stage);
				return -1;
			}
			char tmp2[4700];
			snprintf(tmp2, sizeof tmp2, "%s/%s", epath, sub);
			snprintf(epath, sizeof epath, "%s", tmp2);
		}
		struct stat est;
		if (stat(epath, &est) != 0 || !S_ISREG(est.st_mode) || !(est.st_mode & 0111)) {
			error("binary '%s' for host '%s' not found or not executable (did the instructions produce it?)",
			      entry, name);
			pkg_free(p);
			host_recipe_free(r);
			rm_rf(stage);
			return -1;
		}
		char *slash = strrchr(epath, '/');
		char entrydir[4700];
		snprintf(entrydir, sizeof entrydir, "%.*s", (int)(slash - epath), epath);
		const char *entrybase = slash + 1;
		snprintf(bundlesrc, sizeof bundlesrc, "%s", entrydir);
		if (has_runtime_artifacts(entrydir, entrybase)) {
			char relprefix[4600];
			snprintf(relprefix, sizeof relprefix, "usr/lib/nya/%s", name);
			info("Installing %s (bundle: %s)...", name, entrydir);
			if (merge_tree(c, entrydir, c->rootdir, p, relprefix, &total_size) != 0) {
				error("failed to install bundle for '%s'", name);
				pkg_free(p);
				host_recipe_free(r);
				rm_rf(stage);
				return -1;
			}
			char rp[4600];
			snprintf(rp, sizeof rp, "%s/", relprefix);
			strs_add(&p->files, rp);
			char bindir[4300];
			snprintf(bindir, sizeof bindir, "%s/usr/bin", c->rootdir);
			mkdir_p(bindir, 0755);
			char linkpath[4400];
			snprintf(linkpath, sizeof linkpath, "%s/%s", bindir, name);
			unlink(linkpath);
			char target[512];
			snprintf(target, sizeof target, "../lib/nya/%s/%s", name, entrybase);
			if (symlink(target, linkpath) != 0) {
				error("failed to create symlink %s", linkpath);
				pkg_free(p);
				host_recipe_free(r);
				rm_rf(stage);
				return -1;
			}
			char lrel[512];
			snprintf(lrel, sizeof lrel, "usr/bin/%s", name);
			strs_add(&p->files, lrel);
		} else {
			char bindir[4300];
			snprintf(bindir, sizeof bindir, "%s/usr/bin", c->rootdir);
			mkdir_p(bindir, 0755);
			char dst[4400];
			snprintf(dst, sizeof dst, "%s/%s", bindir, name);
			info("Installing %s...", name);
			if (copy_file(epath, dst, est.st_mode & 07777) != 0) {
				error("failed to install %s", dst);
				pkg_free(p);
				host_recipe_free(r);
				rm_rf(stage);
				return -1;
			}
			char lrel[512];
			snprintf(lrel, sizeof lrel, "usr/bin/%s", name);
			strs_add(&p->files, lrel);
			total_size += est.st_size;
		}
		rc2 = 0;
	}
	rm_rf(stage);
	if (rc2 != 0) {
		pkg_free(p);
		host_recipe_free(r);
		return -1;
	}
	if (p->files.n == 0) {
		error("nothing was installed for host '%s'", name);
		pkg_free(p);
		host_recipe_free(r);
		return -1;
	}
	host_auto_logos(c, p, proj, name, folder, &total_size);
	if (r->app && *r->app) {
		host_app_menu(c, p, proj, name, r->app, bundlesrc, &total_size);
	}
	p->isize = total_size;
	if (db_write_local_pkg(c, p) != 0) {
		error("failed to register '%s' in the local database", name);
		pkg_free(p);
		host_recipe_free(r);
		return -1;
	}
	{
		char mdir[4096], mpath[4600];
		snprintf(mdir, sizeof mdir, "%s/local/%s-%s", c->dbpath, p->name, p->version);
		snprintf(mpath, sizeof mpath, "%s/nya-host", mdir);
		int mfd = open(mpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (mfd >= 0) close(mfd);
	}
	msg("%s installed (from %s)", name, r->repo);
	free(g_local);
	g_local = NULL;
	g_nlocal = 0;
	free(g_sync);
	g_sync = NULL;
	g_nsync = 0;
	db_load_all(c);
	pkg_free(p);
	host_recipe_free(r);
	return 0;
}

int host_install(config *c, const char *name) {
	if (!host_name_ok(name)) {
		error("invalid host name '%s'", name);
		return -1;
	}
	host_recipe r;
	memset(&r, 0, sizeof r);
	if (host_fetch_recipe(c, name, &r, 0) != 0) return -1;
	if (!r.repo) {
		error("recipe for host '%s' has no [repo] url", name);
		host_recipe_free(&r);
		return -1;
	}
	return host_do_install(c, name, &r);
}

int host_try_install(config *c, const char *name) {
	if (!host_name_ok(name)) return -1;
	host_recipe r;
	memset(&r, 0, sizeof r);
	if (host_fetch_recipe(c, name, &r, 1) != 0) {
		warn("%s: not found in nya-hosts", name);
		return -1;
	}
	if (!r.repo) {
		warn("%s: not found in nya-hosts", name);
		host_recipe_free(&r);
		return -1;
	}
	return host_do_install(c, name, &r);
}
