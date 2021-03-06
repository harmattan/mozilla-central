# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import unicode_literals

import os

from .data import (
    DirectoryTraversal,
    ConfigFileSubstitution,
)

from .reader import MozbuildSandbox


class TreeMetadataEmitter(object):
    """Converts the executed mozbuild files into data structures.

    This is a bridge between reader.py and data.py. It takes what was read by
    reader.BuildReader and converts it into the classes defined in the data
    module.
    """

    def __init__(self, config):
        self.config = config

    def emit(self, output):
        """Convert the BuildReader output into data structures.

        The return value from BuildReader.read_topsrcdir() (a generator) is
        typically fed into this function.
        """
        for out in output:
            if isinstance(out, MozbuildSandbox):
                for o in self.emit_from_sandbox(out):
                    yield o
            else:
                raise Exception('Unhandled output type: %s' % out)

    def emit_from_sandbox(self, sandbox):
        """Convert a MozbuildSandbox to tree metadata objects.

        This is a generator of mozbuild.frontend.data.SandboxDerived instances.
        """

        # We always emit a directory traversal descriptor. This is needed by
        # the recursive make backend.
        for o in self._emit_directory_traversal_from_sandbox(sandbox): yield o

        for path in sandbox['CONFIGURE_SUBST_FILES']:
            if os.path.isabs(path):
                path = path[1:]

            sub = ConfigFileSubstitution(sandbox)
            sub.input_path = os.path.join(sandbox['SRCDIR'], '%s.in' % path)
            sub.output_path = os.path.join(sandbox['OBJDIR'], path)
            yield sub

    def _emit_directory_traversal_from_sandbox(self, sandbox):
        o = DirectoryTraversal(sandbox)
        o.dirs = sandbox.get('DIRS', [])
        o.parallel_dirs = sandbox.get('PARALLEL_DIRS', [])
        o.tool_dirs = sandbox.get('TOOL_DIRS', [])
        o.test_dirs = sandbox.get('TEST_DIRS', [])
        o.test_tool_dirs = sandbox.get('TEST_TOOL_DIRS', [])
        o.external_make_dirs = sandbox.get('EXTERNAL_MAKE_DIRS', [])
        o.parallel_external_make_dirs = sandbox.get('PARALLEL_EXTERNAL_MAKE_DIRS', [])

        if 'TIERS' in sandbox:
            for tier in sandbox['TIERS']:
                o.tier_dirs[tier] = sandbox['TIERS'][tier]['regular']
                o.tier_static_dirs[tier] = sandbox['TIERS'][tier]['static']

        yield o

