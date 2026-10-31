// the queue, looking up a song's audio, and the media player

import QtQuick
import QtMultimedia
import "Format.js" as Format

Item {
    id: playback

    required property var queue
    required property var client
    required property var library
    required property var strings

    property real volume: 1.0
    // "off", "all" (the list starts over after its last song) or "one" (the song plays again)
    property string repeat: "off"
    // repeat all at the last song makes the first one come next
    onRepeatChanged: if (hasSong && !findingAudio) prepareNext()

    // the queue's current song as a track map, or {} when idle
    property var nowPlaying: ({})
    property bool findingAudio: false
    property string problem: ""

    readonly property bool hasSong: (nowPlaying.videoId ?? "") !== ""
    readonly property bool canNext: nextRow() >= 0
    readonly property bool isPlaying: player.playbackState === MediaPlayer.PlayingState
    readonly property bool nowPlayingLiked: {
        // likedVersion is a revision counter: isLiked() is a call, so read it to re-run on likes
        library.likedVersion
        return hasSong && library.isLiked(nowPlaying.videoId)
    }

    // while the next song's audio is looked up the player still has the old song's times
    readonly property int positionMs: findingAudio ? 0 : player.position
    readonly property int durationMs: !findingAudio && player.duration > 0
        ? player.duration : (nowPlaying.durationSeconds ?? 0) * 1000

    // "previous" within this many ms of the start goes to the song before, otherwise restarts
    readonly property int restartAfterMs: 3000

    // a card (album/artist/playlist) whose page is still loading, played once it arrives
    property var pendingCard: null
    property bool radioPending: false

    // a message for the status bar; "" clears it
    signal notify(string text)
    // the song jumped to somewhere other than where it was playing (a seek or a restart)
    signal jumped(int positionMs)

    function startCurrent() {
        const song = queue.currentTrack()
        if (song.videoId === undefined) {
            return
        }
        pendingCard = null
        radioPending = false
        nowPlaying = song
        problem = ""
        const local = Format.isLocalSong(song.videoId)
        findingAudio = !local
        // clear the source too, otherwise play() during the lookup plays the previous song
        player.stop()
        player.source = ""
        if (local) {
            player.source = song.videoId
            player.play()
            prepareNext()
        } else {
            client.findAudio(song.videoId)
        }
    }

    function stop() {
        player.stop()
        player.source = ""
        nowPlaying = ({})
        findingAudio = false
        problem = ""
    }

    function playSongs(songs, row) {
        queue.playFrom(songs, row)
        startCurrent()
    }

    // from the first song that isn't hidden
    function playAll(songs) {
        queue.playAll(songs)
        startCurrent()
    }

    function playShuffled(songs) {
        queue.playShuffled(songs)
        startCurrent()
    }

    // a list of track maps, e.g. a shelf on the home page
    function playItems(items, index) {
        queue.playList(items, index)
        startCurrent()
    }

    function playQueueRow(row) {
        if (queue.jumpTo(row)) {
            startCurrent()
        }
    }

    function cycleRepeat() {
        repeat = repeat === "off" ? "all" : repeat === "all" ? "one" : "off"
    }

    // the queue row next() goes to: with repeat all the first after the last; -1 if none.
    // repeat one doesn't count, next() always moves on
    function nextRow() {
        if (queue.hasNext) {
            return queue.current + 1
        }
        return repeat === "all" && queue.count > 0 ? 0 : -1
    }

    function next() {
        const row = nextRow()
        if (row >= 0) {
            queue.jumpTo(row)
            startCurrent()
        }
    }

    function songEnded() {
        // a song hidden while it played finishes, but doesn't come round again
        const hiddenNow = hasSong && library.isHidden(nowPlaying)
        if (repeat === "one" && !hiddenNow) {
            startCurrent()
            // the same song from the start: mpris clients only hear of that as a jump
            jumped(0)
        } else if (nextRow() >= 0 && !(hiddenNow && nextRow() === queue.current)) {
            next()
        } else {
            // back to the idle display once the queue runs out
            stop()
        }
    }

    function previous() {
        if (queue.hasPrevious && player.position <= restartAfterMs) {
            queue.step(-1)
            startCurrent()
        } else {
            seek(0)
        }
    }

    function play() {
        if (!isPlaying && !findingAudio && player.source.toString() !== "") {
            player.play()
        }
    }

    function pause() {
        if (isPlaying) {
            player.pause()
        }
    }

    function playOrPause() {
        if (isPlaying) {
            pause()
        } else {
            play()
        }
    }

    function seek(positionMs) {
        player.position = positionMs
        jumped(positionMs)
    }

    // the step size of the volume keys
    readonly property real volumeStep: 0.1

    function changeVolume(by) {
        volume = Math.max(0, Math.min(1, volume + by))
    }

    // play next, or append at the end; starts playing if the queue was empty
    function addToQueue(song, atEnd) {
        const wasEmpty = queue.count === 0
        if (atEnd) {
            queue.addToEnd(song)
        } else {
            queue.playNext(song)
        }
        if (wasEmpty) {
            startCurrent()
        }
    }

    function playCard(kind, id) {
        const page = client.page(kind, id)
        if (page.loading) {
            pendingCard = page
            notify(strings.status_loading)
        } else {
            // cached, or it failed right away (e.g. the helper didn't start)
            playLoadedCard(page)
        }
    }

    function playLoadedCard(page) {
        if (page.tracks.count > 0) {
            notify("")
            playAll(page.tracks)
        } else {
            notify(page.problem !== "" ? page.problem : strings.empty_remote)
        }
    }

    // the song after this one gets fetched while this one plays, so skipping to it is instant
    function prepareNext() {
        const row = nextRow()
        if (row >= 0 && row !== queue.current) {
            const song = queue.trackAt(row)
            // a file plays straight away and the helper can't look it up
            if (song.videoId !== undefined && !Format.isLocalSong(song.videoId)) {
                client.prepareAudio(song.videoId)
            }
        }
    }

    function cancelPendingCard() {
        pendingCard = null
    }

    function startRadio(song) {
        radioPending = true
        notify(strings.status_radio)
        client.startRadio(song.videoId)
    }

    Connections {
        target: playback.client

        // answers for a song we've already moved on from are dropped
        function onAudioFound(videoId, url) {
            if (videoId === playback.nowPlaying.videoId) {
                playback.findingAudio = false
                player.source = url
                player.play()
                playback.library.recordPlayed(playback.nowPlaying)
                playback.prepareNext()
            }
        }
        function onAudioNotFound(videoId, message) {
            if (videoId === playback.nowPlaying.videoId) {
                playback.findingAudio = false
                playback.problem = message
            }
        }
        // ignored if something else was started meanwhile
        function onRadioFound(videoId) {
            if (playback.radioPending) {
                playback.notify("")
                playback.playSongs(playback.client.radioTracks, 0)
            }
        }
        function onRadioFailed(videoId, message) {
            if (playback.radioPending) {
                playback.radioPending = false
                playback.notify(message)
            }
        }
    }

    // a song put next while one plays (the helper skips songs it has already).
    // a skip or a new list changes the place too, before startCurrent() has run: then the
    // song after is prepared once the new song's audio is found, not alongside its lookup
    Connections {
        target: playback.queue
        function onPlaceChanged() {
            const sameSong = playback.queue.currentTrack().videoId === playback.nowPlaying.videoId
            if (playback.hasSong && sameSong && !playback.findingAudio) {
                playback.prepareNext()
            }
        }
    }

    Connections {
        target: playback.pendingCard
        function onLoadingChanged() {
            const page = playback.pendingCard
            if (page !== null && !page.loading) {
                playback.pendingCard = null
                playback.playLoadedCard(page)
            }
        }
    }

    MediaPlayer {
        id: player
        audioOutput: AudioOutput {
            volume: playback.volume
        }
        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.EndOfMedia) {
                playback.songEnded()
            }
        }
        onErrorOccurred: (error, errorString) => playback.problem = errorString
    }
}
