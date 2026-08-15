#include "nya.h"

typedef struct {
	int op;
	int y, u, s, i, l, k, o, q, w, n, d, e, m, t, g, v;
	int aurup;
	int cclean;
	int cascade;
	int asdeps;
	int noconfirm;
	int overwrite;
	const char *config_path;
	const char *root_ov;
	const char *dbpath_ov;
	const char *cachedir_ov;
	const char *word;
	strs targets;
	strs ignore;
	strs ignoregrp;
} cli;

static void print_help(void) {
	printf("nya %s - pacman-compatible package manager\n", NYA_VERSION);
	printf("\nusage: nya [options] <operation> [targets...]\n");
	printf("\noperations:\n");
	printf("  nya install <pkg>...          install packages (same as -S)\n");
	printf("  nya remove <pkg>...           remove packages (same as -R)\n");
	printf("  nya search <term>...          search package databases (same as -Ss)\n");
	printf("  nya sync                      synchronize package databases (same as -Sy)\n");
	printf("  nya update                    full system upgrade incl. AUR (sync + pacman + AUR)\n");
	printf("  nya upgrade                   full system upgrade incl. AUR (same as update)\n");
	printf("  nya info <pkg>...             show package info (same as -Si)\n");
	printf("  nya list                      list installed packages (same as -Q)\n");
	printf("  nya files <pkg>...            list files of installed packages (same as -Ql)\n");
	printf("  nya check [<pkg>...]          verify installed files (same as -Qk, -kk deep)\n");
	printf("  nya owns <path>               find which package owns a file (same as -Qo)\n");
	printf("  nya clean [--all]             clean package cache (same as -Sc / -Scc)\n");
	printf("  nya aur search|info|install <name>...   Arch User Repository (set 'aur = true')\n");
	printf("  nya nix search|info|update <term>...    nixpkgs search via nix (set 'nix = true')\n");
	printf("  nya -fp search|install|remove|list|update <app>...   flatpak integration\n");
	printf("  nya --read-paconfig [file]    import repositories from pacman.conf\n");
	printf("  nya config                    print effective configuration\n");
	printf("\npacman-compatible flags:\n");
	printf("  -S -Ss -Si -Sy -Su -Syu -Sw -Sc -Scc\n");
	printf("  -Q -Qs -Qi -Ql -Qk -Qo -Qm -Qd -Qe -Qt -Qu\n");
	printf("  -R -Rs -Rn -Rc -Ru   -U <file.pkg.tar.zst>...\n");
	printf("\noptions:\n");
	printf("  --noconfirm    --needed    --asdeps    --overwrite\n");
	printf("  --config <path> --root <dir> --dbpath <path> --cachedir <dir>\n");
	printf("  --color --nocolor --verbose --quiet --help --version\n");
}

static int parse_long(cli *cl, const char *arg, int *i, int argc, char **argv) {
	if (strcmp(arg, "--read-paconfig") == 0) {
		cl->op = 'P';
		return 0;
	}
	if (strcmp(arg, "--noconfirm") == 0) {
		cl->noconfirm = 1;
		return 0;
	}
	if (strcmp(arg, "--needed") == 0) return 0;
	if (strcmp(arg, "--asdeps") == 0) {
		cl->asdeps = 1;
		return 0;
	}
	if (strcmp(arg, "--asexplicit") == 0) {
		cl->asdeps = 0;
		return 0;
	}
	if (strcmp(arg, "--overwrite") == 0) {
		cl->overwrite = 1;
		return 0;
	}
	if (strcmp(arg, "--color") == 0) {
		g_color = 1;
		return 0;
	}
	if (strcmp(arg, "--nocolor") == 0) {
		g_color = 0;
		return 0;
	}
	if (strcmp(arg, "--verbose") == 0) {
		g_verbose = 1;
		return 0;
	}
	if (strcmp(arg, "--quiet") == 0) {
		cl->q = 1;
		return 0;
	}
	if (strcmp(arg, "--help") == 0) {
		cl->op = 'h';
		return 0;
	}
	if (strcmp(arg, "--version") == 0) {
		cl->op = 'V';
		return 0;
	}
	if (strcmp(arg, "--refresh") == 0) {
		cl->y = 1;
		return 0;
	}
	if (strcmp(arg, "--sysupgrade") == 0) {
		cl->u = 1;
		return 0;
	}
	if (strcmp(arg, "--search") == 0) {
		cl->s = 1;
		return 0;
	}
	if (strcmp(arg, "--info") == 0) {
		cl->i++;
		return 0;
	}
	if (strcmp(arg, "--files") == 0) {
		cl->l = 1;
		return 0;
	}
	if (strcmp(arg, "--check") == 0) {
		cl->k++;
		return 0;
	}
	if (strcmp(arg, "--owns") == 0) {
		cl->o = 1;
		return 0;
	}
	if (strcmp(arg, "--clean") == 0) {
		cl->cclean++;
		return 0;
	}
	if (strcmp(arg, "--all") == 0) {
		cl->cclean = 2;
		return 0;
	}
	if (strcmp(arg, "--download") == 0) {
		cl->w = 1;
		return 0;
	}
	if (strcmp(arg, "--nosave") == 0) {
		cl->n = 1;
		return 0;
	}
	if (strcmp(arg, "--recursive") == 0) {
		cl->s = 1;
		return 0;
	}
	if (strcmp(arg, "--cascade") == 0) {
		cl->cascade = 1;
		return 0;
	}
	if (strcmp(arg, "--unneeded") == 0) {
		cl->u = 1;
		return 0;
	}
	if (strcmp(arg, "--foreign") == 0) {
		cl->m = 1;
		return 0;
	}
	if (strcmp(arg, "--deps") == 0) {
		cl->d = 1;
		return 0;
	}
	if (strcmp(arg, "--explicit") == 0) {
		cl->e = 1;
		return 0;
	}
	if (strcmp(arg, "--orphans") == 0) {
		cl->t = 1;
		return 0;
	}
	if (strcmp(arg, "--upgrades") == 0) {
		cl->u = 1;
		return 0;
	}
	if (strncmp(arg, "--config=", 9) == 0) {
		cl->config_path = arg + 9;
		return 0;
	}
	if (strcmp(arg, "--config") == 0) {
		if (*i + 1 >= argc) return -1;
		cl->config_path = argv[++(*i)];
		return 0;
	}
	if (strncmp(arg, "--root=", 7) == 0) {
		cl->root_ov = arg + 7;
		return 0;
	}
	if (strcmp(arg, "--root") == 0) {
		if (*i + 1 >= argc) return -1;
		cl->root_ov = argv[++(*i)];
		return 0;
	}
	if (strncmp(arg, "--dbpath=", 9) == 0) {
		cl->dbpath_ov = arg + 9;
		return 0;
	}
	if (strcmp(arg, "--dbpath") == 0) {
		if (*i + 1 >= argc) return -1;
		cl->dbpath_ov = argv[++(*i)];
		return 0;
	}
	if (strncmp(arg, "--cachedir=", 11) == 0) {
		cl->cachedir_ov = arg + 11;
		return 0;
	}
	if (strcmp(arg, "--cachedir") == 0) {
		if (*i + 1 >= argc) return -1;
		cl->cachedir_ov = argv[++(*i)];
		return 0;
	}
	if (strncmp(arg, "--ignore=", 9) == 0) {
		strs tmp;
		memset(&tmp, 0, sizeof tmp);
		strs_split_ws(arg + 9, &tmp);
		int j;
		for (j = 0; j < tmp.n; j++) strs_add(&cl->ignore, tmp.v[j]);
		strs_free(&tmp);
		return 0;
	}
	if (strncmp(arg, "--ignoregroup=", 14) == 0) {
		strs tmp;
		memset(&tmp, 0, sizeof tmp);
		strs_split_ws(arg + 14, &tmp);
		int j;
		for (j = 0; j < tmp.n; j++) strs_add(&cl->ignoregrp, tmp.v[j]);
		strs_free(&tmp);
		return 0;
	}
	return -1;
}

static int parse_short(cli *cl, const char *arg, int *i, int argc, char **argv) {
	const char *p = arg + 1;
	while (*p) {
		switch (*p) {
		case 'S': cl->op = 'S'; break;
		case 'Q': cl->op = 'Q'; break;
		case 'R': cl->op = 'R'; break;
		case 'U': cl->op = 'U'; break;
		case 'y': cl->y = 1; break;
		case 'u': cl->u = 1; break;
		case 's': cl->s = 1; break;
		case 'i': cl->i++; break;
		case 'l': cl->l = 1; break;
		case 'k': cl->k++; break;
		case 'o': cl->o = 1; break;
		case 'q': cl->q = 1; break;
		case 'w': cl->w = 1; break;
		case 'n': cl->n = 1; break;
		case 'd': cl->d = 1; break;
		case 'e': cl->e = 1; break;
		case 'm': cl->m = 1; break;
		case 't': cl->t = 1; break;
		case 'c': cl->cclean++; break;
		case 'g': cl->g = 1; break;
		case 'v': g_verbose = 1; break;
		case 'h': cl->op = 'h'; break;
		case 'V': cl->op = 'V'; break;
		case 'r':
		case 'b': {
			const char *val;
			if (p[1]) {
				val = p + 1;
				p += strlen(p) - 1;
			} else {
				if (*i + 1 >= argc) return -1;
				val = argv[++(*i)];
				p += strlen(p) - 1;
			}
			if (arg[1] == 'r') cl->root_ov = val;
			else cl->dbpath_ov = val;
			break;
		}
		default:
			error("invalid option -- '%c'", *p);
			return -1;
		}
		p++;
	}
	return 0;
}

static int word_command(cli *cl, const char *w) {
	if (strcmp(w, "install") == 0) {
		cl->op = 'S';
		return 0;
	}
	if (strcmp(w, "search") == 0) {
		cl->op = 'S';
		cl->s = 1;
		return 0;
	}
	if (strcmp(w, "remove") == 0 || strcmp(w, "uninstall") == 0) {
		cl->op = 'R';
		return 0;
	}
	if (strcmp(w, "sync") == 0) {
		cl->op = 'S';
		cl->y = 1;
		return 0;
	}
	if (strcmp(w, "update") == 0) {
		cl->op = 'S';
		cl->y = 1;
		cl->u = 1;
		cl->aurup = 1;
		return 0;
	}
	if (strcmp(w, "upgrade") == 0) {
		cl->op = 'S';
		cl->y = 1;
		cl->u = 1;
		cl->aurup = 1;
		return 0;
	}
	if (strcmp(w, "info") == 0) {
		cl->op = 'S';
		cl->i = 1;
		return 0;
	}
	if (strcmp(w, "list") == 0) {
		cl->op = 'Q';
		return 0;
	}
	if (strcmp(w, "files") == 0) {
		cl->op = 'Q';
		cl->l = 1;
		return 0;
	}
	if (strcmp(w, "check") == 0) {
		cl->op = 'Q';
		cl->k = 1;
		return 0;
	}
	if (strcmp(w, "owns") == 0) {
		cl->op = 'Q';
		cl->o = 1;
		return 0;
	}
	if (strcmp(w, "clean") == 0) {
		cl->op = 'S';
		cl->cclean = 1;
		return 0;
	}
	if (strcmp(w, "nix") == 0) {
		cl->op = 'n';
		return 0;
	}
	if (strcmp(w, "aur") == 0) {
		cl->op = 'a';
		return 0;
	}
	if (strcmp(w, "flatpak") == 0) {
		cl->op = 'f';
		return 0;
	}
	if (strcmp(w, "config") == 0) {
		cl->op = 'c';
		return 0;
	}
	return -1;
}

static void reload_dbs(config *c) {
	free(g_sync);
	g_sync = NULL;
	g_nsync = 0;
	free(g_local);
	g_local = NULL;
	g_nlocal = 0;
	db_load_all(c);
}

static int run_txn(config *c, txn *t, int mode) {
	if (txn_prepare(c, t) != 0) {
		txn_free(t);
		return 1;
	}
	if (txn_download(c, t) != 0) {
		txn_free(t);
		return 1;
	}
	if (txn_scan_archives(c, t) != 0) {
		txn_free(t);
		return 1;
	}
	if (txn_file_conflicts(c, t) != 0) {
		txn_free(t);
		return 1;
	}
	txn_print_summary(c, t, mode);
	if (mode == 1) {
		if (!yesno("Do you want to remove these packages? ")) {
			msg("operation cancelled");
			txn_free(t);
			return 1;
		}
	} else {
		if (!yesno("Proceed with installation? ")) {
			msg("operation cancelled");
			txn_free(t);
			return 1;
		}
	}
	if (txn_commit(c, t) != 0) {
		txn_free(t);
		return 1;
	}
	txn_free(t);
	return 0;
}

static int do_read_paconfig(const char *dest, const char *pacpath) {
	config *existing = NULL;
	if (is_file(dest)) existing = config_load(dest, NULL);
	if (!existing) existing = config_alloc();
	config *pac = config_alloc();
	if (config_read_pacman(pac, pacpath) != 0) {
		error("could not read pacman config %s", pacpath);
		config_free(pac);
		config_free(existing);
		return 1;
	}
	free(existing->rootdir);
	existing->rootdir = xstrdup(pac->rootdir);
	free(existing->dbpath);
	existing->dbpath = xstrdup(pac->dbpath);
	free(existing->arch);
	existing->arch = xstrdup(pac->arch);
	strs_free(&existing->cachedirs);
	existing->cachedirs = pac->cachedirs;
	memset(&pac->cachedirs, 0, sizeof pac->cachedirs);
	strs_free(&existing->siglevel);
	existing->siglevel = pac->siglevel;
	memset(&pac->siglevel, 0, sizeof pac->siglevel);
	strs_free(&existing->holdpkg);
	existing->holdpkg = pac->holdpkg;
	memset(&pac->holdpkg, 0, sizeof pac->holdpkg);
	strs_free(&existing->ignorepkg);
	existing->ignorepkg = pac->ignorepkg;
	memset(&pac->ignorepkg, 0, sizeof pac->ignorepkg);
	strs_free(&existing->ignoregrp);
	existing->ignoregrp = pac->ignoregrp;
	memset(&pac->ignoregrp, 0, sizeof pac->ignoregrp);
	strs_free(&existing->noupgrade);
	existing->noupgrade = pac->noupgrade;
	memset(&pac->noupgrade, 0, sizeof pac->noupgrade);
	existing->parallel = pac->parallel;
	existing->color = pac->color;
	existing->verbosepkglists = pac->verbosepkglists;
	int i;
	for (i = 0; i < existing->nrepos; i++) repo_free(existing->repos[i]);
	free(existing->repos);
	existing->repos = pac->repos;
	existing->nrepos = pac->nrepos;
	pac->repos = NULL;
	pac->nrepos = 0;
	config_free(pac);
	if (config_write(dest, existing, 1) != 0) {
		error("could not write config to %s", dest);
		config_free(existing);
		return 1;
	}
	msg("read pacman config from %s into %s", pacpath, dest);
	config_free(existing);
	return 0;
}

static void print_config(config *c) {
	int i, j;
	printf("RootDir       = %s\n", c->rootdir);
	printf("DBPath        = %s\n", c->dbpath);
	for (i = 0; i < c->cachedirs.n; i++) printf("CacheDir      = %s\n", c->cachedirs.v[i]);
	printf("Architecture  = %s\n", c->arch);
	printf("ParallelDl    = %d\n", c->parallel);
	printf("aur           = %s\n", c->aur ? "true" : "false");
	printf("nix           = %s\n", c->nix ? "true" : "false");
	for (i = 0; i < c->nrepos; i++) {
		repo *r = c->repos[i];
		printf("\n[%s]\n", r->name);
		for (j = 0; j < r->servers.n; j++) printf("Server = %s\n", r->servers.v[j]);
	}
	return;
}

int cli_main(int argc, char **argv) {
	if (argc >= 2 && strcmp(argv[1], "-fp") == 0) {
		return fp_run(argc - 2, argv + 2);
	}
	cli cl;
	memset(&cl, 0, sizeof cl);
	int i;
	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];
		if (strcmp(arg, "--") == 0) {
			for (i++; i < argc; i++) strs_add(&cl.targets, argv[i]);
			break;
		}
		if (arg[0] == '-' && arg[1] == '-') {
			if (parse_long(&cl, arg, &i, argc, argv) != 0) {
				error("invalid option '%s' (use -h for help)", arg);
				strs_free(&cl.targets);
				return 1;
			}
			continue;
		}
		if (arg[0] == '-' && arg[1] != '\0') {
			if (parse_short(&cl, arg, &i, argc, argv) != 0) {
				strs_free(&cl.targets);
				return 1;
			}
			continue;
		}
		if (cl.op == 0 && cl.targets.n == 0) {
			if (word_command(&cl, arg) == 0) {
				if (cl.op == 'n' || cl.op == 'a' || cl.op == 'f') {
					if (i + 1 < argc && argv[i + 1][0] != '-') {
						cl.word = argv[++i];
					}
				}
				continue;
			}
		}
		strs_add(&cl.targets, arg);
	}
	if (cl.op == 'h') {
		print_help();
		strs_free(&cl.targets);
		return 0;
	}
	if (cl.op == 'V') {
		printf("nya %s (pacman-compatible package manager)\n", NYA_VERSION);
		strs_free(&cl.targets);
		return 0;
	}
	if (cl.op == 0) {
		error("no operation specified (use -h for help)");
		strs_free(&cl.targets);
		return 1;
	}
	if (cl.op == 'P') {
		const char *pacpath = cl.targets.n > 0 ? cl.targets.v[0] : "/etc/pacman.conf";
		char dest[4096];
		if (cl.config_path) snprintf(dest, sizeof dest, "%s", cl.config_path);
		else if (getenv("NYA_CONF") && *getenv("NYA_CONF")) snprintf(dest, sizeof dest, "%s", getenv("NYA_CONF"));
		else if (geteuid() == 0) snprintf(dest, sizeof dest, "/etc/nya.conf");
		else snprintf(dest, sizeof dest, "%s/.config/nya/nya.conf", getenv("HOME") ? getenv("HOME") : ".");
		int rc = do_read_paconfig(dest, pacpath);
		strs_free(&cl.targets);
		return rc;
	}
	config *c;
	int generated = 0;
	if (cl.config_path) c = config_load(cl.config_path, &generated);
	else c = config_discover();
	if (!c) {
		strs_free(&cl.targets);
		return 1;
	}
	(void)generated;
	if (cl.root_ov) {
		free(c->rootdir);
		c->rootdir = xstrdup(cl.root_ov);
	}
	if (cl.dbpath_ov) {
		free(c->dbpath);
		c->dbpath = xstrdup(cl.dbpath_ov);
	}
	if (cl.cachedir_ov && !strs_has(&c->cachedirs, cl.cachedir_ov)) strs_add(&c->cachedirs, cl.cachedir_ov);
	if (cl.ignore.n) {
		int j;
		for (j = 0; j < cl.ignore.n; j++) {
			if (!strs_has(&c->ignorepkg, cl.ignore.v[j])) strs_add(&c->ignorepkg, cl.ignore.v[j]);
		}
	}
	if (cl.ignoregrp.n) {
		int j;
		for (j = 0; j < cl.ignoregrp.n; j++) {
			if (!strs_has(&c->ignoregrp, cl.ignoregrp.v[j])) strs_add(&c->ignoregrp, cl.ignoregrp.v[j]);
		}
	}
	char *rooted;
	rooted = (char *)cfg_rooted(c, c->dbpath);
	if (rooted != c->dbpath) {
		free(c->dbpath);
		c->dbpath = xstrdup(rooted);
	}
	strs rooted_cachedirs;
	memset(&rooted_cachedirs, 0, sizeof rooted_cachedirs);
	int j;
	for (j = 0; j < c->cachedirs.n; j++) {
		rooted = (char *)cfg_rooted(c, c->cachedirs.v[j]);
		strs_add(&rooted_cachedirs, rooted);
	}
	strs_free(&c->cachedirs);
	c->cachedirs = rooted_cachedirs;
	rooted = (char *)cfg_rooted(c, c->nyacache);
	if (rooted != c->nyacache) {
		free(c->nyacache);
		c->nyacache = xstrdup(rooted);
	}
	if (c->logfile && *c->logfile) set_logfile(c->logfile);
	g_noconfirm = cl.noconfirm;
	g_overwrite = cl.overwrite;
	if (cl.op == 'c') {
		print_config(c);
		config_free(c);
		strs_free(&cl.targets);
		return 0;
	}
	int needs_dbs = 0;
	if (cl.op == 'Q' || cl.op == 'R' || cl.op == 'U') needs_dbs = 1;
	else if (cl.op == 'S') {
		needs_dbs = !(cl.y && !cl.u && !cl.s && cl.i == 0 && cl.cclean == 0 && cl.targets.n == 0);
	} else if (cl.op == 'a') needs_dbs = 1;
	if (needs_dbs) db_load_all(c);
	int rc = 0;
	switch (cl.op) {
	case 'Q': {
		if (cl.o) {
			if (cl.targets.n == 0) {
				error("no path specified");
				rc = 1;
			} else rc = do_owns(c, cl.targets.v[0]);
		} else if (cl.i > 0) {
			if (cl.targets.n == 0) {
				error("no package specified");
				rc = 1;
			} else rc = do_info(c, (const char **)cl.targets.v, cl.targets.n, 1, cl.i > 1);
		} else if (cl.l) {
			if (cl.targets.n == 0) {
				strs all;
				memset(&all, 0, sizeof all);
				for (j = 0; j < g_nlocal; j++) strs_add(&all, g_local[j]->name);
				rc = do_files(c, (const char **)all.v, all.n);
				strs_free(&all);
			} else rc = do_files(c, (const char **)cl.targets.v, cl.targets.n);
		} else if (cl.k > 0) {
			if (cl.targets.n == 0) {
				strs all;
				memset(&all, 0, sizeof all);
				for (j = 0; j < g_nlocal; j++) strs_add(&all, g_local[j]->name);
				rc = do_check(c, (const char **)all.v, all.n, cl.k > 1);
				strs_free(&all);
			} else rc = do_check(c, (const char **)cl.targets.v, cl.targets.n, cl.k > 1);
		} else if (cl.s) {
			rc = 0;
			do_search(c, (const char **)cl.targets.v, cl.targets.n, 1);
		} else if (cl.u) {
			rc = do_list(c, 0, 0, 0, 0, 1);
		} else {
			rc = do_list(c, cl.m, cl.d, cl.e, cl.t, 0);
		}
		break;
	}
	case 'S': {
		if (cl.cclean > 0) {
			rc = do_clean(c, cl.cclean > 1);
			break;
		}
		if (cl.s) {
			if (cl.targets.n == 0) {
				error("no search terms specified");
				rc = 1;
				break;
			}
			rc = 0;
			do_search(c, (const char **)cl.targets.v, cl.targets.n, 0);
			if (c->aur) {
				aur_search_multi(c, (const char **)cl.targets.v, cl.targets.n);
			}
			break;
		}
		if (cl.i > 0) {
			if (cl.targets.n == 0) {
				error("no package specified");
				rc = 1;
				break;
			}
			rc = do_info(c, (const char **)cl.targets.v, cl.targets.n, 0, cl.i > 1);
			break;
		}
		if (cl.y) {
			rc = refresh_dbs(c);
			if (rc != 0) break;
			reload_dbs(c);
			if (!cl.u && cl.targets.n == 0) break;
		}
		if (cl.u) {
			if (cl.targets.n > 0) {
				error("cannot upgrade and install packages in the same operation");
				rc = 1;
				break;
			}
			txn t;
			txn_init(&t, c);
			if (txn_build_upgrade(c, &t) != 0) {
				txn_free(&t);
				rc = 1;
				break;
			}
			if (cl.aurup && c->aur) {
				if (aur_update(c, &t) != 0) {
					txn_free(&t);
					rc = 1;
					break;
				}
			}
			if (t.nadd == 0 && t.nrm == 0) {
				msg("there is nothing to do");
				txn_free(&t);
				break;
			}
			rc = run_txn(c, &t, 2);
			break;
		}
		if (cl.targets.n == 0) {
			error("no targets specified (use -h for help)");
			rc = 1;
			break;
		}
		txn t;
		txn_init(&t, c);
		if (txn_build_install(c, (const char **)cl.targets.v, cl.targets.n, &t) != 0) {
			txn_free(&t);
			rc = 1;
			break;
		}
		if (cl.w) {
			if (txn_prepare(c, &t) != 0 || txn_download(c, &t) != 0) {
				txn_free(&t);
				rc = 1;
				break;
			}
			txn_free(&t);
			break;
		}
		rc = run_txn(c, &t, 0);
		break;
	}
	case 'R': {
		if (cl.targets.n == 0) {
			error("no targets specified (use -h for help)");
			rc = 1;
			break;
		}
		txn t;
		txn_init(&t, c);
		if (txn_build_remove(c, (const char **)cl.targets.v, cl.targets.n, cl.s, cl.n, cl.cclean > 0, cl.u, &t) != 0) {
			txn_free(&t);
			rc = 1;
			break;
		}
		rc = run_txn(c, &t, 1);
		break;
	}
	case 'U': {
		if (cl.targets.n == 0) {
			error("no package files specified (use -h for help)");
			rc = 1;
			break;
		}
		txn t;
		txn_init(&t, c);
		if (txn_build_install(c, (const char **)cl.targets.v, cl.targets.n, &t) != 0) {
			txn_free(&t);
			rc = 1;
			break;
		}
		rc = run_txn(c, &t, 0);
		break;
	}
	case 'n': {
		if (!cl.word) {
			error("nix operation required: search|info|update");
			rc = 1;
			break;
		}
		if (strcmp(cl.word, "search") == 0) {
			if (cl.targets.n == 0) {
				error("no search term specified");
				rc = 1;
			} else rc = nix_search(c, cl.targets.v[0]);
		} else if (strcmp(cl.word, "info") == 0) {
			if (cl.targets.n == 0) {
				error("no package specified");
				rc = 1;
			} else rc = nix_info(c, cl.targets.v[0]);
		} else if (strcmp(cl.word, "update") == 0) {
			rc = nix_update(c);
		} else {
			error("unknown nix operation '%s'", cl.word);
			rc = 1;
		}
		break;
	}
	case 'a': {
		if (!cl.word) {
			error("aur operation required: search|info|install");
			rc = 1;
			break;
		}
		if (strcmp(cl.word, "search") == 0) {
			if (cl.targets.n == 0) {
				error("no search term specified");
				rc = 1;
			} else {
			rc = aur_search_multi(c, (const char **)cl.targets.v, cl.targets.n);
		}
		} else if (strcmp(cl.word, "info") == 0) {
			if (cl.targets.n == 0) {
				error("no package specified");
				rc = 1;
			} else rc = aur_info(c, cl.targets.v[0]);
		} else if (strcmp(cl.word, "install") == 0) {
			txn t;
			txn_init(&t, c);
			int ok = 1;
			for (j = 0; j < cl.targets.n; j++) {
				if (aur_build_install(c, cl.targets.v[j], &t) != 0) {
					ok = 0;
					break;
				}
			}
			if (ok && t.nadd > 0) rc = run_txn(c, &t, 0);
			else {
				txn_free(&t);
				rc = ok ? 0 : 1;
			}
		} else {
			error("unknown aur operation '%s'", cl.word);
			rc = 1;
		}
		break;
	}
	case 'f': {
		if (!cl.word) {
			error("flatpak operation required: search|install|remove|list|update");
			rc = 1;
			break;
		}
		char **args = xcalloc(cl.targets.n + 2, sizeof *args);
		args[0] = (char *)cl.word;
		for (j = 0; j < cl.targets.n; j++) args[j + 1] = cl.targets.v[j];
		rc = fp_run(cl.targets.n + 1, args);
		free(args);
		break;
	}
	default:
		error("no operation specified (use -h for help)");
		rc = 1;
	}
	config_free(c);
	strs_free(&cl.targets);
	return rc;
}
