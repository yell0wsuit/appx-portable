#include "generated/storage.h"
#include "registry.h"
#include "settings.h"
#include <map>
#include "storage.h"
#include "com/factory.h"
#include "package/package.h"
#include <roapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <filesystem>
#include <mutex>
#include <vector>
#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::Storage;
namespace appx {
namespace {
// Where saves live. APPX_PORTABLE_DATA wins (scripts and tests); otherwise
// SaveLocation in appx-portable.ini beside the game executable:
//   game (default)  <game folder>\SaveData
//   documents       Documents\appx-portable\<package name>
//   appdata         %LOCALAPPDATA%\appx-portable\<package name>
//   anything else   a folder; relative paths start at the game folder
// <package name> is the manifest's identity name, so games never share saves.
std::filesystem::path GameDirectory() {
    std::wstring path(MAX_PATH, L'\0');
    DWORD length = 0;
    while ((length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()))) == path.size())
        path.resize(path.size() * 2);
    path.resize(length);
    return std::filesystem::path(path).parent_path();
}
std::wstring SaveLocationSetting() {
    wchar_t value[1024]{};
    const auto length = GetEnvironmentVariableW(L"APPX_PORTABLE_DATA", value, 1024);
    if (length > 0 && length < 1024) return value;
    GetPrivateProfileStringW(L"storage", L"SaveLocation", L"", value, 1024,
                             (GameDirectory() / L"appx-portable.ini").c_str());
    std::wstring setting(value);
    setting.erase(0, setting.find_first_not_of(L" \t\""));
    setting.erase(setting.find_last_not_of(L" \t\"") + 1);
    return setting;
}
std::filesystem::path KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR path{};
    if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &path))) return {};
    std::filesystem::path result(path);
    CoTaskMemFree(path);
    return result;
}
std::wstring PackageFolderName() {
    const auto& package = CurrentPackage();
    if (!package.identityName.empty()) return package.identityName;
    return GameDirectory().filename().wstring(); // no manifest: the game folder's name
}
std::filesystem::path AppDataRoot() {
    // The environment variable first, so tests can point it elsewhere.
    wchar_t value[MAX_PATH]{};
    auto length = GetEnvironmentVariableW(L"LOCALAPPDATA", value, MAX_PATH);
    auto base = length && length < MAX_PATH ? std::filesystem::path(value) : KnownFolder(FOLDERID_LocalAppData);
    return base.empty() ? base : base / L"appx-portable" / PackageFolderName();
}
std::filesystem::path DocumentsRoot() {
    auto base = KnownFolder(FOLDERID_Documents);
    return base.empty() ? base : base / L"appx-portable" / PackageFolderName();
}
std::filesystem::path LocationPath(const std::wstring& setting) {
    if (setting.empty() || _wcsicmp(setting.c_str(), L"game") == 0) return GameDirectory() / L"SaveData";
    if (_wcsicmp(setting.c_str(), L"documents") == 0) return DocumentsRoot();
    if (_wcsicmp(setting.c_str(), L"appdata") == 0) return AppDataRoot();
    std::wstring expanded(32768, L'\0');
    auto length = ExpandEnvironmentStringsW(setting.c_str(), expanded.data(), static_cast<DWORD>(expanded.size()));
    expanded.resize(length ? length - 1 : 0);
    std::filesystem::path path(expanded.empty() ? setting : expanded);
    return path.is_absolute() ? path : GameDirectory() / path;
}
// Creates the folders and proves the root is writable (a game folder under
// Program Files is not).
bool PrepareRoot(const std::filesystem::path& root) {
    if (root.empty()) return false;
    std::error_code error;
    for (const auto* name : {L"Local", L"Roaming", L"Temp"}) {
        std::filesystem::create_directories(root / name, error);
        if (error) return false;
    }
    HANDLE probe = CreateFileW((root / L"Local" / L".appxportable-write-test").c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (probe == INVALID_HANDLE_VALUE) return false;
    CloseHandle(probe);
    return true;
}
HRESULT ResolveDataRoot(std::filesystem::path& root) {
    const auto setting = SaveLocationSetting();
    std::vector<std::filesystem::path> candidates{LocationPath(setting), DocumentsRoot(), AppDataRoot()};
    for (auto candidate : candidates) {
        // The game joins file names onto these paths; keep them in one style.
        std::error_code error;
        if (!candidate.empty()) candidate = std::filesystem::absolute(candidate, error).lexically_normal();
        if (!PrepareRoot(candidate)) {
            Log("FAIL", "save location %ls is not writable", candidate.c_str());
            continue;
        }
        Log("INFO", "saves: %ls", candidate.c_str());
        root = std::move(candidate);
        return S_OK;
    }
    return E_ACCESSDENIED;
}
}
class Folder final
    : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultIStorageFolder, DefaultIStorageItem, FtmBase> {
    InspectableClass(L"Windows.Storage.StorageFolder", BaseTrust);
    std::wstring path_;

public:
    explicit Folder(std::wstring path) : path_(std::move(path)) {}
    HRESULT STDMETHODCALLTYPE get_Path(HSTRING* value) override {
        if (!value) return E_POINTER;
        return WindowsCreateString(path_.data(), static_cast<UINT32>(path_.size()), value);
    }
    HRESULT STDMETHODCALLTYPE get_Name(HSTRING* value) override {
        if (!value) return E_POINTER;
        auto name = std::filesystem::path(path_).filename().wstring();
        return WindowsCreateString(name.data(), static_cast<UINT32>(name.size()), value);
    }
    HRESULT STDMETHODCALLTYPE get_Attributes(ABI::Windows::Storage::FileAttributes* value) override {
        if (!value) return E_POINTER;
        *value = ABI::Windows::Storage::FileAttributes_Directory;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_DateCreated(ABI::Windows::Foundation::DateTime* value) override {
        if (!value) return E_POINTER;
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (!GetFileAttributesExW(path_.c_str(), GetFileExInfoStandard, &data))
            return HRESULT_FROM_WIN32(GetLastError());
        value->UniversalTime =
            (static_cast<INT64>(data.ftCreationTime.dwHighDateTime) << 32) | data.ftCreationTime.dwLowDateTime;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE IsOfType(StorageItemTypes type, boolean* value) override {
        if (!value) return E_POINTER;
        *value = (type & StorageItemTypes_Folder) != 0;
        return S_OK;
    }
};
// Windows' StorageFolder for a path, waited for on its thread-pool completion.
static HRESULT OsStorageFolder(const std::wstring& path, IStorageFolder** value) {
    ComPtr<IStorageFolderStatics> statics;
    HRESULT hr = OsGetActivationFactory(HStringReference(RuntimeClass_Windows_Storage_StorageFolder).Get(),
                                        IID_PPV_ARGS(&statics));
    ComPtr<__FIAsyncOperation_1_Windows__CStorage__CStorageFolder> operation;
    if (SUCCEEDED(hr))
        hr = statics->GetFolderFromPathAsync(HStringReference(path.c_str(), static_cast<unsigned>(path.size())).Get(),
                                             &operation);
    if (FAILED(hr)) return hr;
    Event done(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!done.IsValid()) return HRESULT_FROM_WIN32(GetLastError());
    HANDLE signal = done.Get();
    auto handler =
        Callback<Implements<RuntimeClassFlags<ClassicCom>,
                            __FIAsyncOperationCompletedHandler_1_Windows__CStorage__CStorageFolder, FtmBase>>(
            [signal](__FIAsyncOperation_1_Windows__CStorage__CStorageFolder*, ABI::Windows::Foundation::AsyncStatus) {
                SetEvent(signal);
                return S_OK;
            });
    if (!handler) return E_OUTOFMEMORY;
    hr = operation->put_Completed(handler.Get());
    if (FAILED(hr)) return hr;
    // A plain wait: the lookup completes on the thread pool, and pumping here
    // (CoWaitForMultipleHandles) would run queued game work inside a game call.
    const DWORD waited = WaitForSingleObject(signal, 10000);
    if (waited != WAIT_OBJECT_0) {
        ComPtr<ABI::Windows::Foundation::IAsyncInfo> info;
        if (SUCCEEDED(operation.As(&info))) info->Cancel();
        return waited == WAIT_TIMEOUT ? HRESULT_FROM_WIN32(ERROR_TIMEOUT) : HRESULT_FROM_WIN32(GetLastError());
    }
    return operation->GetResults(value);
}
HRESULT MakeStorageFolder(const std::wstring& path, IStorageFolder** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    // Windows' folder supports everything games do with it (create, open,
    // enumerate); the shim's own covers only the path, for Wine, whose
    // Windows.Storage is incomplete.
    const HRESULT hr = OsStorageFolder(path, value);
    if (SUCCEEDED(hr) && *value) return hr;
    Log("TRACE", "StorageFolder %ls from Windows failed hr=%08lx; using the path-only folder", path.c_str(), hr);
    auto folder = Make<Folder>(path);
    return folder ? folder.CopyTo(value) : E_OUTOFMEMORY;
}
using ContainerView = __FIMapView_2_HSTRING_Windows__CStorage__CApplicationDataContainer;
class ContainerMap final : public RuntimeClass<RuntimeClassFlags<WinRt>, ContainerView> {
    InspectableClass(L"Windows.Foundation.Collections.IMapView`2", BaseTrust);

public:
    std::map<std::wstring, ComPtr<IApplicationDataContainer>> items;
    std::recursive_mutex mutex;
    HRESULT STDMETHODCALLTYPE Lookup(HSTRING key, IApplicationDataContainer** v) override {
        if (!v) return E_POINTER;
        *v = nullptr;
        std::lock_guard<std::recursive_mutex> lock(mutex);
        auto it = items.find(WindowsGetStringRawBuffer(key, nullptr));
        if (it == items.end()) return E_BOUNDS;
        return it->second.CopyTo(v);
    }
    HRESULT STDMETHODCALLTYPE get_Size(UINT32* v) override {
        if (!v) return E_POINTER;
        std::lock_guard<std::recursive_mutex> lock(mutex);
        *v = static_cast<UINT32>(items.size());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE HasKey(HSTRING key, boolean* v) override {
        if (!v) return E_POINTER;
        std::lock_guard<std::recursive_mutex> lock(mutex);
        *v = items.count(WindowsGetStringRawBuffer(key, nullptr)) != 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Split(ContainerView** a, ContainerView** b) override {
        if (!a || !b) return E_POINTER;
        *a = *b = nullptr;
        return S_OK;
    }
};
class Container final : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultIApplicationDataContainer, FtmBase> {
    InspectableClass(L"Windows.Storage.ApplicationDataContainer", BaseTrust);
    ApplicationDataLocality locality_;
    std::wstring name_, path_;
    ComPtr<ContainerMap> children_ = Make<ContainerMap>();
    ComPtr<ABI::Windows::Foundation::Collections::IPropertySet> values_;
    std::shared_ptr<SettingsPersistence> persistence_;

public:
    explicit Container(ApplicationDataLocality locality) : locality_(locality) {}
    HRESULT Initialize(const std::wstring& path) {
        path_ = path;
        auto hr = CreatePersistentSettings(path, values_, persistence_);
        if (FAILED(hr)) return hr;
        const auto directory = std::filesystem::path(path).parent_path() / L"containers";
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
            if (!entry.is_directory()) continue;
            const auto encoded = entry.path().filename().wstring();
            if (encoded.size() % 4) continue;
            std::wstring name;
            bool valid = true;
            for (size_t i = 0; i < encoded.size(); i += 4) {
                wchar_t* end = nullptr;
                auto chunk = encoded.substr(i, 4);
                auto ch = wcstoul(chunk.c_str(), &end, 16);
                if (*end) {
                    valid = false;
                    break;
                }
                name += static_cast<wchar_t>(ch);
            }
            if (!valid) continue;
            auto child = Make<Container>(locality_);
            child->name_ = name;
            hr = child->Initialize((entry.path() / L"settings.json").wstring());
            if (FAILED(hr)) return hr;
            children_->items[name] = child;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Containers(ContainerView** value) override {
        if (!value) return E_POINTER;
        return children_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE CreateContainer(HSTRING name, ApplicationDataCreateDisposition disposition,
                                              IApplicationDataContainer** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        std::wstring key(WindowsGetStringRawBuffer(name, nullptr), WindowsGetStringLen(name));
        if (key.empty() || key.size() > 64 ||
            (disposition != ApplicationDataCreateDisposition_Always &&
             disposition != ApplicationDataCreateDisposition_Existing))
            return E_INVALIDARG;
        std::lock_guard<std::recursive_mutex> lock(children_->mutex);
        auto it = children_->items.find(key);
        if (it != children_->items.end()) return it->second.CopyTo(value);
        if (disposition == ApplicationDataCreateDisposition_Existing) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        std::wstring encoded;
        for (wchar_t ch : key) {
            wchar_t hex[5];
            swprintf_s(hex, L"%04x", static_cast<unsigned short>(ch));
            encoded += hex;
        }
        auto child = Make<Container>(locality_);
        if (!child) return E_OUTOFMEMORY;
        child->name_ = key;
        auto path = std::filesystem::path(path_).parent_path() / L"containers" / encoded;
        std::error_code ec;
        std::filesystem::create_directories(path, ec);
        if (ec) return HRESULT_FROM_WIN32(ec.value());
        auto hr = child->Initialize((path / L"settings.json").wstring());
        if (FAILED(hr)) return hr;
        children_->items[key] = child;
        return child.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE DeleteContainer(HSTRING name) override {
        std::lock_guard<std::recursive_mutex> lock(children_->mutex);
        auto it = children_->items.find(WindowsGetStringRawBuffer(name, nullptr));
        if (it == children_->items.end()) return S_OK;
        auto child = static_cast<Container*>(it->second.Get());
        std::error_code ec;
        std::filesystem::remove_all(std::filesystem::path(child->path_).parent_path(), ec);
        if (ec) return HRESULT_FROM_WIN32(ec.value());
        children_->items.erase(it);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Name(HSTRING* value) override {
        if (!value) return E_POINTER;
        return WindowsCreateString(name_.c_str(), static_cast<UINT32>(name_.size()), value);
    }
    HRESULT STDMETHODCALLTYPE get_Locality(ApplicationDataLocality* value) override {
        if (!value) return E_POINTER;
        *value = locality_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Values(ABI::Windows::Foundation::Collections::IPropertySet** value) override {
        if (!value) return E_POINTER;
        return values_.CopyTo(value);
    }
};
class AppData final : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultIApplicationData, FtmBase> {
    InspectableClass(L"Windows.Storage.ApplicationData", BaseTrust);
    // Folders are made on first use: Windows' StorageFolder is asynchronous.
    struct LazyFolder {
        std::wstring path;
        ComPtr<IStorageFolder> folder;
    } local_, roaming_, temporary_;
    std::mutex folders_;
    ComPtr<Container> localSettings_, roamingSettings_;
    EventSource<__FITypedEventHandler_2_Windows__CStorage__CApplicationData_IInspectable> changed_;
    HRESULT FolderOf(LazyFolder& lazy, IStorageFolder** value) {
        if (!value) return E_POINTER;
        std::lock_guard<std::mutex> lock(folders_);
        if (!lazy.folder) {
            const HRESULT hr = MakeStorageFolder(lazy.path, &lazy.folder);
            if (FAILED(hr)) return hr;
        }
        return lazy.folder.CopyTo(value);
    }

public:
    HRESULT Initialize() {
        std::filesystem::path root;
        HRESULT hr{};
        try {
            hr = ResolveDataRoot(root);
            if (FAILED(hr)) return hr;
            local_.path = (root / L"Local").wstring();
            roaming_.path = (root / L"Roaming").wstring();
            temporary_.path = (root / L"Temp").wstring();
        } catch (...) {
            return E_FAIL;
        }
        localSettings_ = Make<Container>(ApplicationDataLocality_Local);
        roamingSettings_ = Make<Container>(ApplicationDataLocality_Roaming);
        if (!localSettings_ || !roamingSettings_) return E_OUTOFMEMORY;
        hr = localSettings_->Initialize((root / L"Local" / L"settings.json").wstring());
        return FAILED(hr) ? hr : roamingSettings_->Initialize((root / L"Roaming" / L"settings.json").wstring());
    }
    HRESULT STDMETHODCALLTYPE get_Version(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_LocalFolder(IStorageFolder** value) override {
        Log("TRACE", "ApplicationData.LocalFolder");
        return FolderOf(local_, value);
    }
    HRESULT STDMETHODCALLTYPE get_RoamingFolder(IStorageFolder** value) override { return FolderOf(roaming_, value); }
    HRESULT STDMETHODCALLTYPE get_TemporaryFolder(IStorageFolder** value) override {
        return FolderOf(temporary_, value);
    }
    HRESULT STDMETHODCALLTYPE get_LocalSettings(IApplicationDataContainer** value) override {
        if (!value) return E_POINTER;
        Log("TRACE", "ApplicationData.LocalSettings");
        return localSettings_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE get_RoamingSettings(IApplicationDataContainer** value) override {
        if (!value) return E_POINTER;
        return roamingSettings_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE get_RoamingStorageQuota(UINT64* value) override {
        if (!value) return E_POINTER;
        *value = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE
    add_DataChanged(__FITypedEventHandler_2_Windows__CStorage__CApplicationData_IInspectable* handler,
                    EventRegistrationToken* token) override {
        return changed_.Add(handler, token);
    }
    HRESULT STDMETHODCALLTYPE remove_DataChanged(EventRegistrationToken token) override {
        return changed_.Remove(token);
    }
    HRESULT STDMETHODCALLTYPE SignalDataChanged() override { return changed_.InvokeAll(this, nullptr); }
};
class StorageFileFactory final : public Factory<IStorageFileStatics> {
    ComPtr<IStorageFileStatics> real_;

public:
    StorageFileFactory() : Factory(L"Windows.Storage.StorageFile") {
        OsGetActivationFactory(HStringReference(L"Windows.Storage.StorageFile").Get(), IID_PPV_ARGS(&real_));
    }
    HRESULT STDMETHODCALLTYPE
    GetFileFromPathAsync(HSTRING path, __FIAsyncOperation_1_Windows__CStorage__CStorageFile** operation) override {
        return real_ ? real_->GetFileFromPathAsync(path, operation) : E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE
    GetFileFromApplicationUriAsync(ABI::Windows::Foundation::IUriRuntimeClass* uri,
                                   __FIAsyncOperation_1_Windows__CStorage__CStorageFile** operation) override {
        if (!uri || !operation) return E_POINTER;
        *operation = nullptr;
        HString absolute;
        auto hr = uri->get_AbsoluteUri(absolute.GetAddressOf());
        if (FAILED(hr)) return hr;
        std::wstring text(absolute.GetRawBuffer(nullptr));
        std::filesystem::path root;
        std::wstring relative;
        if (text.compare(0, 11, L"ms-appx:///") == 0) {
            root = CurrentPackage().directory;
            relative = text.substr(11);
        } else if (text.compare(0, 14, L"ms-appdata:///") == 0) {
            hr = ResolveDataRoot(root);
            if (FAILED(hr)) return hr;
            relative = text.substr(14);
            if (relative.compare(0, 6, L"local/") == 0)
                relative = L"Local/" + relative.substr(6);
            else if (relative.compare(0, 8, L"roaming/") == 0)
                relative = L"Roaming/" + relative.substr(8);
            else if (relative.compare(0, 5, L"temp/") == 0)
                relative = L"Temp/" + relative.substr(5);
            else
                return E_INVALIDARG;
        } else {
            return real_ ? real_->GetFileFromApplicationUriAsync(uri, operation) : E_NOTIMPL;
        }
        DWORD length = static_cast<DWORD>(relative.size() + 1);
        std::wstring decoded(length, L'\0');
        hr = UrlUnescapeW(relative.data(), decoded.data(), &length, 0);
        if (FAILED(hr)) return hr;
        decoded.resize(length);
        // Reject URI components that could become a rooted or escaping path.
        if (decoded.find_first_of(L":?#") != std::wstring::npos || decoded.find(L'\0') != std::wstring::npos)
            return E_INVALIDARG;
        const std::filesystem::path part(decoded);
        if (part.is_absolute() || part.has_root_path()) return E_INVALIDARG;
        for (const auto& segment : part)
            if (segment == L"..") return E_INVALIDARG;
        const auto path = (root / part).lexically_normal().wstring();
        return GetFileFromPathAsync(HStringReference(path.c_str()).Get(), operation);
    }
    HRESULT STDMETHODCALLTYPE CreateStreamedFileAsync(
        HSTRING displayNameWithExtension, ABI::Windows::Storage::IStreamedFileDataRequestedHandler* dataRequested,
        ABI::Windows::Storage::Streams::IRandomAccessStreamReference* thumbnail,
        __FIAsyncOperation_1_Windows__CStorage__CStorageFile** operation) override {
        return real_ ? real_->CreateStreamedFileAsync(displayNameWithExtension, dataRequested, thumbnail, operation)
                     : E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE
    ReplaceWithStreamedFileAsync(ABI::Windows::Storage::IStorageFile* fileToReplace,
                                 ABI::Windows::Storage::IStreamedFileDataRequestedHandler* dataRequested,
                                 ABI::Windows::Storage::Streams::IRandomAccessStreamReference* thumbnail,
                                 __FIAsyncOperation_1_Windows__CStorage__CStorageFile** operation) override {
        return real_ ? real_->ReplaceWithStreamedFileAsync(fileToReplace, dataRequested, thumbnail, operation)
                     : E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE
    CreateStreamedFileFromUriAsync(HSTRING displayNameWithExtension, ABI::Windows::Foundation::IUriRuntimeClass* uri,
                                   ABI::Windows::Storage::Streams::IRandomAccessStreamReference* thumbnail,
                                   __FIAsyncOperation_1_Windows__CStorage__CStorageFile** operation) override {
        return real_ ? real_->CreateStreamedFileFromUriAsync(displayNameWithExtension, uri, thumbnail, operation)
                     : E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE ReplaceWithStreamedFileFromUriAsync(
        ABI::Windows::Storage::IStorageFile* fileToReplace, ABI::Windows::Foundation::IUriRuntimeClass* uri,
        ABI::Windows::Storage::Streams::IRandomAccessStreamReference* thumbnail,
        __FIAsyncOperation_1_Windows__CStorage__CStorageFile** operation) override {
        return real_ ? real_->ReplaceWithStreamedFileFromUriAsync(fileToReplace, uri, thumbnail, operation) : E_NOTIMPL;
    }
};
HRESULT MakeStorageFileFactory(IActivationFactory** value) {
    if (!value) return E_POINTER;
    static auto factory = Make<StorageFileFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
class StorageFactory final : public Factory<IApplicationDataStatics> {
    ComPtr<AppData> data_;
    HRESULT initialized_ = E_UNEXPECTED;

public:
    StorageFactory() : Factory(L"Windows.Storage.ApplicationData"), data_(Make<AppData>()) {
        initialized_ = data_ ? data_->Initialize() : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE get_Current(IApplicationData** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return FAILED(initialized_) ? initialized_ : data_.CopyTo(value);
    }
};
HRESULT MakeStorageFactory(IActivationFactory** value) {
    static auto factory = Make<StorageFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
