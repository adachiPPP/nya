#include "nya.h"

pkg **g_sync = NULL;
int g_nsync = 0;
pkg **g_local = NULL;
int g_nlocal = 0;
hmap *g_owner = NULL;

static void db_add_strs(strs *s, const char *val) {
	strs tmp;
	memset(&tmp, 0, sizeof tmp);
	strs_split_ws(val, &tmp);
	int i;
	for (i = 0; i < tmp.n; i++) {
		if (!strs_has(s, tmp.v[i])) strs_add(s, tmp.v[i]);
	}
	strs_free(&tmp);
}

pkg *pkg_new(const char *repo) {
	pkg *p = xcalloc(1, sizeof *p);
	p->repo = repo ? xstrdup(repo) : NULL;
	p->reason = 0;
	return p;
}

void pkg_free(pkg *p) {
	if (!p) return;
	free(p->repo);
	free(p->name);
	free(p->base);
	free(p->version);
	free(p->desc);
	free(p->url);
	free(p->arch);
	free(p->packager);
	free(p->builddate);
	free(p->installdate);
	free(p->md5sum);
	free(p->sha256sum);
	free(p->pgpsig);
	free(p->validation);
	free(p->filename);
	free(p->mtree_data);
	free(p->install_data);
	strs_free(&p->provides);
	strs_free(&p->depends);
	strs_free(&p->optdepends);
	strs_free(&p->conflicts);
	strs_free(&p->replaces);
	strs_free(&p->groups);
	strs_free(&p->licenses);
	strs_free(&p->backup);
	strs_free(&p->files);
	free(p);
}

static hmap *g_sync_idx;

static void sync_index_add(pkg *p) {
	if (!g_sync_idx || !p->name || !p->version || !p->repo) return;
	char tmp[1100];
	snprintf(tmp, sizeof tmp, "%s/%s-%s", p->repo, p->name, p->version);
	hmap_put(g_sync_idx, tmp, p);
}

static void sync_add(pkg *p) {
	g_sync = xrealloc(g_sync, (g_nsync + 1) * sizeof(pkg *));
	g_sync[g_nsync++] = p;
	sync_index_add(p);
}

static void local_add(pkg *p) {
	g_local = xrealloc(g_local, (g_nlocal + 1) * sizeof(pkg *));
	g_local[g_nlocal++] = p;
}

static pkg *sync_find_dir(const char *reponame, const char *dir) {
	if (g_sync_idx) {
		char tmp[1100];
		snprintf(tmp, sizeof tmp, "%s/%s", reponame, dir);
		return hmap_get(g_sync_idx, tmp);
	}
	int i;
	for (i = 0; i < g_nsync; i++) {
		pkg *p = g_sync[i];
		if (strcmp(p->repo, reponame) != 0) continue;
		if (!p->name || !p->version) continue;
		char tmp[1024];
		snprintf(tmp, sizeof tmp, "%s-%s", p->name, p->version);
		if (strcmp(tmp, dir) == 0) return p;
	}
	return NULL;
}

static void name_version_split(const char *dir, char *name, size_t nlen, char *ver, size_t vlen) {
	const char *dash = strrchr(dir, '-');
	if (dash) {
		size_t nl = dash - dir;
		if (nl >= nlen) nl = nlen - 1;
		memcpy(name, dir, nl);
		name[nl] = '\0';
		snprintf(ver, vlen, "%s", dash + 1);
	} else {
		snprintf(name, nlen, "%s", dir);
		snprintf(ver, vlen, "%s", "0");
	}
}

static void desc_set_str(char **dst, const char *val) {
	free(*dst);
	*dst = xstrdup(val);
}

static void desc_into_pkg(pkg *p, const char *data) {
	const char *p2 = data;
	char key[64];
	char val[8192];
	for (;;) {
		const char *nl = strchr(p2, '\n');
		size_t linelen = nl ? (size_t)(nl - p2) : strlen(p2);
		if (linelen >= 2 && p2[0] == '%' && p2[linelen - 1] == '%') {
			snprintf(key, sizeof key, "%.*s", (int)(linelen - 2), p2 + 1);
			p2 = nl ? nl + 1 : p2 + linelen;
			size_t vlen = 0;
			val[0] = '\0';
			while (*p2 && !(p2[0] == '%')) {
				const char *nl2 = strchr(p2, '\n');
				size_t l2 = nl2 ? (size_t)(nl2 - p2) : strlen(p2);
				if (l2 > 0 && l2 + vlen + 2 < sizeof val) {
					if (vlen > 0) val[vlen++] = '\n';
					memcpy(val + vlen, p2, l2);
					vlen += l2;
					val[vlen] = '\0';
				}
				if (!nl2) break;
				p2 = nl2 + 1;
			}
			val[vlen] = '\0';
			trim(val);
			if (strcmp(key, "NAME") == 0) desc_set_str(&p->name, val);
			else if (strcmp(key, "BASE") == 0) desc_set_str(&p->base, val);
			else if (strcmp(key, "VERSION") == 0) desc_set_str(&p->version, val);
			else if (strcmp(key, "DESC") == 0) desc_set_str(&p->desc, val);
			else if (strcmp(key, "URL") == 0) desc_set_str(&p->url, val);
			else if (strcmp(key, "ARCH") == 0) desc_set_str(&p->arch, val);
			else if (strcmp(key, "PACKAGER") == 0) desc_set_str(&p->packager, val);
			else if (strcmp(key, "BUILDDATE") == 0) {
				desc_set_str(&p->builddate, val);
				p->builddate_ts = atoll(val);
			} else if (strcmp(key, "INSTALLDATE") == 0) {
				desc_set_str(&p->installdate, val);
				p->installdate_ts = atoll(val);
			} else if (strcmp(key, "SIZE") == 0) {
				p->isize = atoll(val);
			} else if (strcmp(key, "CSIZE") == 0) {
				p->csize = atoll(val);
			} else if (strcmp(key, "ISIZE") == 0) {
				p->isize = atoll(val);
			} else if (strcmp(key, "MD5SUM") == 0) desc_set_str(&p->md5sum, val);
			else if (strcmp(key, "SHA256SUM") == 0) desc_set_str(&p->sha256sum, val);
			else if (strcmp(key, "PGPSIG") == 0) desc_set_str(&p->pgpsig, val);
			else if (strcmp(key, "VALIDATION") == 0) desc_set_str(&p->validation, val);
			else if (strcmp(key, "FILENAME") == 0) desc_set_str(&p->filename, val);
			else if (strcmp(key, "REASON") == 0) p->reason = atoi(val);
			else if (strcmp(key, "LICENSE") == 0) db_add_strs(&p->licenses, val);
			else if (strcmp(key, "GROUPS") == 0) db_add_strs(&p->groups, val);
			else if (strcmp(key, "REPLACES") == 0) db_add_strs(&p->replaces, val);
			else if (strcmp(key, "CONFLICTS") == 0) db_add_strs(&p->conflicts, val);
			else if (strcmp(key, "PROVIDES") == 0) db_add_strs(&p->provides, val);
			else if (strcmp(key, "DEPENDS") == 0) db_add_strs(&p->depends, val);
			else if (strcmp(key, "OPTDEPENDS") == 0) db_add_strs(&p->optdepends, val);
			else if (strcmp(key, "BACKUP") == 0) db_add_strs(&p->backup, val);
		} else {
			if (!nl) break;
			p2 = nl + 1;
		}
	}
	if (!p->base) p->base = xstrdup(p->name ? p->name : "");
	if (!p->filename && p->name && p->version && p->arch) {
		char *f = xmalloc(strlen(p->name) + strlen(p->version) + strlen(p->arch) + 32);
		sprintf(f, "%s-%s-%s.pkg.tar.zst", p->name, p->version, p->arch);
		p->filename = f;
	}
}

static void files_into_pkg(pkg *p, const char *data) {
	const char *p2 = data;
	int in_files = 0;
	int in_backup = 0;
	for (;;) {
		const char *nl = strchr(p2, '\n');
		size_t linelen = nl ? (size_t)(nl - p2) : strlen(p2);
		if (linelen >= 2 && p2[0] == '%' && p2[linelen - 1] == '%') {
			in_files = linelen == 7 && strncmp(p2, "%FILES%", 7) == 0;
			in_backup = linelen == 8 && strncmp(p2, "%BACKUP%", 8) == 0;
			p2 = nl ? nl + 1 : p2 + linelen;
			continue;
		}
		if (linelen > 0) {
			char *line = xstrndup(p2, linelen);
			if (in_files) strs_add(&p->files, line);
			else if (in_backup) {
				char *tab = strchr(line, '\t');
				if (tab) *tab = '\0';
				strs_add(&p->backup, line);
			}
			free(line);
		}
		if (!nl) break;
		p2 = nl + 1;
	}
}

int db_load_sync(config *c, const char *dbfile, const char *reponame) {
	(void)c;
	if (!is_file(dbfile)) return 0;
	rd *r = rd_open_compressed(dbfile);
	if (!r) {
		error("could not open database file %s", dbfile);
		return -1;
	}
	tar_it t;
	tar_init(&t, r);
	tar_entry e;
	pkg *cur = NULL;
	char curdir[1024] = "";
	while (tar_next(&t, &e) > 0) {
		const char *entry = e.name;
		if (startswith(entry, "./")) entry += 2;
		const char *slash = strchr(entry, '/');
		if (!slash) {
			tar_skip(&t);
			continue;
		}
		char dir[1024];
		size_t dlen = slash - entry;
		if (dlen >= sizeof dir) dlen = sizeof dir - 1;
		memcpy(dir, entry, dlen);
		dir[dlen] = '\0';
		const char *fname = slash + 1;
		if (strcmp(curdir, dir) != 0) {
			snprintf(curdir, sizeof curdir, "%s", dir);
			cur = sync_find_dir(reponame, dir);
			if (!cur) {
				char name[512], ver[512];
				name_version_split(dir, name, sizeof name, ver, sizeof ver);
				cur = pkg_new(reponame);
				cur->name = xstrdup(name);
				cur->version = xstrdup(ver);
				sync_add(cur);
			}
		}
		char *data = NULL;
		if (e.size > 0) {
			data = xmalloc(e.size + 1);
			long off = 0;
			while (off < e.size) {
				long got = tar_read(&t, data + off, e.size - off);
				if (got <= 0) break;
				off += got;
			}
			data[off] = '\0';
		} else {
			tar_skip(&t);
		}
		if (data) {
			if (strcmp(fname, "desc") == 0) desc_into_pkg(cur, data);
			else if (strcmp(fname, "files") == 0) files_into_pkg(cur, data);
			else if (strcmp(fname, "depends") == 0) {
				strs tmp;
				memset(&tmp, 0, sizeof tmp);
				strs_split_ws(data, &tmp);
				int i;
				for (i = 0; i < tmp.n; i++) {
					if (!strs_has(&cur->depends, tmp.v[i])) strs_add(&cur->depends, tmp.v[i]);
				}
				strs_free(&tmp);
			} else if (strcmp(fname, "mtree") == 0) cur->has_mtree = 1;
			free(data);
		}
	}
	rd_close(r);
	return 0;
}

int db_load_local(config *c) {
	char localdir[4096];
	snprintf(localdir, sizeof localdir, "%s/local", c->dbpath);
	DIR *d = opendir(localdir);
	if (!d) {
		mkdir_p(localdir, 0755);
		d = opendir(localdir);
		if (!d) return -1;
	}
	struct dirent *de;
	while ((de = readdir(d)) != NULL) {
		if (de->d_name[0] == '.') continue;
		char entry[4096];
		snprintf(entry, sizeof entry, "%s/%s", localdir, de->d_name);
		if (!is_dir(entry)) continue;
		pkg *p = pkg_new("local");
		p->is_local = 1;
		char path[4600];
		snprintf(path, sizeof path, "%s/desc", entry);
		char *data = read_file(path, NULL);
		if (!data) {
			pkg_free(p);
			continue;
		}
		desc_into_pkg(p, data);
		free(data);
		snprintf(path, sizeof path, "%s/files", entry);
		data = read_file(path, NULL);
		if (data) {
			files_into_pkg(p, data);
			free(data);
		}
		snprintf(path, sizeof path, "%s/mtree", entry);
		if (is_file(path)) {
			p->has_mtree = 1;
			long mlen = 0;
			data = read_file(path, &mlen);
			if (data) {
				p->mtree_data = data;
				p->mtree_len = mlen;
			}
		}
		snprintf(path, sizeof path, "%s/install", entry);
		if (is_file(path)) {
			long ilen = 0;
			data = read_file(path, &ilen);
			if (data) {
				p->install_data = data;
				p->install_len = ilen;
			}
		}
		if (!p->name) {
			char name[512], ver[512];
			name_version_split(de->d_name, name, sizeof name, ver, sizeof ver);
			p->name = xstrdup(name);
			if (!p->version) p->version = xstrdup(ver);
		}
		local_add(p);
	}
	closedir(d);
	db_build_owner_map();
	return 0;
}

int db_load_all(config *c) {
	int i;
	if (g_sync_idx) hmap_free(g_sync_idx);
	g_sync_idx = hmap_new(1 << 17);
	for (i = 0; i < g_nsync; i++) sync_index_add(g_sync[i]);
	for (i = 0; i < c->nrepos; i++) {
		char dbfile[4096];
		snprintf(dbfile, sizeof dbfile, "%s/sync/%s.db", c->dbpath, c->repos[i]->name);
		if (is_file(dbfile)) {
			db_load_sync(c, dbfile, c->repos[i]->name);
		} else if (c->nrepos > 0) {
			warn("no database file for repository '%s' (use 'nya update')", c->repos[i]->name);
		}
	}
	db_load_local(c);
	return 0;
}

pkg *db_find_sync(const char *name) {
	int i;
	for (i = 0; i < g_nsync; i++) {
		if (g_sync[i]->name && strcmp(g_sync[i]->name, name) == 0) return g_sync[i];
	}
	return NULL;
}

pkg *db_find_sync_exact(const char *repo, const char *name) {
	int i;
	if (repo) {
		for (i = 0; i < g_nsync; i++) {
			if (g_sync[i]->name && strcmp(g_sync[i]->name, name) == 0 &&
			    strcmp(g_sync[i]->repo, repo) == 0)
				return g_sync[i];
		}
		return NULL;
	}
	return db_find_sync(name);
}

pkg *db_find_local(const char *name) {
	int i;
	for (i = 0; i < g_nlocal; i++) {
		if (g_local[i]->name && strcmp(g_local[i]->name, name) == 0) return g_local[i];
	}
	return NULL;
}

void db_build_owner_map(void) {
	if (g_owner) hmap_free(g_owner);
	g_owner = hmap_new(1 << 15);
	int i, j;
	for (i = 0; i < g_nlocal; i++) {
		pkg *p = g_local[i];
		for (j = 0; j < p->files.n; j++) {
			const char *f = p->files.v[j];
			size_t fl = strlen(f);
			if (fl > 0 && f[fl - 1] == '/') continue;
			if (!hmap_has(g_owner, f)) hmap_put(g_owner, f, p->name);
		}
	}
}

const char *db_owner(const char *relpath) {
	if (!g_owner) return NULL;
	return hmap_get(g_owner, relpath);
}

int db_entry_path(config *c, pkg *p, char *out, size_t n) {
	snprintf(out, n, "%s/local/%s-%s", c->dbpath, p->name, p->version);
	return 0;
}

static void fwrite_section(FILE *f, const char *key, const char *value) {
	fprintf(f, "%%%s%%\n%s\n\n", key, value);
}

static void fwrite_list(FILE *f, const char *key, strs *s) {
	if (s->n == 0) return;
	fprintf(f, "%%%s%%\n", key);
	int i;
	for (i = 0; i < s->n; i++) fprintf(f, "%s\n", s->v[i]);
	fprintf(f, "\n");
}

int db_write_local_pkg(config *c, pkg *p) {
	char dir[4096];
	db_entry_path(c, p, dir, sizeof dir);
	if (mkdir_p(dir, 0755) != 0) {
		error("could not create directory %s", dir);
		return -1;
	}
	char tmp[4600], final[4600];
	snprintf(tmp, sizeof tmp, "%s/desc.nya-tmp", dir);
	snprintf(final, sizeof final, "%s/desc", dir);
	FILE *f = fopen(tmp, "w");
	if (!f) return -1;
	char sizebuf[64];
	snprintf(sizebuf, sizeof sizebuf, "%lld", p->isize);
	fwrite_section(f, "NAME", p->name ? p->name : "");
	if (p->base && *p->base) fwrite_section(f, "BASE", p->base);
	fwrite_section(f, "VERSION", p->version ? p->version : "");
	if (p->desc && *p->desc) fwrite_section(f, "DESC", p->desc);
	if (p->url && *p->url) fwrite_section(f, "URL", p->url);
	if (p->arch && *p->arch) fwrite_section(f, "ARCH", p->arch);
	if (p->builddate && *p->builddate) fwrite_section(f, "BUILDDATE", p->builddate);
	char idate[64];
	snprintf(idate, sizeof idate, "%lld", (long long)time(NULL));
	fwrite_section(f, "INSTALLDATE", idate);
	if (p->packager && *p->packager) fwrite_section(f, "PACKAGER", p->packager);
	fwrite_section(f, "SIZE", sizebuf);
	char reason[8];
	snprintf(reason, sizeof reason, "%d", p->reason);
	fwrite_section(f, "REASON", reason);
	fwrite_section(f, "VALIDATION", p->validation && *p->validation ? p->validation : "none");
	fwrite_list(f, "LICENSE", &p->licenses);
	fwrite_list(f, "GROUPS", &p->groups);
	fwrite_list(f, "REPLACES", &p->replaces);
	fwrite_list(f, "CONFLICTS", &p->conflicts);
	fwrite_list(f, "PROVIDES", &p->provides);
	fwrite_list(f, "DEPENDS", &p->depends);
	fwrite_list(f, "OPTDEPENDS", &p->optdepends);
	fclose(f);
	rename(tmp, final);
	snprintf(tmp, sizeof tmp, "%s/files.nya-tmp", dir);
	snprintf(final, sizeof final, "%s/files", dir);
	f = fopen(tmp, "w");
	if (!f) return -1;
	fprintf(f, "%%FILES%%\n");
	int i;
	for (i = 0; i < p->files.n; i++) fprintf(f, "%s\n", p->files.v[i]);
	fprintf(f, "\n");
	if (p->backup.n) {
		fprintf(f, "%%BACKUP%%\n");
		for (i = 0; i < p->backup.n; i++) fprintf(f, "%s\t(null)\n", p->backup.v[i]);
		fprintf(f, "\n");
	}
	fclose(f);
	rename(tmp, final);
	if (p->mtree_data && p->mtree_len > 0) {
		snprintf(tmp, sizeof tmp, "%s/mtree.nya-tmp", dir);
		snprintf(final, sizeof final, "%s/mtree", dir);
		write_file(tmp, p->mtree_data, p->mtree_len, 0644);
		rename(tmp, final);
	}
	if (p->install_data && p->install_len > 0) {
		snprintf(tmp, sizeof tmp, "%s/install.nya-tmp", dir);
		snprintf(final, sizeof final, "%s/install", dir);
		write_file(tmp, p->install_data, p->install_len, 0644);
		rename(tmp, final);
	}
	return 0;
}

void db_remove_local(config *c, const char *name, const char *version) {
	char dir[4096];
	snprintf(dir, sizeof dir, "%s/local/%s-%s", c->dbpath, name, version);
	DIR *d = opendir(dir);
	if (d) {
		struct dirent *de;
		while ((de = readdir(d)) != NULL) {
			if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
			char p[4600];
			snprintf(p, sizeof p, "%s/%s", dir, de->d_name);
			unlink(p);
		}
		closedir(d);
		rmdir(dir);
	}
}

hmap *mtree_sha_map(const char *data, long len) {
	hmap *m = hmap_new(256);
	const char *p = data;
	const char *end = data + len;
	while (p < end) {
		const char *nl = memchr(p, '\n', end - p);
		size_t l = nl ? (size_t)(nl - p) : (size_t)(end - p);
		char *line = xstrndup(p, l);
		char *t = trim(line);
		if (*t && *t != '#' && !startswith(t, "/set") && !startswith(t, "/unset")) {
			char *sp = strchr(t, ' ');
			if (!sp) sp = strchr(t, '\t');
			if (sp) {
				*sp = '\0';
				char *path = trim(t);
				if (startswith(path, "./")) path += 2;
				const char *rest = sp + 1;
				const char *sha = NULL;
				const char *q = rest;
				while ((q = strstr(q, "sha256digest=")) != NULL) {
					q += strlen("sha256digest=");
					const char *e = q;
					while (*e && *e != ' ' && *e != '\t' && *e != '\n') e++;
					if (e - q == 64) {
						sha = q;
						break;
					}
				}
				if (sha) {
					char hex[65];
					memcpy(hex, sha, 64);
					hex[64] = '\0';
					if (!hmap_has(m, path)) hmap_put(m, path, xstrdup(hex));
				}
			}
		}
		free(line);
		p = nl ? nl + 1 : end;
	}
	return m;
}
