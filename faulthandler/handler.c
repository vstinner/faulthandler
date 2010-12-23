#include "faulthandler.h"
#include <frameobject.h>
#include <code.h>
#include <signal.h>
#include <unistd.h>

#ifdef HAVE_SIGACTION
typedef struct sigaction _Py_sighandler_t;
#else
typedef PyOS_sighandler_t _Py_sighandler_t;
#endif

int faulthandler_enabled = 0;

typedef struct {
    int signum;
    int enabled;
    const char* name;
    _Py_sighandler_t previous;
} fault_handler_t;

static int fault_signals[] = {
#ifdef SIGBUS
    SIGBUS,
#endif
#ifdef SIGILL
    SIGILL,
#endif
    SIGFPE,
    /* define SIGSEGV at the end to make it the default choice if searching the
       handler fails in faulthandler() */
    SIGSEGV
};
#define NFAULT_SIGNALS (sizeof(fault_signals) / sizeof(fault_signals[0]))
static fault_handler_t fault_handlers[NFAULT_SIGNALS];

/* Fault handler: display the current Python backtrace and restore the previous
   handler. It should only use signal-safe functions. The previous handler will
   be called when the fault handler exits, because the fault will occur
   again. */

static void
faulthandler(int signum)
{
    const int fd = 2; /* should be fileno(stderr) */
    unsigned int i;
    fault_handler_t *handler;

    /* restore the previous handler */
    for (i=0; i < NFAULT_SIGNALS; i++) {
        handler = &fault_handlers[i];
        if (handler->signum == signum)
            break;
    }
#ifdef HAVE_SIGACTION
    (void)sigaction(handler->signum, &handler->previous, NULL);
#else
    (void)signal(handler->signum, handler->previous);
#endif
    handler->enabled = 0;

    PUTS("Fatal Python error: ", fd);
    PUTS(handler->name, fd);
    PUTS("\n\n", fd);

    faulthandler_dump_backtrace(fd);
}

void
faulthandler_enable(void)
{
    unsigned int i;
    fault_handler_t *handler;
#ifdef HAVE_SIGACTION
    struct sigaction action;
    int err;
#endif

    if (faulthandler_enabled)
        return;
    faulthandler_enabled = 1;

#ifdef HAVE_SIGALTSTACK
    /* Try to allocate an alternate stack for faulthandler() signal handler to
     * be able to allocate memory on the stack, even on a stack overflow. If it
     * fails, ignore the error. */
    stack.ss_flags = SS_ONSTACK;
    stack.ss_size = SIGSTKSZ;
    stack.ss_sp = PyMem_Malloc(stack.ss_size);
    if (stack.ss_sp != NULL)
        (void)sigaltstack(&stack, NULL);
#endif

    for (i=0; i < NFAULT_SIGNALS; i++) {
        handler = &fault_handlers[i];
        handler->signum = fault_signals[i];;
        handler->enabled = 0;
        if (handler->signum == SIGFPE)
            handler->name = "Floating point exception";
#ifdef SIGBUS
        else if (handler->signum == SIGBUS)
            handler->name = "Bus error";
#endif
#ifdef SIGILL
        else if (handler->signum == SIGILL)
            handler->name = "Illegal instruction";
#endif
        else
            handler->name = "Segmentation fault";
    }

    for (i=0; i < NFAULT_SIGNALS; i++) {
        handler = &fault_handlers[i];
#ifdef HAVE_SIGACTION
        action.sa_handler = faulthandler;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_ONSTACK;
        err = sigaction(handler->signum, &action, &handler->previous);
        if (!err)
            handler->enabled = 1;
#else
        handler->previous = signal(handler->signum, faulthandler);
        if (handler->previous != SIG_ERR)
            handler->enabled = 1;
#endif
    }
    return;
}

PyObject*
faulthandler_enable_method(PyObject *self)
{
    faulthandler_enable();
    Py_RETURN_NONE;
}

static void
faulthandler_disable(void)
{
    unsigned int i;
    fault_handler_t *handler;

    if (!faulthandler_enabled)
        return;
    faulthandler_enabled = 0;

    for (i=0; i < NFAULT_SIGNALS; i++) {
        handler = &fault_handlers[i];
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

PyObject*
faulthandler_disable_method(PyObject *self)
{
    faulthandler_disable();
    Py_RETURN_NONE;
}

#if 0
static void
faulthandler_unload(void)
{
#ifdef HAVE_SIGALTSTACK
    if (stack.ss_sp != NULL)
        PyMem_Free(stack.ss_sp);
#endif
}
#endif

