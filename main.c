#include "bridge.h"
#include "cli.h"
#include "rflog.h"
#include "loraham_kiss_tnc.h"

#include <signal.h>
#include <stdio.h>

/*
 * Program entry point.
 * Detailed CLI handling and bridge logic live in separate modules.
 */

int main(int argc, char **argv)
{
    lhkt_config_t cfg;
    lhkt_stats_t stats;
    int ret;

    /* Line-buffered so logs arrive promptly under journald/pipes (stdout is
     * fully buffered when not a TTY) and are not lost on an abnormal exit. */
    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGPIPE, SIG_IGN);

    lhkt_config_defaults(&cfg);
    lhkt_stats_init(&stats);

    ret = lhkt_cli_apply(argc, argv, &cfg);
    if (ret != LHKT_OK) {
        return 1;
    }

    lhkt_log_set_verbose(cfg.verbose);

    /* An RF log the operator asked for must exist before the radio path runs:
     * "on" without a path, or an unopenable path, is a startup error, never a
     * silently absent log. */
    if (cfg.rf_log) {
        if (cfg.rf_log_path[0] == '\0') {
            fprintf(stderr, "[CFG] --rflog on needs --rflog-path <absolute path>\n");
            return 1;
        }
        if (lhkt_rflog_open(cfg.rf_log_path) != LHKT_OK) {
            fprintf(stderr, "[CFG] cannot open RF log %s\n", cfg.rf_log_path);
            return 1;
        }
    }

    printf("[Init] LoRaHAM KISS TNC bridge\n");
    lhkt_cli_print_config(&cfg);

    ret = lhkt_bridge_run(&cfg, &stats);
    lhkt_rflog_close();

    if (cfg.stats_interval > 0 || cfg.verbose) {
        lhkt_stats_print(&stats);
    }

    return ret;
}
