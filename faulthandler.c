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
PyObject* faulthandler_is_enabled(PyObject *self);
void faulthandler_unload_fatal_error(void);

void faulthandler_dump_traceback(int fd, PyThreadState *tstate, int write_header);
const char* faulthandler_dump_traceback_threads(
    int fd,
    PyThreadState *current_thread);

PyObject* faulthandler_dump_traceback_py(PyObject *self,
    PyObject *args,
    PyObject *kwargs);

#ifdef FAULTHANDLER_LATER
PyObject* faulthandler_dump_traceback_later(PyObject *self,
    PyObject *args,
    PyObject *kwargs);
PyObject* faulthandler_cancel_dump_traceback_later_py(PyObject *self);
void faulthandler_unload_dump_traceback_later(void);
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

#include <frameobject.h>

#define MAX_LENGTH 100

#if PY_MAJOR_VERSION >= 3
#  define PYSTRING_CHECK PyUnicode_Check
#  define PYINT_CHECK PyLong_Check
#  define PYINT_ASLONG PyLong_AsLong
#else
#  define PYSTRING_CHECK PyString_Check
#  define PYINT_CHECK PyInt_Check
#  define PYINT_ASLONG PyInt_AsLong
#endif

/* Reverse a string. For example, "abcd" becomes "dcba".

   This function is signal safe. */

static void
reverse_string(char *text, const size_t len)
{
    char tmp;
    size_t i, j;
    if (len == 0)
        return;
    for (i=0, j=len-1; i < j; i++, j--) {
        tmp = text[i];
        text[i] = text[j];
        text[j] = tmp;
    }
}

/* Format an integer in range [0; 999999] to decimal,
   and write it into the file fd.

   This function is signal safe. */

static void
dump_decimal(int fd, int value)
{
    char buffer[7];
    int len;
    if (value < 0 || 999999 < value)
        return;
    len = 0;
    do {
        buffer[len] = '0' + (value % 10);
        value /= 10;
        len++;
    } while (value);
    reverse_string(buffer, len);
    write(fd, buffer, len);
}

/* Format an integer in range [0; 0xffffffff] to hexdecimal of 'width' digits,
   and write it into the file fd.

   This function is signal safe. */

static void
dump_hexadecimal(int width, unsigned long value, int fd)
{
    const char *hexdigits = "0123456789abcdef";
    int len;
    char buffer[sizeof(unsigned long) * 2 + 1];
    len = 0;
    do {
        buffer[len] = hexdigits[value & 15];
        value >>= 4;
        len++;
    } while (len < width || value);
    reverse_string(buffer, len);
    write(fd, buffer, len);
}

/* Write an unicode object into the file fd using ascii+backslashreplace.

   This function is signal safe. */

static void
dump_ascii(int fd, PyObject *text)
{
    Py_ssize_t i, size;
    int truncated;
#if PY_MAJOR_VERSION >= 3
    Py_UNICODE *u;
    char c;

    size = PyUnicode_GET_SIZE(text);
    u = PyUnicode_AS_UNICODE(text);
#else
    char *s;
    unsigned char c;

    size = PyString_GET_SIZE(text);
    s = PyString_AS_STRING(text);
#endif

    if (MAX_LENGTH < size) {
        size = MAX_LENGTH;
        truncated = 1;
    }
    else
        truncated = 0;

#if PY_MAJOR_VERSION >= 3
    for (i=0; i < size; i++, u++) {
        if (*u < 128) {
            c = (char)*u;
            write(fd, &c, 1);
        }
        else if (*u < 256) {
            PUTS(fd, "\\x");
            dump_hexadecimal(2, *u, fd);
        }
        else
#ifdef Py_UNICODE_WIDE
        if (*u < 65536)
#endif
        {
            PUTS(fd, "\\u");
            dump_hexadecimal(4, *u, fd);
#ifdef Py_UNICODE_WIDE
        }
        else {
            PUTS(fd, "\\U");
            dump_hexadecimal(8, *u, fd);
#endif
        }
    }
#else
    for (i=0; i < size; i++, s++) {
        c = *s;
        if (c < 128) {
            write(fd, s, 1);
        }
        else {
            PUTS(fd, "\\x");
            dump_hexadecimal(2, c, fd);
        }
    }
#endif
    if (truncated)
        PUTS(fd, "...");
}

/* Write a frame into the file fd: "File "xxx", line xxx in xxx".

   This function is signal safe. */

static void
dump_frame(int fd, PyFrameObject *frame)
{
    PyCodeObject *code;
    int lineno;

    code = frame->f_code;
    PUTS(fd, "  File ");
    if (code != NULL && code->co_filename != NULL
        && PYSTRING_CHECK(code->co_filename))
    {
        write(fd, "\"", 1);
        dump_ascii(fd, code->co_filename);
        write(fd, "\"", 1);
    } else {
        PUTS(fd, "???");
    }

#if (PY_MAJOR_VERSION <= 2 && PY_MINOR_VERSION < 7) \
||  (PY_MAJOR_VERSION >= 3 && PY_MINOR_VERSION < 2)
    /* PyFrame_GetLineNumber() was introduced in Python 2.7.0 and 3.2.0 */
    lineno = PyCode_Addr2Line(frame->f_code, frame->f_lasti);
#else
    lineno = PyFrame_GetLineNumber(frame);
#endif
    PUTS(fd, ", line ");
    dump_decimal(fd, lineno);
    PUTS(fd, " in ");

    if (code != NULL && code->co_name != NULL
        && PYSTRING_CHECK(code->co_name))
        dump_ascii(fd, code->co_name);
    else
        PUTS(fd, "???");

    write(fd, "\n", 1);
}

/* Write the current Python traceback into the file 'fd':

   Traceback (most recent call first):
     File "xxx", line xxx in <xxx>
     File "xxx", line xxx in <xxx>
     ...
     File "xxx", line xxx in <xxx>

   Write only the first MAX_FRAME_DEPTH frames. If the traceback is truncated, write
   the line "  ...".

   This function is signal safe. */

void
faulthandler_dump_traceback(int fd, PyThreadState *tstate, int write_header)
{
    PyFrameObject *frame;
    unsigned int depth;

    frame = _PyThreadState_GetFrame(tstate);
    if (frame == NULL)
        return;

    if (write_header)
        PUTS(fd, "Traceback (most recent call first):\n");
    depth = 0;
    while (frame != NULL) {
        if (MAX_FRAME_DEPTH <= depth) {
            PUTS(fd, "  ...\n");
            break;
        }
        if (!PyFrame_Check(frame))
            break;
        dump_frame(fd, frame);
        frame = frame->f_back;
        depth++;
    }
}

/* Write the thread identifier into the file 'fd': "Current thread 0xHHHH:\" if
   is_current is true, "Thread 0xHHHH:\n" otherwise.

   This function is signal safe. */

static void
write_thread_id(int fd, PyThreadState *tstate, int is_current)
{
    if (is_current)
        PUTS(fd, "Current thread 0x");
    else
        PUTS(fd, "Thread 0x");
    dump_hexadecimal(sizeof(long)*2, (unsigned long)tstate->thread_id, fd);
    PUTS(fd, ":\n");
}

/* Dump the traceback of all threads. Return NULL on success, or an error
   message on error.

   This function is signal safe. */

const char*
faulthandler_dump_traceback_threads(int fd, PyThreadState *current_thread)
{
    PyInterpreterState *interp;
    PyThreadState *tstate;
    unsigned int nthreads;

    /* Get the current interpreter from the current thread */
    interp = current_thread->interp;
    if (interp == NULL)
        return "unable to get the interpreter";

    tstate = PyInterpreterState_ThreadHead(interp);
    if (tstate == NULL)
        return "unable to get the thread head state";

    /* Dump the traceback of each thread */
    tstate = PyInterpreterState_ThreadHead(interp);
    nthreads = 0;
    do
    {
        if (nthreads != 0)
            write(fd, "\n", 1);
        if (nthreads >= MAX_NTHREADS) {
            PUTS(fd, "...\n");
            break;
        }
        write_thread_id(fd, tstate, tstate == current_thread);
        faulthandler_dump_traceback(fd, tstate, 0);
        tstate = PyThreadState_Next(tstate);
        nthreads++;
    } while (tstate != NULL);

    return NULL;
}

int
faulthandler_get_fileno(PyObject *file)
{
    PyObject *result;
    long fd_long;
    int fd;

    result = PyObject_CallMethod(file, "fileno", "");
    if (result == NULL)
        return -1;

    fd = -1;
    if (PYINT_CHECK(result)) {
        fd_long = PYINT_ASLONG(result);
        if (0 < fd_long && fd_long < INT_MAX)
            fd = (int)fd_long;
    }
    Py_DECREF(result);

    if (fd == -1) {
        PyErr_SetString(PyExc_RuntimeError,
                        "file.fileno() is not a valid file descriptor");
        return -1;
    }

    result = PyObject_CallMethod(file, "flush", "");
    if (result != NULL)
        Py_DECREF(result);
    else {
        /* ignore flush() error */
        PyErr_Clear();
    }
    return fd;
}

PyObject*
faulthandler_dump_traceback_py(PyObject *self,
                               PyObject *args, PyObject *kwargs)
{
    static char *kwlist[] = {"file", "all_threads", NULL};
    PyObject *file = NULL;
    int all_threads = 0;
    PyThreadState *tstate;
    const char *errmsg;
    int fd;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs,
        "|Oi:dump_traceback", kwlist,
        &file, &all_threads))
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

    /* The caller holds the GIL and so PyThreadState_Get() can be used */
    tstate = PyThreadState_Get();
    if (tstate == NULL) {
        PyErr_SetString(PyExc_RuntimeError,
                        "unable to get the current thread state");
        return NULL;
    }

    if (all_threads) {
        errmsg = faulthandler_dump_traceback_threads(fd, tstate);
        if (errmsg != NULL) {
            PyErr_SetString(PyExc_RuntimeError, errmsg);
            return NULL;
        }
    }
    else {
        faulthandler_dump_traceback(fd, tstate, 1);
    }
    Py_RETURN_NONE;
}

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

/* Handler of SIGSEGV, SIGFPE, SIGBUS and SIGILL signals.

   Display the current Python traceback and restore the previous handler. The
   previous handler will be called when the fault handler exits, because the
   fault will occur again.

   This function is signal safe and should only call signal safe functions. */

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
    if (handler == NULL) {
        /* faulthandler_nsignals == 0 (unlikely) */
        return;
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
        faulthandler_dump_traceback_threads(fd, tstate);
    else
        faulthandler_dump_traceback(fd, tstate, 1);
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
faulthandler_is_enabled(PyObject *self)
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

#ifdef FAULTHANDLER_LATER
static struct {
    PyObject *file;
    int fd;
    int delay;
    int repeat;
    int all_threads;
} fault_alarm;

/* Handler of the SIGALRM signal.

   Dump the traceback of the current thread, or of all threads if
   fault_alarm.all_threads is true. On success, register itself again if
   fault_alarm.repeat is true.

   This function is signal safe and should only call signal safe functions. */

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

        errmsg = faulthandler_dump_traceback_threads(fault_alarm.fd, tstate);
        ok = (errmsg == NULL);
    }
    else {
        faulthandler_dump_traceback(fault_alarm.fd, tstate, 1);
        ok = 1;
    }

    if (ok && fault_alarm.repeat)
        alarm(fault_alarm.delay);
    else
        /* don't call Py_CLEAR() here because it may call _Py_Dealloc() which
           is not signal safe */
        alarm(0);
}

PyObject*
faulthandler_dump_traceback_later(PyObject *self, PyObject *args, PyObject *kwargs)
{
    static char *kwlist[] = {"delay", "repeat", "file", "all_threads", NULL};
    int delay;
    PyOS_sighandler_t previous;
    int repeat = 0;
    PyObject *file = NULL;
    int all_threads = 0;
    int fd;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs,
        "i|iOi:dump_traceback_later", kwlist,
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
faulthandler_unload_dump_traceback_later()
{
    alarm(0);
    /* Don't call Py_CLEAR(fault_alarm.file): this function is called too late,
       by Py_AtExit(). Destroy a Python object here raise strange errors. */
}

PyObject*
faulthandler_cancel_dump_traceback_later_py(PyObject *self)
{
    alarm(0);
    Py_CLEAR(fault_alarm.file);
    Py_RETURN_NONE;
}
#endif

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

/* Handler of user signals (e.g. SIGUSR1).

   Dump the traceback of the current thread, or of all threads if
   fault_alarm.all_threads is true.

   This function is signal safe and should only call signal safe functions. */

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
        /* user_signals.nsignal == 0 (unlikely) */
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
        faulthandler_dump_traceback_threads(user->fd, tstate);
    else
        faulthandler_dump_traceback(user->fd, tstate, 1);
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
        /* Don't call Py_DECREF(user->file): this function is called too late,
           by Py_AtExit(). Destroy a Python object here raise strange
           errors. */
    }
    user_signals.nsignal = 0;
    free(user_signals.signals);
    user_signals.signals = NULL;
}

PyObject *
faulthandler_sigsegv(PyObject *self, PyObject *args)
{
    int *x = NULL, y;
    int release_gil = 0;
    if (!PyArg_ParseTuple(args, "|i", &release_gil))
        return NULL;
    if (release_gil) {
        Py_BEGIN_ALLOW_THREADS
        y = *x;
        Py_END_ALLOW_THREADS
    } else
        y = *x;
    return PyLong_FromLong(y);

}

PyObject *
faulthandler_sigfpe(PyObject *self, PyObject *args)
{
    int x = 1, y = 0, z;
    z = x / y;
    return PyLong_FromLong(z);
}

#ifdef SIGBUS
PyObject *
faulthandler_sigbus(PyObject *self, PyObject *args)
{
    while(1)
        raise(SIGBUS);
    Py_RETURN_NONE;
}
#endif

#ifdef SIGILL
PyObject *
faulthandler_sigill(PyObject *self, PyObject *args)
{
    while(1)
        raise(SIGILL);
    Py_RETURN_NONE;
}
#endif

/*
 * Fault handler for SIGSEGV, SIGFPE, SIGBUS and SIGILL signals: display the
 * Python traceback and restore the previous handler. Allocate an alternate
 * stack for this handler, if sigaltstack() is available, to be able to
 * allocate memory on the stack, even on stack overflow.
 */

#define VERSION 0x106

/* Forward */
static void faulthandler_unload(void);

PyDoc_STRVAR(module_doc,
"faulthandler module.");

static PyMethodDef module_methods[] = {
    {"enable",
     (PyCFunction)faulthandler_enable, METH_VARARGS|METH_KEYWORDS,
     PyDoc_STR("enable(file=sys.stderr, all_threads=False): "
               "enable the fault handler")},
    {"disable", (PyCFunction)faulthandler_disable_py, METH_NOARGS,
     PyDoc_STR("disable(): disable the fault handler")},
    {"is_enabled", (PyCFunction)faulthandler_is_enabled, METH_NOARGS,
     PyDoc_STR("is_enabled()->bool: check if the handler is enabled")},
    {"dump_traceback",
     (PyCFunction)faulthandler_dump_traceback_py, METH_VARARGS|METH_KEYWORDS,
     PyDoc_STR("dump_traceback(file=sys.stderr, all_threads=False): "
               "dump the traceback of the current thread, or of all threads "
               "if all_threads is True, into file")},
#ifdef FAULTHANDLER_LATER
    {"dump_traceback_later",
     (PyCFunction)faulthandler_dump_traceback_later, METH_VARARGS|METH_KEYWORDS,
     PyDoc_STR("dump_traceback_later(delay, repeat=False, file=sys.stderr, all_threads=False): "
               "dump the traceback of the current thread, or of all threads "
               "if all_threads is True, in delay seconds, or each delay "
               "seconds if repeat is True.")},
    {"cancel_dump_traceback_later",
     (PyCFunction)faulthandler_cancel_dump_traceback_later_py, METH_NOARGS,
     PyDoc_STR("cancel_dump_traceback_later(): cancel the previous call "
               "to dump_traceback_later().")},
#endif

    {"register",
     (PyCFunction)faulthandler_register, METH_VARARGS|METH_KEYWORDS,
     PyDoc_STR("register(signum, file=sys.stderr, all_threads=False): "
               "register an handler for the signal 'signum': dump the "
               "traceback of the current thread, or of all threads if "
               "all_threads is True, into file")},
    {"unregister",
     faulthandler_unregister_py, METH_VARARGS|METH_KEYWORDS,
     PyDoc_STR("unregister(signum): unregister the handler of the signal "
                "'signum' registered by register()")},

    {"sigsegv", faulthandler_sigsegv, METH_VARARGS,
     PyDoc_STR("sigsegv(release_gil=False): raise a SIGSEGV signal")},
    {"sigfpe", (PyCFunction)faulthandler_sigfpe, METH_NOARGS,
     PyDoc_STR("sigfpe(): raise a SIGFPE signal")},
#ifdef SIGBUS
    {"sigbus", (PyCFunction)faulthandler_sigbus, METH_NOARGS,
     PyDoc_STR("sigbus(): raise a SIGBUS signal")},
#endif
#ifdef SIGILL
    {"sigill", (PyCFunction)faulthandler_sigill, METH_NOARGS,
     PyDoc_STR("sigill(): raise a SIGILL signal")},
#endif
    {NULL, NULL} /* terminator */
};

#if PY_MAJOR_VERSION >= 3
static struct PyModuleDef module_def = {
    PyModuleDef_HEAD_INIT,
    "faulthandler",
    module_doc,
    -1,
    module_methods,
    NULL,
    NULL,
    NULL,
    NULL
};
#endif


PyMODINIT_FUNC
#if PY_MAJOR_VERSION >= 3
PyInit_faulthandler(void)
#else
initfaulthandler(void)
#endif
{
    PyObject *m, *version;

#if PY_MAJOR_VERSION >= 3
    m = PyModule_Create(&module_def);
#else
    m = Py_InitModule3("faulthandler", module_methods, module_doc);
#endif
    if (m == NULL) {
#if PY_MAJOR_VERSION >= 3
        return NULL;
#else
        return;
#endif
    }

#ifdef HAVE_SIGALTSTACK
    /* Try to allocate an alternate stack for faulthandler() signal handler to
     * be able to allocate memory on the stack, even on a stack overflow. If it
     * fails, ignore the error. */
    faulthandler_stack.ss_flags = SS_ONSTACK;
    faulthandler_stack.ss_size = SIGSTKSZ;
    faulthandler_stack.ss_sp = PyMem_Malloc(faulthandler_stack.ss_size);
    if (faulthandler_stack.ss_sp != NULL) {
        (void)sigaltstack(&faulthandler_stack, NULL);
    }
#endif

    (void)Py_AtExit(faulthandler_unload);

#if PY_MAJOR_VERSION >= 3
    version = PyLong_FromLong(VERSION);
#else
    version = PyInt_FromLong(VERSION);
#endif
    PyModule_AddObject(m, "version", version);

#if PY_MAJOR_VERSION >= 3
    return m;
#endif
}

static void
faulthandler_unload(void)
{
#ifdef FAULTHANDLER_LATER
    faulthandler_unload_dump_traceback_later();
#endif
    faulthandler_unload_user();
    faulthandler_unload_fatal_error();
#ifdef HAVE_SIGALTSTACK
    if (faulthandler_stack.ss_sp != NULL) {
        PyMem_Free(faulthandler_stack.ss_sp);
        faulthandler_stack.ss_sp = NULL;
    }
#endif
}

