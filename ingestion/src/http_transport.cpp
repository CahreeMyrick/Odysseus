#include "http_transport.hpp"
#include <curl/curl.h>
#include <memory>
#include <stdexcept>
namespace odysseus::ingestion {
namespace {
size_t receive(char* data, size_t size, size_t count, void* context) noexcept {
    auto& body = *static_cast<std::string*>(context);
    const auto bytes = size * count;
    if (bytes > 16 * 1024 * 1024 - body.size()) return 0;
    try { body.append(data, bytes); } catch (...) { return 0; }
    return bytes;
}
}
HttpResponse CurlTransport::get(const std::string& url, const std::vector<std::string>& headers) {
    return request("GET", url, headers, "");
}
HttpResponse CurlTransport::request(const std::string& method, const std::string& url,
                                    const std::vector<std::string>& headers, const std::string& body) {
    static const auto initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (initialized != CURLE_OK) throw std::runtime_error("Cannot initialize HTTP transport");
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle(curl_easy_init(), curl_easy_cleanup);
    if (!handle) throw std::runtime_error("Cannot create HTTP request");
    curl_slist* list = nullptr;
    for (const auto& header : headers) {
        auto* next = curl_slist_append(list, header.c_str());
        if (!next) { curl_slist_free_all(list); throw std::bad_alloc(); }
        list = next;
    }
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> owned(list, curl_slist_free_all);
    HttpResponse response{0, {}};
    curl_easy_setopt(handle.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle.get(), CURLOPT_HTTPHEADER, list);
    curl_easy_setopt(handle.get(), CURLOPT_CUSTOMREQUEST, method.c_str());
    if (method != "GET") {
        curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
    }
    curl_easy_setopt(handle.get(), CURLOPT_USERAGENT, "odysseus-ingestion");
    curl_easy_setopt(handle.get(), CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(handle.get(), CURLOPT_NOSIGNAL, 1L);
    // Do not forward credentials through repository redirects.
    curl_easy_setopt(handle.get(), CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, receive);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &response.body);
    const auto status = curl_easy_perform(handle.get());
    if (status != CURLE_OK) throw std::runtime_error(std::string("HTTP request failed: ") + curl_easy_strerror(status));
    curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &response.status);
    return response;
}
}
