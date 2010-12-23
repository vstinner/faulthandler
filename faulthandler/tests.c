#include "faulthandler.h"

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

#ifdef FAULTHANDLER_HAVE_SIGBUS
PyObject *
faulthandler_sigbus(PyObject *self, PyObject *args)
{
    pid_t pid = getpid();
    while(1)
        kill(pid, SIGBUS);
    Py_RETURN_NONE;
}
#endif

#ifdef FAULTHANDLER_HAVE_SIGILL
PyObject *
faulthandler_sigill(PyObject *self, PyObject *args)
{
    pid_t pid = getpid();
    while(1)
        kill(pid, SIGILL);
    Py_RETURN_NONE;
}
#endif

