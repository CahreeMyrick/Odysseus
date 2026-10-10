#include "rest_github_client.hpp"
#include "content_classifier.hpp"
#include <regex>
#include <stdexcept>
#include <utility>

namespace odysseus::ingestion {
namespace {
std::string encode(const std::string& value) {
    const char* hex = "0123456789ABCDEF";
    std::string result;
    for (unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') result += c;
        else { result += '%'; result += hex[c >> 4]; result += hex[c & 15]; }
    }
    return result;
}
std::string sha(const json& value) {
    const auto result = value.get<std::string>();
    if (!std::regex_match(result, std::regex("[0-9a-fA-F]{40}")))
        throw std::runtime_error("Invalid GitHub object SHA");
    return result;
}
}
RestGitHubClient::RestGitHubClient(std::shared_ptr<HttpTransport> transport, std::string token)
    : transport_(std::move(transport)), token_(std::move(token)) {
    if (!transport_) throw std::invalid_argument("HTTP transport is required");
    if (token_.find_first_of("\r\n") != std::string::npos)
        throw std::invalid_argument("Invalid GITHUB_TOKEN");
}
std::string RestGitHubClient::get(const std::string& path, bool raw) {
    std::vector<std::string> headers{
        raw ? "Accept: application/vnd.github.raw+json" : "Accept: application/vnd.github+json",
        "X-GitHub-Api-Version: 2022-11-28"};
    if (!token_.empty()) headers.push_back("Authorization: Bearer " + token_);
    const auto response = transport_->get("https://api.github.com" + path, headers);
    if (response.status != 200) {
        std::string hint;
        switch (response.status) {
            case 401: hint = "authentication failed; check GITHUB_TOKEN"; break;
            case 403: case 429: hint = "access denied or rate limited; check token permissions and retry later"; break;
            case 404: hint = "repository or revision not found, or token lacks access"; break;
            case 409: hint = "repository may be empty"; break;
            case 301: case 302: case 307: case 308: hint = "repository moved; use its current URL"; break;
            default: hint = "GitHub API request failed";
        }
        throw std::runtime_error("GitHub HTTP " + std::to_string(response.status) + ": " + hint);
    }
    return response.body;
}
GitHubSnapshot RestGitHubClient::fetch_repository(const SourceRequest& request) {
    std::smatch match;
    if (!std::regex_match(request.uri, match,
        std::regex("https://github\\.com/([A-Za-z0-9_-]+)/([A-Za-z0-9_.-]+)/?")))
        throw std::invalid_argument("Expected repository URL: https://github.com/owner/repo");
    std::string repo = match[2];
    if (repo.size() > 4 && repo.substr(repo.size() - 4) == ".git") repo.resize(repo.size() - 4);
    if (repo == "." || repo == "..") throw std::invalid_argument("Invalid repository name");
    const auto base = "/repos/" + match[1].str() + "/" + repo;
    try {
        auto ref = request.options.value("ref", std::string{});
        if (ref.empty()) ref = json::parse(get(base)).at("default_branch").get<std::string>();
        const auto commit = json::parse(get(base + "/commits/" + encode(ref)));
        GitHubSnapshot snapshot{sha(commit.at("sha")), {}};
        const auto root = sha(commit.at("commit").at("tree").at("sha"));
        const auto tree = json::parse(get(base + "/git/trees/" + root + "?recursive=1"));
        if (tree.at("truncated").get<bool>())
            throw std::runtime_error("GitHub tree listing is truncated; refusing incomplete ingestion");
        std::size_t total = 0;
        for (const auto& entry : tree.at("tree")) {
            if (entry.at("type") != "blob") continue; // Submodules are not followed.
            const auto mode = entry.at("mode").get<std::string>();
            if (mode != "100644" && mode != "100755") continue; // Do not ingest symlink targets.
            GitHubFile file{entry.at("path").get<std::string>(), {}};
            if (ContentClassifier::supports_path(file.path)) {
                if (entry.at("size").get<std::size_t>() > 8 * 1024 * 1024)
                    throw std::runtime_error("Supported GitHub file exceeds 8 MiB limit: " + file.path);
                file.content = get(base + "/git/blobs/" + sha(entry.at("sha")), true);
                if (file.content.size() > 8 * 1024 * 1024)
                    throw std::runtime_error("Supported GitHub file exceeds 8 MiB limit: " + file.path);
                total += file.content.size();
                if (total > 64 * 1024 * 1024) throw std::runtime_error("Repository text exceeds 64 MiB snapshot limit");
                // JSON's strict serializer validates UTF-8 without changing the source text.
                (void)json(file.content).dump();
            }
            // Unsupported paths remain visible to the pipeline's skip counter.
            snapshot.files.push_back(std::move(file));
        }
        return snapshot;
    } catch (const json::exception&) {
        throw std::runtime_error("Invalid GitHub response or non-UTF-8 file content");
    }
}
}
