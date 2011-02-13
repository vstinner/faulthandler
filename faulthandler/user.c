#include "faulthandler.h"

typedef struct {
    int signum;
    PyObject *file;
    int fd;
    int all_threads;
    _Py_sighandler_t previous;
} user_signal_t;

static struct {
    size_t nsignal;
    user_signal_t *signals;
} user_signals = {0, NULL};

/*
 * Handler of user signals: dump the backtrace of the current thread, or of all
 * threads if fault_alarm.all_threads is true.
 */
static void
faulthandler_user(int signum)
{
    user_signal_t *user = NULL;
    unsigned int i;
    PyThreadState *tstate;

    for (i=0; i < user_signals.nsignal; i++) {
        user = &user_signals.signals[i];
        if (user->signum == signum)
            break;
    }
    if (user == NULL) {
        /* user_signals.nsignal == 0 */
        return;
    }

    /* PyThreadState_Get() doesn't give the state of the current thread if
       the thread doesn't hold the GIL. Read the thread local storage (TLS)
       instead: call PyGILState_GetThisThreadState(). */
    tstate = PyGILState_GetThisThreadState();
    if (tstate == NULL) {
        /* unable to get the current thread, do nothing */
        return;
    }

    if (user->all_threads)
        faulthandler_dump_backtrace_threads(user->fd, tstate);
    else
        faulthandler_dump_backtrace(user->fd, tstate, 1);
}

PyObject*
faulthandler_register(PyObject *self,
                      PyObject *args, PyObject *kwargs)
{
    static char *kwlist[] = {"signum", "file", "all_threads", NULL};
    int signum;
    PyObject *file = NULL;
    int all_threads = 0;
    int fd;
    unsigned int i;
    user_signal_t *user, *signals;
    size_t size;
    _Py_sighandler_t previous;
#ifdef HAVE_SIGACTION
    struct sigaction action;
#endif
    int err;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs,
        "i|Oi:register", kwlist,
        &signum, &file, &all_threads))
        return NULL;

    for (i=0; i < faulthandler_nsignals; i++) {
        if (faulthandler_handlers[i].signum != signum)
            continue;
        PyErr_Format(PyExc_RuntimeError,
                     "signal %i cannot be registered by register(): "
                     "use enable() instead",
                     signum);
        return NULL;
    }

    /* the following test comes from Python: Modules/signal.c */
#ifdef MS_WINDOWS
    /* Validate that sig_num is one of the allowable signals */
    switch (signum) {
    case SIGABRT: break;
#ifdef SIGBREAK
    /* Issue #10003: SIGBREAK is not documented as permitted, but works
       and corresponds to CTRL_BREAK_EVENT. */
    case SIGBREAK: break;
#endif
    case SIGFPE: break;
    case SIGILL: break;
    case SIGINT: break;
    case SIGSEGV: break;
    case SIGTERM: break;
    default:
        PyErr_SetString(PyExc_ValueError, "invalid signal value");
        return NULL;
    }
#endif

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

    user_signals.nsignal++;
    size = user_signals.nsignal * sizeof(user_signal_t);
    if (size / user_signals.nsignal != sizeof(user_signal_t)) {
        /* integer overflow */
        return PyErr_NoMemory();
    }
    signals = realloc(user_signals.signals, size);
    if (signals == NULL)
        return PyErr_NoMemory();
    user_signals.signals = signals;
    user = &signals[user_signals.nsignal - 1];

#ifdef HAVE_SIGACTION
    action.sa_handler = faulthandler_user;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_ONSTACK;
    err = sigaction(signum, &action, &previous);
#else
    previous = signal(signum, faulthandler_user);
    err = (previous == SIG_ERR);
#endif
    if (err) {
        user_signals.nsignal--;
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    Py_INCREF(file);
    user->signum = signum;
    user->file = file;
    user->fd = fd;
    user->all_threads = all_threads;
    user->previous = previous;

    Py_RETURN_NONE;
}

static void
faulthandler_unregister(user_signal_t *user)
{
#ifdef HAVE_SIGACTION
    (void)sigaction(user->signum, &user->previous, NULL);
#else
    (void)signal(user->signum, user->previous);
#endif
}

PyObject*
faulthandler_unregister_py(PyObject *self, PyObject *args)
{
    int signum;
    unsigned int index;
    user_signal_t *user;
    size_t size;

    if (!PyArg_ParseTuple(args, "i:unregister", &signum))
        return NULL;

    user = NULL;
    for (index=0; index < user_signals.nsignal; index++) {
        if (user_signals.signals[index].signum == signum) {
            user = &user_signals.signals[index];
            break;
        }
    }
    if (user == NULL) {
        Py_INCREF(Py_False);
        return Py_False;
    }

    faulthandler_unregister(user);
    Py_DECREF(user->file);
    if (user_signals.nsignal - index != 1) {
        size = user_signals.nsignal - index - 1;
        size *= sizeof(user_signals.signals[0]);
        memmove(&user_signals.signals[index],
                &user_signals.signals[index+1],
                size);
    }
    user_signals.nsignal--;

    Py_INCREF(Py_True);
    return Py_True;
}

void
faulthandler_unload_user()
{
    unsigned int i;

    for (i=0; i < user_signals.nsignal; i++) {
        faulthandler_unregister(&user_signals.signals[i]);
        /* don't release user->file: faulthandler_unload_user()
           is called too late */
    }
    user_signals.nsignal = 0;
    free(user_signals.signals);
    user_signals.signals = NULL;
}

