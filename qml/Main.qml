// the music window; wires playback, navigation, the pages and the menus together

import QtQml
import QtQuick
import QtQuick.Window
import Bandstand
import "shell/ThemeValues.js" as ThemeValues
import "MusicLook.js" as MusicLook
import "Routes.js" as Routes
import "Strings.js" as Strings

Window {
    id: window

    // settings, client, queue and library are context properties from main.cpp
    readonly property var theme: settings.theme
    // the theme's [music] table over a look drawn from its shared [controls] (MusicLook.js)
    readonly property var resolvedLook: MusicLook.resolve(theme)
    readonly property var looks: resolvedLook.music
    readonly property var fonts: resolvedLook.fonts
    // for the push buttons and the question's box
    readonly property var dialogLooks: resolvedLook.dialog
    readonly property var config: settings.config.music ?? ({})
    readonly property var strings: Strings.withDefaults(config.words)
    readonly property string themeDirectory: settings.theme_directory
    // aliases, so children can use these names for their own properties without a binding loop
    readonly property var desktopSettings: settings
    readonly property var musicClient: client
    readonly property var playQueue: queue
    readonly property var userLibrary: library

    visible: true
    title: playback.hasSong ? playback.nowPlaying.title : strings.window_title
    width: looks.window_size[0]
    height: looks.window_size[1]
    minimumWidth: looks.sidebar.width + looks.display.width + 2 * looks.top_bar.inset + 400
    minimumHeight: 320
    color: ThemeValues.color(looks.page_color)

    function say(text) {
        statusBar.show(text)
    }

    // --- library actions shared by the menus and drag and drop

    // creates a playlist, opens it with its title ready to rename, returns its id
    function newPlaylist() {
        const id = userLibrary.createPlaylist(strings.new_playlist_name)
        startRenaming(id)
        return id
    }

    function addSongToPlaylist(id, song) {
        const name = userLibrary.playlistName(id)
        if (userLibrary.addToPlaylist(id, song)) {
            say(strings.notice_added.replace("%1", name))
        } else {
            say(strings.notice_already.replace("%1", name))
        }
    }

    function likeSong(song) {
        if (userLibrary.isLiked(song.videoId)) {
            say(strings.notice_already_liked)
        } else {
            userLibrary.setLiked(song, true)
            say(strings.notice_liked)
        }
    }

    // hiding also takes it out of the history and up next (Library, PlayQueue); one that's
    // playing plays on
    function toggleHidden(song) {
        const hide = !userLibrary.isHidden(song)
        userLibrary.setHidden(song, hide)
        say(hide ? strings.notice_hidden : strings.notice_unhidden)
    }

    function startRenaming(id) {
        navigation.show("mine:" + id)
        navigation.renaming = true
    }

    // --- confirmations

    // runs when the confirm sheet is accepted
    property var confirmedAction: null

    function confirm(text, button, action) {
        confirmedAction = action
        sheet.ask({ text: text, button: button, cancel: strings.action_cancel })
    }

    function askToDeletePlaylist(id) {
        const name = userLibrary.playlistName(id)
        confirm(strings.confirm_delete_playlist.replace("%1", name), strings.action_delete, () => {
            userLibrary.deletePlaylist(id)
            navigation.forget("mine:" + id)
        })
    }

    function askToClearHistory() {
        confirm(strings.confirm_clear_history, strings.action_clear_history, () => userLibrary.clearHistory())
    }

    Component.onCompleted: {
        if (songsToOpen.length > 0) {
            playback.playItems(songsToOpen, 0)
        }
    }

    // --- controllers

    Playback {
        id: playback
        queue: window.playQueue
        client: window.musicClient
        library: window.userLibrary
        strings: window.strings
        onNotify: text => window.say(text)
        onJumped: positionMs => mediaControls.seeked(positionMs)
    }

    Connections {
        target: songInbox
        function onSongsOpened(songs) {
            playback.playItems(songs, 0)
        }
    }

    Navigation {
        id: navigation
        client: window.musicClient
        library: window.userLibrary
        startPage: window.config.start_page ?? "home"
        onPageChanged: {
            window.say("")
            playback.cancelPendingCard()
        }
    }

    Connections {
        target: window.userLibrary
        // a file the library couldn't write
        function onProblemChanged() {
            if (window.userLibrary.problem !== "") {
                window.say(window.userLibrary.problem)
            }
        }
    }

    // the media keys reach us through mpris (the compositor runs playerctl for them),
    // and so do the panel's now playing controls
    MediaControls {
        id: mediaControls
        playing: playback.isPlaying
        track: playback.nowPlaying
        durationMs: playback.durationMs
        positionMs: playback.positionMs
        canNext: playback.canNext
        canPrevious: window.playQueue.hasPrevious
        volume: playback.volume
        repeat: playback.repeat

        onPlayPauseRequested: playback.playOrPause()
        onPlayRequested: playback.play()
        onPauseRequested: playback.pause()
        onStopRequested: playback.stop()
        onNextRequested: playback.next()
        onPreviousRequested: playback.previous()
        onSeekRequested: positionMs => playback.seek(positionMs)
        onVolumeRequested: volume => playback.volume = volume
        onRepeatRequested: repeat => playback.repeat = repeat
        onRaiseRequested: {
            window.show()
            window.raise()
            window.requestActivate()
        }
    }

    // as in itunes. a focused text field keeps space and ctrl+arrows for itself.
    // the media keys only arrive here when nothing grabbed them before us.
    Shortcut {
        sequences: ["Space", "Media Play", "Toggle Media Play/Pause"]
        enabled: !sheet.visible
        onActivated: playback.playOrPause()
    }
    Shortcut {
        sequences: ["Media Pause"]
        onActivated: playback.pause()
    }
    Shortcut {
        sequences: ["Ctrl+Right", "Media Next"]
        onActivated: playback.next()
    }
    Shortcut {
        sequences: ["Ctrl+Left", "Media Previous"]
        onActivated: playback.previous()
    }
    Shortcut {
        sequences: ["Media Stop"]
        onActivated: playback.stop()
    }
    Shortcut {
        sequences: ["Ctrl+Up"]
        autoRepeat: true
        onActivated: playback.changeVolume(playback.volumeStep)
    }
    Shortcut {
        sequences: ["Ctrl+Down"]
        autoRepeat: true
        onActivated: playback.changeVolume(-playback.volumeStep)
    }
    Shortcut {
        sequences: [StandardKey.Find]
        onActivated: topBar.focusSearch()
    }
    Shortcut {
        sequence: "Escape"
        enabled: upNext.visible || menus.opened
        onActivated: {
            upNext.close()
            menus.close()
        }
    }

    // --- layout

    Pages {
        id: pages
        x: sidebar.width
        y: topBar.height
        width: parent.width - sidebar.width
        height: parent.height - topBar.height - statusBar.height

        looks: window.looks
        fonts: window.fonts
        themeDirectory: window.themeDirectory
        buttonLooks: window.dialogLooks
        strings: window.strings
        client: window.musicClient
        library: window.userLibrary

        route: navigation.route
        contentPage: navigation.contentPage
        canGoBack: navigation.canGoBack
        playingVideoId: playback.nowPlaying.videoId ?? ""
        searchKind: navigation.searchKind
        searchQuery: navigation.searchQuery
        renaming: navigation.renaming

        onPlayRequested: (songs, row) => playback.playSongs(songs, row)
        onPlayAllRequested: songs => playback.playAll(songs)
        onShuffleRequested: songs => playback.playShuffled(songs)
        onPlayItemsRequested: (items, index) => playback.playItems(items, index)
        onPlayCardRequested: (kind, id) => playback.playCard(kind, id)
        onOpenRequested: name => navigation.open(name)
        onBackRequested: navigation.back()
        onSearchKindChosen: kind => navigation.chooseSearchKind(kind)
        onSongMenuRequested: (song, row, x, y) => {
            const place = pages.mapToItem(window.contentItem, x, y)
            const kind = navigation.route.kind
            // home's recently played shelf is the history too
            const fromHistory = kind === "history" || song.fromHistory === true
            const removal = kind === "mine" ? "playlist" : fromHistory ? "history" : ""
            menus.openSongMenu(song, row, removal, place.x, place.y)
        }
        onCardMenuRequested: (card, x, y) => {
            const place = pages.mapToItem(window.contentItem, x, y)
            menus.openCardMenu(card, place.x, place.y)
        }
        onSongDragged: (song, x, y) => songDrag.move(song, x, y)
        onSongDropped: (song, x, y) => songDrag.drop(song, x, y)
        onSongDragEnded: songDrag.end()
        onRenameRequested: navigation.renaming = true
        onRenamed: name => {
            window.userLibrary.renamePlaylist(navigation.route.id, name)
            navigation.renaming = false
        }
        onRenameCancelled: navigation.renaming = false
        onDeletePlaylistRequested: window.askToDeletePlaylist(navigation.route.id)
        onClearHistoryRequested: window.askToClearHistory()
    }

    Sidebar {
        id: sidebar
        y: topBar.height
        width: window.looks.sidebar.width
        height: parent.height - topBar.height - statusBar.height
        looks: window.looks.sidebar
        fonts: window.fonts
        themeDirectory: window.themeDirectory
        entries: window.config.sidebar ?? Routes.defaultSidebar(window.strings)
        playlists: window.userLibrary.playlists
        currentPage: navigation.page
        onPageChosen: name => navigation.show(name)
        onNewPlaylistRequested: window.newPlaylist()
        onPlaylistMenuRequested: (id, x, y) => {
            const place = sidebar.mapToItem(window.contentItem, x, y)
            menus.openPlaylistMenu(id, place.x, place.y)
        }
    }

    TopBar {
        id: topBar
        width: parent.width
        height: window.looks.top_bar.height
        looks: window.looks.top_bar
        displayLooks: window.looks.display
        searchLooks: window.looks.search_field
        fonts: window.fonts
        themeDirectory: window.themeDirectory

        playing: playback.isPlaying
        canPlay: playback.hasSong
        canPrevious: playback.hasSong
        canNext: playback.canNext
        track: playback.nowPlaying
        positionMs: playback.positionMs
        durationMs: playback.durationMs
        loading: playback.findingAudio
        problemText: playback.problem
        volume: playback.volume
        queueOpen: upNext.visible
        liked: playback.nowPlayingLiked
        repeat: playback.repeat

        idleText: window.strings.display_idle
        loadingText: window.strings.display_loading
        searchPlaceholder: window.strings.search_placeholder

        onPreviousClicked: playback.previous()
        onPlayPauseClicked: playback.playOrPause()
        onNextClicked: playback.next()
        onSeekRequested: positionMs => playback.seek(positionMs)
        onVolumeRequested: volume => playback.volume = volume
        onSearchAccepted: text => navigation.search(text)
        onLikeRequested: window.userLibrary.setLiked(playback.nowPlaying, !playback.nowPlayingLiked)
        onRepeatClicked: playback.cycleRepeat()
        onQueueRequested: {
            if (upNext.visible) {
                upNext.close()
            } else {
                upNext.target = topBar.queueButtonBottom()
                upNext.open()
            }
        }
    }

    StatusBar {
        id: statusBar
        y: parent.height - height
        width: parent.width
        looks: window.looks.status_bar
        fonts: window.fonts
        themeDirectory: window.themeDirectory
        strings: window.strings
        songs: pages.songs
    }

    // --- overlays, top to bottom in stacking order

    UpNext {
        id: upNext
        looks: window.looks.queue
        fonts: window.fonts
        themeDirectory: window.themeDirectory
        queue: window.playQueue
        strings: window.strings
        onChosen: row => playback.playQueueRow(row)
        onRemoveRequested: row => window.playQueue.removeAt(row)
        onClearRequested: window.playQueue.clearUpNext()
    }

    SongDrag {
        id: songDrag
        looks: window.looks
        fonts: window.fonts
        themeDirectory: window.themeDirectory
        dropZone: sidebar
        onDropped: (song, target) => {
            if (target === "new") {
                window.addSongToPlaylist(window.newPlaylist(), song)
            } else if (target === "liked") {
                window.likeSong(song)
            } else {
                window.addSongToPlaylist(target, song)
            }
        }
    }

    ContextMenus {
        id: menus
        settings: window.desktopSettings
        library: window.userLibrary
        strings: window.strings

        onSongAction: (action, argument, song, row) => {
            switch (action) {
            case "play":
                // a row in the page's list plays with the rest of the list queued; a shelf song alone
                if (row >= 0) {
                    playback.playSongs(pages.songs, row)
                } else {
                    playback.playItems([song], 0)
                }
                break
            case "next":
                playback.addToQueue(song, false)
                break
            case "queue":
                playback.addToQueue(song, true)
                break
            case "like":
                window.userLibrary.setLiked(song, !window.userLibrary.isLiked(song.videoId))
                break
            case "radio":
                playback.startRadio(song)
                break
            case "remove":
                pages.songs.removeAt(row)
                break
            case "removeHistory":
                window.userLibrary.removeFromHistory(song.videoId)
                break
            case "hide":
                window.toggleHidden(song)
                break
            case "newPlaylist":
                window.userLibrary.addToPlaylist(window.newPlaylist(), song)
                break
            case "toPlaylist":
                window.addSongToPlaylist(argument, song)
                break
            }
        }
        onCardAction: (action, card) => {
            if (action === "open") {
                navigation.open(Routes.make(card.kind, card.browseId))
            } else if (action === "play") {
                playback.playCard(card.kind, card.browseId)
            } else if (action === "save") {
                pages.toggleSaved(card)
            }
        }
        onPlaylistAction: (action, id) => {
            if (action === "play") {
                playback.playAll(window.userLibrary.playlistTracks(id))
            } else if (action === "rename") {
                window.startRenaming(id)
            } else if (action === "delete") {
                window.askToDeletePlaylist(id)
            }
        }
    }

    ConfirmSheet {
        id: sheet
        looks: window.looks.sheet
        buttonLooks: window.dialogLooks
        fonts: window.fonts
        themeDirectory: window.themeDirectory
        onAgreed: {
            const action = window.confirmedAction
            window.confirmedAction = null
            if (action) {
                action()
            }
        }
        onCancelled: window.confirmedAction = null
    }
}
