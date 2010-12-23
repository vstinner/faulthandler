import faulthandler; faulthandler.disable()
import os
import subprocess
import sys
import unittest

class FaultHandlerTests(unittest.TestCase):
    def check_output(self, code, line_number, name):
        code = '\n'.join(code)
        line = '  File "<string>", line %s in <module>' % line_number
        expected = [
            b'Fatal Python error: ' + name,
            b'',
            b'Traceback (most recent call first):',
            line.encode('ascii')]
        process = subprocess.Popen(
            [sys.executable, '-c', code],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        stdout, stderr = process.communicate()
        lines = stderr.splitlines()
        self.assertEqual(lines, expected)

    def test_sigsegv(self):
        self.check_output(
            ("import faulthandler", "faulthandler.sigsegv()"),
            2,
            b'Segmentation fault')

    # SIGFPE cannot be catched on Windows
    if sys.platform != 'win32':
        def test_sigfpe(self):
            self.check_output(
                ("import faulthandler; faulthandler.sigfpe()",),
                1,
                b'Floating point exception')

    # need faulthandler.sigbus()
    if hasattr(faulthandler, 'sigbus'):
        def test_sigbus(self):
            self.check_output(
                ("import faulthandler", "faulthandler.sigbus()"),
                2,
                b'Bus error')

    # need faulthandler.sigill()
    if hasattr(faulthandler, 'sigill'):
        def test_sigill(self):
            self.check_output(
                ("import faulthandler", "faulthandler.sigill()"),
                2,
                b'Illegal instruction')

    def test_gil_released(self):
        self.check_output(
            ("import faulthandler", "faulthandler.sigsegv(True)"),
            2,
            b'Segmentation fault')

    def test_disabled(self):
        code = "import faulthandler; faulthandler.disable(); faulthandler.sigsegv()"
        env = os.environ.copy()
        try:
            del env['PYTHONFAULTHANDLER']
        except KeyError:
            pass
        not_expected = b'Fatal Python error'
        process = subprocess.Popen(
            [sys.executable, '-c', code],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
        stdout, stderr = process.communicate()
        self.assert_(not_expected not in stderr,
                     "%r is present in %r" % (not_expected, stderr))

if __name__ == "__main__":
    unittest.main()

