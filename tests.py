import faulthandler; faulthandler.disable()
import os
import subprocess
import sys
import unittest
import re
import tempfile

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

class FaultHandlerTests(unittest.TestCase):
    def get_output(self, code):
        code = '\n'.join(code)
        process = subprocess.Popen(
            [sys.executable, '-c', code],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        stdout, stderr = process.communicate()
        stdout = stdout.decode('ascii', 'backslashreplace')
        stderr = stderr.decode('ascii', 'backslashreplace')
        return (stdout, stderr)

    def check_enabled(self, code, line_number, name):
        line = '  File "<string>", line %s in <module>' % line_number
        expected = [
            'Fatal Python error: ' + name,
            '',
            'Traceback (most recent call first):',
            line]
        stdout, stderr = self.get_output(code)
        lines = stderr.splitlines()
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

    def check_disabled(self, *code):
        not_expected = 'Fatal Python error'
        stdout, stderr = self.get_output(code)
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
            lineno = 6
        else:
            lineno = 8
        expected = [
            'Traceback (most recent call first):',
            '  File "<string>", line %s in funcB' % lineno,
            '  File "<string>", line 11 in funcA',
            '  File "<string>", line 13 in <module>'
        ]
        stdout, stderr = self.get_output(code)
        if filename:
            with open(filename, "rb") as fp:
                stdout = fp.read()
                stdout = stdout.decode('ascii', 'backslashreplace')
        trace = stdout.splitlines()
        self.assertEqual(trace, expected)

    def test_dumpbacktrace(self):
        self.check_dumpbacktrace(None)
        with tempfile.TemporaryFile() as f:
            self.check_dumpbacktrace(f.name)

    def check_dumpbacktrace_threads(self, filename):
        stdout, stderr = self.get_output((
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
            with open(filename, "rb") as fp:
                stdout = fp.read()
                stdout = stdout.decode('ascii', 'backslashreplace')
        # Normalize newlines for Windows
        lines = '\n'.join(stdout.splitlines())
        if filename:
            lineno = 8
        else:
            lineno = 10
        regex = '\n'.join((
            'Thread #2 \\(0x[0-9a-f]+\\):',
            '  File "<string>", line 19 in run',
            '  File ".*threading.py", line [0-9]+ in __?bootstrap_inner',
            '  File ".*threading.py", line [0-9]+ in __?bootstrap',
            '',
            'Current thread #1 \\(0x[0-9a-f]+\\):',
            '  File "<string>", line %s in dump' % lineno,
            '  File "<string>", line 24 in <module>',
        ))
        self.assertTrue(re.match(regex, lines),
                        "<<<%s>>> doesn't match" % lines)

    def test_dumpbacktrace_threads(self):
        self.check_dumpbacktrace_threads(None)
        with tempfile.TemporaryFile() as tmp:
            self.check_dumpbacktrace_threads(tmp.name)

    def check_dumpbacktrace_later(self, repeat, cancel, all_threads):
        sys.stdout.write("*")
        sys.stdout.flush()
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
            'faulthandler.dumpbacktrace_later(1, repeat=repeat, all_threads=%s)' % all_threads,
            'slow_function(repeat, cancel)',
        )
        stdout, stderr = self.get_output(code)
        trace = stderr
        if all_threads:
            trace = re.sub(
                r'Current thread #1 \(0x[0-9a-f]+\)',
                'Current thread #1 (...)',
                trace)
        trace = trace.splitlines()

        if all_threads:
            expected = [
                'Current thread #1 (...):',
                '  File "<string>", line 12 in slow_function',
                '  File "<string>", line 32 in <module>']
        else:
            expected = [
                'Traceback (most recent call first):',
                '  File "<string>", line 12 in slow_function',
                '  File "<string>", line 32 in <module>']
        if repeat:
            if cancel:
                expected *= 2
            else:
                expected *= 3
        self.assertEqual(trace, expected,
                         "%r != %r: repeat=%s, cancel=%s, all_threads=%s"
                         % (trace, expected, repeat, cancel, all_threads))

    def test_dumpbacktrace_later(self):
        for all_threads in (False, True):
            # don't repeat
            self.check_dumpbacktrace_later(False, False, all_threads)

            # repeat
            self.check_dumpbacktrace_later(True, False, all_threads)

            # repeat and cancel
            self.check_dumpbacktrace_later(True, True, all_threads)


if __name__ == "__main__":
    unittest.main()

