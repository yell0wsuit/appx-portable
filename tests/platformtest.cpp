#include <windows.h>
#include <roapi.h>
#include <windows.graphics.display.h>
#include <windows.applicationmodel.core.h>
#include <windows.ui.input.h>
#include <windows.foundation.diagnostics.h>
#include <windows.globalization.h>
#include <windows.media.devices.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <cstdio>
#include <string>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::Graphics::Display;
int main() {
    RoInitialize(RO_INIT_MULTITHREADED);
    auto* dll = LoadLibraryW(L"appxportable.dll");
    if (!dll) return 1;
    auto get = reinterpret_cast<HRESULT(WINAPI*)(HSTRING, IActivationFactory**)>(GetProcAddress(dll, "ShimGetFactory"));
    ComPtr<IActivationFactory> factory;
    if (FAILED(get(HStringReference(L"Windows.Graphics.Display.DisplayInformation").Get(), &factory))) {
        puts("FAIL display factory");
        return 1;
    }
    ComPtr<IDisplayInformationStatics> statics;
    if (FAILED(factory.As(&statics))) return 2;
    ComPtr<IDisplayInformation> info;
    if (FAILED(statics->GetForCurrentView(&info))) return 3;
    FLOAT dpi = 0;
    if (FAILED(info->get_LogicalDpi(&dpi)) || dpi != 96) return 4;
    DisplayOrientations orientation{};
    if (FAILED(info->get_CurrentOrientation(&orientation)) || orientation != DisplayOrientations_Landscape) return 5;
    ResolutionScale scale{};
    if (FAILED(info->get_ResolutionScale(&scale)) || scale != ResolutionScale_Scale100Percent) return 6;
    if (FAILED(get(HStringReference(L"Windows.ApplicationModel.Core.CoreApplication").Get(), &factory))) return 7;
    ComPtr<ABI::Windows::ApplicationModel::Core::ICoreApplication> core;
    if (FAILED(factory.As(&core))) return 8;
    HString id;
    if (FAILED(core->get_Id(id.GetAddressOf())) || id.Length() == 0) return 9;
    if (FAILED(get(HStringReference(L"Windows.UI.Input.PointerVisualizationSettings").Get(), &factory))) return 10;
    ComPtr<ABI::Windows::UI::Input::IPointerVisualizationSettingsStatics> visualStatics;
    if (FAILED(factory.As(&visualStatics))) return 11;
    ComPtr<ABI::Windows::UI::Input::IPointerVisualizationSettings> visual;
    if (FAILED(visualStatics->GetForCurrentView(&visual))) return 12;
    boolean feedback = true;
    if (FAILED(visual->put_IsContactFeedbackEnabled(false)) ||
        FAILED(visual->get_IsContactFeedbackEnabled(&feedback)) || feedback)
        return 13;
    if (FAILED(get(HStringReference(L"Windows.ApplicationModel.Core.CoreApplication").Get(), &factory))) return 14;
    ComPtr<ABI::Windows::ApplicationModel::Core::ICoreImmersiveApplication> immersive;
    if (FAILED(factory.As(&immersive))) return 15;
    ComPtr<ABI::Windows::ApplicationModel::Core::ICoreApplicationView> view;
    if (FAILED(immersive->get_MainView(&view)) || !view) return 16;
    boolean main = false;
    if (FAILED(view->get_IsMain(&main)) || !main) return 17;
    // ppltasks.h in Store builds traces every task through these statics and
    // throws ClassNotRegisteredException from task creation when they are absent.
    if (FAILED(get(HStringReference(L"Windows.Foundation.Diagnostics.AsyncCausalityTracer").Get(), &factory))) {
        puts("FAIL causality factory");
        return 18;
    }
    ComPtr<ABI::Windows::Foundation::Diagnostics::IAsyncCausalityTracerStatics> causality;
    if (FAILED(factory.As(&causality))) return 19;
    using namespace ABI::Windows::Foundation::Diagnostics;
    const GUID platform{0x4b0b2a8f, 0x8a5c, 0x4f32, {0x8c, 0x2e, 0x31, 0x2a, 0x7e, 0x5f, 0x6b, 0x9d}};
    if (FAILED(causality->TraceOperationCreation(CausalityTraceLevel_Required, CausalitySource_Library, platform, 1,
                                                 HStringReference(L"task").Get(), 0)))
        return 20;
    if (FAILED(causality->TraceSynchronousWorkStart(CausalityTraceLevel_Important, CausalitySource_Library, platform, 1,
                                                    CausalitySynchronousWork_Execution)))
        return 21;
    if (FAILED(causality->TraceSynchronousWorkCompletion(CausalityTraceLevel_Important, CausalitySource_Library,
                                                         CausalitySynchronousWork_Execution)))
        return 22;
    if (FAILED(causality->TraceOperationRelation(CausalityTraceLevel_Important, CausalitySource_Library, platform, 1,
                                                 CausalityRelation_AssignDelegate)))
        return 23;
    if (FAILED(causality->TraceOperationCompletion(CausalityTraceLevel_Required, CausalitySource_Library, platform, 1,
                                                   ABI::Windows::Foundation::AsyncStatus::Completed)))
        return 24;
    // Toast scheduling computes delivery times with Windows.Globalization.Calendar.
    if (FAILED(get(HStringReference(L"Windows.Globalization.Calendar").Get(), &factory))) {
        puts("FAIL calendar factory");
        return 25;
    }
    ComPtr<IInspectable> calendarObject;
    if (FAILED(factory->ActivateInstance(&calendarObject))) return 26;
    ComPtr<ABI::Windows::Globalization::ICalendar> calendar;
    if (FAILED(calendarObject.As(&calendar))) return 27;
    if (FAILED(calendar->SetToNow())) return 28;
    ABI::Windows::Foundation::DateTime now{}, later{};
    if (FAILED(calendar->GetDateTime(&now)) || now.UniversalTime <= 0) return 29;
    if (FAILED(calendar->AddDays(1)) || FAILED(calendar->AddHours(2)) || FAILED(calendar->GetDateTime(&later)))
        return 30;
    if (later.UniversalTime - now.UniversalTime != 26LL * 3600 * 10000000) return 31;
    INT32 hour = -1, minute = -1;
    if (FAILED(calendar->put_Hour(9)) || FAILED(calendar->put_Minute(30)) || FAILED(calendar->get_Hour(&hour)) ||
        FAILED(calendar->get_Minute(&minute)) || hour != 9 || minute != 30)
        return 32;
    INT32 day = 0, nextDay = 0;
    if (FAILED(calendar->get_Day(&day)) || FAILED(calendar->AddDays(1)) || FAILED(calendar->get_Day(&nextDay)) ||
        (nextDay != day + 1 && nextDay != 1))
        return 33;
    // Games reach the complete Windows Calendar, including its Windows 8.1 time
    // zone constructor (Leo's Fortune); the shim's calendar only fills in for Wine.
    using namespace ABI::Windows::Globalization;
    ComPtr<IApplicationLanguagesStatics> languages;
    ComPtr<ABI::Windows::Foundation::Collections::IVectorView<HSTRING>> languageList;
    ComPtr<ABI::Windows::Foundation::Collections::IIterable<HSTRING>> languageItems;
    if (FAILED(RoGetActivationFactory(HStringReference(L"Windows.Globalization.ApplicationLanguages").Get(),
                                      IID_PPV_ARGS(&languages))) ||
        FAILED(languages->get_Languages(&languageList)) || FAILED(languageList.As(&languageItems)))
        return 34;
    ComPtr<ICalendarFactory2> zonedFactory;
    if (FAILED(RoGetActivationFactory(HStringReference(L"Windows.Globalization.Calendar").Get(),
                                      IID_PPV_ARGS(&zonedFactory)))) {
        puts("FAIL calendar time zone factory");
        return 35;
    }
    ComPtr<ICalendar> zoned;
    ComPtr<ITimeZoneOnCalendar> zone;
    HString zoneId;
    if (FAILED(zonedFactory->CreateCalendarWithTimeZone(
            languageItems.Get(), HStringReference(L"GregorianCalendar").Get(), HStringReference(L"24HourClock").Get(),
            HStringReference(L"UTC").Get(), &zoned)) ||
        FAILED(zoned.As(&zone)) || FAILED(zone->GetTimeZone(zoneId.GetAddressOf())))
        return 36;
    // A UTC calendar reads UTC fields whatever this machine's zone is.
    SYSTEMTIME noon{2020, 1, 3, 1, 12, 0, 0, 0};
    FILETIME noonTicks{};
    INT32 utcHour{};
    if (!SystemTimeToFileTime(&noon, &noonTicks) ||
        FAILED(zoned->SetDateTime({static_cast<INT64>(noonTicks.dwHighDateTime) << 32 | noonTicks.dwLowDateTime})) ||
        FAILED(zoned->get_Hour(&utcHour)) || utcHour != 12) {
        printf("FAIL UTC calendar hour %d\n", utcHour);
        return 37;
    }
    // Formatting and ranges at 2020-01-01 12:00 UTC; Leo's Fortune sets the numeral system.
    auto text = [](HRESULT hr, HString& value, const wchar_t* expected) {
        return SUCCEEDED(hr) &&
               (expected ? wcscmp(value.GetRawBuffer(nullptr), expected) == 0 : WindowsGetStringLen(value.Get()) > 0);
    };
    HString numerals, monthText, dayText, yearText, hourText, monthName, dayName;
    INT32 months{};
    ComPtr<ABI::Windows::Foundation::Collections::IVectorView<HSTRING>> calendarLanguages;
    unsigned languageCount{};
    if (FAILED(zoned->put_NumeralSystem(HStringReference(L"Latn").Get())) ||
        !text(zoned->get_NumeralSystem(numerals.GetAddressOf()), numerals, L"Latn") ||
        !text(zoned->MonthAsPaddedNumericString(2, monthText.GetAddressOf()), monthText, L"01") ||
        !text(zoned->DayAsPaddedString(2, dayText.GetAddressOf()), dayText, L"01") ||
        !text(zoned->YearAsTruncatedString(2, yearText.GetAddressOf()), yearText, L"20") ||
        !text(zoned->HourAsPaddedString(2, hourText.GetAddressOf()), hourText, L"12") ||
        !text(zoned->MonthAsFullString(monthName.GetAddressOf()), monthName, nullptr) ||
        !text(zoned->DayOfWeekAsFullString(dayName.GetAddressOf()), dayName, nullptr) ||
        FAILED(zoned->get_NumberOfMonthsInThisYear(&months)) || months != 12 ||
        FAILED(zoned->get_Languages(&calendarLanguages)) || FAILED(calendarLanguages->get_Size(&languageCount)) ||
        languageCount == 0) {
        puts("FAIL calendar formatting");
        return 38;
    }
    // FMOD's Store build asks for the render selector before it opens a device
    // and plays nothing without one; Where's My Water then ended in its levels.
    ComPtr<ABI::Windows::Media::Devices::IMediaDeviceStatics> media;
    HString selector;
    if (FAILED(get(HStringReference(L"Windows.Media.Devices.MediaDevice").Get(), &factory)) ||
        FAILED(factory.As(&media)) || FAILED(media->GetAudioRenderSelector(selector.GetAddressOf()))) {
        puts("FAIL audio render selector");
        return 39;
    }
    std::wstring query(WindowsGetStringRawBuffer(selector.Get(), nullptr));
    for (auto& c : query) c = towupper(c);
    if (query.find(L"{E6327CAD-DCEC-4949-AE8A-991E976A79D2}") == std::wstring::npos) {
        printf("FAIL audio render selector %ls\n", WindowsGetStringRawBuffer(selector.Get(), nullptr));
        return 40;
    }
    puts("platformtest: 0 failure(s)");
    return 0;
}
