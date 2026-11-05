// Windows 8.1 kept ITileNotificationContent's IID but inserted ContentId before
// CreateNotification. Exercise both vtables in separate metadata fixtures.
#include <windows.h>
#include <roapi.h>
#include <windows.ui.notifications.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <cstdio>
#include <cstring>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
int main(int argc, char** argv) {
    RoInitialize(RO_INIT_MULTITHREADED);
    const bool modern = argc > 1 && strcmp(argv[1], "modern") == 0;
    auto dll = LoadLibraryW(L"appxportable.dll");
    if (!dll) return 1;
    auto get = reinterpret_cast<HRESULT(WINAPI*)(HSTRING, IActivationFactory**)>(GetProcAddress(dll, "ShimGetFactory"));
    if (!get) return 2;
    ComPtr<IActivationFactory> factory;
    if (FAILED(get(HStringReference(L"NotificationsExtensions.TileContent.TileContentFactory").Get(), &factory)))
        return 3;
    const GUID statics = {0xd77b1b1e, 0x207f, 0x536b, {0x7d, 0x0a, 0x91, 0xe9, 0xcd, 0xdb, 0x8c, 0xd5}};
    const GUID content = {0x4f5a7235, 0x6bb2, 0x553d, {0x40, 0x00, 0x4a, 0x58, 0x56, 0x2b, 0x70, 0x72}};
    const GUID wide81 = {0xc4ab7b1c, 0xba82, 0x5b98, {0x63, 0xe6, 0xe1, 0xb8, 0x54, 0x31, 0xab, 0xe5}};
    const GUID square81 = {0x147f2666, 0x812e, 0x5b08, {0x50, 0xa5, 0xb0, 0xb6, 0xcc, 0xbe, 0x1f, 0xc4}};
    const GUID text09 = {0x3729afad, 0x3ddf, 0x51b5, {0x58, 0x22, 0x39, 0xd1, 0xd7, 0x9a, 0x6a, 0x33}};
    auto slot = [](IInspectable* object, int index) { return (*reinterpret_cast<void***>(object))[index]; };
    using Get = HRESULT(STDMETHODCALLTYPE*)(IInspectable*, IInspectable**);
    using Put = HRESULT(STDMETHODCALLTYPE*)(IInspectable*, IInspectable*);
    using PutText = HRESULT(STDMETHODCALLTYPE*)(IInspectable*, HSTRING);
    using GetText = HRESULT(STDMETHODCALLTYPE*)(IInspectable*, HSTRING*);
    ComPtr<IInspectable> creator, wide, square, wideShape, squareShape, fields, common;
    if (FAILED(factory->QueryInterface(statics, &creator))) return 4;
    if (FAILED(reinterpret_cast<Get>(slot(creator.Get(), modern ? 80 : 49))(creator.Get(), &wide)) || !wide) return 5;
    if (FAILED(reinterpret_cast<Get>(slot(creator.Get(), 7))(creator.Get(), &square)) || !square) return 6;
    if (FAILED(wide->QueryInterface(wide81, &wideShape)) || FAILED(square->QueryInterface(square81, &squareShape)))
        return 7;
    if (FAILED(reinterpret_cast<Put>(slot(wideShape.Get(), 7))(wideShape.Get(), squareShape.Get()))) return 8;
    if (modern) {
        if (FAILED(wide->QueryInterface(text09, &fields))) return 9;
    } else {
        fields = wide;
    }
    ComPtr<IInspectable> heading, body;
    if (FAILED(reinterpret_cast<Get>(slot(fields.Get(), 6))(fields.Get(), &heading)) || !heading ||
        FAILED(reinterpret_cast<Get>(slot(fields.Get(), 7))(fields.Get(), &body)) || !body)
        return 10;
    if (FAILED(wide->QueryInterface(content, &common))) return 11;
    if (modern) {
        if (FAILED(reinterpret_cast<PutText>(slot(common.Get(), 17))(common.Get(), HStringReference(L"news").Get())))
            return 12;
        HString id;
        if (FAILED(reinterpret_cast<GetText>(slot(common.Get(), 16))(common.Get(), id.GetAddressOf())) ||
            wcscmp(id.GetRawBuffer(nullptr), L"news") != 0)
            return 13;
    }
    ComPtr<IInspectable> notification;
    if (FAILED(reinterpret_cast<Get>(slot(common.Get(), modern ? 18 : 16))(common.Get(), &notification)) ||
        !notification)
        return 14;
    ComPtr<ABI::Windows::UI::Notifications::ITileNotification> typed;
    if (FAILED(notification.As(&typed))) return 15;
    if (modern) {
        // Windows 8.1 inserts large-square templates before its wide templates.
        // This game combines a large square image, a wide tile, and a small square.
        const GUID large81 = {0x305219f9, 0x4de9, 0x5f72, {0x4e, 0x4e, 0x87, 0x7c, 0xa9, 0xce, 0x08, 0x4a}};
        ComPtr<IInspectable> large, shape, largeContent;
        if (FAILED(reinterpret_cast<Get>(slot(creator.Get(), 19))(creator.Get(), &large)) || !large ||
            FAILED(large->QueryInterface(large81, &shape)))
            return 16;
        if (FAILED(reinterpret_cast<Put>(slot(shape.Get(), 7))(shape.Get(), wideShape.Get()))) return 17;
        if (FAILED(large->QueryInterface(content, &largeContent))) return 18;
        notification.Reset();
        if (FAILED(reinterpret_cast<Get>(slot(largeContent.Get(), 18))(largeContent.Get(), &notification)) ||
            !notification || FAILED(notification.As(&typed)))
            return 19;
    }
    puts(modern ? "notifications ABI: Windows 8.1 passed" : "notifications ABI: Windows 8 passed");
    return 0;
}
