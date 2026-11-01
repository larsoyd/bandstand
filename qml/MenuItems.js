// right-click menu items for shell/MenuBox.qml.
// menubox only reports a numeric id, so each item also carries a string
// `action` (and maybe an `argument`); numbered() hands out the ids and
// find() maps a chosen id back to its item.

.pragma library

// menubox wants every one of these keys
function item(action, label, options) {
    return Object.assign({
        id: -1, action: action, argument: null, label: label,
        enabled: true, separator: false, checkable: false, checked: false,
        shortcut: [], children: []
    }, options ?? {})
}

function separator() {
    return item("", "", { enabled: false, separator: true })
}

function numbered(items) {
    let next = 0
    const walk = list => list.forEach(entry => {
        if (!entry.separator) {
            entry.id = next++
        }
        walk(entry.children)
    })
    walk(items)
    return items
}

function find(items, id) {
    for (const entry of items) {
        if (entry.id === id && !entry.separator) {
            return entry
        }
        const inner = find(entry.children, id)
        if (inner !== null) {
            return inner
        }
    }
    return null
}

// playlists: [{ id, name, has }], has = the song is already in it
// removal: "playlist", "history" or "" for what the current page lets you remove from
// hidden: the song isn't suggested (Library.isHidden)
function songMenu(strings, liked, hidden, playlists, removal) {
    const playlistItems = [item("newPlaylist", strings.menu_new_playlist), separator()]
    for (const playlist of playlists) {
        playlistItems.push(item("toPlaylist", playlist.name, {
            argument: playlist.id, checkable: true, checked: playlist.has, enabled: !playlist.has
        }))
    }

    const items = [
        item("play", strings.menu_play),
        item("next", strings.menu_play_next),
        item("queue", strings.menu_add_to_queue),
        separator(),
        item("like", liked ? strings.menu_unlike : strings.menu_like),
        item("", strings.menu_add_to_playlist, { children: playlistItems }),
        item("radio", strings.menu_start_radio)
    ]
    items.push(separator())
    if (removal === "playlist") {
        items.push(item("remove", strings.menu_remove_playlist))
    } else if (removal === "history") {
        items.push(item("removeHistory", strings.menu_remove_history))
    }
    items.push(item("hide", hidden ? strings.menu_unhide : strings.menu_hide))
    return numbered(items)
}

// only albums and artists can be saved to the library
function cardMenu(strings, card, saved) {
    const items = [item("open", strings.menu_open), item("play", strings.menu_play)]
    if (card.kind === "album" || card.kind === "artist") {
        items.push(separator(), item("save", saved ? strings.menu_unsave : strings.menu_save))
    }
    return numbered(items)
}

function playlistMenu(strings) {
    return numbered([
        item("play", strings.menu_play),
        separator(),
        item("rename", strings.action_rename),
        item("delete", strings.action_delete)
    ])
}
