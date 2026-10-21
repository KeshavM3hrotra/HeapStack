/*
 * HeapTrace (C) - command-line front end.
 *
 *   heaptrace_c                      interactive console
 *   heaptrace_c repl    [options]    interactive console
 *   heaptrace_c run     <trace> [options]
 *   heaptrace_c compare <trace> [options]
 *   heaptrace_c gen     [options]    print a generated trace
 *   heaptrace_c help
 */
#include "heaptrace.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEFAULT_LOG "heaptrace_session.log"
#define DEFAULT_HEAP 1000

typedef struct {
    MemSize heap;
    const char *strategy;
    const char *log_path;
    int use_log;
    int brief;
    long long ops;
    long long seed;
} Options;

static void options_init(Options *o) {
    o->heap = DEFAULT_HEAP;
    o->strategy = "first-fit";
    o->log_path = DEFAULT_LOG;
    o->use_log = 1;
    o->brief = 0;
    o->ops = 40;
    o->seed = 1;
}

static void print_usage(FILE *os) {
    fputs("HeapTrace - first-fit vs. best-fit memory allocator simulator for single-server VM placement (C)\n"
          "\n"
          "Usage:\n"
          "  heaptrace_c                          interactive console (same as 'repl')\n"
          "  heaptrace_c repl    [options]        interactive console\n"
          "  heaptrace_c run     <trace> [options]  replay a trace file under one strategy\n"
          "  heaptrace_c compare <trace> [options]  replay a trace under first-, best- and worst-fit\n"
          "  heaptrace_c gen     [options]        print a pseudo-random trace to standard output\n"
          "  heaptrace_c help                     show this text\n"
          "\n"
          "Options:\n"
          "  --heap <mb>        server memory in MB (default 1000; a HEAP line in a trace overrides it)\n"
          "  --strategy <name>  first | best | worst   (run / repl, default first)\n"
          "  --log <file>       session log file (default heaptrace_session.log)\n"
          "  --no-log           do not write a session log\n"
          "  --brief            run: omit the statistics line after each operation\n"
          "  --ops <n>          gen: number of operations, 1..1000000 (default 40)\n"
          "  --seed <n>         gen: random seed (default 1)\n"
          "\n"
          "Trace commands: HEAP, CREATE, TERMINATE, STRATEGY, SNAPSHOT, STATS, COMPARE, HELP, QUIT\n",
          os);
}

/* Returns 1 on success; on failure returns 0 and fills `error`. */
static int parse_options(int argc, char **argv, int first, Options *o, char *error, size_t error_len) {
    int i;
    for (i = first; i < argc; ++i) {
        const char *a = argv[i];
        int needs_value = strcmp(a, "--heap") == 0 || strcmp(a, "--strategy") == 0 || strcmp(a, "--log") == 0 ||
                          strcmp(a, "--ops") == 0 || strcmp(a, "--seed") == 0;
        if (needs_value && i + 1 >= argc) {
            snprintf(error, error_len, "option %s needs a value", a);
            return 0;
        }
        if (strcmp(a, "--heap") == 0) {
            ++i;
            if (!ht_parse_size(argv[i], &o->heap)) {
                snprintf(error, error_len, "invalid --heap value '%s' (whole number of MB from 1 to 1000000000000)",
                         argv[i]);
                return 0;
            }
        } else if (strcmp(a, "--strategy") == 0) {
            ++i;
            o->strategy = argv[i];
            if (ht_strategy_by_name(o->strategy) == NULL) {
                snprintf(error, error_len, "unknown strategy '%s' (use first, best or worst)", o->strategy);
                return 0;
            }
        } else if (strcmp(a, "--log") == 0) {
            o->log_path = argv[++i];
            o->use_log = 1;
        } else if (strcmp(a, "--no-log") == 0) {
            o->use_log = 0;
        } else if (strcmp(a, "--brief") == 0) {
            o->brief = 1;
        } else if (strcmp(a, "--ops") == 0) {
            ++i;
            if (!ht_parse_integer(argv[i], 1, 1000000, &o->ops)) {
                snprintf(error, error_len, "invalid --ops value '%s' (1 to 1000000)", argv[i]);
                return 0;
            }
        } else if (strcmp(a, "--seed") == 0) {
            ++i;
            if (!ht_parse_integer(argv[i], 0, 999999999999999999LL, &o->seed)) {
                snprintf(error, error_len, "invalid --seed value '%s' (a non-negative whole number)", argv[i]);
                return 0;
            }
        } else {
            snprintf(error, error_len, "unknown option '%s'", a);
            return 0;
        }
    }
    return 1;
}

/* Reads one line of any length (without the newline). NULL at end of input. Caller frees. */
static char *read_line(FILE *fp) {
    size_t cap = 128, len = 0;
    char *buf = (char *)malloc(cap);
    int c;
    if (buf == NULL) return NULL;
    while ((c = fgetc(fp)) != EOF) {
        if (c == '\n') break;
        if (len + 2 > cap) {
            char *grown = (char *)realloc(buf, cap * 2);
            if (grown == NULL) {
                free(buf);
                return NULL;
            }
            buf = grown;
            cap *= 2;
        }
        buf[len++] = (char)c;
    }
    if (c == EOF && len == 0) {
        free(buf);
        return NULL;
    }
    buf[len] = '\0';
    return buf;
}

/* Reads all lines of a text file (handles CRLF and a UTF-8 byte-order mark). */
static int read_lines(const char *path, StrList *lines) {
    FILE *fp = fopen(path, "r");
    char *line;
    int first = 1;
    if (fp == NULL) return 0;
    while ((line = read_line(fp)) != NULL) {
        const char *text = line;
        if (first && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF) {
            text = line + 3;
        }
        first = 0;
        strlist_push(lines, text);
        free(line);
    }
    fclose(fp);
    return 1;
}

static void open_log(SessionLog *log, const Options *o, const char *mode, const char *detail) {
    char stamp[64] = "unknown time";
    time_t now;
    const struct tm *local;
    char *line;
    if (!o->use_log) return;
    if (!log_open(log, o->log_path)) {
        fprintf(stderr, "warning: cannot open log file '%s'; continuing without a session log\n", o->log_path);
        return;
    }
    now = time(NULL);
    local = localtime(&now);
    if (local != NULL) strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", local);
    log_write(log, "HeapTrace session log (C implementation)");
    line = ht_asprintf("started: %s", stamp);
    if (line != NULL) log_write(log, line);
    free(line);
    line = ht_asprintf("mode: %s%s%s", mode, detail[0] != '\0' ? " " : "", detail);
    if (line != NULL) log_write(log, line);
    free(line);
}

static void announce_log(const SessionLog *log) {
    if (log->fp != NULL) printf("Session log written to %s\n", log->path);
}

static int cmd_run(const char *path, const Options *o) {
    StrList lines;
    SessionLog log;
    Processor *p;
    ProcessorOptions po = processor_default_options();
    size_t i;

    memset(&lines, 0, sizeof(lines));
    memset(&log, 0, sizeof(log));
    if (!read_lines(path, &lines)) {
        fprintf(stderr, "error: cannot open trace file '%s'\n", path);
        return 1;
    }
    open_log(&log, o, "run", path);
    po.print_stats = !o->brief;
    p = processor_create(o->heap, o->strategy, stdout, o->use_log ? &log : NULL, po);
    if (p == NULL) {
        fprintf(stderr, "fatal error: cannot initialise the simulator\n");
        strlist_free(&lines);
        log_close(&log);
        return 1;
    }
    printf("HeapTrace run: %s | strategy %s | default heap %lld MB\n", path, p->heap->strategy->name, o->heap);
    for (i = 0; i < lines.count; ++i) {
        if (processor_execute(p, lines.items[i], (int)i + 1) == CMD_QUIT) break;
    }
    processor_finish(p);
    announce_log(&log);
    processor_destroy(p);
    strlist_free(&lines);
    log_close(&log);
    return 0;
}

static int cmd_compare(const char *path, const Options *o) {
    StrList lines, text;
    SessionLog log;
    StrategyRun runs[3];
    size_t i;
    int ok;

    memset(&lines, 0, sizeof(lines));
    memset(&text, 0, sizeof(text));
    memset(&log, 0, sizeof(log));
    if (!read_lines(path, &lines)) {
        fprintf(stderr, "error: cannot open trace file '%s'\n", path);
        return 1;
    }
    open_log(&log, o, "compare", path);
    ok = compare_run(&lines, o->heap, o->use_log ? &log : NULL, runs);
    if (!ok) {
        fprintf(stderr, "fatal error: out of memory\n");
        strlist_free(&lines);
        log_close(&log);
        return 1;
    }
    if (!report_comparison_lines(path, runs, 3, &text)) {
        fprintf(stderr, "fatal error: out of memory\n");
        ok = 0;
    }
    for (i = 0; i < text.count; ++i) {
        puts(text.items[i]);
        log_write(&log, text.items[i]);
    }
    announce_log(&log);
    for (i = 0; i < 3; ++i) strategy_run_free(&runs[i]);
    strlist_free(&text);
    strlist_free(&lines);
    log_close(&log);
    return ok ? 0 : 1;
}

static int cmd_repl(const Options *o) {
    SessionLog log;
    Processor *p;
    ProcessorOptions po = processor_default_options();
    char *line;
    int line_no = 0;

    memset(&log, 0, sizeof(log));
    open_log(&log, o, "interactive console", "");
    p = processor_create(o->heap, o->strategy, stdout, o->use_log ? &log : NULL, po);
    if (p == NULL) {
        fprintf(stderr, "fatal error: cannot initialise the simulator\n");
        log_close(&log);
        return 1;
    }
    printf("HeapTrace - single-server VM placement simulator (C)\n"
           "Server memory: %lld MB, strategy: %s. Type HELP for commands, QUIT to exit.\n",
           o->heap, p->heap->strategy->name);
    for (;;) {
        fputs("heaptrace> ", stdout);
        fflush(stdout);
        line = read_line(stdin);
        if (line == NULL) break; /* end of input */
        ++line_no;
        if (processor_execute(p, line, line_no) == CMD_QUIT) {
            free(line);
            break;
        }
        free(line);
    }
    processor_finish(p);
    announce_log(&log);
    processor_destroy(p);
    log_close(&log);
    return 0;
}

static int cmd_gen(const Options *o) {
    StrList lines;
    size_t i;
    memset(&lines, 0, sizeof(lines));
    if (!generate_trace(o->heap, (int)o->ops, (unsigned long long)o->seed, &lines)) {
        fprintf(stderr, "fatal error: out of memory\n");
        strlist_free(&lines);
        return 1;
    }
    for (i = 0; i < lines.count; ++i) puts(lines.items[i]);
    strlist_free(&lines);
    return 0;
}

int main(int argc, char **argv) {
    Options opt;
    char error[256];
    char cmd[16];

    options_init(&opt);
    error[0] = '\0';
    if (argc < 2) return cmd_repl(&opt);

    strncpy(cmd, argv[1], sizeof(cmd) - 1);
    cmd[sizeof(cmd) - 1] = '\0';
    ht_to_lower(cmd);
    if (strlen(argv[1]) >= sizeof(cmd)) cmd[0] = '\0'; /* too long to be any known command */

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0 || strcmp(cmd, "/?") == 0) {
        print_usage(stdout);
        return 0;
    }
    if (strcmp(cmd, "run") == 0 || strcmp(cmd, "compare") == 0) {
        if (argc < 3 || strncmp(argv[2], "--", 2) == 0) {
            fprintf(stderr, "error: '%s' needs a trace file\n\n", cmd);
            print_usage(stderr);
            return 2;
        }
        if (!parse_options(argc, argv, 3, &opt, error, sizeof(error))) {
            fprintf(stderr, "error: %s\n\n", error);
            print_usage(stderr);
            return 2;
        }
        return strcmp(cmd, "run") == 0 ? cmd_run(argv[2], &opt) : cmd_compare(argv[2], &opt);
    }
    if (strcmp(cmd, "repl") == 0 || strcmp(cmd, "gen") == 0) {
        if (!parse_options(argc, argv, 2, &opt, error, sizeof(error))) {
            fprintf(stderr, "error: %s\n\n", error);
            print_usage(stderr);
            return 2;
        }
        return strcmp(cmd, "repl") == 0 ? cmd_repl(&opt) : cmd_gen(&opt);
    }

    fprintf(stderr, "error: unknown command '%s'\n\n", argv[1]);
    print_usage(stderr);
    return 2;
}
