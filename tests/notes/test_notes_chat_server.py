from __future__ import annotations

import io
import sys
import unittest
from unittest import mock

from scripts.notes import notes_chat_server


class NotesChatServerTest(unittest.TestCase):
    def test_invalid_agent_env_default_reports_argparse_error(self) -> None:
        stderr = io.StringIO()
        with (
            mock.patch.dict("os.environ", {"NOTES_CHAT_AGENT": "bad"}, clear=True),
            mock.patch.object(sys, "argv", ["notes_chat_server.py", "--port", "0"]),
            mock.patch("sys.stderr", stderr),
        ):
            with self.assertRaises(SystemExit) as raised:
                notes_chat_server.main()

        self.assertEqual(raised.exception.code, 2)
        self.assertIn("argument --agent: invalid choice: 'bad'", stderr.getvalue())
        self.assertNotIn("Traceback", stderr.getvalue())


if __name__ == "__main__":
    unittest.main()
