#include "source.h"
#include "element.h"
#include "registry.h"
#include "package/resources.h"
#include <windows.ui.xaml.markup.h>
#include <shlwapi.h>
#include <xmllite.h>
#include <algorithm>
#include <cwctype>
#include <vector>
#include <cmath>
#include <locale>
#include <set>
#include <sstream>
using namespace Microsoft::WRL;
using namespace ABI::Windows::UI::Xaml;
using namespace ABI::Windows::UI::Xaml::Controls;
namespace appx {
namespace {
constexpr auto Presentation = L"http://schemas.microsoft.com/winfx/2006/xaml/presentation";
constexpr auto Language = L"http://schemas.microsoft.com/winfx/2006/xaml";
constexpr auto Xmlns = L"http://www.w3.org/2000/xmlns/";
constexpr auto Compatibility = L"http://schemas.openxmlformats.org/markup-compatibility/2006";
constexpr auto Design = L"http://schemas.microsoft.com/expression/blend/2008";
struct Attribute {
    std::wstring space, name, value;
};
struct Node {
    std::wstring space, type, name, className;
    std::vector<Attribute> attributes;
    std::vector<std::unique_ptr<Node>> children;
    Node* content = nullptr;
    bool hasWidth = false, hasHeight = false, hasVisibility = false;
    double width = 0, height = 0;
    Visibility visibility = Visibility_Visible;
    int connection = 0;
    ComPtr<IInspectable> value;
};
HRESULT Fail(const std::wstring& member, HRESULT hr = E_NOTIMPL) {
    Log("FAIL", "source XAML %ls hr=%08lx", member.c_str(), hr);
    return hr;
}
bool Whitespace(const std::wstring& text) { return text.find_first_not_of(L" \t\r\n") == std::wstring::npos; }
bool Identifier(const std::wstring& text) {
    auto letter = [](wchar_t c) { return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || c == L'_'; };
    if (text.empty() || !letter(text[0])) return false;
    return std::all_of(text.begin(), text.end(), [&](wchar_t c) { return letter(c) || (c >= L'0' && c <= L'9'); });
}
HRESULT Read(IXmlReader* reader, std::unique_ptr<Node>& root) {
    std::vector<Node*> stack;
    XmlNodeType kind{};
    HRESULT hr;
    while ((hr = reader->Read(&kind)) == S_OK) {
        if (kind == XmlNodeType_Element) {
            auto node = std::make_unique<Node>();
            const wchar_t *text = nullptr, *space = nullptr;
            reader->GetLocalName(&text, nullptr);
            reader->GetNamespaceUri(&space, nullptr);
            node->type = text ? text : L"";
            node->space = space ? space : L"";
            const bool empty = reader->IsEmptyElement() != FALSE;
            for (auto attr = reader->MoveToFirstAttribute(); attr == S_OK; attr = reader->MoveToNextAttribute()) {
                const wchar_t* value = nullptr;
                reader->GetLocalName(&text, nullptr);
                reader->GetNamespaceUri(&space, nullptr);
                reader->GetValue(&value, nullptr);
                node->attributes.push_back({space ? space : L"", text ? text : L"", value ? value : L""});
            }
            reader->MoveToElement();
            auto* current = node.get();
            if (stack.empty()) {
                if (root) return Fail(L"multiple roots", E_INVALIDARG);
                root = std::move(node);
            } else
                stack.back()->children.push_back(std::move(node));
            if (!empty) stack.push_back(current);
        } else if (kind == XmlNodeType_EndElement) {
            if (stack.empty()) return E_INVALIDARG;
            stack.pop_back();
        } else if (kind == XmlNodeType_Text || kind == XmlNodeType_CDATA) {
            const wchar_t* value = nullptr;
            reader->GetValue(&value, nullptr);
            if (value && !Whitespace(value)) return Fail(L"text content");
        }
    }
    return FAILED(hr) ? hr : root && stack.empty() ? S_OK : E_INVALIDARG;
}
HRESULT Prepare(Node& node, std::map<std::wstring, std::wstring> spaces, bool ignoreDesign,
                std::set<std::wstring>& names, std::set<int>& connections, bool root) {
    if (node.space != Presentation) return Fail(node.space + L":" + node.type);
    if (node.type != L"Page" && node.type != L"Frame" && node.type != L"Grid") return Fail(node.type);
    for (const auto& attr : node.attributes)
        if (attr.space == Xmlns) spaces[attr.name] = attr.value;
    for (const auto& attr : node.attributes)
        if (attr.space == Compatibility && attr.name == L"Ignorable") {
            std::wistringstream tokens(attr.value);
            std::wstring prefix;
            while (tokens >> prefix) {
                if (spaces[prefix] != Design) return Fail(L"mc:Ignorable " + prefix);
                ignoreDesign = true;
            }
        }
    for (const auto& attr : node.attributes) {
        const auto member = node.type + L"." + attr.name;
        if (attr.space == Xmlns || (attr.space == Compatibility && attr.name == L"Ignorable") ||
            (ignoreDesign && attr.space == Design))
            continue;
        if ((attr.space == Language || attr.space.empty()) && attr.name == L"Name") {
            if (!node.name.empty() || !Identifier(attr.value) || !names.insert(attr.value).second)
                return Fail(member, E_INVALIDARG);
            node.name = attr.value;
        } else if (attr.space == Language && attr.name == L"Class" && root)
            node.className = attr.value;
        else if (attr.space == Language && attr.name == L"ConnectionId") {
            std::wistringstream number(attr.value);
            number.imbue(std::locale::classic());
            if (!(number >> node.connection) || !(number >> std::ws).eof() || node.connection <= 0 ||
                !connections.insert(node.connection).second)
                return Fail(member, E_INVALIDARG);
        } else if (attr.space.empty() && (attr.name == L"Width" || attr.name == L"Height")) {
            double value;
            std::wistringstream number(attr.value);
            number.imbue(std::locale::classic());
            if (!(number >> value) || !(number >> std::ws).eof() || !std::isfinite(value) || value < 0)
                return Fail(member, E_INVALIDARG);
            if (attr.name == L"Width") {
                node.width = value;
                node.hasWidth = true;
            } else {
                node.height = value;
                node.hasHeight = true;
            }
        } else if (attr.space.empty() && attr.name == L"Visibility") {
            if (attr.value != L"Visible" && attr.value != L"Collapsed") return Fail(member, E_INVALIDARG);
            node.visibility = attr.value == L"Visible" ? Visibility_Visible : Visibility_Collapsed;
            node.hasVisibility = true;
        } else
            return Fail(member);
    }
    for (auto& child : node.children) {
        Node* content = child.get();
        if (child->space == Presentation && child->type == node.type + L".Content") {
            if (node.type == L"Grid") return Fail(child->type);
            if (!child->attributes.empty() || child->children.size() != 1) return Fail(child->type, E_INVALIDARG);
            content = child->children[0].get();
        }
        if (node.type == L"Grid") return Fail(L"Grid.Children");
        if (node.content) return Fail(node.type + L".Content multiple values", E_INVALIDARG);
        auto hr = Prepare(*content, spaces, ignoreDesign, names, connections, false);
        if (FAILED(hr)) return hr;
        node.content = content;
    }
    return S_OK;
}
HRESULT Create(Node& node) {
    if (node.type == L"Grid") return MakeGrid(&node.value);
    ComPtr<IActivationFactory> factory;
    auto name = L"Windows.UI.Xaml.Controls." + node.type;
    auto hr = ShimGetFactory(Wrappers::HStringReference(name.c_str()).Get(), &factory);
    if (FAILED(hr)) return hr;
    ComPtr<IInspectable> inner;
    if (node.type == L"Page") {
        ComPtr<IPageFactory> compose;
        ComPtr<IPage> value;
        hr = factory.As(&compose);
        if (SUCCEEDED(hr)) hr = compose->CreateInstance(nullptr, &inner, &value);
        return FAILED(hr) ? hr : value.As(&node.value);
    }
    ComPtr<IFrameFactory> compose;
    ComPtr<IFrame> value;
    hr = factory.As(&compose);
    if (SUCCEEDED(hr)) hr = compose->CreateInstance(nullptr, &inner, &value);
    return FAILED(hr) ? hr : value.As(&node.value);
}
HRESULT SetContent(Node& node) {
    if (node.type == L"Grid") return S_OK;
    auto* child = node.content ? node.content->value.Get() : nullptr;
    if (node.type == L"Frame") {
        ComPtr<IContentControl> content;
        auto hr = node.value.As(&content);
        return FAILED(hr) ? hr : content->put_Content(child);
    }
    ComPtr<IUserControl> content;
    auto hr = node.value.As(&content);
    ComPtr<IUIElement> ui;
    if (SUCCEEDED(hr) && child) hr = child->QueryInterface(IID_PPV_ARGS(&ui));
    return FAILED(hr) ? hr : content->put_Content(ui.Get());
}
HRESULT Apply(Node& node) {
    ComPtr<IFrameworkElement> element;
    auto hr = node.value.As(&element);
    if (SUCCEEDED(hr) && !node.name.empty())
        hr = element->put_Name(Wrappers::HStringReference(node.name.c_str()).Get());
    if (SUCCEEDED(hr) && node.hasWidth) hr = element->put_Width(node.width);
    if (SUCCEEDED(hr) && node.hasHeight) hr = element->put_Height(node.height);
    if (SUCCEEDED(hr) && node.hasVisibility) {
        ComPtr<IUIElement> ui;
        hr = node.value.As(&ui);
        if (SUCCEEDED(hr)) hr = ui->put_Visibility(node.visibility);
    }
    return FAILED(hr) ? hr : SetContent(node);
}
HRESULT Build(Node& node) {
    auto hr = Create(node);
    if (SUCCEEDED(hr) && node.content) hr = Build(*node.content);
    return FAILED(hr) ? hr : Apply(node);
}
HRESULT AttachNames(Node& node, const std::shared_ptr<SourceNameScope>& scope, bool root) {
    ComPtr<INamedElementRegistry> registry;
    auto hr = node.value.As(&registry);
    if (FAILED(hr)) return hr;
    registry->SetSourceNameScope(scope, root);
    if (!root && !node.name.empty()) scope->elements[node.name] = node.value;
    return node.content ? AttachNames(*node.content, scope, false) : S_OK;
}
HRESULT Connect(Node& node, ABI::Windows::UI::Xaml::Markup::IComponentConnector* connector) {
    if (node.connection && !connector) return Fail(L"IComponentConnector", E_NOINTERFACE);
    auto hr = node.connection ? connector->Connect(node.connection, node.value.Get()) : S_OK;
    return FAILED(hr) || !node.content ? hr : Connect(*node.content, connector);
}
HRESULT ResourcePath(ABI::Windows::Foundation::IUriRuntimeClass* uri, std::wstring& path) {
    Wrappers::HString scheme, host, query, fragment, raw;
    auto hr = uri->get_SchemeName(scheme.GetAddressOf());
    if (SUCCEEDED(hr)) hr = uri->get_Host(host.GetAddressOf());
    if (SUCCEEDED(hr)) hr = uri->get_Query(query.GetAddressOf());
    if (SUCCEEDED(hr)) hr = uri->get_Fragment(fragment.GetAddressOf());
    if (SUCCEEDED(hr)) hr = uri->get_Path(raw.GetAddressOf());
    if (FAILED(hr)) return hr;
    if (_wcsicmp(scheme.GetRawBuffer(nullptr), L"ms-appx") != 0 || query.Length() || fragment.Length())
        return Fail(L"resource URI", E_INVALIDARG);
    if (host.Length()) return Fail(L"resource URI authority");
    path = raw.GetRawBuffer(nullptr);
    for (size_t i = 0; i < path.size(); ++i)
        if (path[i] == L'%') {
            if (i + 2 >= path.size() || !iswxdigit(path[i + 1]) || !iswxdigit(path[i + 2]) ||
                path.substr(i + 1, 2) == L"00")
                return E_INVALIDARG;
            i += 2;
        }
    hr = UrlUnescapeW(path.data(), nullptr, nullptr, URL_UNESCAPE_INPLACE | URL_UNESCAPE_AS_UTF8);
    if (FAILED(hr)) return hr;
    path.resize(wcslen(path.c_str()));
    if (path.empty() || path.front() != L'/') return E_INVALIDARG;
    path.erase(0, 1);
    return S_OK;
}
}
HRESULT LoadSourceXaml(IInspectable* component, ABI::Windows::Foundation::IUriRuntimeClass* uri) {
    try {
        if (!component || !uri) return E_INVALIDARG;
        ComPtr<INamedElementRegistry> rootRegistry;
        auto hr = component->QueryInterface(IID_PPV_ARGS(&rootRegistry));
        if (SUCCEEDED(hr)) hr = rootRegistry->CheckSourceThread();
        if (FAILED(hr)) return hr;
        std::wstring path;
        hr = ResourcePath(uri, path);
        if (FAILED(hr)) return hr;
        Log("TRACE", "source XAML load %ls", path.c_str());
        ComPtr<IStream> stream;
        hr = OpenPackageResource(CurrentPackage(), path, &stream);
        if (hr == S_FALSE) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        if (FAILED(hr)) return hr;
        ComPtr<IXmlReader> reader;
        hr = CreateXmlReader(IID_PPV_ARGS(&reader), nullptr);
        if (SUCCEEDED(hr)) hr = reader->SetProperty(XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit);
        if (SUCCEEDED(hr)) hr = reader->SetProperty(XmlReaderProperty_MaxElementDepth, 128);
        if (SUCCEEDED(hr)) hr = reader->SetInput(stream.Get());
        std::unique_ptr<Node> root;
        if (SUCCEEDED(hr)) hr = Read(reader.Get(), root);
        std::set<std::wstring> names;
        std::set<int> connections;
        if (SUCCEEDED(hr)) hr = Prepare(*root, {}, false, names, connections, true);
        if (FAILED(hr)) return hr;
        root->value = component;
        ComPtr<IFrameworkElement> element;
        hr = component->QueryInterface(IID_PPV_ARGS(&element));
        ComPtr<IInspectable> kind;
        if (SUCCEEDED(hr))
            hr = root->type == L"Page"    ? component->QueryInterface(__uuidof(IPage), &kind)
                 : root->type == L"Frame" ? component->QueryInterface(__uuidof(IFrame), &kind)
                                          : component->QueryInterface(__uuidof(IGrid), &kind);
        if (FAILED(hr)) return Fail(L"root type " + root->type, E_INVALIDARG);
        if (!root->className.empty()) {
            Wrappers::HString name;
            hr = component->GetRuntimeClassName(name.GetAddressOf());
            if (FAILED(hr)) return hr;
            if (root->className != name.GetRawBuffer(nullptr)) return Fail(L"x:Class " + root->className, E_INVALIDARG);
        }
        ComPtr<ABI::Windows::UI::Xaml::Markup::IComponentConnector> connector;
        if (!connections.empty()) {
            hr = component->QueryInterface(IID_PPV_ARGS(&connector));
            if (FAILED(hr)) return hr;
        }
        if (root->content) hr = Build(*root->content);
        if (FAILED(hr)) return hr;
        auto scope = std::make_shared<SourceNameScope>();
        hr = element.AsWeak(&scope->root);
        if (FAILED(hr)) return Fail(L"root weak reference", hr);
        scope->rootName = root->name;
        hr = Apply(*root);
        if (SUCCEEDED(hr)) hr = AttachNames(*root, scope, true);
        if (SUCCEEDED(hr)) hr = Connect(*root, connector.Get());
        return hr;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
}
}
