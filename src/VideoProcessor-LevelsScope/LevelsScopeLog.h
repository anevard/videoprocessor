#pragma once
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

// What VP logged about one moment of HDR luminance: what peak detection
// measured, what the stream authored (Dolby Vision L1, or a frame's own scene
// values) and the source's declared range. Read from vp.log now; a live
// source (VP's configuration RPC) is meant to fill the same record later.
//
// The lines, and how each is used:
//   "HDR luminance:"          one sample (detection and scene values; the
//                             stats-OSD builds, every 5 s; from cacf7165
//                             every 2.5 s and on each L1 change, with
//                             millisecond time=, the L1 minimum, the
//                             uncapped detected maximum and source_frame);
//   "Dolby Vision luminance:" the DM source range for later DV samples; a
//                             sample of its own only in a log without the
//                             line above ("L1 only", older builds);
//   "LLDV state trace:"       with raw_eotf=PQ, the HDR10 static metadata for
//                             later HDR10 samples, raw_values= MaxCLL/MaxFALL/
//                             min/max. Not "libplacebo tone mapping:": it is
//                             logged only when the EOTF or colorspace changes.
// And for markers where the settings changed, from lines VP already writes:
//   "profiles: automatic selections:"           the DTM profile (display/<name>);
//   "libplacebo resolved render options": all=  every non-default render option;
//   "libplacebo settings:" / "... applied live:" the target nits and black.
namespace LevelsScopeLog
{
    struct Marker
    {
        std::string time;               // the line's first 19 characters
        std::string text;               // what changed
    };

    struct Sample
    {
        std::string time;               // "yyyy-MM-dd HH:mm:ss", then ".zzz" from time=
        bool dolbyVision = false;
        bool measured = false;          // detection reported this sample
        double detectedPeakNits = 0.0;
        double detectedPqAverageNits = 0.0;
        double detectedLinearAverageNits = 0.0;
        double detectedSceneMaxNits = 0.0;  // uncapped maxRGB; 0 if not logged
        bool authored = false;          // L1, or the frame's own scene values
        double authoredPeakNits = 0.0;
        double authoredAverageNits = 0.0;
        bool authoredMinKnown = false;  // the line has an L1 minimum (0 is true black)
        double authoredMinNits = 0.0;
        bool l1Change = false;          // logged because L1 changed
        long long sourceFrame = -1;     // -1 if not logged
        bool rangeKnown = false;        // the source's declared range
        double masteringBlackNits = 0.0;
        double masteringPeakNits = 0.0;
        double maxCllNits = 0.0;        // HDR10 only
        double maxFallNits = 0.0;
    };

    // key=value pairs of one log line; a value ends at a space or '|'.
    inline std::map<std::string, std::string> Values(const std::string& line)
    {
        std::map<std::string, std::string> values;
        const auto word = [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        };
        for (size_t equals = line.find('='); equals != std::string::npos; equals = line.find('=', equals + 1))
        {
            size_t start = equals;
            while (start > 0 && word(line[start - 1])) --start;
            size_t end = equals + 1;
            while (end < line.size() && line[end] != ' ' && line[end] != '|' &&
                line[end] != '\r' && line[end] != '\n') ++end;
            if (start < equals)
                values[line.substr(start, equals - start)] = line.substr(equals + 1, end - equals - 1);
        }
        return values;
    }

    inline double Number(const std::map<std::string, std::string>& values, const char* key)
    {
        const auto found = values.find(key);
        return found == values.end() ? 0.0 : std::strtod(found->second.c_str(), nullptr);
    }

    // A logged value as the operator would type it: 1.590000033378601 -> 1.59.
    inline std::string Short(const std::string& value)
    {
        char* end = nullptr;
        const double number = std::strtod(value.c_str(), &end);
        if (end == value.c_str() || *end != '\0') return value;
        char text[32];
        std::snprintf(text, sizeof text, "%.4g", number);
        return text;
    }

    // Up to three changes, then how many more.
    inline std::string Join(const std::vector<std::string>& changes)
    {
        std::string text;
        for (size_t i = 0; i < changes.size() && i < 3; ++i)
            text += (i ? "; " : "") + changes[i];
        if (changes.size() > 3)
            text += " (+" + std::to_string(changes.size() - 3) + " more)";
        return text;
    }

    // One file's lines in order. Samples come from "HDR luminance:" lines when
    // the log has any, else from "Dolby Vision luminance:" lines.
    class Reader
    {
    public:
        void Line(const std::string& line)
        {
            if (build_.empty())
                FindBuild(line);
            if (line.find("| profiles: automatic selections:") != std::string::npos)
            {
                const size_t at = line.find("display/");
                if (at == std::string::npos) return;
                size_t end = at + 8;
                while (end < line.size() && line[end] != ' ' && line[end] != ',') ++end;
                const std::string profile = line.substr(at + 8, end - at - 8);
                if (profile != profile_)
                    markers_.push_back({ line.substr(0, 19), "DTM profile " + profile });
                profile_ = profile;
            }
            else if (line.find("| libplacebo resolved render options") != std::string::npos)
            {
                const size_t at = line.find(" all=");
                if (at == std::string::npos) return;
                std::map<std::string, std::string> options;
                const std::string all = line.substr(at + 5);
                for (size_t start = 0; start < all.size();)
                {
                    size_t end = all.find(',', start);
                    if (end == std::string::npos) end = all.size();
                    const std::string item = all.substr(start, end - start);
                    const size_t equals = item.find('=');
                    if (equals != std::string::npos)
                        options[item.substr(0, equals)] = item.substr(equals + 1);
                    start = end + 1;
                }
                if (!options_.empty() && options != options_)
                {
                    std::vector<std::string> changes;
                    for (const auto& [key, value] : options)
                    {
                        const auto old = options_.find(key);
                        if (old == options_.end()) changes.push_back(key + " " + Short(value));
                        else if (old->second != value) changes.push_back(key + " " + Short(old->second) + " → " + Short(value));
                    }
                    for (const auto& [key, value] : options_)
                        if (!options.count(key)) changes.push_back(key + " off");
                    markers_.push_back({ line.substr(0, 19), Join(changes) });
                }
                options_ = options;
            }
            else if (line.find("| libplacebo settings:") != std::string::npos ||
                line.find("| libplacebo profile settings applied live:") != std::string::npos)
            {
                const auto values = Values(line);
                // The live line's target= is the gamut (P3-D65); its nits are luminance=.
                const bool live = line.find("applied live:") != std::string::npos;
                const auto target = values.find(live ? "luminance" : "target");
                const auto black = values.find("black");
                if (target == values.end() || black == values.end()) return;
                const double nits = std::strtod(target->second.c_str(), nullptr);
                const double blackNits = std::strtod(black->second.c_str(), nullptr);
                if (targetKnown_ && (nits != targetNits_ || blackNits != targetBlack_))
                    markers_.push_back({ line.substr(0, 19), "target " + Short(target->second) + " nits, black " + Short(black->second) });
                targetKnown_ = true;
                targetNits_ = nits;
                targetBlack_ = blackNits;
            }
            else if (line.find("| Dolby Vision luminance:") != std::string::npos)
            {
                const auto values = Values(line);
                dmRange_ = true;
                dmBlack_ = Number(values, "source_min_nits");
                dmPeak_ = Number(values, "source_max_nits");
                Sample sample;
                sample.time = line.substr(0, 19);
                sample.dolbyVision = true;
                sample.authored = values.count("scene_present") && values.at("scene_present") == "1";
                sample.authoredPeakNits = sample.authored ? Number(values, "scene_peak_nits") : 0.0;
                sample.authoredAverageNits = sample.authored ? Number(values, "scene_average_nits") : 0.0;
                ApplyRange(sample);
                dvOnly_.push_back(sample);
            }
            else if (line.find("| LLDV state trace:") != std::string::npos &&
                line.find("raw_eotf=PQ") != std::string::npos)
            {
                const auto values = Values(line);
                const auto found = values.find("raw_values");
                if (found == values.end()) return;
                std::vector<double> raw;
                for (size_t start = 0; start <= found->second.size();)
                {
                    const size_t slash = found->second.find('/', start);
                    const size_t end = slash == std::string::npos ? found->second.size() : slash;
                    raw.push_back(std::strtod(found->second.substr(start, end - start).c_str(), nullptr));
                    start = end + 1;
                }
                if (raw.size() == 4 && raw[0] != -1.0)
                {
                    staticKnown_ = true;
                    maxCll_ = raw[0]; maxFall_ = raw[1]; staticBlack_ = raw[2]; staticPeak_ = raw[3];
                }
            }
            else if (line.find("| HDR luminance:") != std::string::npos)
            {
                const auto values = Values(line);
                Sample sample;
                sample.time = line.substr(0, 19);
                // time=YYYY-MM-DDTHH:MM:SS.mmm, local like the prefix.
                const auto stamp = values.find("time");
                if (stamp != values.end() && stamp->second.size() == 23 && stamp->second[10] == 'T')
                    sample.time = stamp->second.substr(0, 10) + ' ' + stamp->second.substr(11);
                sample.dolbyVision = values.count("dv") && values.at("dv") == "1";
                sample.measured = values.count("detection") && values.at("detection") == "valid";
                if (sample.measured)
                {
                    sample.detectedPeakNits = Number(values, "detected_peak_nits");
                    sample.detectedPqAverageNits = Number(values, "detected_avg_pq_nits");
                    sample.detectedLinearAverageNits = Number(values, "detected_avg_linear_nits");
                    sample.detectedSceneMaxNits = Number(values, "detected_scene_max_nits");
                }
                const auto scene = values.find("scene");
                sample.authored = scene != values.end() && scene->second != "none";
                if (sample.authored)
                {
                    sample.authoredPeakNits = Number(values, "scene_peak_nits");
                    sample.authoredAverageNits = Number(values, "scene_avg_nits");
                    // Only L1 carries a minimum; elsewhere VP logs 0 for "none".
                    sample.authoredMinKnown = sample.dolbyVision && scene->second == "L1" &&
                        values.count("scene_min_nits");
                    if (sample.authoredMinKnown)
                        sample.authoredMinNits = Number(values, "scene_min_nits");
                }
                // reason=interval, L1 or interval,L1.
                const auto reason = values.find("reason");
                sample.l1Change = reason != values.end() && reason->second.find("L1") != std::string::npos;
                const auto frame = values.find("source_frame");
                if (frame != values.end())
                    sample.sourceFrame = std::strtoll(frame->second.c_str(), nullptr, 10);
                ApplyRange(sample);
                measuredLog_.push_back(sample);
            }
        }

        // True when the log had "HDR luminance:" lines (detection and L1).
        bool Measured() const { return !measuredLog_.empty(); }
        const std::vector<Sample>& Samples() const { return Measured() ? measuredLog_ : dvOnly_; }
        // The build named in the log, as "git-<sha>" or "bench-<sha>"; empty if none.
        const std::string& Build() const { return build_; }
        // Where the DTM profile, a render option or the target changed, in order.
        const std::vector<Marker>& Markers() const { return markers_; }

    private:
        void ApplyRange(Sample& sample) const
        {
            if (sample.dolbyVision && dmRange_)
            {
                sample.rangeKnown = true;
                sample.masteringBlackNits = dmBlack_;
                sample.masteringPeakNits = dmPeak_;
            }
            else if (!sample.dolbyVision && staticKnown_)
            {
                sample.rangeKnown = true;
                sample.masteringBlackNits = staticBlack_;
                sample.masteringPeakNits = staticPeak_;
                sample.maxCllNits = maxCll_;
                sample.maxFallNits = maxFall_;
            }
        }

        void FindBuild(const std::string& line)
        {
            for (const char* prefix : { "git-", "bench-" })
            {
                for (size_t at = line.find(prefix); at != std::string::npos; at = line.find(prefix, at + 1))
                {
                    if (at > 0 && std::isalnum(static_cast<unsigned char>(line[at - 1]))) continue;
                    size_t end = at + std::char_traits<char>::length(prefix);
                    while (end < line.size() && std::isxdigit(static_cast<unsigned char>(line[end])) &&
                        !std::isupper(static_cast<unsigned char>(line[end]))) ++end;
                    if (end - at - std::char_traits<char>::length(prefix) >= 7)
                    {
                        build_ = line.substr(at, end - at);
                        return;
                    }
                }
            }
        }

        std::vector<Sample> measuredLog_, dvOnly_;
        std::vector<Marker> markers_;
        std::string build_, profile_;
        std::map<std::string, std::string> options_;
        bool targetKnown_ = false;
        double targetNits_ = 0.0, targetBlack_ = 0.0;
        bool dmRange_ = false, staticKnown_ = false;
        double dmBlack_ = 0.0, dmPeak_ = 0.0;
        double staticBlack_ = 0.0, staticPeak_ = 0.0, maxCll_ = 0.0, maxFall_ = 0.0;
    };
}
