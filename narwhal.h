#ifndef __NARWHAL__
#define __NARWHAL__

#ifdef __cplusplus
extern "C" {
#endif

// Narwhal: NFS Read/Write Locks
//
// This is a glorified spin-lock supporting multiple readers / single writer. This is a very simple spinlock based
// implementation which means that you should reduce the time you are holding the lock to the bare minimum, since other
// clients will be spinning waiting for you to release it. It is also assumed that writes are relatively rare compared
// to reads, so a request for a write lock will stall requests for read locks until the write lock is granted and
// released.
//
// Note that while this implements locks for synchronizing between processes, the code is not thread safe; make all your
// calls to this API from the same (main?) thread or otherwise ensure only one thread at a time calls the API.
//
// The idea is that this can be used by processes on different NFS clients to coordinate access to a (small) amount of
// data, something like:
//
//      #include <narwhal.h>
//
//      narwhal = Narwhal {
//              .lockdir = "/some/path/to/lockdir",
//              .spin_usec = 1000,
//              .timeout_sec = 10
//      };
//
//      narwhal_read_lock(&narwhal)
//      read_protected_data()
//      narwhal_unlock(&narwhal)
//
//      narwhal_write_lock(&narwhal)
//      read_protected_data()
//      write_updated_data()
//      narwhal_unlock(&narwhal)
//
// This is implemented as a single .h file and a single .c file you can drop into your project. It depends only on ANSI
// and POSIX APIs, and requires a C99 or C++ compiler.

#include <sys/types.h>
#include <time.h>

// Parameters for Narwhal operations.
typedef struct {
    // A path of a directory that will contain lock files, typically stored on a remote NFS server. These files are:
    //
    // - hostname.pid: an empty lock file for a specific process in a specific host.
    //
    // - lockfile: an empty lock file which is a hard link from one of the per-process lock files. Creating this link is
    //   an atomic operation (even in NFS) which is the key to the whole scheme.
    //
    // - state: a text file containing the system state. All accesses to this file is protected by the lockfile. Each
    //   line contains the following space separated fields:
    //
    //   - The hostname() of the process requesting this lock.
    //
    //   - The getpid() of the process requesting this lock.
    //
    //   - The desired lock state of some process, one of R (read) or W (write).
    //
    //   - Whether the lock is G (granted) or P (pending).
    //
    //   - The time() the process requested this lock state. This assumes all the clients have synchronized UTC time()
    //     results.
    //
    // You can "hard reset" the system by removing all files in the lockdir (as long as you are 100% certain that there
    // are no active processes trying to use it). In particular, this is a reasonable thing to do when booting a system.
    // You can also safely delete all files whose last modification time is in the past (more than the maximal timeout
    // you are using).
    const char* lockdir;

    // The number of microseconds to sleep when spinning waiting for a lock. Should be low to minimize the latency of
    // obtaining a lock. This comes at the cost of consuming more CPU and network resources. A reasonable value is ~1000
    // (1 millisecond to deal with local network latency) Sleeping is done using nanosleep().
    suseconds_t spin_usec;

    // The number of seconds after which to assume a held lock is to be ignored due to the process obtaining it having
    // crashed, or not releasing the lock due to a bug. Should be high to minimize false positives. This comes at the
    // cost of stalling the whole system for a long time when a single process crashes. A reasonable number is ~10 (ten
    // seconds is plenty for a process to get its affairs in order, and is bearable for people waiting for a stalled
    // system to recover).
    time_t timeout_sec;
} Narwhal;

// Obtain a read lock. This works by:
//
// - Getting exclusive ownership of the lockfile.
//
// - Parse the state file. Remove any stale entries (older than the timeout).
//
// - If there are no write locks (granted or pending), mark the lock as granted, otherwise as pending.
//
// - Write the state file (if modified) and release the lockfile.
//
// - If the lock was granted, return. Otherwise, sleep and try again (spin).
//
// Behaves like standard C functions - returns 0 on success and -1 on error, setting ERRNO to something appropriate. In
// particular, will set errno to ENOTSUP if the process already has a lock. This will ignore stale lock requests, but if
// there is an abandoned lockfile (very rare, since we only hold it when accessing the state file), this will ETIMEDOUT
// after timeout_sec.
extern int
narwhal_read_lock(const Narwhal* narwhal);

// Obtain a read lock. This works by:
//
// - Getting exclusive ownership of the lockfile.
//
// - Parse the state file. Remove any stale entries (older than the timeout).
//
// - If there are no granted write locks, mark the lock as granted, otherwise as pending.
//
// - Write the state file (if modified) and release the lockfile.
//
// - If the lock was granted, return. Otherwise, sleep and try again (spin).
//
// Behaves like standard C functions - returns 0 on success and -1 on error, setting ERRNO to something appropriate. In
// particular, will set errno to ENOTSUP if the process already has a lock. This will ignore stale lock requests, but if
// there is an abandoned lockfile (very rare, since we only hold it when accessing the state file), this will ETIMEDOUT
// after timeout_sec.
extern int
narwhal_write_lock(const Narwhal* narwhal);

// Release a read or write lock. This works by:
//
// - Getting exclusive ownership of the lockfile.
//
// - Parse the state file. Remove any stale entries (older than the timeout) and
// the entry for the current process.
//
// - Write the state file (if modified) and release the lockfile.
//
// Behaves like standard C functions - returns 0 on success and -1 on error, setting ERRNO to something appropriate. In
// particular, will set errno to ENOTSUP if the process does not have a lock.
extern int
narwhal_unlock(const Narwhal* narwhal);

// Set the hostname to use for this process. By default, uses the result of gethostname, but it is sometimes useful to
// override it (e.g. for tests).
extern void
narwhal_hostname(const char* hostname);

// Set the pid to use for this process. By default, uses the result of getpid, but it is sometimes useful to override it
// (e.g. for tests).
extern void
narwhal_pid(const char* pid);

#ifdef __cplusplus
}
#endif

#endif
