#include "faulthandler.h"
#include <signal.h>

#ifdef HAVE_SIGALTSTACK
stack_t faulthandler_stack;
#endif

static struct {
    int enabled;
    PyObject *file;
    int fd;
    int all_threads;
} fatal_error = {
    /* fileno(stderr)=2: the value is replaced in faulthandler_enable() */
    0, NULL, 2
};


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
    const int fd = fatal_error.fd;
    unsigned int i;
    fault_handler_t *handler = NULL;
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

    if (fatal_error.all_threads)
        faulthandler_dump_backtrace_threads(fd, tstate);
    else
        faulthandler_dump_backtrace(fd, tstate, 1);
}

PyObject*
faulthandler_enable(PyObject *self, PyObject *args, PyObject *kwargs)
{
    static char *kwlist[] = {"file", "all_threads", NULL};
    PyObject *file = NULL;
    int all_threads = 0;
    unsigned int i;
    fault_handler_t *handler;
#ifdef HAVE_SIGACTION
    struct sigaction action;
    int err;
#endif
    int fd;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs,
        "|Oi:enable", kwlist, &file, &all_threads))
        return NULL;

    if (file == NULL) {
        file = PySys_GetObject("stderr");
        if (file == NULL) {
            PyErr_SetString(PyExc_RuntimeError, "unable to get sys.stderr");
            return NULL;
        }
    }

    fd = faulthandler_get_fileno(file);
    if (fd == -1)
        return NULL;

    Py_XDECREF(fatal_error.file);
    Py_INCREF(file);
    fatal_error.file = file;
    fatal_error.fd = fd;
    fatal_error.all_threads = all_threads;

    if (!fatal_error.enabled) {
        fatal_error.enabled = 1;

        for (i=0; i < faulthandler_nsignals; i++) {
            handler = &faulthandler_handlers[i];
#ifdef HAVE_SIGACTION
            action.sa_handler = faulthandler_fatal_error;
            sigemptyset(&action.sa_mask);
            action.sa_flags = 0;
#ifdef HAVE_SIGALTSTACK
            if (faulthandler_stack.ss_sp != NULL)
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

    Py_CLEAR(fatal_error.file);

    if (fatal_error.enabled) {
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
    fatal_error.enabled = 0;
}

PyObject*
faulthandler_disable_py(PyObject *self)
{
    if (!fatal_error.enabled) {
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
    return PyBool_FromLong(fatal_error.enabled);
}

void
faulthandler_unload_fatal_error()
{
    /* don't release file: faulthandler_unload_fatal_error()
       is called too late */
    fatal_error.file = NULL;
    faulthandler_disable();
}

