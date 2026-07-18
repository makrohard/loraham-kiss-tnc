#include "cli.h"
#include "config.h"
#include "version.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int argc_of(char **argv)
{
    int argc = 0;

    while (argv[argc]) {
        argc++;
    }

    return argc;
}

static void test_rx_only_and_port(void)
{
    lhkt_config_t cfg;
    char *argv[] = {
        "test_cli",
        "--rx-only",
        "--kiss-port", "9001",
        "--verbose",
        NULL
    };

    lhkt_config_defaults(&cfg);

    assert(lhkt_cli_apply(argc_of(argv), argv, &cfg) == LHKT_OK);
    assert(cfg.rx_only == 1);
    assert(cfg.kiss_port == 9001);
    assert(cfg.verbose == 1);
}

static void test_config_and_cli_override(void)
{
    const char *path = "/tmp/lhkt_cli_test.conf";
    FILE *fp;
    lhkt_config_t cfg;
    char *argv[] = {
        "test_cli",
        "--config", "/tmp/lhkt_cli_test.conf",
        "--kiss-port", "9005",
        "--data-socket", "/tmp/cli_lora_framed.sock",
        "--rx-only",
        NULL
    };

    fp = fopen(path, "w");
    assert(fp != NULL);
    fprintf(fp, "kiss_port = 8002\n");
    fprintf(fp, "data_socket = /tmp/config_lora.sock\n");
    fprintf(fp, "rx_only = false\n");
    fclose(fp);

    lhkt_config_defaults(&cfg);

    assert(lhkt_cli_apply(argc_of(argv), argv, &cfg) == LHKT_OK);
    assert(cfg.kiss_port == 9005);
    assert(strcmp(cfg.data_socket, "/tmp/cli_lora_framed.sock") == 0);
    assert(cfg.rx_only == 1);

    unlink(path);
}


static void test_timing_options(void)
{
    lhkt_config_t cfg;
    char *argv[] = {
        "test_cli",
        "--tx-settle-ms", "250",
        "--tx-return-ms", "750",
        NULL
    };

    lhkt_config_defaults(&cfg);

    assert(lhkt_cli_apply(argc_of(argv), argv, &cfg) == LHKT_OK);
    assert(cfg.tx_settle_ms == 250);
    assert(cfg.tx_return_ms == 750);
}

static void test_queue_policy_options(void)
{
    lhkt_config_t cfg;
    char *argv[] = {
        "test_cli",
        "--tx-busy-timeout-ms", "120500",
        "--tx-queue-len", "5",
        "--tx-packet-ttl-ms", "181000",
        NULL
    };

    lhkt_config_defaults(&cfg);

    assert(lhkt_cli_apply(argc_of(argv), argv, &cfg) == LHKT_OK);
    assert(cfg.tx_busy_timeout_ms == 120500);
    assert(cfg.tx_queue_len == 5);
    assert(cfg.tx_packet_ttl_ms == 181000);
}

static void test_invalid_timing_option(void)
{
    lhkt_config_t cfg;
    char *argv[] = {
        "test_cli",
        "--tx-return-ms", "60001",
        NULL
    };

    lhkt_config_defaults(&cfg);

    assert(lhkt_cli_apply(argc_of(argv), argv, &cfg) == LHKT_ERR_FORMAT);
}

static void test_invalid_double_option(void)
{
    lhkt_config_t cfg;
    char *argv[] = {
        "test_cli",
        "--rx-freq", "nan",
        NULL
    };

    lhkt_config_defaults(&cfg);

    assert(lhkt_cli_apply(argc_of(argv), argv, &cfg) == LHKT_ERR_FORMAT);
}

static void test_invalid_port(void)
{
    lhkt_config_t cfg;
    char *argv[] = {
        "test_cli",
        "--kiss-port", "0",
        NULL
    };

    lhkt_config_defaults(&cfg);

    assert(lhkt_cli_apply(argc_of(argv), argv, &cfg) == LHKT_ERR_FORMAT);
}

static void test_missing_config_file(void)
{
    lhkt_config_t cfg;
    char *argv[] = {
        "test_cli",
        "--config", "/tmp/lhkt_missing_config_file.conf",
        NULL
    };

    unlink("/tmp/lhkt_missing_config_file.conf");
    lhkt_config_defaults(&cfg);

    assert(lhkt_cli_apply(argc_of(argv), argv, &cfg) != LHKT_OK);
}


static int run_cli_child_capture(char **argv, char *out, size_t out_size)
{
    int pipefd[2];
    pid_t pid;
    int status;
    size_t used = 0;

    assert(out != NULL);
    assert(out_size > 0);

    out[0] = '\0';

    /*
     * Flush inherited stdio buffers before fork(), then read the whole pipe.
     * Otherwise buffered parent output may be flushed by the child before the
     * version string and a single read() may miss the expected text.
     */
    fflush(NULL);

    assert(pipe(pipefd) == 0);

    pid = fork();
    assert(pid >= 0);

    if (pid == 0) {
        lhkt_config_t cfg;

        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);

        lhkt_config_defaults(&cfg);
        lhkt_cli_apply(argc_of(argv), argv, &cfg);

        fflush(NULL);
        _exit(1);
    }

    close(pipefd[1]);

    for (;;) {
        char buf[256];
        ssize_t n = read(pipefd[0], buf, sizeof(buf));

        if (n > 0) {
            size_t copy = (size_t)n;

            if (used + copy >= out_size)
                copy = out_size - 1 - used;

            if (copy > 0) {
                memcpy(out + used, buf, copy);
                used += copy;
                out[used] = '\0';
            }

            continue;
        }

        if (n == 0)
            break;

        break;
    }

    close(pipefd[0]);

    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status));

    return WEXITSTATUS(status);
}



static void test_version_exits_zero_and_prints_version(void)
{
    char out[4096];
    char *argv[] = {
        "test_cli",
        "--version",
        NULL
    };

    assert(run_cli_child_capture(argv, out, sizeof(out)) == 0);
    assert(strstr(out, LHKT_VERSION_TEXT) != NULL);
    assert(strstr(out, LHKT_VERSION) != NULL);
}

static void test_help_exits_zero(void)
{
    pid_t pid;
    int status;
    char *argv[] = {
        "test_cli",
        "--help",
        NULL
    };

    pid = fork();
    assert(pid >= 0);

    if (pid == 0) {
        lhkt_config_t cfg;

        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);

        lhkt_config_defaults(&cfg);
        lhkt_cli_apply(argc_of(argv), argv, &cfg);

        _exit(1);
    }

    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);
}

static void test_bind_cidr(void)
{
    lhkt_config_t cfg;

    /* default = loopback /32, listen loopback */
    lhkt_config_defaults(&cfg);
    assert(cfg.bind_prefix == 32);
    assert(cfg.bind_net == 0x7F000001u);          /* 127.0.0.1 */
    assert(strcmp(cfg.kiss_host, "127.0.0.1") == 0);

    /* --bind a LAN CIDR: network masked, listen 0.0.0.0 */
    {
        char *argv[] = { "test_cli", "--bind", "192.168.178.0/24", NULL };
        lhkt_config_defaults(&cfg);
        assert(lhkt_cli_apply(argc_of(argv), argv, &cfg) == LHKT_OK);
        assert(cfg.bind_prefix == 24);
        assert(cfg.bind_net == 0xC0A8B200u);       /* 192.168.178.0 */
        assert(strcmp(cfg.bind_spec, "192.168.178.0/24") == 0);
        assert(strcmp(cfg.kiss_host, "0.0.0.0") == 0);
    }

    /* host bits are masked off */
    {
        char *argv[] = { "test_cli", "--bind", "192.168.178.55/24", NULL };
        lhkt_config_defaults(&cfg);
        assert(lhkt_cli_apply(argc_of(argv), argv, &cfg) == LHKT_OK);
        assert(cfg.bind_net == 0xC0A8B200u);       /* .55 masked to .0 */
    }

    /* plain IP = /32 */
    {
        char *argv[] = { "test_cli", "--bind", "10.0.0.5", NULL };
        lhkt_config_defaults(&cfg);
        assert(lhkt_cli_apply(argc_of(argv), argv, &cfg) == LHKT_OK);
        assert(cfg.bind_prefix == 32);
        assert(cfg.bind_net == 0x0A000005u);
        assert(strcmp(cfg.kiss_host, "0.0.0.0") == 0);
    }

    /* 0.0.0.0/0 = any, listen all */
    {
        char *argv[] = { "test_cli", "--bind", "0.0.0.0/0", NULL };
        lhkt_config_defaults(&cfg);
        assert(lhkt_cli_apply(argc_of(argv), argv, &cfg) == LHKT_OK);
        assert(cfg.bind_prefix == 0);
        assert(strcmp(cfg.kiss_host, "0.0.0.0") == 0);
    }

    /* loopback /8 stays loopback-listen */
    {
        char *argv[] = { "test_cli", "--bind", "127.0.0.0/8", NULL };
        lhkt_config_defaults(&cfg);
        assert(lhkt_cli_apply(argc_of(argv), argv, &cfg) == LHKT_OK);
        assert(strcmp(cfg.kiss_host, "127.0.0.1") == 0);
    }

    /* invalid forms rejected */
    {
        char *bad1[] = { "test_cli", "--bind", "bogus", NULL };
        char *bad2[] = { "test_cli", "--bind", "192.168.0.0/33", NULL };
        char *bad3[] = { "test_cli", "--bind", "192.168.0.0/-1", NULL };
        char *bad4[] = { "test_cli", "--bind", "1.2.3.4/", NULL };   /* empty prefix */
        lhkt_config_defaults(&cfg);
        assert(lhkt_cli_apply(argc_of(bad1), bad1, &cfg) != LHKT_OK);
        lhkt_config_defaults(&cfg);
        assert(lhkt_cli_apply(argc_of(bad2), bad2, &cfg) != LHKT_OK);
        lhkt_config_defaults(&cfg);
        assert(lhkt_cli_apply(argc_of(bad3), bad3, &cfg) != LHKT_OK);
        lhkt_config_defaults(&cfg);
        assert(lhkt_cli_apply(argc_of(bad4), bad4, &cfg) != LHKT_OK);
    }
}

static void test_bind_membership(void)
{
    /* the accept-time filter helper */
    assert(lhkt_ipv4_in_cidr(0xC0A8B237u, 0xC0A8B200u, 24) == 1); /* .55 in /24 */
    assert(lhkt_ipv4_in_cidr(0xC0A8B301u, 0xC0A8B200u, 24) == 0); /* .179.1 out */
    assert(lhkt_ipv4_in_cidr(0x7F000001u, 0x7F000001u, 32) == 1); /* exact /32 */
    assert(lhkt_ipv4_in_cidr(0x08080808u, 0u, 0) == 1);           /* /0 any */
    assert(lhkt_ipv4_in_cidr(0x0A000006u, 0x0A000005u, 32) == 0); /* /32 mismatch */
}

static void test_kiss_host_override(void)
{
    lhkt_config_t cfg;
    /* explicit --kiss-host overrides the derived listen address; the source
     * allow-list still comes from --bind. */
    char *argv[] = { "test_cli", "--bind", "0.0.0.0/0",
                     "--kiss-host", "192.168.1.5", NULL };
    lhkt_config_defaults(&cfg);
    assert(lhkt_cli_apply(argc_of(argv), argv, &cfg) == LHKT_OK);
    assert(strcmp(cfg.kiss_host, "192.168.1.5") == 0);   /* explicit listen */
    assert(cfg.bind_prefix == 0);                        /* allow any */
}

int main(void)
{
    test_bind_cidr();
    test_bind_membership();
    test_kiss_host_override();
    test_rx_only_and_port();
    test_config_and_cli_override();
    test_timing_options();
    test_queue_policy_options();
    test_invalid_timing_option();
    test_invalid_double_option();
    test_invalid_port();
    test_missing_config_file();
    test_help_exits_zero();
    test_version_exits_zero_and_prints_version();

    puts("test_cli: OK");
    return 0;
}
