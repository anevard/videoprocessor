#pragma once
#include "LevelsScopeLog.h"
#include <ToneCurveProvider.h>
#include <cmath>
#include <string>
#include <vector>

// One logged sample through the Config page's tone engine (ToneCurveModel,
// libplacebo's own functions in VP Renderer (Beta)'s libplacebo-377.dll):
// where a profile would have put each probe level at that moment. Nothing
// here computes a curve; it only fills the request as VP's renderer fills
// libplacebo's source metadata.
namespace LevelsScopeEngine
{
    struct Outcome
    {
        ToneCurveProvider::Result result; // PQ values, as the charts get them
        std::vector<double> profileNits;  // each probe after the profile
        std::vector<double> defaultNits;  // each probe after libplacebo's default
    };

    // A Dolby Vision value is a 12-bit PQ code; the log prints it rounded in
    // nits, so the nearest code is the exact value. Below ~0.01 nits the
    // printed digits are coarser than a code, so the DM black is left as printed.
    inline double DolbyVisionCode(const ToneCurveProvider::Functions& f, double nits)
    {
        return std::round(f.nitsToPq(nits) * 4095.0) / 4095.0;
    }

    // As VP hands the source to libplacebo: TranslateColorSpace (HDR10: the
    // InfoFrame's range and MaxCLL/MaxFALL) or DolbyVisionLuminancePolicy
    // (the DM range, no MaxCLL/MaxFALL, L1 as max_pq_y/avg_pq_y).
    inline ToneCurveProvider::Request RequestFor(const ToneCurveProvider::Request& profile,
        const LevelsScopeLog::Sample& sample, const ToneCurveProvider::Functions& f)
    {
        ToneCurveProvider::Request request = profile;
        request.betaRenderer = true;
        request.sceneMix.clear();
        request.measured = {};
        request.measured.present = sample.measured;
        request.measured.peakNits = sample.detectedPeakNits;
        request.measured.pqAverageNits = sample.detectedPqAverageNits;
        request.measured.linearAverageNits = sample.detectedLinearAverageNits;
        request.scenePeakNits = sample.measured ? sample.detectedPeakNits : sample.authoredPeakNits;
        if (sample.rangeKnown)
        {
            request.masteringBlackNits = sample.masteringBlackNits;
            request.masteringPeakNits = sample.dolbyVision ?
                f.pqToNits(DolbyVisionCode(f, sample.masteringPeakNits)) : sample.masteringPeakNits;
            if (!sample.dolbyVision)
            {
                request.maxCllNits = sample.maxCllNits;
                request.maxFallNits = sample.maxFallNits;
            }
        }
        if (sample.authored)
        {
            request.authoredPeakPq = sample.dolbyVision ?
                DolbyVisionCode(f, sample.authoredPeakNits) : f.nitsToPq(sample.authoredPeakNits);
            request.authoredAveragePq = sample.dolbyVision ?
                DolbyVisionCode(f, sample.authoredAverageNits) : f.nitsToPq(sample.authoredAverageNits);
        }
        return request;
    }

    inline bool Evaluate(const ToneCurveProvider::Request& profile, const LevelsScopeLog::Sample& sample,
        const std::vector<double>& probeNits, Outcome& outcome, std::wstring& error)
    {
        const ToneCurveProvider::Functions& f = ToneCurveProvider::Registered();
        if (!f.sample || !f.nitsToPq || !f.pqToNits)
        {
            error = L"the tone engine is not registered";
            return false;
        }
        ToneCurveProvider::Request request = RequestFor(profile, sample, f);
        request.sourcePq.clear();
        for (double nits : probeNits)
            request.sourcePq.push_back(f.nitsToPq(nits));
        if (!f.sample(request, outcome.result, error))
            return false;
        outcome.profileNits.clear();
        outcome.defaultNits.clear();
        for (double pq : outcome.result.profilePq) outcome.profileNits.push_back(f.pqToNits(pq));
        for (double pq : outcome.result.defaultPq) outcome.defaultNits.push_back(f.pqToNits(pq));
        return true;
    }
}
