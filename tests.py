from __future__ import with_statement
from contextlib import contextmanager
import datetime
import faulthandler
import os
import re
import signal
import subprocess
import sys
import tempfile
import unittest
from textwrap import dedent

try:
    import threading
    HAVE_THREADS = True
except ImportError:
    HAVE_THREADS = False

TIMEOUT = 1

Py_REF_DEBUG = hasattr(sys, 'gettotalrefcount')

try:
    from test.support import SuppressCrashReport
except ImportError:
    try:
        import resource
    except ImportError:
        resource = None

    class SuppressCrashReport:
        """Try to prevent a crash report from popping up.

        On Windows, don't display the Windows Error Reporting dialog.  On UNIX,
        disable the creation of coredump file.
        """
        old_value = None

        def __enter__(self):
            """On Windows, disable Windows Error Reporting dialogs using
            SetErrorMode.

            On UNIX, try to save the previous core file size limit, then set
            soft limit to 0.
            """
            if sys.platform.startswith('win'):
                # see http://msdn.microsoft.com/en-us/library/windows/desktop/ms680621.aspx
                # GetErrorMode is not available on Windows XP and Windows Server 2003,
                # but SetErrorMode returns the previous value, so we can use that
                import ctypes
                self._k32 = ctypes.windll.kernel32
                SEM_NOGPFAULTERRORBOX = 0x02
                self.old_value = self._k32.SetErrorMode(SEM_NOGPFAULTERRORBOX)
                self._k32.SetErrorMode(self.old_value | SEM_NOGPFAULTERRORBOX)
            else:
                if resource is not None:
                    try:
                        self.old_value = resource.getrlimit(resource.RLIMIT_CORE)
                        resource.setrlimit(resource.RLIMIT_CORE,
                                           (0, self.old_value[1]))
                    except (ValueError, OSError):
                        pass
                if sys.platform == 'darwin':
                    # Check if the 'Crash Reporter' on OSX was configured
                    # in 'Developer' mode and warn that it will get triggered
                    # when it is.
                    #
                    # This assumes that this context manager is used in tests
                    # that might trigger the next manager.
                    value = subprocess.Popen(['/usr/bin/defaults', 'read',
                            'com.apple.CrashReporter', 'DialogType'],
                            stdout=subprocess.PIPE).communicate()[0]
                    if value.strip() == b'developer':
                        sys.stdout.write("this test triggers the Crash "
                                         "Reporter, that is intentional")
                        sys.stdout.flush()

            return self

        def __exit__(self, *ignore_exc):
            """Restore Windows ErrorMode or core file behavior to initial value."""
            if self.old_value is None:
                return

            if sys.platform.startswith('win'):
                self._k32.SetErrorMode(self.old_value)
            else:
                if resource is not None:
                    try:
                        resource.setrlimit(resource.RLIMIT_CORE, self.old_value)
                    except (ValueError, OSError):
                        pass


try:
    skipIf = unittest.skipIf
except AttributeError:
    import functools
    def skipIf(test, reason):
        def decorator(func):
            @functools.wraps(func)
            def wrapper(*args, **kw):
                if not test:
                    return func(*args, **kw)
                else:
                    print("skip %s: %s" % (func.__name__, reason))
            return wrapper
        return decorator

try:
    from resource import setrlimit, RLIMIT_CORE, error as resource_error
except ImportError:
    prepare_subprocess = None
else:
    def prepare_subprocess():
        # don't create core file
        try:
            setrlimit(RLIMIT_CORE, (0, 0))
        except (ValueError, resource_error):
            pass

def spawn_python(*args):
    args = (sys.executable,) + args
    return subprocess.Popen(args,
                            stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)

def expected_traceback(lineno1, lineno2, header, min_count=1):
    regex = header
    regex += '  File "<string>", line %s in func\n' % lineno1
    regex += '  File "<string>", line %s in <module>' % lineno2
    if 1 < min_count:
        return '^' + (regex + '\n') * (min_count - 1) + regex
    else:
        return '^' + regex + '$'

@contextmanager
def temporary_filename():
   filename = tempfile.mktemp()
   try:
       yield filename
   finally:
       try:
           os.unlink(filename)
       except OSError:
           pass

class FaultHandlerTests(unittest.TestCase):
    def get_output(self, code, filename=None):
        """
        Run the specified code in Python (in a new child process) and read the
        output from the standard error or from a file (if filename is set).
        Return the output lines as a list.

        Strip the reference count from the standard error for Python debug
        build, and replace "Current thread 0x00007f8d8fbd9700" by "Current
        thread XXX".
        """
        code = dedent(code).strip()
        with SuppressCrashReport():
            process = spawn_python('-c', code)
        stdout, stderr = process.communicate()
        exitcode = process.wait()
        output = re.sub(br"\[\d+ refs\]\r?\n?", b"", stdout).strip()
        output = output.decode('ascii', 'backslashreplace')
        if filename:
            self.assertEqual(output, '')
            with open(filename, "rb") as fp:
                output = fp.read()
            output = output.decode('ascii', 'backslashreplace')
        output = re.sub('Current thread 0x[0-9a-f]+',
                        'Current thread XXX',
                        output)
        return output.splitlines(), exitcode

    def check_fatal_error(self, code, line_number, name_regex,
                          filename=None, all_threads=True, other_regex=None):
        """
        Check that the fault handler for fatal errors is enabled and check the
        traceback from the child process output.

        Raise an error if the output doesn't match the expected format.
        """
        if all_threads:
            header = 'Current thread XXX (most recent call first)'
        else:
            header = 'Stack (most recent call first)'
        regex = """
            ^Fatal Python error: {name}

            {header}:
              File "<string>", line {lineno} in <module>
            """
        regex = dedent(regex).format(
            lineno=line_number,
            name=name_regex,
            header=re.escape(header)).strip()
        if other_regex:
            regex += '|' + other_regex
        output, exitcode = self.get_output(code, filename)
        output = '\n'.join(output)
        self.assertRegex(output, regex)
        self.assertNotEqual(exitcode, 0)

    @skipIf(sys.platform.startswith('aix'),
            "the first page of memory is a mapped read-only on AIX")
    def test_read_null(self):
        self.check_fatal_error("""
            import faulthandler
            faulthandler.enable()
            faulthandler._read_null()
            """,
            3,
            # Issue #12700: Read NULL raises SIGILL on Mac OS X Lion
            '(?:Segmentation fault|Bus error|Illegal instruction)')

    def test_sigsegv(self):
        self.check_fatal_error("""
            import faulthandler
            faulthandler.enable()
            faulthandler._sigsegv()
            """,
            3,
            'Segmentation fault')

    def test_sigabrt(self):
        self.check_fatal_error("""
            import faulthandler
            faulthandler.enable()
            faulthandler._sigabrt()
            """,
            3,
            'Aborted')

    @skipIf(sys.platform == 'win32',
            "SIGFPE cannot be caught on Windows")
    def test_sigfpe(self):
        self.check_fatal_error("""
            import faulthandler
            faulthandler.enable()
            faulthandler._sigfpe()
            """,
            3,
            'Floating point exception')

    @skipIf(not hasattr(signal, 'SIGBUS'), 'need signal.SIGBUS')
    def test_sigbus(self):
        self.check_fatal_error("""
            import faulthandler
            import signal

            faulthandler.enable()
            faulthandler._raise_signal(signal.SIGBUS)
            """,
            5,
            'Bus error')

    @skipIf(not hasattr(signal, 'SIGILL'), 'need signal.SIGILL')
    def test_sigill(self):
        self.check_fatal_error("""
            import faulthandler
            import signal

            faulthandler.enable()
            faulthandler._raise_signal(signal.SIGILL)
            """,
            5,
            'Illegal instruction')

    def test_fatal_error(self):
        if sys.version_info >= (2, 6):
            arg = "b'xyz'"
        else:
            arg = "'xyz'"
        message = "xyz\n"
        if sys.platform.startswith('win'):
            # When running unit tests with Microsoft Windows SDK,
            # Py_FatalError() displays the message "This application has
            # requested the Runtime to terminate it in an unusual way. Please
            # contact the application's support team for more information.".
            # Just ignore this message, it is not related to faulthandler.
            message += r"(.|\n)*"
        message += "Fatal Python error: Aborted"
        self.check_fatal_error("""
            import faulthandler
            faulthandler.enable()
            faulthandler._fatal_error(%s)
            """ % arg,
            3,
            message)

    @skipIf(sys.platform.startswith('openbsd') and HAVE_THREADS,
            "Issue #12868: sigaltstack() doesn't work on "
            "OpenBSD if Python is compiled with pthread")
    @skipIf(not hasattr(faulthandler, '_stack_overflow'),
            'need faulthandler._stack_overflow()')
    def test_stack_overflow(self):
        self.check_fatal_error("""
            import faulthandler
            faulthandler.enable()
            faulthandler._stack_overflow()
            """,
            3,
            '(?:Segmentation fault|Bus error)',
            other_regex='unable to raise a stack overflow')

    def test_gil_released(self):
        self.check_fatal_error("""
            import faulthandler
            faulthandler.enable()
            faulthandler._sigsegv(True)
            """,
            3,
            'Segmentation fault')

    def test_enable_file(self):
        with temporary_filename() as filename:
            self.check_fatal_error("""
                import faulthandler
                output = open({filename}, 'wb')
                faulthandler.enable(output)
                faulthandler._sigsegv()
                """.format(filename=repr(filename)),
                4,
                'Segmentation fault',
                filename=filename)

    def test_enable_single_thread(self):
        self.check_fatal_error("""
            import faulthandler
            faulthandler.enable(all_threads=False)
            faulthandler._sigsegv()
            """,
            3,
            'Segmentation fault',
            all_threads=False)

    def test_disable(self):
        code = """
            import faulthandler
            faulthandler.enable()
            faulthandler.disable()
            faulthandler._sigsegv()
            """
        not_expected = 'Fatal Python error'
        stderr, exitcode = self.get_output(code)
        stderr = '\n'.join(stderr)
        self.assertTrue(not_expected not in stderr,
                     "%r is present in %r" % (not_expected, stderr))
        self.assertNotEqual(exitcode, 0)

    def test_custom_header_on_signal(self):
        self.check_fatal_error("""
            import faulthandler
            faulthandler.enable(all_threads=False, header="My header text")
            faulthandler._sigsegv()
            """,
            3,
            'Segmentation fault: My header text',
            all_threads=False)

    def test_is_enabled(self):
        orig_stderr = sys.stderr
        try:
            # regrtest may replace sys.stderr by io.StringIO object, but
            # faulthandler.enable() requires that sys.stderr has a fileno()
            # method
            sys.stderr = sys.__stderr__

            was_enabled = faulthandler.is_enabled()
            try:
                faulthandler.enable()
                self.assertTrue(faulthandler.is_enabled())
                faulthandler.disable()
                self.assertFalse(faulthandler.is_enabled())
            finally:
                if was_enabled:
                    faulthandler.enable()
                else:
                    faulthandler.disable()
        finally:
            sys.stderr = orig_stderr

    def test_disabled_by_default(self):
        # By default, the module should be disabled
        code = "import faulthandler; print(faulthandler.is_enabled())"
        args = (sys.executable, '-c', code)
        # don't use assert_python_ok() because it always enable faulthandler
        process = subprocess.Popen(args, stdout=subprocess.PIPE)
        output, _ = process.communicate()
        exitcode = process.wait()
        self.assertEqual(output.rstrip(), b"False")
        self.assertEqual(exitcode, 0)

    def check_dump_traceback(self, filename):
        """
        Explicitly call dump_traceback() function and check its output.
        Raise an error if the output doesn't match the expected format.
        """
        code = """
            from __future__ import with_statement
            import faulthandler

            def funcB():
                if {has_filename}:
                    with open({filename}, "wb") as fp:
                        faulthandler.dump_traceback(fp, all_threads=False)
                else:
                    faulthandler.dump_traceback(all_threads=False)

            def funcA():
                funcB()

            funcA()
            """
        code = code.format(
            filename=repr(filename),
            has_filename=bool(filename),
        )
        if filename:
            lineno = 7
        else:
            lineno = 9
        expected = [
            'Stack (most recent call first):',
            '  File "<string>", line %s in funcB' % lineno,
            '  File "<string>", line 12 in funcA',
            '  File "<string>", line 14 in <module>'
        ]
        trace, exitcode = self.get_output(code, filename)
        self.assertEqual(trace, expected)
        self.assertEqual(exitcode, 0)

    def test_dump_traceback(self):
        self.check_dump_traceback(None)

    def test_dump_traceback_file(self):
        with temporary_filename() as filename:
            self.check_dump_traceback(filename)

    def test_truncate(self):
        maxlen = 500
        func_name = 'x' * (maxlen + 50)
        truncated = 'x' * maxlen + '...'
        code = """
            import faulthandler

            def {func_name}():
                faulthandler.dump_traceback(all_threads=False)

            {func_name}()
            """
        code = code.format(
            func_name=func_name,
        )
        expected = [
            'Stack (most recent call first):',
            '  File "<string>", line 4 in %s' % truncated,
            '  File "<string>", line 6 in <module>'
        ]
        trace, exitcode = self.get_output(code)
        self.assertEqual(trace, expected)
        self.assertEqual(exitcode, 0)

    @skipIf(not HAVE_THREADS, 'need threads')
    def check_dump_traceback_threads(self, filename):
        """
        Call explicitly dump_traceback(all_threads=True) and check the output.
        Raise an error if the output doesn't match the expected format.
        """
        code = """
            from __future__ import with_statement
            import faulthandler
            from threading import Thread, Event
            import time

            def dump():
                if {filename}:
                    with open({filename}, "wb") as fp:
                        faulthandler.dump_traceback(fp, all_threads=True)
                else:
                    faulthandler.dump_traceback(all_threads=True)

            class Waiter(Thread):
                # avoid blocking if the main thread raises an exception.
                daemon = True

                def __init__(self):
                    Thread.__init__(self)
                    self.running = Event()
                    self.stop = Event()

                def run(self):
                    self.running.set()
                    self.stop.wait()

            waiter = Waiter()
            waiter.start()
            waiter.running.wait()
            dump()
            waiter.stop.set()
            waiter.join()
            """
        code = code.format(filename=repr(filename))
        output, exitcode = self.get_output(code, filename)
        output = '\n'.join(output)
        if filename:
            lineno = 9
        else:
            lineno = 11
        regex = """
            ^Thread 0x[0-9a-f]+ \(most recent call first\):
            (?:  File ".*threading.py", line [0-9]+ in [_a-z]+
            ){{1,3}}  File "<string>", line 24 in run
              File ".*threading.py", line [0-9]+ in _?_bootstrap_inner
              File ".*threading.py", line [0-9]+ in _?_bootstrap

            Current thread XXX \(most recent call first\):
              File "<string>", line {lineno} in dump
              File "<string>", line 29 in <module>$
            """
        regex = dedent(regex.format(lineno=lineno)).strip()
        self.assertRegex(output, regex)
        self.assertEqual(exitcode, 0)

    def test_dump_traceback_threads(self):
        self.check_dump_traceback_threads(None)

    def test_dump_traceback_threads_file(self):
        with temporary_filename() as filename:
            self.check_dump_traceback_threads(filename)

    def _check_dump_traceback_later(self, repeat, cancel, filename, loops, header=None):
        """
        Check how many times the traceback is written in timeout x 2.5 seconds,
        or timeout x 3.5 seconds if cancel is True: 1, 2 or 3 times depending
        on repeat and cancel options.

        Raise an error if the output doesn't match the expect format.
        """
        timeout_str = str(datetime.timedelta(seconds=TIMEOUT))
        code = """
            import faulthandler
            import time

            def func(timeout, repeat, cancel, file, loops):
                for loop in range(loops):
                    faulthandler.dump_traceback_later(timeout,
                        repeat=repeat, file=file, header={header})
                    if cancel:
                        faulthandler.cancel_dump_traceback_later()
                    # sleep twice because time.sleep() is interrupted by
                    # signals and dump_traceback_later() uses SIGALRM
                    for loop in range(2):
                        time.sleep(timeout * 1.25)
                    faulthandler.cancel_dump_traceback_later()

            timeout = {timeout}
            repeat = {repeat}
            cancel = {cancel}
            loops = {loops}
            if {has_filename}:
                file = open({filename}, "wb")
            else:
                file = None
            func(timeout, repeat, cancel, file, loops)
            if file is not None:
                file.close()
            """
        code = code.format(
            header=repr(header),
            timeout=TIMEOUT,
            repeat=repeat,
            cancel=cancel,
            loops=loops,
            has_filename=bool(filename),
            filename=repr(filename),
        )
        trace, exitcode = self.get_output(code, filename)
        trace = '\n'.join(trace)

        if not cancel:
            count = loops
            if repeat:
                count *= 2
            if header:
                header = r'%s\nCurrent thread XXX \(most recent call first\):\n' % header
            else:
                header = r'Timeout \(%s\)!\nCurrent thread XXX \(most recent call first\):\n' % timeout_str
            regex = expected_traceback(13, 24, header, min_count=count)
            self.assertRegex(trace, regex)
        else:
            self.assertEqual(trace, '')
        self.assertEqual(exitcode, 0)

    @skipIf(not hasattr(faulthandler, 'dump_traceback_later'),
            'need faulthandler.dump_traceback_later()')
    def check_dump_traceback_later(self, repeat=False, cancel=False,
                                    file=False, twice=False, header=None):
        if twice:
            loops = 2
        else:
            loops = 1
        if file:
            with temporary_filename() as filename:
                self._check_dump_traceback_later(repeat, cancel,
                                                 filename, loops, header)
        else:
            self._check_dump_traceback_later(repeat, cancel,
                                             None, loops, header)

    def test_dump_traceback_later(self):
        self.check_dump_traceback_later()

    def test_dump_traceback_later_repeat(self):
        self.check_dump_traceback_later(repeat=True)

    def test_dump_traceback_later_cancel(self):
        self.check_dump_traceback_later(cancel=True)

    def test_dump_traceback_later_file(self):
        self.check_dump_traceback_later(file=True)

    def test_dump_traceback_later_twice(self):
        self.check_dump_traceback_later(twice=True)

    def test_dump_traceback_later_with_header(self):
        self.check_dump_traceback_later(twice=True, header="Custom header")

    @skipIf(not hasattr(faulthandler, "register"),
            "need faulthandler.register")
    def check_register(self, filename=False, all_threads=False,
                       unregister=False, chain=False, header=None):
        """
        Register a handler displaying the traceback on a user signal. Raise the
        signal and check the written traceback.

        If chain is True, check that the previous signal handler is called.

        Raise an error if the output doesn't match the expected format.
        """
        signum = signal.SIGUSR1
        code = """
            import faulthandler
            import os
            import signal
            import sys

            def func(signum):
                os.kill(os.getpid(), signum)

            def handler(signum, frame):
                handler.called = True
            handler.called = False

            exitcode = 0
            signum = {signum}
            unregister = {unregister}
            chain = {chain}

            if {has_filename}:
                file = open({filename}, "wb")
            else:
                file = None
            if chain:
                signal.signal(signum, handler)
            faulthandler.register(signum, file=file,
                                  all_threads={all_threads}, chain={chain},
                                  header={header})
            if unregister:
                faulthandler.unregister(signum)
            func(signum)
            if chain and not handler.called:
                if file is not None:
                    output = file
                else:
                    output = sys.stderr
                output.write("Error: signal handler not called!\\n")
                exitcode = 1
            if file is not None:
                file.close()
            sys.exit(exitcode)
            """
        code = code.format(
            filename=repr(filename),
            has_filename=bool(filename),
            all_threads=all_threads,
            signum=signum,
            unregister=unregister,
            chain=chain,
            header=repr(header),
        )
        trace, exitcode = self.get_output(code, filename)
        trace = '\n'.join(trace)
        if not unregister:
            if all_threads:
                regex = 'Current thread XXX \(most recent call first\):\n'
            else:
                regex = 'Stack \(most recent call first\):\n'
            if header:
                regex = header + '\n' + regex
            regex = expected_traceback(7, 29, regex)
            self.assertRegex(trace, regex)
        else:
            self.assertEqual(trace, '')
        if unregister:
            self.assertNotEqual(exitcode, 0)
        else:
            self.assertEqual(exitcode, 0)

    def test_register(self):
        self.check_register()

    def test_unregister(self):
        self.check_register(unregister=True)

    def test_register_file(self):
        with temporary_filename() as filename:
            self.check_register(filename=filename)

    def test_register_threads(self):
        self.check_register(all_threads=True)

    def test_register_chain(self):
        self.check_register(chain=True)

    def test_header(self):
        self.check_register(header="Custom header text")

    @contextmanager
    def check_stderr_none(self):
        stderr = sys.stderr
        try:
            sys.stderr = None
            err = '<no exception raised>'
            try:
                yield
            except Exception as exc:
                err = exc
            self.assertEqual(str(err), "sys.stderr is None")
        finally:
            sys.stderr = stderr

    def test_stderr_None(self):
        # Issue #21497: provide an helpful error if sys.stderr is None,
        # instead of just an attribute error: "None has no attribute fileno".
        with self.check_stderr_none():
            faulthandler.enable()
        with self.check_stderr_none():
            faulthandler.dump_traceback()
        if hasattr(faulthandler, 'dump_traceback_later'):
            with self.check_stderr_none():
                faulthandler.dump_traceback_later(1)
        if hasattr(faulthandler, "register"):
            with self.check_stderr_none():
                faulthandler.register(signal.SIGUSR1)

    if not hasattr(unittest.TestCase, 'assertRegex'):
        # Copy/paste from Python 3.3: just replace (str, bytes) by str
        def assertRegex(self, text, expected_regex, msg=None):
            """Fail the test unless the text matches the regular expression."""
            if isinstance(expected_regex, str):
                assert expected_regex, "expected_regex must not be empty."
                expected_regex = re.compile(expected_regex)
            if not expected_regex.search(text):
                msg = msg or "Regex didn't match"
                msg = '%s: %r not found in %r' % (msg, expected_regex.pattern, text)
                raise self.failureException(msg)


if __name__ == "__main__":
    unittest.main()
