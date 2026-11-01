// Bionic provides the POSIX memory, threading, sockets and signal APIs used by
// the Linux host layer. Its Android cases omit glibc assumptions, page tracking
// and desktop process restart, and resolve resources in app-private storage.
#ifndef __ANDROID__
#error Build this platform with the Android NDK
#endif
#include "linux.c"
