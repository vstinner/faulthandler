#ifndef FAULTHANDLER_HEADER
#define FAULTHANDLER_HEADER

#include "Python.h"
#include <signal.h>

#ifdef SIGALRM
#  define FAULTHANDLER_LATER
#endif

#define MAX_FRAME_DEPTH 100

#define PUTS(fd, str) write(fd, str, strlen(str))

extern int faulthandler_enabled;

void faulthandler_init(void);

void faulthandler_fatal_error(
    int signum);

int faulthandler_get_fileno(PyObject *file);

PyObject* faulthandler_enable(PyObject *self,
    PyObject *args);
PyObject* faulthandler_disable_py(PyObject *self);
PyObject* faulthandler_isenabled(PyObject *self);

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

#endif

