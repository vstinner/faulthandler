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
dump_decimal(int value, int fd)
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
dump_hexadecimal(int width, unsigned int value, int fd)
{
    const char *hexdigits = "0123456789abcdef";
    char buffer[9];
    int len;
    if (0xffffffffU < value)
        return;
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
dump_ascii(PyObject *text, int fd)
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
            PUTS("\\x", fd);
            dump_hexadecimal(2, *u, fd);
        }
        else
#ifdef Py_UNICODE_WIDE
        if (*u < 65536)
#endif
        {
            PUTS("\\u", fd);
            dump_hexadecimal(4, *u, fd);
#ifdef Py_UNICODE_WIDE
        }
        else {
            PUTS("\\U", fd);
            dump_hexadecimal(8, *u, fd);
#endif
        }
    }
#else
    char *s;

    size = PyString_GET_SIZE(text);
    s = PyString_AS_STRING(text);
    for (i=0; i < size; i++, s++) {
        if (*s < 128) {
            write(fd, s, 1);
        }
        else {
            PUTS("\\x", fd);
            dump_hexadecimal(2, *s, fd);
        }
    }
#endif
}

/* Write a frame into the file fd: "File "xxx", line xxx in xxx" */

static void
dump_frame(PyFrameObject *frame, int fd)
{
    PyCodeObject *code;
    int lineno;

    code = frame->f_code;
    PUTS("  File ", fd);
    if (code != NULL && code->co_filename != NULL
        && PYSTRING_CHECK(code->co_filename))
    {
        write(fd, "\"", 1);
        dump_ascii(code->co_filename, fd);
        write(fd, "\"", 1);
    } else {
        PUTS("???", fd);
    }

#if (PY_MAJOR_VERSION <= 2 && PY_MINOR_VERSION < 7) \
||  (PY_MAJOR_VERSION >= 3 && PY_MINOR_VERSION < 2)
    /* PyFrame_GetLineNumber() was introduced in Python 2.7.0 and 3.2.0 */
    lineno = PyCode_Addr2Line(frame->f_code, frame->f_lasti);
#else
    lineno = PyFrame_GetLineNumber(frame);
#endif
    PUTS(", line ", fd);
    dump_decimal(lineno, fd);
    PUTS(" in ", fd);

    if (code != NULL && code->co_name != NULL
        && PYSTRING_CHECK(code->co_name))
        dump_ascii(code->co_name, fd);
    else
        PUTS("???", fd);

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
faulthandler_dump_backtrace(int fd)
{
    PyThreadState *tstate;
    PyFrameObject *frame;
    unsigned int depth;
    static int running = 0;

    if (!faulthandler_enabled)
        return;

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

    frame = _PyThreadState_GetFrame(tstate);
    if (frame == NULL)
        goto error;

    PUTS("Traceback (most recent call first):\n", fd);
    depth = 0;
    while (frame != NULL) {
        if (MAX_FRAME_DEPTH <= depth) {
            PUTS("  ...\n", fd);
            break;
        }
        if (!PyFrame_Check(frame))
            break;
        dump_frame(frame, fd);
        frame = frame->f_back;
        depth++;
    }
error:
    running = 0;
}

