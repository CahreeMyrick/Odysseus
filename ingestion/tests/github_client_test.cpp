#include "rest_github_client.hpp"
#include "cli.hpp"
#include <deque>
#include <iostream>
#include <stdexcept>
using namespace odysseus::ingestion;
using nlohmann::json;
void check(bool condition) { if (!condition) throw std::runtime_error("GitHub test failed"); }
template<class F> void fails(F action, const std::string& message) {
    try { action(); } catch (const std::exception& e) {
        check(std::string(e.what()).find(message) != std::string::npos); return;
    }
    throw std::runtime_error("Expected failure: " + message);
}
struct FakeHttp : HttpTransport {
    std::deque<std::pair<std::string, HttpResponse>> replies;
    std::vector<std::string> last_headers;
    HttpResponse get(const std::string& url, const std::vector<std::string>& headers) override {
        check(!replies.empty());
        auto reply = replies.front(); replies.pop_front();
        check(url == "https://api.github.com" + reply.first);
        last_headers = headers;
        return reply.second;
    }
    void add(const std::string& path, const std::string& body, long status = 200) {
        replies.push_back({path, {status, body}});
    }
};
int main() {
    try {
        const std::string commit(40, 'a'), tree(40, 'b'), blob(40, 'c');
        const std::string base = "/repos/org/repo";
        const auto commit_body = json{{"sha", commit}, {"commit", {{"tree", {{"sha", tree}}}}}}.dump();
        auto http = std::make_shared<FakeHttp>();
        RestGitHubClient client(http, "test-token");
        SourceRequest request;
        request.type = SourceType::GitHub;
        request.uri = "https://github.com/org/repo.git/";
        http->add(base, R"({"default_branch":"feature/docs"})");
        http->add(base + "/commits/feature%2Fdocs", commit_body);
        const auto entries = json::array({
            {{"path", "README.md"}, {"type", "blob"}, {"mode", "100644"}, {"size", 5}, {"sha", blob}},
            {{"path", "image.png"}, {"type", "blob"}, {"mode", "100644"}},
            {{"path", "link.md"}, {"type", "blob"}, {"mode", "120000"}},
            {{"path", "submodule"}, {"type", "commit"}}});
        http->add(base + "/git/trees/" + tree + "?recursive=1",
                  json{{"truncated", false}, {"tree", entries}}.dump());
        http->add(base + "/git/blobs/" + blob, "hello");
        auto snapshot = client.fetch_repository(request);
        check(snapshot.revision == commit && snapshot.files.size() == 2);
        check(snapshot.files[0].content == "hello" && snapshot.files[1].path == "image.png");
        check(http->replies.empty());
        check(http->last_headers.front() == "Accept: application/vnd.github.raw+json");
        check(http->last_headers.back() == "Authorization: Bearer test-token");
        request.options["ref"] = "v1";
        http->add(base + "/commits/v1", commit_body);
        http->add(base + "/git/trees/" + tree + "?recursive=1", R"({"truncated":true})");
        fails([&] { client.fetch_repository(request); }, "truncated");
        for (long status : {401, 403, 404, 429, 500}) {
            http->add(base + "/commits/v1", "secret response must not be printed", status);
            fails([&] { client.fetch_repository(request); }, "GitHub HTTP " + std::to_string(status));
        }
        http->add(base + "/commits/v1", "not json");
        fails([&] { client.fetch_repository(request); }, "Invalid GitHub response");
        request.uri = "https://evil.example/org/repo";
        fails([&] { client.fetch_repository(request); }, "Expected repository URL");
        check(http->replies.empty());
        fails([&] { RestGitHubClient invalid(http, "token\r\nheader"); }, "Invalid GITHUB_TOKEN");
        char program[] = "odysseus", ingest[] = "ingest", github[] = "github";
        char uri[] = "https://github.com/org/repo", flag[] = "--ref", revision[] = "main";
        char* argv[] = {program, ingest, github, uri, flag, revision};
        const auto options = parse_cli(6, argv);
        check(!options.mock && options.source.options.at("ref") == "main");
        fails([&] { parse_cli(5, argv); }, "--ref requires");
        std::cout << "GitHub client checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
