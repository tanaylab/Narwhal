#include "narwhal.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef LOG
#    define DEBUG_AT(X) fprintf(stderr, "%s:%d: %s (errno: %d)\n", __FILE__, __LINE__, X, errno)
#    define DEBUG_EXP(X, F)                                          \
        {                                                            \
            fprintf(stderr, "%s:%d: %s = ", __FILE__, __LINE__, #X); \
            fprintf(stderr, F, X);                                   \
            fprintf(stderr, " (errno : %d)\n", errno);               \
        }
#else
#    define DEBUG_AT(X)
#    define DEBUG_EXP(X, F)
#endif

// The host name running this process, with spaces replaced by _ characters.
static char* host_name;

// Replace all spaces in the host_name with _ characters to allow for simple parsing of the state file.
static void
patch_host_name() {
    for (char* p = host_name; *p; p++) {
        if (*p == ' ')
            *p = '_';
    }
    DEBUG_EXP(host_name, "%s");
}

// Implement narwhal_hostname. See the header file.
void
narwhal_hostname(const char* hostname) {
    assert(hostname[0]);
    if (host_name) {
        free(host_name);
    }
    host_name = strdup(hostname);
    patch_host_name();
}

// Initialize the host name.
static void
init_host_name() {
    if (host_name) {
        return;
    }
    static char host_name_buffer[1024];
    gethostname(host_name_buffer, sizeof(host_name_buffer));
    host_name_buffer[sizeof(host_name_buffer) - 1] = '\0';
    host_name = strdup(host_name_buffer);
    patch_host_name();
}

// The current process identifier as a string.
static char* pid;

// Implement narwhal_pid. See the header file.
void
narwhal_pid(const char* new_pid) {
    if (pid) {
        free(pid);
    }
    pid = strdup(new_pid);
    assert(pid[0]);
    DEBUG_EXP(pid, "%s");
}

// Initialize the current process identifier.
static void
init_pid() {
    if (pid) {
        return;
    }
    pid = calloc(32, 1);
    sprintf(pid, "%lld", (long long)(getpid()));
    DEBUG_EXP(pid, "%s");
}

// Reusable path buffers.
static char* state_path;
static char* lockfile_path;
static char* private_path;

// Concatenate path name parts into one of the reused path names.
const char*
format_path(char** pathp, ...) {
    va_list(argp);
    va_start(argp, pathp);

    int size = 0;
    for (;;) {
        const char* suffix = va_arg(argp, const char*);
        if (!suffix) {
            DEBUG_EXP(*pathp, "%s");
            return *pathp;
        }
        int size_before = size;
        size += strlen(suffix);
        *pathp = realloc(*pathp, size + 1);
        strcpy(*pathp + size_before, suffix);
    }
}

// The state of a single client, parsed from the state file.
typedef struct {
    bool is_write_lock;
    bool is_granted;
    long long time;
    const char* host_name;
    const char* pid;
} ClientState;

// Reuse buffers for parsed client states.
static ClientState* client_states;
static int n_client_states = 0;

// A state of (some) client that has a granted lock.
static ClientState* granted_state = NULL;

// Whether we changed the client states since parsing them from the state file.
static bool client_states_changed = false;

// Reuse buffer for the text of the state file.
static char* state_text;

// Did we initialize the reusable buffers?
static bool did_init = false;

static void
init() {
    if (did_init)
        return;

    DEBUG_AT("narwhal_init");
    did_init = true;

    init_host_name();
    init_pid();
    state_path = malloc(1024);
    lockfile_path = malloc(1024);
    private_path = malloc(1024);
    state_text = calloc(1024, 1);
    client_states = malloc(1024);
}

// Load the state file into the state_text. Places an extra \0 after the final one to make parsing easier (no valid
// field is empty).
static int
load_state_text(const Narwhal* narwhal) {
    DEBUG_AT("load_state_text");
    int state_fd = open(format_path(&state_path, narwhal->lockdir, "/state", NULL), O_CREAT | O_RDONLY, 0777);
    if (state_fd < 0) {
        if (errno != ENOENT)
            return -1;
        return 0;
    }

    struct stat stbuf;
    if (fstat(state_fd, &stbuf) < 0) {
        int base_errno = errno;
        close(state_fd);
        errno = base_errno;
        return -1;
    }

    ssize_t size = stbuf.st_size;
    state_text = realloc(state_text, size + 2);
    if (read(state_fd, state_text, size) != size) {
        int base_errno = errno;
        close(state_fd);
        errno = base_errno;
        return -1;
    }
    state_text[size] = '\0';
    state_text[size + 1] = '\0';

    if (close(state_fd) < 0)
        return -1;
    return 0;
}

// Parse the loaded state_text into the client_states and n_client_states. Works by splitting the buffer into \0
// separated strings by replacing all spaces and line breaks with \0. This trusts that the file was generated by the
// code so the result will have exactly the right number of fields per line (so we don't need to distinguish between
// space and line break, we can just count). While at it, simply do not load stale client states (if we do, already set
// client_states_changed).
static void
parse_client_states(const Narwhal* narwhal) {
    DEBUG_AT("load_state_text");
    long long first_fresh_time = time(NULL) - narwhal->timeout_sec;

    n_client_states = 0;
    for (char* p = state_text; *p; p++) {
        n_client_states += *p == '\n';
        if (*p == '\n' || *p == ' ')
            *p = '\0';
    }
    client_states = realloc(client_states, (n_client_states + 1) * sizeof(ClientState));

    client_states_changed = false;
    ClientState* next_client_state = client_states;
    granted_state = NULL;
    int field_index = 0;

    const char* p = state_text;
    while (*p) {
        switch (field_index++ % 5) {
        default:
            assert(false);

        case 0:
            next_client_state->host_name = p;
            break;

        case 1:
            next_client_state->pid = p;
            break;

        case 2:
            assert(!p[1]);
            switch (*p) {
            default:
                assert(false);
            case 'R':
                next_client_state->is_write_lock = false;
                break;
            case 'W':
                next_client_state->is_write_lock = true;
                break;
            }
            break;

        case 3:
            assert(!p[1]);
            switch (*p) {
            default:
                assert(false);
            case 'P':
                next_client_state->is_granted = false;
                break;
            case 'G':
                next_client_state->is_granted = true;
                granted_state = next_client_state;
                break;
            }
            break;

        case 4:
            next_client_state->time = atoll(p);
            if (next_client_state->time >= first_fresh_time) {
                DEBUG_EXP(next_client_state->time, "%lld (fresh request)");
                next_client_state++;
            } else {
                DEBUG_EXP(next_client_state->time, "%lld (stale request)");
                client_states_changed = true;
            }
            break;
        }

        while (*p++)
            ;
    }

    n_client_states = next_client_state - client_states;
}

// Load and parse the state file.
static int
load_client_states(const Narwhal* narwhal) {
    DEBUG_AT("load_client_states");
    if (load_state_text(narwhal) < 0)
        return -1;
    parse_client_states(narwhal);  // We assume this never fails because only we write the state file.
    return 0;
}

// Write an updated version of the state file.
static int
dump_client_states() {
    DEBUG_AT("dump_client_states");
    FILE* state_fp = fopen(state_path, "w");
    if (!state_fp)
        return -1;

    for (const ClientState* client_state = client_states; client_state != client_states + n_client_states;
         client_state++) {
        if (fprintf(state_fp,
                    "%s %s %c %c %lld\n",
                    client_state->host_name,
                    client_state->pid,
                    client_state->is_write_lock ? 'W' : 'R',
                    client_state->is_granted ? 'G' : 'P',
                    client_state->time)
            < 0) {
            fclose(state_fp);
            return -1;
        }
    }

    return fclose(state_fp);
}

// Update the client_states to include a lock request from the current process. Returns -1 on error, 0 if the request
// can't be granted yet, and 1 if it was granted. Will update an existing request, or add a new one if needed. Will fail
// if an incompatible request already exists.
static int
request_lock(bool is_write_lock) {
    DEBUG_AT("request_lock");
    DEBUG_EXP(is_write_lock, "%d");

    bool is_granted = !granted_state || (!is_write_lock && !granted_state->is_write_lock);

    ClientState* client_state = client_states;
    ClientState* end_state = client_states + n_client_states;
    for (; client_state != end_state; client_state++) {
        if (strcmp(client_state->pid, pid) || strcmp(client_state->host_name, host_name)) {
            fprintf(stderr, "%s != %s || %s != %s\n", client_state->pid, pid, client_state->host_name, host_name);
            continue;
        }

        if (client_state->is_granted || client_state->is_write_lock != is_write_lock) {
            errno = ENOTSUP;
            return -1;
        }

        if (is_granted) {
            client_state->is_granted = true;
            client_states_changed = true;
        }

        break;
    }

    if (client_state == end_state) {
        client_state->host_name = host_name;
        client_state->pid = pid;
        client_state->is_write_lock = is_write_lock;
        client_state->is_granted = is_granted;
        client_state->time = time(NULL);
        client_states_changed = true;
        DEBUG_EXP(client_state->time, "%lld (new request)");
        n_client_states++;
    } else {
        long long now = time(NULL);
        if (client_state->time != now) {
            client_state->time = now;
            client_states_changed = true;
            DEBUG_EXP(client_state->time, "%lld (renew request)");
        } else {
            DEBUG_EXP(client_state->time, "%lld (current request)");
        }
    }

    if (client_states_changed && dump_client_states() < 0)
        return -1;

    DEBUG_EXP(is_granted, "%d");
    return is_granted;
}

// Get an exclusive lock of the state file. This must be done before loading it. This just spins trying to create the
// lock; if we spin a very long time we assume whoever held the lock died without removing the lock file, but we have no
// way to safely remove it without introducing a race condition, so we just fail with ETIMEDOUT.
static int
exclusive_lock(const Narwhal* narwhal) {
    DEBUG_AT("exclusive_lock");
    int private_fd = creat(format_path(&private_path, narwhal->lockdir, "/", host_name, ".", pid, NULL), 0777);
    if (private_fd < 0 || close(private_fd) < 0)
        return -1;

    format_path(&lockfile_path, narwhal->lockdir, "/lockfile", NULL);
    long long last_reasonable_time = time(NULL) + narwhal->timeout_sec;

    const struct timespec duration = { .tv_sec = narwhal->spin_usec / 1000000,
                                       .tv_nsec = (narwhal->spin_usec % 1000000) * 1000 };
    for (;;) {
        if (!link(private_path, lockfile_path))
            return 0;
        nanosleep(&duration, NULL);
        if (time(NULL) > last_reasonable_time) {
            errno = ETIMEDOUT;
            return -1;
        }
    }
}

// Release the exclusive lock of the state file.
static int
exclusive_unlock(const Narwhal* narwhal) {
    int base_errno = errno;
    errno = 0;
    DEBUG_AT("exclusive_unlock");
    int lockfile_result = unlink(lockfile_path);
    int private_result = unlink(private_path);
    if (base_errno != 0)
        errno = base_errno;
    return lockfile_result < private_result ? lockfile_result : private_result;
}

// Implement narwhal_read_lock. See the header file.
int
narwhal_read_lock(const Narwhal* narwhal) {
    init();
    DEBUG_AT("narwhal_read_lock");
    for (;;) {
        if (exclusive_lock(narwhal) < 0)
            return -1;
        int result = load_client_states(narwhal) < 0 ? -1 : request_lock(false);
        if (exclusive_unlock(narwhal) < 0 || result < 0)
            return -1;
        if (result)
            return 0;
    }
}

// Implement narwhal_write_lock. See the header file.
int
narwhal_write_lock(const Narwhal* narwhal) {
    init();
    DEBUG_AT("narwhal_write_lock");
    for (;;) {
        if (exclusive_lock(narwhal) < 0)
            return -1;
        int result = load_client_states(narwhal) < 0 ? -1 : request_lock(true);
        if (exclusive_unlock(narwhal) < 0 || result < 0)
            return -1;
        if (result)
            return 0;
    }
}

// Update the client_states to remove the request of the current process (which must exist and be granted).
static int
remove_lock(bool is_write_lock) {
    DEBUG_AT("remove_lock");
    ClientState* client_state = client_states;
    ClientState* end_state = client_states + n_client_states;
    for (; client_state != end_state; client_state++) {
        if (strcmp(client_state->pid, pid) || strcmp(client_state->host_name, host_name))
            continue;

        assert(client_state->is_granted);
        break;
    }

    if (client_state == end_state) {
        errno = ENOTSUP;
        return -1;
    }

    memmove(client_state, client_state + 1, (end_state - client_state - 1) * sizeof(ClientState));

    if (dump_client_states() < 0)
        return -1;

    return 0;
}

// Implement narwhal_unlock. See the header file.
int
narwhal_unlock(const Narwhal* narwhal) {
    init();
    DEBUG_AT("narwhal_unlock");
    for (;;) {
        if (exclusive_lock(narwhal) < 0)
            return -1;
        int result = load_client_states(narwhal) < 0 ? -1 : remove_lock(narwhal);
        if (exclusive_unlock(narwhal) < 0 || result < 0)
            return -1;
        return 0;
    }
}
