# protocol tests for music_helper, with fakes instead of ytmusicapi and yt-dlp. no network needed.

import io
import json
import pathlib
import sys
import threading
import unittest
import unittest.mock

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "helper"))

import music_helper  # noqa: E402
from youtube_music import HelperError  # noqa: E402


class FakeMusic:
    def __init__(self, result=None, error=None):
        self.result = result if result is not None else {"tracks": []}
        self.error = error
        self.calls = []

    def _call(self, name, *args):
        self.calls.append((name,) + args)
        if self.error:
            raise self.error
        return self.result

    def search(self, query, kind):
        return self._call("search", query, kind)

    def album(self, browse_id):
        return self._call("album", browse_id)

    def artist(self, browse_id):
        return self._call("artist", browse_id)

    def playlist(self, browse_id):
        return self._call("playlist", browse_id)

    def home(self):
        return self._call("home")

    def radio(self, video_id):
        return self._call("radio", video_id)

    def radio_like(self, query):
        return self._call("radio_like", query)


class FakeStreams:
    def __init__(self, result=None, error=None, block_on=None):
        self.result = result or {"url": "https://example.test/audio", "expires_epoch": 0, "duration_seconds": 0}
        self.error = error
        self.block_on = block_on
        self.video_ids = []
        self.prepared = []

    def prepare(self, video_id):
        self.prepared.append(video_id)
        return {}

    def find_audio(self, video_id):
        self.video_ids.append(video_id)
        if self.block_on:
            self.block_on.wait(timeout=10)
        if self.error:
            raise self.error
        return self.result


class CollectingOutput:
    """Thread-safe stdout stand-in that lets a test wait for a number of lines."""

    def __init__(self):
        self.lines = []
        self._cond = threading.Condition()

    def write(self, text):
        with self._cond:
            self.lines.extend(line for line in text.split("\n") if line)
            self._cond.notify_all()

    def flush(self):
        pass

    def responses(self):
        with self._cond:
            return [json.loads(line) for line in self.lines]

    def wait_for(self, count):
        with self._cond:
            return self._cond.wait_for(lambda: len(self.lines) >= count, timeout=10)


def make_helper(music=None, streams=None):
    return music_helper.Helper(music or FakeMusic(), streams or FakeStreams())


class HandleTest(unittest.TestCase):
    def test_search_returns_result_with_id(self):
        music = FakeMusic({"tracks": [{"video_id": "a"}]})
        response = make_helper(music).handle({"id": 7, "command": "search", "query": "aurora"})
        self.assertEqual(response, {"id": 7, "ok": True, "tracks": [{"video_id": "a"}]})
        self.assertEqual(music.calls, [("search", "aurora", "songs")])

    def test_search_passes_kind(self):
        music = FakeMusic({"cards": []})
        make_helper(music).handle({"id": 1, "command": "search", "query": "x", "kind": "albums"})
        self.assertEqual(music.calls, [("search", "x", "albums")])

    def test_commands_dispatch_to_music(self):
        music = FakeMusic({"title": "T"})
        helper = make_helper(music)
        for command, args, expected in [
            ("album", {"browse_id": "MPREb_1"}, ("album", "MPREb_1")),
            ("artist", {"browse_id": "UC1"}, ("artist", "UC1")),
            ("playlist", {"browse_id": "VLPL1"}, ("playlist", "VLPL1")),
            ("radio", {"video_id": "abc"}, ("radio", "abc")),
            ("radio_like", {"query": "Ane Blå"}, ("radio_like", "Ane Blå")),
            ("home", {}, ("home",)),
        ]:
            with self.subTest(command=command):
                response = helper.handle({"id": 1, "command": command, **args})
                self.assertTrue(response["ok"])
                self.assertEqual(response["title"], "T")
                self.assertEqual(music.calls[-1], expected)

    def test_stream_returns_url(self):
        streams = FakeStreams({"url": "https://example.test/x", "expires_epoch": 5, "duration_seconds": 215})
        response = make_helper(streams=streams).handle({"id": 8, "command": "stream", "video_id": "abc"})
        self.assertEqual(
            response,
            {"id": 8, "ok": True, "url": "https://example.test/x", "expires_epoch": 5, "duration_seconds": 215},
        )
        self.assertEqual(streams.video_ids, ["abc"])

    def test_prepare_answers_with_nothing_but_ok(self):
        streams = FakeStreams()
        response = make_helper(streams=streams).handle({"id": 9, "command": "prepare", "video_id": "abc"})
        self.assertEqual(response, {"id": 9, "ok": True})
        self.assertEqual(streams.prepared, ["abc"])

    def test_local_lists_the_music_folders(self):
        class FakeLocal:
            def songs(self):
                return {"folders": ["~/Music"], "tracks": [{"video_id": "file:///m/a.flac"}]}

        helper = music_helper.Helper(FakeMusic(), FakeStreams(), FakeLocal())
        response = helper.handle({"id": 3, "command": "local"})
        self.assertEqual(
            response, {"id": 3, "ok": True, "folders": ["~/Music"], "tracks": [{"video_id": "file:///m/a.flac"}]}
        )

    def test_local_without_folders_fails_politely(self):
        response = make_helper().handle({"id": 3, "command": "local"})
        self.assertFalse(response["ok"])
        self.assertIn("music folders", response["error"])

    def test_unknown_command_lists_known_ones(self):
        response = make_helper().handle({"id": 1, "command": "dance"})
        self.assertFalse(response["ok"])
        self.assertIn("unknown command", response["error"])
        for known in music_helper.REQUIRED_ARGS:
            self.assertIn(known, response["error"])

    def test_missing_or_empty_args_rejected(self):
        for command, name in [
            ("search", "query"), ("stream", "video_id"), ("prepare", "video_id"), ("album", "browse_id"),
            ("artist", "browse_id"), ("playlist", "browse_id"), ("radio", "video_id"), ("radio_like", "query"),
        ]:
            for value in (None, "", "   ", 5):
                with self.subTest(command=command, value=value):
                    request = {"id": 1, "command": command}
                    if value is not None:
                        request[name] = value
                    response = make_helper().handle(request)
                    self.assertFalse(response["ok"])
                    self.assertIn(name, response["error"])

    def test_non_object_request_rejected(self):
        response = make_helper().handle(["search"])
        self.assertFalse(response["ok"])
        self.assertIsNone(response["id"])

    def test_helper_error_message_passed_through(self):
        music = FakeMusic(error=HelperError("yt-dlp is not installed"))
        response = make_helper(music).handle({"id": 3, "command": "search", "query": "x"})
        self.assertEqual(response, {"id": 3, "ok": False, "error": "yt-dlp is not installed"})

    def test_unexpected_exception_becomes_error_response(self):
        music = FakeMusic(error=RuntimeError("network is down"))
        response = make_helper(music).handle({"id": 4, "command": "search", "query": "x"})
        self.assertFalse(response["ok"])
        self.assertIn("RuntimeError", response["error"])
        self.assertIn("network is down", response["error"])


class OptionsTest(unittest.TestCase):
    def test_cache_dir_follows_xdg_cache_home(self):
        with unittest.mock.patch.dict("os.environ", {"XDG_CACHE_HOME": "/tmp/xdg"}):
            self.assertEqual(music_helper.default_cache_dir(), "/tmp/xdg/bandstand/audio")
        with unittest.mock.patch.dict("os.environ", {"XDG_CACHE_HOME": "", "HOME": "/home/someone"}):
            self.assertEqual(music_helper.default_cache_dir(), "/home/someone/.cache/bandstand/audio")


class ServeTest(unittest.TestCase):
    def test_answers_each_line_and_skips_bad_ones(self):
        helper = make_helper(FakeMusic({"tracks": [{"video_id": "a"}]}))
        output = io.StringIO()
        lines = [
            '{"id": 1, "command": "search", "query": "a"}\n',
            "\n",
            "this is not json\n",
            '{"id": 2, "command": "stream", "video_id": "a"}\n',
        ]
        music_helper.serve(lines, output, helper)

        responses = [json.loads(line) for line in output.getvalue().splitlines()]
        self.assertEqual(len(responses), 3)
        by_id = {r["id"]: r for r in responses}
        self.assertTrue(by_id[1]["ok"])
        self.assertTrue(by_id[2]["ok"])
        self.assertFalse(by_id[None]["ok"])
        self.assertIn("invalid JSON", by_id[None]["error"])

    def test_slow_request_does_not_block_others(self):
        release = threading.Event()
        helper = make_helper(streams=FakeStreams(block_on=release))
        output = CollectingOutput()
        lines = [
            '{"id": 1, "command": "stream", "video_id": "slow"}\n',
            '{"id": 2, "command": "search", "query": "quick"}\n',
        ]
        server = threading.Thread(target=music_helper.serve, args=(lines, output, helper))
        server.start()

        self.assertTrue(output.wait_for(1), "search wasn't answered while stream was blocked")
        self.assertEqual([r["id"] for r in output.responses()], [2])

        release.set()
        server.join(timeout=10)
        self.assertEqual(sorted(r["id"] for r in output.responses()), [1, 2])


if __name__ == "__main__":
    unittest.main()
