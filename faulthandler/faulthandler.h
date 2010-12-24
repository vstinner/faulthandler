#ifndef FAULTHANDLER_HEADER
#define FAULTHANDLER_HEADER

#include "Python.h"
#include <signal.h>

#define MAX_FRAME_DEPTH 100

#define PUTS(str, fd) write(fd, str, strlen(str))

extern int faulthandler_enabled;

void faulthandler_enable(void);
PyObject* faulthandler_enable_method(PyObject *self);
PyObject* faulthandler_disable_method(PyObject *self);

void faulthandler_dump_backtrace(int fd);

PyObject* faulthandler_sigsegv(PyObject *self, PyObject *args);
PyObject* faulthandler_sigfpe(PyObject *self, PyObject *args);

#if defined(SIGBUS) && defined(HAVE_KILL)
#define FAULTHANDLER_HAVE_SIGBUS
PyObject* faulthandler_sigbus(PyObject *self, PyObject *args);
#endif

#if defined(SIGILL) && defined(HAVE_KILL)
#define FAULTHANDLER_HAVE_SIGILL
PyObject* faulthandler_sigill(PyObject *self, PyObject *args);
#endif

PyObject* faulthandler_fatal_error(PyObject *self, PyObject *args);

#endif

