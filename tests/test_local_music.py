# local music tests: scanning folders into tracks, the index that spares re-reading
# unchanged files, and covers. tags come from a fake reader, so mutagen isn't needed.

import os
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "helper"))

import local_music  # noqa: E402
from youtube_music import HelperError  # noqa: E402


def tags(title="", artist="", album="", track=0, disc=0, seconds=0, album_artist=None):
    return {
        "title": title, "artist": artist, "album_artist": artist if album_artist is None else album_artist,
        "album": album, "track": track, "disc": disc, "duration_seconds": seconds,
    }


class FakeReader:
    """Tags by file name; counts the files it was asked about."""

    def __init__(self, by_name=None, pictures=None):
        self.by_name = by_name or {}
        self.pictures = pictures or {}
        self.read = []
        self.pictured = []

    def read_tags(self, path):
        self.read.append(os.path.basename(path))
        return self.by_name.get(os.path.basename(path), tags())

    def read_picture(self, path):
        self.pictured.append(os.path.basename(path))
        return self.pictures.get(os.path.basename(path))


class LocalMusicTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.music = os.path.join(self.temp.name, "Music")
        self.cache = os.path.join(self.temp.name, "cache")
        os.makedirs(self.music)

    def add(self, relative, data=b"audio"):
        path = os.path.join(self.music, relative)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as file:
            file.write(data)
        return path

    def scan(self, reader):
        local = local_music.LocalMusic([self.music], self.cache, reader.read_tags, reader.read_picture)
        return local.songs()

    def test_songs_become_tracks_with_file_urls(self):
        path = self.add("Aurora Vale/First Light/01 Dawn.flac")
        reader = FakeReader({"01 Dawn.flac": tags("Dawn", "Aurora Vale", "First Light", 1, seconds=201)})
        result = self.scan(reader)
        self.assertEqual(result["tracks"], [{
            "video_id": pathlib.Path(path).as_uri(), "title": "Dawn", "artist": "Aurora Vale",
            "album": "First Light", "duration_seconds": 201, "thumbnail_url": "",
        }])
        self.assertEqual(result["folders"], [self.music])

    # the app writes the same id for a file opened from the file manager
    def test_file_url_is_percent_encoded(self):
        self.add("a b/Björk (live) [2014] #1 100% +&,;='!~?@$*.mp3")
        self.add("皇后大道東 (feat. 蔣志光).mp3")
        ids = sorted(track["video_id"].removeprefix(pathlib.Path(self.music).as_uri()) for track in self.scan(FakeReader())["tracks"])
        self.assertEqual(ids, [
            "/%E7%9A%87%E5%90%8E%E5%A4%A7%E9%81%93%E6%9D%B1%20%28feat.%20%E8%94%A3%E5%BF%97%E5%85%89%29.mp3",
            "/a%20b/Bj%C3%B6rk%20%28live%29%20%5B2014%5D%20%231%20100%25%20%2B%26%2C%3B%3D%27%21~%3F%40%24%2A.mp3",
        ])

    def test_only_audio_files_and_no_hidden_ones(self):
        self.add("a.mp3")
        self.add("notes.txt")
        self.add("cover.jpg")
        self.add(".hidden.mp3")
        self.add(".stash/b.mp3")
        self.add("Sub/C.OPUS")
        reader = FakeReader()
        titles = [track["title"] for track in self.scan(reader)["tracks"]]
        self.assertEqual(sorted(titles), ["C", "a"])

    def test_missing_tags_fall_back_to_file_and_folder_names(self):
        self.add("loose song.mp3")
        self.add("Some Album/02 Track Two.ogg")
        tracks = {track["title"]: track for track in self.scan(FakeReader())["tracks"]}
        self.assertEqual(tracks["loose song"]["album"], "")
        self.assertEqual(tracks["02 Track Two"]["album"], "Some Album")

    def test_sorted_by_album_artist_album_disc_and_track(self):
        self.add("x/b2.mp3")
        self.add("x/b1.mp3")
        self.add("x/a1.mp3")
        self.add("x/c.mp3")
        reader = FakeReader({
            "b2.mp3": tags("B2", "beta", "One", track=2),
            "b1.mp3": tags("B1", "Beta", "One", track=1),
            "a1.mp3": tags("A1", "alpha", "Two", track=9, disc=2),
            "c.mp3": tags("C", "guest", "One", track=3, album_artist="Beta"),
        })
        titles = [track["title"] for track in self.scan(reader)["tracks"]]
        self.assertEqual(titles, ["A1", "B1", "B2", "C"])

    def test_unchanged_files_are_not_read_again(self):
        self.add("a.mp3")
        path_b = self.add("b.mp3")
        self.scan(FakeReader())

        reader = FakeReader()
        self.assertEqual(len(self.scan(reader)["tracks"]), 2)
        self.assertEqual(reader.read, [])

        with open(path_b, "ab") as file:
            file.write(b"retagged")
        reader = FakeReader({"b.mp3": tags("New Title")})
        titles = sorted(track["title"] for track in self.scan(reader)["tracks"])
        self.assertEqual(reader.read, ["b.mp3"])
        self.assertEqual(titles, ["New Title", "a"])

    def test_deleted_files_leave_the_index(self):
        path = self.add("a.mp3")
        self.scan(FakeReader())
        os.remove(path)
        self.assertEqual(self.scan(FakeReader())["tracks"], [])

    def test_unreadable_files_are_skipped(self):
        self.add("broken.mp3")
        self.add("fine.mp3")

        class Reader(FakeReader):
            def read_tags(self, path):
                return None if path.endswith("broken.mp3") else tags("Fine")

        self.assertEqual([t["title"] for t in self.scan(Reader())["tracks"]], ["Fine"])

    def test_a_broken_index_is_read_again(self):
        self.add("a.mp3")
        os.makedirs(self.cache)
        with open(os.path.join(self.cache, "index.json"), "w") as file:
            file.write("{not json")
        reader = FakeReader()
        self.assertEqual(len(self.scan(reader)["tracks"]), 1)
        self.assertEqual(reader.read, ["a.mp3"])

    def test_cover_file_in_the_folder_wins(self):
        self.add("Album/1.mp3")
        cover = self.add("Album/Folder.JPG", b"jpeg")
        reader = FakeReader(pictures={"1.mp3": (b"embedded", "image/jpeg")})
        track = self.scan(reader)["tracks"][0]
        self.assertEqual(track["thumbnail_url"], pathlib.Path(cover).as_uri())
        self.assertEqual(reader.pictured, [])

    def test_embedded_picture_is_kept_once_per_folder(self):
        self.add("Album/1.flac")
        self.add("Album/2.flac")
        reader = FakeReader(pictures={"1.flac": (b"png bytes", "image/png")})
        tracks = self.scan(reader)["tracks"]
        url = tracks[0]["thumbnail_url"]
        self.assertTrue(url.startswith("file://") and url.endswith(".png"), url)
        self.assertEqual(tracks[1]["thumbnail_url"], url)
        with open(pathlib.Path(url.removeprefix("file://")), "rb") as file:
            self.assertEqual(file.read(), b"png bytes")

        # nothing new in the folder, so no file is opened for its picture again
        reader = FakeReader()
        self.assertEqual(self.scan(reader)["tracks"][0]["thumbnail_url"], url)
        self.assertEqual(reader.pictured, [])

    def test_no_cover_anywhere(self):
        self.add("Album/1.mp3")
        self.assertEqual(self.scan(FakeReader())["tracks"][0]["thumbnail_url"], "")

    def test_missing_folder_is_named_in_the_error(self):
        local = local_music.LocalMusic([os.path.join(self.temp.name, "Nope")], self.cache)
        with self.assertRaisesRegex(HelperError, "Nope"):
            local.songs()

    def test_missing_folders_are_left_out_if_another_exists(self):
        self.add("a.mp3")
        reader = FakeReader()
        local = local_music.LocalMusic([os.path.join(self.temp.name, "Nope"), self.music], self.cache,
                                       reader.read_tags, reader.read_picture)
        result = local.songs()
        self.assertEqual(result["folders"], [self.music])
        self.assertEqual(len(result["tracks"]), 1)

    def test_nested_folders_list_a_song_once(self):
        self.add("Sub/a.mp3")
        reader = FakeReader()
        local = local_music.LocalMusic([self.music, os.path.join(self.music, "Sub")], self.cache,
                                       reader.read_tags, reader.read_picture)
        self.assertEqual(len(local.songs()["tracks"]), 1)

    def test_display_path_shortens_home(self):
        home = os.path.expanduser("~")
        self.assertEqual(local_music.display_path(os.path.join(home, "Music")), "~/Music")
        self.assertEqual(local_music.display_path("/srv/music"), "/srv/music")


if __name__ == "__main__":
    unittest.main()
