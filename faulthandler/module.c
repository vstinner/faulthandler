/*
 * Fault handler for SIGSEGV, SIGFPE, SIGBUS and SIGILL signals: display the
 * Python backtrace and restore the previous handler. Allocate an alternate
 * stack for this handler, if sigaltstack() is available, to be able to
 * allocate memory on the stack, even on stack overflow.
 */

#include "faulthandler.h"

#define VERSION 0x104

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
    {"isenabled", (PyCFunction)faulthandler_isenabled, METH_NOARGS,
     PyDoc_STR("isenabled()->bool: check if the handler is enabled")},
    {"dumpbacktrace",
     (PyCFunction)faulthandler_dump_backtrace_py, METH_VARARGS|METH_KEYWORDS,
     PyDoc_STR("dumpbacktrace(file=sys.stderr, all_threads=False): "
               "dump the backtrace of the current thread, or of all threads "
               "if all_threads is True, into file")},
#ifdef FAULTHANDLER_LATER
    {"dumpbacktrace_later",
     (PyCFunction)faulthandler_dumpbacktrace_later, METH_VARARGS|METH_KEYWORDS,
     PyDoc_STR("dumpbacktrace_later(delay, repeat=False, file=sys.stderr, all_threads=False): "
               "dump the backtrace of the current thread, or of all threads "
               "if all_threads is True, in delay seconds, or each delay "
               "seconds if repeat is True.")},
    {"cancel_dumpbacktrace_later",
     (PyCFunction)faulthandler_cancel_dumpbacktrace_later_py, METH_NOARGS,
     PyDoc_STR("cancel_dumpbacktrace_later(): cancel the previous call "
               "to dumpbacktrace_later().")},
#endif

    {"register",
     (PyCFunction)faulthandler_register, METH_VARARGS|METH_KEYWORDS,
     PyDoc_STR("register(signum, file=sys.stderr, all_threads=False): "
               "register an handler for the signal 'signum': dump the "
               "backtrace of the current thread, or of all threads if "
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
    faulthandler_cancel_dumpbacktrace_later();
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

