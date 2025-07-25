# foobar2000 Artwork Display Component

A comprehensive foobar2000 component that displays cover artwork for currently playing tracks and internet radio streams with intelligent API fallback support. Both Default UI (DUI) and Columns UI (CUI) are supported.

<img width="742" height="550" alt="foo_artwork" src="https://github.com/user-attachments/assets/d9a495bc-34d4-48cf-9cb9-684f4cde2a7a" />

## Features

- **Local Artwork Search**: Automatically searches for artwork files in the same directory as your music files
- **Online API Integration**: Falls back to iTunes, Deezer, Last.fm, MusicBrainz, and Discogs APIs when local artwork is not found
- **User-Customizable API Priority**: Configure the order of API fallback chain through an intuitive interface
- **Internet Radio Support**: Displays artwork for internet radio streams using metadata
- **Custom Station Logos**: Support for custom logo files for internet radio stations
- **Configurable API Services**: Enable/disable individual API services and manage API keys
- **Smart Caching**: Prevents repeated API calls for the same track during the current session
- **High-Quality Display**: Uses GDI+ for smooth, high-quality artwork rendering with aspect ratio preservation
- **Responsive Scaling**: Automatically scales artwork to fit window size while maintaining aspect ratio
- **Dark Theme Support**: Seamless integration with foobar2000's dark mode

## Installation

1. Download the latest **foo_artwork.fb2k-component** from [releases](https://github.com/jame25/foo_artwork/releases)
2. Run the file, foobar2000 will automatically restart

**Note**: The component includes a fully functional preferences dialog accessible through File → Preferences → Tools → Artwork Display. All API services can be configured through the preferences interface.

## Configuration

### API Services

The component supports five online artwork services.

1. **iTunes API** (No API key required)
   - Uses iTunes Search API
   - Generally has good coverage for popular music

2. **Deezer API** (No API key required) ⭐ **Default**
   - Uses Deezer's public search API
   - Excellent coverage for contemporary and popular music
   - High-quality artwork
   - No registration or API key needed

3. **Last.fm API** (API key required)
   - Requires a free Last.fm account and API key
   - Good general coverage with community-contributed artwork
   - Get your API key from: https://www.last.fm/api/account/create

4. **MusicBrainz API** (No API key required)
   - Uses MusicBrainz database with Cover Art Archive integration
   - Excellent for classical, jazz, and international music
   - Community-maintained database with high accuracy

5. **Discogs API** (Consumer key/secret required)
   - Requires a free Discogs account and consumer key/secret
   - API key is optional if consumer credentials are provided
   - Excellent for rare, underground, and vinyl releases
   - Get your credentials from: https://www.discogs.com/settings/developers

### Local Artwork Search

The component automatically searches for these common artwork filenames:
- `cover.jpg`, `cover.jpeg`, `cover.png`
- `folder.jpg`, `folder.jpeg`, `folder.png`
- `album.jpg`, `album.jpeg`, `album.png`
- `front.jpg`, `front.jpeg`, `front.png`
- `artwork.jpg`, `artwork.jpeg`, `artwork.png`

### Custom Station Logos

The component supports custom logo files for internet radio stations. This feature allows you to display station-specific logos instead of track artwork when listening to internet radio.

#### Setup

1. **Logo Directory**: Place your logo files in the `foo_artwork_data/logos/` directory within your foobar2000 profile folder
   - **Profile location**: Usually `%APPDATA%\foobar2000-v2\foo_artwork_data\logos\`
   - The directory is automatically created when the component starts

2. **Filename Format**: Logo files should be named using the station's domain name or identifier
   - **Domain-based naming**: Use the full domain from the stream URL
     - Example: For `http://maxxima.mine.nu:8000/stream` → name the file `maxxima.mine.nu.png`
     - Example: For `http://somafm.com/groovesalad.pls` → name the file `somafm.com.jpg`
   - **Station-based naming**: If domain extraction fails, the component will try to extract the station name from metadata

3. **Supported Formats**: The component supports common image formats:
   - `.png` (recommended for logos with transparency)
   - `.jpg` / `.jpeg`
   - `.gif`
   - `.bmp`

#### How It Works

- **Automatic Detection**: When connecting to an internet radio stream, the component automatically:
  1. Extracts the domain name from the stream URL
  2. Looks for a matching logo file in the `logos/` directory
  3. Displays the custom logo if found
  4. Falls back to normal track artwork search if no logo is found

- **Priority**: Station logos are used as fallbacks when API artwork search fails

- **No Configuration Required**: Simply place logo files in the correct directory with the correct filename - the component handles the rest automatically

#### Example Setup

For a radio station with URL `http://stream.example.com:8000/radio`:

1. Create logo file: `stream.example.com.png`
2. Place in: `%APPDATA%\foobar2000-v2\foo_artwork_data\logos\stream.example.com.png`
3. Connect to the radio station
4. The logo will appear immediately

This feature is particularly useful for frequently listened radio stations, providing immediate visual identification.

#### Fallback Images for Failed Artwork Searches

The component also supports **fallback images** that display when artwork searches fail for specific radio stations. This is useful for stations where track artwork is rarely available.

**Setup for Fallback Images:**

1. **Filename Format**: Add `-noart` suffix to the domain name before the file extension
   - Example: For `http://pub0302.101.ru:8000/stream` → create `pub0302.101.ru-noart.png`
   - Example: For `http://stream.example.com/radio` → create `stream.example.com-noart.jpg`

2. **When It's Used**: The fallback image is displayed **only** when:
   - Normal track artwork search fails (all APIs exhausted)
   - The stream is an internet radio station
   - No regular station logo exists (station logos have higher priority)

3. **Priority Order** for internet radio streams:
   1. **Track artwork** from APIs - highest priority, searches using metadata (if available)
   2. **Fallback hierarchy** - when API artwork search fails:
      - **Station logo** (e.g., `domain.com.png`)
      - **Station-specific fallback** (e.g., `domain.com-noart.png`)
      - **Generic fallback** (`noart.png`)
   3. **No artwork** message

**Example Setup:**

For a radio station `http://pub0302.101.ru:8000/stream/trust/mp3/128/24`:

1. Create fallback image: `pub0302.101.ru-noart.png`
2. Place in: `%APPDATA%\foobar2000-v2\foo_artwork_data\logos\pub0302.101.ru-noart.png`
3. When connecting to this station:
   - If track artwork is found → shows track artwork
   - If no track artwork is found → shows your fallback image
   - Status will display: "No artwork - showing station fallback"

#### Generic Fallback Image

In addition to station-specific fallbacks, the component supports a **universal fallback image** that applies to all internet radio streams when artwork searches fail.

**Setup for Generic Fallback:**

1. **Filename**: Create a file named `noart.png` (or `.jpg`, `.jpeg`, `.gif`, `.bmp`)
2. **Location**: Place in `%APPDATA%\foobar2000-v2\foo_artwork_data\logos\noart.png`
3. **Usage**: This image will be displayed for **any** internet radio stream when:
   - Track artwork search fails
   - No station-specific logo exists
   - No station-specific "-noart" image exists

**Priority for Generic Fallback:**
- Lowest priority in the fallback chain
- Only used when all other options are exhausted
- Provides a consistent "no artwork" visual across all radio stations

This feature ensures that radio stations always have some visual representation, even when individual track artwork cannot be found.

### Preferences Configuration

Access the preferences dialog through:
- File → Preferences → Tools → Artwork Display

Configure:
- **API Services**: Enable/disable individual API services
- **API Keys**: Set API keys and consumer credentials for services that require them
- **Priority Order**: Customize the fallback chain by selecting your preferred API for each position
- **Stream Delay**: Configure delay (1-30 seconds) before checking metadata on internet radio streams
- All changes are saved automatically

#### Priority Section
The **Priority** section contains five dropdown menus allowing you to customize the API search order:
- Each dropdown lists all available APIs (iTunes, Deezer, Last.fm, MusicBrainz, Discogs)
- Arrange them in your preferred order from left to right (highest to lowest priority)
- Only enabled APIs will be used during searches, disabled APIs are automatically skipped

#### Stream Delay (May fix stream issues)
The **Stream Delay** setting controls how long the component waits before checking metadata and searching for artwork when **initially connecting** to internet radio streams:

- **Range**: 1-10 seconds (adjustable with up/down arrows)
- **Default**: 0 seconds
- **Purpose**: Allows radio streams time to provide proper track metadata before artwork search begins
- **When to increase**: If you experience issues with artwork searches using station names instead of track names
- **Applies to**: Initial stream connections and station name detection

**Recommended values:**
- **Fast streams**: 0-2 seconds (default works well for most streams)
- **Slow streams**: 3-5 seconds (for streams that take longer to provide initial track metadata)  
- **Problem streams**: 5-10 seconds (for streams with persistent initial metadata issues)

## Usage

1. **Add UI Element**: Go to Preferences → Display → Default User Interface → Layout Editing Mode
2. **Insert Component**: Right-click in a panel and select "Insert UI Element" → "Artwork Display"
3. **Configure Size**: Resize the component to your preferred dimensions
4. **Exit Layout Mode**: Turn off Layout Editing Mode to start using the component

## Fallback Chain

The component uses the following priority order for artwork retrieval:

1. **Local Files**: Searches music file directory for common artwork filenames
2. **iTunes API**: Searches iTunes database (if enabled)
3. **Deezer API**: Searches Deezer database (if enabled)
4. **Last.fm API**: Searches Last.fm database (if enabled and API key provided)
5. **MusicBrainz API**: Searches MusicBrainz/Cover Art Archive (if enabled)
6. **Discogs API**: Searches Discogs database (if enabled and API key provided)

## Building from Source

### Prerequisites

- **Visual Studio 2019 or 2022** with C++ development tools
- **Platform Toolset v143** (Visual Studio 2022 Build Tools)
- **Windows 10 SDK** (10.0 or later)
- **foobar2000 SDK** (included in `lib/` directory)

### Quick Build

Use the provided batch file for easy building:

```batch
# Build Release version (recommended)
build.bat

# Build Debug version
build.bat debug

# Clean and build
build.bat clean release
```

The build script will:
- Automatically detect Visual Studio 2019/2022 installation
- Build all required SDK dependencies
- Generate the `foo_artwork.dll` in the appropriate directory
- Provide installation instructions upon successful build

### Manual Build Steps

1. Open `foo_artwork.sln` in Visual Studio
2. Select **Release** configuration and **x64** platform
3. Build the solution (Ctrl+Shift+B)
4. The compiled DLL will be in the `Release/` directory

### Build Configuration

- **Target Platform**: x64 only (64-bit foobar2000)
- **Platform Toolset**: v143 (Visual Studio 2022)
- **C++ Standard**: C++17
- **Runtime Library**: Multi-threaded (Release) / Multi-threaded Debug (Debug)

### Dependencies

The component links against:
- `shared-x64.lib` (foobar2000 SDK for 64-bit)
- `wininet.lib` (Windows HTTP API)
- `gdiplus.lib` (GDI+ graphics)

## API Implementation Details

### iTunes API
- Endpoint: `https://itunes.apple.com/search`
- No authentication required
- Returns JSON with artwork URLs

### Deezer API
- Endpoint: `https://api.deezer.com/search/track`
- No authentication required
- Returns JSON with high-quality artwork URLs (1000x1000)
- Automatic URL unescaping for proper image downloads

### Last.fm API
- Endpoint: `https://ws.audioscrobbler.com/2.0/`
- Requires API key parameter
- Returns XML/JSON with image URLs

### MusicBrainz API
- Search Endpoint: `https://musicbrainz.org/ws/2/release`
- Cover Art Endpoint: `https://coverartarchive.org/release/`
- No authentication required
- Two-step process: search for release → fetch cover art
- Proper User-Agent header compliance
- Multiple release and cover type fallback support

### Discogs API
- Endpoint: `https://api.discogs.com/database/search`
- Requires API key in headers
- Returns JSON with image URLs

## Troubleshooting

### Common Issues

1. **No artwork displayed**
   - Deezer should work immediately without configuration
   - Check that at least one API service is enabled in preferences
   - Verify API keys are correctly entered for services that require them
   - Check internet connection for online services

2. **High memory usage**
   - Restart foobar2000 to clear in-memory cache
   - Only one artwork image is stored in memory at a time

## License

This component is provided as-is for educational and personal use. Please respect the terms of service of the API providers when using their services.

## Contributing

Contributions are welcome! Please ensure your code follows the existing style and includes appropriate error handling.
