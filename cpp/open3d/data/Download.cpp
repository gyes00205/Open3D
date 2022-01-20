// ----------------------------------------------------------------------------
// -                        Open3D: www.open3d.org                            -
// ----------------------------------------------------------------------------
// The MIT License (MIT)
//
// Copyright (c) 2018-2021 www.open3d.org
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
// ----------------------------------------------------------------------------

#include "open3d/data/Download.h"

// clang-format off
// Must include openssl before curl to build on Windows.
#include <openssl/sha.h>

// https://stackoverflow.com/a/41873190/1255535
#ifdef WINDOWS
#pragma comment(lib, "wldap32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "Ws2_32.lib")
#define USE_SSLEAY
#define USE_OPENSSL
#endif

#define CURL_STATICLIB

#include <curl/curl.h>
#include <curl/easy.h>
// clang-format on

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "open3d/data/Dataset.h"
#include "open3d/utility/FileSystem.h"
#include "open3d/utility/Logging.h"

namespace open3d {
namespace data {

// https://gist.github.com/arrieta/7d2e196c40514d8b5e9031f2535064fc
// author    J. Arrieta <Juan.Arrieta@nablazerolabs.com>
// copyright (c) 2017 Nabla Zero Labs
// license   MIT License
std::string GetSHA256(const std::string& file_path) {
    if (!utility::filesystem::FileExists(file_path)) {
        utility::LogError("{} does not exist.", file_path);
    }

    std::ifstream fp(file_path.c_str(), std::ios::in | std::ios::binary);

    if (!fp.good()) {
        std::ostringstream os;
        utility::LogError("Cannot open {}", file_path);
    }

    constexpr const std::size_t buffer_size{1 << 12};  // 4 KiB
    char buffer[buffer_size];
    unsigned char hash[SHA256_DIGEST_LENGTH] = {0};

    SHA256_CTX ctx;
    SHA256_Init(&ctx);

    while (fp.good()) {
        fp.read(buffer, buffer_size);
        SHA256_Update(&ctx, buffer, fp.gcount());
    }

    SHA256_Final(hash, &ctx);
    fp.close();

    std::ostringstream os;
    os << std::hex << std::setfill('0');

    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        os << std::setw(2) << static_cast<unsigned int>(hash[i]);
    }

    return os.str();
}

static size_t WriteDataCb(void* ptr, size_t size, size_t nmemb, FILE* stream) {
    size_t written;
    written = fwrite(ptr, size, nmemb, stream);
    return written;
}

void DownloadFromURL(const std::string& url,
                     const std::string& sha256,
                     const std::string& prefix,
                     const std::string& data_root) {
    // Always print URL to inform the user. If the download fails, the user
    // knows the URL.
    utility::LogInfo("Downloading {}", url);

    // Sanity checks.
    if (sha256.size() != SHA256_DIGEST_LENGTH * 2) {
        utility::LogError("Invalid sha256 length {}, expected to be {}.",
                          sha256.size(), SHA256_DIGEST_LENGTH * 2);
    }
    if (prefix.empty()) {
        utility::LogError("Download prefix cannot be empty.");
    }

    // Resolve path.
    const std::string resolved_data_root =
            data_root.empty() ? LocateDataRoot() : data_root;
    const std::string file_dir = resolved_data_root + "/" + prefix;
    const std::string file_name =
            utility::filesystem::GetFileNameWithoutDirectory(url);
    const std::string file_path = file_dir + "/" + file_name;
    if (!utility::filesystem::DirectoryExists(file_dir)) {
        utility::filesystem::MakeDirectoryHierarchy(file_dir);
    }

    // Check if the file exists.
    if (utility::filesystem::FileExists(file_path) &&
        GetSHA256(file_path) == sha256) {
        utility::LogInfo("{} exists and SHA256 matches. Skipped downloading.",
                         file_path);
        return;
    }

    // Download.
    CURL* curl;
    FILE* fp;
    CURLcode res;
    curl = curl_easy_init();
    if (!curl) {
        utility::LogError("Failed to initialize CURL.");
    }
    fp = fopen(file_path.c_str(), "wb");
    if (!fp) {
        utility::LogError("Failed to open file {}.", file_path);
    }
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);  // -L redirection.
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, false);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, false);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteDataCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    fclose(fp);

    if (res == CURLE_OK) {
        const std::string actual_sha256 = GetSHA256(file_path);
        if (actual_sha256 == sha256) {
            utility::LogInfo("Downloaded to {}", file_path);
        } else {
            utility::LogError(
                    "SHA256 mismatch for {}.\n- Expected: {}\n- Actual  : {}",
                    file_path, sha256, actual_sha256);
        }
    } else {
        utility::LogError("Download failed with error code: {}.",
                          curl_easy_strerror(res));
    }
}

}  // namespace data
}  // namespace open3d
