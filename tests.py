import faulthandler; faulthandler.disable()
import os
import subprocess
import sys
import unittest

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


if __name__ == "__main__":
    unittest.main()

