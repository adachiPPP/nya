#include "nya.h"

static const uint32_t K[64] = {
	0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
	0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
	0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
	0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
	0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
	0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
	0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
	0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static uint32_t rotr(uint32_t x, int n) {
	return (x >> n) | (x << (32 - n));
}

static void sha256_block(sha256_t *c, const uint8_t *p) {
	uint32_t w[64];
	uint32_t a, b, cc, d, e, f, g, h, t1, t2, i;
	for (i = 0; i < 16; i++) {
		w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) |
		       ((uint32_t)p[i * 4 + 2] << 8) | (uint32_t)p[i * 4 + 3];
	}
	for (i = 16; i < 64; i++) {
		uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
		uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}
	a = c->s[0]; b = c->s[1]; cc = c->s[2]; d = c->s[3];
	e = c->s[4]; f = c->s[5]; g = c->s[6]; h = c->s[7];
	for (i = 0; i < 64; i++) {
		uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
		uint32_t ch = (e & f) ^ (~e & g);
		uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
		uint32_t maj = (a & b) ^ (a & cc) ^ (b & cc);
		t1 = h + S1 + ch + K[i] + w[i];
		t2 = S0 + maj;
		h = g; g = f; f = e; e = d + t1;
		d = cc; cc = b; b = a; a = t1 + t2;
	}
	c->s[0] += a; c->s[1] += b; c->s[2] += cc; c->s[3] += d;
	c->s[4] += e; c->s[5] += f; c->s[6] += g; c->s[7] += h;
}

void sha256_init(sha256_t *c) {
	c->s[0] = 0x6a09e667; c->s[1] = 0xbb67ae85; c->s[2] = 0x3c6ef372; c->s[3] = 0xa54ff53a;
	c->s[4] = 0x510e527f; c->s[5] = 0x9b05688c; c->s[6] = 0x1f83d9ab; c->s[7] = 0x5be0cd19;
	c->len = 0;
	c->blen = 0;
}

void sha256_update(sha256_t *c, const void *d, size_t n) {
	const uint8_t *p = d;
	c->len += n;
	while (n > 0) {
		size_t take = 64 - c->blen;
		if (take > n) take = n;
		memcpy(c->buf + c->blen, p, take);
		c->blen += take;
		p += take;
		n -= take;
		if (c->blen == 64) {
			sha256_block(c, c->buf);
			c->blen = 0;
		}
	}
}

void sha256_final(sha256_t *c, char out[65]) {
	uint64_t bits = c->len * 8;
	uint8_t pad = 0x80;
	sha256_update(c, &pad, 1);
	uint8_t z = 0;
	while (c->blen != 56) sha256_update(c, &z, 1);
	uint8_t lenb[8];
	int i;
	for (i = 0; i < 8; i++) lenb[i] = (uint8_t)(bits >> (56 - i * 8));
	sha256_update(c, lenb, 8);
	for (i = 0; i < 8; i++) {
		snprintf(out + i * 8, 9, "%08x", c->s[i]);
	}
	out[64] = '\0';
}

void sha256_buf(const void *d, size_t n, char out[65]) {
	sha256_t c;
	sha256_init(&c);
	sha256_update(&c, d, n);
	sha256_final(&c, out);
}

int sha256_file(const char *path, char out[65]) {
	int fd = open(path, O_RDONLY);
	if (fd < 0) return -1;
	sha256_t c;
	sha256_init(&c);
	uint8_t buf[65536];
	ssize_t got;
	while ((got = read(fd, buf, sizeof buf)) > 0) sha256_update(&c, buf, got);
	close(fd);
	if (got < 0) return -1;
	sha256_final(&c, out);
	return 0;
}

void strs_add(strs *s, const char *x) {
	if (s->n == s->cap) {
		s->cap = s->cap ? s->cap * 2 : 8;
		s->v = xrealloc(s->v, s->cap * sizeof(char *));
	}
	s->v[s->n++] = xstrdup(x);
}

void strs_add_own(strs *s, char *x) {
	if (s->n == s->cap) {
		s->cap = s->cap ? s->cap * 2 : 8;
		s->v = xrealloc(s->v, s->cap * sizeof(char *));
	}
	s->v[s->n++] = x;
}

void strs_free(strs *s) {
	int i;
	for (i = 0; i < s->n; i++) free(s->v[i]);
	free(s->v);
	s->v = NULL;
	s->n = s->cap = 0;
}

int strs_has(const strs *s, const char *x) {
	int i;
	for (i = 0; i < s->n; i++) {
		if (strcmp(s->v[i], x) == 0) return 1;
	}
	return 0;
}

void strs_split_ws(const char *line, strs *out) {
	const char *p = line;
	while (*p) {
		while (*p && isspace((unsigned char)*p)) p++;
		if (!*p) break;
		const char *start = p;
		while (*p && !isspace((unsigned char)*p)) p++;
		strs_add_own(out, xstrndup(start, p - start));
	}
}

static uint64_t fnv1a(const char *k) {
	uint64_t h = 1469598103934665603ULL;
	while (*k) {
		h ^= (unsigned char)*k++;
		h *= 1099511628211ULL;
	}
	return h;
}

hmap *hmap_new(int n) {
	hmap *m = xcalloc(1, sizeof *m);
	m->n = n > 0 ? n : 64;
	m->buckets = xcalloc(m->n, sizeof(hnode *));
	return m;
}

void hmap_put(hmap *m, const char *k, void *v) {
	uint64_t h = fnv1a(k) % m->n;
	hnode *node;
	for (node = m->buckets[h]; node; node = node->next) {
		if (strcmp(node->k, k) == 0) {
			node->v = v;
			return;
		}
	}
	node = xmalloc(sizeof *node);
	node->k = xstrdup(k);
	node->v = v;
	node->next = m->buckets[h];
	m->buckets[h] = node;
}

void *hmap_get(hmap *m, const char *k) {
	uint64_t h = fnv1a(k) % m->n;
	hnode *node;
	for (node = m->buckets[h]; node; node = node->next) {
		if (strcmp(node->k, k) == 0) return node->v;
	}
	return NULL;
}

int hmap_has(hmap *m, const char *k) {
	return hmap_get(m, k) != NULL;
}

void hmap_free(hmap *m) {
	int i;
	for (i = 0; i < m->n; i++) {
		hnode *node = m->buckets[i];
		while (node) {
			hnode *next = node->next;
			free(node->k);
			free(node);
			node = next;
		}
	}
	free(m->buckets);
	free(m);
}

char *xstrdup(const char *s) {
	char *r = strdup(s);
	if (!r) {
		fprintf(stderr, "nya: out of memory\n");
		exit(1);
	}
	return r;
}

char *xstrndup(const char *s, size_t n) {
	char *r = strndup(s, n);
	if (!r) {
		fprintf(stderr, "nya: out of memory\n");
		exit(1);
	}
	return r;
}

void *xmalloc(size_t n) {
	void *p = malloc(n ? n : 1);
	if (!p) {
		fprintf(stderr, "nya: out of memory\n");
		exit(1);
	}
	return p;
}

void *xcalloc(size_t n, size_t m) {
	void *p = calloc(n ? n : 1, m ? m : 1);
	if (!p) {
		fprintf(stderr, "nya: out of memory\n");
		exit(1);
	}
	return p;
}

void *xrealloc(void *p, size_t n) {
	void *r = realloc(p, n ? n : 1);
	if (!r) {
		fprintf(stderr, "nya: out of memory\n");
		exit(1);
	}
	return r;
}

char *trim(char *s) {
	char *end;
	while (*s && isspace((unsigned char)*s)) s++;
	end = s + strlen(s);
	while (end > s && isspace((unsigned char)end[-1])) end--;
	*end = '\0';
	return s;
}

char *str_lower(char *s) {
	char *p = s;
	for (; *p; p++) *p = tolower((unsigned char)*p);
	return s;
}

int str_ieq(const char *a, const char *b) {
	while (*a && *b) {
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
		a++;
		b++;
	}
	return *a == *b;
}

int startswith(const char *s, const char *p) {
	return strncmp(s, p, strlen(p)) == 0;
}

int endswith(const char *s, const char *p) {
	size_t ls = strlen(s), lp = strlen(p);
	return ls >= lp && strcmp(s + ls - lp, p) == 0;
}

char *path_join(const char *a, const char *b) {
	size_t la = strlen(a);
	int need = la > 0 && a[la - 1] != '/';
	char *r = xmalloc(la + need + strlen(b) + 1);
	sprintf(r, "%s%s%s", a, need ? "/" : "", b);
	return r;
}

int mkdir_p(const char *path, mode_t mode) {
	char tmp[4096];
	size_t len = strlen(path);
	if (len >= sizeof tmp) return -1;
	memcpy(tmp, path, len + 1);
	size_t i;
	for (i = 1; i < len; i++) {
		if (tmp[i] == '/') {
			tmp[i] = '\0';
			if (mkdir(tmp, mode) != 0 && errno != EEXIST) return -1;
			tmp[i] = '/';
		}
	}
	if (mkdir(tmp, mode) != 0 && errno != EEXIST) return -1;
	return 0;
}

char *read_file(const char *path, long *len) {
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return NULL;
	}
	long sz = ftell(f);
	if (sz < 0) {
		fclose(f);
		return NULL;
	}
	rewind(f);
	char *data = xmalloc(sz + 1);
	if (sz > 0 && fread(data, 1, sz, f) != (size_t)sz) {
		free(data);
		fclose(f);
		return NULL;
	}
	data[sz] = '\0';
	fclose(f);
	if (len) *len = sz;
	return data;
}

int write_file(const char *path, const char *data, long len, mode_t mode) {
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
	if (fd < 0) return -1;
	long off = 0;
	while (off < len) {
		ssize_t w = write(fd, data + off, len - off);
		if (w < 0) {
			if (errno == EINTR) continue;
			close(fd);
			return -1;
		}
		off += w;
	}
	close(fd);
	return 0;
}

int is_dir(const char *p) {
	struct stat st;
	return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

int is_file(const char *p) {
	struct stat st;
	return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

int file_mtime(const char *p, time_t *out) {
	struct stat st;
	if (stat(p, &st) != 0) return -1;
	*out = st.st_mtime;
	return 0;
}

void fmt_size(long long bytes, char *out, size_t n) {
	const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
	int u = 0;
	double v = (double)bytes;
	while (v >= 1024.0 && u < 4) {
		v /= 1024.0;
		u++;
	}
	if (u == 0) snprintf(out, n, "%.0f B", v);
	else snprintf(out, n, "%.2f %s", v, units[u]);
}

char *now_iso8601(void) {
	static char buf[64];
	time_t t = time(NULL);
	struct tm tm;
	gmtime_r(&t, &tm);
	strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S+0000", &tm);
	return buf;
}

void rm_rf(const char *path) {
	struct stat st;
	if (lstat(path, &st) != 0) return;
	if (S_ISDIR(st.st_mode)) {
		DIR *d = opendir(path);
		if (d) {
			struct dirent *e;
			while ((e = readdir(d)) != NULL) {
				if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
				char *sub = path_join(path, e->d_name);
				rm_rf(sub);
				free(sub);
			}
			closedir(d);
		}
		rmdir(path);
	} else {
		unlink(path);
	}
}

int run_cmd(char *const argv[]) {
	pid_t pid = fork();
	if (pid < 0) return -1;
	if (pid == 0) {
		execvp(argv[0], argv);
		_exit(127);
	}
	int st;
	while (waitpid(pid, &st, 0) < 0) {
		if (errno != EINTR) return -1;
	}
	if (WIFEXITED(st)) return WEXITSTATUS(st);
	return -1;
}

static int run_capture_impl(char *const argv[], char **out, int quiet_err) {
	int pipefd[2];
	if (pipe(pipefd) != 0) return -1;
	pid_t pid = fork();
	if (pid < 0) return -1;
	if (pid == 0) {
		dup2(pipefd[1], STDOUT_FILENO);
		if (quiet_err) {
			int devnull = open("/dev/null", O_WRONLY);
			if (devnull >= 0) {
				dup2(devnull, STDERR_FILENO);
				close(devnull);
			}
		}
		close(pipefd[0]);
		close(pipefd[1]);
		execvp(argv[0], argv);
		_exit(127);
	}
	close(pipefd[1]);
	char *buf = xmalloc(65536);
	long cap = 65536, n = 0;
	for (;;) {
		if (n + 1 >= cap) {
			cap *= 2;
			buf = xrealloc(buf, cap);
		}
		long got = read(pipefd[0], buf + n, cap - n - 1);
		if (got <= 0) break;
		n += got;
	}
	close(pipefd[0]);
	int st;
	while (waitpid(pid, &st, 0) < 0) {
		if (errno != EINTR) break;
	}
	buf[n] = '\0';
	*out = buf;
	if (WIFEXITED(st)) return WEXITSTATUS(st);
	return -1;
}

int run_capture(char *const argv[], char **out) {
	return run_capture_impl(argv, out, 0);
}

int run_capture_quiet(char *const argv[], char **out) {
	return run_capture_impl(argv, out, 1);
}

int run_sh(const char *cmd) {
	return system(cmd);
}

int g_noconfirm = 0;
int g_overwrite = 0;
int g_color = 0;
int g_verbose = 0;
static const char *g_logpath = NULL;

void set_logfile(const char *path) {
	g_logpath = path;
}

void log_alpm(const char *fmt, ...) {
	if (!g_logpath) return;
	FILE *f = fopen(g_logpath, "a");
	if (!f) return;
	fprintf(f, "[%s] [NYA] ", now_iso8601());
	va_list ap;
	va_start(ap, fmt);
	vfprintf(f, fmt, ap);
	va_end(ap);
	fputc('\n', f);
	fclose(f);
}

void info(const char *fmt, ...) {
	va_list ap;
	printf("%s:: %s", col_bold(), col_reset());
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
}

void warn(const char *fmt, ...) {
	va_list ap;
	fprintf(stderr, "%swarning:%s ", col_yellow(), col_reset());
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
}

void error(const char *fmt, ...) {
	va_list ap;
	fprintf(stderr, "%serror:%s ", col_red(), col_reset());
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
}

void msg(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
}

void bar_draw(const char *label, long long done, long long total) {
	if (total <= 0) return;
	if (done > total) done = total;
	int pct = (int)(done * 100 / total);
	if (pct > 100) pct = 100;
	int barw = 22;
	int filled = barw * pct / 100;
	int i;
	fprintf(stderr, "\r\033[K%s [", label);
	for (i = 0; i < barw; i++) fputc(i < filled ? '=' : (i == filled ? '>' : ' '), stderr);
	fprintf(stderr, "] %3d%%", pct);
	fflush(stderr);
}

void bar_done(void) {
	fprintf(stderr, "\n");
	fflush(stderr);
}

static int read_key(void) {
	int fd = fileno(stdin);
	struct termios old, raw;
	if (tcgetattr(fd, &old) != 0) return -1;
	raw = old;
	raw.c_lflag &= ~(ICANON | ECHO);
	raw.c_cc[VMIN] = 1;
	raw.c_cc[VTIME] = 0;
	tcsetattr(fd, TCSANOW, &raw);
	int c = getchar();
	tcsetattr(fd, TCSANOW, &old);
	return c;
}

int yesno(const char *fmt, ...) {
	if (g_noconfirm) return 1;
	char buf[512];
	va_list ap;
	printf("%s:: %s", col_bold(), col_reset());
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf(" [Y/n] ");
	fflush(stdout);
	if (isatty(fileno(stdin))) {
		for (;;) {
			int c = read_key();
			if (c == ' ' || c == '\n' || c == '\r' || c == 'y' || c == 'Y') {
				printf("\n");
				return 1;
			}
			if (c == 'n' || c == 'N') {
				printf("\n");
				return 0;
			}
			if (c == EOF || c < 0) {
				printf("\n");
				return 1;
			}
		}
	}
	if (!fgets(buf, sizeof buf, stdin)) return 0;
	trim(buf);
	if (buf[0] == ' ' || buf[0] == '\0' || buf[0] == 'y' || buf[0] == 'Y') return 1;
	return 0;
}

static const char *esc(const char *code) {
	return g_color ? code : "";
}

const char *col_bold(void) { return esc("\033[1m"); }
const char *col_red(void) { return esc("\033[1;31m"); }
const char *col_green(void) { return esc("\033[1;32m"); }
const char *col_yellow(void) { return esc("\033[1;33m"); }
const char *col_cyan(void) { return esc("\033[1;36m"); }
const char *col_magenta(void) { return esc("\033[1;35m"); }
const char *col_reset(void) { return esc("\033[0m"); }
