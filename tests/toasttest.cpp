#include <windows.h>
#include <roapi.h>
#include <windows.ui.notifications.h>
#include <windows.data.xml.dom.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <cstdio>
#include <cstring>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::UI::Notifications;
int main(int argc, char** argv) {
    const bool islands = argc > 1 && strcmp(argv[1], "--islands") == 0;
    RoInitialize(RO_INIT_MULTITHREADED);
    auto* dll = LoadLibraryW(L"appxportable.dll");
    if (!dll) return 1;
    auto get = reinterpret_cast<HRESULT(WINAPI*)(HSTRING, IActivationFactory**)>(GetProcAddress(dll, "ShimGetFactory"));
    ComPtr<IActivationFactory> factory;
    auto hr = get(HStringReference(L"Windows.UI.Notifications.ToastNotificationManager").Get(), &factory);
    if (FAILED(hr)) {
        printf("FAIL Toast factory hr=%08lx\n", hr);
        return 2;
    }
    ComPtr<IToastNotificationManagerStatics> manager;
    if (FAILED(factory.As(&manager))) return 3;
    ComPtr<IToastNotifier> notifier;
    if (FAILED(manager->CreateToastNotifier(&notifier)) || !notifier) return 4;
    NotificationSetting setting{};
    if (FAILED(notifier->get_Setting(&setting)) || setting == NotificationSetting_Enabled) return 5;
    ComPtr<ABI::Windows::Foundation::Collections::IVectorView<ScheduledToastNotification*>> list;
    if (FAILED(notifier->GetScheduledToastNotifications(&list)) || !list) return 6;
    UINT32 count = 42;
    if (FAILED(list->get_Size(&count)) || count) return 7;
    ComPtr<IScheduledToastNotification> item;
    if (list->GetAt(0, &item) != E_BOUNDS || item) return 8;
    ComPtr<ABI::Windows::Foundation::Collections::IIterable<ScheduledToastNotification*>> iterable;
    ComPtr<ABI::Windows::Foundation::Collections::IIterator<ScheduledToastNotification*>> iterator;
    if (FAILED(list.As(&iterable)) || FAILED(iterable->First(&iterator))) return 9;
    boolean current = true;
    if (FAILED(iterator->get_HasCurrent(&current)) || current) return 10;
    if (notifier->Show(nullptr) != E_INVALIDARG || notifier->AddToSchedule(nullptr) != E_INVALIDARG) return 11;
    // Disabled notifications fail silently, as on Windows: a C++/CX caller
    // turns any failing HRESULT into an exception that ends its work item.
    using namespace ABI::Windows::Data::Xml::Dom;
    ComPtr<IXmlDocument> document;
    if (FAILED(manager->GetTemplateContent(ToastTemplateType_ToastText02, &document)) || !document) return 12;
    ComPtr<IXmlNodeList> texts;
    UINT32 length = 0;
    if (FAILED(document->GetElementsByTagName(HStringReference(L"text").Get(), &texts)) ||
        FAILED(texts->get_Length(&length)) || length != 2)
        return 13;
    ComPtr<IXmlNode> first;
    if (FAILED(texts->Item(0, &first)) || !first) return 14;
    // XmlNodeList is also IVectorView/IIterable<IXmlNode>; C++/CX GetAt and range-for use those.
    ComPtr<ABI::Windows::Foundation::Collections::IVectorView<IXmlNode*>> view;
    UINT32 size = 0;
    if (FAILED(texts.As(&view)) || FAILED(view->get_Size(&size)) || size != 2) return 27;
    ComPtr<IXmlNode> second;
    if (FAILED(view->GetAt(1, &second)) || !second || view->GetAt(2, &second) != E_BOUNDS) return 28;
    ComPtr<ABI::Windows::Foundation::Collections::IIterable<IXmlNode*>> nodes;
    ComPtr<ABI::Windows::Foundation::Collections::IIterator<IXmlNode*>> cursor;
    boolean has = false;
    UINT32 seen = 0;
    if (FAILED(texts.As(&nodes)) || FAILED(nodes->First(&cursor))) return 29;
    while (SUCCEEDED(cursor->get_HasCurrent(&has)) && has) {
        ++seen;
        cursor->MoveNext(&has);
    }
    if (seen != 2) return 30;
    ComPtr<IXmlText> text;
    if (FAILED(document->CreateTextNode(HStringReference(L"Om Nom misses you").Get(), &text))) return 15;
    ComPtr<IXmlNode> textNode, appended;
    if (FAILED(text.As(&textNode)) || FAILED(first->AppendChild(textNode.Get(), &appended))) return 16;
    ComPtr<IXmlNodeSerializer> serializer;
    HString xml;
    if (FAILED(document.As(&serializer)) || FAILED(serializer->GetXml(xml.GetAddressOf()))) return 17;
    if (!wcsstr(xml.GetRawBuffer(nullptr), L"<text id=\"1\">Om Nom misses you</text>")) {
        printf("xml=%ls\n", xml.GetRawBuffer(nullptr));
        return 18;
    }
    if (islands) {
        // Exercise the managed-mode route and the OS XML -> OS toast boundary.
        // Creating an unpackaged system notifier would fail with 0x80070490.
        ComPtr<IToastNotificationFactory> toastFactory;
        auto result = RoGetActivationFactory(HStringReference(L"Windows.UI.Notifications.ToastNotification").Get(),
                                             IID_PPV_ARGS(&toastFactory));
        ComPtr<IToastNotification> toast;
        if (SUCCEEDED(result)) result = toastFactory->CreateToastNotification(document.Get(), &toast);
        if (SUCCEEDED(result)) result = notifier->Show(toast.Get());
        if (SUCCEEDED(result)) result = notifier->Hide(toast.Get());
        if (FAILED(result)) {
            printf("FAIL Islands toast hr=%08lx\n", result);
            return 31;
        }
    }
    if (FAILED(get(HStringReference(L"Windows.UI.Notifications.ScheduledToastNotification").Get(), &factory)))
        return 19;
    ComPtr<IScheduledToastNotificationFactory> scheduledFactory;
    if (FAILED(factory.As(&scheduledFactory))) return 20;
    ABI::Windows::Foundation::DateTime when{132000000000000000};
    ComPtr<IScheduledToastNotification> scheduled;
    if (FAILED(scheduledFactory->CreateScheduledToastNotification(document.Get(), when, &scheduled)) || !scheduled)
        return 21;
    ABI::Windows::Foundation::DateTime delivery{};
    if (FAILED(scheduled->get_DeliveryTime(&delivery)) || delivery.UniversalTime != when.UniversalTime) return 22;
    if (FAILED(notifier->AddToSchedule(scheduled.Get()))) return 23;
    list.Reset();
    if (FAILED(notifier->GetScheduledToastNotifications(&list)) || FAILED(list->get_Size(&count)) || count != 1)
        return 24;
    if (FAILED(notifier->RemoveFromSchedule(scheduled.Get()))) return 25;
    list.Reset();
    if (FAILED(notifier->GetScheduledToastNotifications(&list)) || FAILED(list->get_Size(&count)) || count) return 26;
    if (!islands) {
        // Notification code addresses template parts by path and edits image
        // sources through attribute nodes, which stay bound to their element.
        ComPtr<IXmlNodeSelector> selector;
        ComPtr<IXmlNode> toastNode;
        if (FAILED(document.As(&selector)) || FAILED(selector->SelectSingleNode(HStringReference(L"/toast").Get(), &toastNode)) ||
            !toastNode)
            return 32;
        ComPtr<IXmlNodeList> bindings;
        if (FAILED(selector->SelectNodes(HStringReference(L"//binding").Get(), &bindings)) ||
            FAILED(bindings->get_Length(&length)) || length != 1)
            return 33;
        ComPtr<IXmlNode> missing;
        if (selector->SelectSingleNode(HStringReference(L"/toast[1]").Get(), &missing) != E_NOTIMPL) return 34;
        if (FAILED(get(HStringReference(L"Windows.UI.Notifications.TileUpdateManager").Get(), &factory))) return 35;
        ComPtr<ITileUpdateManagerStatics> tiles;
        ComPtr<IXmlDocument> tile;
        if (FAILED(factory.As(&tiles)) || FAILED(tiles->GetTemplateContent(TileTemplateType_TileWide310x150ImageAndText01, &tile)) ||
            !tile)
            return 36;
        ComPtr<IXmlNodeList> images;
        if (FAILED(tile->GetElementsByTagName(HStringReference(L"image").Get(), &images)) ||
            FAILED(images->get_Length(&length)) || length != 1 ||
            FAILED(tile->GetElementsByTagName(HStringReference(L"text").Get(), &texts)) ||
            FAILED(texts->get_Length(&length)) || length != 1)
            return 37;
        ComPtr<IXmlNode> image;
        ComPtr<IXmlNamedNodeMap> attributes;
        if (FAILED(images->Item(0, &image)) || FAILED(image->get_Attributes(&attributes)) || !attributes ||
            FAILED(attributes->get_Length(&length)) || length != 2)
            return 38;
        // The managed projection enumerates the map as IIterable<IXmlNode>.
        ComPtr<ABI::Windows::Foundation::Collections::IIterable<IXmlNode*>> entries;
        if (FAILED(attributes.As(&entries)) || FAILED(entries->First(&cursor))) return 39;
        bool edited = false;
        while (SUCCEEDED(cursor->get_HasCurrent(&has)) && has) {
            ComPtr<IXmlNode> node;
            ComPtr<IXmlAttribute> attribute;
            HString name;
            NodeType kind{};
            if (FAILED(cursor->get_Current(&node)) || FAILED(node.As(&attribute)) ||
                FAILED(node->get_NodeType(&kind)) || kind != NodeType_AttributeNode ||
                FAILED(attribute->get_Name(name.GetAddressOf())))
                return 40;
            if (!wcscmp(name.GetRawBuffer(nullptr), L"src")) {
                if (FAILED(attribute->put_Value(HStringReference(L"Assets/Tiles/Logo_Wide.png").Get()))) return 41;
                edited = true;
            }
            cursor->MoveNext(&has);
        }
        ComPtr<IXmlElement> element;
        HString source;
        if (!edited || FAILED(image.As(&element)) ||
            FAILED(element->GetAttribute(HStringReference(L"src").Get(), source.GetAddressOf())) ||
            wcscmp(source.GetRawBuffer(nullptr), L"Assets/Tiles/Logo_Wide.png"))
            return 42;
        ComPtr<IXmlNode> named;
        ComPtr<IXmlNode> none;
        if (FAILED(attributes->GetNamedItem(HStringReference(L"id").Get(), &named)) || !named ||
            FAILED(attributes->GetNamedItem(HStringReference(L"alt").Get(), &none)) || none)
            return 43;
        if (FAILED(tile.As(&serializer)) || FAILED(serializer->GetXml(xml.ReleaseAndGetAddressOf())) ||
            !wcsstr(xml.GetRawBuffer(nullptr),
                    L"<tile><visual><binding template=\"TileWideImageAndText01\"><image id=\"1\" src=\"Assets/Tiles/Logo_Wide.png\"/><text id=\"1\"/>")) {
            printf("tile xml=%ls\n", xml.GetRawBuffer(nullptr));
            return 44;
        }
        // Under Wine the shim makes the notification objects games build from templates.
        if (FAILED(get(HStringReference(L"Windows.UI.Notifications.TileNotification").Get(), &factory))) return 46;
        ComPtr<ITileNotificationFactory> tileFactory;
        ComPtr<ITileNotification> tileNotification;
        ComPtr<IXmlDocument> held;
        if (FAILED(factory.As(&tileFactory)) || FAILED(tileFactory->CreateTileNotification(tile.Get(), &tileNotification)) ||
            FAILED(tileNotification->get_Content(&held)) || held != tile ||
            tileFactory->CreateTileNotification(nullptr, &tileNotification) != E_INVALIDARG)
            return 47;
        if (FAILED(get(HStringReference(L"Windows.UI.Notifications.ToastNotification").Get(), &factory))) return 48;
        ComPtr<IToastNotificationFactory> toastFactory;
        ComPtr<IToastNotification> toast;
        EventRegistrationToken activated{};
        if (FAILED(factory.As(&toastFactory)) || FAILED(toastFactory->CreateToastNotification(document.Get(), &toast)) ||
            FAILED(toast->get_Content(&held)) || held != document || FAILED(notifier->Show(toast.Get())))
            return 49;
        auto handler = Callback<ABI::Windows::Foundation::ITypedEventHandler<ToastNotification*, IInspectable*>>(
            [](IToastNotification*, IInspectable*) { return S_OK; });
        if (FAILED(toast->add_Activated(handler.Get(), &activated)) || FAILED(toast->remove_Activated(activated))) return 50;
        ComPtr<IXmlDocument> unknown;
        if (tiles->GetTemplateContent(static_cast<TileTemplateType>(46), &unknown) != E_INVALIDARG) return 45;
    }
    puts("toasttest: notifications disabled; schedule accepted silently");
    return 0;
}
