+++++++++++++
Fault handler
+++++++++++++

.. image:: https://img.shields.io/pypi/v/faulthandler.svg
   :alt: Latest release on the Python Cheeseshop (PyPI)
   :target: https://pypi.python.org/pypi/faulthandler

.. image:: https://travis-ci.org/haypo/faulthandler.svg?branch=master
   :alt: Build status of faulthandler on Travis CI
   :target: https://travis-ci.org/haypo/faulthandler

Fault handler for SIGSEGV, SIGFPE, SIGABRT, SIGBUS and SIGILL signals: display
the Python traceback and restore the previous handler. Allocate an alternate
stack for this handler, if sigaltstack() is available, to be able to allocate
memory on the stack, even on stack overflow (not available on Windows).

Import the module and call faulthandler.enable() to enable the fault handler.

Alternatively you can set the PYTHONFAULTHANDLER environment variable to a
non-empty value.

The fault handler is called on catastrophic cases and so it can only use
signal-safe functions (eg. it doesn't allocate memory on the heap). That's why
the traceback is limited: it only supports ASCII encoding (use the
backslashreplace error handler for non-ASCII characters) and limits each string
to 100 characters, doesn't print the source code in the traceback (only the
filename, the function name and the line number), is limited to 100 frames and
100 threads.

By default, the Python traceback is written to the standard error stream. Start
your graphical applications in a terminal and run your server in foreground to
see the traceback, or pass a file to faulthandler.enable().

faulthandler is implemented in C using signal handlers to be able to dump a
traceback on a crash or when Python is blocked (eg. deadlock).

Website:
https://faulthandler.readthedocs.io/

faulthandler is part of Python since Python 3.3:
http://docs.python.org/dev/library/faulthandler.html

