#pragma once
#include <string>
#include <stdexcept>
#include <vector>
namespace odysseus::ingestion {
struct HttpResponse { long status; std::string body; };
class HttpTransport {
public:
    virtual HttpResponse get(const std::string& url, const std::vector<std::string>& headers) = 0;
    virtual HttpResponse request(const std::string&, const std::string&,
                                 const std::vector<std::string>&, const std::string&) {
        throw std::runtime_error("HTTP method not implemented by transport");
    }
    virtual ~HttpTransport() = default;
};
class CurlTransport final : public HttpTransport {
public:
    HttpResponse get(const std::string& url, const std::vector<std::string>& headers) override;
    HttpResponse request(const std::string& method, const std::string& url,
                         const std::vector<std::string>& headers, const std::string& body) override;
};
}
