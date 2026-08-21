#include "nya.h"

static void remove_cached_tarballs(config *c, pkg *p);

static int lock_fd = -1;
static char lock_path[4096];

static int lock_db(config *c) {
	char p[4096];
	snprintf(p, sizeof p, "%s/db.lck", c->dbpath);
	snprintf(lock_path, sizeof lock_path, "%s", p);
	mkdir_p(c->dbpath, 0755);
	lock_fd = open(p, O_CREAT | O_RDWR, 0644);
	if (lock_fd < 0) {
		error("could not lock database: %s", strerror(errno));
		lock_path[0] = '\0';
		return -1;
	}
	if (flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
		error("failed to init transaction (unable to lock database)");
		error("if you're sure a package manager is not already running, you can remove %s", p);
		close(lock_fd);
		lock_fd = -1;
		lock_path[0] = '\0';
		return -1;
	}
	return 0;
}

static void unlock_db(void) {
	if (lock_fd >= 0) {
		flock(lock_fd, LOCK_UN);
		close(lock_fd);
		lock_fd = -1;
		if (lock_path[0]) {
			unlink(lock_path);
			lock_path[0] = '\0';
		}
	}
}

static char *shell_quote(const char *s) {
	if (!s) return xstrdup("");
	size_t n = strlen(s);
	char *out = xmalloc(n * 4 + 4);
	char *p = out;
	*p++ = '\'';
	while (*s) {
		if (*s == '\'') {
			memcpy(p, "'\\''", 4);
			p += 4;
		} else {
			*p++ = *s;
		}
		s++;
	}
	*p++ = '\'';
	*p = '\0';
	return out;
}

static int run_scriptlet(config *c, const char *data, long len, const char *func, const char *a1, const char *a2) {
	char scr[] = "/tmp/nya-scr-XXXXXX";
	int fd = mkstemp(scr);
	if (fd < 0) return -1;
	long off = 0;
	while (off < len) {
		ssize_t w = write(fd, data + off, len - off);
		if (w < 0) {
			if (errno == EINTR) continue;
			close(fd);
			unlink(scr);
			return -1;
		}
		off += w;
	}
	close(fd);
	char wrap[] = "/tmp/nya-wrap-XXXXXX";
	int wfd = mkstemp(wrap);
	if (wfd < 0) {
		unlink(scr);
		return -1;
	}
	char *q1 = shell_quote(a1);
	char *q2 = shell_quote(a2);
	char *qs = shell_quote(scr);
	char buf[8192];
	if (a2) snprintf(buf, sizeof buf, "#!/bin/sh\numask 022\n. %s\nif command -v %s >/dev/null 2>&1; then %s %s %s; fi\n", qs, func, func, q1, q2);
	else if (a1) snprintf(buf, sizeof buf, "#!/bin/sh\numask 022\n. %s\nif command -v %s >/dev/null 2>&1; then %s %s; fi\n", qs, func, func, q1);
	else snprintf(buf, sizeof buf, "#!/bin/sh\numask 022\n. %s\nif command -v %s >/dev/null 2>&1; then %s; fi\n", qs, func, func);
	write(wfd, buf, strlen(buf));
	close(wfd);
	chmod(wrap, 0755);
	free(q1);
	free(q2);
	free(qs);
	pid_t pid = fork();
	if (pid < 0) {
		unlink(scr);
		unlink(wrap);
		return -1;
	}
	if (pid == 0) {
		chdir(c->rootdir);
		char home[4096];
		snprintf(home, sizeof home, "%s/root", c->rootdir);
		setenv("HOME", home, 1);
		execl("/bin/sh", "sh", wrap, (char *)NULL);
		_exit(127);
	}
	int st;
	while (waitpid(pid, &st, 0) < 0) {
		if (errno != EINTR) break;
	}
	unlink(scr);
	unlink(wrap);
	if (WIFEXITED(st) && WEXITSTATUS(st) != 0) {
		warn("scriptlet '%s' failed with exit status %d", func, WEXITSTATUS(st));
		return -1;
	}
	return 0;
}

static void apply_meta(const char *path, tar_entry *e, int owner) {
	if (owner) {
		chown(path, e->uid, e->gid);
	}
	chmod(path, e->mode & 07777);
	struct timespec ts[2];
	ts[0].tv_sec = e->mtime;
	ts[0].tv_nsec = 0;
	ts[1].tv_sec = e->mtime;
	ts[1].tv_nsec = 0;
	utimensat(AT_FDCWD, path, ts, 0);
}

static int noupgrade_match(config *c, const char *relpath) {
	int i;
	for (i = 0; i < c->noupgrade.n; i++) {
		if (fnmatch(c->noupgrade.v[i], relpath, 0) == 0) return 1;
	}
	return 0;
}

static int write_file_from_tar(tar_it *t, tar_entry *e, const char *dest, char *shaout) {
	int fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, e->mode & 0777);
	if (fd < 0) {
		error("could not open %s: %s", dest, strerror(errno));
		return -1;
	}
	sha256_t ctx;
	sha256_init(&ctx);
	char buf[65536];
	long long remain = e->size;
	while (remain > 0) {
		long n = remain > (long long)sizeof buf ? (long long)sizeof buf : remain;
		long got = tar_read(t, buf, n);
		if (got <= 0) break;
		sha256_update(&ctx, buf, got);
		long off = 0;
		while (off < got) {
			ssize_t w = write(fd, buf + off, got - off);
			if (w < 0) {
				if (errno == EINTR) continue;
				close(fd);
				return -1;
			}
			off += w;
		}
		remain -= got;
	}
	close(fd);
	sha256_final(&ctx, shaout);
	return 0;
}

static int extract_pkg(config *c, pkg *p, const char *label) {
	rd *r = rd_open_compressed(p->filename);
	if (!r) {
		error("could not open package file %s", p->filename);
		return -1;
	}
	strs_free(&p->files);
	memset(&p->files, 0, sizeof p->files);
	strs dirs;
	memset(&dirs, 0, sizeof dirs);
	strs dirmodes;
	memset(&dirmodes, 0, sizeof dirmodes);
	tar_it t;
	tar_init(&t, r);
	tar_entry e;
	int owner = geteuid() == 0;
	int rc = 0;
	int tty = isatty(fileno(stderr));
	int bar = tty && p->nfiles > 0;
	int done = 0;
	if (bar) {
		bar_draw(label, 0, p->nfiles);
	} else {
		msg("%s", label);
	}
	while (tar_next(&t, &e) > 0) {
		char clean[4096];
		if (tar_safe_path(e.name, clean, sizeof clean) != 0) {
			warn("skipping unsafe path '%s'", e.name);
			tar_skip(&t);
			continue;
		}
		if (strchr(clean, '/') == NULL && clean[0] == '.') {
			tar_skip(&t);
			continue;
		}
		char rooted[4300];
		snprintf(rooted, sizeof rooted, "%s/%s", c->rootdir, clean);
		if (e.type == '5') {
			if (mkdir_p(rooted, e.mode & 07777) != 0 && !is_dir(rooted)) {
				error("could not create directory %s", rooted);
				rc = -1;
			}
			size_t cl = strlen(clean);
			char *ws = xmalloc(cl + 2);
			sprintf(ws, "%s%s", clean, (cl > 0 && clean[cl - 1] == '/') ? "" : "/");
			strs_add_own(&p->files, ws);
			strs_add(&dirs, rooted);
			char mbuf[32];
			snprintf(mbuf, sizeof mbuf, "%lld", e.mode);
			strs_add(&dirmodes, mbuf);
			tar_skip(&t);
			if (bar) bar_draw(label, ++done, p->nfiles);
		} else if (e.type == '0') {
			int is_backup = strs_has(&p->backup, clean);
			int is_noup = noupgrade_match(c, clean);
			int exists = lstat(rooted, &(struct stat){0}) == 0;
			char sha_in[65];
			if (is_backup && exists) {
				char tmpl[4300];
				snprintf(tmpl, sizeof tmpl, "%s/.nya-tmp-XXXXXX", c->rootdir);
				char *slash = strrchr(rooted, '/');
				if (slash) {
					snprintf(tmpl, sizeof tmpl, "%.*s/.nya-tmp-XXXXXX", (int)(slash - rooted), rooted);
				}
				int tfd = mkstemp(tmpl);
				if (tfd < 0) {
					error("could not create temp file for %s", rooted);
					rc = -1;
					tar_skip(&t);
					continue;
				}
				close(tfd);
				if (write_file_from_tar(&t, &e, tmpl, sha_in) != 0) {
					unlink(tmpl);
					rc = -1;
					continue;
				}
				char sha_ex[65];
				if (sha256_file(rooted, sha_ex) == 0 && strcmp(sha_ex, sha_in) == 0) {
					rename(tmpl, rooted);
					apply_meta(rooted, &e, owner);
				} else {
					char pacnew[4400];
					snprintf(pacnew, sizeof pacnew, "%s.pacnew", rooted);
					rename(tmpl, pacnew);
					apply_meta(pacnew, &e, owner);
					warn("%s installed as %s.pacnew", rooted, rooted);
				}
			} else if (is_noup && exists) {
				char pacnew[4400];
				snprintf(pacnew, sizeof pacnew, "%s.pacnew", rooted);
				if (write_file_from_tar(&t, &e, pacnew, sha_in) != 0) {
					rc = -1;
					continue;
				}
				apply_meta(pacnew, &e, owner);
				warn("%s installed as %s.pacnew", rooted, rooted);
			} else {
				if (write_file_from_tar(&t, &e, rooted, sha_in) != 0) {
					rc = -1;
					continue;
				}
				apply_meta(rooted, &e, owner);
			}
			strs_add(&p->files, clean);
			if (bar) bar_draw(label, ++done, p->nfiles);
		} else if (e.type == '2') {
			unlink(rooted);
			if (symlink(e.linkname, rooted) != 0) {
				warn("could not create symlink %s -> %s", rooted, e.linkname);
			}
			if (owner) lchown(rooted, e.uid, e.gid);
			strs_add(&p->files, clean);
			tar_skip(&t);
			if (bar) bar_draw(label, ++done, p->nfiles);
		} else if (e.type == '1') {
			char target[4096];
			if (tar_safe_path(e.linkname, target, sizeof target) == 0) {
				char rooted_t[4300];
				snprintf(rooted_t, sizeof rooted_t, "%s/%s", c->rootdir, target);
				unlink(rooted);
				if (link(rooted_t, rooted) != 0) {
					warn("could not create hardlink %s -> %s", rooted, rooted_t);
				}
			} else {
				warn("skipping unsafe hardlink '%s'", e.linkname);
			}
			strs_add(&p->files, clean);
			tar_skip(&t);
			if (bar) bar_draw(label, ++done, p->nfiles);
		} else if (e.type == '6') {
			unlink(rooted);
			mkfifo(rooted, e.mode & 07777);
			apply_meta(rooted, &e, owner);
			strs_add(&p->files, clean);
			tar_skip(&t);
			if (bar) bar_draw(label, ++done, p->nfiles);
		} else {
			tar_skip(&t);
		}
	}
	int i;
	for (i = dirs.n - 1; i >= 0; i--) {
		long long mode = atoll(dirmodes.v[i]);
		chmod(dirs.v[i], mode & 07777);
	}
	strs_free(&dirs);
	strs_free(&dirmodes);
	rd_close(r);
	if (bar) bar_done();
	return rc;
}

static void remove_old_files(config *c, pkg *p, int keep_backup) {
	int i;
	for (i = 0; i < p->files.n; i++) {
		const char *f = p->files.v[i];
		size_t fl = strlen(f);
		if (fl > 0 && f[fl - 1] == '/') continue;
		if (keep_backup && strs_has(&p->backup, f)) continue;
		if (noupgrade_match(c, f)) continue;
		char rooted[4300];
		snprintf(rooted, sizeof rooted, "%s/%s", c->rootdir, f);
		struct stat st;
		if (lstat(rooted, &st) != 0) continue;
		if (S_ISDIR(st.st_mode)) continue;
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
}

static int remove_pkg(config *c, pkg *p, int nosave) {
	if (p->install_data && p->install_len > 0) {
		run_scriptlet(c, p->install_data, p->install_len, "pre_remove", NULL, NULL);
	}
	hmap *sha = NULL;
	if (p->mtree_data && p->mtree_len > 0) {
		sha = mtree_sha_map(p->mtree_data, p->mtree_len);
	}
	int i;
	for (i = 0; i < p->files.n; i++) {
		const char *f = p->files.v[i];
		size_t fl = strlen(f);
		if (fl > 0 && f[fl - 1] == '/') continue;
		char rooted[4300];
		snprintf(rooted, sizeof rooted, "%s/%s", c->rootdir, f);
		struct stat st;
		if (lstat(rooted, &st) != 0) continue;
		if (S_ISDIR(st.st_mode)) continue;
		int is_backup = strs_has(&p->backup, f);
		if (is_backup && !nosave && S_ISREG(st.st_mode)) {
			char hex[65];
			if (sha256_file(rooted, hex) == 0) {
				const char *orig = sha ? hmap_get(sha, f) : NULL;
				if (!orig || strcasecmp(orig, hex) != 0) {
					char pacsave[4400];
					snprintf(pacsave, sizeof pacsave, "%s.pacsave", rooted);
					if (rename(rooted, pacsave) == 0) {
						warn("%s saved as %s", rooted, pacsave);
						continue;
					}
				}
			}
		}
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
	if (p->install_data && p->install_len > 0) {
		run_scriptlet(c, p->install_data, p->install_len, "post_remove", NULL, NULL);
	}
	db_remove_local(c, p->name, p->version);
	log_alpm("removed %s (%s)", p->name, p->version);
	remove_cached_tarballs(c, p);
	if (sha) hmap_free(sha);
	return 0;
}

static void remove_cached_tarballs(config *c, pkg *p) {
	if (!p->name) return;
	int i;
	for (i = 0; i < c->cachedirs.n; i++) {
		const char *dir = cfg_rooted(c, c->cachedirs.v[i]);
		if (p->filename && *p->filename) {
			char path[4600];
			snprintf(path, sizeof path, "%s/%s", dir, p->filename);
			if (is_file(path)) {
				unlink(path);
				log_alpm("removed cached file %s", p->filename);
			}
		}
		char prefix[512];
		snprintf(prefix, sizeof prefix, "%s-", p->name);
		size_t plen = strlen(prefix);
		DIR *d = opendir(dir);
		if (!d) continue;
		struct dirent *de;
		while ((de = readdir(d)) != NULL) {
			const char *n = de->d_name;
			if (strncmp(n, prefix, plen) != 0) continue;
			if (!isdigit((unsigned char)n[plen])) continue;
			if (!strstr(n, ".pkg.tar.")) continue;
			char path[4600];
			snprintf(path, sizeof path, "%s/%s", dir, n);
			if (is_file(path)) {
				unlink(path);
				log_alpm("removed cached file %s", n);
			}
		}
		closedir(d);
	}
}

static int in_rm_list(txn *t, const char *name) {
	int i;
	for (i = 0; i < t->nrm; i++) {
		if (t->rm[i]->name && strcmp(t->rm[i]->name, name) == 0) return 1;
	}
	return 0;
}

static int install_pkg(config *c, pkg *p, txn *t, const char *label) {
	pkg *old = db_find_local(p->name);
	if (old && in_rm_list(t, old->name)) old = NULL;
	if (p->is_upgrade && old) {
		if (p->install_data && p->install_len > 0) {
			run_scriptlet(c, p->install_data, p->install_len, "pre_upgrade", p->version, old->version);
		}
		remove_old_files(c, old, 1);
	} else {
		if (p->install_data && p->install_len > 0) {
			run_scriptlet(c, p->install_data, p->install_len, "pre_install", p->version, NULL);
		}
	}
	if (extract_pkg(c, p, label) != 0) return -1;
	if (p->is_upgrade && old) {
		if (p->install_data && p->install_len > 0) {
			run_scriptlet(c, p->install_data, p->install_len, "post_upgrade", p->version, old->version);
		}
	} else {
		if (p->install_data && p->install_len > 0) {
			run_scriptlet(c, p->install_data, p->install_len, "post_install", p->version, NULL);
		}
	}
	free(p->validation);
	p->validation = xstrdup((p->sha256sum && *p->sha256sum) ? "sha256" : "none");
	if (db_write_local_pkg(c, p) != 0) return -1;
	if (p->is_upgrade && old) {
		if (strcmp(old->version, p->version) != 0) {
			db_remove_local(c, old->name, old->version);
		}
		log_alpm("upgraded %s (%s -> %s)", p->name, old->version, p->version);
	} else {
		log_alpm("installed %s (%s)", p->name, p->version);
	}
	return 0;
}

int txn_download(config *c, txn *t) {
	if (t->nadd == 0) return 0;
	if (c->cachedirs.n > 0) mkdir_p(c->cachedirs.v[0], 0755);
	dl *jobs = xcalloc(t->nadd, sizeof *jobs);
	pkg **need = xcalloc(t->nadd, sizeof *need);
	char **tmps = xcalloc(t->nadd, sizeof *tmps);
	int n = 0;
	int i;
	for (i = 0; i < t->nadd; i++) {
		pkg *p = t->add[i];
		if (!p->filename || p->filename[0] == '/' || startswith(p->filename, "./")) continue;
		if (!p->repo) continue;
		char cached[4096];
		if (cache_find(c, p->filename, cached, sizeof cached) == 0) {
			if (pkg_verify_file(c, p, cached) == 0) {
				free(p->filename);
				p->filename = xstrdup(cached);
				continue;
			}
		}
		repo *r = config_find_repo(c, p->repo);
		if (!r || r->servers.n == 0) {
			error("no servers configured for repository '%s'", p->repo);
			for (i = 0; i < n; i++) {
				free(tmps[i]);
				int j;
				for (j = 0; j < jobs[i].nurls; j++) free(jobs[i].urls[j]);
				free(jobs[i].urls);
			}
			free(jobs);
			free(need);
			free(tmps);
			return -1;
		}
		char tmp[4200];
		snprintf(tmp, sizeof tmp, "%s/.nya-%s-%ld", c->cachedirs.v[0], p->filename, (long)getpid());
		jobs[n].dest = xstrdup(tmp);
		jobs[n].urls = xcalloc(r->servers.n, sizeof(char *));
		int j;
		for (j = 0; j < r->servers.n; j++) {
			size_t sl = strlen(r->servers.v[j]);
			int need_slash = sl > 0 && r->servers.v[j][sl - 1] != '/';
			char *u = xmalloc(sl + need_slash + strlen(p->filename) + 1);
			sprintf(u, "%s%s%s", r->servers.v[j], need_slash ? "/" : "", p->filename);
			jobs[n].urls[j] = u;
			jobs[n].nurls++;
		}
		tmps[n] = xstrdup(tmp);
		need[n] = p;
		n++;
	}
	if (n > 0) {
		if (g_verbose) msg("downloading %d package(s)", n);
		dl_parallel(c, jobs, n);
	}
	int rc = 0;
	for (i = 0; i < n; i++) {
		if (jobs[i].ok) {
			if (pkg_verify_file(c, need[i], tmps[i]) != 0) {
				unlink(tmps[i]);
				error("failed to retrieve some files");
				rc = -1;
				continue;
			}
			char final[4200];
			snprintf(final, sizeof final, "%s/%s", c->cachedirs.v[0], need[i]->filename);
			mkdir_p(c->cachedirs.v[0], 0755);
			rename(tmps[i], final);
			free(need[i]->filename);
			need[i]->filename = xstrdup(final);
		} else {
			unlink(tmps[i]);
			if (jobs[i].err[0]) error("%s", jobs[i].err);
			rc = -1;
		}
	}
	for (i = 0; i < n; i++) {
		free(tmps[i]);
		int j;
		for (j = 0; j < jobs[i].nurls; j++) free(jobs[i].urls[j]);
		free(jobs[i].urls);
		free((char *)jobs[i].dest);
		free(jobs[i].data);
	}
	free(jobs);
	free(need);
	free(tmps);
	return rc;
}

static void fmt_summary_line(const char *label, long long bytes, char *out, size_t n) {
	char sz[64];
	fmt_size(bytes, sz, sizeof sz);
	snprintf(out, n, "%s%*s", label, (int)(24 - strlen(label)), sz);
}

int txn_print_summary(config *c, txn *t, int mode) {
	(void)c;
	long long dl = 0, isz = 0, rsz = 0;
	int total = t->nadd + t->nrm;
	char list[16384] = "";
	int i;
	for (i = 0; i < t->nadd; i++) {
		char tmp[512];
		snprintf(tmp, sizeof tmp, "%s%s-%s ", list[0] ? " " : "", t->add[i]->name, t->add[i]->version);
		if (strlen(list) + strlen(tmp) < sizeof list) strncat(list, tmp, sizeof list - strlen(list) - 1);
		dl += t->add[i]->csize;
		isz += t->add[i]->isize;
	}
	for (i = 0; i < t->nrm; i++) {
		char tmp[512];
		snprintf(tmp, sizeof tmp, "%s%s-%s ", list[0] ? " " : "", t->rm[i]->name, t->rm[i]->version);
		if (strlen(list) + strlen(tmp) < sizeof list) strncat(list, tmp, sizeof list - strlen(list) - 1);
		rsz += t->rm[i]->isize;
	}
	msg("%sPackages (%d)%s %s", col_bold(), total, col_reset(), list);
	msg("");
	char line[128];
	if (mode == 0 || mode == 2) {
		fmt_summary_line("Total Download Size:", dl, line, sizeof line);
		msg("%s", line);
		fmt_summary_line("Total Installed Size:", isz, line, sizeof line);
		msg("%s", line);
	}
	if (mode == 1) {
		fmt_summary_line("Total Removed Size:", rsz, line, sizeof line);
		msg("%s", line);
	}
	msg("");
	return 0;
}

typedef struct hookev {
	char *op;
	char *name;
	strs paths;
} hookev;

typedef struct hookfile {
	strs ops;
	strs types;
	strs targets;
	strs whens;
	strs execs;
	char *desc;
	int needs_targets;
} hookfile;

static void hookfile_free(hookfile *h) {
	strs_free(&h->ops);
	strs_free(&h->types);
	strs_free(&h->targets);
	strs_free(&h->whens);
	strs_free(&h->execs);
	free(h->desc);
}

static int parse_hook(const char *path, hookfile *h) {
	FILE *f = fopen(path, "r");
	if (!f) return -1;
	char line[4096];
	int in_action = 0;
	while (fgets(line, sizeof line, f)) {
		char *s = trim(line);
		if (*s == '\0' || *s == '#') continue;
		if (*s == '[') {
			char *end = strchr(s, ']');
			if (!end) continue;
			*end = '\0';
			in_action = str_ieq(trim(s + 1), "Action");
			continue;
		}
		char *eq = strchr(s, '=');
		if (!eq) {
			if (in_action && str_ieq(s, "NeedsTargets")) h->needs_targets = 1;
			continue;
		}
		*eq = '\0';
		char *key = trim(s);
		char *val = trim(eq + 1);
		if (in_action) {
			if (str_ieq(key, "Exec")) strs_add(&h->execs, val);
			else if (str_ieq(key, "When")) strs_add(&h->whens, val);
			else if (str_ieq(key, "Description")) {
				free(h->desc);
				h->desc = xstrdup(val);
			}
		} else {
			if (str_ieq(key, "Operation")) strs_add(&h->ops, val);
			else if (str_ieq(key, "Type")) strs_add(&h->types, val);
			else if (str_ieq(key, "Target")) strs_add(&h->targets, val);
		}
	}
	fclose(f);
	return 0;
}

static int hook_when_matches(hookfile *h, int pre) {
	const char *want = pre ? "PreTransaction" : "PostTransaction";
	if (h->whens.n == 0) return pre ? 0 : 1;
	int i;
	for (i = 0; i < h->whens.n; i++) {
		if (str_ieq(h->whens.v[i], want)) return 1;
	}
	return 0;
}

static char *subst_hook(const char *exec, const char *targets, const char *files) {
	char *out = xmalloc(strlen(exec) + (targets ? strlen(targets) : 0) + (files ? strlen(files) : 0) + 64);
	const char *p = exec;
	char *o = out;
	while (*p) {
		if (strncmp(p, "%TARGET%", 8) == 0) {
			if (targets) {
				size_t l = strlen(targets);
				memcpy(o, targets, l);
				o += l;
			}
			p += 8;
		} else if (strncmp(p, "%FILES%", 7) == 0) {
			if (files) {
				size_t l = strlen(files);
				memcpy(o, files, l);
				o += l;
			}
			p += 7;
		} else {
			*o++ = *p++;
		}
	}
	*o = '\0';
	return out;
}

static int run_sh_pipe(config *c, const char *cmd, const char *stdin_data) {
	int fds[2] = {-1, -1};
	if (stdin_data && pipe(fds) != 0) stdin_data = NULL;
	pid_t pid = fork();
	if (pid < 0) {
		if (fds[0] >= 0) {
			close(fds[0]);
			close(fds[1]);
		}
		return -1;
	}
	if (pid == 0) {
		int in = fds[0];
		if (in < 0) in = open("/dev/null", O_RDONLY);
		if (in >= 0) {
			dup2(in, STDIN_FILENO);
			if (in != STDIN_FILENO) close(in);
		}
		if (fds[1] >= 0) close(fds[1]);
		chdir(c->rootdir);
		execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
		_exit(127);
	}
	if (fds[0] >= 0) close(fds[0]);
	if (fds[1] >= 0) {
		if (stdin_data) {
			size_t l = strlen(stdin_data), off = 0;
			while (off < l) {
				ssize_t w = write(fds[1], stdin_data + off, l - off);
				if (w <= 0) break;
				off += (size_t)w;
			}
		}
		close(fds[1]);
	}
	int st;
	while (waitpid(pid, &st, 0) < 0) {
		if (errno != EINTR) break;
	}
	if (WIFEXITED(st)) return WEXITSTATUS(st);
	return -1;
}

int run_hooks(config *c, txn *t, int pre) {
	int nevents = t->nadd + t->nrm;
	if (nevents == 0) return 0;
	hookev *ev = xcalloc(nevents, sizeof *ev);
	int ei = 0;
	int i;
	for (i = 0; i < t->nadd; i++) {
		ev[ei].op = xstrdup(t->add[i]->is_upgrade ? "Upgrade" : "Install");
		ev[ei].name = xstrdup(t->add[i]->name);
		int j;
		for (j = 0; j < t->add[i]->files.n; j++) {
			char p[4300];
			snprintf(p, sizeof p, "%s/%s", c->rootdir, t->add[i]->files.v[j]);
			strs_add(&ev[ei].paths, p);
		}
		ei++;
	}
	for (i = 0; i < t->nrm; i++) {
		ev[ei].op = xstrdup("Remove");
		ev[ei].name = xstrdup(t->rm[i]->name);
		int j;
		for (j = 0; j < t->rm[i]->files.n; j++) {
			char p[4300];
			snprintf(p, sizeof p, "%s/%s", c->rootdir, t->rm[i]->files.v[j]);
			strs_add(&ev[ei].paths, p);
		}
		ei++;
	}
	const char *hdirs[2] = {"/usr/share/libalpm/hooks", "/etc/pacman.d/hooks"};
	strs hooks_s;
	memset(&hooks_s, 0, sizeof hooks_s);
	int h;
	for (h = 0; h < 2; h++) {
		char hroot[4096];
		snprintf(hroot, sizeof hroot, "%s%s", c->rootdir, hdirs[h]);
		DIR *d = opendir(hroot);
		if (!d) continue;
		struct dirent *de;
		while ((de = readdir(d)) != NULL) {
			size_t dl = strlen(de->d_name);
			if (dl < 6 || strcmp(de->d_name + dl - 5, ".hook") != 0) continue;
			char hpath[4200];
			snprintf(hpath, sizeof hpath, "%s/%s", hroot, de->d_name);
			hookfile hf;
			memset(&hf, 0, sizeof hf);
			if (parse_hook(hpath, &hf) != 0 || !hook_when_matches(&hf, pre)) {
				hookfile_free(&hf);
			continue;
		}
		int matched = 0;
		int k;
		for (k = 0; k < nevents && !matched; k++) {
			int op_ok = 0;
			int o2;
			for (o2 = 0; o2 < hf.ops.n; o2++) {
				if (strcmp(hf.ops.v[o2], ev[k].op) == 0) {
					op_ok = 1;
					break;
				}
			}
			if (!op_ok) continue;
			int t2;
			for (t2 = 0; t2 < hf.types.n && !matched; t2++) {
				if (str_ieq(hf.types.v[t2], "Package")) {
					int g;
					for (g = 0; g < hf.targets.n && !matched; g++) {
						if (fnmatch(hf.targets.v[g], ev[k].name, 0) == 0) matched = 1;
					}
				} else {
					int g;
					for (g = 0; g < hf.targets.n && !matched; g++) {
						int p2;
						for (p2 = 0; p2 < ev[k].paths.n && !matched; p2++) {
							const char *rel = ev[k].paths.v[p2] + strlen(c->rootdir);
							while (*rel == '/') rel++;
							if (fnmatch(hf.targets.v[g], rel, 0) == 0) matched = 1;
						}
					}
				}
			}
		}
		hookfile_free(&hf);
		if (matched) strs_add(&hooks_s, hpath);
		}
		closedir(d);
	}
	if (hooks_s.n > 1) {
		int x;
		for (x = 0; x < hooks_s.n; x++) {
			int y;
			for (y = x + 1; y < hooks_s.n; y++) {
				const char *ax = strrchr(hooks_s.v[x], '/') + 1;
				const char *by = strrchr(hooks_s.v[y], '/') + 1;
				if (strcmp(ax, by) > 0) {
					char *tmp = hooks_s.v[x];
					hooks_s.v[x] = hooks_s.v[y];
					hooks_s.v[y] = tmp;
				}
			}
		}
	}
	if (hooks_s.n > 0) {
		msg("Running %s hooks...", pre ? "pre-transaction" : "post-transaction");
		int seq = 0;
		for (h = 0; h < hooks_s.n; h++) {
			hookfile hf;
			memset(&hf, 0, sizeof hf);
			if (parse_hook(hooks_s.v[h], &hf) != 0) {
				hookfile_free(&hf);
				continue;
			}
			strs targets_s;
			memset(&targets_s, 0, sizeof targets_s);
			strs files_s;
			memset(&files_s, 0, sizeof files_s);
			strs files_rel_s;
			memset(&files_rel_s, 0, sizeof files_rel_s);
			int matched = 0;
			int k;
			for (k = 0; k < nevents; k++) {
				int op_ok = 0;
				int o2;
				for (o2 = 0; o2 < hf.ops.n; o2++) {
					if (strcmp(hf.ops.v[o2], ev[k].op) == 0) {
						op_ok = 1;
						break;
					}
				}
				if (!op_ok) continue;
				int t2;
				for (t2 = 0; t2 < hf.types.n; t2++) {
					if (str_ieq(hf.types.v[t2], "Package")) {
						int g;
						for (g = 0; g < hf.targets.n; g++) {
							if (fnmatch(hf.targets.v[g], ev[k].name, 0) == 0) {
								if (!strs_has(&targets_s, ev[k].name)) strs_add(&targets_s, ev[k].name);
								matched = 1;
							}
						}
					} else {
						int g;
						for (g = 0; g < hf.targets.n; g++) {
							int p2;
							for (p2 = 0; p2 < ev[k].paths.n; p2++) {
								const char *rel = ev[k].paths.v[p2] + strlen(c->rootdir);
								while (*rel == '/') rel++;
								if (fnmatch(hf.targets.v[g], rel, 0) == 0) {
									if (!strs_has(&files_s, ev[k].paths.v[p2])) strs_add(&files_s, ev[k].paths.v[p2]);
									if (!strs_has(&files_rel_s, rel)) strs_add(&files_rel_s, rel);
									matched = 1;
								}
							}
						}
					}
				}
			}
			if (matched && hf.execs.n > 0) {
				char tbuf[8192] = "";
				char fbuf[65536] = "";
				char *payload = NULL;
				int x;
				for (x = 0; x < targets_s.n; x++) {
					if (tbuf[0]) strncat(tbuf, " ", sizeof tbuf - strlen(tbuf) - 1);
					strncat(tbuf, targets_s.v[x], sizeof tbuf - strlen(tbuf) - 1);
				}
				for (x = 0; x < files_s.n; x++) {
					strncat(fbuf, files_s.v[x], sizeof fbuf - strlen(fbuf) - 1);
					strncat(fbuf, "\n", sizeof fbuf - strlen(fbuf) - 1);
				}
				if (hf.needs_targets) {
					size_t plen = 1;
					for (x = 0; x < files_rel_s.n; x++) plen += strlen(files_rel_s.v[x]) + 1;
					for (x = 0; x < targets_s.n; x++) plen += strlen(targets_s.v[x]) + 1;
					payload = xmalloc(plen);
					char *pp = payload;
					for (x = 0; x < files_rel_s.n; x++) {
						size_t l = strlen(files_rel_s.v[x]);
						memcpy(pp, files_rel_s.v[x], l);
						pp += l;
						*pp++ = '\n';
					}
					for (x = 0; x < targets_s.n; x++) {
						size_t l = strlen(targets_s.v[x]);
						memcpy(pp, targets_s.v[x], l);
						pp += l;
						*pp++ = '\n';
					}
					*pp = '\0';
				}
				const char *base = strrchr(hooks_s.v[h], '/');
				base = base ? base + 1 : hooks_s.v[h];
				printf("(%d/%d) %s...\n", ++seq, hooks_s.n, hf.desc ? hf.desc : base);
				int e2;
				for (e2 = 0; e2 < hf.execs.n; e2++) {
					char *cmd = subst_hook(hf.execs.v[e2], tbuf, fbuf);
					run_sh_pipe(c, cmd, payload);
					free(cmd);
				}
				free(payload);
			}
			strs_free(&targets_s);
			strs_free(&files_s);
			strs_free(&files_rel_s);
			hookfile_free(&hf);
		}
		strs_free(&hooks_s);
	}
	for (i = 0; i < nevents; i++) {
		free(ev[i].op);
		free(ev[i].name);
		strs_free(&ev[i].paths);
	}
	free(ev);
	return 0;
}

int txn_commit(config *c, txn *t) {
	if (t->nadd == 0 && t->nrm == 0) return 0;
	if (lock_db(c) != 0) return -1;
	log_alpm("transaction started");
	run_hooks(c, t, 1);
	int i;
	for (i = 0; i < t->nrm; i++) {
		msg("%s(%d/%d) removing %s%s", col_green(), i + 1, t->nrm + t->nadd, t->rm[i]->name, col_reset());
		if (remove_pkg(c, t->rm[i], t->nosave) != 0) {
			unlock_db();
			return -1;
		}
	}
	int seq = 1;
	char label[512];
	for (i = 0; i < t->nadd; i++) {
		pkg *p = t->add[i];
		if (!p->is_dep && p->reason != 1) continue;
		snprintf(label, sizeof label, "%s(%d/%d) %s %s%s", col_green(), seq++, t->nrm + t->nadd,
		         p->is_reinstall ? "reinstalling" : p->is_upgrade ? "upgrading" : "installing", p->name, col_reset());
		if (install_pkg(c, p, t, label) != 0) {
			unlock_db();
			return -1;
		}
	}
	for (i = 0; i < t->nadd; i++) {
		pkg *p = t->add[i];
		if (p->is_dep || p->reason == 1) continue;
		snprintf(label, sizeof label, "%s(%d/%d) %s %s%s", col_green(), seq++, t->nrm + t->nadd,
		         p->is_reinstall ? "reinstalling" : p->is_upgrade ? "upgrading" : "installing", p->name, col_reset());
		if (install_pkg(c, p, t, label) != 0) {
			unlock_db();
			return -1;
		}
	}
	run_hooks(c, t, 0);
	log_alpm("transaction completed");
	unlock_db();
	g_nlocal = 0;
	free(g_local);
	g_local = NULL;
	db_load_local(c);
	return 0;
}

int install_pkgfile(config *c, const char *path) {
	pkg *p = pkg_new("aur");
	p->filename = xstrdup(path);
	if (pkg_scan_archive(c, path, p) != 0) {
		pkg_free(p);
		return -1;
	}
	txn dummy;
	memset(&dummy, 0, sizeof dummy);
	char label[512];
	snprintf(label, sizeof label, "installing %s", p->name);
	int rc = install_pkg(c, p, &dummy, label);
	pkg_free(p);
	return rc;
}
