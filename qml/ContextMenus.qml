// the right-click menus for songs, cards and the sidebar's playlists.
// what a chosen item does is up to the window (the *Action signals).

import QtQuick
import "MenuItems.js" as MenuItems

PopupMenu {
    id: menus

    required property var library
    required property var strings

    // what the open menu is about: { kind: "song", song, row } / { kind: "card", card } /
    // { kind: "playlist", id }, or null
    property var subject: null

    signal songAction(string action, var argument, var song, int row)
    signal cardAction(string action, var card)
    signal playlistAction(string action, string id)

    function show(items, subject, x, y) {
        menus.subject = subject
        menus.items = items
        openAt = Qt.point(x, y)
        opened = true
    }

    function close() {
        opened = false
        subject = null
    }

    // row is the song's row in the page's list, or -1 (e.g. a song on a home shelf).
    // removal: what the page lets the song be removed from, see MenuItems.songMenu;
    // a playlist removes by row, the history by video id
    function openSongMenu(song, row, removal, x, y) {
        const playlists = []
        for (let i = 0; i < library.playlists.count; i++) {
            const id = library.playlists.idAt(i)
            playlists.push({ id: id, name: library.playlistName(id), has: library.playlistHas(id, song.videoId) })
        }
        const items = MenuItems.songMenu(strings, library.isLiked(song.videoId), library.isHidden(song), playlists,
                                         removal === "playlist" && row < 0 ? "" : removal)
        show(items, { kind: "song", song: song, row: row }, x, y)
    }

    function openCardMenu(card, x, y) {
        show(MenuItems.cardMenu(strings, card, library.isSaved(card.browseId)), { kind: "card", card: card }, x, y)
    }

    function openPlaylistMenu(id, x, y) {
        show(MenuItems.playlistMenu(strings), { kind: "playlist", id: id }, x, y)
    }

    onChosen: id => {
        const item = MenuItems.find(items, id)
        const about = subject
        close()
        if (item === null || item.action === "" || about === null) {
            return
        }
        if (about.kind === "song") {
            songAction(item.action, item.argument, about.song, about.row)
        } else if (about.kind === "card") {
            cardAction(item.action, about.card)
        } else {
            playlistAction(item.action, about.id)
        }
    }
    onDismissed: close()
}
