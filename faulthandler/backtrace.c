#include "faulthandler.h"
#include <frameobject.h>

#if PY_MAJOR_VERSION >= 3
#  define PYSTRING_CHECK PyUnicode_Check
#  define PYINT_CHECK PyLong_Check
#  define PYINT_ASLONG PyLong_AsLong
#else
#  define PYSTRING_CHECK PyString_Check
#  define PYINT_CHECK PyInt_Check
#  define PYINT_ASLONG PyInt_AsLong
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

void
faulthandler_dump_backtrace(int fd, PyThreadState *tstate, int write_header)
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
    unsigned int nthreads;

    /* Get the current interpreter from the current thread */
    interp = current_thread->interp;
    if (interp == NULL)
        return "unable to get the interpreter";

    tstate = PyInterpreterState_ThreadHead(interp);
    if (tstate == NULL)
        return "unable to get the thread head state";

    /* Dump the backtrace of each thread */
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
        faulthandler_dump_backtrace(fd, tstate, 0);
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
faulthandler_dump_backtrace_py(PyObject *self,
                               PyObject *args, PyObject *kwargs)
{
    static char *kwlist[] = {"file", "all_threads", NULL};
    PyObject *file = NULL;
    int all_threads = 0;
    PyThreadState *tstate;
    const char *errmsg;
    int fd;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs,
        "|Oi:dump_backtrace", kwlist,
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
        errmsg = faulthandler_dump_backtrace_threads(fd, tstate);
        if (errmsg != NULL) {
            PyErr_SetString(PyExc_RuntimeError, errmsg);
            return NULL;
        }
    }
    else {
        faulthandler_dump_backtrace(fd, tstate, 1);
    }
    Py_RETURN_NONE;
}

