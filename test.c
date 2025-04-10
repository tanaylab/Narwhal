#include "narwhal.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void
assert_errno(const char* where, ...) {
    if (errno == 0)
        return;
    va_list(argp);
    va_start(argp, where);

    for (;;) {
        const char* suffix = va_arg(argp, const char*);
        if (!suffix)
            break;
        fputs(where, stderr);
        where = suffix;
    }

    perror(where);
    assert(false);
}

void
cleanup(const char* lockdir) {
    DIR* dir = opendir(lockdir);
    assert_errno("opendir(", lockdir, ")", NULL);

    char cwd[PATH_MAX];
    getcwd(cwd, sizeof(cwd));
    assert_errno("cwd", NULL);

    chdir(lockdir);
    assert_errno("chdir(", lockdir, ")", NULL);

    for (;;) {
        struct dirent* entry = readdir(dir);
        assert_errno("readdir(", lockdir, ")", NULL);

        if (entry) {
            if (*entry->d_name != '.') {
                remove(entry->d_name);
                assert_errno("remove(", lockdir, "/", entry->d_name, ")", NULL);
            }
            continue;
        }

        chdir(cwd);
        assert_errno("chdir(", cwd, ")", NULL);

        rmdir(lockdir);
        assert_errno("rmdir(", lockdir, ")", NULL);

        return;
    }
}

void
test_read_lock(const char* lockdir) {
    fprintf(stderr, "test_read_lock\n");
    const Narwhal narwhal = { .lockdir = lockdir, .spin_usec = 1000, .timeout_sec = 1 };

    narwhal_read_lock(&narwhal);
    assert_errno("narwhal_read_lock", NULL);

    narwhal_unlock(&narwhal);
    assert_errno("narwhal_unlock", NULL);
}

void
test_write_lock(const char* lockdir) {
    fprintf(stderr, "test_write_lock\n");
    const Narwhal narwhal = { .lockdir = lockdir, .spin_usec = 1000, .timeout_sec = 10 };

    narwhal_read_lock(&narwhal);
    assert_errno("narwhal_write_lock", NULL);

    narwhal_unlock(&narwhal);
    assert_errno("narwhal_unlock", NULL);
}

void
run_test(void (*function)(const char*)) {
    char template[] = "tmp.XXXXXX";
    const char* lockdir = mkdtemp(template);
    assert_errno("mkdtemp", NULL);

    (*function)(lockdir);

    cleanup(lockdir);
}

extern void
narwhal_test_pid(char* test_pid);

int
main(int argc, char* argv[]) {
    if (argc == 2 && !strcmp(argv[1], "run")) {
        run_test(test_read_lock);
        run_test(test_write_lock);
        return 0;
    }

    if (argc == 5) {
        char* lockdir = argv[1];
        char* hostname = argv[2];
        char* pid = argv[3];
        char* operation = argv[4];

        const Narwhal narwhal = { .lockdir = lockdir, .spin_usec = 1000, .timeout_sec = 10 };

        narwhal_hostname(hostname);
        narwhal_pid(pid);

        if (!strcmp(operation, "R")) {
            narwhal_read_lock(&narwhal);
            assert_errno("narwhal_read_lock", NULL);
            return 0;
        }

        if (!strcmp(operation, "W")) {
            narwhal_write_lock(&narwhal);
            assert_errno("narwhal_write_lock", NULL);
            return 0;
        }

        if (!strcmp(operation, "U")) {
            narwhal_write_lock(&narwhal);
            assert_errno("narwhal_write_lock", NULL);
            return 0;
        }
    }

    fprintf(stderr, "usage: %s run\n", argv[0]);
    fprintf(stderr, "or: %s lockfile hostname pid op\n", argv[0]);
    fprintf(stderr, "where pid is a fake one to use,\n");
    fprintf(stderr, "and op is one of:\n");
    fprintf(stderr, "  R - read lock\n");
    fprintf(stderr, "  W - write lock\n");
    fprintf(stderr, "  U - unlock\n");

    return 0;
}
