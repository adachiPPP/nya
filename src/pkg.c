#include "nya.h"

static int rpmvercmp(const char *a, const char *b) {
	char oldch1, oldch2;
	char *str1, *str2, *ptr1, *ptr2, *one, *two;
	int rc = 0, isnum;
	if (strcmp(a, b) == 0) return 0;
	str1 = xstrdup(a);
	str2 = xstrdup(b);
	one = str1;
	two = str2;
	while (*one || *two) {
		while (*one && !isalnum((unsigned char)*one)) one++;
		while (*two && !isalnum((unsigned char)*two)) two++;
		if (!(*one && *two)) break;
		if ((one - str1) != (two - str2)) break;
		ptr1 = one;
		ptr2 = two;
		if (isdigit((unsigned char)*ptr1)) {
			while (*ptr1 && isdigit((unsigned char)*ptr1)) ptr1++;
			while (*ptr2 && isdigit((unsigned char)*ptr2)) ptr2++;
			isnum = 1;
		} else {
			while (*ptr1 && isalpha((unsigned char)*ptr1)) ptr1++;
			while (*ptr2 && isalpha((unsigned char)*ptr2)) ptr2++;
			isnum = 0;
		}
		oldch1 = *ptr1;
		*ptr1 = '\0';
		oldch2 = *ptr2;
		*ptr2 = '\0';
		if (one == ptr1) {
			rc = strcmp(one, two);
			if (rc) break;
		}
		if (two == ptr2) {
			rc = isnum ? 1 : -1;
			break;
		}
		if (isnum) {
			while (*one == '0') one++;
			while (*two == '0') two++;
			if (strlen(one) > strlen(two)) {
				rc = 1;
				break;
			}
			if (strlen(one) < strlen(two)) {
				rc = -1;
				break;
			}
		}
		rc = strcmp(one, two);
		if (rc) break;
		*ptr1 = oldch1;
		one = ptr1;
		*ptr2 = oldch2;
		two = ptr2;
	}
	if (!rc) {
		if (*one && !*two) rc = 1;
		else if (!*one && *two) rc = -1;
	}
	free(str1);
	free(str2);
	return rc;
}

int vercmp(const char *a, const char *b) {
	char *ac = xstrdup(a ? a : "");
	char *bc = xstrdup(b ? b : "");
	char *dash_a = strrchr(ac, '-');
	char *dash_b = strrchr(bc, '-');
	char rel_a[64] = "", rel_b[64] = "";
	if (dash_a) {
		*dash_a = '\0';
		snprintf(rel_a, sizeof rel_a, "%s", dash_a + 1);
	}
	if (dash_b) {
		*dash_b = '\0';
		snprintf(rel_b, sizeof rel_b, "%s", dash_b + 1);
	}
	long long epoch_a = 0, epoch_b = 0;
	char *colon_a = strchr(ac, ':');
	char *colon_b = strchr(bc, ':');
	const char *av = ac, *bv = bc;
	if (colon_a) {
		*colon_a = '\0';
		epoch_a = atoll(ac);
		av = colon_a + 1;
	}
	if (colon_b) {
		*colon_b = '\0';
		epoch_b = atoll(bc);
		bv = colon_b + 1;
	}
	int rc;
	if (epoch_a != epoch_b) {
		rc = epoch_a < epoch_b ? -1 : 1;
	} else {
		rc = rpmvercmp(av, bv);
		if (rc == 0) rc = rpmvercmp(rel_a, rel_b);
	}
	free(ac);
	free(bc);
	return rc;
}

int depspec_parse(const char *s, depspec *d) {
	memset(d, 0, sizeof *d);
	const char *op = NULL;
	const char *p;
	for (p = s; *p; p++) {
		if (*p == '<' || *p == '>' || *p == '=') {
			op = p;
			break;
		}
	}
	if (op) {
		const char *e = op;
		if (e[1] == '=') e += 2;
		else e += 1;
		char *name = xstrndup(s, op - s);
		d->name = xstrdup(trim(name));
		free(name);
		d->mod = xstrndup(op, e - op);
		while (*e == ' ') e++;
		d->ver = xstrdup(e);
	} else {
		d->name = xstrdup(s);
	}
	return 0;
}

void depspec_free(depspec *d) {
	free(d->name);
	free(d->mod);
	free(d->ver);
	memset(d, 0, sizeof *d);
}

int depspec_matches(const depspec *dep, const char *pkgname, const char *pkgver) {
	if (!dep->name || !pkgname) return 0;
	if (strcmp(dep->name, pkgname) != 0) return 0;
	if (!dep->mod || !dep->mod[0]) return 1;
	if (!pkgver) return 0;
	int r = vercmp(pkgver, dep->ver);
	if (strcmp(dep->mod, ">=") == 0) return r >= 0;
	if (strcmp(dep->mod, "<=") == 0) return r <= 0;
	if (strcmp(dep->mod, ">") == 0) return r > 0;
	if (strcmp(dep->mod, "<") == 0) return r < 0;
	if (strcmp(dep->mod, "=") == 0) return r == 0;
	return 0;
}

static int prov_matches(const char *prov, const depspec *dep) {
	depspec pd;
	depspec_parse(prov, &pd);
	int ok = depspec_matches(dep, pd.name, pd.ver);
	depspec_free(&pd);
	return ok;
}

int pkg_matches_dep(pkg *p, const depspec *dep) {
	if (depspec_matches(dep, p->name, p->version)) return 1;
	int i;
	for (i = 0; i < p->provides.n; i++) {
		if (prov_matches(p->provides.v[i], dep)) return 1;
	}
	return 0;
}

int pkg_read_pkginfo(const char *data, long len, pkg *p) {
	(void)len;
	const char *p2 = data;
	while (*p2) {
		const char *nl = strchr(p2, '\n');
		size_t l = nl ? (size_t)(nl - p2) : strlen(p2);
		char *line = xstrndup(p2, l);
		char *eq = strchr(line, '=');
		if (eq) {
			*eq = '\0';
			char *key = trim(line);
			char *val = trim(eq + 1);
			if (strcmp(key, "pkgname") == 0) {
				free(p->name);
				p->name = xstrdup(val);
			} else if (strcmp(key, "pkgbase") == 0) {
				free(p->base);
				p->base = xstrdup(val);
			} else if (strcmp(key, "pkgver") == 0) {
				free(p->version);
				p->version = xstrdup(val);
			} else if (strcmp(key, "pkgdesc") == 0) {
				free(p->desc);
				p->desc = xstrdup(val);
			} else if (strcmp(key, "url") == 0) {
				free(p->url);
				p->url = xstrdup(val);
			} else if (strcmp(key, "builddate") == 0) {
				free(p->builddate);
				p->builddate = xstrdup(val);
				p->builddate_ts = atoll(val);
			} else if (strcmp(key, "packager") == 0) {
				free(p->packager);
				p->packager = xstrdup(val);
			} else if (strcmp(key, "size") == 0) {
				p->isize = atoll(val);
			} else if (strcmp(key, "arch") == 0) {
				free(p->arch);
				p->arch = xstrdup(val);
			} else if (strcmp(key, "license") == 0) {
				if (!strs_has(&p->licenses, val)) strs_add(&p->licenses, val);
			} else if (strcmp(key, "depend") == 0) {
				if (!strs_has(&p->depends, val)) strs_add(&p->depends, val);
			} else if (strcmp(key, "optdepend") == 0) {
				if (!strs_has(&p->optdepends, val)) strs_add(&p->optdepends, val);
			} else if (strcmp(key, "provides") == 0) {
				if (!strs_has(&p->provides, val)) strs_add(&p->provides, val);
			} else if (strcmp(key, "conflict") == 0) {
				if (!strs_has(&p->conflicts, val)) strs_add(&p->conflicts, val);
			} else if (strcmp(key, "replaces") == 0) {
				if (!strs_has(&p->replaces, val)) strs_add(&p->replaces, val);
			} else if (strcmp(key, "backup") == 0) {
				if (!strs_has(&p->backup, val)) strs_add(&p->backup, val);
			} else if (strcmp(key, "groups") == 0) {
				if (!strs_has(&p->groups, val)) strs_add(&p->groups, val);
			}
		}
		free(line);
		if (!nl) break;
		p2 = nl + 1;
	}
	if (!p->name || !p->version) {
		error("invalid package metadata (missing pkgname/pkgver)");
		return -1;
	}
	if (!p->base) p->base = xstrdup(p->name);
	return 0;
}

int pkg_scan_archive(config *c, const char *path, pkg *p) {
	(void)c;
	rd *r = rd_open_compressed(path);
	if (!r) {
		error("could not open package file %s", path);
		return -1;
	}
	tar_it t;
	tar_init(&t, r);
	tar_entry e;
	while (tar_next(&t, &e) > 0) {
		char clean[4096];
		if (tar_safe_path(e.name, clean, sizeof clean) != 0) {
			tar_skip(&t);
			continue;
		}
		int is_meta = strchr(clean, '/') == NULL && clean[0] == '.';
		long len = 0;
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
			len = off;
		} else {
			tar_skip(&t);
		}
		if (is_meta) {
			if (strcmp(clean, ".PKGINFO") == 0 && data) {
				if (pkg_read_pkginfo(data, len, p) != 0) {
					free(data);
					rd_close(r);
					return -1;
				}
			} else if (strcmp(clean, ".MTREE") == 0 && data) {
				free(p->mtree_data);
				p->mtree_data = data;
				p->mtree_len = len;
				p->has_mtree = 1;
				data = NULL;
			} else if (strcmp(clean, ".INSTALL") == 0 && data) {
				free(p->install_data);
				p->install_data = data;
				p->install_len = len;
				data = NULL;
			}
		} else if (e.type == '5') {
			size_t cl = strlen(clean);
			char *withslash = xmalloc(cl + 2);
			sprintf(withslash, "%s%s", clean, (cl > 0 && clean[cl - 1] == '/') ? "" : "/");
			if (!strs_has(&p->files, withslash)) strs_add_own(&p->files, withslash);
			p->nfiles++;
		} else if (e.type == '0' || e.type == '1' || e.type == '2' || e.type == '6') {
			if (!strs_has(&p->files, clean)) strs_add(&p->files, clean);
			p->nfiles++;
		}
		free(data);
	}
	rd_close(r);
	if (!p->name) {
		error("package %s has no valid .PKGINFO", path);
		return -1;
	}
	if (!p->filename) p->filename = xstrdup(path);
	return 0;
}
