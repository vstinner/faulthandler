+++++++++++++
Fault handler
+++++++++++++

.. image:: llama.jpg
   :alt: Llama
   :align: right
   :target: http://www.flickr.com/photos/haypo/7199652438/

Fault handler for SIGSEGV, SIGFPE, SIGABRT, SIGBUS and SIGILL signals: display
the Python traceback and restore the previous handler. Allocate an alternate
stack for this handler, if sigaltstack() is available, to be able to allocate
memory on the stack, even on stack overflow (not available on Windows).

Import the module and call ``faulthandler.enable()`` to enable the fault handler.

You can also enable it at startup by setting the PYTHONFAULTHANDLER environment
variable.

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

faulthandler works on Python 2.6-3.5. It is part of Python standard library
since Python 3.3: `faulthandler module
<http://docs.python.org/dev/library/faulthandler.html>`_

* `faulthandler website <http://faulthandler.readthedocs.org/>`_
  (this page)
* `faulthandler project at github
  <https://github.com/haypo/faulthandler/>`_: source code, bug tracker
* `faulthandler at Python Cheeshop (PyPI)
  <http://pypi.python.org/pypi/faulthandler/>`_
* Article: `New faulthandler module in Python 3.3 helps debugging
  <http://blog.python.org/2011/05/new-faulthandler-module-in-python-33.html>`_


Example
=======

Example of a segmentation fault on Linux: ::

    $ python
    >>> import faulthandler
    >>> faulthandler.enable()
    >>> faulthandler._sigsegv()
    Fatal Python error: Segmentation fault

    Traceback (most recent call first):
      File "<stdin>", line 1 in <module>
    Segmentation fault


Nosetests and py.test
=====================

To use faulthandler in `nose tests <https://nose.readthedocs.org/en/latest/>`_ or `py.test <http://pytest.org/latest/>`_, you can use `nose-faulthandler <https://nose.readthedocs.org/en/latest/>`_ or `pytest-faulthandler <https://github.com/nicoddemus/pytest-faulthandler>`_ plugins.


Installation
============

faulthandler supports Python 2.6, 2.7 and 3.2. It may also support Python 2.5
and 3.1, but these versions are no more officially supported.

Install faulthandler on Windows using pip
-----------------------------------------

Procedure to install faulthandler on Windows:

* `Install pip
  <http://www.pip-installer.org/en/latest/installing.html>`_: download
  ``get-pip.py`` and type::

  \Python27\python.exe get-pip.py

* If you already have pip, ensure that you have at least pip 1.4 (to support
  wheel packages). If you need to upgrade::

  \Python27\python.exe -m pip install -U pip

* Install faulthandler::

  \Python27\python.exe -m pip install faulthandler

.. note::

   Only wheel packages for Python 2.7 are currently distributed on the
   Cheeseshop (PyPI). If you need wheel packages for other Python versions,
   please ask.


Linux packages
--------------

==================  ===================
Linux distribution  Package name
==================  ===================
Debian              python-faulthandler
OpenSuSE            python-faulthandler
PLD Linux           python-faulthandler
Ubuntu              python-faulthandler
==================  ===================

Some links:

* `Debian python-faulthandler package
  <https://packages.debian.org/sid/python-faulthandler>`_
* `Ubuntu faulthandler source package
  <http://packages.ubuntu.com/source/precise/faulthandler>`_


pythonxy (Windows)
------------------

faulthandler is part of `pythonxy distribution
<http://code.google.com/p/pythonxy/>`_: free scientific and engineering
development software for Windows.


Install from source code
------------------------

Download the latest tarball from the `Python Cheeseshop (PyPI)
<http://pypi.python.org/pypi/faulthandler/>`_.

To install faulthandler module, type the following command: ::

    python setup.py install

Then you can test your setup using the following command: ::

    python tests.py

You need a C compiler (eg. gcc) and Python headers to build the faulthandler
module. Eg. on Fedora, you have to install python-devel package (sudo yum
install python-devel).


faulthandler module API
=======================

There are 4 different ways to display the Python traceback:

* enable(): on a crash
* dump_traceback_later(): after a timeout (useful if your program hangs)
* register(): by sending a signal (eg. SIGUSR1). It doesn't work on Windows.
* dump_traceback(): explicitly

Fault handler state (disabled by default):

* enable(file=sys.stderr, all_threads=False): enable the fault handler
* disable(): disable the fault handler
* is_enabled(): get the status of the fault handler

Dump the current traceback:

* dump_traceback(file=sys.stderr, all_threads=False): dump traceback of the
  current thread, or of all threads if all_threads is True, into file
* dump_traceback_later(timeout, repeat=False, file=sys.stderr,
  exit=False): dump the traceback of all threads in timeout seconds, or each
  timeout seconds if repeat is True. If the function is called twice, the new
  call replaces previous parameters. Exit immediatly if exit is True.
* cancel_dump_traceback_later(): cancel the previous call to
  dump_traceback_later()

dump_traceback_later() is implemented using the SIGALRM signal and the alarm()
function: if the signal handler is called during a system call, the system call
is interrupted (return EINTR). It it not available on Windows.

enable() and dump_traceback_later() keep an internal reference to the output
file. Use disable() and cancel_dump_traceback_later() to clear this reference.

Dump the traceback on an user signal:

* register(signum, file=sys.stderr, all_threads=False, chain=False): register
  an handler for the signal 'signum': dump the traceback of the current
  thread, or of all threads if all_threads is True, into file". Call the
  previous handler if chain is ``True``. Not available on Windows.
* unregister(signum): unregister the handler of the signal 'signum' registered
  by register(). Not available on Windows.

Functions to test the fault handler:

* ``_fatal_error(message)``: Exit Python with a fatal error, call Py_FatalError()
  with message.
* ``_read_null()``: read from the NULL pointer (raise SIGSEGV or SIGBUS depending
  on the platform)
* ``_sigabrt()``: raise a SIGABRT signal (Aborted)
* ``_sigbus()``: raise a SIGBUS signal (Bus error)
* ``_sigfpe()``: raise a SIGFPE signal (Floating point exception), do a division by
  zero
* ``_sigill()``: raise a SIGILL signal (Illegal instruction)
* ``_sigsegv()``: raise a SIGSEGV signal (Segmentation fault), read memory from
  NULL (address 0)
* ``_stack_overflow()``: raise a stack overflow error. Not available on all
  platforms.

register(), unregister(), sigbus() and sigill() are not available on all
operation systems.

faulthandler.version_info is the module version as a tuple: (major, minor),
faulthandler.__version__ is the module version as a string (e.g. "2.0").


Changelog
=========

Version 2.5
-----------

* Issue #23433: Fix faulthandler._stack_overflow(). Fix undefined behaviour:
  don't compare pointers. Use Py_uintptr_t type instead of void*. It fixes
  test_faulthandler on Fedora 22 which now uses GCC 5.
* Drop support and Python 2.5 and 3.1: no Linux distribution use it anymore,
  and it becomes difficult to test them.
* Add tox.ini to run tests with tox: it creates a virtual environment, compile
  and install faulthandler, and run unit tests.
* Add support for the PYTHONFAULTHANDLER environment variable.

Version 2.4 (2014-10-02)
------------------------

* Add a new documentation written with Sphinx used to built a new website:
  http://faulthandler.readthedocs.org/
* Python issue #19306: Add extra hints to faulthandler stack dumps that they
  are upside down.
* Python issue #15463: the faulthandler module truncates strings to 500
  characters, instead of 100, to be able to display long file paths.
* faulthandler issue #7: Ignore Windows SDK message "This application has
  requested the Runtime to terminate it in an unusual way. (...)" in
  test_fatal_error(). It was not a bug in faulthandler, just an issue with
  the unit test on some Windows setup.
* Python issue #21497: faulthandler functions now raise a better error if
  ``sys.stderr`` is ``None``: RuntimeError("sys.stderr is None") instead of
  AttributeError("'NoneType' object has no attribute 'fileno'").
* Suppress crash reporter in tests. For example, avoid popup on Windows and
  don't generate a core dump on Linux.


Version 2.3 (2013-12-17)
------------------------

* faulthandler.register() now keeps the previous signal handler when the
  function is called twice, so faulthandler.unregister() restores correctly
  the original signal handler.

Version 2.2 (2013-03-19)
------------------------

* Rename dump_tracebacks_later() to dump_traceback_later():
  use the same API than the faulthandler module of Python 3.3
* Fix handling of errno variable in the handler of user signals
* Fix the handler of user signals: chain the previous signal
  handler even if getting the current thread state failed

Version 2.1 (2012-02-05)
------------------------

Major changes:

* Add an optional chain argument to faulthandler.register()

Minor changes:

* Fix faulthandler._sigsegv() for Clang 3.0
* Fix compilation on Visual Studio

Version 2.0 (2011-05-10)
------------------------

Major changes:

* faulthandler is now part of Python 3.3
* enable() handles also the SIGABRT signal
* Add exit option to dump_traceback_later(): if True, exit the program
  on timeout after dumping the traceback

Other changes:

* Change default value of the all_threads argument: dump all threads by
  default because under some rare conditions, it is not possible to get
  the current thread
* Save/restore errno in signal handlers
* dump_traceback_later() always dump all threads: remove all_threads option
* Add faulthandler.__version__ attribute (module version as a string)
* faulthandler.version is now a tuple
* Rename:

  * dump_traceback_later() to dump_traceback_later()
  * cancel_dump_traceback_later() to cancel_dump_traceback_later()
  * sigsegv() to _sigsegv()
  * sigfpe() to _sigfpe()
  * sigbus() to _sigbus()
  * sigill() to _sigill()

* register() and unregister() are no more available on Windows. They were
  useless: only SIGSEGV, SIGABRT and SIGILL can be handled by the application,
  and these signals can only be handled by enable().
* Add _fatal_error(), _read_null(), _sigabrt() and _stack_overflow() test
  functions
* register() uses sigaction() SA_RESTART flag to try to not interrupt the
  current system call
* The fault handler calls the previous signal handler, using sigaction()
  SA_NODEFER flag to call it immediatly
* enable() raises an OSError if it was not possible to register a signal
  handler
* Set module size to 0, instead of -1, to be able to unload the module with
  Python 3
* Fix a reference leak in dump_traceback_later()
* Fix register() if it called twice with the same signal
* Implement m_traverse for Python 3 to help the garbage collector
* Move code from faulthandler/\*.c to faulthandler.c and traceback.c: the code
  is simpler and it was easier to integrate faulthandler into Python 3.3 using
  one file (traceback.c already existed in Python)
* register() uses a static list for all signals instead of reallocating memory
  each time a new signal is registered, because the list is shared with the
  signal handler which may be called anytime.

Version 1.5 (2011-03-24)
------------------------

* Conform to the PEP 8:

  * Rename isenabled() to is_enabled()
  * Rename dumpbacktrace() to dump_traceback()
  * Rename dumpbacktrace_later() to dump_traceback_later()
  * Rename cancel_dumpbacktrace_later() to cancel_dump_traceback_later()

* Limit strings to 100 characters
* dump_traceback_later() signal handler doesn't clear its reference to the
  file, because Py_CLEAR() is not signal safe: you have to call explicitly
  cancel_dump_traceback_later()

Version 1.4 (2011-02-14)
------------------------

* Add register() and unregister() functions
* Add optional all_threads argument to enable()
* Limit the backtrace to 100 threads
* Allocate an alternative stack for the fatal signal handler to be able to
  display a backtrace on a stack overflow (define HAVE_SIGALTSTACK). Not
  available on Windows.

Version 1.3 (2011-01-31)
------------------------

* Don't compile dumpbacktrace_later() and cancel_dumpbacktrace_later() on
  Windows because alarm() is missing

Version 1.2 (2011-01-31)
------------------------

* Add dumpbacktrace_later() and cancel_dumpbacktrace_later() function
* enable() and dumpbacktrace() get an optional file argument
* Replace dumpbacktrace_threads() function by a new dumpbacktrace() argument:
  dumpbacktrace(all_threads=True)
* enable() gets the file descriptor of sys.stderr instead of using the file
  descriptor 2

Version 1.1 (2011-01-03)
------------------------

* Disable the handler by default, because pkgutil may load the module and so
  enable the handler which is unexpected
* Add dumpbacktrace() and dumpbacktrace_threads() functions
* sigill() is available on Windows thanks to Martin's patch
* Fix dump_ascii() for signed char type (eg. on FreeBSD)
* Fix tests.py for Python 2.5

Version 1.0 (2010-12-24)
------------------------

  First public release


Similar projects
================

Python debuggers:

* `minidumper <https://bitbucket.org/briancurtin/minidumper/>`_
  is a C extension for writing "minidumps" for post-mortem analysis of crashes
  in Python or its extensions
* `tipper <http://pypi.python.org/pypi/tipper/>`_:
  write the traceback of the current thread into a file on SIGUSR1
  signal
* `crier <https://gist.github.com/737056>`_:
  write the traceback of the current thread into a file (eg.
  ``/tmp/dump-<pid>``) if a "request" file is created (eg.
  ``/tmp/crier-<pid>``). Implemented using a thread.
* `Python WAD <http://www.dabeaz.com/papers/Python2001/python.html>`_
  (Wrapped Application Debugger), not update since 2001:

Application fault handlers:

* The GNU libc has a fault handler in debug/segfault.c
* XEmacs has a fault handler displaying the Lisp traceback
* RPy has a fault handler

System-wide fault handlers:

* Ubuntu uses `Apport <https://wiki.ubuntu.com/Apport>`_
* Fedora has `ABRT <http://fedoraproject.org/wiki/Features/ABRT>`_
* The Linux kernel logs also segfaults into /var/log/kern.log (and
  /var/log/syslog). /proc/sys/kernel/core_pattern contols how coredumps are
  created.
* Windows opens a popup on a fatal error asking if the error should be
  reported to Microsoft


See also
========

* `Python issue #8863 <http://bugs.python.org/issue8863>`_ (may 2010):
  Display Python backtrace on SIGSEGV, SIGFPE and fatal error
* `Python issue #3999 <http://bugs.python.org/issue3999>`_ (sept. 2008):
  Real segmentation fault handler

