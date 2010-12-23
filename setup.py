#!/usr/bin/env python

from __future__ import with_statement
from distutils.core import setup, Extension
from os.path import join as path_join

VERSION = "0.5"

FILENAMES = ('backtrace.c', 'handler.c', 'module.c', 'tests.c')

CLASSIFIERS = [
    'Development Status :: 5 - Production/Stable',
    'Intended Audience :: Developers',
    'License :: OSI Approved :: BSD License',
    'Operating System :: OS Independent',
    'Natural Language :: English',
    'Programming Language :: C',
    'Programming Language :: Python',
    'Topic :: Software Development :: Libraries :: Python Modules',
]

with open('README') as f:
    long_description = f.read().strip()

options = {
    'name': "faulthandler",
    'version': VERSION,
    'license': "BSD (2-clause)",
    'description': 'Python fault handler',
    'long_description': long_description,
    'url': "http://github.com/haypo/faulthandler/",
    'author': 'Victor Stinner',
    'author_email': 'victor.stinner@haypocalc.com',
    'ext_modules': [Extension('faulthandler',
        [path_join('faulthandler', filename) for filename in FILENAMES]
    )],
    'classifiers': CLASSIFIERS,
}

setup(**options)

