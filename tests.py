import faulthandler; faulthandler.disable()
import os
import subprocess
import sys
import unittest
import re

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

    def test_dumpbacktrace(self):
        stdout, stderr = self.get_output((
            'import faulthandler',
            '',
            'def funcB():',
            '    faulthandler.dumpbacktrace()',
            '',
            'def funcA():',
            '    funcB()',
            '',
            'funcA()',
        ))
        trace = stdout.splitlines()
        self.assertEqual(trace, [
            'Traceback (most recent call first):',
            '  File "<string>", line 4 in funcB',
            '  File "<string>", line 7 in funcA',
            '  File "<string>", line 9 in <module>'
        ])

    def test_dumpbacktrace_threads(self):
        stdout, stderr = self.get_output((
            'import faulthandler',
            'from threading import Thread',
            'import time',
            '',
            'def dump():',
            '    faulthandler.dumpbacktrace_threads()',
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
        self.assertTrue(re.match(
            'Thread #2 \(0x[0-9a-f]+\):\n'
            '  File "<string>", line 15 in run\n'
            '  File ".*threading.py", line [0-9]+ in __?bootstrap_inner\n'
            '  File ".*threading.py", line [0-9]+ in __?bootstrap\n'
            '\n'
            'Current thread #1 \(0x[0-9a-f]+\):\n'
            '  File "<string>", line 6 in dump\n'
            '  File "<string>", line 20 in <module>\n'
        , stdout), "<<<%s>>> doesn't match" % stdout)


if __name__ == "__main__":
    unittest.main()

