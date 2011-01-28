#include "faulthandler.h"
#include <frameobject.h>

#if PY_MAJOR_VERSION >= 3
#  define PYSTRING_CHECK PyUnicode_Check
#else
#  define PYSTRING_CHECK PyString_Check
#endif

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
   and write it into the file fd */

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
   and write it into the file fd */

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

/* Write an unicode object into the file fd using ascii+backslashreplace */

static void
dump_ascii(int fd, PyObject *text)
{
    Py_ssize_t i, size;
#if PY_MAJOR_VERSION >= 3
    Py_UNICODE *u;
    char c;

    size = PyUnicode_GET_SIZE(text);
    u = PyUnicode_AS_UNICODE(text);
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
    char *s;
    unsigned char c;

    size = PyString_GET_SIZE(text);
    s = PyString_AS_STRING(text);
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
}

/* Write a frame into the file fd: "File "xxx", line xxx in xxx" */

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

/* Write the current Python backtrace into the file 'fd':

   Traceback (most recent call first):
     File "xxx", line xxx in <xxx>
     File "xxx", line xxx in <xxx>
     ...
     File "xxx", line xxx in <xxx>

   Write only the first MAX_FRAME_DEPTH frames. If the traceback is truncated, write
   the line "  ...".
 */

static void
dump_backtrace(int fd, PyThreadState *tstate, int write_header)
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

/* Function used by the signal handler, faulthandler(). */
void
faulthandler_dump_backtrace(int fd)
{
    PyThreadState *tstate;
    static int running = 0;

    if (running) {
        /* Error: recursive call, do nothing. It may occurs if Py_FatalError()
           is called (eg. by find_key()). */
        return;
    }
    running = 1;

    /* SIGSEGV, SIGFPE, SIGBUS and SIGILL are synchronous signals and so are
       delivered to the thread that caused the fault. Get the Python thread
       state of the current thread.

       PyThreadState_Get() doesn't give the state of the thread that caused the
       fault if the thread released the GIL, and so this function cannot be
       used. Read the thread local storage (TLS) instead: call
       PyGILState_GetThisThreadState(). */
    tstate = PyGILState_GetThisThreadState();
    if (tstate == NULL)
        goto error;

    dump_backtrace(fd, tstate, 1);

error:
    running = 0;
}

/*
 * Call file.flush() and return file.fileno(). Use sys.stdout if file is not
 * set (NULL). Set an exception and return -1 on error.
 */
static int
get_fileno(PyObject *file)
{
    PyObject *result;
    long fd_long;
    long fd;

    if (file == NULL) {
        file = PySys_GetObject("stdout");
        if (file == NULL) {
            PyErr_SetString(PyExc_RuntimeError, "unable to get sys.stdout");
            return -1;
        }
    }

    result = PyObject_CallMethod(file, "fileno", "");
    if (result == NULL)
        return -1;

    fd = -1;
    if (PyInt_Check(result)) {
        fd_long = PyInt_AsLong(result);
        if (0 < fd_long && fd_long < INT_MAX)
            fd = (int)fd_long;
    }
    Py_DECREF(result);

    if (fd == -1) {
        PyErr_SetString(PyExc_RuntimeError,
                        "sys.stdout.fileno() is not a valid file descriptor");
        return -1;
    }

    result = PyObject_CallMethod(file, "flush", "");
    if (result != NULL)
        Py_DECREF(result);
    else
        PyErr_Clear();

    return fd;
}

PyObject*
faulthandler_dump_backtrace_py(PyObject *self, PyObject *args)
{
    PyObject *file = NULL;
    int fd;
    PyThreadState *tstate;

    if (!PyArg_ParseTuple(args, "|O:dump_backtrace", &file))
        return NULL;

    fd = get_fileno(file);
    if (fd == -1)
        return NULL;

    /* The caller holds the GIL and so PyThreadState_Get() can be used */
    tstate = PyThreadState_Get();
    if (tstate == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "unable to get the thread state");
        return NULL;
    }
    dump_backtrace(fd, tstate, 1);
    Py_RETURN_NONE;
}

static void
write_thread_id(int fd, PyThreadState *tstate,
                unsigned int local_id, int is_current)
{
    if (is_current)
        PUTS(fd, "Current thread #");
    else
        PUTS(fd, "Thread #");
    dump_decimal(fd, local_id);
    PUTS(fd, " (0x");
    dump_hexadecimal(sizeof(long)*2, (unsigned long)tstate->thread_id, fd);
    PUTS(fd, "):\n");
}

/*
 * Dump the backtrace of all threads.
 *
 * Return NULL on success, or an error message on error.
 */
const char*
faulthandler_dump_backtrace_threads(int fd, PyThreadState *current_thread)
{
    PyInterpreterState *interp;
    PyThreadState *tstate;
    int newline;
    unsigned int local_id;

    /* Get the current interpreter from the current thread */
    interp = current_thread->interp;
    if (interp == NULL)
        return "unable to get the interpreter";

    tstate = PyInterpreterState_ThreadHead(interp);
    if (tstate == NULL)
        return "unable to get the thread head state";

    /* Count the number of threads */
    local_id = 0;
    do
    {
        local_id++;
        tstate = PyThreadState_Next(tstate);
    } while (tstate != NULL);

    /* Dump the backtrace of each thread */
    tstate = PyInterpreterState_ThreadHead(interp);
    newline = 0;
    do
    {
        if (newline)
            write(fd, "\n", 1);
        else
            newline = 1;
        write_thread_id(fd, tstate, local_id, tstate == current_thread);
        dump_backtrace(fd, tstate, 0);
        tstate = PyThreadState_Next(tstate);
        local_id--;
    } while (tstate != NULL);

    return NULL;
}

PyObject*
faulthandler_dump_backtrace_threads_py(PyObject *self, PyObject *args)
{
    PyObject *file = NULL;
    int fd;
    PyThreadState *current_thread;
    const char *errmsg;

    if (!PyArg_ParseTuple(args, "|O:dump_backtrace_threads", &file))
        return NULL;

    fd = get_fileno(file);
    if (fd == -1)
        return NULL;

    /* The caller holds the GIL and so PyThreadState_Get() can be used */
    current_thread = PyThreadState_Get();
    if (current_thread == NULL) {
        PyErr_SetString(PyExc_RuntimeError,
                        "unable to get the current thread state");
        return NULL;
    }

    errmsg = faulthandler_dump_backtrace_threads(fd, current_thread);
    if (errmsg != NULL) {
        PyErr_SetString(PyExc_RuntimeError, errmsg);
        return NULL;
    }
    Py_RETURN_NONE;
}

