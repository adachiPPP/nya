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
				if (pname) printf("  Name          : %s\n", pname);
				if (version) printf("  Version       : %s\n", json_str(version));
				if (desc) printf("  Description   : %s\n", json_str(desc));
				if (license && license->type == J_STR) printf("  License       : %s\n", json_str(license));
				printf("\n");
			} else {
				printf("%snixpkgs.%s %s%s\n", col_bold(), attr,
				       version && json_str(version) ? json_str(version) : "", col_reset());
				if (desc && json_str(desc)) printf("    %s\n", json_str(desc));
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
				if (version) printf("  Version       : %s\n", json_str(version));
				if (desc) printf("  Description   : %s\n", json_str(desc));
				printf("\n");
			} else {
				printf("%snixpkgs.%s %s%s\n", col_bold(), attr,
				       version && json_str(version) ? json_str(version) : "", col_reset());
				if (desc && json_str(desc)) printf("    %s\n", json_str(desc));
			}
			count++;
		}
	}
	json_free(root);
	return count;
}

int nix_search(config *c, const char *term) {
	if (!nix_enabled(c)) return -1;
	char *argv[] = {"nix", "search", "nixpkgs", (char *)term, "--json", NULL};
	char *out;
	if (run_capture(argv, &out) != 0) {
		free(out);
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
	char *argv[] = {"nix", "search", "nixpkgs", (char *)name, "--json", NULL};
	char *out;
	if (run_capture(argv, &out) != 0) {
		free(out);
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
	char **argv = xcalloc(n + 4, sizeof *argv);
	argv[0] = "nix";
	argv[1] = "search";
	argv[2] = "nixpkgs";
	int i;
	for (i = 0; i < n; i++) argv[i + 3] = (char *)terms[i];
	argv[n + 3] = "--json";
	argv[n + 4] = NULL;
	char *out;
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
