#pragma once
#include <string>
#include <vector>
#include <unordered_set>
#include <queue>
#include <mutex>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <thread>
#include <functional>
#include "httplib.h"
#include "logger.h"

// Minimal HTML text + link extractor
struct CrawlResult {
    std::string url;
    std::string text;
    std::vector<std::string> links;
};

struct CrawlerConfig {
    int maxPages      = 50;
    int maxDepth      = 3;
    int delayMs       = 500;
    std::string outputDir = "docs/crawled";
};

class Crawler {
public:
    explicit Crawler(CrawlerConfig cfg = CrawlerConfig{}) : cfg(cfg) {}

    // Crawl starting from seedUrl, save text files to cfg.outputDir
    // Returns number of pages crawled
    int crawl(const std::string &seedUrl,
              std::function<void(const std::string&)> onPage = nullptr)
    {
        namespace fs = std::filesystem;
        // ensure output dir exists
        try { fs::create_directories(cfg.outputDir); } catch(...) {}

        visited.clear();
        std::queue<std::pair<std::string,int>> frontier; // {url, depth}
        frontier.push({seedUrl, 0});

        int crawled = 0;
        while (!frontier.empty() && crawled < cfg.maxPages) {
            auto [url, depth] = frontier.front();
            frontier.pop();

            if (visited.count(url)) continue;
            visited.insert(url);

            if (depth > cfg.maxDepth) continue;

            // Parse host + path
            std::string scheme, host, path;
            if (!parseUrl(url, scheme, host, path)) continue;

            // Check robots.txt (cached per host)
            if (!isAllowed(scheme, host, path)) {
                Logger::instance().log(LINFO, "Crawler: blocked by robots.txt: " + url);
                continue;
            }

            auto result = fetch(scheme, host, path);
            if (result.text.empty()) continue;

            // Save to file
            std::string filename = cfg.outputDir + "/" + sanitizeFilename(url) + ".txt";
            {
                std::ofstream ofs(filename);
                ofs << "URL: " << url << "\n\n" << result.text;
            }

            Logger::instance().log(LINFO, "Crawler: saved " + url + " -> " + filename);
            crawled++;

            if (onPage) onPage(filename);

            // Enqueue discovered links
            if (depth < cfg.maxDepth) {
                for (auto &link : result.links) {
                    std::string absLink = resolveUrl(scheme, host, link);
                    if (!absLink.empty() && !visited.count(absLink))
                        frontier.push({absLink, depth + 1});
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(cfg.delayMs));
        }
        return crawled;
    }

private:
    CrawlerConfig cfg;
    std::unordered_set<std::string> visited;
    std::unordered_map<std::string, std::vector<std::string>> robotsCache;

    // ---- URL parsing ----
    static bool parseUrl(const std::string &url,
                         std::string &scheme, std::string &host, std::string &path)
    {
        size_t s = url.find("://");
        if (s == std::string::npos) return false;
        scheme = url.substr(0, s);
        std::string rest = url.substr(s + 3);
        size_t slash = rest.find('/');
        if (slash == std::string::npos) { host = rest; path = "/"; }
        else { host = rest.substr(0, slash); path = rest.substr(slash); }
        return !host.empty();
    }

    static std::string resolveUrl(const std::string &scheme,
                                  const std::string &host,
                                  const std::string &link)
    {
        if (link.empty() || link[0] == '#') return "";
        if (link.substr(0,4) == "http") return link;
        if (link[0] == '/') return scheme + "://" + host + link;
        return ""; // relative paths without base — skip
    }

    static std::string sanitizeFilename(const std::string &url) {
        std::string s = url;
        for (char &c : s)
            if (c == '/' || c == ':' || c == '?' || c == '&' || c == '=' || c == '#')
                c = '_';
        if (s.size() > 120) s = s.substr(0, 120);
        return s;
    }

    // ---- robots.txt ----
    bool isAllowed(const std::string &scheme,
                   const std::string &host,
                   const std::string &path)
    {
        if (!robotsCache.count(host)) fetchRobots(scheme, host);
        for (auto &disallowed : robotsCache[host])
            if (!disallowed.empty() && path.substr(0, disallowed.size()) == disallowed)
                return false;
        return true;
    }

    void fetchRobots(const std::string &scheme, const std::string &host) {
        robotsCache[host] = {}; // default: allow all
        auto result = fetch(scheme, host, "/robots.txt");
        std::istringstream ss(result.text);
        std::string line;
        bool ourAgent = false;
        while (std::getline(ss, line)) {
            if (line.empty() || line[0] == '#') continue;
            if (line.substr(0, 10) == "User-agent") {
                std::string agent = line.substr(line.find(':') + 1);
                agent.erase(0, agent.find_first_not_of(" \t"));
                ourAgent = (agent == "*" || agent == "CppSearchBot");
            }
            if (ourAgent && line.substr(0, 8) == "Disallow") {
                std::string p = line.substr(line.find(':') + 1);
                p.erase(0, p.find_first_not_of(" \t"));
                if (!p.empty()) robotsCache[host].push_back(p);
            }
        }
    }

    // ---- HTTP fetch ----
    CrawlResult fetch(const std::string &scheme,
                      const std::string &host,
                      const std::string &path)
    {
        CrawlResult res;
        res.url = scheme + "://" + host + path;
        try {
            httplib::Client cli(scheme + "://" + host);
            cli.set_connection_timeout(5);
            cli.set_read_timeout(10);
            cli.set_follow_location(true);
            auto r = cli.Get(path.c_str(),
                {{"User-Agent", "CppSearchBot/1.0"},
                 {"Accept", "text/html"}});
            if (!r || r->status != 200) return res;
            res.text  = extractText(r->body);
            res.links = extractLinks(r->body);
        } catch (...) {}
        return res;
    }

    // ---- HTML stripping ----
    static std::string extractText(const std::string &html) {
        std::string out;
        out.reserve(html.size() / 2);
        bool inTag = false;
        bool inScript = false;
        for (size_t i = 0; i < html.size(); ++i) {
            if (!inTag && i + 7 < html.size()) {
                std::string low = html.substr(i, 7);
                std::transform(low.begin(), low.end(), low.begin(), ::tolower);
                if (low.substr(0,7) == "<script" || low.substr(0,6) == "<style")
                    inScript = true;
            }
            if (html[i] == '<') { inTag = true; continue; }
            if (html[i] == '>') {
                inTag = false;
                if (inScript) {
                    // check for </script> or </style>
                    std::string tag = html.substr(i > 8 ? i-8 : 0, 9);
                    std::transform(tag.begin(), tag.end(), tag.begin(), ::tolower);
                    if (tag.find("/script") != std::string::npos ||
                        tag.find("/style")  != std::string::npos)
                        inScript = false;
                }
                out += ' ';
                continue;
            }
            if (!inTag && !inScript) out += html[i];
        }
        // collapse whitespace
        std::string clean;
        bool lastSpace = false;
        for (char c : out) {
            if (c == '\n' || c == '\r' || c == '\t') c = ' ';
            if (c == ' ') { if (!lastSpace) clean += c; lastSpace = true; }
            else { clean += c; lastSpace = false; }
        }
        return clean;
    }

    static std::vector<std::string> extractLinks(const std::string &html) {
        std::vector<std::string> links;
        size_t pos = 0;
        while ((pos = html.find("href=\"", pos)) != std::string::npos) {
            pos += 6;
            size_t end = html.find('"', pos);
            if (end == std::string::npos) break;
            std::string link = html.substr(pos, end - pos);
            if (!link.empty() && link[0] != '#' && link.substr(0,7) != "mailto:")
                links.push_back(link);
            pos = end + 1;
        }
        return links;
    }
};
