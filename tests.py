from __future__ import with_statement
from contextlib import contextmanager
import faulthandler; faulthandler.disable()
import os
import re
import signal
import subprocess
import sys
import tempfile
import unittest

Py_REF_DEBUG = hasattr(sys, 'gettotalrefcount')

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

def decode_output(output):
    return output.decode('ascii', 'backslashreplace')

def read_file(filename):
    with open(filename, "rb") as fp:
        output = fp.read()
    return decode_output(output)

def normalize_threads(trace):
    return re.sub(
        r'Current thread 0x[0-9a-f]+',
        'Current thread ...',
        trace)

def expected_backtrace(line1, line2, all_threads):
    if all_threads:
        expected = ['Current thread ...:']
    else:
        expected = ['Traceback (most recent call first):']
    expected.extend((
        '  File "<string>", line %s in func' % line1,
        '  File "<string>", line %s in <module>' % line2))
    return expected

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
        code = '\n'.join(code)
        process = subprocess.Popen(
            [sys.executable, '-c', code],
            stderr=subprocess.PIPE)
        stdout, stderr = process.communicate()
        if filename:
            output = read_file(filename)
        else:
            output = decode_output(stderr)
            if Py_REF_DEBUG:
                output = re.sub(r"\[\d+ refs\]\r?\n?$", "", output)
        return output

    def check_enabled(self, code, line_number, name,
                      filename=None, all_threads=False):
        expected = [
            'Fatal Python error: ' + name,
            '']
        if all_threads:
            expected.append('Current thread ...:')
        else:
            expected.append('Traceback (most recent call first):')
        expected.append('  File "<string>", line %s in <module>' % line_number)
        output = self.get_output(code, filename)
        if all_threads:
            output = normalize_threads(output)
        lines = output.splitlines()
        self.assertEqual(lines, expected)

    def test_sigsegv(self):
        self.check_enabled(
            ("import faulthandler; faulthandler.enable()",
             "faulthandler.sigsegv()"),
            2,
            'Segmentation fault')

    @skipIf(sys.platform == 'win32',
            "SIGFPE cannot be caught on Windows")
    def test_sigfpe(self):
        self.check_enabled(
            ("import faulthandler; faulthandler.enable(); "
             "faulthandler.sigfpe()",),
            1,
            'Floating point exception')

    @skipIf(not hasattr(faulthandler, 'sigbus'),
            "need faulthandler.sigbus()")
    def test_sigbus(self):
        self.check_enabled(
            ("import faulthandler; faulthandler.enable()",
             "faulthandler.sigbus()"),
            2,
            'Bus error')

    @skipIf(not hasattr(faulthandler, 'sigill'),
            "need faulthandler.sigill()")
    def test_sigill(self):
        self.check_enabled(
            ("import faulthandler; faulthandler.enable()",
             "faulthandler.sigill()"),
            2,
            'Illegal instruction')

    def test_gil_released(self):
        self.check_enabled(
            ("import faulthandler; faulthandler.enable()",
             "faulthandler.sigsegv(True)"),
            2,
            'Segmentation fault')

    def test_enable_file(self):
        with temporary_filename() as filename:
            self.check_enabled(
                ("import faulthandler",
                 "output = open(%r, 'wb')" % filename,
                 "faulthandler.enable(output)",
                 "faulthandler.sigsegv(True)"),
                4,
                'Segmentation fault',
                filename=filename)

    def test_enable_threads(self):
        self.check_enabled(
            ("import faulthandler",
             "faulthandler.enable(all_threads=True)",
             "faulthandler.sigsegv(True)"),
            3,
            'Segmentation fault',
            all_threads=True)

    def check_disabled(self, *code):
        not_expected = 'Fatal Python error'
        stderr = self.get_output(code)
        self.assertTrue(not_expected not in stderr,
                     "%r is present in %r" % (not_expected, stderr))

    def test_disabled(self):
        self.check_disabled(
            "import faulthandler",
            "faulthandler.sigsegv()")

    def test_enable_disable(self):
        self.check_disabled(
            "import faulthandler",
            "faulthandler.enable()",
            "faulthandler.disable()",
            "faulthandler.sigsegv()")

    def test_isenabled(self):
        self.assertFalse(faulthandler.isenabled())
        faulthandler.enable()
        self.assertTrue(faulthandler.isenabled())
        faulthandler.disable()
        self.assertFalse(faulthandler.isenabled())

    def check_dumpbacktrace(self, filename):
        code = (
            'from __future__ import with_statement',
            'import faulthandler',
            '',
            'def funcB():',
            '    if %r:' % (bool(filename),),
            '        with open(%r, "wb") as fp:' % (filename,),
            '            faulthandler.dumpbacktrace(fp)',
            '    else:',
            '        faulthandler.dumpbacktrace()',
            '',
            'def funcA():',
            '    funcB()',
            '',
            'funcA()',
        )
        if filename:
            lineno = 7
        else:
            lineno = 9
        expected = [
            'Traceback (most recent call first):',
            '  File "<string>", line %s in funcB' % lineno,
            '  File "<string>", line 12 in funcA',
            '  File "<string>", line 14 in <module>'
        ]
        trace = self.get_output(code, filename)
        trace = trace.splitlines()
        self.assertEqual(trace, expected)

    def test_dumpbacktrace(self):
        self.check_dumpbacktrace(None)
        with temporary_filename() as filename:
            self.check_dumpbacktrace(filename)

    def check_dumpbacktrace_threads(self, filename):
        output = self.get_output((
            'from __future__ import with_statement',
            'import faulthandler',
            'from threading import Thread, Event',
            'import time',
            '',
            'def dump():',
            '    if %r:' % (bool(filename),),
            '        with open(%r, "wb") as fp:' % (filename,),
            '            faulthandler.dumpbacktrace(fp, all_threads=True)',
            '    else:',
            '        faulthandler.dumpbacktrace(all_threads=True)',
            '',
            'class Waiter(Thread):',
            '    def __init__(self):',
            '        Thread.__init__(self)',
            '        self.running = Event()',
            '',
            '    def run(self):',
            '        self.running.set()',
            '        time.sleep(5.0)',
            '',
            'waiter = Waiter()',
            'waiter.start()',
            'waiter.running.wait()',
            'dump()',
            'waiter.stop = True',
            'waiter.join()',
        ), filename)
        # Normalize newlines for Windows
        lines = '\n'.join(output.splitlines())
        if filename:
            lineno = 9
        else:
            lineno = 11
        regex = '\n'.join((
            'Thread 0x[0-9a-f]+:',
            '  File "<string>", line 20 in run',
            '  File ".*threading.py", line [0-9]+ in __?bootstrap_inner',
            '  File ".*threading.py", line [0-9]+ in __?bootstrap',
            '',
            'Current thread 0x[0-9a-f]+:',
            '  File "<string>", line %s in dump' % lineno,
            '  File "<string>", line 25 in <module>',
        ))
        self.assertTrue(re.match(regex, lines),
                        "<<<%s>>> doesn't match" % lines)

    def test_dumpbacktrace_threads(self):
        self.check_dumpbacktrace_threads(None)
        with temporary_filename() as filename:
            self.check_dumpbacktrace_threads(filename)

    def _check_dumpbacktrace_later(self, repeat, cancel,
                                   filename, all_threads):
        code = (
            'import faulthandler',
            'import time',
            '',
            'def func(repeat, cancel):',
            '    if not repeat:',
            '        loops = 2',
            '    else:',
            '        loops = 3',
            '    dump = True',
            '    for x in range(loops):',
            '        a = time.time()',
            '        time.sleep(2)',
            '        b = time.time()',
            '        diff = (b - a)',
            '        if dump:',
            '            # sleep() interrupted after 1 second',
            '            assert diff < 2.0',
            '        else:',
            '            assert diff >= 2.0',
            '        if repeat and cancel and 1 <= x:',
            '            faulthandler.cancel_dumpbacktrace_later()',
            '            dump = False',
            '            cancel = False',
            '        if not repeat:',
            '            dump = False',
            '    if repeat and (not cancel):',
            '        faulthandler.cancel_dumpbacktrace_later()',
            '',
            'repeat = %s' % repeat,
            'cancel = %s' % cancel,
            'if %s:' % bool(filename),
            '    file = open(%r, "wb")' % filename,
            'else:',
            '    file = None',
            'faulthandler.dumpbacktrace_later(1, ',
            '    repeat=repeat, all_threads=%s, file=file)' % all_threads,
            'func(repeat, cancel)',
            'if file is not None:',
            '    file.close()',
        )
        trace = self.get_output(code, filename)
        if all_threads:
            trace = normalize_threads(trace)
        trace = trace.splitlines()

        expected = expected_backtrace(12, 37, all_threads)
        if repeat:
            if cancel:
                expected *= 2
            else:
                expected *= 3
        self.assertEqual(trace, expected,
                         "%r != %r: repeat=%s, cancel=%s, use_filename=%s, all_threads=%s"
                         % (trace, expected, repeat, cancel, bool(filename), all_threads))

    @skipIf(not hasattr(faulthandler, 'dumpbacktrace_later'),
            'need faulthandler.dumpbacktrace_later()')
    def check_dumpbacktrace_later(self, repeat=False, cancel=False,
                                  all_threads=False, file=False):
        if file:
            with temporary_filename() as filename:
                self._check_dumpbacktrace_later(repeat, cancel, filename, all_threads)
        else:
            self._check_dumpbacktrace_later(repeat, cancel, None, all_threads)

    def test_dumpbacktrace_later(self):
        self.check_dumpbacktrace_later()

    def test_dumpbacktrace_later_repeat(self):
        self.check_dumpbacktrace_later(repeat=True)

    def test_dumpbacktrace_later_repeat_cancel(self):
        self.check_dumpbacktrace_later(repeat=True, cancel=True)

    def test_dumpbacktrace_later_threads(self):
        self.check_dumpbacktrace_later(all_threads=True)

    def test_dumpbacktrace_later_file(self):
        self.check_dumpbacktrace_later(file=True)

    @skipIf(not hasattr(signal, "SIGUSR1"),
            "need signal.SIGUSR1")
    def check_register(self, filename=False, all_threads=False):
        code = (
            'import faulthandler',
            'import os',
            'import signal',
            '',
            'def func(signum):',
            '    os.kill(os.getpid(), signum)',
            '',
            'signum = signal.SIGUSR1',
            'if %s:' % bool(filename),
            '    file = open(%r, "wb")' % filename,
            'else:',
            '    file = None',
            'faulthandler.register(signum, file=file, all_threads=%s)' % all_threads,
            'func(signum)',
            'if file is not None:',
            '    file.close()',
        )
        trace = self.get_output(code, filename)
        if all_threads:
            trace = normalize_threads(trace)
        trace = trace.splitlines()
        expected = expected_backtrace(6, 14, all_threads)
        self.assertEqual(trace, expected,
                         "%r != %r: use_filename=%s, all_threads=%s"
                         % (trace, expected, bool(filename), all_threads))

    def test_register(self):
        self.check_register()

    def test_register_file(self):
        with temporary_filename() as filename:
            self.check_register(filename=filename)

    def test_register_threads(self):
        self.check_register(all_threads=True)

if __name__ == "__main__":
    unittest.main()

