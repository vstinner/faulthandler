#ifndef FAULTHANDLER_HEADER
#define FAULTHANDLER_HEADER

#include "Python.h"
#include <signal.h>

#ifdef SIGALRM
#  define FAULTHANDLER_LATER
#endif

#ifndef MS_WINDOWS
#  define HAVE_SIGALTSTACK
#endif

#define MAX_FRAME_DEPTH 100
#define MAX_NTHREADS 100

#define PUTS(fd, str) write(fd, str, strlen(str))

#ifdef HAVE_SIGACTION
typedef struct sigaction _Py_sighandler_t;
#else
typedef PyOS_sighandler_t _Py_sighandler_t;
#endif

typedef struct {
    int signum;
    int enabled;
    const char* name;
    _Py_sighandler_t previous;
    int all_threads;
} fault_handler_t;

extern fault_handler_t faulthandler_handlers[];
extern unsigned char faulthandler_nsignals;
#ifdef HAVE_SIGALTSTACK
extern stack_t faulthandler_stack;
#endif

void faulthandler_init(void);

void faulthandler_fatal_error(
    int signum);

int faulthandler_get_fileno(PyObject *file);

PyObject* faulthandler_enable(PyObject *self,
    PyObject *args,
    PyObject *kwargs);
void faulthandler_disable(void);
PyObject* faulthandler_disable_py(PyObject *self);
PyObject* faulthandler_isenabled(PyObject *self);
void faulthandler_unload_fatal_error(void);

void faulthandler_dump_backtrace(int fd, PyThreadState *tstate, int write_header);
const char* faulthandler_dump_backtrace_threads(
    int fd,
    PyThreadState *current_thread);

PyObject* faulthandler_dump_backtrace_py(PyObject *self,
    PyObject *args,
    PyObject *kwargs);

#ifdef FAULTHANDLER_LATER
PyObject* faulthandler_dumpbacktrace_later(PyObject *self,
    PyObject *args,
    PyObject *kwargs);
PyObject* faulthandler_cancel_dumpbacktrace_later_py(PyObject *self);
void faulthandler_cancel_dumpbacktrace_later(void);
#endif

PyObject* faulthandler_sigsegv(PyObject *self, PyObject *args);
PyObject* faulthandler_sigfpe(PyObject *self, PyObject *args);

#if defined(SIGBUS)
PyObject* faulthandler_sigbus(PyObject *self, PyObject *args);
#endif

#if defined(SIGILL)
PyObject* faulthandler_sigill(PyObject *self, PyObject *args);
#endif

PyObject* faulthandler_register(PyObject *self,
    PyObject *args, PyObject *kwargs);
PyObject* faulthandler_unregister_py(PyObject *self,
    PyObject *args);
void faulthandler_unload_user(void);

#endif

