#!/usr/bin/python
# encoding=UTF-8

# Copyright © 2012 Jakub Wilk <jwilk@jwilk.net>
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the “Software”), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

import os
import random
import subprocess as ipc
import tempfile

import nose
from nose.tools import *

def random_string(size):
    return ''.join(
        chr(random.randint(0, 0xff))
        for i in xrange(0, size)
    )

random_blob = random_string(1 << 14)


def run_hungrycat(options, input_):
    fd, input_file = tempfile.mkstemp(prefix='hungrycat')
    os.write(fd, input_)
    os.close(fd)
    child = ipc.Popen(
        ['./hungrycat'] + map(str, options) + [input_file],
        stdout=ipc.PIPE,
        stderr=ipc.PIPE,
    )
    output = child.stdout.read()
    errors = child.stderr.read()
    rc = child.wait()
    if rc == 0:
        assert_false(os.path.exists(input_file))
    else:
        if os.path.exists(input_file):
            os.unlink(input_file)
    return output, errors, rc

def _test_ftruncate(size, block_size):
    input_ = random_blob[:size]
    output, errors, rc = run_hungrycat(['-s', block_size], input_)
    assert_equal(output, input_)
    assert_equal(errors, '')
    assert_equal(rc, 0)

def test_ftrunacate():
    for size in xrange(0, 26 + 1):
        for block_size in xrange(1, 32 + 1):
            yield _test_ftruncate, size, block_size

if __name__ == '__main__':
    nose.main()

# vim:ts=4 sw=4 et