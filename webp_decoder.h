#pragma once
#include <windows.h>
#include <gdiplus.h>

// Decode WebP image data to a GDI+ Bitmap using WIC.
// Returns a new Gdiplus::Bitmap* on success, or nullptr on failure.
// Caller takes ownership of the returned bitmap.
// Fails silently on Windows versions without WIC WebP codec (pre-1809).
Gdiplus::Bitmap* decode_webp_via_wic(const unsigned char* data, size_t size);

// Check if data starts with WebP signature (RIFF....WEBP)
bool is_webp_signature(const unsigned char* data, size_t size);
