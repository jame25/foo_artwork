# foobar2000 Artwork Display Component

A comprehensive foobar2000 component that displays cover artwork for currently playing tracks and internet radio streams with intelligent API fallback support. Both Default UI (DUI) and Columns UI (CUI) are supported.

<img width="745" height="553" alt="foo_artwork" src="https://github.com/user-attachments/assets/21bbdabe-5948-4129-9652-e35f939a30b7" />


## Features

- **Local Artwork Search**: Automatically searches for artwork files defined in foobar2000 Preferences > Display > Album Art
- **Online API Integration**: Falls back to iTunes, Deezer, Last.fm, MusicBrainz, and Discogs APIs when local artwork is not found
- **User-Customizable API Priority**: Configure the order of API fallback chain through an intuitive interface
- **Internet Radio Support**: Displays artwork for internet radio streams using metadata
- **Custom Station Logos**: Support for custom logo files for internet radio stations
- **Configurable API Services**: Enable/disable individual API services and manage API keys
- **Smart Caching**: Prevents repeated API calls for the same track during the current session
- **High-Quality Display**: Uses GDI+ for smooth, high-quality artwork rendering with aspect ratio preservation
- **Responsive Scaling**: Automatically scales artwork to fit window size while maintaining aspect ratio
- **Dark Theme Support**: Seamless integration with foobar2000's dark mode
- **Artwork Viewer**: Double-click on artwork to open a popup viewer

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

The component automatically searches searches for all artwork files defined in foobar2000 Preferences > Display > Album Art.
The priority order to find any available tagged artwork is  Front cover > Disc > Artist > Back
Supported image formats
   - `.png` 
   - `.jpg` / `.jpeg`
   - `.gif`
   - `.bmp`
   - `.webp`

### Custom Station Logos

The component supports custom logo files for internet radio stations. This feature allows you to display station-specific logos instead of track artwork when listening to internet radio.

#### Setup

1. **Logo Directory**: Place your logo files in the `foo_artwork_data/logos/` directory within your foobar2000 profile folder
   - **Default location**: `%APPDATA%\foobar2000-v2\foo_artwork_data\logos\`
   - **Custom location**: Can be configured through Advanced preferences
   - The directory is automatically created when needed

2. **Filename Format**: The component supports **two levels of specificity** for station logos:

   **Full URL Path Matching** (Most Specific):
   - **Format**: `{full_path}.{ext}`
   - **Example**: For `https://ice1.somafm.com/indiepop-128-aac` → create `https---ice1.somafm.com-indiepop-128-aac.png`
   - **Use Case**: Different logos for different streams on the same domain
   - **Help**: The matching filename without extension that must be created is displayed in the console when playback starts.
   - **Using the logo with other readers**: The same file can be used to display the logo in other readers by adding for example the following in Preferences>Display>Album Art (front cover)
`C:\Users\name\Desktop\foobar2000\profile\foo_artwork_data\logos\$replace(%path%,$char(47),$char(45),$char(92),$char(45),$char(448),$char(45),$char(58),$char(45),$char(42),$char(140),$char(34),$char(39)$char(39),$char(60),$char(95),$char(62),$char(95),$char(63),$char(95)).*`
   
   **Domain-Only Matching** (Fallback Compatibility):
   - **Format**: `{domain}.{ext}`
   - **Example**: For `https://ice1.somafm.com/indiepop-128-aac` → create `somafm.com.png`
   - **Use Case**: Single logo for all streams from a domain

3. **Supported Formats**: The component supports common image formats:
   - `.png` (recommended for logos with transparency)
   - `.jpg` / `.jpeg`
   - `.gif`
   - `.bmp`

#### How It Works

- **Automatic Detection**: When connecting to an internet radio stream, the component automatically:
  1. Attempts to search for track artwork using metadata via configured APIs
  2. If API search fails, extracts the full URL path from the stream URL
  3. Looks for a full-path matching station logo file first
  4. Falls back to domain-only matching if no full-path logo is found
  5. Falls back to station-specific fallback images (-noart.png files)
  6. Falls back to generic fallback image (noart.png)

- **Priority**: Station logos are used as fallback when API artwork search fails (not as primary source)
  
  **Note**: API-based track artwork searches always take priority over station logos for internet radio streams. Station logos serve as visual fallbacks when metadata-based artwork searches are unsuccessful.
  
- **Configuration**: Enable/disable and customize folder path through Advanced preferences

#### Example Setup for Multi-Stream Domain

For SomaFM with multiple streams:
- `https://ice1.somafm.com/indiepop-128-aac` (Indie Pop Rocks!)
- `https://ice1.somafm.com/dronezone-256-mp3` (Drone Zone)

**Option 1: Stream-Specific Logos**
1. Create specific logos:
   - `https---ice1.somafm.com-indiepop-128-aac.png` (colorful indie logo)
   - `https---ice1.somafm.com-dronezone-256-mp3.png` (ambient space logo)
2. Place in: `%APPDATA%\foobar2000-v2\foo_artwork_data\logos\`

**Option 2: Domain-Wide Logo**
1. Create: `somafm.com.png` (applies to all SomaFM streams)
2. Place in: `%APPDATA%\foobar2000-v2\foo_artwork_data\logos\somafm.com.png`

#### Advanced Configuration

Access **Preferences → Tools → Artwork Display → Advanced** to:
- **Enable/disable** custom station logos feature
- **Set custom folder path** for logo files (or leave empty for default)

This feature is particularly useful for frequently listened radio stations, providing immediate visual identification and supporting both stream-specific and domain-wide customization.

#### Fallback Images for Failed Artwork Searches

The component supports **fallback images** that display when artwork searches fail for specific radio stations. This is useful for stations where track artwork is rarely available.

**Setup for Fallback Images:**

The component supports **two levels of specificity** for fallback images, allowing you to target specific streams or entire domains:

1. **Full URL Path Matching** (Most Specific):
   - **Format**: `{full_path}-noart.{ext}`
   - **Example**: For `https://ice1.somafm.com/indiepop-128-aac` → create `https---ice1.somafm.com-indiepop-128-aac-noart.png`
   - **Use Case**: Different fallback images for different streams on the same domain

2. **Domain-Only Matching** (Fallback Compatibility):
   - **Format**: `{domain}-noart.{ext}`
   - **Example**: For `https://ice1.somafm.com/indiepop-128-aac` → create `somafm.com-noart.png`
   - **Use Case**: Single fallback image for all streams from a domain

**When It's Used**: The fallback image is displayed **only** when:
- Normal track artwork search fails (all APIs exhausted)
- The stream is an internet radio station
- No station logo exists (checked before fallback images in the hierarchy)

**Priority Order** for internet radio streams:
1. **Track artwork** from APIs - highest priority, searches using metadata (if available)
2. **Fallback hierarchy** - when API artwork search fails:
   - **Station logo with full path** (e.g., `https---ice1.somafm.com-indiepop-128-aac.png`)
   - **Station logo with domain** (e.g., `somafm.com.png`)
   - **Station-specific fallback with full path** (e.g., `https---ice1.somafm.com-indiepop-128-aac-noart.png`)
   - **Station-specific fallback with domain** (e.g., `somafm.com-noart.png`)
   - **Generic fallback** (`noart.png`)
3. **No artwork** message

**Example Setup for Multi-Stream Domain:**

For SomaFM with multiple streams like:
- `https://ice1.somafm.com/indiepop-128-aac` (Indie Pop Rocks!)
- `https://ice1.somafm.com/dronezone-256-mp3` (Drone Zone)

**Option 1: Stream-Specific Fallbacks**
1. Create specific fallback images:
   - `https---ice1.somafm.com-indiepop-128-aac-noart.png` (indie themed image)
   - `https---ice1.somafm.com-dronezone-256-mp3-noart.png` (ambient themed image)
2. Place in: `%APPDATA%\foobar2000-v2\foo_artwork_data\logos\`

**Option 2: Domain-Wide Fallback**
1. Create: `somafm.com-noart.png` (applies to all SomaFM streams)
2. Place in: `%APPDATA%\foobar2000-v2\foo_artwork_data\logos\somafm.com-noart.png`

#### Generic Fallback Image

The component supports a **universal fallback image** that applies to all internet radio streams when artwork searches fail.

**Setup for Generic Fallback:**

The generic fallback now supports the **same two-tier system**:

1. **Stream-Specific Generic Fallback**:
   - **Format**: `{full_path}-noart.{ext}` (same as station-specific, but used as universal fallback)
   - **Example**: `https---ice1.somafm.com-indiepop-128-aac-noart.png`

2. **Domain-Specific Generic Fallback**:
   - **Format**: `{domain}-noart.{ext}` (applies to entire domain)
   - **Example**: `somafm.com-noart.png`

3. **Universal Generic Fallback**:
   - **Format**: `noart.{ext}` (applies to ALL streams)
   - **Example**: `noart.png`

**Location**: `%APPDATA%\foobar2000-v2\foo_artwork_data\logos\`

**Usage Priority**: The generic fallback tries these in order:
1. Stream-specific fallback (if exists)
2. Domain-specific fallback (if exists)  
3. Universal fallback (`noart.png`)

**When Used**: Displayed for **any** internet radio stream when:
- Track artwork search fails
- No station-specific logo exists
- No station-specific fallback image exists
- Provides a consistent "no artwork" visual across all radio stations

This feature ensures that radio stations always have some visual representation, even when individual track artwork cannot be found.

### Preferences Configuration

Access the preferences dialog through:
- File → Preferences → Tools → Artwork Display

Configure:
- **API Services**: Enable/disable individual API services
- **API Keys**: Set API keys and consumer credentials for services that require them
- **Priority Order**: Customize the fallback chain by selecting your preferred API for each position
- **Clear panel when playback stopped**: Option to automatically clear the artwork panel when playback stops
- All changes are saved automatically

#### Priority Section
The **Priority** section contains five dropdown menus allowing you to customize the API search order:
- Each dropdown lists all available APIs (iTunes, Deezer, Last.fm, MusicBrainz, Discogs)
- Arrange them in your preferred order from left to right (highest to lowest priority)
- Only enabled APIs will be used during searches, disabled APIs are automatically skipped

## Usage

1. **Add UI Element**: Go to Preferences → Display → Default User Interface → Layout Editing Mode
2. **Insert Component**: Right-click in a panel and select "Insert UI Element" → "Artwork Display"
3. **Configure Size**: Resize the component to your preferred dimensions
4. **Exit Layout Mode**: Turn off Layout Editing Mode to start using the component

## Fallback Chain

The component uses the following priority order for artwork retrieval:

1. **Local Files**: Searches all artwork files defined in foobar2000 Preferences > Display > Album Art
2. **iTunes API**: Searches iTunes database (if enabled)
3. **Deezer API**: Searches Deezer database (if enabled)
4. **Last.fm API**: Searches Last.fm database (if enabled and API key provided)
5. **MusicBrainz API**: Searches MusicBrainz/Cover Art Archive (if enabled)
6. **Discogs API**: Searches Discogs database (if enabled and API key provided)

## Inverted Stations (artist is title , title is artist)

For stations that send inverted metadata (Title - Artist) instead of (Artist - Title) you can invert the values to the correct values to be searched by appending the parameter `?inverted` to the url.

- **Example**:  `http://icecast-qmusicnl-cdp.triple-it.nl/Qmusic_nl_live_32.aac` -> `http://icecast-qmusicnl-cdp.triple-it.nl/Qmusic_nl_live_32.aac?inverted`

For those who use External Tags or m-tags components, they can get the values inverted by creating a field `%STREAM_INVERTED%` with value `1`

## Building from Source

### Prerequisites

- **Visual Studio 2022** with C++ development tools
- **Platform Toolset v143** (Visual Studio 2022 Build Tools)
- **Windows 10 SDK** (10.0 or later)
- **foobar2000 SDK** (included via Columns UI SDK in `columns_ui/` directory)

### SDK Structure

This component uses the **Columns UI SDK** which includes the complete foobar2000 SDK. The SDK is located in:
```
columns_ui/
├── foobar2000/
│   └── SDK/                  # Complete foobar2000 SDK
│   └── shared/shared-x64.lib # Pre-compiled SDK library
└── ...
```

This approach provides:
- **Dual UI Support**: Both Default UI (DUI) and Columns UI (CUI) compatibility
- **Complete SDK**: All necessary foobar2000 headers and libraries
- **64-bit Ready**: Pre-compiled for x64 architecture

### Additional libraries needed

- **nlohmann/json** https://github.com/nlohmann/json


### Manual Build (Recommended)

#### Step 1: Build SDK Dependencies

First, compile the required SDK components with the correct platform toolset:

```batch
# Build foobar2000 SDK
msbuild columns_ui/foobar2000/SDK/foobar2000_SDK.vcxproj /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 /v:minimal

# Build PFC library
msbuild columns_ui/pfc/pfc.vcxproj /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 /v:minimal

# Build Columns UI SDK
msbuild columns_ui/columns_ui-sdk/columns_ui-sdk.vcxproj /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 /v:minimal
```

#### Step 2: Build Main Component

After SDK dependencies are built, compile the main component:

```batch
# Build Release version (recommended)
msbuild foo_artwork.vcxproj /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 /v:minimal

# Build Debug version
msbuild foo_artwork.vcxproj /p:Configuration=Debug /p:Platform=x64 /p:PlatformToolset=v143 /v:minimal

# Clean and rebuild
msbuild foo_artwork.vcxproj /t:Clean,Build /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 /v:minimal
```

**Command Parameters:**
- `/p:Configuration=Release` - Build optimized release version
- `/p:Platform=x64` - Target 64-bit architecture
- `/p:PlatformToolset=v143` - Use Visual Studio 2022 toolset (required for all projects)
- `/v:minimal` - Minimal build output for cleaner console

**Important:** All SDK dependencies must be compiled with the same platform toolset (`v143`) to ensure compatibility.

- ### Build Configuration

- **Target Platform**: x64 only (64-bit foobar2000)
- **Platform Toolset**: v143 (Visual Studio 2022)
- **C++ Standard**: C++17
- **Runtime Library**: Multi-threaded (Release) / Multi-threaded Debug (Debug)
- **UI Compatibility**: Both Default UI and Columns UI supported

### Output

After successful build:
- **Release DLL**: `Release/foo_artwork.dll`
- **Debug DLL**: `Debug/foo_artwork.dll`

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

## Support Development

If you find these components useful, consider supporting development:

| Platform | Payment Methods |
|----------|----------------|
| **[Ko-fi](https://ko-fi.com/Jame25)** | Cards, PayPal |
| **[Stripe](https://buy.stripe.com/3cIdR874Bg1NfRdaJf1sQ02)** | Alipay, WeChat Pay, Cards, Apple Pay, Google Pay |

Your support helps cover development time and enables new features. Thank you! 🙏

---

## 支持开发

如果您觉得这些组件有用，请考虑支持开发：

| 平台 | 支付方式 |
|------|----------|
| **[Ko-fi](https://ko-fi.com/Jame25)** | 银行卡、PayPal |
| **[Stripe](https://buy.stripe.com/dRmcN474B8zlfRd2cJ1sQ01)** | 支付宝、银行卡、Apple Pay、Google Pay |

您的支持有助于支付开发时间并实现新功能。谢谢！🙏

---

**Feature Requests:** Paid feature requests are available for supporters. [Contact me on Telegram](https://t.me/jame25) to discuss.

**功能请求：** 为支持者提供付费功能请求。[请在 Telegram 上联系我](https://t.me/jame25) 进行讨论。
