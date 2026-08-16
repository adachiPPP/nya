#ifndef NYA_H
#define NYA_H

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <dirent.h>
#include <fnmatch.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <strings.h>

#define NYA_VERSION "1.0.0"

typedef struct {
	char **v;
	int n;
	int cap;
} strs;

void strs_add(strs *s, const char *x);
void strs_add_own(strs *s, char *x);
void strs_free(strs *s);
int strs_has(const strs *s, const char *x);
void strs_split_ws(const char *line, strs *out);

typedef struct hnode {
	char *k;
	void *v;
	struct hnode *next;
} hnode;

typedef struct {
	hnode **buckets;
	int n;
} hmap;

hmap *hmap_new(int n);
void hmap_put(hmap *m, const char *k, void *v);
void *hmap_get(hmap *m, const char *k);
int hmap_has(hmap *m, const char *k);
void hmap_free(hmap *m);

typedef struct {
	uint32_t s[8];
	uint64_t len;
	uint8_t buf[64];
	size_t blen;
} sha256_t;

void sha256_init(sha256_t *c);
void sha256_update(sha256_t *c, const void *d, size_t n);
void sha256_final(sha256_t *c, char out[65]);
void sha256_buf(const void *d, size_t n, char out[65]);
int sha256_file(const char *path, char out[65]);

char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);
void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t m);
void *xrealloc(void *p, size_t n);
char *trim(char *s);
char *str_lower(char *s);
int str_ieq(const char *a, const char *b);
int startswith(const char *s, const char *p);
int endswith(const char *s, const char *p);
char *path_join(const char *a, const char *b);
int mkdir_p(const char *path, mode_t mode);
char *read_file(const char *path, long *len);
int write_file(const char *path, const char *data, long len, mode_t mode);
int is_dir(const char *p);
int is_file(const char *p);
int file_mtime(const char *p, time_t *out);
void fmt_size(long long bytes, char *out, size_t n);
char *now_iso8601(void);
void rm_rf(const char *path);
int run_cmd(char *const argv[]);
int run_capture(char *const argv[], char **out);
int run_capture_quiet(char *const argv[], char **out);
const char *invoking_user_name(void);
int run_sh(const char *cmd);

extern int g_noconfirm;
extern int g_overwrite;
extern int g_color;
extern int g_verbose;
void log_alpm(const char *fmt, ...);
void set_logfile(const char *path);
void info(const char *fmt, ...);
void warn(const char *fmt, ...);
void error(const char *fmt, ...);
void msg(const char *fmt, ...);
int yesno(const char *fmt, ...);
void bar_draw(const char *label, long long done, long long total);
void bar_done(void);
const char *col_bold(void);
const char *col_red(void);
const char *col_green(void);
const char *col_yellow(void);
const char *col_cyan(void);
const char *col_magenta(void);
const char *col_reset(void);

typedef struct rd {
	long (*read)(struct rd *r, void *buf, size_t n);
	void *ctx;
} rd;

long rd_read(rd *r, void *buf, size_t n);
rd *rd_open_file(const char *path);
rd *rd_open_compressed(const char *path);
rd *rd_open_mem(const void *data, size_t len);
void rd_close(rd *r);
char *rd_read_all(rd *r, long *len);

typedef struct {
	char name[4096];
	char linkname[4096];
	long long size;
	long long mode;
	long long uid;
	long long gid;
	long long mtime;
	int type;
} tar_entry;

typedef struct {
	rd *r;
	char longname[4096];
	char longlink[4096];
	char pax_path[4096];
	char pax_link[4096];
	long long remain;
	long long last_size;
	int need_pad;
	int eof;
} tar_it;

void tar_init(tar_it *t, rd *r);
int tar_next(tar_it *t, tar_entry *e);
long tar_read(tar_it *t, void *buf, size_t n);
void tar_skip(tar_it *t);
int tar_safe_path(const char *name, char *out, size_t n);

typedef struct repo {
	char *name;
	strs servers;
} repo;

typedef struct config {
	char *path;
	char *rootdir;
	char *dbpath;
	char *logfile;
	char *arch;
	char *nyacache;
	char *nixchannel;
	char *aurbase;
	char *sudobin;
	char *hostsrepo;
	strs cachedirs;
	strs holdpkg;
	strs ignorepkg;
	strs ignoregrp;
	strs siglevel;
	strs noupgrade;
	int parallel;
	int color;
	int verbosepkglists;
	int aur;
	int nix;
	int searchaur;
	int searchnix;
	int searchflatpak;
	int aurfirst;
	repo **repos;
	int nrepos;
} config;

#define J_NULL 0
#define J_BOOL 1
#define J_NUM 2
#define J_STR 3
#define J_ARR 4
#define J_OBJ 5

config *config_alloc(void);
void repo_free(repo *r);
config *config_load(const char *path, int *generated);
void config_free(config *c);
int config_resolve_arch(config *c);
int config_read_pacman(config *c, const char *pacpath);
int config_write(const char *path, config *c, int from_pac);
config *config_discover(void);
const char *cfg_rooted(const config *c, const char *path);
repo *config_find_repo(config *c, const char *name);

typedef struct pkg {
	char *repo;
	char *name;
	char *base;
	char *version;
	char *desc;
	char *url;
	char *arch;
	char *packager;
	char *builddate;
	char *installdate;
	long long builddate_ts;
	long long installdate_ts;
	long long csize;
	long long isize;
	char *md5sum;
	char *sha256sum;
	char *pgpsig;
	char *validation;
	char *filename;
	char *mtree_data;
	long mtree_len;
	char *install_data;
	long install_len;
	strs provides;
	strs depends;
	strs optdepends;
	strs conflicts;
	strs replaces;
	strs groups;
	strs licenses;
	strs backup;
	strs files;
	int reason;
	int is_local;
	int is_host;
	int has_mtree;
	int is_upgrade;
	int is_reinstall;
	int is_dep;
	int nfiles;
} pkg;

pkg *pkg_new(const char *repo);
void pkg_free(pkg *p);
int vercmp(const char *a, const char *b);

typedef struct depspec {
	char *name;
	char *mod;
	char *ver;
} depspec;

int depspec_parse(const char *s, depspec *d);
void depspec_free(depspec *d);
int depspec_matches(const depspec *dep, const char *pkgname, const char *pkgver);
int pkg_matches_dep(pkg *p, const depspec *dep);

extern pkg **g_sync;
extern int g_nsync;
extern pkg **g_local;
extern int g_nlocal;
extern hmap *g_owner;

int db_load_all(config *c);
int db_load_sync(config *c, const char *dbfile, const char *reponame);
int db_load_local(config *c);
pkg *db_find_sync(const char *name);
pkg *db_find_sync_exact(const char *repo, const char *name);
pkg *db_find_local(const char *name);
void db_build_owner_map(void);
const char *db_owner(const char *relpath);
int db_write_local_pkg(config *c, pkg *p);
void db_remove_local(config *c, const char *name, const char *version);
int db_entry_path(config *c, pkg *p, char *out, size_t n);
hmap *mtree_sha_map(const char *data, long len);

int pkg_read_pkginfo(const char *data, long len, pkg *p);
int pkg_scan_archive(config *c, const char *path, pkg *p);
int pkg_meta_from_desc(pkg *p, const char *data);

typedef struct dl {
	char **urls;
	int nurls;
	const char *dest;
	char *data;
	long datalen;
	char err[256];
	int ok;
} dl;

int dl_url(config *c, const char *url, char **data, long *len);
int dl_url_quick(config *c, const char *url, char **data, long *len, long tmo);
int dl_url_file(config *c, const char *url, const char *dest);
int dl_parallel(config *c, dl *jobs, int n);
int dl_mirror(config *c, repo *r, const char *rel, const char *dest);
int download_pkg(config *c, pkg *p, char *outpath, size_t outlen);
int download_url(config *c, const char *url, const char *dest);
int cache_find(config *c, const char *filename, char *out, size_t n);
int pkg_verify_file(config *c, pkg *p, const char *path);

typedef struct txn {
	config *c;
	pkg **add;
	int nadd;
	pkg **rm;
	int nrm;
	int nosave;
	int recursive;
	int cascade;
	int unneeded;
} txn;

void txn_init(txn *t, config *c);
void txn_free(txn *t);
void txn_add_add(txn *t, pkg *p);
void txn_add_rm(txn *t, pkg *p);
int txn_build_install(config *c, const char **targets, int ntargets, txn *t, strs *notfound);
int txn_build_remove(config *c, const char **targets, int ntargets, int recursive, int nosave, int cascade, int unneeded, txn *t, strs *notfound);
int txn_build_upgrade(config *c, txn *t);
int txn_prepare(config *c, txn *t);
int txn_file_conflicts(config *c, txn *t);
int txn_download(config *c, txn *t);
int txn_commit(config *c, txn *t);
int txn_print_summary(config *c, txn *t, int mode);
int txn_scan_archives(config *c, txn *t);

int refresh_dbs(config *c);
int do_search(config *c, const char **terms, int n, int local);
int do_info(config *c, const char **targets, int n, int local, int verbose);
int do_list(config *c, int foreign, int deps, int explicit, int orphans, int upgradable);
int do_files(config *c, const char **targets, int n);
int do_check(config *c, const char **targets, int n, int deep);
int do_owns(config *c, const char *path);
int do_clean(config *c, int all);

int aur_search(config *c, const char *term, int quiet);
int aur_search_multi(config *c, const char **terms, int n);
int aur_search_any(config *c, const char **terms, int n);
int aur_info(config *c, const char *name);
int aur_build_install(config *c, const char *name, txn *t);
int aur_pkg_exists(config *c, const char *name);
int fp_available(void);
int aur_update(config *c, txn *t);
int aur_malware_check(config *c, const char *name);
int nix_search(config *c, const char *term);
int nix_info(config *c, const char *name);
int nix_update(config *c);
int nix_search_any(config *c, const char **terms, int n);
int fp_run(int argc, char **argv);
int fp_search(config *c, const char **terms, int n);
int fp_update(config *c);
int host_install(config *c, const char *name);
int host_try_install(config *c, const char *name);

typedef struct json {
	int type;
	struct json *next;
	char *key;
	char *str;
	double num;
	struct json *child;
} json;

json *json_parse(const char *s, long len);
void json_free(json *j);
json *json_get(json *obj, const char *key);
const char *json_str(json *j);

int cli_main(int argc, char **argv);

#endif
