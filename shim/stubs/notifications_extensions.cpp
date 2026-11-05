// NotificationsExtensions: Microsoft's C# tile and toast XML helper library,
// which Windows 8 games ship as a WinRT component hosted by Windows'
// CLRHost.dll, which needs the package to run. Stood in for its Windows 8 tile
// templates, all 45 of them: Asphalt 7: Heat builds its live tile from
// TileWideImage and TileSquareImage at launch, ilomilo plus from
// TileWideImageAndText02 and TileSquareText04. The offline TileUpdater
// discards the notification. Interfaces declared from
// NotificationsExtensions.winmd (same ABI for object parameters as
// IInspectable*).
#include "registry.h"
#include "package/package.h"
#include <fstream>
#include <iterator>
#include <type_traits>
#include "com/factory.h"
#include "log.h"
#include <windows.data.xml.dom.h>
#include <windows.ui.notifications.h>
#include <wrl.h>
#include <string>
#include <vector>
using namespace Microsoft::WRL;
using ABI::Windows::Data::Xml::Dom::IXmlDocument;
using ABI::Windows::UI::Notifications::ITileNotification;
namespace appx {
namespace {
MIDL_INTERFACE("2C0D137A-1FC8-5DB6-4C9A-580221EEF1F4")
INotificationContent : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetContent(HSTRING * value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetXml(IXmlDocument * *value) = 0;
};
MIDL_INTERFACE("02E42CE2-2E61-5393-6E49-F9CB367C34D5")
INotificationContentImage : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Src(HSTRING * value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Src(HSTRING value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Alt(HSTRING * value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Alt(HSTRING value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AddImageQuery(boolean * value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AddImageQuery(boolean value) = 0;
};
MIDL_INTERFACE("7B093628-752B-5D3A-42C0-ABE72D42D323")
INotificationContentText : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Text(HSTRING * value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Text(HSTRING value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Lang(HSTRING * value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Lang(HSTRING value) = 0;
};
MIDL_INTERFACE("4F5A7235-6BB2-553D-4000-4A58562B7072")
ITileNotificationContent : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_StrictValidation(boolean * value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_StrictValidation(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Lang(HSTRING * value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Lang(HSTRING value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_BaseUri(HSTRING * value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_BaseUri(HSTRING value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Branding(INT32 * value) = 0; // TileBranding: None, Logo, Name
    virtual HRESULT STDMETHODCALLTYPE put_Branding(INT32 value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AddImageQuery(boolean * value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AddImageQuery(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateNotification(ITileNotification * *value) = 0;
};
// Internal IID; QueryInterface selects this vtable for the public IID when
// the package metadata has the Windows 8.1 ContentId slots.
MIDL_INTERFACE("CA37118C-A768-4393-A78C-9E97E1CCF065")
ITileNotificationContent81 : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_StrictValidation(boolean * value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_StrictValidation(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Lang(HSTRING * value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Lang(HSTRING value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_BaseUri(HSTRING * value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_BaseUri(HSTRING value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Branding(INT32 * value) = 0; // TileBranding: None, Logo, Name
    virtual HRESULT STDMETHODCALLTYPE put_Branding(INT32 value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AddImageQuery(boolean * value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AddImageQuery(boolean value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ContentId(HSTRING * value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_ContentId(HSTRING value) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateNotification(ITileNotification * *value) = 0;
};
MIDL_INTERFACE("2537E592-94FF-5DCC-6279-0366C54998C5")
ISquareTileNotificationContent : public IInspectable{};
MIDL_INTERFACE("B803960C-2950-54E8-49C7-4B0033742AE4")
IWideTileNotificationContent : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_SquareContent(ISquareTileNotificationContent * *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_SquareContent(ISquareTileNotificationContent * value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_RequireSquareContent(boolean * value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_RequireSquareContent(boolean value) = 0;
};
MIDL_INTERFACE("1FC38DDD-E3C8-44FB-A487-5CD5B1B34B7A") ILargeTileContent : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Child(IInspectable * *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Child(IInspectable * value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_RequireChild(boolean * value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_RequireChild(boolean value) = 0;
};
// The interface the factory returns for each template: the getters of its
// fields, from slot 6. Taken from the Windows 8.1 NotificationsExtensions.winmd,
// whose TileSquare150x150Text04 is Windows 8's TileSquareText04 renamed, and so
// on. Windows 8.1 adds larger templates with up to twenty-two fields. IIDs
// come from the NotificationsExtensions.winmd games ship; knownTemplates
// includes the legacy interfaces and the dimension-qualified 8.1 interfaces.
// TileWidePeekImageCollection01-06 also carry TileWideImageCollection's five
// images, reached only through that interface's IID, which is not known.
struct __declspec(uuid("e3ace2bb-8796-4166-8f80-4d6a6e22fc82")) ITileTemplate : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE Field0(IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE Field1(IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE Field2(IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE Field3(IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE Field4(IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE Field5(IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE Field6(IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE Field7(IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE Field8(IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE Field9(IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE Field10(IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE Field11(IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE Field12(IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE Field13(IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE Field14(IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE Field15(IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE Field16(IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE Field17(IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE Field18(IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE Field19(IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE Field20(IInspectable** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE Field21(IInspectable** value) = 0;
};
// Every template, in ITileContentFactoryStatic's slot order, with its fields
// in its interface's order: I an image, T a text.
#define APPX_TILE_TEMPLATES(X)                                                                                         \
    X(SquareBlock, "TT")                                                                                               \
    X(SquareImage, "I")                                                                                                \
    X(SquarePeekImageAndText01, "ITTTT")                                                                               \
    X(SquarePeekImageAndText02, "ITT")                                                                                 \
    X(SquarePeekImageAndText03, "ITTTT")                                                                               \
    X(SquarePeekImageAndText04, "IT")                                                                                  \
    X(SquareText01, "TTTT")                                                                                            \
    X(SquareText02, "TT")                                                                                              \
    X(SquareText03, "TTTT")                                                                                            \
    X(SquareText04, "T")                                                                                               \
    X(WideBlockAndText01, "TTTTTT")                                                                                    \
    X(WideBlockAndText02, "TTT")                                                                                       \
    X(WideImage, "I")                                                                                                  \
    X(WideImageAndText01, "IT")                                                                                        \
    X(WideImageAndText02, "ITT")                                                                                       \
    X(WideImageCollection, "IIIII")                                                                                    \
    X(WidePeekImage01, "ITT")                                                                                          \
    X(WidePeekImage02, "ITTTTT")                                                                                       \
    X(WidePeekImage03, "IT")                                                                                           \
    X(WidePeekImage04, "IT")                                                                                           \
    X(WidePeekImage05, "IITT")                                                                                         \
    X(WidePeekImage06, "IIT")                                                                                          \
    X(WidePeekImageAndText01, "IT")                                                                                    \
    X(WidePeekImageAndText02, "ITTTTT")                                                                                \
    X(WidePeekImageCollection01, "TT")                                                                                 \
    X(WidePeekImageCollection02, "TTTTT")                                                                              \
    X(WidePeekImageCollection03, "T")                                                                                  \
    X(WidePeekImageCollection04, "T")                                                                                  \
    X(WidePeekImageCollection05, "ITT")                                                                                \
    X(WidePeekImageCollection06, "IT")                                                                                 \
    X(WideSmallImageAndText01, "IT")                                                                                   \
    X(WideSmallImageAndText02, "ITTTTT")                                                                               \
    X(WideSmallImageAndText03, "IT")                                                                                   \
    X(WideSmallImageAndText04, "ITT")                                                                                  \
    X(WideSmallImageAndText05, "ITT")                                                                                  \
    X(WideText01, "TTTTT")                                                                                             \
    X(WideText02, "TTTTTTTTT")                                                                                         \
    X(WideText03, "T")                                                                                                 \
    X(WideText04, "T")                                                                                                 \
    X(WideText05, "TTTTT")                                                                                             \
    X(WideText06, "TTTTTTTTTT")                                                                                        \
    X(WideText07, "TTTTTTTTT")                                                                                         \
    X(WideText08, "TTTTTTTTTT")                                                                                        \
    X(WideText09, "TT")                                                                                                \
    X(WideText10, "TTTTTTTTT")                                                                                         \
    X(WideText11, "TTTTTTTTTT")
#define APPX_TILE_TEMPLATES81(X)                                                                                       \
    X(Square150x150Block, "TT")                                                                                        \
    X(Square150x150Image, "I")                                                                                         \
    X(Square150x150PeekImageAndText01, "ITTTT")                                                                        \
    X(Square150x150PeekImageAndText02, "ITT")                                                                          \
    X(Square150x150PeekImageAndText03, "ITTTT")                                                                        \
    X(Square150x150PeekImageAndText04, "IT")                                                                           \
    X(Square150x150Text01, "TTTT")                                                                                     \
    X(Square150x150Text02, "TT")                                                                                       \
    X(Square150x150Text03, "TTTT")                                                                                     \
    X(Square150x150Text04, "T")                                                                                        \
    X(Square210x210IconWithBadge, "I")                                                                                 \
    X(Square310x310BlockAndText01, "TTTTTTTTT")                                                                        \
    X(Square310x310BlockAndText02, "ITTTTTTT")                                                                         \
    X(Square310x310Image, "I")                                                                                         \
    X(Square310x310ImageAndText01, "IT")                                                                               \
    X(Square310x310ImageAndText02, "ITT")                                                                              \
    X(Square310x310ImageAndTextOverlay01, "IT")                                                                        \
    X(Square310x310ImageAndTextOverlay02, "ITT")                                                                       \
    X(Square310x310ImageAndTextOverlay03, "ITTTT")                                                                     \
    X(Square310x310ImageCollection, "IIIII")                                                                           \
    X(Square310x310ImageCollectionAndText01, "IIIIIT")                                                                 \
    X(Square310x310ImageCollectionAndText02, "IIIIITT")                                                                \
    X(Square310x310SmallImageAndText01, "ITTT")                                                                        \
    X(Square310x310SmallImagesAndTextList01, "ITTTITTTITTT")                                                           \
    X(Square310x310SmallImagesAndTextList02, "ITITIT")                                                                 \
    X(Square310x310SmallImagesAndTextList03, "ITTITTITT")                                                              \
    X(Square310x310SmallImagesAndTextList04, "ITTITTITT")                                                              \
    X(Square310x310SmallImagesAndTextList05, "TITTITTITT")                                                             \
    X(Square310x310Text01, "TTTTTTTTTT")                                                                               \
    X(Square310x310Text02, "TTTTTTTTTTTTTTTTTTT")                                                                      \
    X(Square310x310Text03, "TTTTTTTTTTT")                                                                              \
    X(Square310x310Text04, "TTTTTTTTTTTTTTTTTTTTTT")                                                                   \
    X(Square310x310Text05, "")                                                                                         \
    X(Square310x310Text06, "")                                                                                         \
    X(Square310x310Text07, "")                                                                                         \
    X(Square310x310Text08, "")                                                                                         \
    X(Square310x310Text09, "TTTTT")                                                                                    \
    X(Square310x310TextList01, "TTTTTTTTT")                                                                            \
    X(Square310x310TextList02, "TTT")                                                                                  \
    X(Square310x310TextList03, "TTTTTT")                                                                               \
    X(Square99x99IconWithBadge, "I")                                                                                   \
    X(Wide310x150BlockAndText01, "TTTTTT")                                                                             \
    X(Wide310x150BlockAndText02, "TTT")                                                                                \
    X(Wide310x150Image, "I")                                                                                           \
    X(Wide310x150ImageAndText01, "IT")                                                                                 \
    X(Wide310x150ImageAndText02, "ITT")                                                                                \
    X(Wide310x150ImageCollection, "IIIII")                                                                             \
    X(Wide310x150PeekImage01, "ITT")                                                                                   \
    X(Wide310x150PeekImage02, "ITTTTT")                                                                                \
    X(Wide310x150PeekImage03, "IT")                                                                                    \
    X(Wide310x150PeekImage04, "IT")                                                                                    \
    X(Wide310x150PeekImage05, "IITT")                                                                                  \
    X(Wide310x150PeekImage06, "IIT")                                                                                   \
    X(Wide310x150PeekImageAndText01, "IT")                                                                             \
    X(Wide310x150PeekImageAndText02, "ITTTTT")                                                                         \
    X(Wide310x150PeekImageCollection01, "TT")                                                                          \
    X(Wide310x150PeekImageCollection02, "TTTTT")                                                                       \
    X(Wide310x150PeekImageCollection03, "T")                                                                           \
    X(Wide310x150PeekImageCollection04, "T")                                                                           \
    X(Wide310x150PeekImageCollection05, "ITT")                                                                         \
    X(Wide310x150PeekImageCollection06, "IT")                                                                          \
    X(Wide310x150SmallImageAndText01, "IT")                                                                            \
    X(Wide310x150SmallImageAndText02, "ITTTTT")                                                                        \
    X(Wide310x150SmallImageAndText03, "IT")                                                                            \
    X(Wide310x150SmallImageAndText04, "ITT")                                                                           \
    X(Wide310x150SmallImageAndText05, "ITT")                                                                           \
    X(Wide310x150Text01, "TTTTT")                                                                                      \
    X(Wide310x150Text02, "TTTTTTTTT")                                                                                  \
    X(Wide310x150Text03, "T")                                                                                          \
    X(Wide310x150Text04, "T")                                                                                          \
    X(Wide310x150Text05, "TTTTT")                                                                                      \
    X(Wide310x150Text06, "TTTTTTTTTT")                                                                                 \
    X(Wide310x150Text07, "TTTTTTTTT")                                                                                  \
    X(Wide310x150Text08, "TTTTTTTTTT")                                                                                 \
    X(Wide310x150Text09, "TT")                                                                                         \
    X(Wide310x150Text10, "TTTTTTTTT")                                                                                  \
    X(Wide310x150Text11, "TTTTTTTTTT")                                                                                 \
    X(Wide432x210IconWithBadgeAndText, "ITTT")
#define APPX_DECLARE_TEMPLATE(name, fields)                                                                            \
    virtual HRESULT STDMETHODCALLTYPE CreateTile##name(IInspectable** value) = 0;
MIDL_INTERFACE("D77B1B1E-207F-536B-7D0A-91E9CDDB8CD5")
ITileContentFactoryStatic : public IInspectable{APPX_TILE_TEMPLATES(APPX_DECLARE_TEMPLATE)};
// Internal IID: the public factory IID is shared with Windows 8.
MIDL_INTERFACE("266D4F5F-F1CA-4D80-AF55-196C62807B2B")
ITileContentFactoryStatic81 : public IInspectable{APPX_TILE_TEMPLATES81(APPX_DECLARE_TEMPLATE)};
#undef APPX_DECLARE_TEMPLATE
// Private: a wide tile reads the <binding> of the square tile it carries.
struct __declspec(uuid("bd02df64-3a6f-4625-9761-bc33d4ff0ab7")) ITileBinding : public IInspectable {
    virtual std::wstring STDMETHODCALLTYPE Binding() = 0;
};

std::wstring Text(HSTRING value) {
    UINT32 length{};
    const auto* raw = WindowsGetStringRawBuffer(value, &length);
    return std::wstring(raw, length);
}
HRESULT CopyText(const std::wstring& text, HSTRING* value) {
    if (!value) return E_POINTER;
    return WindowsCreateString(text.data(), static_cast<UINT32>(text.size()), value);
}
HRESULT CopyFlag(bool flag, boolean* value) {
    if (!value) return E_POINTER;
    *value = flag;
    return S_OK;
}
std::wstring Escape(const std::wstring& value) {
    std::wstring out;
    for (auto c : value) {
        if (c == L'&')
            out += L"&amp;";
        else if (c == L'<')
            out += L"&lt;";
        else if (c == L'"')
            out += L"&quot;";
        else
            out += c;
    }
    return out;
}
std::wstring Attribute(const wchar_t* name, const std::wstring& value) {
    return std::wstring(L" ") + name + L"=\"" + Escape(value) + L"\"";
}

class Image final : public RuntimeClass<RuntimeClassFlags<WinRt>, INotificationContentImage, FtmBase> {
    InspectableClass(L"NotificationsExtensions.NotificationContentImage", BaseTrust);
    std::wstring src_, alt_;
    bool query_ = false;

public:
    HRESULT STDMETHODCALLTYPE get_Src(HSTRING* value) override { return CopyText(src_, value); }
    HRESULT STDMETHODCALLTYPE put_Src(HSTRING value) override {
        src_ = Text(value);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Alt(HSTRING* value) override { return CopyText(alt_, value); }
    HRESULT STDMETHODCALLTYPE put_Alt(HSTRING value) override {
        alt_ = Text(value);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_AddImageQuery(boolean* value) override { return CopyFlag(query_, value); }
    HRESULT STDMETHODCALLTYPE put_AddImageQuery(boolean value) override {
        query_ = value;
        return S_OK;
    }
    std::wstring Element(int id) const {
        std::wstring out = L"<image id=\"" + std::to_wstring(id) + L"\"" + Attribute(L"src", src_);
        if (!alt_.empty()) out += Attribute(L"alt", alt_);
        if (query_) out += L" addImageQuery=\"true\"";
        return out + L"/>";
    }
};

class TextField final : public RuntimeClass<RuntimeClassFlags<WinRt>, INotificationContentText, FtmBase> {
    InspectableClass(L"NotificationsExtensions.NotificationContentText", BaseTrust);
    std::wstring text_, lang_;

public:
    HRESULT STDMETHODCALLTYPE get_Text(HSTRING* value) override { return CopyText(text_, value); }
    HRESULT STDMETHODCALLTYPE put_Text(HSTRING value) override {
        text_ = Text(value);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Lang(HSTRING* value) override { return CopyText(lang_, value); }
    HRESULT STDMETHODCALLTYPE put_Lang(HSTRING value) override {
        lang_ = Text(value);
        return S_OK;
    }
    std::wstring Element(int id) const {
        std::wstring out = L"<text id=\"" + std::to_wstring(id) + L"\"";
        if (!lang_.empty()) out += Attribute(L"lang", lang_);
        return out + L">" + Escape(text_) + L"</text>";
    }
};

// Windows' own notification, which the offline TileUpdater accepts and
// discards. Wine has no XmlDocument, so there Content is null.
class Notification final : public RuntimeClass<RuntimeClassFlags<WinRt>, ITileNotification, FtmBase> {
    InspectableClass(L"Windows.UI.Notifications.TileNotification", BaseTrust);
    ComPtr<IXmlDocument> content_;
    ComPtr<__FIReference_1_Windows__CFoundation__CDateTime> expiration_;
    Wrappers::HString tag_;

public:
    explicit Notification(IXmlDocument* content) : content_(content) {}
    HRESULT STDMETHODCALLTYPE get_Content(IXmlDocument** value) override {
        if (!value) return E_POINTER;
        return content_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE put_ExpirationTime(__FIReference_1_Windows__CFoundation__CDateTime* value) override {
        expiration_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ExpirationTime(__FIReference_1_Windows__CFoundation__CDateTime** value) override {
        if (!value) return E_POINTER;
        return expiration_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE put_Tag(HSTRING value) override { return tag_.Set(value); }
    HRESULT STDMETHODCALLTYPE get_Tag(HSTRING* value) override {
        if (!value) return E_POINTER;
        return tag_.CopyTo(value);
    }
};

struct TemplateInfo {
    const wchar_t* name; // after "Tile"
    const char* fields;
};
#define APPX_TEMPLATE_INFO(name, fields) {L#name, fields},
constexpr TemplateInfo templates[] = {APPX_TILE_TEMPLATES(APPX_TEMPLATE_INFO)
                                          APPX_TILE_TEMPLATES81(APPX_TEMPLATE_INFO)};
#undef APPX_TEMPLATE_INFO
#define APPX_TEMPLATE_INDEX(name, fields) Template##name,
enum TemplateIndex { APPX_TILE_TEMPLATES(APPX_TEMPLATE_INDEX) APPX_TILE_TEMPLATES81(APPX_TEMPLATE_INDEX) };
#undef APPX_TEMPLATE_INDEX
// Windows 8 IIDs known for template interfaces (Asphalt 7: Heat, ilomilo plus).
struct KnownTemplate {
    TemplateIndex index;
    GUID iid;
};
constexpr KnownTemplate knownTemplates[] = {
    // Windows 8.1 dimension-qualified template interfaces.
    {TemplateSquare150x150Block, {0xd25692a3, 0x09f5, 0x5f6d, {0x66, 0xc5, 0x2d, 0xd6, 0xdd, 0x4f, 0xf0, 0x63}}},
    {TemplateSquare150x150Image, {0xd56f7443, 0x4e39, 0x5128, {0x7b, 0x8a, 0xc6, 0xa9, 0x0b, 0x67, 0xa0, 0xbe}}},
    {TemplateSquare150x150PeekImageAndText01,
     {0x8450aaa2, 0x0100, 0x50b0, {0x43, 0xc8, 0xa1, 0xa2, 0x7b, 0x93, 0x41, 0xa4}}},
    {TemplateSquare150x150PeekImageAndText02,
     {0xa9ab5c0b, 0xe82e, 0x509d, {0x43, 0x21, 0x05, 0x97, 0x16, 0xb6, 0x02, 0x39}}},
    {TemplateSquare150x150PeekImageAndText03,
     {0x4cba1483, 0x115b, 0x5129, {0x6e, 0xc8, 0xde, 0x22, 0xbe, 0x5f, 0xfb, 0x6d}}},
    {TemplateSquare150x150PeekImageAndText04,
     {0x74cd3540, 0xf19a, 0x5a35, {0x44, 0x52, 0x82, 0xe1, 0x04, 0x11, 0xc4, 0x17}}},
    {TemplateSquare150x150Text01, {0x68e7e946, 0xeac4, 0x584e, {0x61, 0xe9, 0x0e, 0x49, 0x55, 0xbe, 0x32, 0x6d}}},
    {TemplateSquare150x150Text02, {0x53c1d2ea, 0xa2b3, 0x55d1, {0x5e, 0xce, 0xc5, 0x5d, 0xa6, 0x40, 0xa1, 0xa4}}},
    {TemplateSquare150x150Text03, {0x8cee5dc0, 0x4aa6, 0x5110, {0x66, 0xc1, 0xc9, 0xfb, 0xa0, 0x51, 0x5f, 0x69}}},
    {TemplateSquare150x150Text04, {0xc3ee0578, 0x51db, 0x5e43, {0x7a, 0xed, 0x43, 0x88, 0xbb, 0x00, 0x45, 0x1b}}},
    {TemplateSquare210x210IconWithBadge,
     {0xecc772c1, 0xb195, 0x5f40, {0x4b, 0x45, 0xf0, 0xfc, 0x36, 0xa4, 0x7b, 0xdd}}},
    {TemplateSquare310x310BlockAndText01,
     {0x5b0c7bcf, 0x6e20, 0x5a14, {0x59, 0xd6, 0x7e, 0x56, 0x8d, 0x73, 0x70, 0xc4}}},
    {TemplateSquare310x310BlockAndText02,
     {0x2148156d, 0x06a5, 0x5737, {0x7d, 0xb5, 0x60, 0xa9, 0xf0, 0xd6, 0x15, 0x4a}}},
    {TemplateSquare310x310Image, {0x4453c1b2, 0x5a0f, 0x5068, {0x52, 0x18, 0x8a, 0xbf, 0xa4, 0x91, 0x21, 0x87}}},
    {TemplateSquare310x310ImageAndText01,
     {0x44c15b72, 0xd7e2, 0x5eb8, {0x76, 0x03, 0xf4, 0xf9, 0xd9, 0xc5, 0x13, 0xa6}}},
    {TemplateSquare310x310ImageAndText02,
     {0xc562333f, 0x1627, 0x5e4c, {0x4d, 0xa9, 0x4e, 0xfa, 0x75, 0x0a, 0x68, 0x91}}},
    {TemplateSquare310x310ImageAndTextOverlay01,
     {0x48dadd05, 0x5003, 0x5189, {0x72, 0x8b, 0xa5, 0x96, 0x12, 0x04, 0xd5, 0xd0}}},
    {TemplateSquare310x310ImageAndTextOverlay02,
     {0xb3b4865d, 0xd496, 0x54e1, {0x43, 0x69, 0xa2, 0x95, 0x5f, 0xc2, 0x9e, 0x4f}}},
    {TemplateSquare310x310ImageAndTextOverlay03,
     {0xd9667268, 0x0f24, 0x5be2, {0x70, 0xd9, 0xd1, 0x05, 0xa4, 0xf8, 0xde, 0xf7}}},
    {TemplateSquare310x310ImageCollection,
     {0x3331a4a7, 0x72f1, 0x50a5, {0x7f, 0x1b, 0x4a, 0xd3, 0x1f, 0x09, 0x07, 0xa7}}},
    {TemplateSquare310x310ImageCollectionAndText01,
     {0xa6d411a8, 0xf8fa, 0x5ebd, {0x49, 0x88, 0xfd, 0x17, 0x6b, 0x38, 0x0c, 0x13}}},
    {TemplateSquare310x310ImageCollectionAndText02,
     {0xf4e846d2, 0xf14e, 0x5441, {0x45, 0xe0, 0xd9, 0x98, 0x6d, 0xb3, 0x0e, 0x24}}},
    {TemplateSquare310x310SmallImageAndText01,
     {0xb4246eed, 0x3464, 0x5148, {0x5e, 0x2f, 0x62, 0xcc, 0xae, 0x02, 0x6f, 0x28}}},
    {TemplateSquare310x310SmallImagesAndTextList01,
     {0xdfc3ccf1, 0xceab, 0x5252, {0x4e, 0x92, 0xec, 0x67, 0x86, 0x4f, 0x34, 0x03}}},
    {TemplateSquare310x310SmallImagesAndTextList02,
     {0x5c10d9fc, 0x6725, 0x5336, {0x7b, 0x25, 0xe7, 0xfe, 0x76, 0xcf, 0x8c, 0x75}}},
    {TemplateSquare310x310SmallImagesAndTextList03,
     {0x350a0044, 0x56fe, 0x5a3c, {0x4b, 0x38, 0x4d, 0x02, 0xff, 0x66, 0x84, 0x89}}},
    {TemplateSquare310x310SmallImagesAndTextList04,
     {0x15ebb2b7, 0x8eb3, 0x54e5, {0x7d, 0x32, 0xee, 0x21, 0x66, 0xfb, 0x13, 0x82}}},
    {TemplateSquare310x310SmallImagesAndTextList05,
     {0x50accbf5, 0xe75b, 0x5a86, {0x6c, 0x73, 0x29, 0x54, 0xa2, 0x32, 0xe5, 0x2e}}},
    {TemplateSquare310x310Text01, {0x0687e772, 0x17b1, 0x5545, {0x58, 0xad, 0x75, 0x26, 0x2d, 0x2e, 0x80, 0xb2}}},
    {TemplateSquare310x310Text02, {0xef210979, 0x8aa6, 0x5fd3, {0x45, 0xaf, 0x3f, 0xd9, 0xd8, 0x11, 0x5a, 0xa2}}},
    {TemplateSquare310x310Text03, {0x1e818a24, 0x3683, 0x5ef1, {0x73, 0x6e, 0x2c, 0x95, 0x59, 0xee, 0xea, 0xf9}}},
    {TemplateSquare310x310Text04, {0xab7b3fee, 0x5964, 0x5daf, {0x67, 0x47, 0x30, 0x72, 0x79, 0x00, 0x8d, 0x78}}},
    {TemplateSquare310x310Text05, {0xc9fbfc61, 0x5673, 0x5210, {0x64, 0x3c, 0x1c, 0x5e, 0x23, 0x8a, 0x78, 0x5f}}},
    {TemplateSquare310x310Text06, {0x114e083e, 0x37bf, 0x58be, {0x6e, 0x60, 0x87, 0x22, 0x0e, 0xbf, 0xa9, 0x23}}},
    {TemplateSquare310x310Text07, {0x3d7e6b7f, 0x7401, 0x503a, {0x68, 0x85, 0xc4, 0x17, 0xd6, 0xc4, 0x5a, 0x6a}}},
    {TemplateSquare310x310Text08, {0x40128904, 0x5157, 0x5eef, {0x41, 0xb2, 0xa4, 0xee, 0x19, 0x37, 0x6c, 0x27}}},
    {TemplateSquare310x310Text09, {0x6063ce51, 0x96e1, 0x53a0, {0x65, 0xb7, 0x42, 0xa4, 0x5f, 0xbd, 0xc5, 0x6b}}},
    {TemplateSquare310x310TextList01, {0xbdeeb13d, 0x31ab, 0x579b, {0x53, 0x6d, 0x5e, 0x02, 0xa6, 0xbb, 0x35, 0xae}}},
    {TemplateSquare310x310TextList02, {0x25941e01, 0xfa54, 0x51de, {0x7e, 0xaf, 0x91, 0x1d, 0x08, 0x31, 0xe4, 0x2c}}},
    {TemplateSquare310x310TextList03, {0xf14e95c9, 0x4b0e, 0x5d61, {0x76, 0x74, 0xc7, 0x5a, 0x84, 0x83, 0xde, 0xd3}}},
    {TemplateSquare99x99IconWithBadge, {0x0f725a67, 0x186f, 0x5ccf, {0x6b, 0x4e, 0x66, 0xa8, 0xd6, 0x00, 0x9f, 0xdb}}},
    {TemplateWide310x150BlockAndText01, {0x0f1c43d2, 0x5ca7, 0x53f0, {0x6d, 0x82, 0x40, 0x70, 0xcc, 0x1a, 0xce, 0xa1}}},
    {TemplateWide310x150BlockAndText02, {0x47ce7a80, 0x9080, 0x5bba, {0x4a, 0x10, 0x9a, 0xed, 0x1d, 0xbd, 0x3d, 0xef}}},
    {TemplateWide310x150Image, {0x92c03ae3, 0x7546, 0x5858, {0x54, 0x2c, 0xc4, 0x7c, 0x4c, 0xc6, 0xa6, 0xd0}}},
    {TemplateWide310x150ImageAndText01, {0x2cbb03e1, 0x74a3, 0x5709, {0x4d, 0x50, 0x89, 0x96, 0x30, 0xbd, 0x85, 0x66}}},
    {TemplateWide310x150ImageAndText02, {0xa7d3a222, 0x3726, 0x518a, {0x59, 0xcb, 0x7c, 0x92, 0xfc, 0xdc, 0x59, 0xd2}}},
    {TemplateWide310x150ImageCollection,
     {0xf06fec34, 0xf9cf, 0x5fb1, {0x75, 0x16, 0x3a, 0x16, 0xdf, 0x12, 0xcd, 0xc9}}},
    {TemplateWide310x150PeekImage01, {0x2c678764, 0x09d0, 0x546b, {0x56, 0x75, 0x06, 0xd9, 0x03, 0x6e, 0x63, 0x27}}},
    {TemplateWide310x150PeekImage02, {0x5d3d99f5, 0x2af6, 0x5f99, {0x4a, 0x7a, 0x6f, 0xa6, 0xaa, 0x32, 0xa8, 0x96}}},
    {TemplateWide310x150PeekImage03, {0xa61dcc58, 0xdb7e, 0x5588, {0x69, 0xb0, 0x94, 0x49, 0xd9, 0x89, 0x5a, 0xf7}}},
    {TemplateWide310x150PeekImage04, {0xc4240f2c, 0x42dc, 0x594f, {0x5a, 0x4d, 0xe3, 0x1b, 0x37, 0x61, 0xe0, 0x8e}}},
    {TemplateWide310x150PeekImage05, {0x9ed4d977, 0x165d, 0x5c74, {0x6d, 0x83, 0xa5, 0xf9, 0x9b, 0x9b, 0x8f, 0x31}}},
    {TemplateWide310x150PeekImage06, {0x7ceb0d59, 0xb43b, 0x5192, {0x4b, 0xbd, 0x92, 0x71, 0x34, 0xc5, 0x55, 0xd4}}},
    {TemplateWide310x150PeekImageAndText01,
     {0x7a38a6e2, 0x1b2d, 0x589e, {0x6d, 0x38, 0xc5, 0x65, 0xbf, 0x1e, 0xfb, 0xb3}}},
    {TemplateWide310x150PeekImageAndText02,
     {0x0f337137, 0x9edf, 0x598d, {0x7c, 0x20, 0x29, 0x82, 0x74, 0x5a, 0x43, 0xf4}}},
    {TemplateWide310x150PeekImageCollection01,
     {0xd53fc0d8, 0x91d2, 0x576a, {0x68, 0x20, 0xcc, 0xa2, 0x54, 0x8b, 0x50, 0x82}}},
    {TemplateWide310x150PeekImageCollection02,
     {0x080ec632, 0xad91, 0x57c2, {0x63, 0xe2, 0x10, 0x63, 0xe7, 0x0c, 0x92, 0xd9}}},
    {TemplateWide310x150PeekImageCollection03,
     {0xc7c3a15b, 0x0f5d, 0x5460, {0x65, 0x83, 0xca, 0x35, 0x7e, 0xda, 0x03, 0x17}}},
    {TemplateWide310x150PeekImageCollection04,
     {0xe15a4508, 0x95d5, 0x5e23, {0x61, 0x36, 0xe7, 0xdc, 0xd7, 0x17, 0x6d, 0x20}}},
    {TemplateWide310x150PeekImageCollection05,
     {0x5ea282de, 0xcd0b, 0x5e7e, {0x41, 0x2c, 0x75, 0xbc, 0x50, 0x02, 0x5f, 0xb6}}},
    {TemplateWide310x150PeekImageCollection06,
     {0xe75b21e3, 0x7e23, 0x543e, {0x41, 0x36, 0x4d, 0x48, 0x74, 0x70, 0xcc, 0x9a}}},
    {TemplateWide310x150SmallImageAndText01,
     {0x80495218, 0xe5c6, 0x5b27, {0x75, 0x14, 0x54, 0x26, 0xf8, 0xdf, 0x15, 0x62}}},
    {TemplateWide310x150SmallImageAndText02,
     {0x6efe2d1e, 0xdc42, 0x577a, {0x6b, 0x62, 0xe4, 0xdd, 0xf0, 0x50, 0x93, 0xfb}}},
    {TemplateWide310x150SmallImageAndText03,
     {0x6418b152, 0xa4a4, 0x534c, {0x63, 0x97, 0x90, 0x4b, 0x0e, 0x68, 0x5a, 0xe6}}},
    {TemplateWide310x150SmallImageAndText04,
     {0x11895a4a, 0x9791, 0x5165, {0x75, 0xcb, 0xa6, 0xef, 0x2c, 0x38, 0xba, 0xd0}}},
    {TemplateWide310x150SmallImageAndText05,
     {0xa758d466, 0x4463, 0x5ae5, {0x52, 0x55, 0x8f, 0x7a, 0x6b, 0x31, 0xdc, 0x43}}},
    {TemplateWide310x150Text01, {0xb1ee7ca6, 0x2528, 0x5589, {0x6e, 0x39, 0x3e, 0x0e, 0xdd, 0x6c, 0x84, 0x88}}},
    {TemplateWide310x150Text02, {0x6ee14b20, 0x739b, 0x5110, {0x74, 0x64, 0x4a, 0x24, 0x0a, 0xf6, 0x68, 0x15}}},
    {TemplateWide310x150Text03, {0xdb9fe00a, 0x30c6, 0x573f, {0x53, 0xd0, 0x86, 0xb4, 0x70, 0x56, 0x5c, 0xa7}}},
    {TemplateWide310x150Text04, {0x94463e5a, 0x07d8, 0x53ad, {0x71, 0x1a, 0xae, 0x07, 0x73, 0xbe, 0x79, 0xcc}}},
    {TemplateWide310x150Text05, {0x32f086f6, 0x882a, 0x5666, {0x5f, 0xe1, 0xc0, 0x8b, 0xef, 0x3c, 0xb0, 0x1f}}},
    {TemplateWide310x150Text06, {0xaf51220a, 0xbeee, 0x5121, {0x68, 0x91, 0x01, 0xba, 0xae, 0x26, 0xd6, 0x32}}},
    {TemplateWide310x150Text07, {0xbc624a6a, 0x51f2, 0x5bd3, {0x51, 0x64, 0x58, 0xdf, 0x9b, 0xb4, 0x15, 0xdc}}},
    {TemplateWide310x150Text08, {0xb7578493, 0x1537, 0x5f22, {0x6d, 0x1e, 0xb4, 0xd5, 0x41, 0x2b, 0xf2, 0x06}}},
    {TemplateWide310x150Text09, {0x3729afad, 0x3ddf, 0x51b5, {0x58, 0x22, 0x39, 0xd1, 0xd7, 0x9a, 0x6a, 0x33}}},
    {TemplateWide310x150Text10, {0xd9e3784e, 0xb726, 0x5a6e, {0x4e, 0x4b, 0x0f, 0x6f, 0x9f, 0x49, 0xb7, 0x5c}}},
    {TemplateWide310x150Text11, {0xfcbc6b46, 0xcdb9, 0x56ef, {0x41, 0x89, 0x9a, 0xcf, 0xdf, 0x5e, 0x81, 0x3f}}},
    {TemplateWide432x210IconWithBadgeAndText,
     {0xea45bc5c, 0x77e8, 0x5801, {0x61, 0x1f, 0xe0, 0xdc, 0x7d, 0x15, 0x02, 0x98}}},
    {TemplateSquareImage, {0xd56f7443, 0x4e39, 0x5128, {0x7b, 0x8a, 0xc6, 0xa9, 0x0b, 0x67, 0xa0, 0xbe}}},
    {TemplateSquarePeekImageAndText01, {0x8450aaa2, 0x0100, 0x50b0, {0x43, 0xc8, 0xa1, 0xa2, 0x7b, 0x93, 0x41, 0xa4}}},
    {TemplateSquarePeekImageAndText02, {0xa9ab5c0b, 0xe82e, 0x509d, {0x43, 0x21, 0x05, 0x97, 0x16, 0xb6, 0x02, 0x39}}},
    {TemplateSquarePeekImageAndText03, {0x4cba1483, 0x115b, 0x5129, {0x6e, 0xc8, 0xde, 0x22, 0xbe, 0x5f, 0xfb, 0x6d}}},
    {TemplateSquarePeekImageAndText04, {0x74cd3540, 0xf19a, 0x5a35, {0x44, 0x52, 0x82, 0xe1, 0x04, 0x11, 0xc4, 0x17}}},
    {TemplateSquareText01, {0x68e7e946, 0xeac4, 0x584e, {0x61, 0xe9, 0x0e, 0x49, 0x55, 0xbe, 0x32, 0x6d}}},
    {TemplateSquareText02, {0x53c1d2ea, 0xa2b3, 0x55d1, {0x5e, 0xce, 0xc5, 0x5d, 0xa6, 0x40, 0xa1, 0xa4}}},
    {TemplateSquareText03, {0x8cee5dc0, 0x4aa6, 0x5110, {0x66, 0xc1, 0xc9, 0xfb, 0xa0, 0x51, 0x5f, 0x69}}},
    {TemplateSquareText04, {0xc3ee0578, 0x51db, 0x5e43, {0x7a, 0xed, 0x43, 0x88, 0xbb, 0x00, 0x45, 0x1b}}},
    {TemplateWideBlockAndText01, {0x0f1c43d2, 0x5ca7, 0x53f0, {0x6d, 0x82, 0x40, 0x70, 0xcc, 0x1a, 0xce, 0xa1}}},
    {TemplateWideBlockAndText02, {0x47ce7a80, 0x9080, 0x5bba, {0x4a, 0x10, 0x9a, 0xed, 0x1d, 0xbd, 0x3d, 0xef}}},
    {TemplateWideImage, {0x92c03ae3, 0x7546, 0x5858, {0x54, 0x2c, 0xc4, 0x7c, 0x4c, 0xc6, 0xa6, 0xd0}}},
    {TemplateWideImageAndText01, {0x2cbb03e1, 0x74a3, 0x5709, {0x4d, 0x50, 0x89, 0x96, 0x30, 0xbd, 0x85, 0x66}}},
    {TemplateWideImageAndText02, {0xa7d3a222, 0x3726, 0x518a, {0x59, 0xcb, 0x7c, 0x92, 0xfc, 0xdc, 0x59, 0xd2}}},
    {TemplateWideImageCollection, {0xf06fec34, 0xf9cf, 0x5fb1, {0x75, 0x16, 0x3a, 0x16, 0xdf, 0x12, 0xcd, 0xc9}}},
    {TemplateWidePeekImage01, {0x2c678764, 0x09d0, 0x546b, {0x56, 0x75, 0x06, 0xd9, 0x03, 0x6e, 0x63, 0x27}}},
    {TemplateWidePeekImage02, {0x5d3d99f5, 0x2af6, 0x5f99, {0x4a, 0x7a, 0x6f, 0xa6, 0xaa, 0x32, 0xa8, 0x96}}},
    {TemplateWidePeekImage03, {0xa61dcc58, 0xdb7e, 0x5588, {0x69, 0xb0, 0x94, 0x49, 0xd9, 0x89, 0x5a, 0xf7}}},
    {TemplateWidePeekImage04, {0xc4240f2c, 0x42dc, 0x594f, {0x5a, 0x4d, 0xe3, 0x1b, 0x37, 0x61, 0xe0, 0x8e}}},
    {TemplateWidePeekImage05, {0x9ed4d977, 0x165d, 0x5c74, {0x6d, 0x83, 0xa5, 0xf9, 0x9b, 0x9b, 0x8f, 0x31}}},
    {TemplateWidePeekImage06, {0x7ceb0d59, 0xb43b, 0x5192, {0x4b, 0xbd, 0x92, 0x71, 0x34, 0xc5, 0x55, 0xd4}}},
    {TemplateWidePeekImageAndText01, {0x7a38a6e2, 0x1b2d, 0x589e, {0x6d, 0x38, 0xc5, 0x65, 0xbf, 0x1e, 0xfb, 0xb3}}},
    {TemplateWidePeekImageAndText02, {0x0f337137, 0x9edf, 0x598d, {0x7c, 0x20, 0x29, 0x82, 0x74, 0x5a, 0x43, 0xf4}}},
    {TemplateWidePeekImageCollection01, {0xd53fc0d8, 0x91d2, 0x576a, {0x68, 0x20, 0xcc, 0xa2, 0x54, 0x8b, 0x50, 0x82}}},
    {TemplateWidePeekImageCollection02, {0x080ec632, 0xad91, 0x57c2, {0x63, 0xe2, 0x10, 0x63, 0xe7, 0x0c, 0x92, 0xd9}}},
    {TemplateWidePeekImageCollection03, {0xc7c3a15b, 0x0f5d, 0x5460, {0x65, 0x83, 0xca, 0x35, 0x7e, 0xda, 0x03, 0x17}}},
    {TemplateWidePeekImageCollection04, {0xe15a4508, 0x95d5, 0x5e23, {0x61, 0x36, 0xe7, 0xdc, 0xd7, 0x17, 0x6d, 0x20}}},
    {TemplateWidePeekImageCollection05, {0x5ea282de, 0xcd0b, 0x5e7e, {0x41, 0x2c, 0x75, 0xbc, 0x50, 0x02, 0x5f, 0xb6}}},
    {TemplateWidePeekImageCollection06, {0xe75b21e3, 0x7e23, 0x543e, {0x41, 0x36, 0x4d, 0x48, 0x74, 0x70, 0xcc, 0x9a}}},
    {TemplateWideSmallImageAndText01, {0x80495218, 0xe5c6, 0x5b27, {0x75, 0x14, 0x54, 0x26, 0xf8, 0xdf, 0x15, 0x62}}},
    {TemplateWideSmallImageAndText02, {0x6efe2d1e, 0xdc42, 0x577a, {0x6b, 0x62, 0xe4, 0xdd, 0xf0, 0x50, 0x93, 0xfb}}},
    {TemplateWideSmallImageAndText03, {0x6418b152, 0xa4a4, 0x534c, {0x63, 0x97, 0x90, 0x4b, 0x0e, 0x68, 0x5a, 0xe6}}},
    {TemplateWideSmallImageAndText04, {0x11895a4a, 0x9791, 0x5165, {0x75, 0xcb, 0xa6, 0xef, 0x2c, 0x38, 0xba, 0xd0}}},
    {TemplateWideSmallImageAndText05, {0xa758d466, 0x4463, 0x5ae5, {0x52, 0x55, 0x8f, 0x7a, 0x6b, 0x31, 0xdc, 0x43}}},
    {TemplateWideText01, {0xb1ee7ca6, 0x2528, 0x5589, {0x6e, 0x39, 0x3e, 0x0e, 0xdd, 0x6c, 0x84, 0x88}}},
    {TemplateWideText02, {0x6ee14b20, 0x739b, 0x5110, {0x74, 0x64, 0x4a, 0x24, 0x0a, 0xf6, 0x68, 0x15}}},
    {TemplateWideText03, {0xdb9fe00a, 0x30c6, 0x573f, {0x53, 0xd0, 0x86, 0xb4, 0x70, 0x56, 0x5c, 0xa7}}},
    {TemplateWideText04, {0x94463e5a, 0x07d8, 0x53ad, {0x71, 0x1a, 0xae, 0x07, 0x73, 0xbe, 0x79, 0xcc}}},
    {TemplateWideText05, {0x32f086f6, 0x882a, 0x5666, {0x5f, 0xe1, 0xc0, 0x8b, 0xef, 0x3c, 0xb0, 0x1f}}},
    {TemplateWideText06, {0xaf51220a, 0xbeee, 0x5121, {0x68, 0x91, 0x01, 0xba, 0xae, 0x26, 0xd6, 0x32}}},
    {TemplateWideText07, {0xbc624a6a, 0x51f2, 0x5bd3, {0x51, 0x64, 0x58, 0xdf, 0x9b, 0xb4, 0x15, 0xdc}}},
    {TemplateWideText08, {0xb7578493, 0x1537, 0x5f22, {0x6d, 0x1e, 0xb4, 0xd5, 0x41, 0x2b, 0xf2, 0x06}}},
    {TemplateWideText09, {0x3729afad, 0x3ddf, 0x51b5, {0x58, 0x22, 0x39, 0xd1, 0xd7, 0x9a, 0x6a, 0x33}}},
    {TemplateWideText10, {0xd9e3784e, 0xb726, 0x5a6e, {0x4e, 0x4b, 0x0f, 0x6f, 0x9f, 0x49, 0xb7, 0x5c}}},
    {TemplateWideText11, {0xfcbc6b46, 0xcdb9, 0x56ef, {0x41, 0x89, 0x9a, 0xcf, 0xdf, 0x5e, 0x81, 0x3f}}},
    {TemplateSquareImage, {0xbcaa22f7, 0x9a02, 0x5fbb, {0x60, 0x94, 0xe8, 0x65, 0xcf, 0x95, 0xa0, 0xaf}}},
    {TemplateWideImage, {0x4dca931b, 0x2d24, 0x5435, {0x78, 0xf2, 0x49, 0xf4, 0xc6, 0x84, 0x9a, 0x0d}}},
    {TemplateWideImageAndText02, {0xe20b8c46, 0x7868, 0x5903, {0x7b, 0x2d, 0xc8, 0x0b, 0x95, 0xbf, 0xef, 0xf1}}},
};

// A tile template: its fields, and the notification XML they make. Wide tiles
// also carry the square tile shown when the user makes the tile small.
bool Notifications81() {
    static const bool modern = [] {
        std::ifstream input(CurrentPackage().directory / L"NotificationsExtensions.winmd", std::ios::binary);
        const std::string metadata((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        return metadata.find("get_ContentId") != std::string::npos;
    }();
    return modern;
}
template <class Shape>
class Tile : public RuntimeClass<RuntimeClassFlags<WinRt>, ITileTemplate, ITileNotificationContent,
                                 ITileNotificationContent81, Shape, INotificationContent, ITileBinding, FtmBase> {
    using Base = RuntimeClass<RuntimeClassFlags<WinRt>, ITileTemplate, ITileNotificationContent,
                              ITileNotificationContent81, Shape, INotificationContent, ITileBinding, FtmBase>;
    const TemplateIndex index_;
    const bool modern_ = Notifications81();
    std::wstring contentId_;
    std::vector<ComPtr<Image>> images_;
    std::vector<ComPtr<TextField>> texts_;
    std::vector<ComPtr<IInspectable>> fields_; // in the interface's order
    std::wstring lang_, baseUri_;
    INT32 branding_ = 1; // TileBranding.Logo, the tile's default
    bool strict_ = true, query_ = false;
    HRESULT Field(size_t index, IInspectable** value) {
        if (!value) return E_POINTER;
        *value = nullptr;
        return index < fields_.size() ? fields_[index].CopyTo(value) : E_NOTIMPL;
    }

protected:
    virtual std::wstring Bindings() { return Binding(); }

public:
    explicit Tile(TemplateIndex index) : index_(index) {
        for (const char* kind = templates[index].fields; *kind; ++kind) {
            if (*kind == 'I') {
                images_.push_back(Make<Image>());
                fields_.emplace_back(static_cast<INotificationContentImage*>(images_.back().Get()));
            } else {
                texts_.push_back(Make<TextField>());
                fields_.emplace_back(static_cast<INotificationContentText*>(texts_.back().Get()));
            }
        }
    }
    bool Ready() const {
        for (const auto& field : fields_)
            if (!field) return false;
        return true;
    }
    // Answers the template's interface by a known Windows 8 IID, and logs what
    // a game asks of a tile that this one lacks.
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) noexcept override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (modern_ && iid == __uuidof(ITileNotificationContent)) {
            *value = static_cast<ITileNotificationContent81*>(this);
            static_cast<ITileNotificationContent81*>(this)->AddRef();
            return S_OK;
        }
        constexpr GUID square81 = {0x147f2666, 0x812e, 0x5b08, {0x50, 0xa5, 0xb0, 0xb6, 0xcc, 0xbe, 0x1f, 0xc4}};
        constexpr GUID wide81 = {0xc4ab7b1c, 0xba82, 0x5b98, {0x63, 0xe6, 0xe1, 0xb8, 0x54, 0x31, 0xab, 0xe5}};
        if ((std::is_same_v<Shape, ISquareTileNotificationContent> && iid == square81 &&
             wcsncmp(templates[index_].name, L"Square99x99", 11) != 0) ||
            (std::is_same_v<Shape, IWideTileNotificationContent> && iid == wide81)) {
            *value = static_cast<Shape*>(this);
            static_cast<Shape*>(this)->AddRef();
            return S_OK;
        }
        constexpr GUID shape0 = {0x305219f9, 0x4de9, 0x5f72, {0x4e, 0x4e, 0x87, 0x7c, 0xa9, 0xce, 0x08, 0x4a}};
        constexpr GUID shape1 = {0xd403fd8f, 0xd551, 0x5e8f, {0x4c, 0xd8, 0xc7, 0x50, 0xf6, 0x85, 0x90, 0x12}};
        constexpr GUID shape2 = {0xc2c1cd21, 0x3c84, 0x514c, {0x60, 0x71, 0xda, 0x23, 0xa2, 0xef, 0x59, 0xe4}};
        constexpr GUID shape3 = {0x6e4da40f, 0x1e4b, 0x5219, {0x7d, 0x40, 0xff, 0x7e, 0x40, 0xa9, 0x12, 0x9d}};
        const auto* name = templates[index_].name;
        if constexpr (std::is_same_v<Shape, ILargeTileContent>) {
            if ((wcsncmp(name, L"Square310x310", 13) == 0 && iid == shape0) ||
                (wcsncmp(name, L"Square210x210", 13) == 0 && iid == shape1) ||
                (wcsncmp(name, L"Wide432x210", 11) == 0 && iid == shape2)) {
                *value = static_cast<Shape*>(this);
                static_cast<Shape*>(this)->AddRef();
                return S_OK;
            }
        }
        if constexpr (std::is_same_v<Shape, ISquareTileNotificationContent>) {
            if (wcsncmp(name, L"Square99x99", 11) == 0 && iid == shape3) {
                *value = static_cast<Shape*>(this);
                static_cast<Shape*>(this)->AddRef();
                return S_OK;
            }
        }
        for (const auto& known : knownTemplates) {
            if (known.index != index_ || iid != known.iid || !value) continue;
            *value = static_cast<ITileTemplate*>(this);
            static_cast<ITileTemplate*>(this)->AddRef();
            return S_OK;
        }
        auto hr = Base::QueryInterface(iid, value);
        if (FAILED(hr))
            Log("FAIL", "NotificationsExtensions Tile%ls QI missing %08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                templates[index_].name, iid.Data1, iid.Data2, iid.Data3, iid.Data4[0], iid.Data4[1], iid.Data4[2],
                iid.Data4[3], iid.Data4[4], iid.Data4[5], iid.Data4[6], iid.Data4[7]);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* value) noexcept override {
        return CopyText(std::wstring(L"NotificationsExtensions.TileContent.Tile") + templates[index_].name, value);
    }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* value) noexcept override {
        if (!value) return E_POINTER;
        *value = BaseTrust;
        return S_OK;
    }
#define APPX_FIELD(n)                                                                                                  \
    HRESULT STDMETHODCALLTYPE Field##n(IInspectable** value) override { return Field(n, value); }
    APPX_FIELD(0)
    APPX_FIELD(1)
    APPX_FIELD(2)
    APPX_FIELD(3)
    APPX_FIELD(4)
    APPX_FIELD(5)
    APPX_FIELD(6)
    APPX_FIELD(7)
    APPX_FIELD(8)
    APPX_FIELD(9)
    APPX_FIELD(10)
    APPX_FIELD(11)
    APPX_FIELD(12)
    APPX_FIELD(13)
    APPX_FIELD(14)
    APPX_FIELD(15)
    APPX_FIELD(16)
    APPX_FIELD(17)
    APPX_FIELD(18)
    APPX_FIELD(19)
    APPX_FIELD(20)
    APPX_FIELD(21)
#undef APPX_FIELD
    std::wstring STDMETHODCALLTYPE Binding() override {
        std::wstring out =
            std::wstring(L"<binding") + Attribute(L"template", std::wstring(L"Tile") + templates[index_].name);
        if (!contentId_.empty()) out += Attribute(L"contentId", contentId_);
        out += L">";
        for (size_t i = 0; i < images_.size(); ++i) out += images_[i]->Element(static_cast<int>(i + 1));
        for (size_t i = 0; i < texts_.size(); ++i) out += texts_[i]->Element(static_cast<int>(i + 1));
        return out + L"</binding>";
    }
    HRESULT STDMETHODCALLTYPE get_StrictValidation(boolean* value) override { return CopyFlag(strict_, value); }
    HRESULT STDMETHODCALLTYPE put_StrictValidation(boolean value) override {
        strict_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Lang(HSTRING* value) override { return CopyText(lang_, value); }
    HRESULT STDMETHODCALLTYPE put_Lang(HSTRING value) override {
        lang_ = Text(value);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_BaseUri(HSTRING* value) override { return CopyText(baseUri_, value); }
    HRESULT STDMETHODCALLTYPE put_BaseUri(HSTRING value) override {
        baseUri_ = Text(value);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Branding(INT32* value) override {
        if (!value) return E_POINTER;
        *value = branding_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE put_Branding(INT32 value) override {
        if (value < 0 || value > 2) return E_INVALIDARG;
        branding_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_AddImageQuery(boolean* value) override { return CopyFlag(query_, value); }
    HRESULT STDMETHODCALLTYPE put_AddImageQuery(boolean value) override {
        query_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetContent(HSTRING* value) override {
        std::wstring visual = L"<visual version=\"1\"";
        if (!lang_.empty()) visual += Attribute(L"lang", lang_);
        if (!baseUri_.empty()) visual += Attribute(L"baseUri", baseUri_);
        if (branding_ != 1) visual += branding_ ? L" branding=\"name\"" : L" branding=\"none\"";
        if (query_) visual += L" addImageQuery=\"true\"";
        return CopyText(L"<tile>" + visual + L">" + Bindings() + L"</visual></tile>", value);
    }
    HRESULT STDMETHODCALLTYPE GetXml(IXmlDocument** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        Wrappers::HString xml;
        ComPtr<IInspectable> instance;
        ComPtr<ABI::Windows::Data::Xml::Dom::IXmlDocumentIO> io;
        auto hr = GetContent(xml.GetAddressOf());
        if (SUCCEEDED(hr))
            hr = OsActivateInstance(Wrappers::HStringReference(L"Windows.Data.Xml.Dom.XmlDocument").Get(), &instance);
        if (SUCCEEDED(hr)) hr = instance.As(&io);
        if (SUCCEEDED(hr)) hr = io->LoadXml(xml.Get());
        return SUCCEEDED(hr) ? instance.CopyTo(value) : hr;
    }
    HRESULT STDMETHODCALLTYPE get_ContentId(HSTRING* value) override { return CopyText(contentId_, value); }
    HRESULT STDMETHODCALLTYPE put_ContentId(HSTRING value) override {
        contentId_ = Text(value);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateNotification(ITileNotification** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        ComPtr<IXmlDocument> content;
        const auto hr = GetXml(&content);
        if (FAILED(hr)) Log("TRACE", "NotificationsExtensions: tile without XML content hr=%08lx", hr);
        auto notification = Make<Notification>(content.Get());
        return notification ? notification.CopyTo(value) : E_OUTOFMEMORY;
    }
};
class SquareTile final : public Tile<ISquareTileNotificationContent> {
public:
    explicit SquareTile(TemplateIndex index) : Tile(index) {}
};
class WideTile final : public Tile<IWideTileNotificationContent> {
    ComPtr<ISquareTileNotificationContent> square_;
    bool requireSquare_ = true;

protected:
    std::wstring Bindings() override {
        ComPtr<ITileBinding> square;
        auto bindings = Binding();
        if (square_ && SUCCEEDED(square_.As(&square))) bindings += square->Binding();
        return bindings;
    }

public:
    explicit WideTile(TemplateIndex index) : Tile(index) {}
    HRESULT STDMETHODCALLTYPE get_SquareContent(ISquareTileNotificationContent** value) override {
        if (!value) return E_POINTER;
        return square_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE put_SquareContent(ISquareTileNotificationContent* value) override {
        square_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_RequireSquareContent(boolean* value) override {
        return CopyFlag(requireSquare_, value);
    }
    HRESULT STDMETHODCALLTYPE put_RequireSquareContent(boolean value) override {
        requireSquare_ = value;
        return S_OK;
    }
};

class LargeTile final : public Tile<ILargeTileContent> {
    ComPtr<IInspectable> child_;
    bool requireChild_ = true;

protected:
    std::wstring Bindings() override {
        ComPtr<ITileBinding> child;
        auto result = Binding();
        if (child_ && SUCCEEDED(child_.As(&child))) result += child->Binding();
        return result;
    }

public:
    explicit LargeTile(TemplateIndex index) : Tile(index) {}
    HRESULT STDMETHODCALLTYPE get_Child(IInspectable** value) override {
        if (!value) return E_POINTER;
        return child_.CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE put_Child(IInspectable* value) override {
        child_ = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_RequireChild(boolean* value) override { return CopyFlag(requireChild_, value); }
    HRESULT STDMETHODCALLTYPE put_RequireChild(boolean value) override {
        requireChild_ = value;
        return S_OK;
    }
};
class TileContentFactory final : public Factory<ITileContentFactoryStatic, ITileContentFactoryStatic81> {
    template <class T> static HRESULT Create(TemplateIndex index, IInspectable** value) {
        auto tile = Make<T>(index);
        if (!tile || !tile->Ready()) return E_OUTOFMEMORY;
        *value = static_cast<ITileTemplate*>(tile.Detach()); // the template's interface, as Windows returns
        return S_OK;
    }
    static HRESULT Create(TemplateIndex index, IInspectable** value) {
        if (!value) return E_POINTER;
        *value = nullptr;
        const wchar_t* name = templates[index].name;
        Log("TRACE", "NotificationsExtensions TileContentFactory.CreateTile%ls", name);
        if (wcsncmp(name, L"Square310x310", 13) == 0 || wcsncmp(name, L"Square210x210", 13) == 0 ||
            wcsncmp(name, L"Wide432x210", 11) == 0)
            return Create<LargeTile>(index, value);
        return wcsncmp(name, L"Wide", 4) == 0 ? Create<WideTile>(index, value) : Create<SquareTile>(index, value);
    }

public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) noexcept override {
        if (!value) return E_POINTER;
        if (Notifications81() && iid == __uuidof(ITileContentFactoryStatic)) {
            *value = static_cast<ITileContentFactoryStatic81*>(this);
            static_cast<ITileContentFactoryStatic81*>(this)->AddRef();
            return S_OK;
        }
        return Factory<ITileContentFactoryStatic, ITileContentFactoryStatic81>::QueryInterface(iid, value);
    }
    TileContentFactory() : Factory(L"NotificationsExtensions.TileContent.TileContentFactory") {}
#define APPX_CREATE_TEMPLATE(name, fields)                                                                             \
    HRESULT STDMETHODCALLTYPE CreateTile##name(IInspectable** value) override { return Create(Template##name, value); }
    APPX_TILE_TEMPLATES(APPX_CREATE_TEMPLATE)
    APPX_TILE_TEMPLATES81(APPX_CREATE_TEMPLATE)
#undef APPX_CREATE_TEMPLATE
};
}
HRESULT MakeTileContentFactory(IActivationFactory** value) {
    auto factory = Make<TileContentFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
const ShimClass tileContentClass{L"NotificationsExtensions.TileContent.TileContentFactory", MakeTileContentFactory,
                                 ShimPolicy::Replace, InIslands};
}
