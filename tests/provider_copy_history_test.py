"""Keep local recovery-storage checks inside a private temporary namespace."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root = Path(tempfile.mkdtemp(prefix="luciole-history-test-"))
env = dict(os.environ, TMPDIR=str(root), LUCIOLE_HISTORY_TEST_TMP=str(root))
try:
    result = subprocess.run(sys.argv[1:], env=env, check=False)
except BaseException:
    print(f"History fixture retained at {root}", file=sys.stderr)
    raise
if result.returncode == 0:
    shutil.rmtree(root)
else:
    print(f"History fixture retained at {root}", file=sys.stderr)
sys.exit(result.returncode)
