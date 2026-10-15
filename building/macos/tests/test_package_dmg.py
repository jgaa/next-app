"""Exercise packaging failures without requiring macOS or rebuilding the app."""

import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "package-dmg.sh"
MOCK_HDIUTIL = r'''#!/usr/bin/env python3
import json
import os
from pathlib import Path
import sys

args = sys.argv[1:]
with open(os.environ["CALL_LOG"], "a") as log:
    log.write(json.dumps(args) + "\n")
if args[0] == os.environ.get("FAIL_COMMAND"):
    sys.exit(7)
if args[0] in ("makehybrid", "convert"):
    Path(args[args.index("-o") + 1]).write_bytes(b"test image")
elif args[0] == "verify":
    assert Path(args[-1]).read_bytes() == b"test image"
else:
    sys.exit("Unexpected hdiutil command: " + args[0])
'''


class PackageDmgTests(unittest.TestCase):
    def run_package(self, fail_command=""):
        with tempfile.TemporaryDirectory(prefix="dmg test ") as directory:
            root = Path(directory)
            source = root / "source folder"
            source.mkdir()
            (source / "nextapp.app").mkdir()
            (source / "Applications").symlink_to("/Applications")
            mock = root / "hdiutil"
            mock.write_text(MOCK_HDIUTIL)
            mock.chmod(0o755)
            output = root / "nextapp.dmg"
            output.write_bytes(b"previous image")
            log = root / "calls.jsonl"
            result = subprocess.run(
                ["bash", str(SCRIPT), str(source), "NextApp test version", str(output)],
                env={**os.environ, "PATH": f"{root}:{os.environ['PATH']}",
                     "CALL_LOG": str(log), "FAIL_COMMAND": fail_command},
                capture_output=True, text=True,
            )
            calls = [json.loads(line) for line in log.read_text().splitlines()]
            self.assertEqual(result.returncode, 7 if fail_command else 0, result.stderr)
            self.assertEqual(output.read_bytes(),
                             b"previous image" if fail_command else b"test image")
            self.assertEqual(list(root.glob(".nextapp-dmg.*")), [])
            self.assertTrue((source / "Applications").is_symlink())
            return calls

    def test_success_preserves_paths_and_volume_name(self):
        calls = self.run_package()
        self.assertEqual([call[0] for call in calls], ["makehybrid", "convert", "verify"])
        self.assertIn("-hfs", calls[0])
        self.assertEqual(calls[0][calls[0].index("-hfs-volume-name") + 1],
                         "NextApp test version")
        self.assertEqual(calls[1][calls[1].index("-format") + 1], "UDZO")

    def test_failures_stop_without_publishing_or_leaving_intermediates(self):
        commands = ["makehybrid", "convert", "verify"]
        for index, command in enumerate(commands):
            with self.subTest(command=command):
                calls = self.run_package(command)
                self.assertEqual([call[0] for call in calls], commands[:index + 1])


if __name__ == "__main__":
    unittest.main()
