# foobar2000 Artwork Display Component (`foo_artwork`)

A high-performance, feature-rich artwork display component for **foobar2000 v2** (x64 and 32-bit). It provides intelligent, multi-tiered artwork discovery and acoustic audio recognition for local tracks, internet radio streams, and YouTube playback. It seamlessly supports both **Default User Interface (DUI)** and **Columns UI (CUI)**.

<img width="743" height="551" alt="Screenshot_20260905_160105" src="https://github.com/user-attachments/assets/94cb9892-df17-4fc3-a247-ba4bb7b5ff81" />

---

## Table of Contents

- [Key Features](#key-features)
- [Installation](#installation)
- [UI Integration & Setup](#ui-integration--setup)
- [Artwork Retrieval & Fallback Hierarchy](#artwork-retrieval--fallback-hierarchy)
- [Title Formatting Reference](#title-formatting-reference)
- [Stream URL Modifiers & m-TAGS Integration](#stream-url-modifiers--m-tags-integration)
- [External Stream APIs & Auto-Probing](#external-stream-apis--auto-probing)
- [Preferences Configuration](#preferences-configuration)
  - [1. Main Preferences (Tools → Artwork Display)](#1-main-preferences-tools--artwork-display)
  - [2. Advanced Preferences (Tools → Artwork Display → Advanced)](#2-advanced-preferences-tools--artwork-display--advanced)
  - [3. Noise Words Blacklist (Tools → Artwork Display → Blacklist)](#3-noise-words-blacklist-tools--artwork-display--blacklist)
  - [4. ACRCloud Live Recognition (Tools → Artwork Display → ACRCloud)](#4-acrcloud-live-recognition-tools--artwork-display--acrcloud)
- [Metadata Cleaning & Sanitization Engine](#metadata-cleaning--sanitization-engine)
- [Custom Station Logos & Fallback Images](#custom-station-logos--fallback-images)
- [Interactive UI & Artwork Viewer](#interactive-ui--artwork-viewer)
- [Main Menu Commands & Keyboard Shortcuts](#main-menu-commands--keyboard-shortcuts)
- [Caching & Storage Architecture](#caching--storage-architecture)
- [Developer C-API & Companion Integration](#developer-c-api--companion-integration)
- [Building from Source](#building-from-source)
- [Troubleshooting & FAQ](#troubleshooting--faq)
- [Support Development](#support-development)

---

## Key Features

### Artwork Discovery & Retrieval
- **Local Artwork Search**: Automatically searches for artwork files and embedded tags configured in foobar2000 (*Preferences > Display > Album Art*).
- **Online Commercial Search APIs**: Automatically queries **Deezer**, **iTunes**, **Last.fm**, **MusicBrainz** (Cover Art Archive), and **Discogs** when local artwork is missing.
- **Customizable API Priority**: Reorder and enable/disable individual API services via an intuitive 5-position priority chain.
- **In-Stream Broadcast Artwork Extraction**: Parses dynamic in-stream cover links from `$info(cover_url)`, `COVER_URL`, `ARTWORK_URL`, `icy-cover-url`, `song.art`, and XML/JSON broadcast streams (e.g. Radio Paradise, Cidade FM).
- **YouTube Video Thumbnail Retrieval**: Automatically extracts 11-character video IDs from YouTube links and plugin protocols (`youtube.com`, `youtu.be`, `3dyd://`, `fy://`, `fy+https://`), querying commercial music APIs first and falling back to high-resolution video thumbnails (`maxresdefault.jpg` / `hqdefault.jpg`).
- **Interactive Provider Rejection**: Reject mismatched artwork on-the-fly via shortcut or menu command (*View > Artwork Display > Reject Artwork & Search Next Provider*). Evicts the bad cache file, blacklists the provider for the active track, and queries the next provider in the fallback chain.
- **Universal Format Support**: High-performance decoding for `.png`, `.jpg` / `.jpeg`, `.webp` (via Windows Imaging Component), `.gif`, and `.bmp`.

### Internet Radio & Streaming Intelligence
- **ACRCloud Acoustic Audio Recognition**: Local 16 kHz PCM stream sampling and RIFF WAV payload generation to identify untagged radio streams and music tracks via acoustic fingerprinting.
- **Multi-Account ACRCloud Failover**: Configure Primary and Secondary credentials with automatic, seamless failover when the Primary account exhausts its monthly quota (Error 3001).
- **Windows DPAPI Credential Security**: ACRCloud Access Keys and Secrets are securely encrypted on disk using the Windows Data Protection API (`CryptProtectData`), eliminating plaintext exposure in `.cfg` files.
- **CoverSync Stream Latency Compensation**: Asynchronous cue delay compensation (`?coversync=±N`) synchronizes artwork and metadata transitions on radio streams where the audio buffer lags behind metadata changes.
- **External Now-Playing Stream APIs**: Automated polling and auto-probing for AzuraCast (`/api/nowplaying`) and RadioReg (`api.radioreg.net`) JSON endpoints, supporting live listener counts, cue timings, and HLS streams.
- **m-TAGS & URL Modifiers**: Full support for `.tags` files (reading target stream URLs from the `@` metadata tag) and URL query flags (`?forceacr`, `?coversync`, `?inverted`, `?rejectstationcovers`, `?ext_api_autoprobe`, `?bypass`).

### Dynamic Title Formatting Variables
- **Canonical API Metadata Publication**: Commercial search APIs (Deezer, iTunes, Last.fm, Discogs) and ACRCloud extract authoritative metadata directly into foobar2000 title formatting variables (`%foo_artwork_artist%`, `%foo_artwork_title%`, `%foo_artwork_album%`), replacing messy stream tags.
- **Dynamic Track Timing**: Real-time duration (`%foo_artwork_length%`), elapsed time (`%foo_artwork_playback_time%`), and countdown remaining (`%foo_artwork_playback_remaining%`) calculated for both local audio files and live radio streams.
- **Live Stream Diagnostics**: Real-time status string (`%foo_artwork_status%`) reporting query stages, stream connection progress, CoverSync countdowns, and image dimensions with payload size (e.g. `Art 1200x1200 342 KB from Deezer`).
- **Live Listener Counts**: Real-time listener statistics (`%foo_artwork_listeners%`) from AzuraCast and RadioReg broadcasts.

### Advanced Metadata Sanitization
- **4-Stage Stream Metadata Sanitizer**: Cleans noisy stream metadata by stripping broadcast slogans, stream headers, URLs, timestamps, track numbers, and media noise while protecting band names containing `&`, `and`, or slashes (`Mumford & Sons`, `Earth, Wind & Fire`, `AC/DC`).
- **Accented UTF-8 Title Casing**: Normalizes all-caps and all-lowercase stream metadata into Title Case with full support for accented characters across Portuguese, Spanish, French, and Italian without CP1252 character corruption.
- **Non-Latin Script Preservation**: Conservative sanitization rules protect Cyrillic, Greek, and other non-Latin alphabets from accidental bracket stripping or character truncation.
- **Dedicated Noise Words Blacklist**: User-configurable blacklist page backed by an auto-reloaded text file (`foo_artwork_data\blacklist.txt`).

### Caching & Storage Architecture
- **Persistent Disk Metadata Caching (`.meta` Sidecars)**: Remote API metadata is cached on disk alongside image binaries (`<hash>.meta` alongside `<hash>.cache`), ensuring instant warm track replays in under 1ms with zero network overhead.
- **Automatic 1 GB Disk Cache Limit**: Intelligent background LRU (Least Recently Used) cache eviction automatically keeps the cache under 1 GB, pruning old entries down to 900 MB when needed.
- **Single-File Cache Mode (`current.cache`)**: Overwrites a single cache file (`current.cache`) for external skin scripts and panels (e.g. JScript Panel 3 Thumbs) that monitor a folder for the currently playing track's artwork.
- **Cache Freshness Verification**: Automatically verifies whether local artwork or file tags were updated after the cache was generated, refreshing stale cache entries automatically.

### User Interface & Interactivity
- **Dual UI Support**: Native panel integration for both Default UI (DUI) and Columns UI (CUI).
- **Popup Artwork Viewer**: Double-click any artwork panel to launch an interactive viewer with window memory, a toggle between "Fit to window" and "Show original size", mouse-drag panning for oversized images, and a "Save Image..." button.
- **foo_artgrab Integration**: Automatic hover overlay displaying a download arrow icon in the bottom-left corner when `foo_artgrab.dll` is installed, launching the art grabber gallery in one click.
- **On-Screen Display (OSD) & Infobar**: Optional Infobar in DUI displaying artist, album, and track title; configurable OSD overlay showing the artwork provider source.
- **Dark Mode Support**: Full native theme integration with foobar2000 dark mode across all UI elements and dialogs.

---

## Installation

1. Download the latest **`foo_artwork.fb2k-component`** from [GitHub Releases](https://github.com/jame25/foo_artwork/releases).
2. Double-click the downloaded file or navigate in foobar2000 to **File → Preferences → Components → Install...** and select the `.fb2k-component` file.
3. Click **Apply**; foobar2000 will prompt to restart.

---

## UI Integration & Setup

### Default User Interface (DUI)
1. Navigate to **View → Layout → Enable layout editing mode** (or **Preferences → Display → Default User Interface**).
2. Right-click the desired area in your layout and select **Add New UI Element...** (or **Insert UI Element**).
3. Under the element selection dialog, choose **Artwork Display** and click **OK**.
4. Resize the panel as desired.
5. Exit layout editing mode (**View → Layout → Enable layout editing mode**).

### Columns UI (CUI)
1. Go to **File → Preferences → Display → Columns UI → Layout**.
2. Select the container where you want to add the panel and click **Insert Panel...**.
3. Under **Panels**, select **Artwork Display**.
4. Click **Apply** and **OK**.

---

## Artwork Retrieval & Fallback Hierarchy

When a track or stream begins playing, `foo_artwork` searches for artwork through a prioritized 8-stage hierarchy:

```
┌─────────────────────────────────────────────────────────────┐
│ 1. Local Files & Embedded Tags (cover.jpg, folder.jpg, ID3) │
└──────────────────────────────┬──────────────────────────────┘
                               │ (Not found or "Always skip local" enabled)
                               ▼
┌─────────────────────────────────────────────────────────────┐
│ 2. In-Stream Broadcast Artwork ($info(cover_url), song.art) │
└──────────────────────────────┬──────────────────────────────┘
                               │ (Not found, disabled, or rejected)
                               ▼
┌─────────────────────────────────────────────────────────────┐
│ 3. YouTube Video Thumbnail (maxresdefault.jpg / hqdefault)  │
└──────────────────────────────┬──────────────────────────────┘
                               │ (Not a YouTube track or rejected)
                               ▼
┌─────────────────────────────────────────────────────────────┐
│ 4. Online Commercial Search APIs (User Priority Order)      │
│    Deezer ──► iTunes ──► Last.fm ──► MusicBrainz ──► Discogs │
└──────────────────────────────┬──────────────────────────────┘
                               │ (All APIs exhausted or returned no match)
                               ▼
┌─────────────────────────────────────────────────────────────┐
│ 5. ACRCloud Acoustic Audio Recognition (Live PCM Sampling)  │
└──────────────────────────────┬──────────────────────────────┘
                               │ (No acoustic match found)
                               ▼
┌─────────────────────────────────────────────────────────────┐
│ 6. Custom Station Logos (Full URL Path ──► Domain Match)     │
└──────────────────────────────┬──────────────────────────────┘
                               │ (No station logo found)
                               ▼
┌─────────────────────────────────────────────────────────────┐
│ 7. Station-Specific Fallback Images ({path}-noart / {dom})  │
└──────────────────────────────┬──────────────────────────────┘
                               │ (No station fallback found)
                               ▼
┌─────────────────────────────────────────────────────────────┐
│ 8. Universal Generic Fallback (noart.png / Image Cycling)   │
└─────────────────────────────────────────────────────────────┘
```

1. **Local Files & Embedded Tags**: Searches all artwork files and embedded tags defined in foobar2000 (*Preferences > Display > Album Art*), respecting priority order (Front Cover > Disc > Artist > Back). Automatically skipped if **"Always skip local artwork & tags"** is enabled in preferences.
2. **In-Stream Broadcast Artwork**: Extracts cover images broadcast in stream headers (`$info(cover_url)`, `COVER_URL`, `ARTWORK_URL`, `icy-cover-url`, `song.art`, or AzuraCast/RadioReg JSON). Bypassed if **"Disable in-stream artwork"** is checked or if `?rejectstationcovers` is active.
3. **YouTube Video Thumbnail**: For YouTube playback (`youtube.com`, `youtu.be`, `3dyd://`, `fy://`), queries commercial music APIs first and falls back to downloading the highest available resolution video thumbnail (`maxresdefault.jpg` or `hqdefault.jpg`).
4. **Online Commercial Search APIs**: Queries enabled music databases in the user's configured priority order (Deezer, iTunes, Last.fm, MusicBrainz, Discogs) using cleaned artist and title tokens.
5. **ACRCloud Acoustic Recognition**: Captures a 5-second PCM audio sample and queries ACRCloud fingerprinting. Triggered automatically on streams with `?forceacr`, via manual hotkey (*View > Artwork Display > Force ACRCloud Audio Recognition*), or on untagged streams. On a successful match, commercial search APIs are automatically queried for high-resolution release artwork.
6. **Custom Station Logos**: Checks `%APPDATA%\foobar2000-v2\foo_artwork_data\logos\` for stream-specific logos (`{full_path}.png`), then falls back to domain-wide logos (`{domain}.png`).
7. **Station-Specific Fallbacks**: Checks for `{full_path}-noart.png` or `{domain}-noart.png`.
8. **Universal Generic Fallback**: Displays `noart.png` or cycles through placeholder images in the configured No-Art directory (Sequential or Random mode).

---

## Title Formatting Reference

`foo_artwork` registers dynamic fields with foobar2000's title formatting engine via `metadb_display_field_provider`. These fields can be used anywhere title formatting is supported (playlist columns, status bar, Default UI / Columns UI headers, JScript Panel, foo_nowbar, etc.).

### Available Variables

| Variable | Description | Example Output |
|:---|:---|:---|
| `%foo_artwork_artist%` | Canonical artist name resolved from commercial search APIs (Deezer, iTunes, Last.fm, Discogs), ACRCloud recognition, or sanitized stream metadata. Preserves featured collaborators. | `Daft Punk feat. Pharrell Williams` |
| `%foo_artwork_artist_full%` | Alias to `%foo_artwork_artist%` for backward compatibility. | `Daft Punk feat. Pharrell Williams` |
| `%foo_artwork_title%` | Canonical track title resolved from commercial search APIs, ACRCloud, or cleaned stream cues. | `Get Lucky` |
| `%foo_artwork_album%` | Release album name resolved from commercial search APIs, in-stream headers (`ALBUM`, `ICY-ALBUM`), or audio file tags. | `Random Access Memories` |
| `%foo_artwork_cover%` | Full local filesystem path to the currently displayed artwork or cache file. | `C:\...\foo_artwork_data\_cache\a1b2c3d4.cache` |
| `%foo_artwork_path%` | Alias for `%foo_artwork_cover%`. | `C:\...\foo_artwork_data\_cache\a1b2c3d4.cache` |
| `%foo_artwork_source%` | The resolved source provider of the currently displayed artwork. | `Deezer`, `iTunes`, `Broadcast Artwork`, `Local`, `Cache`, `ACRCloud`, `Station Logo`, `No-Art` |
| `%foo_artwork_status%` | Real-time status string showing query activity, stream connection progress, CoverSync countdowns, or image dimensions and size. | `Art 1200x1200 342 KB from Deezer`, `Cover 600x600 89 KB from iTunes (ACR)`, `Next cue in 15s: Artist - Title` |
| `%foo_artwork_listeners%` | Current live listener count broadcast by AzuraCast or RadioReg JSON streams. | `1428` |
| `%foo_artwork_length%` | Formatted track duration (`M:SS` or `H:MM:SS`), automatically calculated for both local audio files and radio streams with JSON cue timing. | `4:08` |
| `%foo_artwork_playback_time%` | Formatted elapsed playback time (`M:SS` or `H:MM:SS`), synchronized with live playback and compensated for CoverSync delay offsets. | `1:25` |
| `%foo_artwork_playback_remaining%` | Formatted remaining track playback time counting down to the next stream cue or track finish. | `2:43` |

### Title Formatting Examples

**Status Bar / Now Playing Script:**
```titleformat
[%foo_artwork_artist% - ][%foo_artwork_title%][ '('%foo_artwork_album%')'] 
$if(%foo_artwork_listeners%, | Listeners: %foo_artwork_listeners%,)
$if(%foo_artwork_source%, | Art: %foo_artwork_source%,)
$if(%foo_artwork_playback_time%, | [%foo_artwork_playback_time% / %foo_artwork_length%],)
```

**Artwork Status & Source Display:**
```titleformat
$if(%foo_artwork_status%,%foo_artwork_status%,No Artwork)
```

---

## Stream URL Modifiers & m-TAGS Integration

You can customize how individual radio streams and tracks are processed by appending query parameters to stream URLs in your playlists or by adding corresponding metadata tags in **m-TAGS** (`.tags` files / `foo_tags`) or track properties.

Multiple parameters can be combined using `&` or `;` delimiters (e.g. `http://stream.url/live?ext_api_autoprobe&rejectstationcovers`).

| Parameter / Tag | Tag Equivalent | Purpose & Effect |
|:---|:---|:---|
| `?forceacr` | `FORCEACR=1` | Bypasses online text search APIs entirely and forces ACRCloud acoustic fingerprinting as the exclusive recognition method for that stream. Activates the 3-second acoustic transition engine and RMS silence detector. |
| `?inverted` | `INVERTED=1`<br>`STREAM_INVERTED=1` | Inverts stream metadata parsing order from `Title - Artist` to `Artist - Title`. Essential for stations that broadcast reversed metadata. |
| `?bypass` | `BYPASS=1` | Completely disables artwork lookups and background polling for this specific track or stream. |
| `?rejectstationcovers` | `REJECTSTATIONCOVERS=1` | Instructs the component to skip station logos and generic in-stream broadcast covers, ensuring online APIs are queried for commercial music release artwork. |
| `?ext_api_autoprobe` | `EXT_API_AUTOPROBE=1` | Forces external now-playing server probing and background polling (AzuraCast / RadioReg) even when native ICY stream metadata is present. |
| `?coversync=N`<br>`?coversync=-N` | `COVERSYNC=N`<br>`COVERSYNC=-N` | Compensates for radio stream buffer latency. Delays (positive `N`) or advances (negative `N`) artwork and metadata cue updates by `N` seconds to perfectly synchronize with delayed audio streams. |
| `?azuracast_api`<br>`?azuracast_api={id}` | `AZURACAST_API={id}` | Explicitly designates the stream host as an AzuraCast server and optionally targets a specific numeric station ID or slug (e.g. `?azuracast_api=2`). |
| `?radioreg_api`<br>`?radioreg_api={id}` | `RADIOREG_API={id}` | Explicitly designates the stream as a RadioReg server and targets `https://api.radioreg.net/stream/{id}`. |

### m-TAGS (`.tags` Files) Compatibility
For users organizing radio streams with `foo_tags` (m-TAGS):
- The component automatically inspects the `@` metadata field (which holds the underlying `http://` or `https://` target stream URL).
- URL query parameters placed on the `@` target stream URL or custom metadata fields (`COVERSYNC`, `INVERTED`, `FORCEACR`, `REJECTSTATIONCOVERS`, etc.) are recognized identically to direct playlist stream URLs.
- Delimiter handling supports semicolons (`;`), ampersands (`&`), XML entities (`&amp;`), and percent-encoded characters (`%26`, `%3B`).

---

## External Stream APIs & Auto-Probing

For modern internet radio servers (such as AzuraCast and RadioReg) and HLS streams that do not embed standard ICY metadata directly in the audio stream, `foo_artwork` includes an asynchronous background polling engine:

- **Automatic Server Auto-Probing**: When connecting to an internet radio stream without explicit URL tags, the component probes candidate endpoints in the background (e.g. `/api/nowplaying/<slug>`, `/api/nowplaying`, and `api.radioreg.net`). Once a compatible API is detected, background polling activates automatically.
- **Multi-Station JSON Disambiguation**: When an AzuraCast root `/api/nowplaying` endpoint returns an array of multiple stations, `foo_artwork` uses a 4-stage matching heuristic (mount point paths, HLS URLs, and URL slug comparison) to identify the exact station playing.
- **CoverSync Integration**: Metadata changes detected via external APIs trigger the configured `?coversync=N` latency countdown, reporting real-time status via `%foo_artwork_status%` (e.g. `Next cue in 12s: Artist - Title`).
- **Resource Deduplication**: Repeated poll iterations compare sanitized track cues case-insensitively; identical songs do not trigger redundant downloads or UI redraws. All pollers immediately terminate when playback stops or the track changes.

---

## Preferences Configuration

Access preferences via **File → Preferences → Tools → Artwork Display**.

```
Preferences
 └── Tools
      └── Artwork Display
           ├── Advanced
           ├── Blacklist
           └── ACRCloud
```

### 1. Main Preferences (Tools → Artwork Display)

- **API Services**:
  - **iTunes**: Search API covering popular and contemporary releases. No key required.
  - **Deezer**: High-resolution 1000x1000 artwork search. No key required. (Enabled by default).
  - **MusicBrainz**: Community metadata with Cover Art Archive integration. No key required.
  - **Discogs**: Excellent for rare, international, and vinyl releases. Supports direct API Key or Consumer Key & Secret pair. Get credentials from [Discogs Developers](https://www.discogs.com/settings/developers).
  - **Last.fm**: Community music scrobbler database. Requires a free API key from [Last.fm API](https://www.last.fm/api/account/create).
- **Priority Section**: Five dropdown menus allow you to define the exact sequence of online API queries from left to right (highest to lowest priority). Disabled APIs in the priority chain are automatically skipped.
- **Disk Artwork Cache**:
  - **Disabled**: Keeps artwork in memory only for the current session.
  - **Enabled**: Caches downloaded artwork and canonical metadata sidecars (`.meta`) to disk with an automatic 1 GB LRU limit.
  - **Enabled (Single File)**: Keeps only the currently playing track's artwork as `foo_artwork_data\_cache\current.cache`. Ideal for external scripts (such as JScript Panel 3 Thumbs) that watch a folder for artwork changes.
  - **Clear**: Deletes all cached `.cache` and `.meta` files and clears memory cache immediately.
  - **`...` (Browse Button)**: Configure a custom disk cache directory outside the profile directory.
- **Hide / Show Artwork Source**: Toggles whether the artwork provider source name (e.g. `Deezer`, `Local`) is rendered on the panel as an On-Screen Display (OSD).
- **Always skip local artwork & tags**: When enabled, the component ignores local embedded artwork and local audio file tags for `%foo_artwork_*%` title formatting variables, ensuring only verified online API or cloud metadata is displayed.
- **Console Logging Mode**:
  - **Quiet**: Suppresses all console output from the component.
  - **Track Info**: Outputs a clean single-line entry (`foo_artwork: 'Artist - Title' DD/MM/YYYY HH:MM`) for played tracks.
  - **Debug**: Detailed diagnostic logging (HTTP queries, retry attempts, PCM buffer states, acoustic shift events).

---

### 2. Advanced Preferences (Tools → Artwork Display → Advanced)

- **Station Logos**:
  - **Enable custom station logos**: Toggle the station logo fallback system.
  - **Custom logos folder**: Specify a custom folder for logos (defaults to `%APPDATA%\foobar2000-v2\foo_artwork_data\logos\`).
  - **No-Art placeholder folder**: Custom directory containing fallback images (leave empty to share the logos folder).
  - **No-Art Image Cycling**: Select how placeholder images are displayed when no artwork is available:
    - *Disabled (Single noart)*: Always uses `noart.png`.
    - *Sequential*: Cycles through all supported images in the folder in alphabetical order on track transitions.
    - *Random*: Selects a random image from the folder on each track transition.
- **Miscellaneous**:
  - **Clear panel when playback stopped**: Clears the artwork panel when playback stops.
  - **[ Use noart image ]**: Displays the configured `noart.png` placeholder image when playback is stopped instead of leaving the panel blank.
  - **Show Infobar (DUI only)**: Renders a top bar in Default UI showing Artist, Album, Title, and source information.
  - **Disable in-stream artwork**: Completely ignores in-stream broadcast artwork headers (`$info(cover_url)`, `song.art`, `COVER_URL`), allowing radio streams with blurry or low-resolution broadcast covers to fall back directly to high-definition commercial search APIs.
  - **HTTP Timeout & Retries**: Configurable network timeout in seconds (default: 10s) and retry count (0–5) utilizing exponential backoff (1s, 2s, 4s...) for transient network failures.

---

### 3. Noise Words Blacklist (Tools → Artwork Display → Blacklist)

Internet radio streams frequently inject advertising tags, station jingles, promotional slogans, and broadcast identifiers into track titles. The Blacklist preferences page provides a dedicated editor to filter these phrases out before querying artwork APIs:

- **Storage**: Blacklist entries are stored as a UTF-8 text file at `%APPDATA%\foobar2000-v2\foo_artwork_data\blacklist.txt`.
- **Live Hot-Reloading**: Edits made directly in `blacklist.txt` using external text editors are automatically detected and reloaded on-the-fly during playback.
- **Quick Action Buttons**:
  - **Open File**: Opens `blacklist.txt` in your default text editor.
  - **Open Folder**: Opens the `foo_artwork_data` directory in Windows Explorer.
  - **Reload File**: Manually re-reads the file from disk into active memory.
- **Syntax Rules**:
  - Delimit phrases with newlines, commas (`,`), or semicolons (`;`).
  - Full-line comments begin with `#` or `//`.
  - Phrases are matched case-insensitively and stripped from artist and track title strings. If a title becomes empty after filtering, the component gracefully treats it as station metadata and skips invalid API searches.

---

### 4. ACRCloud Live Recognition (Tools → Artwork Display → ACRCloud)

Acoustic fingerprinting recognition identifies music from live audio streams even when streams have no metadata or incorrect tags:

- **Enable ACRCloud Live Stream Recognition**: Master toggle for ACRCloud acoustic recognition.
- **Primary Account**: Host endpoint, Access Key, and Access Secret obtained from the [ACRCloud Console](https://console.acrcloud.com/).
- **Secondary Account (Automatic Failover)**: Secondary credentials used for seamless failover. When the Primary account hits the 2,000 monthly free request quota (Error 3001: `requests limit exceeded`), `foo_artwork` automatically switches to the Secondary account without interrupting playback.
- **DPAPI Credential Encryption**: Secret keys are masked in the UI (`ES_PASSWORD`) and encrypted on disk using the Windows Data Protection API (`CryptProtectData`) with Base64 encoding (`ENC:` prefix). Credentials are decrypted only in memory when making HTTPS authentication signatures.

---

## Metadata Cleaning & Sanitization Engine

Raw metadata from internet streams often contains formatting inconsistencies that break online search API lookups. `foo_artwork` runs all stream metadata through a 4-stage sanitization pipeline:

1. **Stage 1: Noise Pre-Cleaning & Character Normalization**:
   - Strips UTF-8 BOM markers, standardizes Unicode curly quotes/apostrophes (`’`, `‘`, `‚` $\to$ `'`), and converts underscores to spaces (`Black_Sabbath` $\to$ `Black Sabbath`).
   - Removes trailing duration timestamps (` - 03:45`), pipe delimiters (`|`), bullet markers (`•`), and tilde stream delimiters (`Title~Artist~...`).
   - Strips leading track numbers (`01. `, `02 - `, `Track 05: `) while protecting numeric band names (`10cc`, `Blink-182`, `1975`, `2Pac`, `50 Cent`).
   - Strips broadcast dates (`(2023-05-12)`, `[2024.01.15]`) and media format keywords (`CD`, `DVD`, `Vinyl`, `LP`, `Album`, `Disco`, `Faixa`, `Pista`) across English, Portuguese, German, Spanish, Italian, and French.
   - Script-aware bracket removal: Conservatively preserves bracketed text in Cyrillic, Greek, and other non-Latin scripts while stripping remix/tag chatter in Latin scripts.
2. **Stage 2: Smart Stream Splitter**:
   - If the artist field is empty or contains placeholder text (`?`, `Unknown`, `Radio Station`), detects embedded song information in the title (e.g. `"The Beatles - Hey Jude"` or `"Hey Jude by The Beatles"`). Delimiters (` - `, ` / `, ` by `) are split and inverted if necessary.
   - Decodes XML stream titles broadcasting metadata in XML elements (`<artist>`, `<title>`, `<album>`) including CDATA and entity unescaping.
3. **Stage 3: Multilingual Collaboration & Token Extraction**:
   - Separates multiple artists across standard connectors (`feat.`, `ft.`, `with`, `pres.`, `presents`, `X`, `vs`, `y`, `e`, `part.`, `/`).
   - Intelligently protects compound band names containing `&` or `and` (`Mumford & Sons`, `Earth, Wind & Fire`, `Al Bano & Romina Power`, `AC/DC`).
   - Extracts `first_artist`, `second_artist`, and `primary_title` to enable multi-tiered fallback querying.
4. **Stage 4: Search Validation**:
   - Detects station names, URLs (`*.fm`, `*.stream`, `*.live`, `somafm`, `di.fm`, etc.), promotional slogans (`24/7`, `non-stop`, `on air`), and advertisement breaks (`adbreak`, `advertisement`), suppressing spurious API queries.

---

## Custom Station Logos & Fallback Images

When listening to internet radio streams where track-specific artwork cannot be found, `foo_artwork` displays custom station logos or fallback images stored in:
`%APPDATA%\foobar2000-v2\foo_artwork_data\logos\`

### Two-Tier Naming Specificity

1. **Full URL Path Matching (Most Specific)**:
   - Matches the sanitized URL path of the stream.
   - Example: For `https://ice1.somafm.com/indiepop-128-aac` $\to$ create `https---ice1.somafm.com-indiepop-128-aac.png`.
   - *Tip*: The exact filename expected by the component is printed to the foobar2000 console when playback begins.
2. **Domain-Only Matching (Broad Fallback)**:
   - Matches the domain name of the stream host.
   - Example: For `https://ice1.somafm.com/indiepop-128-aac` $\to$ create `somafm.com.png`. All streams hosted under SomaFM will use this logo if no path-specific logo exists.

### Station-Specific & Generic Fallbacks

- **Station Fallback**: Displayed when track artwork search fails for a specific station:
  - Path-specific: `https---ice1.somafm.com-indiepop-128-aac-noart.png`
  - Domain-specific: `somafm.com-noart.png`
- **Universal Generic Fallback**: Place `noart.png` in the logos directory. Displayed when all API searches, logos, and station-specific fallbacks are exhausted.
- **Image Cycling**: When multiple images are placed in the No-Art placeholder directory, the component can cycle through them sequentially or randomly on track transitions.

Supported image formats: `.png`, `.jpg` / `.jpeg`, `.webp`, `.bmp`, `.gif`.

### Compatibility with Other foobar2000 Artwork Readers
To display station logos created for `foo_artwork` in other foobar2000 artwork readers, add the following string to **Preferences > Display > Album Art (Front Cover)**:
```titleformat
C:\Users\<username>\AppData\Roaming\foobar2000-v2\foo_artwork_data\logos\$replace(%path%,$char(47),$char(45),$char(92),$char(45),$char(448),$char(45),$char(58),$char(45),$char(42),$char(140),$char(34),$char(39)$char(39),$char(60),$char(95),$char(62),$char(95),$char(63),$char(95)).*
```

---

## Interactive UI & Artwork Viewer

### Popup Artwork Viewer
Double-clicking on any artwork panel opens the interactive **Artwork Viewer Popup**:
- **Display Modes**: Toggle between **Fit to window** (maintaining aspect ratio) and **Show original size**. The selected display mode is remembered across sessions.
- **Image Panning**: In "original size" mode, click and drag with the left mouse button to smoothly pan around images larger than the window. Pan position resets on mode switch or window resize.
- **Save Image**: Click the **Save Image...** button to export the displayed artwork directly to disk in PNG, JPEG, BMP, or GIF formats.
- **Image Dimensions & Source Info**: Displays native image resolution and the artwork provider source in the title bar and status label.
- **DPI-Aware Scaling**: Enforces a minimum window size of 6x6 inches scaled to your system DPI.

### `foo_artgrab` Integration (https://github.com/jame25/foo_artgrab)
When the companion component `foo_artgrab.dll` is present in your foobar2000 components folder:
- Hovering your mouse over the artwork panel reveals an interactive **download arrow icon** in the bottom-left corner.
- Clicking the icon instantly opens the `foo_artgrab` search gallery pre-populated with the artist, album, and file path of the currently playing track.

---

## Main Menu Commands & Keyboard Shortcuts

All component commands are consolidated in the foobar2000 main menu under **View → Artwork Display**:

| Menu Command | Description | Recommended Hotkey |
|:---|:---|:---|
| **Force ACRCloud Audio Recognition** | Forces an immediate live PCM capture and ACRCloud acoustic recognition scan on the active track or stream, bypassing all cooldowns. | `Ctrl+Alt+A` |
| **Reject Artwork & Search Next Provider** | Rejects the displayed cover art, evicts the disk cache entry, blacklists the provider for the active track, and immediately searches the next provider in the chain. | `Ctrl+Alt+R` |
| **Probe External Stream API / CoverSync** | Manually triggers auto-probing of external broadcast APIs (AzuraCast / RadioReg) and synchronizes CoverSync endpoints for the playing stream. | `Ctrl+Alt+P` |
| **Force Display No-Art Placeholder** | Immediately forces the artwork display panel into No-Art mode using configured placeholders or station logos. | `Ctrl+Alt+N` |

*To assign keyboard shortcuts, open **File → Preferences → General → Keyboard Shortcuts**, click **Add New**, and search for `Artwork Display`.*

---

## Caching & Storage Architecture

All persistent component data is centralized inside the `foo_artwork_data` directory within your foobar2000 profile folder (`%APPDATA%\foobar2000-v2\foo_artwork_data\`):

```
foo_artwork_data/
├── _cache/                  # Cached binary images and metadata sidecars
│   ├── 4a8f9b2c.cache       # Image binary (JPEG, PNG, WebP)
│   ├── 4a8f9b2c.meta        # Canonical metadata (Artist, Title, Album, Source)
│   └── current.cache        # Active track image (in Single-File mode)
├── logos/                   # Custom station logos and fallback placeholders
│   ├── somafm.com.png
│   ├── https---ice1...png
│   └── noart.png
└── blacklist.txt            # Custom noise words blacklist
```

- **Persistent Disk Metadata Caching (`.meta` Sidecars)**: Canonical metadata (Artist, Title, Album, Source) resolved from remote APIs is serialized to disk alongside image cache files. On track replays, both cover art and authoritative tags load in under 1ms with zero network requests.
- **Cache Invalidation & Freshness Verification**: When playing local audio files, `foo_artwork` verifies whether new local artwork files were placed in the album folder or if audio file tags were modified after the cache was generated. If changes are detected, stale cache entries are automatically refreshed.
- **Automatic 1 GB LRU Cleanup**: Background maintenance keeps total cache size under 1 GB, automatically pruning the oldest `.cache` and `.meta` files down to 900 MB whenever the limit is reached.
- **Single-File Cache Mode**: Designed for external components and scripts (e.g. JScript Panel 3 Thumbs) that monitor a directory for the active track's art. Every track change overwrites `current.cache`, eliminating cache growth.

---

## Developer C-API & Companion Integration

`foo_artwork.dll` exports a native C-interface, allowing external components and scripts (such as `foo_nowbar`, `foo_traycontrols`, `foo_artgrab`, and JScript Panel 3) to integrate directly with the artwork pipeline:

```cpp
// DLL Exports
extern "C" {
    // Register / unregister multi-subscriber artwork update callbacks
    void foo_artwork_set_callback(pfn_artwork_callback callback);
    void foo_artwork_remove_callback(pfn_artwork_callback callback);

    // Retrieve the active GDI+ HBITMAP handle
    HBITMAP foo_artwork_get_bitmap();

    // Trigger an asynchronous artwork search for specific metadata
    void foo_artwork_search(const char* artist, const char* title);

    // Refresh all panels using the now-playing track (local files -> APIs)
    void foo_artwork_refresh();

    // Invalidate a specific track entry from memory and disk cache
    void foo_artwork_cache_remove(const char* artist, const char* track);

    // Check loading and API state
    bool foo_artwork_is_loading();
    bool foo_artwork_is_api_enabled(const char* api_name);

    // Retrieve configured API keys
    const char* foo_artwork_get_lastfm_key();
    const char* foo_artwork_get_discogs_key();
    const char* foo_artwork_get_discogs_consumer_key();
    const char* foo_artwork_get_discogs_consumer_secret();
}
```

---

## Building from Source

### Prerequisites

- **Visual Studio 2022** with C++ development tools
- **Platform Toolset v143** (Visual Studio 2022 Build Tools)
- **MSVC v143 - VS 2022 C++ ATL for v143 build tools**
- **Windows 10 / 11 SDK** (10.0.19041.0 or later)
- **foobar2000 SDK** (included via Columns UI SDK in `columns_ui/` directory)
- C++ Standard: **C++17** (`/std:c++17`, `/utf-8`)

### SDK Structure

This component uses the **Columns UI SDK** which includes the complete foobar2000 SDK. The SDK is located in:
```
columns_ui/
├── foobar2000/
│   └── SDK/                  # Complete foobar2000 SDK
│   └── shared/shared-x64.lib # Pre-compiled SDK library (shared-x64.lib / shared-Win32.lib)
└── ...
```

This approach provides:
- **Dual UI Support**: Both Default UI (DUI) and Columns UI (CUI) compatibility
- **Complete SDK**: All necessary foobar2000 headers and libraries
- **x64 and 32-bit Ready**: Supports both 64-bit and 32-bit foobar2000 v2

### Additional Libraries Needed

- **nlohmann/json**: https://github.com/nlohmann/json (included in `nlohmann/`)

### Manual Build (Recommended)

#### Step 1: Build SDK Dependencies

Compile the required SDK components with the correct platform toolset (e.g. x64 Release):

```batch
# Build foobar2000 SDK
msbuild columns_ui/foobar2000/SDK/foobar2000_SDK.vcxproj /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 /v:minimal

# Build PFC library
msbuild columns_ui/pfc/pfc.vcxproj /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 /v:minimal

# Build Columns UI SDK
msbuild columns_ui/columns_ui-sdk/columns_ui-sdk.vcxproj /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 /v:minimal
```

#### Step 2: Build Main Component

After SDK dependencies are built, compile `foo_artwork`:

```batch
# Build Release version (recommended)
msbuild foo_artwork.vcxproj /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 /v:minimal

# Build Debug version
msbuild foo_artwork.vcxproj /p:Configuration=Debug /p:Platform=x64 /p:PlatformToolset=v143 /v:minimal

# Clean and rebuild
msbuild foo_artwork.vcxproj /t:Clean,Build /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 /v:minimal
```

*Note*: You can also run the included `build.bat` script from a Visual Studio developer command prompt.

### Output
- **Release DLL**: `Release/foo_artwork.dll` (or `Release/foo_artwork.fb2k-component`)
- **Debug DLL**: `Debug/foo_artwork.dll`

---

## API Implementation Details

### Deezer API (Default)
- Search Endpoint: `https://api.deezer.com/search/track`
- Authentication: None required
- Returns JSON with high-resolution 1000x1000 release artwork URLs
- Automatic unescaping and parameter sanitization

### iTunes API
- Search Endpoint: `https://itunes.apple.com/search`
- Authentication: None required
- Returns JSON with album release artwork

### Last.fm API
- Endpoint: `https://ws.audioscrobbler.com/2.0/`
- Authentication: Requires API key parameter
- Returns XML/JSON with community-contributed artwork URLs

### MusicBrainz API
- Search Endpoint: `https://musicbrainz.org/ws/2/release`
- Cover Art Endpoint: `https://coverartarchive.org/release/`
- Authentication: None required (uses User-Agent compliance)
- Two-step process: queries release metadata, then fetches front cover from Cover Art Archive

### Discogs API
- Endpoint: `https://api.discogs.com/database/search`
- Authentication: Requires API key or Consumer Key & Secret
- Returns JSON with release artwork URLs

### ACRCloud API
- Endpoint: Custom host endpoint (e.g. `https://<host>/v1/identify`)
- Authentication: HMAC-SHA1 signature using Access Key & Secret (DPAPI encrypted)
- Sends 5-second 16 kHz mono RIFF WAV audio fingerprint payload

---

## Troubleshooting & FAQ

### 1. No artwork is displayed for internet radio streams
- **Check Enabled APIs**: Ensure Deezer or iTunes is enabled in *Preferences > Tools > Artwork Display*. Deezer works immediately without requiring registration.
- **Station Broadcasts Blurry or Incorrect Artwork**: If an internet stream broadcasts low-resolution station logos as in-stream artwork, check **"Disable in-stream artwork"** in *Preferences > Tools > Artwork Display > Advanced*, or append `?rejectstationcovers` to the stream URL.
- **Inverted Artist and Title**: If the radio station transmits `Title - Artist` instead of `Artist - Title`, append `?inverted` to the stream URL.
- **Station Noise Words**: If the station title includes broadcast slogans (e.g. `LIVE ON AIR`, `BEST 80S HITS`), add the phrases to *Preferences > Tools > Artwork Display > Blacklist*.

### 2. Stream audio lags behind artwork and metadata transitions
- Append `?coversync=N` to the stream URL (e.g. `http://stream.url/live?coversync=15`), where `N` is the number of seconds the stream audio buffer is delayed. `foo_artwork` will delay updating artwork and title formatting until the audio catches up.

### 3. ACRCloud returns Error 3001 (`requests limit exceeded`)
- Free ACRCloud developer accounts have a monthly limit of 2,000 requests. Configure a **Secondary Account** in *Preferences > Tools > Artwork Display > ACRCloud*. When the Primary account quota is reached, `foo_artwork` automatically fails over to the Secondary account.

### 4. Third-party panel or skin does not show current artwork
- In *Preferences > Tools > Artwork Display*, set **Disk Artwork Cache** to **Enabled (Single File)**. This writes the currently playing artwork to `foo_artwork_data\_cache\current.cache`.
- Alternatively, use the title formatting variable `%foo_artwork_cover%` to directly reference the cached image file path in your skin.

### 5. Local tags are showing up in title formatting when listening to streams
- The component strictly separates local and streaming playback. If you want `%foo_artwork_*%` title formatting variables to only display verified online API metadata, check **"Always skip local artwork & tags"** in Preferences.

---

## License

This component is provided as-is for educational and personal use. Please respect the terms of service of the API providers when using their services.

## Contributing

Contributions are welcome! Please ensure your code follows the existing style and includes appropriate error handling.

## Support Development

If you find these components useful, consider supporting development:

| Platform | Payment Methods |
|----------|----------------|
| **[Ko-fi](https://ko-fi.com/Jame25)** | Cards, PayPal |

Your support helps cover development time and enables new features. Thank you! 🙏

---

## 支持开发

如果您觉得这些组件有用，请考虑支持开发：

| 平台 | 支付方式 |
|------|----------|
| **[Ko-fi](https://ko-fi.com/Jame25)** | 银行卡、PayPal |

您的支持有助于支付开发时间并实现新功能。谢谢！🙏

---

**Feature Requests:** Paid feature requests are available for supporters. [Contact me on Discord](https://discord.gg/jSajdJ3nMz) to discuss.

**功能请求：** 为支持者提供付费功能请求。[请在 Discord 上联系我](https://discord.gg/jSajdJ3nMz) 进行讨论。
