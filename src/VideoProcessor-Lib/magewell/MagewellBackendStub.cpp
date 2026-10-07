#include "pch.h"
#include "MagewellBackend.h"

#include <atomic>

// Local build only (anevard): the Magewell backend compiled out, so this tree
// builds without the vendor MWCapture SDK headers. It replaces
// MagewellBackend.cpp and the four SDK-dependent sources in the Lib project.
// The facade stays the same: no Magewell device is ever found, and a saved
// Magewell selection explains why instead of failing silently.
namespace
{
    class NoMagewellDiscoverer final : public ACaptureDeviceDiscoverer
    {
    public:
        explicit NoMagewellDiscoverer(ICaptureDeviceDiscovererCallback& callback) :
            ACaptureDeviceDiscoverer(callback)
        {
        }

        void Start() override {}
        void Stop() override {}

        HRESULT QueryInterface(REFIID iid, LPVOID* ppv) override
        {
            if (!ppv)
                return E_INVALIDARG;
            *ppv = nullptr;
            if (iid == IID_IUnknown)
            {
                *ppv = this;
                AddRef();
                return S_OK;
            }
            return E_NOINTERFACE;
        }

        ULONG AddRef() override
        {
            return ++m_refCount;
        }

        ULONG Release() override
        {
            const ULONG newRefValue = --m_refCount;
            if (newRefValue == 0)
                delete this;
            return newRefValue;
        }

    private:
        std::atomic<ULONG> m_refCount{ 0 };
    };
}

namespace MagewellBackend
{
    CComPtr<ACaptureDeviceDiscoverer> CreateDiscoverer(ICaptureDeviceDiscovererCallback& callback)
    {
        return CComPtr<ACaptureDeviceDiscoverer>(new NoMagewellDiscoverer(callback));
    }

    CString UnavailableMessage(const CString& configuredDeviceName)
    {
        CString lower(configuredDeviceName);
        lower.MakeLower();
        if (lower.Find(TEXT("magewell")) < 0 && lower.Find(TEXT("pro capture")) < 0)
            return CString();
        return CString(TEXT("Magewell unavailable. This local build has no Magewell support."));
    }

    void AppendDeviceNames(std::vector<std::wstring>&)
    {
    }

    std::vector<std::wstring> ConnectionNames(const std::wstring&)
    {
        return {};
    }
}
