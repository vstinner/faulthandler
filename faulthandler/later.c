#include "faulthandler.h"

#ifdef FAULTHANDLER_LATER
static struct {
    PyObject *file;
    int fd;
    int delay;
    int repeat;
    int all_threads;
} fault_alarm;

/*
 * Handler of the SIGALRM signal: dump the backtrace of the current thread or
 * of all threads if fault_alarm.all_threads is true. On success, register
 * itself again if fault_alarm.repeat is true.
 */
static void
faulthandler_alarm(int signum)
{
    int ok;
    PyThreadState *tstate;

    /* PyThreadState_Get() doesn't give the state of the current thread if
       the thread doesn't hold the GIL. Read the thread local storage (TLS)
       instead: call PyGILState_GetThisThreadState(). */
    tstate = PyGILState_GetThisThreadState();
    if (tstate == NULL) {
        /* unable to get the current thread, do nothing */
        return;
    }

    if (fault_alarm.all_threads) {
        const char* errmsg;

        errmsg = faulthandler_dump_backtrace_threads(fault_alarm.fd, tstate);
        ok = (errmsg == NULL);
    }
    else {
        faulthandler_dump_backtrace(fault_alarm.fd, tstate, 1);
        ok = 1;
    }

    if (ok && fault_alarm.repeat)
        alarm(fault_alarm.delay);
    else
        faulthandler_cancel_dumpbacktrace_later();
}

PyObject*
faulthandler_dumpbacktrace_later(PyObject *self, PyObject *args, PyObject *kwargs)
{
    static char *kwlist[] = {"delay", "repeat", "file", "all_threads", NULL};
    int delay;
    PyOS_sighandler_t previous;
    int repeat = 0;
    PyObject *file = NULL;
    int all_threads = 0;
    int fd;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs,
        "i|iOi:dump_backtrace_later", kwlist,
        &delay, &repeat, &file, &all_threads))
        return NULL;
    if (delay <= 0) {
        PyErr_SetString(PyExc_ValueError, "delay must be greater than 0");
        return NULL;
    }

    if (file == NULL || file == Py_None) {
        file = PySys_GetObject("stderr");
        if (file == NULL) {
            PyErr_SetString(PyExc_RuntimeError, "unable to get sys.stderr");
            return NULL;
        }
    }

    fd = faulthandler_get_fileno(file);
    if (fd == -1)
        return NULL;

    previous = signal(SIGALRM, faulthandler_alarm);
    if (previous == SIG_ERR) {
        PyErr_SetString(PyExc_RuntimeError, "unable to set SIGALRM handler");
        return NULL;
    }

    Py_INCREF(file);
    fault_alarm.file = file;
    fault_alarm.fd = fd;
    fault_alarm.delay = delay;
    fault_alarm.repeat = repeat;
    fault_alarm.all_threads = all_threads;

    alarm(delay);

    Py_RETURN_NONE;
}

void
faulthandler_cancel_dumpbacktrace_later()
{
    Py_CLEAR(fault_alarm.file);
    alarm(0);
}

PyObject*
faulthandler_cancel_dumpbacktrace_later_py(PyObject *self)
{
    faulthandler_cancel_dumpbacktrace_later();
    Py_RETURN_NONE;
}
#endif

