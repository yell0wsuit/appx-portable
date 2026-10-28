#include "generated/globalization.h"
#include "com/factory.h"
#include "registry.h"
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <cstdio>
using namespace Microsoft::WRL;
using namespace ABI::Windows::Globalization;
namespace appx {
namespace {
// Gregorian, 24-hour calendar over a UTC DateTime, in the local time zone or
// UTC. Day/hour arithmetic is exact elapsed time; fields read and write the
// calendar's zone. Names come from the user's locale; numbers use Latin digits.
constexpr INT64 Second = 10000000, Minute = 60 * Second, Hour = 60 * Minute, Day = 24 * Hour;
class Calendar final
    : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultICalendar, DefaultITimeZoneOnCalendar, FtmBase> {
    InspectableClass(L"Windows.Globalization.Calendar", BaseTrust);
    INT64 ticks_ = 0;
    bool utc_ = false;
    Wrappers::HString numerals_;
    HRESULT Local(SYSTEMTIME& local) const {
        FILETIME utc{static_cast<DWORD>(ticks_), static_cast<DWORD>(ticks_ >> 32)};
        SYSTEMTIME system{};
        if (!FileTimeToSystemTime(&utc, utc_ ? &local : &system) ||
            (!utc_ && !SystemTimeToTzSpecificLocalTime(nullptr, &system, &local)))
            return HRESULT_FROM_WIN32(GetLastError());
        return S_OK;
    }
    HRESULT SetLocal(const SYSTEMTIME& local) {
        SYSTEMTIME system = local;
        FILETIME utc{};
        if ((!utc_ && !TzSpecificLocalTimeToSystemTime(nullptr, &local, &system)) || !SystemTimeToFileTime(&system, &utc))
            return E_INVALIDARG;
        ticks_ = (static_cast<INT64>(utc.dwHighDateTime) << 32 | utc.dwLowDateTime) + ticks_ % 10000;
        return S_OK;
    }
    template <class Change> HRESULT Edit(Change change) {
        SYSTEMTIME local{};
        auto hr = Local(local);
        if (FAILED(hr)) return hr;
        if (!change(local)) return E_INVALIDARG;
        return SetLocal(local);
    }
    static int DaysIn(int year, int month) {
        static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        return month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0) ? 29 : days[month - 1];
    }
    HRESULT Field(INT32* value, WORD SYSTEMTIME::* field) const {
        if (!value) return E_POINTER;
        SYSTEMTIME local{};
        auto hr = Local(local);
        if (FAILED(hr)) return hr;
        *value = local.*field;
        return S_OK;
    }
    HRESULT AddTicks(INT64 delta) {
        ticks_ += delta;
        return S_OK;
    }
    static HRESULT Text(const wchar_t* text, HSTRING* result) {
        if (!result) return E_POINTER;
        return WindowsCreateString(text, static_cast<UINT32>(wcslen(text)), result);
    }
    static HRESULT Number(INT64 value, INT32 minDigits, HSTRING* result) {
        wchar_t text[32]{};
        swprintf_s(text, L"%0*lld", std::clamp(minDigits, 1, 20), value);
        return Text(text, result);
    }
    template <class Read> HRESULT LocalNumber(Read read, INT32 minDigits, HSTRING* result) const {
        if (!result) return E_POINTER;
        SYSTEMTIME local{};
        auto hr = Local(local);
        return FAILED(hr) ? hr : Number(read(local), minDigits, result);
    }
    // The full locale name, or its abbreviation when idealLength is 0 or the full name is longer.
    static HRESULT LocaleName(LCTYPE full, LCTYPE abbreviated, INT32 idealLength, HSTRING* result) {
        if (!result) return E_POINTER;
        wchar_t name[80]{};
        if (!GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, full, name, ARRAYSIZE(name))) return HRESULT_FROM_WIN32(GetLastError());
        if (idealLength >= 0 && (idealLength == 0 || static_cast<INT32>(wcslen(name)) > idealLength) &&
            !GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, abbreviated, name, ARRAYSIZE(name)))
            return HRESULT_FROM_WIN32(GetLastError());
        return Text(name, result);
    }
    HRESULT MonthName(INT32 idealLength, HSTRING* result) const {
        SYSTEMTIME local{};
        auto hr = Local(local);
        if (FAILED(hr)) return hr;
        return LocaleName(LOCALE_SMONTHNAME1 + local.wMonth - 1, LOCALE_SABBREVMONTHNAME1 + local.wMonth - 1, idealLength,
                          result);
    }
    HRESULT DayName(INT32 idealLength, HSTRING* result) const {
        SYSTEMTIME local{};
        auto hr = Local(local);
        if (FAILED(hr)) return hr;
        const int monday = (local.wDayOfWeek + 6) % 7; // LOCALE_SDAYNAME1 is Monday
        return LocaleName(LOCALE_SDAYNAME1 + monday, LOCALE_SABBREVDAYNAME1 + monday, idealLength, result);
    }
    static HRESULT EraName(CALTYPE type, HSTRING* result) {
        if (!result) return E_POINTER;
        wchar_t name[40]{};
        if (!GetCalendarInfoEx(LOCALE_NAME_USER_DEFAULT, CAL_GREGORIAN, nullptr, type, name, ARRAYSIZE(name), nullptr))
            return Text(L"A.D.", result);
        return Text(name, result);
    }
    static HRESULT Value(INT32 number, INT32* value) {
        if (!value) return E_POINTER;
        *value = number;
        return S_OK;
    }
    // The one era, period of a 24-hour day, and fixed field ranges: only 1 is valid to set.
    static HRESULT One(INT32 value) { return value == 1 ? S_OK : E_INVALIDARG; }
    HRESULT AddMonthsLocal(INT64 months) {
        return Edit([months](SYSTEMTIME& local) {
            auto total = static_cast<INT64>(local.wYear) * 12 + (local.wMonth - 1) + months;
            if (total < INT64{1601} * 12 || total > INT64{30827} * 12 + 11) return false;
            local.wYear = static_cast<WORD>(total / 12);
            local.wMonth = static_cast<WORD>(total % 12 + 1);
            local.wDay = static_cast<WORD>(std::min(static_cast<int>(local.wDay), DaysIn(local.wYear, local.wMonth)));
            return true;
        });
    }

public:
    Calendar() { SetToNow(); }
    HRESULT STDMETHODCALLTYPE Clone(ICalendar** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto copy = Make<Calendar>();
        if (!copy) return E_OUTOFMEMORY;
        copy->ticks_ = ticks_;
        copy->utc_ = utc_;
        auto hr = copy->numerals_.Set(numerals_.Get());
        return FAILED(hr) ? hr : copy.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE GetCalendarSystem(HSTRING* value) override {
        if (!value) return E_POINTER;
        return WindowsCreateString(L"GregorianCalendar", 17, value);
    }
    HRESULT STDMETHODCALLTYPE GetClock(HSTRING* value) override {
        if (!value) return E_POINTER;
        return WindowsCreateString(L"24HourClock", 11, value);
    }
    HRESULT STDMETHODCALLTYPE GetDateTime(ABI::Windows::Foundation::DateTime* value) override {
        if (!value) return E_POINTER;
        value->UniversalTime = ticks_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetDateTime(ABI::Windows::Foundation::DateTime value) override {
        ticks_ = value.UniversalTime;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetToNow() override {
        FILETIME now{};
        GetSystemTimeAsFileTime(&now);
        ticks_ = static_cast<INT64>(now.dwHighDateTime) << 32 | now.dwLowDateTime;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Year(INT32* value) override { return Field(value, &SYSTEMTIME::wYear); }
    HRESULT STDMETHODCALLTYPE put_Year(INT32 value) override {
        return Edit([value](SYSTEMTIME& local) {
            if (value < 1601 || value > 30827) return false;
            local.wYear = static_cast<WORD>(value);
            local.wDay = static_cast<WORD>(std::min(static_cast<int>(local.wDay), DaysIn(value, local.wMonth)));
            return true;
        });
    }
    HRESULT STDMETHODCALLTYPE AddYears(INT32 years) override { return AddMonthsLocal(static_cast<INT64>(years) * 12); }
    HRESULT STDMETHODCALLTYPE get_Month(INT32* value) override { return Field(value, &SYSTEMTIME::wMonth); }
    HRESULT STDMETHODCALLTYPE put_Month(INT32 value) override {
        return Edit([value](SYSTEMTIME& local) {
            if (value < 1 || value > 12) return false;
            local.wMonth = static_cast<WORD>(value);
            return local.wDay <= DaysIn(local.wYear, value);
        });
    }
    HRESULT STDMETHODCALLTYPE AddMonths(INT32 months) override { return AddMonthsLocal(months); }
    HRESULT STDMETHODCALLTYPE AddWeeks(INT32 weeks) override { return AddTicks(static_cast<INT64>(weeks) * 7 * Day); }
    HRESULT STDMETHODCALLTYPE get_FirstDayInThisMonth(INT32* value) override {
        if (!value) return E_POINTER;
        *value = 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_LastDayInThisMonth(INT32* value) override {
        return get_NumberOfDaysInThisMonth(value);
    }
    HRESULT STDMETHODCALLTYPE get_NumberOfDaysInThisMonth(INT32* value) override {
        if (!value) return E_POINTER;
        SYSTEMTIME local{};
        auto hr = Local(local);
        if (FAILED(hr)) return hr;
        *value = DaysIn(local.wYear, local.wMonth);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Day(INT32* value) override { return Field(value, &SYSTEMTIME::wDay); }
    HRESULT STDMETHODCALLTYPE put_Day(INT32 value) override {
        return Edit([value](SYSTEMTIME& local) {
            if (value < 1 || value > DaysIn(local.wYear, local.wMonth)) return false;
            local.wDay = static_cast<WORD>(value);
            return true;
        });
    }
    HRESULT STDMETHODCALLTYPE AddDays(INT32 days) override { return AddTicks(days * Day); }
    HRESULT STDMETHODCALLTYPE get_DayOfWeek(ABI::Windows::Globalization::DayOfWeek* value) override {
        if (!value) return E_POINTER;
        SYSTEMTIME local{};
        auto hr = Local(local);
        if (FAILED(hr)) return hr;
        *value = static_cast<ABI::Windows::Globalization::DayOfWeek>(local.wDayOfWeek);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Hour(INT32* value) override { return Field(value, &SYSTEMTIME::wHour); }
    HRESULT STDMETHODCALLTYPE put_Hour(INT32 value) override {
        return Edit([value](SYSTEMTIME& local) {
            if (value < 0 || value > 23) return false;
            local.wHour = static_cast<WORD>(value);
            return true;
        });
    }
    HRESULT STDMETHODCALLTYPE AddHours(INT32 hours) override { return AddTicks(hours * Hour); }
    HRESULT STDMETHODCALLTYPE get_Minute(INT32* value) override { return Field(value, &SYSTEMTIME::wMinute); }
    HRESULT STDMETHODCALLTYPE put_Minute(INT32 value) override {
        return Edit([value](SYSTEMTIME& local) {
            if (value < 0 || value > 59) return false;
            local.wMinute = static_cast<WORD>(value);
            return true;
        });
    }
    HRESULT STDMETHODCALLTYPE AddMinutes(INT32 minutes) override { return AddTicks(minutes * Minute); }
    HRESULT STDMETHODCALLTYPE get_Second(INT32* value) override { return Field(value, &SYSTEMTIME::wSecond); }
    HRESULT STDMETHODCALLTYPE put_Second(INT32 value) override {
        return Edit([value](SYSTEMTIME& local) {
            if (value < 0 || value > 59) return false;
            local.wSecond = static_cast<WORD>(value);
            return true;
        });
    }
    HRESULT STDMETHODCALLTYPE AddSeconds(INT32 seconds) override { return AddTicks(seconds * Second); }
    HRESULT STDMETHODCALLTYPE get_Nanosecond(INT32* value) override {
        if (!value) return E_POINTER;
        *value = static_cast<INT32>(ticks_ % Second * 100);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Nanosecond(INT32 value) override {
        if (value < 0 || value > 999999999) return E_INVALIDARG;
        ticks_ = ticks_ - ticks_ % Second + value / 100;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE AddNanoseconds(INT32 nanoseconds) override { return AddTicks(nanoseconds / 100); }
    HRESULT STDMETHODCALLTYPE Compare(ICalendar* other, INT32* result) override {
        if (!other || !result) return E_POINTER;
        ABI::Windows::Foundation::DateTime value{};
        auto hr = other->GetDateTime(&value);
        if (FAILED(hr)) return hr;
        return CompareDateTime(value, result);
    }
    HRESULT STDMETHODCALLTYPE CompareDateTime(ABI::Windows::Foundation::DateTime other, INT32* result) override {
        if (!result) return E_POINTER;
        *result = ticks_ < other.UniversalTime ? -1 : ticks_ > other.UniversalTime ? 1 : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CopyTo(ICalendar* other) override {
        if (!other) return E_POINTER;
        return other->SetDateTime({ticks_});
    }
    // The range SYSTEMTIME covers, 1601 through 30827.
    HRESULT STDMETHODCALLTYPE SetToMin() override {
        ticks_ = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetToMax() override {
        SYSTEMTIME last{30827, 12, 0, 31, 23, 59, 59, 999};
        FILETIME utc{};
        if (!SystemTimeToFileTime(&last, &utc)) return HRESULT_FROM_WIN32(GetLastError());
        ticks_ = (static_cast<INT64>(utc.dwHighDateTime) << 32 | utc.dwLowDateTime) + 9999;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Languages(__FIVectorView_1_HSTRING** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        try {
            Wrappers::HString language;
            auto hr = get_ResolvedLanguage(language.GetAddressOf());
            if (FAILED(hr)) return hr;
            auto languages = winrt::single_threaded_vector<winrt::hstring>({winrt::hstring(language.GetRawBuffer(nullptr))});
            *value = reinterpret_cast<__FIVectorView_1_HSTRING*>(winrt::detach_abi(languages.GetView()));
            return S_OK;
        } catch (...) {
            return winrt::to_hresult();
        }
    }
    HRESULT STDMETHODCALLTYPE get_ResolvedLanguage(HSTRING* value) override {
        wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
        if (!GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH)) wcscpy_s(name, L"en-US");
        return Text(name, value);
    }
    HRESULT STDMETHODCALLTYPE get_NumeralSystem(HSTRING* value) override {
        if (!value) return E_POINTER;
        return numerals_.Get() ? numerals_.CopyTo(value) : Text(L"Latn", value);
    }
    HRESULT STDMETHODCALLTYPE put_NumeralSystem(HSTRING value) override {
        const wchar_t* system = WindowsGetStringRawBuffer(value, nullptr);
        if (_wcsicmp(system, L"Latn") != 0) Log("TRACE", "Calendar numeral system %ls: using Latin digits", system);
        return numerals_.Set(value);
    }
    HRESULT STDMETHODCALLTYPE ChangeCalendarSystem(HSTRING value) override {
        const wchar_t* system = WindowsGetStringRawBuffer(value, nullptr);
        if (_wcsicmp(system, L"GregorianCalendar") != 0)
            Log("TRACE", "Calendar %ls: using the Gregorian calendar", system);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ChangeClock(HSTRING value) override {
        const wchar_t* clock = WindowsGetStringRawBuffer(value, nullptr);
        if (_wcsicmp(clock, L"24HourClock") != 0) Log("TRACE", "Calendar clock %ls: using the 24-hour clock", clock);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_FirstEra(INT32* value) override { return Value(1, value); }
    HRESULT STDMETHODCALLTYPE get_LastEra(INT32* value) override { return Value(1, value); }
    HRESULT STDMETHODCALLTYPE get_NumberOfEras(INT32* value) override { return Value(1, value); }
    HRESULT STDMETHODCALLTYPE get_Era(INT32* value) override { return Value(1, value); }
    HRESULT STDMETHODCALLTYPE put_Era(INT32 value) override { return One(value); }
    HRESULT STDMETHODCALLTYPE AddEras(INT32 eras) override { return eras == 0 ? S_OK : E_INVALIDARG; }
    HRESULT STDMETHODCALLTYPE EraAsFullString(HSTRING* result) override { return EraName(CAL_SERASTRING, result); }
    HRESULT STDMETHODCALLTYPE EraAsString(INT32, HSTRING* result) override {
        return EraName(CAL_SABBREVERASTRING, result);
    }
    HRESULT STDMETHODCALLTYPE get_FirstYearInThisEra(INT32* value) override { return Value(1601, value); }
    HRESULT STDMETHODCALLTYPE get_LastYearInThisEra(INT32* value) override { return Value(30827, value); }
    HRESULT STDMETHODCALLTYPE get_NumberOfYearsInThisEra(INT32* value) override { return Value(30827 - 1601 + 1, value); }
    HRESULT STDMETHODCALLTYPE YearAsString(HSTRING* result) override {
        return LocalNumber([](const SYSTEMTIME& local) { return local.wYear; }, 1, result);
    }
    HRESULT STDMETHODCALLTYPE YearAsTruncatedString(INT32 remainingDigits, HSTRING* result) override {
        INT64 modulus = 1;
        for (INT32 digit = 0; digit < std::clamp(remainingDigits, 1, 5); ++digit) modulus *= 10;
        return LocalNumber([modulus](const SYSTEMTIME& local) { return local.wYear % modulus; },
                           std::clamp(remainingDigits, 1, 5), result);
    }
    HRESULT STDMETHODCALLTYPE YearAsPaddedString(INT32 minDigits, HSTRING* result) override {
        return LocalNumber([](const SYSTEMTIME& local) { return local.wYear; }, minDigits, result);
    }
    HRESULT STDMETHODCALLTYPE get_FirstMonthInThisYear(INT32* value) override { return Value(1, value); }
    HRESULT STDMETHODCALLTYPE get_LastMonthInThisYear(INT32* value) override { return Value(12, value); }
    HRESULT STDMETHODCALLTYPE get_NumberOfMonthsInThisYear(INT32* value) override { return Value(12, value); }
    HRESULT STDMETHODCALLTYPE MonthAsFullString(HSTRING* result) override { return MonthName(-1, result); }
    HRESULT STDMETHODCALLTYPE MonthAsString(INT32 idealLength, HSTRING* result) override {
        return MonthName(idealLength, result);
    }
    HRESULT STDMETHODCALLTYPE MonthAsFullSoloString(HSTRING* result) override { return MonthName(-1, result); }
    HRESULT STDMETHODCALLTYPE MonthAsSoloString(INT32 idealLength, HSTRING* result) override {
        return MonthName(idealLength, result);
    }
    HRESULT STDMETHODCALLTYPE MonthAsNumericString(HSTRING* result) override {
        return LocalNumber([](const SYSTEMTIME& local) { return local.wMonth; }, 1, result);
    }
    HRESULT STDMETHODCALLTYPE MonthAsPaddedNumericString(INT32 minDigits, HSTRING* result) override {
        return LocalNumber([](const SYSTEMTIME& local) { return local.wMonth; }, minDigits, result);
    }
    HRESULT STDMETHODCALLTYPE DayAsString(HSTRING* result) override {
        return LocalNumber([](const SYSTEMTIME& local) { return local.wDay; }, 1, result);
    }
    HRESULT STDMETHODCALLTYPE DayAsPaddedString(INT32 minDigits, HSTRING* result) override {
        return LocalNumber([](const SYSTEMTIME& local) { return local.wDay; }, minDigits, result);
    }
    HRESULT STDMETHODCALLTYPE DayOfWeekAsFullString(HSTRING* result) override { return DayName(-1, result); }
    HRESULT STDMETHODCALLTYPE DayOfWeekAsString(INT32 idealLength, HSTRING* result) override {
        return DayName(idealLength, result);
    }
    HRESULT STDMETHODCALLTYPE DayOfWeekAsFullSoloString(HSTRING* result) override { return DayName(-1, result); }
    HRESULT STDMETHODCALLTYPE DayOfWeekAsSoloString(INT32 idealLength, HSTRING* result) override {
        return DayName(idealLength, result);
    }
    // A 24-hour clock has one period, the whole day.
    HRESULT STDMETHODCALLTYPE get_FirstPeriodInThisDay(INT32* value) override { return Value(1, value); }
    HRESULT STDMETHODCALLTYPE get_LastPeriodInThisDay(INT32* value) override { return Value(1, value); }
    HRESULT STDMETHODCALLTYPE get_NumberOfPeriodsInThisDay(INT32* value) override { return Value(1, value); }
    HRESULT STDMETHODCALLTYPE get_Period(INT32* value) override { return Value(1, value); }
    HRESULT STDMETHODCALLTYPE put_Period(INT32 value) override { return One(value); }
    HRESULT STDMETHODCALLTYPE AddPeriods(INT32 periods) override { return AddTicks(periods * Day); }
    HRESULT STDMETHODCALLTYPE PeriodAsFullString(HSTRING* result) override { return Text(L"", result); }
    HRESULT STDMETHODCALLTYPE PeriodAsString(INT32, HSTRING* result) override { return Text(L"", result); }
    HRESULT STDMETHODCALLTYPE get_FirstHourInThisPeriod(INT32* value) override { return Value(0, value); }
    HRESULT STDMETHODCALLTYPE get_LastHourInThisPeriod(INT32* value) override { return Value(23, value); }
    HRESULT STDMETHODCALLTYPE get_NumberOfHoursInThisPeriod(INT32* value) override { return Value(24, value); }
    HRESULT STDMETHODCALLTYPE HourAsString(HSTRING* result) override {
        return LocalNumber([](const SYSTEMTIME& local) { return local.wHour; }, 1, result);
    }
    HRESULT STDMETHODCALLTYPE HourAsPaddedString(INT32 minDigits, HSTRING* result) override {
        return LocalNumber([](const SYSTEMTIME& local) { return local.wHour; }, minDigits, result);
    }
    HRESULT STDMETHODCALLTYPE MinuteAsString(HSTRING* result) override {
        return LocalNumber([](const SYSTEMTIME& local) { return local.wMinute; }, 1, result);
    }
    HRESULT STDMETHODCALLTYPE MinuteAsPaddedString(INT32 minDigits, HSTRING* result) override {
        return LocalNumber([](const SYSTEMTIME& local) { return local.wMinute; }, minDigits, result);
    }
    HRESULT STDMETHODCALLTYPE SecondAsString(HSTRING* result) override {
        return LocalNumber([](const SYSTEMTIME& local) { return local.wSecond; }, 1, result);
    }
    HRESULT STDMETHODCALLTYPE SecondAsPaddedString(INT32 minDigits, HSTRING* result) override {
        return LocalNumber([](const SYSTEMTIME& local) { return local.wSecond; }, minDigits, result);
    }
    HRESULT STDMETHODCALLTYPE NanosecondAsString(HSTRING* result) override {
        return Number(ticks_ % Second * 100, 1, result);
    }
    HRESULT STDMETHODCALLTYPE NanosecondAsPaddedString(INT32 minDigits, HSTRING* result) override {
        return Number(ticks_ % Second * 100, minDigits, result);
    }
    HRESULT STDMETHODCALLTYPE get_FirstMinuteInThisHour(INT32* value) override { return Value(0, value); }
    HRESULT STDMETHODCALLTYPE get_LastMinuteInThisHour(INT32* value) override { return Value(59, value); }
    HRESULT STDMETHODCALLTYPE get_NumberOfMinutesInThisHour(INT32* value) override { return Value(60, value); }
    HRESULT STDMETHODCALLTYPE get_FirstSecondInThisMinute(INT32* value) override { return Value(0, value); }
    HRESULT STDMETHODCALLTYPE get_LastSecondInThisMinute(INT32* value) override { return Value(59, value); }
    HRESULT STDMETHODCALLTYPE get_NumberOfSecondsInThisMinute(INT32* value) override { return Value(60, value); }
    HRESULT STDMETHODCALLTYPE get_IsDaylightSavingTime(boolean* value) override {
        if (!value) return E_POINTER;
        *value = false;
        if (utc_) return S_OK;
        SYSTEMTIME local{};
        FILETIME localTicks{};
        TIME_ZONE_INFORMATION zone{};
        auto hr = Local(local);
        if (FAILED(hr)) return hr;
        if (!SystemTimeToFileTime(&local, &localTicks) || GetTimeZoneInformation(&zone) == TIME_ZONE_ID_INVALID)
            return HRESULT_FROM_WIN32(GetLastError());
        // Daylight time is any offset from UTC other than the standard one. The
        // local SYSTEMTIME keeps milliseconds, so compare at that precision.
        const INT64 localValue = static_cast<INT64>(localTicks.dwHighDateTime) << 32 | localTicks.dwLowDateTime;
        const INT64 offset = (localValue - (ticks_ - ticks_ % 10000)) / Minute;
        *value = zone.DaylightBias != 0 && offset != -(zone.Bias + zone.StandardBias);
        return S_OK;
    }
    // Only UTC and the local zone: other IDs keep local time, as on a PC set to that zone.
    HRESULT STDMETHODCALLTYPE ChangeTimeZone(HSTRING timeZoneId) override {
        const wchar_t* id = WindowsGetStringRawBuffer(timeZoneId, nullptr);
        utc_ = _wcsicmp(id, L"UTC") == 0 || _wcsicmp(id, L"Etc/UTC") == 0;
        wchar_t local[128]{};
        if (!utc_ && *id && _wcsicmp(id, LocalZone(local)) != 0)
            Log("TRACE", "Calendar time zone %ls: using the local time zone", id);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTimeZone(HSTRING* value) override {
        if (!value) return E_POINTER;
        wchar_t local[128]{};
        const wchar_t* zone = utc_ ? L"UTC" : LocalZone(local);
        return WindowsCreateString(zone, static_cast<UINT32>(wcslen(zone)), value);
    }
    HRESULT STDMETHODCALLTYPE TimeZoneAsFullString(HSTRING* value) override {
        if (!value) return E_POINTER;
        TIME_ZONE_INFORMATION zone{};
        const bool daylight = GetTimeZoneInformation(&zone) == TIME_ZONE_ID_DAYLIGHT;
        const wchar_t* name = utc_ ? L"Coordinated Universal Time" : daylight ? zone.DaylightName : zone.StandardName;
        return WindowsCreateString(name, static_cast<UINT32>(wcslen(name)), value);
    }
    HRESULT STDMETHODCALLTYPE TimeZoneAsString(INT32, HSTRING* value) override { return TimeZoneAsFullString(value); }

private:
    // The local zone's Windows ID, such as "Pacific Standard Time".
    static const wchar_t* LocalZone(wchar_t (&buffer)[128]) {
        DYNAMIC_TIME_ZONE_INFORMATION zone{};
        if (GetDynamicTimeZoneInformation(&zone) == TIME_ZONE_ID_INVALID || !zone.TimeZoneKeyName[0]) return L"Local";
        wcsncpy_s(buffer, zone.TimeZoneKeyName, _TRUNCATE);
        return buffer;
    }

public:
    // The constructors' languages, calendar and clock: this calendar is always
    // Gregorian with a 24-hour clock.
    static HRESULT Create(HSTRING calendar, HSTRING timeZoneId, ICalendar** value) {
        if (!value) return E_POINTER;
        *value = nullptr;
        const wchar_t* system = WindowsGetStringRawBuffer(calendar, nullptr);
        if (*system && _wcsicmp(system, L"GregorianCalendar") != 0)
            Log("TRACE", "Calendar %ls: using the Gregorian calendar", system);
        auto created = Make<Calendar>();
        if (!created) return E_OUTOFMEMORY;
        if (timeZoneId) created->ChangeTimeZone(timeZoneId);
        return created.CopyTo(value);
    }
};
class CalendarFactory final : public Factory<DefaultICalendarFactory, DefaultICalendarFactory2> {
public:
    CalendarFactory() : Factory(L"Windows.Globalization.Calendar") {}
    HRESULT STDMETHODCALLTYPE CreateCalendarDefaultCalendarAndClock(__FIIterable_1_HSTRING*,
                                                                    ICalendar** result) override {
        return Calendar::Create(nullptr, nullptr, result);
    }
    HRESULT STDMETHODCALLTYPE CreateCalendar(__FIIterable_1_HSTRING*, HSTRING calendar, HSTRING,
                                             ICalendar** result) override {
        return Calendar::Create(calendar, nullptr, result);
    }
    HRESULT STDMETHODCALLTYPE CreateCalendarWithTimeZone(__FIIterable_1_HSTRING*, HSTRING calendar, HSTRING,
                                                         HSTRING timeZoneId, ICalendar** result) override {
        return Calendar::Create(calendar, timeZoneId, result);
    }
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto calendar = Make<Calendar>();
        return calendar ? calendar.CopyTo(value) : E_OUTOFMEMORY;
    }
};
}
HRESULT MakeCalendarFactory(IActivationFactory** value) {
    auto factory = Make<CalendarFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
// A Gregorian calendar for Wine, which has none, with Windows' constructors:
// Leo's Fortune creates one with a time zone.
const ShimClass calendarClass{L"Windows.Globalization.Calendar", MakeCalendarFactory, ShimPolicy::FillMissing};
}
