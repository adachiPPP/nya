#include "nya.h"
#include <curl/curl.h>

static void set_proxy(CURL *h) {
	const char *p = getenv("https_proxy");
	if (!p || !*p) p = getenv("HTTPS_PROXY");
	if (!p || !*p) p = getenv("http_proxy");
	if (!p || !*p) p = getenv("HTTP_PROXY");
	if (!p || !*p) p = getenv("all_proxy");
	if (!p || !*p) p = getenv("ALL_PROXY");
	if (p && *p) curl_easy_setopt(h, CURLOPT_PROXY, p);
}

static CURL *easy_new(const char *url) {
	CURL *h = curl_easy_init();
	if (!h) return NULL;
	curl_easy_setopt(h, CURLOPT_URL, url);
	curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(h, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(h, CURLOPT_USERAGENT, "nya/" NYA_VERSION);
	curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 20L);
	curl_easy_setopt(h, CURLOPT_TIMEOUT, 900L);
	curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(h, CURLOPT_ACCEPT_ENCODING, "");
	set_proxy(h);
	return h;
}

typedef struct {
	char *data;
	long len;
	long cap;
} membuf;

static size_t mem_cb(void *ptr, size_t sz, size_t nm, void *ud) {
	membuf *m = ud;
	size_t n = sz * nm;
	if (m->len + n + 1 > m->cap) {
		while (m->len + n + 1 > m->cap) m->cap = m->cap ? m->cap * 2 : 65536;
		m->data = xrealloc(m->data, m->cap);
	}
	memcpy(m->data + m->len, ptr, n);
	m->len += n;
	m->data[m->len] = '\0';
	return n;
}

static int dl_url_tmo(config *c, const char *url, char **data, long *len, long tmo) {
	(void)c;
	CURL *h = easy_new(url);
	if (!h) return -1;
	if (tmo > 0) curl_easy_setopt(h, CURLOPT_TIMEOUT, tmo);
	membuf m;
	memset(&m, 0, sizeof m);
	curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, mem_cb);
	curl_easy_setopt(h, CURLOPT_WRITEDATA, &m);
	CURLcode rc = curl_easy_perform(h);
	curl_easy_cleanup(h);
	if (rc != CURLE_OK) {
		free(m.data);
		return -1;
	}
	*data = m.data ? m.data : xstrdup("");
	*len = m.len;
	return 0;
}

int dl_url(config *c, const char *url, char **data, long *len) {
	return dl_url_tmo(c, url, data, len, 0);
}

int dl_url_quick(config *c, const char *url, char **data, long *len, long tmo) {
	return dl_url_tmo(c, url, data, len, tmo);
}

int dl_url_file(config *c, const char *url, const char *dest) {
	(void)c;
	CURL *h = easy_new(url);
	if (!h) return -1;
	FILE *f = fopen(dest, "wb");
	if (!f) {
		curl_easy_cleanup(h);
		return -1;
	}
	curl_easy_setopt(h, CURLOPT_WRITEDATA, f);
	CURLcode rc = curl_easy_perform(h);
	fclose(f);
	curl_easy_cleanup(h);
	if (rc != CURLE_OK) {
		unlink(dest);
		return -1;
	}
	return 0;
}

typedef struct pj {
	dl *d;
	CURL *easy;
	FILE *fp;
	membuf mem;
	int urlidx;
	int err;
	long long dlnow;
	long long dltotal;
} pj;

static size_t pj_write(void *ptr, size_t sz, size_t nm, void *ud) {
	pj *j = ud;
	size_t n = sz * nm;
	if (j->d->dest) {
		return fwrite(ptr, sz, nm, j->fp);
	}
	membuf *m = &j->mem;
	if (m->len + n + 1 > m->cap) {
		while (m->len + n + 1 > m->cap) m->cap = m->cap ? m->cap * 2 : 65536;
		m->data = xrealloc(m->data, m->cap);
	}
	memcpy(m->data + m->len, ptr, n);
	m->len += n;
	m->data[m->len] = '\0';
	return n;
}

static pj *prog_active = NULL;

static int prog_cb(void *ud, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
	pj *j = ud;
	(void)ultotal;
	(void)ulnow;
	j->dlnow = dlnow;
	j->dltotal = dltotal;
	prog_active = j;
	return 0;
}

static const char *url_basename(const char *u) {
	const char *s = u ? strrchr(u, '/') : NULL;
	return s ? s + 1 : (u ? u : "");
}

static int draw_progress(pj *js, int n) {
	long long now = 0, all = 0;
	int i;
	for (i = 0; i < n; i++) {
		now += js[i].dlnow;
		all += js[i].dltotal > 0 ? js[i].dltotal : js[i].dlnow;
	}
	if (all <= 0) return 0;
	int pct = (int)(now * 100 / all);
	if (pct > 100) pct = 100;
	int barw = 22;
	int filled = barw * pct / 100;
	const char *label = "";
	if (prog_active && prog_active->d && prog_active->d->urls) {
		label = url_basename(prog_active->d->urls[prog_active->urlidx]);
	}
	fprintf(stderr, "\r\033[K  %-28.28s [", label);
	for (i = 0; i < barw; i++) fputc(i < filled ? '=' : (i == filled ? '>' : ' '), stderr);
	fprintf(stderr, "] %3d%%", pct);
	fflush(stderr);
	return 1;
}

static void clear_progress(void) {
	fprintf(stderr, "\r\033[K");
	fflush(stderr);
}

static void pj_setup(pj *j) {
	j->easy = easy_new(j->d->urls[j->urlidx]);
	curl_easy_setopt(j->easy, CURLOPT_WRITEFUNCTION, pj_write);
	curl_easy_setopt(j->easy, CURLOPT_WRITEDATA, j);
	curl_easy_setopt(j->easy, CURLOPT_PRIVATE, j);
	curl_easy_setopt(j->easy, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(j->easy, CURLOPT_XFERINFOFUNCTION, prog_cb);
	curl_easy_setopt(j->easy, CURLOPT_XFERINFODATA, j);
	j->dlnow = 0;
	j->dltotal = 0;
	if (j->d->dest) {
		j->fp = fopen(j->d->dest, "wb");
		if (!j->fp) {
			j->err = errno;
			curl_easy_setopt(j->easy, CURLOPT_WRITEFUNCTION, NULL);
		}
	}
}

static void pj_finish(pj *j) {
	if (j->easy) {
		curl_easy_cleanup(j->easy);
		j->easy = NULL;
	}
	if (j->fp) {
		fclose(j->fp);
		j->fp = NULL;
	}
}

int dl_parallel(config *c, dl *jobs, int n) {
	(void)c;
	if (n == 0) return 0;
	CURLM *m = curl_multi_init();
	if (!m) return -1;
	pj *js = xcalloc(n, sizeof *js);
	int i;
	int alive = 0;
	int tty = isatty(fileno(stderr));
	int drew = 0;
	for (i = 0; i < n; i++) {
		js[i].d = &jobs[i];
		js[i].urlidx = 0;
		if (jobs[i].nurls <= 0) {
			jobs[i].ok = 0;
			snprintf(jobs[i].err, sizeof jobs[i].err, "no servers configured");
			continue;
		}
		pj_setup(&js[i]);
		if (!js[i].easy || (jobs[i].dest && !js[i].fp)) {
			jobs[i].ok = 0;
			snprintf(jobs[i].err, sizeof jobs[i].err, "failed to initialize download: %s",
			         js[i].err ? strerror(js[i].err) : "unknown error");
			pj_finish(&js[i]);
			continue;
		}
		curl_multi_add_handle(m, js[i].easy);
		alive++;
	}
	while (alive > 0) {
		int still = 0;
		curl_multi_perform(m, &still);
		CURLMsg *msg;
		int msgs;
		while ((msg = curl_multi_info_read(m, &msgs)) != NULL) {
			if (msg->msg != CURLMSG_DONE) continue;
			pj *j = NULL;
			curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &j);
			if (!j) continue;
			curl_multi_remove_handle(m, msg->easy_handle);
			CURLcode rc = msg->data.result;
			long code = 0;
			curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &code);
			int ok = rc == CURLE_OK && (code == 0 || (code >= 200 && code < 300));
			if (ok) {
				j->d->ok = 1;
				if (j->d->dest) {
					fflush(j->fp);
				} else {
					j->d->data = j->mem.data ? j->mem.data : xstrdup("");
					j->d->datalen = j->mem.len;
					j->mem.data = NULL;
					j->mem.len = j->mem.cap = 0;
				}
				alive--;
				pj_finish(j);
			} else if (j->urlidx + 1 < j->d->nurls) {
				pj_finish(j);
				j->urlidx++;
				pj_setup(j);
				if (!j->easy || (j->d->dest && !j->fp)) {
					j->d->ok = 0;
					snprintf(j->d->err, sizeof j->d->err, "failed to initialize download: %s",
					         j->err ? strerror(j->err) : "unknown error");
					pj_finish(j);
					alive--;
				} else {
					curl_multi_add_handle(m, j->easy);
				}
			} else {
				j->d->ok = 0;
				snprintf(j->d->err, sizeof j->d->err, "download failed (curl %d, http %ld)", rc, code);
				alive--;
				pj_finish(j);
			}
		}
		if (alive <= 0) break;
		curl_multi_wait(m, NULL, 0, 100, NULL);
		if (tty && draw_progress(js, n)) drew = 1;
	}
	if (drew) clear_progress();
	curl_multi_cleanup(m);
	free(js);
	int allok = 1;
	for (i = 0; i < n; i++) {
		if (!jobs[i].ok) allok = 0;
	}
	return allok ? 0 : -1;
}

int dl_mirror(config *c, repo *r, const char *rel, const char *dest) {
	(void)c;
	if (r->servers.n == 0) {
		error("no servers configured for repository '%s'", r->name);
		return -1;
	}
	dl d;
	memset(&d, 0, sizeof d);
	d.dest = dest;
	d.urls = xcalloc(r->servers.n, sizeof(char *));
	int i;
	for (i = 0; i < r->servers.n; i++) {
		size_t sl = strlen(r->servers.v[i]);
		int need = sl > 0 && r->servers.v[i][sl - 1] != '/';
		char *u = xmalloc(sl + need + strlen(rel) + 1);
		sprintf(u, "%s%s%s", r->servers.v[i], need ? "/" : "", rel);
		d.urls[i] = u;
		d.nurls++;
	}
	int rc = dl_parallel(c, &d, 1);
	for (i = 0; i < d.nurls; i++) free(d.urls[i]);
	free(d.urls);
	if (rc != 0 && d.err[0]) error("%s", d.err);
	return rc;
}

int download_url(config *c, const char *url, const char *dest) {
	dl d;
	memset(&d, 0, sizeof d);
	d.urls = xcalloc(1, sizeof(char *));
	d.urls[0] = xstrdup(url);
	d.nurls = 1;
	d.dest = dest;
	int rc = dl_parallel(c, &d, 1);
	free(d.urls[0]);
	free(d.urls);
	if (rc != 0 && d.err[0]) error("%s", d.err);
	return rc;
}

int download_url_quiet(config *c, const char *url, const char *dest) {
	dl d;
	memset(&d, 0, sizeof d);
	d.urls = xcalloc(1, sizeof(char *));
	d.urls[0] = xstrdup(url);
	d.nurls = 1;
	d.dest = dest;
	int rc = dl_parallel(c, &d, 1);
	free(d.urls[0]);
	free(d.urls);
	return rc;
}

int cache_find(config *c, const char *filename, char *out, size_t n) {
	int i;
	for (i = 0; i < c->cachedirs.n; i++) {
		snprintf(out, n, "%s/%s", c->cachedirs.v[i], filename);
		if (is_file(out)) return 0;
	}
	return -1;
}

int pkg_verify_file(config *c, pkg *p, const char *path) {
	(void)c;
	if (p->sha256sum && *p->sha256sum) {
		char hex[65];
		if (sha256_file(path, hex) == 0 && strcasecmp(hex, p->sha256sum) == 0) return 0;
		warn("checksum mismatch for %s", p->filename ? p->filename : p->name);
		return -1;
	}
	return 0;
}

int download_pkg(config *c, pkg *p, char *outpath, size_t outlen) {
	char cached[4096];
	if (p->filename && cache_find(c, p->filename, cached, sizeof cached) == 0) {
		if (pkg_verify_file(c, p, cached) == 0) {
			snprintf(outpath, outlen, "%s", cached);
			return 0;
		}
	}
	repo *r = config_find_repo(c, p->repo);
	if (!r) {
		error("repository '%s' not configured", p->repo ? p->repo : "?");
		return -1;
	}
	if (r->servers.n == 0) {
		error("no servers configured for repository '%s'", r->name);
		return -1;
	}
	char tmp[4200];
	if (c->cachedirs.n == 0) {
		error("no cache directories configured");
		return -1;
	}
	mkdir_p(c->cachedirs.v[0], 0755);
	snprintf(tmp, sizeof tmp, "%s/.nya-%s-%ld", c->cachedirs.v[0], p->filename ? p->filename : "pkg", (long)getpid());
	dl d;
	memset(&d, 0, sizeof d);
	d.dest = tmp;
	d.urls = xcalloc(r->servers.n, sizeof(char *));
	int i;
	for (i = 0; i < r->servers.n; i++) {
		size_t sl = strlen(r->servers.v[i]);
		int need = sl > 0 && r->servers.v[i][sl - 1] != '/';
		char *u = xmalloc(sl + need + strlen(p->filename) + 1);
		sprintf(u, "%s%s%s", r->servers.v[i], need ? "/" : "", p->filename);
		d.urls[i] = u;
		d.nurls++;
	}
	if (g_verbose) msg("downloading %s", p->filename);
	int rc = dl_parallel(c, &d, 1);
	for (i = 0; i < d.nurls; i++) free(d.urls[i]);
	free(d.urls);
	if (rc != 0) {
		unlink(tmp);
		if (d.err[0]) error("%s", d.err);
		error("failed to retrieve some files");
		return -1;
	}
	if (pkg_verify_file(c, p, tmp) != 0) {
		unlink(tmp);
		error("failed to retrieve some files");
		return -1;
	}
	char final[4200];
	snprintf(final, sizeof final, "%s/%s", c->cachedirs.v[0], p->filename);
	rename(tmp, final);
	snprintf(outpath, outlen, "%s", final);
	return 0;
}
