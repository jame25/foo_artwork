#include "webp_decoder.h"
#include <vector>
#include <wincodec.h>
#include <shlwapi.h>

#pragma comment(lib, "windowscodecs.lib")

bool is_webp_signature(const unsigned char* data, size_t size) {
    if (size < 12) return false;
    return data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' &&
           data[8] == 'W' && data[9] == 'E' && data[10] == 'B' && data[11] == 'P';
}

Gdiplus::Bitmap* decode_webp_via_wic(const unsigned char* data, size_t size) {
    if (!data || size < 12 || !is_webp_signature(data, size)) return nullptr;

    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    IStream* stream = nullptr;
    Gdiplus::Bitmap* result = nullptr;

    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IWICImagingFactory, (void**)&factory);
    if (FAILED(hr) || !factory) return nullptr;

    // Create IStream from memory
    stream = SHCreateMemStream(data, (UINT)size);
    if (!stream) {
        factory->Release();
        return nullptr;
    }

    // Create decoder from stream — this is where it fails on pre-1809 Windows (no WebP codec)
    hr = factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr) || !decoder) {
        stream->Release();
        factory->Release();
        return nullptr;
    }

    // Get first frame
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr) || !frame) {
        decoder->Release();
        stream->Release();
        factory->Release();
        return nullptr;
    }

    // Convert to 32bpp BGRA
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr) || !converter) {
        frame->Release();
        decoder->Release();
        stream->Release();
        factory->Release();
        return nullptr;
    }

    hr = converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA,
                                WICBitmapDitherTypeNone, nullptr, 0.0,
                                WICBitmapPaletteTypeMedianCut);
    if (FAILED(hr)) {
        converter->Release();
        frame->Release();
        decoder->Release();
        stream->Release();
        factory->Release();
        return nullptr;
    }

    // Get dimensions
    UINT width = 0, height = 0;
    hr = converter->GetSize(&width, &height);
    if (FAILED(hr) || width == 0 || height == 0 || width > 16384 || height > 16384) {
        converter->Release();
        frame->Release();
        decoder->Release();
        stream->Release();
        factory->Release();
        return nullptr;
    }

    // Create GDI+ Bitmap and copy pixels
    try {
        result = new Gdiplus::Bitmap(width, height, PixelFormat32bppRGB);
    } catch (...) {
        result = nullptr;
    }
    if (!result || result->GetLastStatus() != Gdiplus::Ok) {
        delete result;
        converter->Release();
        frame->Release();
        decoder->Release();
        stream->Release();
        factory->Release();
        return nullptr;
    }

    Gdiplus::BitmapData bmpData;
    Gdiplus::Rect lockRect(0, 0, width, height);
    if (result->LockBits(&lockRect, Gdiplus::ImageLockModeWrite, PixelFormat32bppRGB, &bmpData) == Gdiplus::Ok) {
        // WIC outputs BGRA, GDI+ PixelFormat32bppRGB is also BGRA in memory
        UINT stride = bmpData.Stride;
        UINT wicStride = width * 4;

        if ((UINT)stride == wicStride) {
            // Strides match — single copy
            hr = converter->CopyPixels(nullptr, wicStride, wicStride * height, (BYTE*)bmpData.Scan0);
        } else {
            // Strides differ — copy row by row
            std::vector<BYTE> buffer(wicStride * height);
            hr = converter->CopyPixels(nullptr, wicStride, wicStride * height, buffer.data());
            if (SUCCEEDED(hr)) {
                for (UINT y = 0; y < height; y++) {
                    memcpy((BYTE*)bmpData.Scan0 + y * stride, buffer.data() + y * wicStride, wicStride);
                }
            }
        }
        result->UnlockBits(&bmpData);

        if (FAILED(hr)) {
            delete result;
            result = nullptr;
        }
    } else {
        delete result;
        result = nullptr;
    }

    converter->Release();
    frame->Release();
    decoder->Release();
    stream->Release();
    factory->Release();

    return result;
}
