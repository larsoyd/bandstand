# helper process for the music app: searches youtube music and resolves stream urls.
# speaks json lines on stdin/stdout, one request per line, one response per line.
#
# request:  {"id": 1, "command": "search", "query": "aurora vale", "kind": "albums"}
# response: {"id": 1, "ok": true, ...}  or  {"id": 1, "ok": false, "error": "..."}
#
# commands (args -> response fields, track/card shapes are in youtube_music.py):
#   search    query, kind = songs|albums|artists|playlists  -> tracks (songs) or cards
#   album     browse_id  -> title, artist, year, duration_text, thumbnail_url, tracks
#   artist    browse_id  -> name, description, thumbnail_url, songs, albums, singles
#   playlist  browse_id  -> title, author, duration_text, thumbnail_url, tracks
#   home                 -> shelves: [{title, items}], items are tracks or cards
#   radio     video_id   -> tracks, the seed song first
#   stream    video_id   -> url, expires_epoch (0 if unknown), duration_seconds
#   prepare   video_id   -> nothing, once the song is looked up; it then downloads
#                           into the song cache, so playing it later starts at once
#   local                -> folders (as shown to the user), tracks: the songs in the
#                           music folders, see local_music.py. the app plays these itself
#
# options: --cache-dir DIR (default $XDG_CACHE_HOME/bandstand/audio) and
# --cache-limit-mb N (default 2048, 0 keeps no songs on disk). see song_cache.py.
# --music-folder DIR, repeatable: where local songs are (default: none, so "local" fails).
#
# requests run concurrently (stream lookups take seconds), so responses can come
# back out of order. match them by id.

import argparse
import json
import os
import sys
import threading
from concurrent.futures import ThreadPoolExecutor

from audio_relay import AudioRelay
from local_music import LocalMusic
from song_cache import SongCache
from streams import YtDlpStreams
from youtube_music import HelperError, YouTubeMusic

# a search sends four requests at once; leave room so a song lookup never waits behind it
MAX_WORKERS = 8

DEFAULT_CACHE_LIMIT_MB = 2048

REQUIRED_ARGS = {
    "search": ("query",),
    "stream": ("video_id",),
    "prepare": ("video_id",),
    "album": ("browse_id",),
    "artist": ("browse_id",),
    "playlist": ("browse_id",),
    "radio": ("video_id",),
    "radio_like": ("query",),
    "home": (),
    "local": (),
}


class Helper:
    def __init__(self, music, streams, local=None):
        self._music = music
        self._streams = streams
        self._local = local

    def handle(self, request):
        """Returns the response dict for one request. Never raises."""
        response = {"id": request.get("id") if isinstance(request, dict) else None}
        try:
            response.update(self._run(request))
            response["ok"] = True
        except HelperError as error:
            response.update({"ok": False, "error": str(error)})
        except Exception as error:  # noqa: BLE001 - a library crash must not kill the helper
            response.update({"ok": False, "error": "unexpected %s: %s" % (type(error).__name__, error)})
        return response

    def _run(self, request):
        if not isinstance(request, dict):
            raise HelperError("request must be a JSON object")

        command = request.get("command")
        if command not in REQUIRED_ARGS:
            raise HelperError("unknown command %r (expected one of: %s)" % (command, ", ".join(REQUIRED_ARGS)))

        for name in REQUIRED_ARGS[command]:
            value = request.get(name)
            if not isinstance(value, str) or not value.strip():
                raise HelperError('%s requires a non-empty "%s"' % (command, name))

        if command == "search":
            return self._music.search(request["query"], request.get("kind", "songs"))
        if command == "stream":
            return self._streams.find_audio(request["video_id"])
        if command == "prepare":
            return self._streams.prepare(request["video_id"])
        if command == "album":
            return self._music.album(request["browse_id"])
        if command == "artist":
            return self._music.artist(request["browse_id"])
        if command == "playlist":
            return self._music.playlist(request["browse_id"])
        if command == "radio":
            return self._music.radio(request["video_id"])
        if command == "local":
            if self._local is None:
                raise HelperError("No music folders are set.")
            return self._local.songs()
        if command == "radio_like":
            return self._music.radio_like(request["query"])
        return self._music.home()


def serve(lines, output, helper):
    write_lock = threading.Lock()

    def send(response):
        line = json.dumps(response, ensure_ascii=False)
        with write_lock:
            output.write(line + "\n")
            output.flush()

    def respond(request):
        send(helper.handle(request))

    # leaving the with block waits for pending requests, so nothing is lost on eof
    with ThreadPoolExecutor(max_workers=MAX_WORKERS) as pool:
        for line in lines:
            if not line.strip():
                continue
            try:
                request = json.loads(line)
            except json.JSONDecodeError as error:
                send({"id": None, "ok": False, "error": "invalid JSON: %s" % error})
                continue
            pool.submit(respond, request)


def cache_home():
    base = os.environ.get("XDG_CACHE_HOME") or os.path.join(os.path.expanduser("~"), ".cache")
    return os.path.join(base, "bandstand")


def default_cache_dir():
    return os.path.join(cache_home(), "audio")


def main():
    parser = argparse.ArgumentParser(description="search youtube music and play songs, over json lines")
    parser.add_argument("--cache-dir", default=default_cache_dir())
    parser.add_argument("--cache-limit-mb", type=int, default=DEFAULT_CACHE_LIMIT_MB)
    parser.add_argument("--music-folder", action="append", default=[])
    options = parser.parse_args()

    cache = None
    if options.cache_limit_mb > 0:
        cache = SongCache(options.cache_dir, options.cache_limit_mb * 1024 * 1024)
        # a smaller limit than last time, or leftovers of a crash
        cache.evict()
    relay = AudioRelay(cache)
    local = LocalMusic(options.music_folder, os.path.join(cache_home(), "local"))
    serve(sys.stdin, sys.stdout, Helper(YouTubeMusic(), YtDlpStreams(relay, cache), local))


if __name__ == "__main__":
    main()
