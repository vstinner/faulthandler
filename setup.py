#!/usr/bin/env python

# Todo list to prepare a release:
#  - set VERSION: faulthandler.c, setup.py, doc/conf.py, doc/index.rst
#  - update the changelog: doc/index.rst
#  - run tests with run tox
#  - run tests on Linux, Windows, FreeBSD and Mac OS X:
#    test at least Python 2.7
#  - set release date in the changelog: doc/index.rst
#  - git commit -a
#  - git tag -a faulthandler-x.y -m "tag version x.y"
#  - git push
#  - git push --tags
#  - python setup.py register sdist upload
#  - Build 32-bit and 64-bit wheel packages on Windows:
#
#    - python2.6 setup.py bdist_wheel upload
#    - python2.7 setup.py bdist_wheel upload
#    - python3.2 setup.py bdist_wheel upload
#
#  - update the website
#
# After the release:
#  - increment VERSION: faulthandler.c, setup.py, doc/conf.py, doc/index.rst
#  - git commit -a
#  - git push

from __future__ import with_statement

import sys
from os.path import join as path_join
from os.path import basename
from os.path import dirname
from distutils.command.build import build
from setuptools import Command
from setuptools import Extension
from setuptools import setup
from setuptools.command.develop import develop
from setuptools.command.easy_install import easy_install

if sys.version_info >= (3, 3):
    print("ERROR: faulthandler is a builtin module since Python 3.3")
    sys.exit(1)


class BuildWithPTH(build):
    def run(self):
        build.run(self)
        path = path_join(dirname(__file__), 'faulthandler.pth')
        dest = path_join(self.build_lib, basename(path))
        self.copy_file(path, dest)


class EasyInstallWithPTH(easy_install):
    def run(self):
        easy_install.run(self)
        path = path_join(dirname(__file__), 'faulthandler.pth')
        dest = path_join(self.install_dir, basename(path))
        self.copy_file(path, dest)


class DevelopWithPTH(develop):
    def run(self):
        develop.run(self)
        path = path_join(dirname(__file__), 'faulthandler.pth')
        dest = path_join(self.install_dir, basename(path))
        self.copy_file(path, dest)


class GeneratePTH(Command):
    user_options = []

    def initialize_options(self):
        pass

    def finalize_options(self):
        pass

    def run(self):
        with open(path_join(dirname(__file__), 'faulthandler.pth'), 'w') as fh:
            with open(path_join(dirname(__file__), 'faulthandler.embed')) as sh:
                fh.write(
                    'import os, sys;'
                    'exec(%r)' % sh.read().replace('    ', ' ')
                )


VERSION = "2.5"

FILES = ['faulthandler.c', 'traceback.c']

CLASSIFIERS = [
    'Development Status :: 5 - Production/Stable',
    'Intended Audience :: Developers',
    'License :: OSI Approved :: BSD License',
    'Operating System :: OS Independent',
    'Natural Language :: English',
    'Programming Language :: C',
    'Programming Language :: Python',
    'Programming Language :: Python :: 3',
    'Topic :: Software Development :: Debuggers',
    'Topic :: Software Development :: Libraries :: Python Modules',
]

with open('README') as f:
    long_description = f.read().strip()

options = {
    'name': "faulthandler",
    'version': VERSION,
    'license': "BSD (2-clause)",
    'description': 'Display the Python traceback on a crash',
    'long_description': long_description,
    'url': "https://faulthandler.readthedocs.io/",
    'author': 'Victor Stinner',
    'author_email': 'victor.stinner@gmail.com',
    'ext_modules': [Extension('faulthandler', FILES)],
    'classifiers': CLASSIFIERS,
    'cmdclass': {
        'build': BuildWithPTH,
        'easy_install': EasyInstallWithPTH,
        'develop': DevelopWithPTH,
        'genpth': GeneratePTH,
    },
}

setup(**options)

