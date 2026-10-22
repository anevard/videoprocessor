#pragma once
#include <string>

// Loads the drawn renderer's libplacebo for the tone charts at runtime. The
// Config application does not link libplacebo, so it starts without VP
// Renderer's files; every function and data export is found by name once loaded.
namespace ToneCurveRuntime
{
    // The renderer the charts draw: VP's newest renderer, today VP Renderer
    // (Beta) with libplacebo 377. NOT the old VP Renderer (libplacebo 360).
    // When the beta becomes VP Renderer, repoint both of these at it then,
    // never before: pointing them at the old renderer would draw 360's curves.
    constexpr bool BetaRenderer = true;
    inline const wchar_t* RendererName() { return L"VP Renderer (Beta)"; }

    // libplacebo's file name in that renderer's folder, from VP's own list of
    // the renderer's files, or empty if the list names none.
    const wchar_t* LibraryFile();

    // True when libplacebo is loaded. Otherwise `missing` names the first VP
    // Renderer file that could not be found or loaded. Tried once per run.
    bool Load(std::wstring& missing);

    // The named export of the loaded libplacebo, or null.
    void* Find(const char* exportName);
}
