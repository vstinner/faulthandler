#!/usr/bin/env python

# Todo list to prepare a release:
#  - run python tests.py
#  - run tests on FreeBSD and Windows
#  - set VERSION in faulthandler/module.c
#  - set VERSION in setup.py
#  - set release date in the ChangeLog (README file)
#  - git commit -a
#  - git tag -a faulthandler-x.y -m "tag version x.y"
#  - git push
#  - git push --tags
#  - python setup.py register sdist upload
#  - python2.6 setup.py bdist_wininst upload
#  - python2.7 setup.py bdist_wininst upload
#  - python3.1 setup.py bdist_wininst upload
#  - update the website
#
# After the release:
#  - increment VERSION in faulthandler/module.c
#  - increment VERSION in setup.py
#  - add a new empty section in the Changelog for the new version
#  - git commit -a
#  - git push

from __future__ import with_statement
from distutils.core import setup, Extension
from os.path import join as path_join

VERSION = "1.4"

FILENAMES = (
    'backtrace.c',
    'fatal_error.c',
    'later.c',
    'module.c',
    'tests.c',
    'user.c',
)

CLASSIFIERS = [
    'Development Status :: 5 - Production/Stable',
    'Intended Audience :: Developers',
    'License :: OSI Approved :: BSD License',
    'Operating System :: OS Independent',
    'Natural Language :: English',
    'Programming Language :: C',
    'Programming Language :: Python',
    'Topic :: Software Development :: Debuggers',
    'Topic :: Software Development :: Libraries :: Python Modules',
]

with open('README') as f:
    long_description = f.read().strip()

options = {
    'name': "faulthandler",
    'version': VERSION,
    'license': "BSD (2-clause)",
    'description': 'Display the Python backtrace on a crash',
    'long_description': long_description,
    'url': "https://github.com/haypo/faulthandler/wiki/",
    'author': 'Victor Stinner',
    'author_email': 'victor.stinner@haypocalc.com',
    'ext_modules': [Extension('faulthandler',
        [path_join('faulthandler', filename) for filename in FILENAMES]
    )],
    'classifiers': CLASSIFIERS,
}

setup(**options)

