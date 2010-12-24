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
    def check_output(self, code, line_number, name):
        code = '\n'.join(code)
        line = '  File "<string>", line %s in <module>' % line_number
        expected = [
            'Fatal Python error: ' + name,
            '',
            'Traceback (most recent call first):',
            line]
        process = subprocess.Popen(
            [sys.executable, '-c', code],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        stdout, stderr = process.communicate()
        stderr = stderr.decode('ascii', 'backslashreplace')
        lines = stderr.splitlines()
        self.assertEqual(lines, expected)

    def test_sigsegv(self):
        self.check_output(
            ("import faulthandler", "faulthandler.sigsegv()"),
            2,
            'Segmentation fault')

    @skipIf(sys.platform == 'win32', "SIGFPE cannot be caught on Windows")
    def test_sigfpe(self):
        self.check_output(
            ("import faulthandler; faulthandler.sigfpe()",),
            1,
            'Floating point exception')

    @skipIf(not hasattr(faulthandler, 'sigbus'), "need faulthandler.sigbus()")
    def test_sigbus(self):
        self.check_output(
            ("import faulthandler", "faulthandler.sigbus()"),
            2,
            'Bus error')

    @skipIf(not hasattr(faulthandler, 'sigill'), "need faulthandler.sigill()")
    def test_sigill(self):
        self.check_output(
            ("import faulthandler", "faulthandler.sigill()"),
            2,
            'Illegal instruction')

    def test_gil_released(self):
        self.check_output(
            ("import faulthandler", "faulthandler.sigsegv(True)"),
            2,
            'Segmentation fault')

    def test_disabled(self):
        code = "import faulthandler; faulthandler.disable(); faulthandler.sigsegv()"
        env = os.environ.copy()
        try:
            del env['PYTHONFAULTHANDLER']
        except KeyError:
            pass
        not_expected = 'Fatal Python error'
        process = subprocess.Popen(
            [sys.executable, '-c', code],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
        stdout, stderr = process.communicate()
        stderr = stderr.decode('ascii', 'backslashreplace')
        self.assertTrue(not_expected not in stderr,
                     "%r is present in %r" % (not_expected, stderr))

    def test_isenabled(self):
        self.assertFalse(faulthandler.isenabled())
        faulthandler.enable()
        self.assertTrue(faulthandler.isenabled())
        faulthandler.disable()
        self.assertFalse(faulthandler.isenabled())

if __name__ == "__main__":
    unittest.main()

