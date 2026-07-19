#include "bridge.h"
#include "cli.h"
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

    printf("[Init] LoRaHAM KISS TNC bridge\n");
    lhkt_cli_print_config(&cfg);

    ret = lhkt_bridge_run(&cfg, &stats);

    if (cfg.stats_interval > 0 || cfg.verbose) {
        lhkt_stats_print(&stats);
    }

    return ret;
}
