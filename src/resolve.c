#include "nya.h"

void txn_init(txn *t, config *c) {
	memset(t, 0, sizeof *t);
	t->c = c;
}

void txn_free(txn *t) {
	free(t->add);
	free(t->rm);
	memset(t, 0, sizeof *t);
}

void txn_add_add(txn *t, pkg *p) {
	int i;
	for (i = 0; i < t->nadd; i++) {
		if (t->add[i] == p) return;
	}
	t->add = xrealloc(t->add, (t->nadd + 1) * sizeof(pkg *));
	t->add[t->nadd++] = p;
}

void txn_add_rm(txn *t, pkg *p) {
	int i;
	for (i = 0; i < t->nrm; i++) {
		if (t->rm[i] == p) return;
	}
	t->rm = xrealloc(t->rm, (t->nrm + 1) * sizeof(pkg *));
	t->rm[t->nrm++] = p;
}

static int in_rm(txn *t, const char *name) {
	int i;
	for (i = 0; i < t->nrm; i++) {
		if (t->rm[i]->name && strcmp(t->rm[i]->name, name) == 0) return 1;
	}
	return 0;
}

static int in_add(txn *t, const char *name) {
	int i;
	for (i = 0; i < t->nadd; i++) {
		if (t->add[i]->name && strcmp(t->add[i]->name, name) == 0) return 1;
	}
	return 0;
}

static int installed_matches(txn *t, const depspec *dep) {
	int i;
	for (i = 0; i < g_nlocal; i++) {
		pkg *l = g_local[i];
		if (in_rm(t, l->name)) continue;
		if (pkg_matches_dep(l, dep)) return 1;
	}
	return 0;
}

static int add_matches(txn *t, const depspec *dep) {
	int i;
	for (i = 0; i < t->nadd; i++) {
		if (pkg_matches_dep(t->add[i], dep)) return 1;
	}
	return 0;
}

static int rm_matches(txn *t, const depspec *dep) {
	int i;
	for (i = 0; i < t->nrm; i++) {
		if (pkg_matches_dep(t->rm[i], dep)) return 1;
	}
	return 0;
}

static pkg *sync_provider(const depspec *dep) {
	int i;
	for (i = 0; i < g_nsync; i++) {
		if (g_sync[i]->name && strcmp(g_sync[i]->name, dep->name) == 0) return g_sync[i];
	}
	for (i = 0; i < g_nsync; i++) {
		if (pkg_matches_dep(g_sync[i], dep)) return g_sync[i];
	}
	return NULL;
}

static pkg *sync_provider_versioned(const char *name, const depspec *dep) {
	int i;
	for (i = 0; i < g_nsync; i++) {
		pkg *p = g_sync[i];
		if (p->name && strcmp(p->name, name) == 0) {
			if (depspec_matches(dep, p->name, p->version)) return p;
		}
	}
	return NULL;
}

static int is_ignored(config *c, pkg *p) {
	int i;
	for (i = 0; i < c->ignorepkg.n; i++) {
		if (fnmatch(c->ignorepkg.v[i], p->name, 0) == 0) return 1;
	}
	for (i = 0; i < c->ignoregrp.n; i++) {
		int j;
		for (j = 0; j < p->groups.n; j++) {
			if (fnmatch(c->ignoregrp.v[i], p->groups.v[j], 0) == 0) return 1;
		}
	}
	return 0;
}

static int is_holdpkg(config *c, const char *name) {
	int i;
	for (i = 0; i < c->holdpkg.n; i++) {
		if (strcmp(c->holdpkg.v[i], name) == 0) return 1;
	}
	return 0;
}

static int looks_like_file(const char *s) {
	if (s[0] == '/' || startswith(s, "./") || startswith(s, "file://") ||
	    startswith(s, "http://") || startswith(s, "https://"))
		return 1;
	if (endswith(s, ".pkg.tar.zst") || endswith(s, ".pkg.tar.xz") ||
	    endswith(s, ".pkg.tar.gz") || endswith(s, ".pkg.tar"))
		return 1;
	return 0;
}

static int group_members(config *c, const char *group, strs *out) {
	(void)c;
	int i;
	for (i = 0; i < g_nsync; i++) {
		int j;
		for (j = 0; j < g_sync[i]->groups.n; j++) {
			if (strcmp(g_sync[i]->groups.v[j], group) == 0) {
				if (!strs_has(out, g_sync[i]->name)) strs_add(out, g_sync[i]->name);
			}
		}
	}
	return out->n;
}

static int local_group_members(const char *group, strs *out) {
	int i;
	for (i = 0; i < g_nlocal; i++) {
		int j;
		for (j = 0; j < g_local[i]->groups.n; j++) {
			if (strcmp(g_local[i]->groups.v[j], group) == 0) {
				if (!strs_has(out, g_local[i]->name)) strs_add(out, g_local[i]->name);
			}
		}
	}
	return out->n;
}

int txn_build_install(config *c, const char **targets, int ntargets, txn *t, strs *notfound) {
	int ti;
	for (ti = 0; ti < ntargets; ti++) {
		const char *target = targets[ti];
		if (looks_like_file(target)) {
			char path[4096];
			if (startswith(target, "file://")) snprintf(path, sizeof path, "%s", target + 7);
			else snprintf(path, sizeof path, "%s", target);
			pkg *p = pkg_new("local");
			p->filename = xstrdup(path);
			if (pkg_scan_archive(c, path, p) != 0) {
				pkg_free(p);
				return -1;
			}
			if (p->csize == 0) p->csize = 0;
			txn_add_add(t, p);
			continue;
		}
		const char *repofilter = NULL;
		char repobuf[256];
		const char *name = target;
		if (target[0] != '/' && strchr(target, '/')) {
			const char *slash = strchr(target, '/');
			size_t rl = slash - target;
			if (rl >= sizeof repobuf) rl = sizeof repobuf - 1;
			memcpy(repobuf, target, rl);
			repobuf[rl] = '\0';
			repofilter = repobuf;
			name = slash + 1;
		}
		depspec dep;
		depspec_parse(name, &dep);
		pkg *p = NULL;
		if (dep.mod) {
			p = sync_provider_versioned(dep.name, &dep);
			if (!p) {
				error("target not found: %s", target);
				depspec_free(&dep);
				return -1;
			}
		} else {
			p = db_find_sync_exact(repofilter, dep.name);
			if (!p) {
				strs members;
				memset(&members, 0, sizeof members);
				if (group_members(c, dep.name, &members) > 0) {
					int m;
					for (m = 0; m < members.n; m++) {
						pkg *gp = db_find_sync(members.v[m]);
						if (gp) txn_add_add(t, gp);
					}
					strs_free(&members);
					depspec_free(&dep);
					continue;
				}
				if (c->aur) {
					if (aur_build_install(c, dep.name, t) == 0) {
						depspec_free(&dep);
						continue;
					}
				}
				if (notfound) {
					strs_add(notfound, dep.name);
					depspec_free(&dep);
					continue;
				}
				error("target not found: %s", target);
				depspec_free(&dep);
				return -1;
			}
			if (repofilter && strcmp(p->repo, repofilter) != 0) {
				error("target not found: %s", target);
				depspec_free(&dep);
				return -1;
			}
		}
		depspec_free(&dep);
		txn_add_add(t, p);
	}
	int idx = 0;
	while (idx < t->nadd) {
		pkg *p = t->add[idx++];
		int di;
		for (di = 0; di < p->depends.n; di++) {
			depspec dep;
			depspec_parse(p->depends.v[di], &dep);
			if (dep.mod && strchr(dep.name, ':')) {
				char *colon = strchr(dep.name, ':');
				*colon = '\0';
			}
			if (installed_matches(t, &dep)) {
				depspec_free(&dep);
				continue;
			}
			if (add_matches(t, &dep)) {
				depspec_free(&dep);
				continue;
			}
			pkg *prov = sync_provider(&dep);
			if (!prov) {
				error("unable to satisfy dependency '%s' required by %s", p->depends.v[di], p->name);
				depspec_free(&dep);
				return -1;
			}
			if (!in_add(t, prov->name)) {
				prov->is_dep = 1;
				prov->reason = 1;
				txn_add_add(t, prov);
			}
			depspec_free(&dep);
		}
	}
	for (idx = 0; idx < t->nadd; idx++) {
		pkg *p = t->add[idx];
		pkg *l = db_find_local(p->name);
		if (l) {
			if (vercmp(l->version, p->version) >= 0) {
				if (p->is_dep) {
					pkg *tmp = t->add[idx];
					t->add[idx] = t->add[t->nadd - 1];
					t->add[t->nadd - 1] = tmp;
					t->nadd--;
					idx--;
					continue;
				}
				/* explicitly requested: reinstall the same version */
				p->is_upgrade = 1;
				p->is_reinstall = 1;
				p->reason = l->reason;
				continue;
			}
			p->is_upgrade = 1;
			p->reason = l->reason;
		}
	}
	return 0;
}

static int replaced_by_add(txn *t, pkg *l) {
	int i;
	for (i = 0; i < t->nadd; i++) {
		pkg *a = t->add[i];
		int j;
		for (j = 0; j < a->replaces.n; j++) {
			depspec rd;
			depspec_parse(a->replaces.v[j], &rd);
			int m = pkg_matches_dep(l, &rd);
			depspec_free(&rd);
			if (m) return 1;
		}
	}
	return 0;
}

static int txn_check_conflicts(config *c, txn *t) {
	(void)c;
	int i, j;
	for (i = 0; i < t->nadd; i++) {
		pkg *p = t->add[i];
		for (j = 0; j < p->conflicts.n; j++) {
			depspec cd;
			depspec_parse(p->conflicts.v[j], &cd);
			int k;
			for (k = 0; k < g_nlocal; k++) {
				pkg *l = g_local[k];
				if (strcmp(l->name, p->name) == 0) continue;
				if (in_rm(t, l->name)) continue;
				if (!pkg_matches_dep(l, &cd)) continue;
				if (replaced_by_add(t, l)) continue;
				error("failed to prepare transaction (conflicting packages)");
				error("%s conflicts with %s", l->name, p->name);
				depspec_free(&cd);
				return -1;
			}
			depspec_free(&cd);
		}
		for (j = 0; j < p->replaces.n; j++) {
			depspec rd;
			depspec_parse(p->replaces.v[j], &rd);
			int k;
			for (k = 0; k < g_nlocal; k++) {
				pkg *l = g_local[k];
				if (strcmp(l->name, p->name) == 0) continue;
				if (in_rm(t, l->name)) continue;
				if (pkg_matches_dep(l, &rd)) {
					txn_add_rm(t, l);
					msg("%sreplacing %s with %s%s", col_green(), l->name, p->name, col_reset());
				}
			}
			depspec_free(&rd);
		}
	}
	for (i = 0; i < g_nlocal; i++) {
		pkg *l = g_local[i];
		if (in_rm(t, l->name)) continue;
		if (in_add(t, l->name)) continue;
		int j;
		for (j = 0; j < l->depends.n; j++) {
			depspec dep;
			depspec_parse(l->depends.v[j], &dep);
			if (dep.mod && strchr(dep.name, ':')) {
				char *colon = strchr(dep.name, ':');
				*colon = '\0';
			}
			if (rm_matches(t, &dep) && !add_matches(t, &dep)) {
				error("failed to prepare transaction (could not satisfy dependencies)");
				error("removing %s breaks dependency '%s' required by %s", t->rm[0]->name, l->depends.v[j], l->name);
				depspec_free(&dep);
				return -1;
			}
			depspec_free(&dep);
		}
	}
	return 0;
}

int txn_scan_archives(config *c, txn *t) {
	int i;
	for (i = 0; i < t->nadd; i++) {
		pkg *p = t->add[i];
		if (p->files.n > 0 && p->mtree_data) continue;
		if (!p->filename) {
			error("no package file for %s", p->name);
			return -1;
		}
		if (pkg_scan_archive(c, p->filename, p) != 0) return -1;
	}
	return 0;
}

int txn_file_conflicts(config *c, txn *t) {
	int i, j;
	for (i = 0; i < t->nadd; i++) {
		pkg *p = t->add[i];
		for (j = 0; j < p->files.n; j++) {
			const char *f = p->files.v[j];
			size_t fl = strlen(f);
			if (fl > 0 && f[fl - 1] == '/') continue;
			const char *owner = db_owner(f);
			if (owner && strcmp(owner, p->name) != 0 && !in_rm(t, owner) && !in_add(t, owner)) {
				if (g_overwrite) continue;
				error("failed to prepare transaction (conflicting files)");
				error("%s: %s exists in filesystem (owned by %s)", p->name, f, owner);
				return -1;
			}
			char rooted[4096];
			snprintf(rooted, sizeof rooted, "%s/%s", c->rootdir, f);
			if (lstat(rooted, &(struct stat){0}) == 0) {
				if (owner && strcmp(owner, p->name) == 0) continue;
				if (owner && in_rm(t, owner)) continue;
				if (!owner) {
					if (g_overwrite) continue;
					error("failed to prepare transaction (conflicting files)");
					error("%s: %s exists in filesystem", p->name, f);
					return -1;
				}
			}
		}
	}
	return 0;
}

int txn_prepare(config *c, txn *t) {
	(void)c;
	int idx = 0;
	while (idx < t->nadd) {
		pkg *p = t->add[idx++];
		int di;
		for (di = 0; di < p->depends.n; di++) {
			depspec dep;
			depspec_parse(p->depends.v[di], &dep);
			if (dep.mod && strchr(dep.name, ':')) {
				char *colon = strchr(dep.name, ':');
				*colon = '\0';
			}
			if (installed_matches(t, &dep)) {
				depspec_free(&dep);
				continue;
			}
			if (add_matches(t, &dep)) {
				depspec_free(&dep);
				continue;
			}
			pkg *prov = sync_provider(&dep);
			if (!prov) {
				error("unable to satisfy dependency '%s' required by %s", p->depends.v[di], p->name);
				depspec_free(&dep);
				return -1;
			}
			if (!in_add(t, prov->name)) {
				prov->is_dep = 1;
				prov->reason = 1;
				txn_add_add(t, prov);
			}
			depspec_free(&dep);
		}
	}
	return txn_check_conflicts(c, t);
}

int txn_build_upgrade(config *c, txn *t) {
	int i;
	for (i = 0; i < g_nlocal; i++) {
		pkg *l = g_local[i];
		if (!l->name) continue;
		pkg *s = db_find_sync(l->name);
		if (!s) continue;
		if (vercmp(s->version, l->version) <= 0) continue;
		if (is_ignored(c, s)) {
			warn("%s: ignoring package upgrade (%s => %s)", s->name, l->version, s->version);
			continue;
		}
		if (is_holdpkg(c, s->name)) {
			if (!yesno("%s-%s: upgrade? ", s->name, s->version)) {
				warn("%s: ignoring package upgrade (holdpkg)", s->name);
				continue;
			}
		}
		s->is_upgrade = 1;
		s->reason = l->reason;
		txn_add_add(t, s);
	}
	if (t->nadd == 0) return 0;
	return txn_prepare(c, t);
}

static int required_by_any(config *c, txn *t, pkg *target) {
	(void)c;
	int i, j;
	for (i = 0; i < g_nlocal; i++) {
		pkg *l = g_local[i];
		if (l == target) continue;
		if (in_rm(t, l->name)) continue;
		for (j = 0; j < l->depends.n; j++) {
			depspec dep;
			depspec_parse(l->depends.v[j], &dep);
			if (dep.mod && strchr(dep.name, ':')) {
				char *colon = strchr(dep.name, ':');
				*colon = '\0';
			}
			int m = pkg_matches_dep(target, &dep);
			depspec_free(&dep);
			if (m) return 1;
		}
	}
	return 0;
}

static int depends_on_rm(txn *t, pkg *l) {
	int i, j;
	for (i = 0; i < t->nrm; i++) {
		for (j = 0; j < l->depends.n; j++) {
			depspec dep;
			depspec_parse(l->depends.v[j], &dep);
			if (dep.mod && strchr(dep.name, ':')) {
				char *colon = strchr(dep.name, ':');
				*colon = '\0';
			}
			int m = pkg_matches_dep(t->rm[i], &dep);
			depspec_free(&dep);
			if (m) return 1;
		}
	}
	return 0;
}

int txn_build_remove(config *c, const char **targets, int ntargets, int recursive, int nosave, int cascade, int unneeded, txn *t, strs *notfound) {
	int i;
	for (i = 0; i < ntargets; i++) {
		const char *target = targets[i];
		pkg *l = db_find_local(target);
		if (!l) {
			strs members;
			memset(&members, 0, sizeof members);
			if (local_group_members(target, &members) > 0) {
				int m;
				for (m = 0; m < members.n; m++) {
					pkg *gp = db_find_local(members.v[m]);
					if (gp) txn_add_rm(t, gp);
				}
				strs_free(&members);
				continue;
			}				if (notfound) {
					strs_add(notfound, target);
					continue;
				}
				error("target not found: %s", target);
				return -1;
			}
			if (unneeded && required_by_any(c, t, l)) {
			warn("%s: skipping, required by other packages", l->name);
			continue;
		}
		txn_add_rm(t, l);
	}
	if (recursive) {
		int changed = 1;
		while (changed) {
			changed = 0;
			for (i = 0; i < g_nlocal; i++) {
				pkg *l = g_local[i];
				if (in_rm(t, l->name)) continue;
				if (l->reason != 1) continue;
				if (!required_by_any(c, t, l)) {
					txn_add_rm(t, l);
					changed = 1;
				}
			}
		}
	}
	if (cascade) {
		int changed = 1;
		while (changed) {
			changed = 0;
			for (i = 0; i < g_nlocal; i++) {
				pkg *l = g_local[i];
				if (in_rm(t, l->name)) continue;
				if (depends_on_rm(t, l)) {
					txn_add_rm(t, l);
					changed = 1;
				}
			}
		}
	}
	for (i = 0; i < g_nlocal; i++) {
		pkg *l = g_local[i];
		if (in_rm(t, l->name)) continue;
		if (depends_on_rm(t, l)) {
			error("failed to prepare transaction (could not satisfy dependencies)");
			error("removing %s breaks dependency required by %s", t->rm[0]->name, l->name);
			return -1;
		}
	}
	t->recursive = recursive;
	t->nosave = nosave;
	t->cascade = cascade;
	t->unneeded = unneeded;
	return 0;
}
