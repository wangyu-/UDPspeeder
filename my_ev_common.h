
#define EV_STANDALONE 1
#define EV_COMMON \
    void *data;   \
    unsigned long long u64;
#define EV_COMPAT3 0
//#define EV_VERIFY 2

#if defined(__MINGW32__)
#define EV_FD_TO_WIN32_HANDLE(fd) (fd)
#define EV_WIN32_HANDLE_TO_FD(handle) (handle)
#define EV_WIN32_CLOSE_FD(fd) closesocket(fd)
#define FD_SETSIZE 4096

#endif
