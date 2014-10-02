#include "Python.h"
#include <frameobject.h>

#if PY_MAJOR_VERSION >= 3
#  define PYSTRING_CHECK PyUnicode_Check
#else
#  define PYSTRING_CHECK PyString_Check
#endif

#define PUTS(fd, str) write(fd, str, (int)strlen(str))
#define MAX_STRING_LENGTH 500
#define MAX_FRAME_DEPTH 100
#define MAX_NTHREADS 100

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

/* Format an integer in range [0; 0xffffffff] to hexadecimal of 'width' digits,
   and write it into the file fd.

   This function is signal safe. */

static void
dump_hexadecimal(int fd, unsigned long value, int width)
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
    unsigned long ch;
#if PY_MAJOR_VERSION >= 3
    Py_UNICODE *u;

    size = PyUnicode_GET_SIZE(text);
    u = PyUnicode_AS_UNICODE(text);
#else
    char *s;

    size = PyString_GET_SIZE(text);
    s = PyString_AS_STRING(text);
#endif

    if (MAX_STRING_LENGTH < size) {
        size = MAX_STRING_LENGTH;
        truncated = 1;
    }
    else
        truncated = 0;

#if PY_MAJOR_VERSION >= 3
    for (i=0; i < size; i++, u++) {
        ch = *u;
        if (' ' <= ch && ch < 0x7f) {
            /* printable ASCII character */
            char c = (char)ch;
            write(fd, &c, 1);
        }
        else if (ch <= 0xff) {
            PUTS(fd, "\\x");
            dump_hexadecimal(fd, ch, 2);
        }
        else
#ifdef Py_UNICODE_WIDE
        if (ch <= 0xffff)
#endif
        {
            PUTS(fd, "\\u");
            dump_hexadecimal(fd, ch, 4);
#ifdef Py_UNICODE_WIDE
        }
        else {
            PUTS(fd, "\\U");
            dump_hexadecimal(fd, ch, 8);
#endif
        }
    }
#else
    for (i=0; i < size; i++, s++) {
        ch = *s;
        if (' ' <= ch && ch <= 126) {
            /* printable ASCII character */
            write(fd, s, 1);
        }
        else {
            PUTS(fd, "\\x");
            dump_hexadecimal(fd, ch, 2);
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
    lineno = PyCode_Addr2Line(code, frame->f_lasti);
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

static void
dump_traceback(int fd, PyThreadState *tstate, int write_header)
{
    PyFrameObject *frame;
    unsigned int depth;

    if (write_header)
        PUTS(fd, "Stack (most recent call first):\n");

    frame = _PyThreadState_GetFrame(tstate);
    if (frame == NULL)
        return;

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
_Py_DumpTraceback(int fd, PyThreadState *tstate)
{
    dump_traceback(fd, tstate, 1);
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
    dump_hexadecimal(fd, (unsigned long)tstate->thread_id, sizeof(unsigned long)*2);
    PUTS(fd, " (most recent call first):\n");
}

/* Dump the traceback of all threads. Return NULL on success, or an error
   message on error.

   This function is signal safe. */

const char*
_Py_DumpTracebackThreads(int fd,
                         PyInterpreterState *interp,
                         PyThreadState *current_thread)
{
    PyThreadState *tstate;
    unsigned int nthreads;

    /* Get the current interpreter from the current thread */
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
        dump_traceback(fd, tstate, 0);
        tstate = PyThreadState_Next(tstate);
        nthreads++;
    } while (tstate != NULL);

    return NULL;
}

