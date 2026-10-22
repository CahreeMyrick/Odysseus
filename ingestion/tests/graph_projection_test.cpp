#include "indexing.hpp"
#include "code_extractor.hpp"
#include "model_json.hpp"
#include <iostream>
#include <stdexcept>
using namespace odysseus::ingestion;
using odysseus::json;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct RecordingTransport : HttpTransport {
    std::vector<json> queries;
    HttpResponse get(const std::string&, const std::vector<std::string>&) override {
        throw std::runtime_error("Unexpected GET");
    }
    HttpResponse request(const std::string& method, const std::string&, const std::vector<std::string>&,
                         const std::string& body) override {
        check(method == "POST", "Unexpected method");
        queries.push_back(json::parse(body));
        return {200, "{}"};
    }
};
int main() {
    try {
        RecordingTransport http;
        IndexingWriter writer(http, {});
        auto model = CodeExtractor::extract("void f() { external(); } void f(int x) { external(); }");
        IndexingTask task{};
        task.id = 7; task.generation = 2; task.artifact_id = "artifact"; task.target = IndexTarget::Graph;
        task.payload = {{"source_uri", "https://example.com/repo"}, {"revision", "abc"},
                        {"path", "sample.cpp"}, {"model_type", "code"}, {"model", model}};
        int beats = 0;
        writer.write(task, [&] { ++beats; });
        check(beats == 2, "Heartbeat missing");
        std::vector<std::string> function_ids, site_ids;
        for (const auto& query : http.queries) {
            const auto statement = query.at("statement").get<std::string>();
            const auto& params = query.at("parameters");
            check(statement.find(":CALLS") == std::string::npos, "Syntax created resolved CALLS edge");
            if (params.contains("fnid")) function_ids.push_back(params.at("fnid"));
            if (params.contains("expression")) {
                site_ids.push_back(params.at("sid"));
                check(params.at("status") == "unresolved", "Resolution status lost");
                check(params.at("callee") == "external", "Call spelling lost");
            }
        }
        check(function_ids.size() == 2 && function_ids[0] != function_ids[1], "Overloads collapsed");
        check(site_ids.size() == 2 && site_ids[0] != site_ids[1], "Call sites collapsed");
        check(function_ids[0].rfind("7:2::", 0) == 0, "Projection identity missing");
        const auto first = http.queries;
        http.queries.clear();
        writer.write(task, [] {});
        check(http.queries.size() + 6 == first.size(), "Constraints recreated on retry");
        for (std::size_t i = 0; i < http.queries.size(); ++i)
            check(http.queries[i] == first[i+6], "Retry changed projection");
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
