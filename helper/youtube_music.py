# youtube music via python-ytmusicapi, no account needed.
# everything comes back as plain dicts in two shapes:
#   track  {video_id, title, artist, album, duration_seconds, thumbnail_url}
#   card   {kind: album|artist|playlist, browse_id, title, subtitle, thumbnail_url}

import threading
import time

SEARCH_LIMIT = 25
PLAYLIST_TRACK_LIMIT = 200
HOME_SHELF_LIMIT = 6

# youtube sometimes returns an empty page, which ytmusicapi surfaces as a KeyError.
# the same request a moment later usually works (see UPSTREAM-ISSUES.md)
MAX_ATTEMPTS = 3
RETRY_DELAY_SECONDS = 1.0

# covers are drawn at 140px, 226 leaves room for hidpi
MIN_THUMBNAIL_WIDTH = 226

SEARCH_KINDS = ("songs", "albums", "artists", "playlists")


class HelperError(Exception):
    """An error whose message is meant to be shown to the user."""


def thumbnail_url(thumbnails):
    # smallest one that is wide enough, otherwise the widest available
    if not thumbnails:
        return ""
    wide_enough = [t for t in thumbnails if t.get("width", 0) >= MIN_THUMBNAIL_WIDTH]
    if wide_enough:
        return min(wide_enough, key=lambda t: t["width"])["url"]
    return max(thumbnails, key=lambda t: t.get("width", 0))["url"]


def parse_duration(text):
    # "3:35" -> 215, "1:02:07" -> 3727, anything else -> 0
    if not text:
        return 0
    try:
        seconds = 0
        for part in text.split(":"):
            seconds = seconds * 60 + int(part)
        return seconds
    except ValueError:
        return 0


def join_artists(artists):
    return ", ".join(a["name"] for a in artists or [] if a.get("name"))


def parse_track(item, fallback_thumbnails=None, fallback_album=""):
    """Builds a track from any ytmusicapi song dict, or None if it has no video id.

    album tracks have no thumbnail of their own, so callers pass the album's.
    """
    video_id = item.get("videoId")
    if not video_id:
        return None

    album = item.get("album")
    if isinstance(album, dict):
        album = album.get("name", "")

    # most results have an "artists" list, a few only a plain "artist" string
    artist = join_artists(item.get("artists"))
    if not artist and isinstance(item.get("artist"), str):
        artist = item["artist"]

    # radio results use "thumbnail" and "length" instead of "thumbnails" and "duration"
    thumbnails = item.get("thumbnails") or item.get("thumbnail") or fallback_thumbnails
    seconds = item.get("duration_seconds") or parse_duration(item.get("duration") or item.get("length"))

    return {
        "video_id": video_id,
        "title": item.get("title", ""),
        "artist": artist,
        "album": album or fallback_album,
        "duration_seconds": seconds,
        "thumbnail_url": thumbnail_url(thumbnails),
    }


def parse_tracks(items, fallback_thumbnails=None, fallback_album=""):
    tracks = (parse_track(item, fallback_thumbnails, fallback_album) for item in items or [])
    return [track for track in tracks if track is not None]


def album_card(item):
    parts = [join_artists(item.get("artists")), str(item.get("year") or "")]
    return {
        "kind": "album",
        "browse_id": item["browseId"],
        "title": item.get("title", ""),
        "subtitle": " · ".join(p for p in parts if p),
        "thumbnail_url": thumbnail_url(item.get("thumbnails")),
    }


def artist_card(item):
    return {
        "kind": "artist",
        "browse_id": item["browseId"],
        # search results put the name in "artist", home shelves in "title"
        "title": item.get("artist") or item.get("title", ""),
        "subtitle": item.get("subscribers") or "",
        "thumbnail_url": thumbnail_url(item.get("thumbnails")),
    }


def playlist_card(item):
    parts = []
    author = item.get("author")
    if author:
        parts.append(author if isinstance(author, str) else author.get("name", ""))
    if item.get("itemCount"):
        parts.append("%s songs" % item["itemCount"])
    return {
        "kind": "playlist",
        "browse_id": item.get("browseId") or item.get("playlistId"),
        "title": item.get("title", ""),
        "subtitle": " · ".join(p for p in parts if p),
        "thumbnail_url": thumbnail_url(item.get("thumbnails")),
    }


def parse_home_item(item):
    if item.get("videoId"):
        return parse_track(item)

    # home items don't say what they are, the browse id prefix does
    browse_id = item.get("browseId") or ""
    if browse_id.startswith(("MPRE", "OLAK")):
        return album_card(item)
    if browse_id.startswith("UC"):
        return artist_card(item)
    if item.get("playlistId") or browse_id:
        return playlist_card(item)
    return None


class YouTubeMusic:
    def __init__(self, api_factory=None, sleep=time.sleep):
        # tests pass a fake api and a no-op sleep
        self._api_factory = api_factory or self._create_api
        self._sleep = sleep
        self._api = None
        self._api_lock = threading.Lock()
        # requests.RequestException is an OSError, so this covers network trouble too
        self._retryable = (KeyError, OSError)

    def _create_api(self):
        try:
            from ytmusicapi import YTMusic  # lazy, so a missing install shows up as an error response
        except ImportError as error:
            raise HelperError("python-ytmusicapi is not installed (on Arch: pacman -S python-ytmusicapi)") from error
        try:
            from ytmusicapi.exceptions import YTMusicServerError
            self._retryable += (YTMusicServerError,)
        except ImportError:
            pass  # older ytmusicapi has no exception classes
        return YTMusic()

    def _get_api(self):
        # requests come in on several threads, only build the client once
        with self._api_lock:
            if self._api is None:
                self._api = self._api_factory()
            return self._api

    def _call(self, fn):
        api = self._get_api()
        for attempt in range(1, MAX_ATTEMPTS + 1):
            try:
                return fn(api)
            except self._retryable as error:
                if attempt == MAX_ATTEMPTS:
                    raise HelperError(
                        "YouTube Music request failed (%s: %s). Check your network connection or try again in a moment."
                        % (type(error).__name__, str(error)[:120])
                    ) from error
                self._sleep(RETRY_DELAY_SECONDS)

    def search(self, query, kind="songs"):
        if kind not in SEARCH_KINDS:
            raise HelperError("can't search for %r (expected one of: %s)" % (kind, ", ".join(SEARCH_KINDS)))

        results = self._call(lambda api: api.search(query, filter=kind, limit=SEARCH_LIMIT))
        if kind == "songs":
            return {"tracks": parse_tracks(results)}

        make_card = {"albums": album_card, "artists": artist_card, "playlists": playlist_card}[kind]
        return {"cards": [make_card(item) for item in results if item.get("browseId")]}

    def album(self, browse_id):
        page = self._call(lambda api: api.get_album(browse_id))
        thumbnails = page.get("thumbnails")
        return {
            "title": page.get("title", ""),
            "artist": join_artists(page.get("artists")),
            "year": str(page.get("year") or ""),
            "duration_text": page.get("duration") or "",
            "thumbnail_url": thumbnail_url(thumbnails),
            "tracks": parse_tracks(page.get("tracks"), thumbnails, page.get("title", "")),
        }

    def artist(self, browse_id):
        page = self._call(lambda api: api.get_artist(browse_id))

        def shelf(key):
            return (page.get(key) or {}).get("results", [])

        return {
            "name": page.get("name", ""),
            "description": page.get("description") or "",
            "thumbnail_url": thumbnail_url(page.get("thumbnails")),
            "songs": parse_tracks(shelf("songs")),
            "albums": [album_card(item) for item in shelf("albums") if item.get("browseId")],
            "singles": [album_card(item) for item in shelf("singles") if item.get("browseId")],
        }

    def playlist(self, browse_id):
        # search hands out playlist browse ids with a "VL" prefix, get_playlist wants it without
        playlist_id = browse_id.removeprefix("VL")
        page = self._call(lambda api: api.get_playlist(playlist_id, limit=PLAYLIST_TRACK_LIMIT))
        author = page.get("author")
        return {
            "title": page.get("title", ""),
            "author": author.get("name", "") if isinstance(author, dict) else (author or ""),
            "duration_text": page.get("duration") or "",
            "thumbnail_url": thumbnail_url(page.get("thumbnails")),
            "tracks": parse_tracks(page.get("tracks")),
        }

    def home(self):
        shelves = []
        for shelf in self._call(lambda api: api.get_home(limit=HOME_SHELF_LIMIT)):
            items = (parse_home_item(item) for item in shelf.get("contents", []) if isinstance(item, dict))
            items = [item for item in items if item is not None]
            if items:
                shelves.append({"title": shelf.get("title", ""), "items": items})
        return {"shelves": shelves}

    def radio(self, video_id):
        page = self._call(lambda api: api.get_watch_playlist(videoId=video_id, radio=True, limit=SEARCH_LIMIT))
        return {"tracks": parse_tracks(page.get("tracks"))}

    def radio_like(self, query):
        """A radio from the closest song, for music that isn't on YouTube Music by id."""
        tracks = self.search(query)["tracks"]
        if not tracks:
            raise HelperError('nothing like "%s" on YouTube Music' % query)
        return self.radio(tracks[0]["video_id"])
