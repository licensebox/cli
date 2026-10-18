#ifndef LICENSEBOX_CLI_DISPATCH_H
#define LICENSEBOX_CLI_DISPATCH_H
#include <licensebox_json.h>
/* Shared production dispatch; the test adapter supplies trusted callbacks only. */
lb_result cli_dispatch(lb_client *, const char *operation, const char *arguments, char **output);
#endif
