#pragma once
#include "stdafx.h"
#include <string>
#include <vector>

class ACRCloudClient {
public:
    struct RecognitionResult {
        bool success = false;
        std::string artist;
        std::string title;
        std::string album;
        uint32_t duration_ms = 0;
        uint32_t play_offset_ms = 0;
        std::string error_message;
    };

    // Computes HMAC-SHA1 signature formatted for ACRCloud REST API authentication
    static std::string generate_signature(
        const std::string& method,
        const std::string& uri,
        const std::string& access_key,
        const std::string& access_secret,
        const std::string& data_type,
        const std::string& signature_version,
        long timestamp);

    // Parses JSON response from ACRCloud identify API
    static RecognitionResult parse_response_json(const std::string& json_response);

    // Generates a compact ACRCloud acoustic fingerprint locally from PCM audio samples
    static bool create_fingerprint(
        const int16_t* pcm_samples,
        size_t sample_count,
        int sample_rate,
        std::vector<uint8_t>& out_fp);

    // Queries ACRCloud identify REST API with a compact local fingerprint (Option B: data_type = "fingerprint")
    static RecognitionResult recognize_fingerprint(
        const char* host,
        const char* access_key,
        const char* access_secret,
        const uint8_t* fp_data,
        size_t fp_size);

    // Queries ACRCloud identify REST API with raw audio / WAV snippet data (Option A fallback)
    static RecognitionResult recognize_audio(
        const char* host,
        const char* access_key,
        const char* access_secret,
        const uint8_t* audio_data,
        size_t audio_size);
};
