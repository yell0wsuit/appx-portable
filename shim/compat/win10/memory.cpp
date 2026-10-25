#include "generated/win10_memory.h"
#include "com/factory.h"
#include <psapi.h>
#include <wrl/event.h>
using namespace Microsoft::WRL;
using namespace ABI::Windows::System;
namespace appx {
namespace {
class MemoryFactory final : public Factory<DefaultIMemoryManagerStatics> {
    EventSource<ABI::Windows::Foundation::IEventHandler<IInspectable*>> increased_, decreased_;
    EventSource<ABI::Windows::Foundation::IEventHandler<AppMemoryUsageLimitChangingEventArgs*>> limitChanging_;

public:
    MemoryFactory() : Factory(L"Windows.System.MemoryManager") {}
    HRESULT STDMETHODCALLTYPE get_AppMemoryUsage(UINT64* value) override {
        if (!value) return E_POINTER;
        *value = 0;
        PROCESS_MEMORY_COUNTERS_EX counters{};
        counters.cb = sizeof(counters);
        if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                                  sizeof(counters)))
            return HRESULT_FROM_WIN32(GetLastError());
        *value = counters.PrivateUsage;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_AppMemoryUsageLimit(UINT64* value) override {
        if (!value) return E_POINTER;
        *value = 0;
        MEMORYSTATUSEX memory{sizeof(memory)};
        if (!GlobalMemoryStatusEx(&memory)) return HRESULT_FROM_WIN32(GetLastError());
        *value = memory.ullTotalPhys;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_AppMemoryUsageLevel(AppMemoryUsageLevel* value) override {
        if (!value) return E_POINTER;
        UINT64 usage{}, limit{};
        auto hr = get_AppMemoryUsage(&usage);
        if (SUCCEEDED(hr)) hr = get_AppMemoryUsageLimit(&limit);
        if (FAILED(hr)) return hr;
        *value = usage >= limit           ? AppMemoryUsageLevel_OverLimit
                 : usage >= limit / 5 * 4 ? AppMemoryUsageLevel_High
                 : usage >= limit / 2     ? AppMemoryUsageLevel_Medium
                                          : AppMemoryUsageLevel_Low;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE add_AppMemoryUsageIncreased(__FIEventHandler_1_IInspectable* h,
                                                          EventRegistrationToken* t) override {
        return increased_.Add(h, t);
    }
    HRESULT STDMETHODCALLTYPE remove_AppMemoryUsageIncreased(EventRegistrationToken t) override {
        return increased_.Remove(t);
    }
    HRESULT STDMETHODCALLTYPE add_AppMemoryUsageDecreased(__FIEventHandler_1_IInspectable* h,
                                                          EventRegistrationToken* t) override {
        return decreased_.Add(h, t);
    }
    HRESULT STDMETHODCALLTYPE remove_AppMemoryUsageDecreased(EventRegistrationToken t) override {
        return decreased_.Remove(t);
    }
    HRESULT STDMETHODCALLTYPE
    add_AppMemoryUsageLimitChanging(__FIEventHandler_1_Windows__CSystem__CAppMemoryUsageLimitChangingEventArgs* h,
                                    EventRegistrationToken* t) override {
        return limitChanging_.Add(h, t);
    }
    HRESULT STDMETHODCALLTYPE remove_AppMemoryUsageLimitChanging(EventRegistrationToken t) override {
        return limitChanging_.Remove(t);
    }
};
}
HRESULT MakeMemoryManagerFactory(IActivationFactory** value) {
    static auto factory = Make<MemoryFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
