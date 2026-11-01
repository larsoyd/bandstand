.pragma library

// "1 song", "12 songs"
function songCount(count, strings) {
    return count === 1 ? strings.song_one : strings.songs_many.replace("%1", count)
}

// rounds down so a list summary never overstates, but anything non-zero is at least 1 min
function duration(seconds, strings) {
    const minutes = seconds > 0 ? Math.max(1, Math.floor(seconds / 60)) : 0
    if (minutes < 60) {
        return strings.duration_minutes.replace("%1", minutes)
    }
    return strings.duration_hours
        .replace("%1", Math.floor(minutes / 60))
        .replace("%2", minutes % 60)
}

// "12 songs, 45 min", or just the count when the lengths aren't known
function listSummary(count, seconds, strings) {
    if (count === 0) {
        return ""
    }
    const songs = songCount(count, strings)
    return seconds > 0 ? songs + ", " + duration(seconds, strings) : songs
}

// 83 -> "1:23", 3723 -> "1:02:03"
function clock(seconds) {
    const total = Math.max(0, Math.floor(seconds))
    const h = Math.floor(total / 3600)
    const m = Math.floor(total / 60) % 60
    const s = total % 60
    const ss = (s < 10 ? "0" : "") + s
    if (h > 0) {
        return h + ":" + (m < 10 ? "0" : "") + m + ":" + ss
    }
    return m + ":" + ss
}

// a song from a file has its url for an id
function isLocalSong(videoId) {
    return videoId.startsWith("file:")
}

function searchTextOf(song) {
    return (song.artist + " " + song.title).trim()
}
