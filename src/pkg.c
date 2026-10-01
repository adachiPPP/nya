#include "nya.h"
#include <zlib.h>

static char *maybe_inflate_mtree(const char *data, long len, long *outlen) {
	*outlen = len;
	if (len >= 2 && (unsigned char)data[0] == 0x1f && (unsigned char)data[1] == 0x8b) {
		z_stream zs;
		memset(&zs, 0, sizeof zs);
		if (inflateInit2(&zs, 15 + 32) != Z_OK) return NULL;
		size_t cap = (size_t)len * 8 + 65536;
		char *out = xmalloc(cap);
		size_t olen = 0;
		zs.next_in = (Bytef *)(size_t)data;
		zs.avail_in = (uInt)len;
		int ret = Z_OK;
		for (;;) {
			if (olen == cap) {
				cap *= 2;
				out = xrealloc(out, cap);
			}
			zs.next_out = (Bytef *)out + olen;
			zs.avail_out = (uInt)(cap - olen);
			ret = inflate(&zs, Z_NO_FLUSH);
			olen = cap - zs.avail_out;
			if (ret == Z_STREAM_END) break;
			if (ret != Z_OK) break;
			if (zs.avail_in == 0) break;
		}
		inflateEnd(&zs);
		if (ret != Z_STREAM_END) {
			free(out);
			return NULL;
		}
		out = xrealloc(out, olen + 1);
		out[olen] = '\0';
		*outlen = (long)olen;
		return out;
	}
	return NULL;
}

static void mtree_files_into_pkg(pkg *p) {
	const char *ptr = p->mtree_data;
	const char *end = p->mtree_data + p->mtree_len;
	while (ptr < end) {
		const char *nl = memchr(ptr, '\n', end - ptr);
		size_t l = nl ? (size_t)(nl - ptr) : (size_t)(end - ptr);
		char *line = xstrndup(ptr, l);
		char *t = trim(line);
		if (*t && *t != '#' && !startswith(t, "/set") && !startswith(t, "/unset")) {
			char *sp = strchr(t, ' ');
			if (!sp) sp = strchr(t, '\t');
			if (sp) {
				*sp = '\0';
				char *path = trim(t);
				if (startswith(path, "./")) path += 2;
				if (*path && !(strchr(path, '/') == NULL && path[0] == '.')) {
					if (strstr(sp + 1, "type=dir") != NULL) {
						size_t pl = strlen(path);
						if (pl == 0 || path[pl - 1] != '/') {
							char *ws = xmalloc(pl + 2);
							sprintf(ws, "%s/", path);
							if (!strs_has(&p->files, ws)) {
								strs_add_own(&p->files, ws);
								p->nfiles++;
							} else {
								free(ws);
							}
						} else if (!strs_has(&p->files, path)) {
							strs_add(&p->files, path);
							p->nfiles++;
						}
					} else if (!strs_has(&p->files, path)) {
						strs_add(&p->files, path);
						p->nfiles++;
					}
				}
			}
		}
		free(line);
		ptr = nl ? nl + 1 : end;
	}
}

static int seg_cmp(const char *a, size_t al, const char *b, size_t bl) {
	size_t m = al < bl ? al : bl;
	int rc = memcmp(a, b, m);
	if (rc) return rc < 0 ? -1 : 1;
	if (al == bl) return 0;
	return al < bl ? -1 : 1;
}

static int rpmvercmp(const char *a, size_t al, const char *b, size_t bl) {
	const char *ae = a + al, *be = b + bl;
	if (a == b) return 0;
	if (al == bl && memcmp(a, b, al) == 0) return 0;
	const char *one = a, *two = b;
	while (one < ae || two < be) {
		while (one < ae && !isalnum((unsigned char)*one)) one++;
		while (two < be && !isalnum((unsigned char)*two)) two++;
		if (!(one < ae && two < be)) break;
		if ((one - a) != (two - b)) return (one - a) < (two - b) ? -1 : 1;
		const char *ptr1 = one, *ptr2 = two;
		int isnum;
		if (isdigit((unsigned char)*ptr1)) {
			while (ptr1 < ae && isdigit((unsigned char)*ptr1)) ptr1++;
			while (ptr2 < be && isdigit((unsigned char)*ptr2)) ptr2++;
			isnum = 1;
		} else {
			while (ptr1 < ae && isalpha((unsigned char)*ptr1)) ptr1++;
			while (ptr2 < be && isalpha((unsigned char)*ptr2)) ptr2++;
			isnum = 0;
		}
		if (one == ptr1) {
			int rc = seg_cmp(one, ptr1 - one, two, ptr2 - two);
			if (rc) return rc;
		}
		if (two == ptr2) return isnum ? 1 : -1;
		if (isnum) {
			const char *z1 = one;
			const char *z2 = two;
			while (*z1 == '0') z1++;
			while (*z2 == '0') z2++;
			size_t l1 = (size_t)(ptr1 - z1), l2 = (size_t)(ptr2 - z2);
			if (l1 != l2) return l1 > l2 ? 1 : -1;
			int rc = seg_cmp(z1, l1, z2, l2);
			if (rc) return rc;
		} else {
			int rc = seg_cmp(one, ptr1 - one, two, ptr2 - two);
			if (rc) return rc;
		}
		one = ptr1;
		two = ptr2;
	}
	if (one >= ae && two < be) return -1;
	if (one < ae && two >= be) return 1;
	return 0;
}

static void ver_split(const char *v, const char **epoch, long long *ep, const char **main, size_t *ml, const char **rel, size_t *rl) {
	if (!v) v = "";
	const char *colon = strchr(v, ':');
	if (colon) {
		*epoch = v;
		*ep = atoll(v);
		v = colon + 1;
	} else {
		*epoch = NULL;
		*ep = 0;
	}
	const char *dash = strrchr(v, '-');
	if (dash) {
		*main = v;
		*ml = (size_t)(dash - v);
		*rel = dash + 1;
		*rl = strlen(dash + 1);
	} else {
		*main = v;
		*ml = strlen(v);
		*rel = "";
		*rl = 0;
	}
}

int vercmp(const char *a, const char *b) {
	const char *ea, *ma, *ra, *eb, *mb, *rb;
	long long epa, epb;
	size_t mal, ral, mbl, rbl;
	ver_split(a, &ea, &epa, &ma, &mal, &ra, &ral);
	ver_split(b, &eb, &epb, &mb, &mbl, &rb, &rbl);
	if (epa != epb) return epa < epb ? -1 : 1;
	int rc = rpmvercmp(ma, mal, mb, mbl);
	if (rc == 0) rc = rpmvercmp(ra, ral, rb, rbl);
	return rc;
}

static void spec_split(const char *s, const char **name, size_t *nlen, const char **mod, size_t *mlen, const char **ver) {
	const char *op = NULL;
	const char *p;
	for (p = s; *p; p++) {
		if (*p == '<' || *p == '>' || *p == '=') {
			op = p;
			break;
		}
	}
	*name = s;
	if (!op) {
		*nlen = strlen(s);
		*mod = NULL;
		*mlen = 0;
		*ver = NULL;
		return;
	}
	*nlen = (size_t)(op - s);
	while (*nlen > 0 && (s[*nlen - 1] == ' ' || s[*nlen - 1] == '\t')) (*nlen)--;
	*mod = op;
	if (op[1] == '=') *mlen = 2;
	else *mlen = 1;
	p = op + *mlen;
	while (*p == ' ') p++;
	*ver = p;
}

int depspec_parse(const char *s, depspec *d) {
	memset(d, 0, sizeof *d);
	const char *np, *mp, *vp;
	size_t nlen, mlen;
	spec_split(s, &np, &nlen, &mp, &mlen, &vp);
	char *name = xstrndup(np, nlen);
	d->name = xstrdup(trim(name));
	free(name);
	if (mp) {
		d->mod = xstrndup(mp, mlen);
		d->ver = xstrdup(vp);
	}
	return 0;
}

void depspec_free(depspec *d) {
	free(d->name);
	free(d->mod);
	free(d->ver);
	memset(d, 0, sizeof *d);
}

static int mod_ok(const char *m, size_t ml, int r) {
	if (ml == 2 && m[0] == '>' && m[1] == '=') return r >= 0;
	if (ml == 2 && m[0] == '<' && m[1] == '=') return r <= 0;
	if (ml == 1 && m[0] == '>') return r > 0;
	if (ml == 1 && m[0] == '<') return r < 0;
	if (ml == 1 && m[0] == '=') return r == 0;
	return 0;
}

int depspec_matches(const depspec *dep, const char *pkgname, const char *pkgver) {
	if (!dep->name || !pkgname) return 0;
	if (strcmp(dep->name, pkgname) != 0) return 0;
	if (!dep->mod || !dep->mod[0]) return 1;
	if (!pkgver) return 0;
	int r = vercmp(pkgver, dep->ver);
	return mod_ok(dep->mod, strlen(dep->mod), r);
}

static int spec_matches(const char *name, size_t nlen, const char *mod, size_t mlen,
                        const char *ver, const char *pkgname, size_t pnlen, const char *pkgver) {
	if (!pkgname || !name) return 0;
	if (pnlen != nlen) return 0;
	if (nlen > 0 && memcmp(name, pkgname, nlen) != 0) return 0;
	if (!mod || mlen == 0) return 1;
	if (!pkgver) return 0;
	int r = vercmp(pkgver, ver);
	return mod_ok(mod, mlen, r);
}

static int prov_matches(const char *prov, const depspec *dep) {
	const char *pn, *pm, *pv;
	size_t pnl, pml;
	spec_split(prov, &pn, &pnl, &pm, &pml, &pv);
	return spec_matches(dep->name, dep->name ? strlen(dep->name) : 0,
	                    dep->mod, dep->mod ? strlen(dep->mod) : 0,
	                    dep->ver, pn, pnl, pv);
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
	int have_mtree = 0;
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
			if (is_meta || !have_mtree) {
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
				continue;
			}
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
				long infl = 0;
				char *inflated = maybe_inflate_mtree(data, len, &infl);
				if (inflated) {
					free(data);
					data = inflated;
					len = infl;
				}
				free(p->mtree_data);
				p->mtree_data = data;
				p->mtree_len = len;
				p->has_mtree = 1;
				data = NULL;
				if (p->mtree_data && p->mtree_len > 0) {
					strs_free(&p->files);
					memset(&p->files, 0, sizeof p->files);
					mtree_files_into_pkg(p);
					have_mtree = 1;
				}
			} else if (strcmp(clean, ".INSTALL") == 0 && data) {
				free(p->install_data);
				p->install_data = data;
				p->install_len = len;
				data = NULL;
			}
		} else if (!have_mtree && e.type == '5') {
			size_t cl = strlen(clean);
			char *withslash = xmalloc(cl + 2);
			sprintf(withslash, "%s%s", clean, (cl > 0 && clean[cl - 1] == '/') ? "" : "/");
			if (!strs_has(&p->files, withslash)) strs_add_own(&p->files, withslash);
			p->nfiles++;
		} else if (!have_mtree && (e.type == '0' || e.type == '1' || e.type == '2' || e.type == '6')) {
			if (!strs_has(&p->files, clean)) strs_add(&p->files, clean);
			p->nfiles++;
		}
		free(data);
	}
	rd_close(r);
	if (p->name) {
		int i;
		for (i = 0; i < p->files.n; i++) {
			const char *f = p->files.v[i];
			size_t fl = strlen(f);
			if (fl == 0 || f[fl - 1] != '/') continue;
			if (!db_file_has_other_owner(f, p->name)) {
				strs_add(&p->owners, f);
				p->nowners++;
			}
		}
	}
	if (!p->name) {
		error("package %s has no valid .PKGINFO", path);
		return -1;
	}
	if (!p->filename) p->filename = xstrdup(path);
	return 0;
}
