#include <windows.h>
#include <roapi.h>
#include <ctxtcall.h>
#include <windows.applicationmodel.core.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <atomic>
#include <cstdio>
#include <thread>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::ApplicationModel::Core;
using namespace ABI::Windows::UI::Core;
// PPL resumes a task's continuation on the thread that started the task by
// calling into that thread's context from a thread-pool thread, with this IID
// (from ppltasks.h). Fruit Ninja quit when Wine failed the call into the view
// thread with RPC_S_SERVER_UNAVAILABLE.
static const IID callbackToSta = {0x0a299774, 0x3e4e, 0xfc42, {0x1d, 0x9d, 0x72, 0xce, 0xe1, 0x05, 0xca, 0x57}};
static HRESULT result = E_PENDING;
static DWORD viewThread, calledOn;
static HRESULT __stdcall OnViewThread(ComCallData*) {
    calledOn = GetCurrentThreadId();
    return S_OK;
}
class View final : public RuntimeClass<IFrameworkViewSource, IFrameworkView> {
    InspectableClass(L"ContextTest.View", BaseTrust);

public:
    HRESULT STDMETHODCALLTYPE CreateView(IFrameworkView** view) override { return QueryInterface(IID_PPV_ARGS(view)); }
    HRESULT STDMETHODCALLTYPE Initialize(ICoreApplicationView*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetWindow(ICoreWindow*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Load(HSTRING) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Run() override {
        viewThread = GetCurrentThreadId();
        ComPtr<IContextCallback> context;
        result = CoGetObjectContext(IID_PPV_ARGS(&context));
        if (FAILED(result)) return S_OK;
        std::atomic<bool> done{false};
        std::thread pool([&] {
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            ComCallData data{};
            result = context->ContextCallback(&OnViewThread, &data, callbackToSta, 5, nullptr);
            CoUninitialize();
            done = true;
        });
        // The call runs here, through this thread's messages.
        const ULONGLONG deadline = GetTickCount64() + 10000;
        while (!done && GetTickCount64() < deadline) {
            MsgWaitForMultipleObjectsEx(0, nullptr, 50, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
        }
        if (!done) {
            puts("contexttest: the call into the view thread never returned");
            ExitProcess(2);
        }
        pool.join();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Uninitialize() override { return S_OK; }
};
int main() {
    RoInitialize(RO_INIT_MULTITHREADED);
    auto* dll = LoadLibraryW(L"appxportable.dll");
    if (!dll) return 1;
    auto get = reinterpret_cast<HRESULT(WINAPI*)(HSTRING, IActivationFactory**)>(GetProcAddress(dll, "ShimGetFactory"));
    ComPtr<IActivationFactory> factory;
    ComPtr<ICoreApplication> app;
    if (!get || FAILED(get(HStringReference(L"Windows.ApplicationModel.Core.CoreApplication").Get(), &factory)) ||
        FAILED(factory.As(&app)))
        return 1;
    auto view = Make<View>();
    const HRESULT run = app->Run(view.Get());
    printf("contexttest: run=%08lx callback=%08lx on view thread=%d\n", run, result, calledOn == viewThread);
    return SUCCEEDED(run) && result == S_OK && calledOn == viewThread ? 0 : 1;
}
