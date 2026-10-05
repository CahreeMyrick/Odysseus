#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include "../schema.hpp"

using json = nlohmann::json;

static std::string base64Decode(const std::string& in) {
    static const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string out;
    uint32_t val = 0;
    int bits = -8;

    for (unsigned char c : in) {
        if (c == '\n' || c == '\r') continue;
        if (c == '=') break;

        size_t pos = chars.find(c);
        if (pos == std::string::npos) break;

        val = ((val << 6) | pos) & 0xFFFFFF;
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<char>((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return out;
}

static size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userp) {
    static_cast<std::string*>(userp)->append(ptr, size * nmemb);
    return size * nmemb;
}

std::string getGithubFile(const std::string& owner, const std::string& repo,
                          const std::string& filePath, const std::string& token = "") {
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("Failed to initialize cURL");

    std::string url = "https://api.github.com/repos/" + owner + "/" + repo + "/contents/" + filePath;

    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "User-Agent: C++ HttpClient");
    headers = curl_slist_append(headers, "Accept: application/vnd.github.v3+json");
    if (!token.empty())
        headers = curl_slist_append(headers, ("Authorization: Bearer " + token).c_str());

    std::string body;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);

    CURLcode res = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
        throw std::runtime_error(curl_easy_strerror(res));
    if (status != 200)
        throw std::runtime_error("HTTP " + std::to_string(status));

    return base64Decode(json::parse(body).at("content").get<std::string>());
}

