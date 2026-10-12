#pragma once
#include <ToneMappingTuning.h>
#include <string>
#include <vector>

// The tone charts read libplacebo, which only the Config application loads
// (delay-loaded from VP Renderer's folder). This header is the seam: the page
// fills a Request without libplacebo, and the application registers the
// functions at startup. Anything that compiles the page alone sees no
// functions registered and shows the charts as unavailable.
namespace ToneCurveProvider
{
    struct Request
    {
        // Values as the profile resolves them: lower case, "auto" when unset.
        std::string quality = "high";
        std::string toneMapping = "auto";
        std::string peakDetection = "auto";
        ToneMappingTuning::Overrides toneTuning{};
        double targetNits = 203.0;
        double blackNits = 0.0;
        // The scene the charts imagine; not VP settings. Without peak detection
        // the peak is the metadata peak and there is no scene average.
        double scenePeakNits = 600.0;
        double sceneAverageNits = 15.0;
        bool sceneAverageKnown = true;
        // Source points in PQ (0 to 1).
        std::vector<double> sourcePq;
    };

    struct Result
    {
        std::vector<double> profilePq;  // the profile, as VP runs it
        std::vector<double> defaultPq;  // libplacebo's own default curve, whatever the profile
        double inputMaxPq = 0.0;        // the scene peak libplacebo uses
        double outputMaxPq = 0.0;       // the display peak libplacebo uses
        double outputMinPq = 0.0;
        std::string curve;              // the libplacebo curve in use
    };

    struct Functions
    {
        bool (*sample)(const Request&, Result&, std::wstring& error) = nullptr;
        double (*nitsToPq)(double nits) = nullptr;
        double (*pqToNits)(double pq) = nullptr;
    };

    inline Functions& Registered()
    {
        static Functions functions;
        return functions;
    }
}
