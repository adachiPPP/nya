#include "nya.h"

static int nix_available(void) {
	return access("/usr/bin/nix", X_OK) == 0 || access("/bin/nix", X_OK) == 0 ||
	       access("/usr/local/bin/nix", X_OK) == 0;
}

static int nix_enabled(config *c) {
	if (!c->nix) {
		error("nix support is disabled (set 'nix = true' in %s)", c->path);
		return 0;
	}
	if (!nix_available()) {
		error("the 'nix' package manager is not installed (install it with 'nya install nix')");
		return 0;
	}
	return 1;
}

static int print_nix_results(const char *json_data, int want_all) {
	json *root = json_parse(json_data, strlen(json_data));
	if (!root) return -1;
	int count = 0;
	if (root->type == J_OBJ) {
		json *n;
		for (n = root->child; n; n = n->next) {
			const char *attr = n->key;
			json *name = json_get(n, "name");
			json *version = json_get(n, "version");
			json *desc = json_get(n, "description");
			json *license = json_get(n, "license");
			const char *pname = name ? json_str(name) : NULL;
			if (!pname) pname = attr;
			if (want_all) {
				printf("%snixpkgs.%s%s\n", col_bold(), attr, col_reset());
				if (pname) printf("  %sName%s          : %s\n", col_bold(), col_reset(), pname);
				if (version) printf("  %sVersion%s       : %s%s%s\n", col_bold(), col_reset(), col_green(), json_str(version), col_reset());
				if (desc) printf("  %sDescription%s   : %s\n", col_bold(), col_reset(), json_str(desc));
				if (license && license->type == J_STR) printf("  %sLicense%s       : %s\n", col_bold(), col_reset(), json_str(license));
				printf("\n");
			} else {
				printf("%snixpkgs.%s %s%s%s%s\n", col_bold(), attr, col_green(), version && json_str(version) ? json_str(version) : "", col_reset(), col_reset());
				if (desc && json_str(desc)) printf("    %s%s%s\n", col_cyan(), json_str(desc), col_reset());
			}
			count++;
		}
	} else if (root->type == J_ARR) {
		json *n;
		for (n = root->child; n; n = n->next) {
			const char *attr = json_str(json_get(n, "attr"));
			json *version = json_get(n, "version");
			json *desc = json_get(n, "description");
			if (!attr) attr = json_str(json_get(n, "name"));
			if (!attr) continue;
			if (want_all) {
				printf("%snixpkgs.%s%s\n", col_bold(), attr, col_reset());
				if (version) printf("  %sVersion%s       : %s%s%s\n", col_bold(), col_reset(), col_green(), json_str(version), col_reset());
				if (desc) printf("  %sDescription%s   : %s\n", col_bold(), col_reset(), json_str(desc));
				printf("\n");
			} else {
				printf("%snixpkgs.%s %s%s%s%s\n", col_bold(), attr, col_green(), version && json_str(version) ? json_str(version) : "", col_reset(), col_reset());
				if (desc && json_str(desc)) printf("    %s%s%s\n", col_cyan(), json_str(desc), col_reset());
			}
			count++;
		}
	}
	json_free(root);
	return count;
}

int nix_search(config *c, const char *term) {
	if (!nix_enabled(c)) return -1;
	char *argv[] = {"nix", "--extra-experimental-features", "nix-command flakes", "search", "nixpkgs", (char *)term, "--json", NULL};
	char *out = NULL;
	if (run_capture(argv, &out) != 0) {
		free(out);
		if (strstr(out, "big-lock") || strstr(out, "Permission denied")) {
			error("nix search failed - nix daemon not running or nix not initialized");
			error("try: doas systemctl start nix-daemon");
			error("or: doas nix-channel --add https://nixos.org/channels/nixpkgs-unstable nixpkgs && doas nix-channel --update");
			return -1;
		}
		error("nix search failed (is nixpkgs available to evaluate?)");
		return -1;
	}
	int count = print_nix_results(out, 0);
	free(out);
	if (count < 0) {
		error("could not parse nix search output");
		return -1;
	}
	if (count == 0) msg("no nix packages found matching '%s'", term);
	return 0;
}

int nix_info(config *c, const char *name) {
	if (!nix_enabled(c)) return -1;
	char *argv[] = {"nix", "--extra-experimental-features", "nix-command flakes", "search", "nixpkgs", (char *)name, "--json", NULL};
	char *out = NULL;
	if (run_capture(argv, &out) != 0) {
		free(out);
		if (strstr(out, "big-lock") || strstr(out, "Permission denied")) {
			error("nix search failed - nix daemon not running or nix not initialized");
			error("try: doas systemctl start nix-daemon");
			error("or: doas nix-channel --add https://nixos.org/channels/nixpkgs-unstable nixpkgs && doas nix-channel --update");
			return -1;
		}
		error("nix search failed (is nixpkgs available to evaluate?)");
		return -1;
	}
	int count = print_nix_results(out, 1);
	free(out);
	if (count < 0) {
		error("could not parse nix search output");
		return -1;
	}
	if (count == 0) {
		error("no nix packages found matching '%s'", name);
		return -1;
	}
	return 0;
}

int nix_search_any(config *c, const char **terms, int n) {
	(void)c;
	if (!nix_available()) return 0;
	int argc = n + 6;  /* nix + 2 flags + search + nixpkgs + terms + json + null */
	char **argv = xcalloc(argc, sizeof *argv);
	argv[0] = "nix";
	argv[1] = "--extra-experimental-features";
	argv[2] = "nix-command flakes";
	argv[3] = "search";
	argv[4] = "nixpkgs";
	int i;
	for (i = 0; i < n; i++) argv[i + 5] = (char *)terms[i];
	argv[n + 5] = "--json";
	argv[n + 6] = NULL;
	char *out = NULL;
	if (run_capture_quiet(argv, &out) != 0) {
		free(argv);
		free(out);
		return 0;
	}
	free(argv);
	int count = print_nix_results(out, 0);
	free(out);
	return count < 0 ? 0 : count;
}

int nix_update(config *c) {
	if (!nix_enabled(c)) return -1;
	info("Updating nixpkgs channel...");
	char *argv[] = {"nix-channel", "--update", NULL};
	int rc = run_cmd(argv);
	if (rc != 0) {
		warn("nix-channel --update failed (nix search evaluates the latest nixpkgs from the flake registry regardless)");
	}
	return rc;
}

int nix_install(config *c, const char *name) {
	if (!nix_enabled(c)) return -1;
	info("installing %s via nix...", name);
	char *argv[] = {"nix", "profile", "install", (char *)name, NULL};
	int rc = run_cmd(argv);
	if (rc != 0) {
		error("nix install failed for %s", name);
		return -1;
	}
	msg("installed %s via nix", name);
	return 0;
}
