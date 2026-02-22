#include "messages.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct Params
{
    double mean{};
    double stddev{};
};

struct Station
{
    int id{};
    GasType type{};
    Params handle{};
};

struct Settings
{
    std::vector<Station> stations_params{};
    Params create{};
    int number_message_queue{};
};

// -------------------- small helpers --------------------

static inline std::string Trim(std::string s)
{
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

static inline std::string StripInlineComment(std::string s)
{
    // comments start with '#' or ';' (first occurrence)
    auto pos1 = s.find('#');
    auto pos2 = s.find(';');
    auto pos = std::min(pos1 == std::string::npos ? s.size() : pos1,
                        pos2 == std::string::npos ? s.size() : pos2);
    s.resize(pos);
    return s;
}

static inline bool StartsWith(const std::string& s, const char* prefix)
{
    const size_t n = std::char_traits<char>::length(prefix);
    return s.size() >= n && std::equal(prefix, prefix + n, s.begin());
}

static inline bool ParseInt(const std::string& s, int& out)
{
    std::string t = Trim(s);
    if (t.empty()) return false;
    const char* b = t.data();
    const char* e = t.data() + t.size();
    auto [ptr, ec] = std::from_chars(b, e, out);
    return ec == std::errc{} && ptr == e;
}

static inline bool ParseDouble(const std::string& s, double& out)
{
    // from_chars(double) is not fully supported everywhere; use stod safely.
    try {
        std::string t = Trim(s);
        size_t idx = 0;
        out = std::stod(t, &idx);
        return idx == t.size();
    } catch (...) {
        return false;
    }
}

static inline bool SplitKeyValue(const std::string& line, std::string& key, std::string& value)
{
    auto eq = line.find('=');
    if (eq == std::string::npos) return false;
    key = Trim(line.substr(0, eq));
    value = Trim(line.substr(eq + 1));
    return !key.empty();
}

// -------------------- parser & loader --------------------

inline std::optional<Settings> GetSettings(const std::string& path)
{
    std::ifstream in(path);
    if (!in) return std::nullopt;

    Settings settings{};

    std::unordered_map<int, Station> stations;

    enum class SectionKind { Root, Create, Station };
    SectionKind section = SectionKind::Root;
    int current_station_id = -1;

    std::string raw;
    int line_no = 0;

    auto require_station = [&](int id) -> Station& {
        auto it = stations.find(id);
        if (it == stations.end()) {
            Station st{};
            st.id = id;
            it = stations.emplace(id, st).first;
        }
        return it->second;
    };

    while (std::getline(in, raw))
    {
        ++line_no;

        std::string line = Trim(StripInlineComment(raw));
        if (line.empty()) continue;


        if (line.size() >= 2 && line.front() == '[' && line.back() == ']')
        {
            std::string name = Trim(line.substr(1, line.size() - 2));

            if (name == "create") {
                section = SectionKind::Create;
                current_station_id = -1;
                continue;
            }

            // [station 123]
            if (StartsWith(name, "station")) {
                auto rest = Trim(name.substr(std::string("station").size()));
                int id = 0;
                if (!ParseInt(rest, id) || id <= 0) return std::nullopt;
                section = SectionKind::Station;
                current_station_id = id;
                (void)require_station(id);
                continue;
            }

            return std::nullopt;
        }


        std::string key, value;
        if (!SplitKeyValue(line, key, value)) return std::nullopt;

        auto assign_params = [&](Params& p, const std::string& k, const std::string& v) -> bool {
            if (k == "mean")   return ParseDouble(v, p.mean);
            if (k == "stddev") return ParseDouble(v, p.stddev);
            return false;
        };

        if (section == SectionKind::Root)
        {
            if (key == "number_message_queue") {
                int v = 0;
                if (!ParseInt(value, v)) return std::nullopt;
                settings.number_message_queue = v;
                continue;
            }
            return std::nullopt;
        }

        if (section == SectionKind::Create)
        {
            if (assign_params(settings.create, key, value)) continue;
            return std::nullopt;
        }

        if (section == SectionKind::Station)
        {
            if (current_station_id <= 0) return std::nullopt;
            Station& st = require_station(current_station_id);

            if (key == "type") {
                int t = 0;
                if (!ParseInt(value, t)) return std::nullopt;
                st.type = static_cast<GasType>(t);
                continue;
            }
            if (key == "handle.mean") {
                if (!ParseDouble(value, st.handle.mean)) return std::nullopt;
                continue;
            }
            if (key == "handle.stddev") {
                if (!ParseDouble(value, st.handle.stddev)) return std::nullopt;
                continue;
            }

            return std::nullopt;
        }
    }

    settings.stations_params.clear();
    settings.stations_params.reserve(stations.size());
    for (auto& kv : stations) settings.stations_params.push_back(std::move(kv.second));
    std::sort(settings.stations_params.begin(), settings.stations_params.end(),
              [](const Station& a, const Station& b){ return a.id < b.id; });

    if (settings.number_message_queue <= 0) return std::nullopt;
    if (settings.stations_params.empty()) return std::nullopt;

    return settings;
}
