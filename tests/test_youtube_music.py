# tests for parsing ytmusicapi responses into tracks/cards, and for the retry logic.
# the fixtures are trimmed copies of real responses from 2026-10-21.

import pathlib
import sys
import threading
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "helper"))

import streams  # noqa: E402
import youtube_music  # noqa: E402
from youtube_music import HelperError, YouTubeMusic  # noqa: E402

THUMBNAILS = [
    {"url": "small", "width": 60}, {"url": "medium", "width": 226}, {"url": "large", "width": 544},
]

SEARCH_SONG = {
    "category": "Songs", "resultType": "song", "videoId": "abc123", "title": "Creep",
    "artists": [{"name": "Radiohead", "id": "UC1"}], "album": {"name": "Pablo Honey", "id": "MPREb_1"},
    "duration": "3:58", "duration_seconds": 238, "thumbnails": THUMBNAILS,
}
# album tracks have no thumbnails and a plain string album
ALBUM_TRACK = {
    "videoId": "eKbrty2x8rM", "title": "Cello Suite No. 1: I. Prélude",
    "artists": [{"name": "Yo-Yo Ma - Topic", "id": "UCF0"}], "album": "Bach: Cello Suites",
    "thumbnails": None, "duration": "2:32", "duration_seconds": 152,
}
PLAYLIST_TRACK = {
    "videoId": "ISPc0xa2WPM", "title": "Suite No. 1", "artists": [{"name": "China NCPA", "id": "UC2"}],
    "album": None, "thumbnails": [{"url": "https://i.ytimg.com/vi/x/hq.jpg", "width": 400}], "duration": "2:47",
    "duration_seconds": 167,
}
# radio tracks use "thumbnail" and "length"
RADIO_TRACK = {
    "videoId": "QWOvw77vPMk", "title": "Waltz No. 19", "length": "2:23", "thumbnail": THUMBNAILS,
    "artists": [{"name": "Someone", "id": "UC3"}], "album": {"name": "Waltzes", "id": "MPREb_2"},
}


class FakeApi:
    """Stands in for ytmusicapi.YTMusic. Raises `error` for the first `failures` calls."""

    def __init__(self, **results):
        self.results = results
        self.calls = []
        self.failures = 0
        self.error = KeyError("contents")

    def _call(self, name, *args, **kwargs):
        self.calls.append((name, args, kwargs))
        if self.failures > 0:
            self.failures -= 1
            raise self.error
        return self.results[name]

    def search(self, query, filter=None, limit=None):
        return self._call("search", query, filter=filter, limit=limit)

    def get_album(self, browse_id):
        return self._call("get_album", browse_id)

    def get_artist(self, browse_id):
        return self._call("get_artist", browse_id)

    def get_playlist(self, playlist_id, limit=None):
        return self._call("get_playlist", playlist_id, limit=limit)

    def get_home(self, limit=None):
        return self._call("get_home", limit=limit)

    def get_watch_playlist(self, videoId=None, radio=False, limit=None):
        return self._call("get_watch_playlist", videoId, radio=radio, limit=limit)


def make_music(api, sleeps=None):
    sleep = sleeps.append if sleeps is not None else (lambda seconds: None)
    return YouTubeMusic(api_factory=lambda: api, sleep=sleep)


class ThumbnailTest(unittest.TestCase):
    def test_picks_smallest_wide_enough(self):
        self.assertEqual(youtube_music.thumbnail_url(THUMBNAILS), "medium")

    def test_falls_back_to_widest(self):
        self.assertEqual(youtube_music.thumbnail_url([{"url": "a", "width": 60}, {"url": "b", "width": 120}]), "b")

    def test_empty(self):
        self.assertEqual(youtube_music.thumbnail_url(None), "")
        self.assertEqual(youtube_music.thumbnail_url([]), "")


class DurationTest(unittest.TestCase):
    def test_parses_clock_format(self):
        self.assertEqual(youtube_music.parse_duration("3:35"), 215)
        self.assertEqual(youtube_music.parse_duration("1:02:07"), 3727)
        self.assertEqual(youtube_music.parse_duration("59"), 59)

    def test_invalid_is_zero(self):
        for text in (None, "", "soon", "2 hours, 10 minutes"):
            self.assertEqual(youtube_music.parse_duration(text), 0)


class TrackTest(unittest.TestCase):
    def test_search_result(self):
        self.assertEqual(
            youtube_music.parse_track(SEARCH_SONG),
            {"video_id": "abc123", "title": "Creep", "artist": "Radiohead", "album": "Pablo Honey",
             "duration_seconds": 238, "thumbnail_url": "medium"},
        )

    def test_album_track_uses_album_thumbnail(self):
        track = youtube_music.parse_track(ALBUM_TRACK, fallback_thumbnails=THUMBNAILS, fallback_album="X")
        self.assertEqual(track["thumbnail_url"], "medium")
        self.assertEqual(track["album"], "Bach: Cello Suites")
        self.assertEqual(track["duration_seconds"], 152)

    def test_missing_album_uses_fallback(self):
        track = youtube_music.parse_track(PLAYLIST_TRACK, fallback_album="From the playlist")
        self.assertEqual(track["album"], "From the playlist")

    def test_radio_track(self):
        track = youtube_music.parse_track(RADIO_TRACK)
        self.assertEqual(track["duration_seconds"], 143)
        self.assertEqual(track["thumbnail_url"], "medium")

    def test_multiple_artists_joined(self):
        item = dict(SEARCH_SONG, artists=[{"name": "A"}, {"name": "B"}, {"id": "no name"}])
        self.assertEqual(youtube_music.parse_track(item)["artist"], "A, B")

    def test_item_without_video_skipped(self):
        self.assertIsNone(youtube_music.parse_track({"title": "removed video"}))
        tracks = youtube_music.parse_tracks([SEARCH_SONG, {"title": "gone"}, PLAYLIST_TRACK])
        self.assertEqual([t["video_id"] for t in tracks], ["abc123", "ISPc0xa2WPM"])


class CardTest(unittest.TestCase):
    def test_album(self):
        card = youtube_music.album_card({
            "title": "OK Computer", "browseId": "MPREb_9", "year": "1997",
            "artists": [{"name": "Radiohead"}], "thumbnails": THUMBNAILS,
        })
        self.assertEqual(card, {"kind": "album", "browse_id": "MPREb_9", "title": "OK Computer",
                                "subtitle": "Radiohead · 1997", "thumbnail_url": "medium"})

    def test_album_without_artists(self):
        card = youtube_music.album_card({"title": "KID A MNESIA", "browseId": "MPREb_E", "artists": [], "year": "2021"})
        self.assertEqual(card["subtitle"], "2021")

    def test_artist_from_search_and_shelf(self):
        searched = youtube_music.artist_card({"artist": "Yo-Yo Ma", "browseId": "UCF0", "thumbnails": THUMBNAILS})
        shelf = youtube_music.artist_card({"title": "Aurora", "browseId": "UC5"})
        self.assertEqual((searched["title"], searched["kind"]), ("Yo-Yo Ma", "artist"))
        self.assertEqual(shelf["title"], "Aurora")

    def test_playlist(self):
        card = youtube_music.playlist_card({
            "title": "Bach (live)", "browseId": "VLPL1", "author": "Someone", "itemCount": 19,
            "thumbnails": THUMBNAILS,
        })
        self.assertEqual(card["browse_id"], "VLPL1")
        self.assertEqual(card["subtitle"], "Someone · 19 songs")

    def test_home_item_types(self):
        self.assertEqual(youtube_music.parse_home_item(SEARCH_SONG)["video_id"], "abc123")
        self.assertEqual(youtube_music.parse_home_item({"title": "A", "browseId": "MPREb_1"})["kind"], "album")
        self.assertEqual(youtube_music.parse_home_item({"title": "B", "browseId": "UC1"})["kind"], "artist")
        self.assertEqual(youtube_music.parse_home_item({"title": "C", "playlistId": "RDCLAK"})["kind"], "playlist")
        self.assertIsNone(youtube_music.parse_home_item({"title": "nothing to open"}))


class YouTubeMusicTest(unittest.TestCase):
    def test_search_songs(self):
        api = FakeApi(search=[SEARCH_SONG, {"title": "no video"}])
        result = make_music(api).search("creep", "songs")
        self.assertEqual([t["video_id"] for t in result["tracks"]], ["abc123"])
        self.assertEqual(api.calls[0], ("search", ("creep",), {"filter": "songs", "limit": youtube_music.SEARCH_LIMIT}))

    def test_search_other_kinds_returns_cards(self):
        for kind, item in [
            ("albums", {"title": "A", "browseId": "MPREb_1"}),
            ("artists", {"artist": "B", "browseId": "UC1"}),
            ("playlists", {"title": "C", "browseId": "VLPL1"}),
        ]:
            with self.subTest(kind=kind):
                api = FakeApi(search=[item, {"title": "nothing to open"}])
                result = make_music(api).search("x", kind)
                self.assertEqual([card["kind"] for card in result["cards"]], [kind[:-1]])
                self.assertEqual(api.calls[0][2]["filter"], kind)

    def test_search_unknown_kind(self):
        with self.assertRaises(HelperError) as caught:
            make_music(FakeApi()).search("x", "podcasts")
        self.assertIn("podcasts", str(caught.exception))

    def test_album(self):
        api = FakeApi(get_album={
            "title": "Bach: Cello Suites", "year": "1985", "duration": "2 hours, 10 minutes", "thumbnails": THUMBNAILS,
            "artists": [{"name": "Yo-Yo Ma"}, {"name": "J. S. Bach"}], "tracks": [ALBUM_TRACK],
        })
        page = make_music(api).album("MPREb_1")
        self.assertEqual(page["artist"], "Yo-Yo Ma, J. S. Bach")
        self.assertEqual(page["year"], "1985")
        self.assertEqual(page["tracks"][0]["thumbnail_url"], "medium")
        self.assertEqual(page["tracks"][0]["album"], "Bach: Cello Suites")

    def test_artist(self):
        api = FakeApi(get_artist={
            "name": "Radiohead", "description": "A band.", "thumbnails": THUMBNAILS,
            "songs": {"results": [SEARCH_SONG]},
            "albums": {"results": [{"title": "KID A", "browseId": "MPREb_3", "year": "2000"}]},
            "singles": {"results": []},
        })
        page = make_music(api).artist("UC1")
        self.assertEqual(page["name"], "Radiohead")
        self.assertEqual(page["songs"][0]["title"], "Creep")
        self.assertEqual(page["albums"][0]["browse_id"], "MPREb_3")
        self.assertEqual(page["singles"], [])

    def test_artist_without_shelves(self):
        api = FakeApi(get_artist={"name": "Newcomer", "thumbnails": THUMBNAILS})
        page = make_music(api).artist("UC9")
        self.assertEqual((page["songs"], page["albums"], page["singles"]), ([], [], []))

    def test_playlist_strips_vl_prefix(self):
        api = FakeApi(get_playlist={
            "title": "Bach (live)", "author": {"name": "Someone", "id": "UC4"}, "duration": "1 hour, 12 minutes",
            "thumbnails": THUMBNAILS, "tracks": [PLAYLIST_TRACK],
        })
        page = make_music(api).playlist("VLPL123")
        self.assertEqual(api.calls[0][1], ("PL123",))
        self.assertEqual(page["author"], "Someone")
        self.assertEqual(page["tracks"][0]["video_id"], "ISPc0xa2WPM")
        make_music(api).playlist("PL456")
        self.assertEqual(api.calls[1][1], ("PL456",))

    def test_home_drops_empty_shelves(self):
        api = FakeApi(get_home=[
            {"title": "Quick picks", "contents": [SEARCH_SONG, {"title": "A", "browseId": "MPREb_1"}]},
            {"title": "Empty", "contents": []},
            {"title": "Odd", "contents": [{"title": "nothing to open"}]},
        ])
        shelves = make_music(api).home()["shelves"]
        self.assertEqual([shelf["title"] for shelf in shelves], ["Quick picks"])
        self.assertEqual(shelves[0]["items"][1]["kind"], "album")

    def test_radio(self):
        api = FakeApi(get_watch_playlist={"tracks": [RADIO_TRACK, SEARCH_SONG]})
        result = make_music(api).radio("abc")
        self.assertEqual(len(result["tracks"]), 2)
        self.assertEqual(api.calls[0][2]["radio"], True)

    def test_radio_like_starts_from_the_closest_song(self):
        api = FakeApi(search=[SEARCH_SONG], get_watch_playlist={"tracks": [RADIO_TRACK]})
        result = make_music(api).radio_like("Radiohead Creep")
        self.assertEqual(len(result["tracks"]), 1)
        self.assertEqual(api.calls[0][1], ("Radiohead Creep",))
        self.assertEqual(api.calls[-1][1], (SEARCH_SONG["videoId"],))

    def test_radio_like_without_a_match_is_an_error(self):
        api = FakeApi(search=[])
        with self.assertRaises(youtube_music.HelperError):
            make_music(api).radio_like("nothing like it")
        self.assertEqual([call[0] for call in api.calls if call[0] == "get_watch_playlist"], [])


class RetryTest(unittest.TestCase):
    def test_retries_empty_page(self):
        api = FakeApi(search=[SEARCH_SONG])
        api.failures = 2
        sleeps = []
        result = make_music(api, sleeps).search("creep", "songs")
        self.assertEqual(len(result["tracks"]), 1)
        self.assertEqual(len(api.calls), 3)
        self.assertEqual(len(sleeps), 2)

    def test_retries_network_error(self):
        api = FakeApi(search=[SEARCH_SONG])
        api.failures = 1
        api.error = ConnectionError("connection reset")
        self.assertEqual(len(make_music(api).search("creep", "songs")["tracks"]), 1)
        self.assertEqual(len(api.calls), 2)

    def test_gives_up_with_useful_message(self):
        api = FakeApi(search=[SEARCH_SONG])
        api.failures = 99
        with self.assertRaises(HelperError) as caught:
            make_music(api).search("creep", "songs")
        self.assertEqual(len(api.calls), youtube_music.MAX_ATTEMPTS)
        self.assertIn("request failed", str(caught.exception))
        self.assertIn("KeyError", str(caught.exception))
        self.assertIn("network", str(caught.exception))

    def test_programming_error_not_retried(self):
        api = FakeApi(search=[SEARCH_SONG])
        api.failures = 1
        api.error = TypeError("bad argument")
        sleeps = []
        with self.assertRaises(TypeError):
            make_music(api, sleeps).search("creep", "songs")
        self.assertEqual(len(api.calls), 1)
        self.assertEqual(sleeps, [])

    def test_missing_library_not_retried(self):
        def missing_library():
            raise HelperError("python-ytmusicapi is not installed")

        sleeps = []
        music = YouTubeMusic(api_factory=missing_library, sleep=sleeps.append)
        with self.assertRaises(HelperError):
            music.search("x", "songs")
        self.assertEqual(sleeps, [])

    def test_api_created_once_across_threads(self):
        created = []
        barrier = threading.Barrier(8)

        def factory():
            created.append(1)
            return FakeApi(search=[])

        music = YouTubeMusic(api_factory=factory, sleep=lambda seconds: None)

        def search():
            barrier.wait()
            music.search("x", "songs")

        threads = [threading.Thread(target=search) for _ in range(8)]
        for t in threads:
            t.start()
        for t in threads:
            t.join(timeout=10)
        self.assertEqual(len(created), 1)


class StreamUrlTest(unittest.TestCase):
    def test_reads_expire_param(self):
        url = "https://rr1.googlevideo.example/videoplayback?id=1&expire=1792600000&itag=251"
        self.assertEqual(streams.expiry_from_url(url), 1792600000)

    def test_missing_or_invalid_expire_is_zero(self):
        self.assertEqual(streams.expiry_from_url("https://example.test/audio"), 0)
        self.assertEqual(streams.expiry_from_url("https://example.test/a?expire=soon"), 0)


if __name__ == "__main__":
    unittest.main()
