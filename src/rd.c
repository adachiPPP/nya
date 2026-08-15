#include "nya.h"
#include <zlib.h>
#include <zstd.h>
#include <lzma.h>

struct gz_rd;
struct zst_rd;
struct xz_rd;
long gz_read(rd *r, void *buf, size_t n);
long zst_read(rd *r, void *buf, size_t n);
long xz_read(rd *r, void *buf, size_t n);

long rd_read(rd *r, void *buf, size_t n) {
	return r->read(r, buf, n);
}

typedef struct {
	int fd;
} file_rd;

static long file_read(rd *r, void *buf, size_t n) {
	file_rd *f = r->ctx;
	ssize_t got;
	do {
		got = read(f->fd, buf, n);
	} while (got < 0 && errno == EINTR);
	return got;
}

rd *rd_open_file(const char *path) {
	int fd = open(path, O_RDONLY);
	if (fd < 0) return NULL;
	rd *r = xmalloc(sizeof *r);
	file_rd *f = xmalloc(sizeof *f);
	f->fd = fd;
	r->read = file_read;
	r->ctx = f;
	return r;
}

typedef struct {
	const uint8_t *data;
	size_t len;
	size_t pos;
} mem_rd;

static long mem_read(rd *r, void *buf, size_t n) {
	mem_rd *m = r->ctx;
	size_t avail = m->len - m->pos;
	if (n > avail) n = avail;
	memcpy(buf, m->data + m->pos, n);
	m->pos += n;
	return n;
}

rd *rd_open_mem(const void *data, size_t len) {
	rd *r = xmalloc(sizeof *r);
	mem_rd *m = xmalloc(sizeof *m);
	m->data = data;
	m->len = len;
	m->pos = 0;
	r->read = mem_read;
	r->ctx = m;
	return r;
}

char *rd_read_all(rd *r, long *len) {
	char *buf = xmalloc(65536);
	long cap = 65536;
	long n = 0;
	for (;;) {
		if (n == cap) {
			cap *= 2;
			buf = xrealloc(buf, cap);
		}
		long got = rd_read(r, buf + n, cap - n);
		if (got <= 0) break;
		n += got;
	}
	if (len) *len = n;
	return buf;
}

struct gz_rd {
	int fd;
	z_stream zs;
	uint8_t in[65536];
	int eof;
};

long gz_read(rd *r, void *buf, size_t n) {
	struct gz_rd *g = r->ctx;
	z_stream *zs = &g->zs;
	zs->next_out = buf;
	zs->avail_out = n;
	long total = 0;
	for (;;) {
		if (zs->avail_in == 0 && !g->eof) {
			ssize_t got;
			do {
				got = read(g->fd, g->in, sizeof g->in);
			} while (got < 0 && errno == EINTR);
			if (got < 0) return -1;
			if (got == 0) g->eof = 1;
			zs->next_in = g->in;
			zs->avail_in = got;
		}
		size_t before = zs->avail_out;
		int ret = inflate(zs, Z_NO_FLUSH);
		total += before - zs->avail_out;
		if (ret == Z_STREAM_END) {
			if (g->eof && zs->avail_in == 0) return total;
			if (inflateReset(zs) != Z_OK) return -1;
			continue;
		}
		if (ret == Z_OK || ret == Z_BUF_ERROR) {
			if (before - zs->avail_out > 0) continue;
			if (g->eof && zs->avail_in == 0) return total;
			if (ret == Z_BUF_ERROR) return total;
			if (zs->avail_in == 0) continue;
			return total;
		}
		return -1;
	}
}

static rd *rd_open_gzip(const char *path) {
	int fd = open(path, O_RDONLY);
	if (fd < 0) return NULL;
	rd *r = xmalloc(sizeof *r);
	struct gz_rd *g = xcalloc(1, sizeof *g);
	g->fd = fd;
	if (inflateInit2(&g->zs, 15 + 16) != Z_OK) {
		close(fd);
		free(g);
		free(r);
		return NULL;
	}
	r->read = gz_read;
	r->ctx = g;
	return r;
}

struct zst_rd {
	int fd;
	ZSTD_DStream *ds;
	uint8_t in[65536];
	size_t inlen;
	size_t inpos;
	int eof;
};

long zst_read(rd *r, void *buf, size_t n) {
	struct zst_rd *z = r->ctx;
	ZSTD_outBuffer ob = {buf, n, 0};
	ZSTD_inBuffer ib = {z->in, z->inlen, z->inpos};
	for (;;) {
		if (ob.pos == ob.size) break;
		if (ib.pos == ib.size) {
			if (z->eof) break;
			ssize_t got;
			do {
				got = read(z->fd, z->in, sizeof z->in);
			} while (got < 0 && errno == EINTR);
			if (got < 0) return -1;
			if (got == 0) z->eof = 1;
			z->inlen = got;
			z->inpos = 0;
			ib.src = z->in;
			ib.size = got;
			ib.pos = 0;
		}
		size_t rc = ZSTD_decompressStream(z->ds, &ob, &ib);
		z->inpos = ib.pos;
		if (ZSTD_isError(rc)) return -1;
		if (rc == 0) {
			if (ZSTD_initDStream(z->ds) == 0) return -1;
			if (z->eof && ib.pos == ib.size) break;
			continue;
		}
		if (ib.pos == ib.size && z->eof) break;
	}
	return ob.pos;
}

static rd *rd_open_zstd(const char *path) {
	int fd = open(path, O_RDONLY);
	if (fd < 0) return NULL;
	rd *r = xmalloc(sizeof *r);
	struct zst_rd *z = xcalloc(1, sizeof *z);
	z->fd = fd;
	z->ds = ZSTD_createDStream();
	if (!z->ds || ZSTD_initDStream(z->ds) == 0) {
		if (z->ds) ZSTD_freeDStream(z->ds);
		close(fd);
		free(z);
		free(r);
		return NULL;
	}
	r->read = zst_read;
	r->ctx = z;
	return r;
}

struct xz_rd {
	int fd;
	lzma_stream ls;
	uint8_t in[65536];
	int eof;
};

long xz_read(rd *r, void *buf, size_t n) {
	struct xz_rd *x = r->ctx;
	lzma_stream *ls = &x->ls;
	ls->next_out = buf;
	ls->avail_out = n;
	long total = 0;
	for (;;) {
		if (ls->avail_in == 0) {
			if (x->eof) break;
			ssize_t got;
			do {
				got = read(x->fd, x->in, sizeof x->in);
			} while (got < 0 && errno == EINTR);
			if (got < 0) return -1;
			if (got == 0) x->eof = 1;
			ls->next_in = x->in;
			ls->avail_in = got;
		}
		size_t before = ls->avail_out;
		lzma_ret ret = lzma_code(ls, x->eof ? LZMA_FINISH : LZMA_RUN);
		total += before - ls->avail_out;
		if (ret == LZMA_STREAM_END) break;
		if (ret != LZMA_OK) {
			if (ret == LZMA_BUF_ERROR && x->eof && ls->avail_in == 0) break;
			return -1;
		}
	}
	return total;
}

static rd *rd_open_xz(const char *path) {
	int fd = open(path, O_RDONLY);
	if (fd < 0) return NULL;
	rd *r = xmalloc(sizeof *r);
	struct xz_rd *x = xcalloc(1, sizeof *x);
	x->fd = fd;
	if (lzma_stream_decoder(&x->ls, UINT64_MAX, 0) != LZMA_OK) {
		close(fd);
		free(x);
		free(r);
		return NULL;
	}
	r->read = xz_read;
	r->ctx = x;
	return r;
}

rd *rd_open_compressed(const char *path) {
	int fd = open(path, O_RDONLY);
	if (fd < 0) return NULL;
	uint8_t magic[8];
	ssize_t got;
	do {
		got = read(fd, magic, sizeof magic);
	} while (got < 0 && errno == EINTR);
	close(fd);
	if (got < 0) return NULL;
	rd *r = NULL;
	if (got >= 2 && magic[0] == 0x1f && magic[1] == 0x8b) {
		r = rd_open_gzip(path);
	} else if (got >= 4 && magic[0] == 0x28 && magic[1] == 0xb5 && magic[2] == 0x2f && magic[3] == 0xfd) {
		r = rd_open_zstd(path);
	} else if (got >= 6 && magic[0] == 0xfd && magic[1] == 0x37 && magic[2] == 0x7a &&
	           magic[3] == 0x58 && magic[4] == 0x5a && magic[5] == 0x00) {
		r = rd_open_xz(path);
	} else {
		r = rd_open_file(path);
	}
	return r;
}

void rd_close(rd *r) {
	if (!r) return;
	if (r->read == file_read) {
		file_rd *f = r->ctx;
		close(f->fd);
		free(f);
	} else if (r->read == mem_read) {
		free(r->ctx);
	} else if (r->read == gz_read) {
		struct gz_rd *g = r->ctx;
		close(g->fd);
		inflateEnd(&g->zs);
		free(g);
	} else if (r->read == zst_read) {
		struct zst_rd *z = r->ctx;
		close(z->fd);
		ZSTD_freeDStream(z->ds);
		free(z);
	} else if (r->read == xz_read) {
		struct xz_rd *x = r->ctx;
		close(x->fd);
		lzma_end(&x->ls);
		free(x);
	}
	free(r);
}
