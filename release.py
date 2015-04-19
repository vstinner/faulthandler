#!/usr/bin/env python3
"""
Script to upload 32 bits and 64 bits wheel packages on Windows.

Usage: "python release.py GIT_TAG" where GIT_TAG is a Git tag, usually
a version number like "2.O".

Requirements:

- The Windows SDK 7.0 is required to build wheel packages on Windows
- Python 2.6, 2.7 and 3.2
"""
import contextlib
import optparse
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import textwrap

PROJECT = 'faulthandler'
PYTHON_VERSIONS = (
    (2, 6),
    (2, 7),
    (3, 2),
)
PY3 = (sys.version_info >= (3,))
GIT = 'git'
SDK_ROOT = r"C:\Program Files\Microsoft SDKs\Windows"
BATCH_FAIL_ON_ERROR = "@IF %errorlevel% neq 0 exit /b %errorlevel%"
WINDOWS = (sys.platform == 'win32')
LIBRARY = PROJECT + ('.pyd' if WINDOWS else '.so')


def get_architecture_bits():
    arch = platform.architecture()[0]
    return int(arch[:2])


class PythonVersion:
    def __init__(self, major, minor, bits):
        self.major = major
        self.minor = minor
        self.bits = bits
        self._executable = None
        self._pydebug = None

    @staticmethod
    def running():
        bits = get_architecture_bits()
        pyver = PythonVersion(sys.version_info.major,
                              sys.version_info.minor,
                              bits)
        pyver._executable = sys.executable
        return pyver

    def _get_executable_windows(self, app):
        if self.bits == 32:
            executable = 'c:\\Python%s%s_32bit\\python.exe'
        else:
            executable = 'c:\\Python%s%s\\python.exe'
        executable = executable % (self.major, self.minor)
        if not os.path.exists(executable):
            print("Unable to find python %s" % self)
            print("%s does not exists" % executable)
            sys.exit(1)
        return executable

    def _get_executable_unix(self, app):
        return 'python%s.%s' % (self.major, self.minor)

    def get_executable(self, app):
        if self._executable:
            return self._executable

        if WINDOWS:
            executable = self._get_executable_windows(app)
        else:
            executable = self._get_executable_unix(app)

        code = (
            'import platform, sys; '
            'print("{ver[0]}.{ver[1]} {bits}".format('
            'ver=sys.version_info, '
            'bits=platform.architecture()[0]))'
        )
        try:
            exitcode, stdout = app.get_output(executable, '-c', code,
                                              ignore_stderr=True)
        except OSError as exc:
            print("Error while checking %s:" % self)
            print(str(exc))
            print("Executable: %s" % executable)
            sys.exit(1)
        else:
            stdout = stdout.rstrip()
            expected = "%s.%s %sbit" % (self.major, self.minor, self.bits)
            if stdout != expected:
                print("Python version or architecture doesn't match")
                print("got %r, expected %r" % (stdout, expected))
                print("Executable: %s" % executable)
                sys.exit(1)

        self._executable = executable
        return executable

    def get_pydebug(self, app):
        if self._pydebug is not None:
            return self._pydebug

        executable = self.get_executable(app)
        code = 'import sys; print(hasattr(sys, "gettotalrefcount"))'
        exitcode, stdout = app.get_output(executable, '-c', code,
                                          ignore_stderr=True)
        stdout = stdout.rstrip()
        if stdout == 'True':
            self._pydebug = True
            return True
        elif stdout == 'False':
            self._pydebug = False
        else:
            print("Unable to get pydebug flag of %s" % self)
            print("stdout: %r, exitcode: %s" % (stdout, exitcode))
            print("Executable: %s" % executable)
            sys.exit(1)
        return self._pydebug

    def __str__(self):
        return 'Python %s.%s (%s bits)' % (self.major, self.minor, self.bits)


class Release(object):
    def __init__(self):
        root = os.path.dirname(__file__)
        self.root = os.path.realpath(root)
        # Set these attributes to True to run also register sdist upload
        self.wheel = False
        self.test = False
        self.register = False
        self.sdist = False
        self.verbose = False
        self.upload = False
        # Release mode: enable more tests
        self.release = False
        self.python_versions = []
        if WINDOWS:
            supported_archs = (32, 64)
        else:
            bits = get_architecture_bits()
            supported_archs = (bits,)
        for major, minor in PYTHON_VERSIONS:
            for bits in supported_archs:
                pyver = PythonVersion(major, minor, bits)
                self.python_versions.append(pyver)

    @contextlib.contextmanager
    def _popen(self, args, **kw):
        verbose = kw.pop('verbose', True)
        if self.verbose and verbose:
            print('+ ' + ' '.join(args))
        if PY3:
            kw['universal_newlines'] = True
        proc = subprocess.Popen(args, **kw)
        try:
            yield proc
        except:
            proc.kill()
            proc.wait()
            raise

    def get_output(self, *args, **kw):
        kw['stdout'] = subprocess.PIPE
        ignore_stderr = kw.pop('ignore_stderr', False)
        if ignore_stderr:
            devnull = open(os.path.devnull, 'wb')
            kw['stderr'] = devnull
        else:
            kw['stderr'] = subprocess.STDOUT
        try:
            with self._popen(args, **kw) as proc:
                stdout, stderr = proc.communicate()
                return proc.returncode, stdout
        finally:
            if ignore_stderr:
                devnull.close()

    def check_output(self, *args, **kw):
        exitcode, output = self.get_output(*args, **kw)
        if exitcode:
            sys.stdout.write(output)
            sys.stdout.flush()
            sys.exit(1)
        return output

    def run_command(self, *args, **kw):
        with self._popen(args, **kw) as proc:
            exitcode = proc.wait()
        if exitcode:
            sys.exit(exitcode)

    def get_local_changes(self):
        status = self.check_output(GIT, 'status', '--porcelain')
        return [line for line in status.splitlines()
                if not line.startswith("?")]

    def remove_directory(self, name):
        path = os.path.join(self.root, name)
        if os.path.exists(path):
            if self.verbose:
                print("Remove directory: %s" % name)
            shutil.rmtree(path)

    def remove_file(self, name):
        path = os.path.join(self.root, name)
        if os.path.exists(path):
            if self.verbose:
                print("Remove file: %s" % name)
            os.unlink(path)

    def windows_sdk_setenv(self, pyver):
        if (pyver.major, pyver.minor) >= (3, 3):
            path = "v7.1"
            sdkver = (7, 1)
        else:
            path = "v7.0"
            sdkver = (7, 0)
        setenv = os.path.join(SDK_ROOT, path, 'Bin', 'SetEnv.cmd')
        if not os.path.exists(setenv):
            print("Unable to find Windows SDK %s.%s for %s"
                  % (sdkver[0], sdkver[1], pyver))
            print("Please download and install it")
            print("%s does not exists" % setenv)
            sys.exit(1)
        if pyver.bits == 64:
            arch = '/x64'
        else:
            arch = '/x86'
        cmd = ["CALL", setenv, "/release", arch]
        return (cmd, sdkver)

    def quote(self, arg):
        if not re.search("[ '\"]", arg):
            return arg
        # FIXME: should we escape "?
        return '"%s"' % arg

    def quote_args(self, args):
        return ' '.join(self.quote(arg) for arg in args)

    def cleanup(self):
        if self.verbose:
            print("Cleanup")
        self.remove_directory('build')
        self.remove_directory('dist')
        self.remove_file(LIBRARY )

    def sdist_upload(self):
        self.cleanup()
        self.run_command(sys.executable, 'setup.py', 'sdist', 'upload')

    def build_inplace(self, pyver):
        print("Build for %s" % pyver)
        self.build(pyver, 'build')

        if WINDOWS:
            if pyver.bits == 64:
                arch = 'win-amd64'
            else:
                arch = 'win32'
        else:
            arch = 'linux'
            if pyver.bits == 64:
                arch += '-x86_64'
            else:
                arch += '-x86'
        build_dir = 'lib.%s-%s.%s' % (arch, pyver.major, pyver.minor)
        if pyver.get_pydebug(self):
            build_dir += '-pydebug'
        src = os.path.join(self.root, 'build', build_dir, LIBRARY)
        dst = os.path.join(self.root, LIBRARY)
        shutil.copyfile(src, dst)

    def runtests(self, pyver):
        print("Run tests on %s" % pyver)

        if not self.options.no_compile:
            self.build_inplace(pyver)

        python = pyver.get_executable(self)
        args = (python, 'tests.py')

        print("Run tests.py on %s" % pyver)
        self.run_command(*args)

        print("")

    def _build_windows(self, pyver, cmd):
        setenv, sdkver = self.windows_sdk_setenv(pyver)

        temp = tempfile.NamedTemporaryFile(mode="w", suffix=".bat",
                                           delete=False)
        with temp:
            temp.write("SETLOCAL EnableDelayedExpansion\n")
            temp.write(self.quote_args(setenv) + "\n")
            temp.write(BATCH_FAIL_ON_ERROR + "\n")
            # Restore console colors: lightgrey on black
            temp.write("COLOR 07\n")
            temp.write("\n")
            temp.write("SET DISTUTILS_USE_SDK=1\n")
            temp.write("SET MSSDK=1\n")
            temp.write("CD %s\n" % self.quote(self.root))
            temp.write(self.quote_args(cmd) + "\n")
            temp.write(BATCH_FAIL_ON_ERROR + "\n")

        try:
            if self.verbose:
                print("Setup Windows SDK %s.%s" % sdkver)
                print("+ " + ' '.join(cmd))
            # SDK 7.1 uses the COLOR command which makes SetEnv.cmd failing
            # if the stdout is not a TTY (if we redirect stdout into a file)
            if self.verbose or sdkver >= (7, 1):
                self.run_command(temp.name, verbose=False)
            else:
                self.check_output(temp.name, verbose=False)
        finally:
            os.unlink(temp.name)

    def _build_unix(self, pyver, cmd):
        self.check_output(*cmd)

    def build(self, pyver, *cmds):
        self.cleanup()

        python = pyver.get_executable(self)
        cmd = [python, 'setup.py'] + list(cmds)

        if WINDOWS:
            self._build_windows(pyver, cmd)
        else:
            self._build_unix(pyver, cmd)

    def test_wheel(self, pyver):
        print("Test building wheel package for %s" % pyver)
        self.build(pyver, 'bdist_wheel')

    def publish_wheel(self, pyver):
        print("Build and publish wheel package for %s" % pyver)
        self.build(pyver, 'bdist_wheel', 'upload')

    def parse_options(self):
        parser = optparse.OptionParser(
            description="Run all unittests.",
            usage="%prog [options] command")
        parser.add_option(
            '-v', '--verbose', action="store_true", dest='verbose',
            default=0, help='verbose')
        parser.add_option(
            '-t', '--tag', type="str",
            help='Mercurial tag or revision, required to release')
        parser.add_option(
            '-p', '--python', type="str",
            help='Only build/test one specific Python version, ex: "2.7:32"')
        parser.add_option(
            '-C', "--no-compile", action="store_true",
            help="Don't compile the module, this options implies --running",
            default=False)
        parser.add_option(
            '-r', "--running", action="store_true",
            help='Only use the running Python version',
            default=False)
        parser.add_option(
            '--ignore', action="store_true",
            help='Ignore local changes',
            default=False)
        self.options, args = parser.parse_args()
        if len(args) == 1:
            command = args[0]
        else:
            command = None

        if self.options.no_compile:
            self.options.running = True

        if command == 'clean':
            self.options.verbose = True
        elif command == 'build':
            self.options.running = True
        elif command == 'test_wheel':
            self.wheel = True
        elif command == 'test':
            self.test = True
        elif command == 'release':
            if not self.options.tag:
                print("The release command requires the --tag option")
                sys.exit(1)

            self.release = True
            self.wheel = True
            self.test = True
            self.upload = True
        else:
            if command:
                print("Invalid command: %s" % command)
            else:
                parser.print_help()
                print("")

            print("Available commands:")
            print("- build: build in place, imply --running")
            print("- test: run tests")
            print("- test_wheel: test building wheel packages")
            print("- release: run tests and publish wheel packages,")
            print("  require the --tag option")
            print("- clean: cleanup the project")
            sys.exit(1)

        if self.options.python and self.options.running:
            print("--python and --running options are exclusive")
            sys.exit(1)

        python = self.options.python
        if python:
            match = re.match("^([23])\.([0-9])/(32|64)$", python)
            if not match:
                print("Invalid Python version: %s" % python)
                print('Format of a Python version: "x.y/bits"')
                print("Example: 2.7/32")
                sys.exit(1)
            major = int(match.group(1))
            minor = int(match.group(2))
            bits = int(match.group(3))
            self.python_versions = [PythonVersion(major, minor, bits)]

        if self.options.running:
            self.python_versions = [PythonVersion.running()]

        self.verbose = self.options.verbose
        self.command = command

    def main(self):
        self.parse_options()

        print("Directory: %s" % self.root)
        os.chdir(self.root)

        if self.command == "clean":
            self.cleanup()
            sys.exit(1)

        if self.command == "build":
            if len(self.python_versions) != 1:
                print("build command requires one specific Python version")
                print("Use the --python command line option")
                sys.exit(1)
            pyver = self.python_versions[0]
            self.build_inplace(pyver)

        if (self.register or self.upload) and (not self.options.ignore):
            lines = self.get_local_changes()
        else:
            lines = ()
        if lines:
            print("ERROR: Found local changes")
            for line in lines:
                print(line)
            print("")
            print("Revert local changes")
            print("or use the --ignore command line option")
            sys.exit(1)

        git_tag = self.options.tag
        if git_tag:
            print("Update repository to revision %s" % git_tag)
            self.check_output(GIT, 'checkout', git_tag)

        # FIXME
        #git_rev = self.check_output(HG, 'id').rstrip()
        git_rev = 'FIXME'

        if self.wheel:
            for pyver in self.python_versions:
                self.test_wheel(pyver)

        if self.test:
            for pyver in self.python_versions:
                self.runtests(pyver)

        if self.register:
            self.run_command(sys.executable, 'setup.py', 'register')

        if self.sdist:
            self.sdist_upload()

        if self.upload:
            for pyver in self.python_versions:
                self.publish_wheel(pyver)

        # FIXME
        #git_rev2 = self.check_output(HG, 'id').rstrip()
        git_rev2 = 'FIXME'
        if git_rev != git_rev2:
            print("ERROR: The Mercurial revision changed")
            print("Before: %s" % git_rev)
            print("After: %s" % git_rev2)
            sys.exit(1)

        print("")
        print("Git revision: %s" % git_rev)
        if self.command == 'build':
            print("Inplace compilation done")
        if self.wheel:
            print("Compilation of wheel packages succeeded")
        if self.test:
            print("Tests succeeded")
        if self.register:
            print("Project registered on the Python cheeseshop (PyPI)")
        if self.sdist:
            print("Project source code uploaded to the Python "
                  "cheeseshop (PyPI)")
        if self.upload:
            print("Wheel packages uploaded to the Python cheeseshop (PyPI)")
        for pyver in self.python_versions:
            print("- %s" % pyver)


if __name__ == "__main__":
    Release().main()
