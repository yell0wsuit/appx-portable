#include "source.h"
#include "animation.h"
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
    std::vector<Node*> resources, timelines;
    bool explicitTimelines = false, hasDuration = false, hasTargetName = false, hasThemeName = false,
         hasTargetProperty = false;
    Duration duration{};
    std::wstring targetName, themeName, targetProperty;
    std::wstring key;
    bool hasKey = false, hasResources = false;
    ComPtr<IResourceDictionary> dictionary;
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
                std::set<std::wstring>& names, std::set<int>& connections, bool root, bool resource = false) {
    if (node.space != Presentation) return Fail(node.space + L":" + node.type);
    const bool animation = AnimationType(node.type);
    if (!animation && node.type != L"Page" && node.type != L"Frame" && node.type != L"Grid") return Fail(node.type);
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
        if (animation && attr.space.empty() &&
            (attr.name == L"Storyboard.TargetName" || attr.name == L"Storyboard.TargetProperty" ||
             attr.name == L"TargetName") &&
            !attr.value.empty() && attr.value.front() == L'{')
            return Fail(member);
        if (attr.space == Xmlns || (attr.space == Compatibility && attr.name == L"Ignorable") ||
            (ignoreDesign && attr.space == Design))
            continue;
        if (attr.space == Language && attr.name == L"Key") {
            if (!resource) return Fail(member, E_INVALIDARG);
            if (!attr.value.empty() && attr.value.front() == L'{') return Fail(member);
            node.key = attr.value;
            node.hasKey = true;
        } else if ((attr.space == Language || (!animation && attr.space.empty())) && attr.name == L"Name") {
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
        } else if (animation && attr.space.empty() && attr.name == L"Duration") {
            auto hr = ParseAnimationDuration(attr.value, &node.duration);
            if (FAILED(hr)) return Fail(node.type + L".Duration", hr);
            node.hasDuration = true;
        } else if (animation && attr.space.empty() && attr.name == L"Storyboard.TargetName") {
            node.targetName = attr.value;
            node.hasTargetName = true;
        } else if (animation && attr.space.empty() && attr.name == L"Storyboard.TargetProperty") {
            node.targetProperty = attr.value;
            node.hasTargetProperty = true;
        } else if (animation && node.type != L"Storyboard" && attr.space.empty() && attr.name == L"TargetName") {
            node.themeName = attr.value;
            node.hasThemeName = true;
        } else if (!animation && attr.space.empty() && (attr.name == L"Width" || attr.name == L"Height")) {
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
        } else if (!animation && attr.space.empty() && attr.name == L"Visibility") {
            if (attr.value != L"Visible" && attr.value != L"Collapsed") return Fail(member, E_INVALIDARG);
            node.visibility = attr.value == L"Visible" ? Visibility_Visible : Visibility_Collapsed;
            node.hasVisibility = true;
        } else
            return Fail(member);
    }
    if (resource && !node.hasKey) {
        if (node.name.empty()) return Fail(node.type + L" resource key", E_INVALIDARG);
        node.key = node.name;
    }
    for (auto& child : node.children) {
        if (!animation && child->space == Presentation && child->type == node.type + L".Resources") {
            if (node.hasResources) return Fail(child->type + L" multiple values", E_INVALIDARG);
            node.hasResources = true;
            auto resourceSpaces = spaces;
            auto declarations = [&](Node& holder) -> HRESULT {
                for (const auto& attr : holder.attributes) {
                    if (attr.space != Xmlns) return Fail(holder.type + L"." + attr.name);
                    resourceSpaces[attr.name] = attr.value;
                }
                return S_OK;
            };
            auto hr = declarations(*child);
            if (FAILED(hr)) return hr;
            Node* holder = child.get();
            if (child->children.size() == 1 && child->children[0]->space == Presentation &&
                child->children[0]->type == L"ResourceDictionary") {
                holder = child->children[0].get();
                hr = declarations(*holder);
                if (FAILED(hr)) return hr;
            }
            std::set<std::wstring> keys;
            for (auto& entry : holder->children) {
                if (entry->space == Presentation && entry->type == L"ResourceDictionary")
                    return Fail(child->type + L" mixed dictionary entries", E_INVALIDARG);
                hr = Prepare(*entry, resourceSpaces, ignoreDesign, names, connections, false, true);
                if (FAILED(hr)) return hr;
                if (!keys.insert(entry->key).second)
                    return Fail(child->type + L" duplicate key " + entry->key, E_INVALIDARG);
                node.resources.push_back(entry.get());
            }
            continue;
        }
        if (node.type == L"Storyboard") {
            Node* holder = nullptr;
            if (child->space == Presentation && child->type == L"Storyboard.Children") {
                if (node.explicitTimelines || !node.timelines.empty())
                    return Fail(L"Storyboard.Children multiple values", E_INVALIDARG);
                for (const auto& attr : child->attributes)
                    if (attr.space != Xmlns) return Fail(L"Storyboard.Children." + attr.name);
                node.explicitTimelines = true;
                holder = child.get();
            } else if (node.explicitTimelines)
                return Fail(L"Storyboard.Children mixed values", E_INVALIDARG);
            auto prepareTimeline = [&](Node& entry) -> HRESULT {
                auto hr = Prepare(entry, spaces, ignoreDesign, names, connections, false);
                if (FAILED(hr)) return hr;
                if (!AnimationType(entry.type)) return Fail(L"Storyboard.Children type " + entry.type, E_INVALIDARG);
                node.timelines.push_back(&entry);
                return S_OK;
            };
            if (holder) {
                for (auto& entry : holder->children) {
                    auto hr = prepareTimeline(*entry);
                    if (FAILED(hr)) return hr;
                }
            } else {
                auto hr = prepareTimeline(*child);
                if (FAILED(hr)) return hr;
            }
            continue;
        }
        if (animation) return Fail(node.type + L".Children");
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
        if (node.type == L"Page" && AnimationType(content->type))
            return Fail(L"Page.Content nonvisual value", E_INVALIDARG);
        node.content = content;
    }
    return S_OK;
}
HRESULT Create(Node& node) {
    if (AnimationType(node.type)) {
        auto name = L"Windows.UI.Xaml.Media.Animation." + node.type;
        ComPtr<IActivationFactory> factory;
        auto hr = ShimGetFactory(Wrappers::HStringReference(name.c_str()).Get(), &factory);
        return FAILED(hr) ? hr : factory->ActivateInstance(&node.value);
    }
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
    if (AnimationType(node.type))
        return ConfigureAnimation(node.value.Get(), node.hasDuration ? &node.duration : nullptr,
                                  node.hasTargetName ? &node.targetName : nullptr,
                                  node.hasThemeName ? &node.themeName : nullptr,
                                  node.hasTargetProperty ? &node.targetProperty : nullptr);
    ComPtr<IFrameworkElement> element;
    auto hr = node.value.As(&element);
    if (SUCCEEDED(hr) && !node.name.empty())
        hr = element->put_Name(Wrappers::HStringReference(node.name.c_str()).Get());
    if (SUCCEEDED(hr) && node.hasResources) hr = element->put_Resources(node.dictionary.Get());
    if (SUCCEEDED(hr) && node.hasWidth) hr = element->put_Width(node.width);
    if (SUCCEEDED(hr) && node.hasHeight) hr = element->put_Height(node.height);
    if (SUCCEEDED(hr) && node.hasVisibility) {
        ComPtr<IUIElement> ui;
        hr = node.value.As(&ui);
        if (SUCCEEDED(hr)) hr = ui->put_Visibility(node.visibility);
    }
    return FAILED(hr) ? hr : SetContent(node);
}
HRESULT Build(Node& node);
HRESULT BuildParts(Node& node) {
    for (auto* timeline : node.timelines) {
        auto hr = Build(*timeline);
        if (SUCCEEDED(hr)) hr = AppendAnimation(node.value.Get(), timeline->value.Get());
        if (FAILED(hr)) return hr;
    }
    for (auto* resource : node.resources) {
        auto hr = Build(*resource);
        if (FAILED(hr)) return hr;
    }
    if (node.hasResources) {
        auto hr = MakeResourceDictionary(&node.dictionary);
        if (FAILED(hr)) return hr;
        for (auto* resource : node.resources) {
            hr = AddSourceResource(node.dictionary.Get(), Wrappers::HStringReference(resource->key.c_str()).Get(),
                                   resource->value.Get());
            if (FAILED(hr)) return hr;
        }
    }
    return node.content ? Build(*node.content) : S_OK;
}
HRESULT Build(Node& node) {
    auto hr = Create(node);
    if (SUCCEEDED(hr)) hr = BuildParts(node);
    return FAILED(hr) ? hr : Apply(node);
}
HRESULT AttachNames(Node& node, const std::shared_ptr<SourceNameScope>& scope, bool root) {
    HRESULT hr = S_OK;
    if (AnimationType(node.type))
        hr = BindAnimationScope(node.value.Get(), scope);
    else {
        ComPtr<INamedElementRegistry> registry;
        hr = node.value.As(&registry);
        if (SUCCEEDED(hr)) registry->SetSourceNameScope(scope, root);
    }
    if (FAILED(hr)) return hr;
    if (!root && !node.name.empty()) scope->elements[node.name] = node.value;
    for (auto* timeline : node.timelines) {
        hr = AttachNames(*timeline, scope, false);
        if (FAILED(hr)) return hr;
    }
    for (auto* resource : node.resources) {
        hr = AttachNames(*resource, scope, false);
        if (FAILED(hr)) return hr;
    }
    return node.content ? AttachNames(*node.content, scope, false) : S_OK;
}
HRESULT Connect(Node& node, ABI::Windows::UI::Xaml::Markup::IComponentConnector* connector) {
    if (node.connection && !connector) return Fail(L"IComponentConnector", E_NOINTERFACE);
    auto hr = node.connection ? connector->Connect(node.connection, node.value.Get()) : S_OK;
    if (FAILED(hr)) return hr;
    for (auto* timeline : node.timelines) {
        hr = Connect(*timeline, connector);
        if (FAILED(hr)) return hr;
    }
    for (auto* resource : node.resources) {
        hr = Connect(*resource, connector);
        if (FAILED(hr)) return hr;
    }
    return node.content ? Connect(*node.content, connector) : S_OK;
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
        hr = BuildParts(*root);
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
