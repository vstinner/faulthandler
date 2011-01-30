from __future__ import with_statement
import faulthandler; faulthandler.disable()
import os
import subprocess
import sys
import unittest
import re
import tempfile

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

class FaultHandlerTests(unittest.TestCase):
    def get_output(self, code):
        code = '\n'.join(code)
        process = subprocess.Popen(
            [sys.executable, '-c', code],
            stderr=subprocess.PIPE)
        stdout, stderr = process.communicate()
        output = decode_output(stderr)
        if Py_REF_DEBUG:
            output = re.sub(r"\[\d+ refs\]\r?\n?$", "", output)
        return output

    def check_enabled(self, code, line_number, name, filename=None):
        line = '  File "<string>", line %s in <module>' % line_number
        expected = [
            'Fatal Python error: ' + name,
            '',
            'Traceback (most recent call first):',
            line]
        output = self.get_output(code)
        if filename:
            output = read_file(filename)
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
        with tempfile.NamedTemporaryFile() as f:
            self.check_enabled(
                ("from __future__ import with_statement",
                 "import faulthandler",
                 "output = open(%r, 'wb')" % f.name,
                 "faulthandler.enable(output)",
                 "faulthandler.sigsegv(True)"),
                5,
                'Segmentation fault',
                filename=f.name)

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
        trace = self.get_output(code)
        if filename:
            trace = read_file(filename)
        trace = trace.splitlines()
        self.assertEqual(trace, expected)

    def test_dumpbacktrace(self):
        self.check_dumpbacktrace(None)
        with tempfile.NamedTemporaryFile() as f:
            self.check_dumpbacktrace(f.name)

    def check_dumpbacktrace_threads(self, filename):
        output = self.get_output((
            'from __future__ import with_statement',
            'import faulthandler',
            'from threading import Thread',
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
            '        self.stop = False',
            '',
            '    def run(self):',
            '        while not self.stop:',
            '            time.sleep(0.1)',
            '',
            'waiter = Waiter()',
            'waiter.start()',
            'time.sleep(0.1)',
            'dump()',
            'waiter.stop = True',
            'waiter.join()',
        ))
        if filename:
            output = read_file(filename)
        # Normalize newlines for Windows
        lines = '\n'.join(output.splitlines())
        if filename:
            lineno = 9
        else:
            lineno = 11
        regex = '\n'.join((
            'Thread #2 \\(0x[0-9a-f]+\\):',
            '  File "<string>", line 20 in run',
            '  File ".*threading.py", line [0-9]+ in __?bootstrap_inner',
            '  File ".*threading.py", line [0-9]+ in __?bootstrap',
            '',
            'Current thread #1 \\(0x[0-9a-f]+\\):',
            '  File "<string>", line %s in dump' % lineno,
            '  File "<string>", line 25 in <module>',
        ))
        self.assertTrue(re.match(regex, lines),
                        "<<<%s>>> doesn't match" % lines)

    def test_dumpbacktrace_threads(self):
        self.check_dumpbacktrace_threads(None)
        with tempfile.NamedTemporaryFile() as tmp:
            self.check_dumpbacktrace_threads(tmp.name)

    def _check_dumpbacktrace_later(self, repeat, cancel,
                                   filename, all_threads):
        code = (
            'import faulthandler',
            'import time',
            '',
            'def slow_function(repeat, cancel):',
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
            'slow_function(repeat, cancel)',
            'if file is not None:',
            '    file.close()',
        )
        stderr = self.get_output(code)
        if filename:
            trace = read_file(filename)
        else:
            trace = stderr
        if all_threads:
            trace = re.sub(
                r'Current thread #1 \(0x[0-9a-f]+\)',
                'Current thread #1 (...)',
                trace)
        trace = trace.splitlines()

        if all_threads:
            expected = ['Current thread #1 (...):']
        else:
            expected = ['Traceback (most recent call first):']
        expected.extend((
            '  File "<string>", line 12 in slow_function',
            '  File "<string>", line 37 in <module>'))
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
                                  all_threads=False, filename=False):
        if filename:
            with tempfile.NamedTemporaryFile() as f:
                self._check_dumpbacktrace_later(repeat, cancel, f.name, all_threads)
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
        self.check_dumpbacktrace_later(filename=True)

if __name__ == "__main__":
    unittest.main()

