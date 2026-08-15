#include "nya.h"
#include <curl/curl.h>

int main(int argc, char **argv) {
	g_color = isatty(STDOUT_FILENO);
	curl_global_init(CURL_GLOBAL_DEFAULT);
	int rc = cli_main(argc, argv);
	curl_global_cleanup();
	return rc;
}
