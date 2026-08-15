#include "nya.h"

static long long tar_num(const char *p, size_t len) {
	if ((unsigned char)p[0] & 0x80) {
		unsigned long long v = 0;
		size_t i;
		for (i = 1; i < len; i++) v = (v << 8) | (unsigned char)p[i];
		return (long long)v;
	}
	size_t i = 0;
	while (i < len && (p[i] == ' ' || p[i] == '\0')) i++;
	long long v = 0;
	while (i < len && p[i] >= '0' && p[i] <= '7') {
		v = v * 8 + (p[i] - '0');
		i++;
	}
	return v;
}

void tar_init(tar_it *t, rd *r) {
	memset(t, 0, sizeof *t);
	t->r = r;
}

static int read_exact(rd *r, void *buf, size_t n) {
	size_t off = 0;
	while (off < n) {
		long got = rd_read(r, (char *)buf + off, n - off);
		if (got <= 0) return -1;
		off += got;
	}
	return 0;
}

static void pax_decode(char *s) {
	char *src = s, *dst = s;
	while (*src) {
		if (*src == '%' && src[1] && src[2]) {
			int hi = src[1], lo = src[2];
			int v = 0;
			if (hi >= '0' && hi <= '9') v = hi - '0';
			else if (hi >= 'a' && hi <= 'f') v = hi - 'a' + 10;
			else if (hi >= 'A' && hi <= 'F') v = hi - 'A' + 10;
			else {
				*dst++ = *src++;
				continue;
			}
			v <<= 4;
			int lv = 0;
			if (lo >= '0' && lo <= '9') lv = lo - '0';
			else if (lo >= 'a' && lo <= 'f') lv = lo - 'a' + 10;
			else if (lo >= 'A' && lo <= 'F') lv = lo - 'A' + 10;
			else {
				*dst++ = *src++;
				continue;
			}
			*dst++ = (char)(v | lv);
			src += 3;
		} else {
			*dst++ = *src++;
		}
	}
	*dst = '\0';
}

static void pax_apply(tar_it *t, const char *data, long len) {
	const char *p = data;
	while (p < data + len) {
		while (p < data + len && (*p == '\n')) p++;
		if (p >= data + len) break;
		long long reclen = 0;
		while (p < data + len && *p >= '0' && *p <= '9') {
			reclen = reclen * 10 + (*p - '0');
			p++;
		}
		if (reclen <= 0) break;
		const char *rec = p;
		if (p + reclen > data + len) break;
		const char *eq = memchr(rec, '=', reclen);
		if (eq) {
			size_t klen = eq - rec;
			char key[64];
			if (klen < sizeof key) {
				memcpy(key, rec, klen);
				key[klen] = '\0';
				size_t vlen = reclen - (eq - rec) - 1;
				if (vlen >= sizeof t->pax_path) vlen = sizeof t->pax_path - 1;
				char val[4096];
				memcpy(val, eq + 1, vlen);
				val[vlen] = '\0';
				pax_decode(val);
				if (strcmp(key, "path") == 0) {
					snprintf(t->pax_path, sizeof t->pax_path, "%s", val);
				} else if (strcmp(key, "linkpath") == 0) {
					snprintf(t->pax_link, sizeof t->pax_link, "%s", val);
				}
			}
		}
		p += reclen;
	}
}

int tar_safe_path(const char *name, char *out, size_t n) {
	const char *p = name;
	while (*p == '/' || (p[0] == '.' && p[1] == '/')) p++;
	if (*p == '\0') {
		if (n < 2) return -1;
		out[0] = '.';
		out[1] = '\0';
		return 0;
	}
	char tmp[4096];
	size_t l = 0;
	while (*p && l + 1 < sizeof tmp) {
		if (*p == '/') {
			while (*p == '/') p++;
			if (*p == '\0') break;
			if (p[0] == '.' && p[1] == '/') {
				p += 2;
				continue;
			}
			if (p[0] == '.' && p[1] == '\0') {
				p += 2;
				continue;
			}
			if (p[0] == '.' && p[1] == '.' && (p[2] == '/' || p[2] == '\0')) {
				return -1;
			}
			tmp[l++] = '/';
			continue;
		}
		tmp[l++] = *p++;
	}
	tmp[l] = '\0';
	if (l == 0 || l >= n) return -1;
	memcpy(out, tmp, l + 1);
	return 0;
}

static int skip_bytes(rd *r, long long n) {
	char tmp[4096];
	while (n > 0) {
		long take = n > (long long)sizeof tmp ? (long long)sizeof tmp : n;
		long got = rd_read(r, tmp, take);
		if (got <= 0) return -1;
		n -= got;
	}
	return 0;
}

static int skip_pad(tar_it *t) {
	long pad = (512 - (t->last_size % 512)) % 512;
	t->need_pad = 0;
	return pad > 0 ? skip_bytes(t->r, pad) : 0;
}

int tar_next(tar_it *t, tar_entry *e) {
	unsigned char hdr[512];
	if (t->remain > 0) {
		long long skip = t->remain + ((512 - (t->remain % 512)) % 512);
		if (skip_bytes(t->r, skip) != 0) return -1;
		t->remain = 0;
		t->need_pad = 0;
	}
	if (t->need_pad) {
		if (skip_pad(t) != 0) return -1;
	}
	for (;;) {
		if (read_exact(t->r, hdr, 512) != 0) return 0;
		int allzero = 1;
		int i;
		for (i = 0; i < 512; i++) {
			if (hdr[i]) {
				allzero = 0;
				break;
			}
		}
		if (allzero) {
			read_exact(t->r, hdr, 512);
			return 0;
		}
		char name[256];
		char prefix[256];
		memcpy(name, hdr, 100);
		name[100] = '\0';
		memcpy(prefix, hdr + 345, 155);
		prefix[155] = '\0';
		char linkname[256];
		memcpy(linkname, hdr + 157, 100);
		linkname[100] = '\0';
		int type = hdr[156];
		if (type == 0) type = '0';
		long long size = tar_num((char *)hdr + 124, 12);
		memset(e, 0, sizeof *e);
		e->size = size;
		e->mode = tar_num((char *)hdr + 100, 8);
		e->uid = tar_num((char *)hdr + 108, 8);
		e->gid = tar_num((char *)hdr + 116, 8);
		e->mtime = tar_num((char *)hdr + 136, 12);
		e->type = type;
		t->remain = size;
		t->last_size = size;
		t->need_pad = 1;
		if (type == 'L') {
			char data[4096];
			long n = size < (long long)sizeof data ? size : (long long)sizeof data - 1;
			if (read_exact(t->r, data, n) != 0) return -1;
			data[n] = '\0';
			snprintf(t->longname, sizeof t->longname, "%s", data);
			t->remain -= n;
			tar_skip(t);
			skip_pad(t);
			continue;
		}
		if (type == 'K') {
			char data[4096];
			long n = size < (long long)sizeof data ? size : (long long)sizeof data - 1;
			if (read_exact(t->r, data, n) != 0) return -1;
			data[n] = '\0';
			snprintf(t->longlink, sizeof t->longlink, "%s", data);
			t->remain -= n;
			tar_skip(t);
			skip_pad(t);
			continue;
		}
		if (type == 'x' || type == 'g') {
			char *data = xmalloc(size + 1);
			if (read_exact(t->r, data, size) != 0) {
				free(data);
				return -1;
			}
			data[size] = '\0';
			pax_apply(t, data, size);
			free(data);
			t->remain -= size;
			tar_skip(t);
			skip_pad(t);
			continue;
		}
		if (t->longname[0]) {
			snprintf(e->name, sizeof e->name, "%s", t->longname);
			t->longname[0] = '\0';
		} else if (t->pax_path[0]) {
			snprintf(e->name, sizeof e->name, "%s", t->pax_path);
			t->pax_path[0] = '\0';
		} else if (prefix[0]) {
			snprintf(e->name, sizeof e->name, "%s/%s", prefix, name);
		} else {
			snprintf(e->name, sizeof e->name, "%s", name);
		}
		if (t->longlink[0]) {
			snprintf(e->linkname, sizeof e->linkname, "%s", t->longlink);
			t->longlink[0] = '\0';
		} else if (t->pax_link[0]) {
			snprintf(e->linkname, sizeof e->linkname, "%s", t->pax_link);
			t->pax_link[0] = '\0';
		} else {
			snprintf(e->linkname, sizeof e->linkname, "%s", linkname);
		}
		return 1;
	}
}

long tar_read(tar_it *t, void *buf, size_t n) {
	if (n > t->remain) n = t->remain;
	if (n == 0) return 0;
	long got = rd_read(t->r, buf, n);
	if (got > 0) t->remain -= got;
	return got;
}

void tar_skip(tar_it *t) {
	char tmp[4096];
	while (t->remain > 0) {
		long n = t->remain > (long long)sizeof tmp ? (long long)sizeof tmp : t->remain;
		long got = rd_read(t->r, tmp, n);
		if (got <= 0) break;
		t->remain -= got;
	}
}
