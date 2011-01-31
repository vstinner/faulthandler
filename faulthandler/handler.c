#include "faulthandler.h"
#include <signal.h>

#ifdef HAVE_SIGALTSTACK
static stack_t stack;
#endif

int faulthandler_enabled = 0;

static PyObject *fatal_error_file = NULL;
/* fileno(stderr)=2: the value is replaced in faulthandler_enable() */
static int fatal_error_fd = 2;

static struct {
    PyObject *file;
    int fd;
    int delay;
    int repeat;
    int all_threads;
} fault_alarm;

fault_handler_t faulthandler_handlers[] = {
#ifdef SIGBUS
    {SIGBUS, 0, "Bus error", },
#endif
#ifdef SIGILL
    {SIGILL, 0, "Illegal instruction", },
#endif
    {SIGFPE, 0, "Floating point exception", },
    /* define SIGSEGV at the end to make it the default choice if searching the
       handler fails in faulthandler_fatal_error() */
    {SIGSEGV, 0, "Segmentation fault", }
};
unsigned char faulthandler_nsignals = \
    sizeof(faulthandler_handlers) / sizeof(faulthandler_handlers[0]);

/* Fault handler: display the current Python backtrace and restore the previous
   handler. It should only use signal-safe functions. The previous handler will
   be called when the fault handler exits, because the fault will occur
   again. */

void
faulthandler_fatal_error(int signum)
{
    const int fd = fatal_error_fd;
    unsigned int i;
    fault_handler_t *handler;
    PyThreadState *tstate;

    /* restore the previous handler */
    for (i=0; i < faulthandler_nsignals; i++) {
        handler = &faulthandler_handlers[i];
        if (handler->signum == signum)
            break;
    }
#ifdef HAVE_SIGACTION
    (void)sigaction(handler->signum, &handler->previous, NULL);
#else
    (void)signal(handler->signum, handler->previous);
#endif
    handler->enabled = 0;

    PUTS(fd, "Fatal Python error: ");
    PUTS(fd, handler->name);
    PUTS(fd, "\n\n");

    /* SIGSEGV, SIGFPE, SIGBUS and SIGILL are synchronous signals and so are
       delivered to the thread that caused the fault. Get the Python thread
       state of the current thread.

       PyThreadState_Get() doesn't give the state of the thread that caused the
       fault if the thread released the GIL, and so this function cannot be
       used. Read the thread local storage (TLS) instead: call
       PyGILState_GetThisThreadState(). */
    tstate = PyGILState_GetThisThreadState();
    if (tstate == NULL)
        return;

    faulthandler_dump_backtrace(fd, tstate, 1);
}

PyObject*
faulthandler_enable(PyObject *self, PyObject *args)
{
    PyObject *file = NULL;
    unsigned int i;
    fault_handler_t *handler;
#ifdef HAVE_SIGACTION
    struct sigaction action;
    int err;
#endif

    if (!PyArg_ParseTuple(args, "|O:enable", &file))
        return NULL;

    if (file == NULL) {
        file = PySys_GetObject("stderr");
        if (file == NULL) {
            PyErr_SetString(PyExc_RuntimeError, "unable to get sys.stderr");
            return NULL;
        }
    }

    Py_XDECREF(fatal_error_file);
    Py_INCREF(file);
    fatal_error_file = file;
    fatal_error_fd = faulthandler_get_fileno(file);
    if (fatal_error_fd == -1)
        return NULL;

    if (!faulthandler_enabled) {
        faulthandler_enabled = 1;

        for (i=0; i < faulthandler_nsignals; i++) {
            handler = &faulthandler_handlers[i];
#ifdef HAVE_SIGACTION
            action.sa_handler = faulthandler_fatal_error;
            sigemptyset(&action.sa_mask);
            action.sa_flags = 0;
#ifdef HAVE_SIGALTSTACK
            if (stack.ss_sp != NULL)
                action.sa_flags |= SA_ONSTACK;
#endif
            err = sigaction(handler->signum, &action, &handler->previous);
            if (!err)
                handler->enabled = 1;
#else
            handler->previous = signal(handler->signum,
                                       faulthandler_fatal_error);
            if (handler->previous != SIG_ERR)
                handler->enabled = 1;
#endif
        }
    }
    Py_RETURN_NONE;
}

void
faulthandler_disable()
{
    unsigned int i;
    fault_handler_t *handler;

    Py_CLEAR(fatal_error_file);

    if (faulthandler_enabled) {
        for (i=0; i < faulthandler_nsignals; i++) {
            handler = &faulthandler_handlers[i];
            if (!handler->enabled)
                continue;
#ifdef HAVE_SIGACTION
            (void)sigaction(handler->signum, &handler->previous, NULL);
#else
            (void)signal(handler->signum, handler->previous);
#endif
            handler->enabled = 0;
        }
    }
    faulthandler_enabled = 0;
}

PyObject*
faulthandler_disable_py(PyObject *self)
{
    if (!faulthandler_enabled) {
        Py_INCREF(Py_False);
        return Py_False;
    }
    faulthandler_disable();
    Py_INCREF(Py_True);
    return Py_True;
}

PyObject*
faulthandler_isenabled(PyObject *self)
{
    return PyBool_FromLong(faulthandler_enabled);
}

#ifdef FAULTHANDLER_LATER
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

