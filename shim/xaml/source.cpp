#include "source.h"
#include "animation.h"
#include "brush.h"
#include "image.h"
#include "text.h"
#include "viewbox.h"
#include "grid.h"
#include "style.h"
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
    std::shared_ptr<const std::wstring> document; // Package path of the containing file.
    std::wstring text;                            // Rejected by RejectText until an element type consumes it.
    std::vector<std::wstring> mergedSources;
    bool hasMerged = false;
    std::wstring targetType;               // Style/ControlTemplate TargetType, a short type name.
    Node* templateNode = nullptr;          // Style: the ControlTemplate of its Template setter.
    std::shared_ptr<Node> templateContent; // ControlTemplate: its root, kept beyond the source document.
    bool hasAudioCategory = false, hasAutoPlay = false, autoPlay = false;
    ABI::Windows::UI::Xaml::Media::AudioCategory audioCategory{};
    std::vector<Attribute> attributes;
    std::vector<std::unique_ptr<Node>> children;
    Node* content = nullptr;
    Node* background = nullptr;
    Node* foregroundNode = nullptr;
    bool hasForeground = false, hasActive = false, active = false;
    ABI::Windows::UI::Color foregroundColor{};
    ComPtr<ABI::Windows::UI::Xaml::Media::IBrush> foreground;
    std::vector<Node*> stops;
    bool explicitStops = false;
    BrushSourceData brushData;
    std::vector<Node*> resources, timelines, visuals;
    bool explicitVisuals = false;
    std::shared_ptr<const VisualChildren> visualValues;
    bool explicitTimelines = false, hasDuration = false, hasTargetName = false, hasThemeName = false,
         hasTargetProperty = false;
    Duration duration{};
    std::wstring targetName, themeName, targetProperty;
    std::wstring key;
    bool hasKey = false, hasResources = false;
    ComPtr<IResourceDictionary> dictionary;
    ComPtr<ABI::Windows::UI::Xaml::Markup::IXamlType> customType; // A using: type, built as its system base.
    bool hasWidth = false, hasHeight = false, hasVisibility = false;
    double width = 0, height = 0;
    Visibility visibility = Visibility_Visible;
    bool hasImageSource = false, hasStretch = false, hasMargin = false, hasHorizontal = false, hasVertical = false;
    std::wstring imageUri;
    Microsoft::WRL::ComPtr<ABI::Windows::UI::Xaml::Media::IImageSource> imageSource;
    ABI::Windows::UI::Xaml::Media::Stretch stretch = ABI::Windows::UI::Xaml::Media::Stretch_Uniform;
    Thickness margin{};
    HorizontalAlignment horizontal = HorizontalAlignment_Stretch;
    VerticalAlignment vertical = VerticalAlignment_Stretch;
    bool hasText = false, hasFontSize = false, hasFontFamily = false, hasWrapping = false, hasTextAlignment = false;
    std::wstring textValue, familyName;
    double fontSize = 0;
    ComPtr<ABI::Windows::UI::Xaml::Media::IFontFamily> fontFamily;
    TextWrapping wrapping = TextWrapping_NoWrap;
    TextAlignment textAlignment = TextAlignment_Left;
    bool hasDirection = false;
    StretchDirection direction = StretchDirection_Both;
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
HRESULT Read(IXmlReader* reader, const std::wstring& path, std::unique_ptr<Node>& root) {
    auto document = std::make_shared<const std::wstring>(path);
    std::vector<Node*> stack;
    XmlNodeType kind{};
    HRESULT hr;
    UINT lastLine = 0, lastColumn = 0;
    while ((hr = reader->Read(&kind)) == S_OK) {
        if (kind == XmlNodeType_Element || kind == XmlNodeType_EndElement) {
            UINT line = 0, column = 0;
            reader->GetLineNumber(&line);
            reader->GetLinePosition(&column);
            // Wine's reader can replay an earlier node at truncated EOF. A
            // structural token must advance when line information is available.
            if (line && column && lastLine && (line < lastLine || (line == lastLine && column <= lastColumn)))
                return Fail(L"non-advancing XML reader", E_INVALIDARG);
            lastLine = line;
            lastColumn = column;
        }
        if (kind == XmlNodeType_Element) {
            auto node = std::make_unique<Node>();
            const wchar_t *text = nullptr, *space = nullptr;
            reader->GetLocalName(&text, nullptr);
            reader->GetNamespaceUri(&space, nullptr);
            node->type = text ? text : L"";
            node->space = space ? space : L"";
            node->document = document;
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
            if (value && !Whitespace(value)) {
                if (stack.empty()) return Fail(L"text content");
                stack.back()->text += value;
            }
        }
    }
    return FAILED(hr) ? hr : root && stack.empty() ? S_OK : E_INVALIDARG;
}
HRESULT RejectText(const Node& node) {
    if (!node.text.empty()) return Fail(L"text content");
    for (const auto& child : node.children) {
        auto hr = RejectText(*child);
        if (FAILED(hr)) return hr;
    }
    return S_OK;
}
int Choice(const std::wstring& text, std::initializer_list<const wchar_t*> names) {
    int value = 0;
    for (auto* name : names) {
        if (text == name) return value;
        ++value;
    }
    return -1;
}
HRESULT ParseMargin(const std::wstring& text, Thickness& value) {
    std::vector<double> parts;
    size_t start = 0;
    do {
        auto end = text.find(L',', start);
        auto part = text.substr(start, end == std::wstring::npos ? end : end - start);
        double number = 0;
        std::wistringstream input(part);
        input.imbue(std::locale::classic());
        if (!(input >> number) || !(input >> std::ws).eof() || !std::isfinite(number)) return E_INVALIDARG;
        parts.push_back(number);
        if (parts.size() > 4) return E_INVALIDARG;
        if (end == std::wstring::npos) break;
        start = end + 1;
    } while (true);
    if (parts.size() == 1)
        value = {parts[0], parts[0], parts[0], parts[0]};
    else if (parts.size() == 2)
        value = {parts[0], parts[1], parts[0], parts[1]};
    else if (parts.size() == 4)
        value = {parts[0], parts[1], parts[2], parts[3]};
    else
        return E_INVALIDARG;
    return S_OK;
}
HRESULT Prepare(Node& node, std::map<std::wstring, std::wstring> spaces, bool ignoreDesign,
                std::set<std::wstring>& names, std::set<int>& connections, bool root, bool resource = false);
HRESULT ResourcePath(ABI::Windows::Foundation::IUriRuntimeClass* uri, std::wstring& path);
// A merged Source is an ms-appx URI or a path relative to the containing file.
HRESULT ResolveSourcePath(const Node& owner, const std::wstring& value, std::wstring& path) {
    if (value.find(L':') != std::wstring::npos) {
        ComPtr<ABI::Windows::Foundation::IUriRuntimeClassFactory> factory;
        auto hr = RoGetActivationFactory(Wrappers::HStringReference(L"Windows.Foundation.Uri").Get(),
                                         IID_PPV_ARGS(&factory));
        ComPtr<ABI::Windows::Foundation::IUriRuntimeClass> uri;
        if (SUCCEEDED(hr)) hr = factory->CreateUri(Wrappers::HStringReference(value.c_str()).Get(), &uri);
        if (FAILED(hr)) return Fail(L"ResourceDictionary.Source " + value, E_INVALIDARG);
        return ResourcePath(uri.Get(), path);
    }
    if (value.empty() || value.find_first_of(L"\\%?#") != std::wstring::npos)
        return Fail(L"ResourceDictionary.Source " + value);
    size_t start = 0;
    do {
        auto end = value.find(L'/', start);
        auto segment = value.substr(start, end == std::wstring::npos ? end : end - start);
        if (segment.empty() || segment == L"." || segment == L"..") return Fail(L"ResourceDictionary.Source " + value);
        if (end == std::wstring::npos) break;
        start = end + 1;
    } while (true);
    const auto& base = owner.document ? *owner.document : std::wstring();
    auto slash = base.rfind(L'/');
    path = (slash == std::wstring::npos ? std::wstring() : base.substr(0, slash + 1)) + value;
    return S_OK;
}
// The element types source XAML builds; Style and ControlTemplate targets must be one of them.
bool SourceElementType(const std::wstring& type) {
    return type == L"Page" || type == L"Frame" || type == L"Grid" || type == L"Image" || type == L"ProgressRing" ||
           type == L"TextBlock" || type == L"Viewbox" || type == L"MediaElement" || type == L"ContentPresenter";
}
std::wstring ControlType(const std::wstring& type) { return L"Windows.UI.Xaml.Controls." + type; }
HRESULT ParseTargetType(const std::wstring& owner, const std::wstring& value, std::wstring& type) {
    if (!value.empty() && (value.front() == L'{' || value.find(L':') != std::wstring::npos))
        return Fail(owner + L".TargetType " + value);
    if (!Identifier(value)) return Fail(owner + L".TargetType", E_INVALIDARG);
    if (!SourceElementType(value)) return Fail(owner + L".TargetType " + value);
    type = value;
    return S_OK;
}
// A using: element names an app type, found through the application's XAML metadata
// provider. Its nearest system base type selects which source element it is built as.
HRESULT ResolveCustomType(Node& node) {
    const auto full = node.space.substr(6) + L"." + node.type;
    const auto member = node.space + L":" + node.type;
    ComPtr<IApplication> application;
    auto hr = CurrentApplication(&application);
    ComPtr<ABI::Windows::UI::Xaml::Markup::IXamlMetadataProvider> provider;
    if (SUCCEEDED(hr)) hr = application ? application.As(&provider) : E_NOINTERFACE;
    if (FAILED(hr)) return Fail(member + L" metadata provider", hr);
    hr = provider->GetXamlTypeByFullName(Wrappers::HStringReference(full.c_str()).Get(), &node.customType);
    if (FAILED(hr)) return Fail(member, hr);
    if (!node.customType) return Fail(member, TYPE_E_ELEMENTNOTFOUND);
    auto type = node.customType;
    for (int depth = 0; type && depth < 64; ++depth) {
        ABI::Windows::UI::Xaml::Interop::TypeName underlying{};
        hr = type->get_UnderlyingType(&underlying);
        if (FAILED(hr)) return Fail(member, hr);
        Wrappers::HString name;
        name.Attach(underlying.Name);
        if (underlying.Kind == ABI::Windows::UI::Xaml::Interop::TypeKind_Metadata) {
            const std::wstring system = name.GetRawBuffer(nullptr);
            const std::wstring controls = L"Windows.UI.Xaml.Controls.";
            const auto base = system.compare(0, controls.size(), controls) == 0 ? system.substr(controls.size()) : L"";
            if (!SourceElementType(base)) return Fail(member + L" base " + system);
            node.space = Presentation;
            node.type = base;
            return S_OK;
        }
        ComPtr<ABI::Windows::UI::Xaml::Markup::IXamlType> next;
        hr = type->get_BaseType(&next);
        if (FAILED(hr)) return Fail(member, hr);
        type = next;
    }
    return Fail(member + L" system base", E_INVALIDARG);
}
HRESULT PrepareTemplate(Node& node, std::map<std::wstring, std::wstring> spaces, bool ignoreDesign,
                        const std::wstring& owner) {
    for (const auto& attr : node.attributes)
        if (attr.space == Xmlns) spaces[attr.name] = attr.value;
    for (const auto& attr : node.attributes) {
        if (attr.space == Xmlns || (ignoreDesign && attr.space == Design)) continue;
        if (attr.space.empty() && attr.name == L"TargetType") {
            auto hr = ParseTargetType(node.type, attr.value, node.targetType);
            if (FAILED(hr)) return hr;
            if (node.targetType != owner) return Fail(L"ControlTemplate.TargetType " + attr.value + L" for " + owner);
        } else
            return Fail(node.type + L"." + attr.name);
    }
    if (node.children.size() > 1) return Fail(L"ControlTemplate multiple roots", E_INVALIDARG);
    if (node.children.empty()) return S_OK;
    auto& root = *node.children[0];
    if (root.space == Presentation && root.type.find(L'.') != std::wstring::npos) return Fail(root.type);
    // Template content has its own namescope and no connections to the loading component.
    std::set<std::wstring> names;
    std::set<int> connections;
    auto hr = Prepare(root, spaces, ignoreDesign, names, connections, false);
    if (FAILED(hr)) return hr;
    if (!connections.empty()) return Fail(L"ControlTemplate connections");
    if (AnimationType(root.type) || BrushDataType(root.type)) return Fail(L"ControlTemplate nonvisual root", E_INVALIDARG);
    node.templateContent = std::shared_ptr<Node>(std::move(node.children[0]));
    node.children.clear();
    return S_OK;
}
// Setters are resolved now by (TargetType, property name): only Frame.Template has an applier.
HRESULT PrepareSetter(Node& style, Node& setter, std::map<std::wstring, std::wstring> spaces, bool ignoreDesign) {
    if (setter.space != Presentation || setter.type != L"Setter")
        return Fail(L"Style.Setters type " + setter.type, E_INVALIDARG);
    const Attribute *property = nullptr, *text = nullptr;
    for (const auto& attr : setter.attributes)
        if (attr.space == Xmlns)
            spaces[attr.name] = attr.value;
        else if (attr.space.empty() && attr.name == L"Property" && !property)
            property = &attr;
        else if (attr.space.empty() && attr.name == L"Value" && !text)
            text = &attr;
        else if (!(ignoreDesign && attr.space == Design))
            return Fail(L"Setter." + attr.name);
    Node* element = nullptr;
    auto valueSpaces = spaces;
    for (auto& child : setter.children) {
        if (child->space != Presentation || child->type != L"Setter.Value") return Fail(L"Setter content " + child->type);
        for (const auto& attr : child->attributes) {
            if (attr.space != Xmlns) return Fail(L"Setter.Value." + attr.name);
            valueSpaces[attr.name] = attr.value;
        }
        if (element || child->children.size() != 1) return Fail(L"Setter.Value multiple values", E_INVALIDARG);
        element = child->children[0].get();
    }
    if (!property || property->value.empty()) return Fail(L"Setter.Property", E_INVALIDARG);
    if ((text != nullptr) == (element != nullptr)) return Fail(L"Setter.Value", E_INVALIDARG);
    if (property->value.front() == L'{') return Fail(L"Setter.Property " + property->value);
    if (style.targetType != L"Frame" || property->value != L"Template")
        return Fail(L"Style setter " + style.targetType + L"." + property->value);
    if (!element) return Fail(L"Setter.Value " + text->value);
    if (element->space != Presentation || element->type != L"ControlTemplate")
        return Fail(L"Frame.Template type " + element->type, E_INVALIDARG);
    if (style.templateNode) return Fail(L"Style duplicate setter Template", E_INVALIDARG);
    auto hr = PrepareTemplate(*element, valueSpaces, ignoreDesign, style.targetType);
    if (SUCCEEDED(hr)) style.templateNode = element;
    return hr;
}
HRESULT PrepareStyle(Node& node, std::map<std::wstring, std::wstring> spaces, bool ignoreDesign, bool resource) {
    if (!resource) return Fail(L"Style outside resources");
    for (const auto& attr : node.attributes)
        if (attr.space == Xmlns) spaces[attr.name] = attr.value;
    bool hasTarget = false;
    for (const auto& attr : node.attributes) {
        if (attr.space == Xmlns || (ignoreDesign && attr.space == Design)) continue;
        if (attr.space == Language && attr.name == L"Key") {
            if (attr.value.empty() || attr.value.front() == L'{') return Fail(L"Style.Key");
            node.key = attr.value;
            node.hasKey = true;
        } else if (attr.space.empty() && attr.name == L"TargetType") {
            auto hr = ParseTargetType(node.type, attr.value, node.targetType);
            if (FAILED(hr)) return hr;
            hasTarget = true;
        } else
            return Fail(L"Style." + attr.name);
    }
    if (!hasTarget) return Fail(L"Style.TargetType", E_INVALIDARG);
    if (!node.hasKey) return Fail(L"Style implicit key");
    std::vector<Node*> setters;
    bool explicitSetters = false;
    for (auto& child : node.children) {
        if (child->space == Presentation && child->type == L"Style.Setters") {
            if (explicitSetters || !setters.empty()) return Fail(L"Style.Setters multiple values", E_INVALIDARG);
            for (const auto& attr : child->attributes)
                if (attr.space != Xmlns) return Fail(L"Style.Setters." + attr.name);
            explicitSetters = true;
            for (auto& entry : child->children) setters.push_back(entry.get());
        } else if (explicitSetters)
            return Fail(L"Style.Setters mixed values", E_INVALIDARG);
        else if (child->space == Presentation && child->type.find(L'.') != std::wstring::npos)
            return Fail(child->type);
        else
            setters.push_back(child.get());
    }
    for (auto* setter : setters) {
        auto hr = PrepareSetter(node, *setter, spaces, ignoreDesign);
        if (FAILED(hr)) return hr;
    }
    return S_OK;
}
HRESULT PrepareMerged(Node& owner, Node& property) {
    if (owner.hasMerged) return Fail(property.type + L" multiple values", E_INVALIDARG);
    owner.hasMerged = true;
    for (const auto& attr : property.attributes)
        if (attr.space != Xmlns) return Fail(property.type + L"." + attr.name);
    for (auto& entry : property.children) {
        if (entry->space != Presentation || entry->type != L"ResourceDictionary")
            return Fail(property.type + L" type " + entry->type, E_INVALIDARG);
        const Attribute* source = nullptr;
        for (const auto& attr : entry->attributes)
            if (attr.space.empty() && attr.name == L"Source" && !source)
                source = &attr;
            else if (attr.space != Xmlns)
                return Fail(L"merged ResourceDictionary." + attr.name);
        if (!source || !entry->children.empty()) return Fail(L"inline merged ResourceDictionary");
        if (!source->value.empty() && source->value.front() == L'{') return Fail(L"ResourceDictionary.Source");
        std::wstring path;
        auto hr = ResolveSourcePath(*entry, source->value, path);
        if (FAILED(hr)) return hr;
        owner.mergedSources.push_back(path);
    }
    return S_OK;
}
HRESULT PrepareResources(Node& owner, Node& property, std::map<std::wstring, std::wstring> spaces, bool ignoreDesign,
                         std::set<std::wstring>& names, std::set<int>& connections) {
    if (owner.hasResources) return Fail(property.type + L" multiple values", E_INVALIDARG);
    owner.hasResources = true;
    auto resourceSpaces = spaces;
    auto declarations = [&](Node& holder) -> HRESULT {
        for (const auto& attr : holder.attributes) {
            if (attr.space != Xmlns) return Fail(holder.type + L"." + attr.name);
            resourceSpaces[attr.name] = attr.value;
        }
        return S_OK;
    };
    auto hr = declarations(property);
    if (FAILED(hr)) return hr;
    Node* holder = &property;
    if (property.children.size() == 1 && property.children[0]->space == Presentation &&
        property.children[0]->type == L"ResourceDictionary") {
        holder = property.children[0].get();
        hr = declarations(*holder);
        if (FAILED(hr)) return hr;
    }
    std::set<std::wstring> keys;
    for (auto& entry : holder->children) {
        if (holder != &property && entry->space == Presentation &&
            entry->type == L"ResourceDictionary.MergedDictionaries") {
            hr = PrepareMerged(owner, *entry);
            if (FAILED(hr)) return hr;
            continue;
        }
        if (entry->space == Presentation && entry->type == L"ResourceDictionary")
            return Fail(property.type + L" mixed dictionary entries", E_INVALIDARG);
        hr = Prepare(*entry, resourceSpaces, ignoreDesign, names, connections, false, true);
        if (FAILED(hr)) return hr;
        if (!keys.insert(entry->key).second) return Fail(property.type + L" duplicate key " + entry->key, E_INVALIDARG);
        owner.resources.push_back(entry.get());
    }
    return S_OK;
}
HRESULT Prepare(Node& node, std::map<std::wstring, std::wstring> spaces, bool ignoreDesign,
                std::set<std::wstring>& names, std::set<int>& connections, bool root, bool resource) {
    // Only a root of an app type for now: its instance is the component being loaded.
    if (root && node.space.compare(0, 6, L"using:") == 0) {
        auto hr = ResolveCustomType(node);
        if (FAILED(hr)) return hr;
    }
    if (node.space != Presentation) return Fail(node.space + L":" + node.type);
    if (node.type == L"Style") return PrepareStyle(node, spaces, ignoreDesign, resource);
    const bool animation = AnimationType(node.type);
    const bool brush = BrushDataType(node.type);
    const bool nonvisual = animation || brush;
    if (root && nonvisual) return Fail(L"nonvisual root", E_INVALIDARG);
    if (!nonvisual && !SourceElementType(node.type)) return Fail(node.type);
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
        } else if ((attr.space == Language || (!nonvisual && attr.space.empty())) && attr.name == L"Name") {
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
        } else if (brush && attr.space.empty()) {
            auto hr = ParseBrushAttribute(node.type, attr.name, attr.value, node.brushData);
            if (FAILED(hr)) return Fail(member, hr);
        } else if (node.type == L"ProgressRing" && attr.space.empty() && attr.name == L"IsActive") {
            if (!attr.value.empty() && attr.value.front() == L'{') return Fail(member);
            if (_wcsicmp(attr.value.c_str(), L"True") != 0 && _wcsicmp(attr.value.c_str(), L"False") != 0)
                return Fail(member, E_INVALIDARG);
            node.active = _wcsicmp(attr.value.c_str(), L"True") == 0;
            node.hasActive = true;
        } else if ((node.type == L"ProgressRing" || node.type == L"TextBlock") && attr.space.empty() &&
                   attr.name == L"Foreground") {
            auto hr = ParseSourceColor(attr.value, node.foregroundColor);
            if (FAILED(hr)) return Fail(member, hr);
            node.hasForeground = true;
        } else if (node.type == L"MediaElement" && attr.space.empty() && attr.name == L"AudioCategory") {
            if (!attr.value.empty() && attr.value.front() == L'{') return Fail(member);
            auto value = Choice(attr.value, {L"Other", L"ForegroundOnlyMedia", L"BackgroundCapableMedia",
                                             L"Communications", L"Alerts", L"SoundEffects", L"GameEffects",
                                             L"GameMedia", L"GameChat", L"Speech", L"Movie", L"Media"});
            if (value < 0) return Fail(member, E_INVALIDARG);
            node.audioCategory = static_cast<ABI::Windows::UI::Xaml::Media::AudioCategory>(value);
            node.hasAudioCategory = true;
        } else if (node.type == L"MediaElement" && attr.space.empty() && attr.name == L"AutoPlay") {
            if (!attr.value.empty() && attr.value.front() == L'{') return Fail(member);
            if (_wcsicmp(attr.value.c_str(), L"True") != 0 && _wcsicmp(attr.value.c_str(), L"False") != 0)
                return Fail(member, E_INVALIDARG);
            node.autoPlay = _wcsicmp(attr.value.c_str(), L"True") == 0;
            node.hasAutoPlay = true;
        } else if (node.type == L"TextBlock" && attr.space.empty() && attr.name == L"Text") {
            if (!attr.value.empty() && attr.value.front() == L'{') return Fail(member);
            node.textValue = attr.value;
            node.hasText = true;
        } else if (node.type == L"TextBlock" && attr.space.empty() && attr.name == L"FontFamily") {
            if (attr.value.empty()) return Fail(member, E_INVALIDARG);
            if (attr.value.front() == L'{') return Fail(member);
            node.familyName = attr.value;
            node.hasFontFamily = true;
        } else if (node.type == L"TextBlock" && attr.space.empty() && attr.name == L"FontSize") {
            std::wistringstream number(attr.value);
            number.imbue(std::locale::classic());
            if (!(number >> node.fontSize) || !(number >> std::ws).eof() || !std::isfinite(node.fontSize) ||
                node.fontSize < 0)
                return Fail(member, E_INVALIDARG);
            node.hasFontSize = true;
        } else if (node.type == L"TextBlock" && attr.space.empty() && attr.name == L"TextWrapping") {
            auto value = Choice(attr.value, {L"NoWrap", L"Wrap", L"WrapWholeWords"});
            if (value < 0) return Fail(member, E_INVALIDARG);
            node.wrapping = static_cast<TextWrapping>(value + 1);
            node.hasWrapping = true;
        } else if (node.type == L"TextBlock" && attr.space.empty() && attr.name == L"TextAlignment") {
            auto value = Choice(attr.value, {L"Center", L"Left", L"Right", L"Justify", L"DetectFromContent"});
            if (attr.value == L"Start") value = TextAlignment_Start;
            if (attr.value == L"End") value = TextAlignment_End;
            if (value < 0) return Fail(member, E_INVALIDARG);
            node.textAlignment = static_cast<TextAlignment>(value);
            node.hasTextAlignment = true;
        } else if (node.type == L"Image" && attr.space.empty() && attr.name == L"Source") {
            if (!attr.value.empty() && attr.value.front() == L'{') return Fail(member);
            node.imageUri = attr.value;
            node.hasImageSource = true;
        } else if ((node.type == L"Image" || node.type == L"Viewbox") && attr.space.empty() &&
                   attr.name == L"Stretch") {
            if (!attr.value.empty() && attr.value.front() == L'{') return Fail(member);
            auto value = Choice(attr.value, {L"None", L"Fill", L"Uniform", L"UniformToFill"});
            if (value < 0) return Fail(member, E_INVALIDARG);
            node.stretch = static_cast<ABI::Windows::UI::Xaml::Media::Stretch>(value);
            node.hasStretch = true;
        } else if (node.type == L"Viewbox" && attr.space.empty() && attr.name == L"StretchDirection") {
            if (!attr.value.empty() && attr.value.front() == L'{') return Fail(member);
            auto value = Choice(attr.value, {L"UpOnly", L"DownOnly", L"Both"});
            if (value < 0) return Fail(member, E_INVALIDARG);
            node.direction = static_cast<StretchDirection>(value);
            node.hasDirection = true;
        } else if (!nonvisual && attr.space.empty() && attr.name == L"Margin") {
            auto hr = ParseMargin(attr.value, node.margin);
            if (FAILED(hr)) return Fail(member, hr);
            node.hasMargin = true;
        } else if (!nonvisual && attr.space.empty() && attr.name == L"HorizontalAlignment") {
            auto value = Choice(attr.value, {L"Left", L"Center", L"Right", L"Stretch"});
            if (value < 0) return Fail(member, E_INVALIDARG);
            node.horizontal = static_cast<HorizontalAlignment>(value);
            node.hasHorizontal = true;
        } else if (!nonvisual && attr.space.empty() && attr.name == L"VerticalAlignment") {
            auto value = Choice(attr.value, {L"Top", L"Center", L"Bottom", L"Stretch"});
            if (value < 0) return Fail(member, E_INVALIDARG);
            node.vertical = static_cast<VerticalAlignment>(value);
            node.hasVertical = true;
        } else if (!nonvisual && attr.space.empty() && (attr.name == L"Width" || attr.name == L"Height")) {
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
        } else if (!nonvisual && attr.space.empty() && attr.name == L"Visibility") {
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
        if (!nonvisual && child->space == Presentation && child->type == node.type + L".Resources") {
            auto hr = PrepareResources(node, *child, spaces, ignoreDesign, names, connections);
            if (FAILED(hr)) return hr;
            continue;
        }
        if (node.type == L"Grid" && child->space == Presentation && child->type == L"Grid.Background") {
            if (node.background || child->children.size() != 1)
                return Fail(L"Grid.Background multiple values", E_INVALIDARG);
            auto backgroundSpaces = spaces;
            for (const auto& attr : child->attributes) {
                if (attr.space != Xmlns) return Fail(L"Grid.Background." + attr.name);
                backgroundSpaces[attr.name] = attr.value;
            }
            auto* value = child->children[0].get();
            auto hr = Prepare(*value, backgroundSpaces, ignoreDesign, names, connections, false);
            if (FAILED(hr)) return hr;
            if (!BrushType(value->type)) return Fail(L"Grid.Background type " + value->type, E_INVALIDARG);
            node.background = value;
            continue;
        }
        if ((node.type == L"ProgressRing" || node.type == L"TextBlock") && child->space == Presentation &&
            child->type == node.type + L".Foreground") {
            if (node.hasForeground || child->children.size() != 1)
                return Fail(node.type + L".Foreground multiple values", E_INVALIDARG);
            auto foregroundSpaces = spaces;
            for (const auto& attr : child->attributes) {
                if (attr.space != Xmlns) return Fail(node.type + L".Foreground." + attr.name);
                foregroundSpaces[attr.name] = attr.value;
            }
            auto* value = child->children[0].get();
            auto hr = Prepare(*value, foregroundSpaces, ignoreDesign, names, connections, false);
            if (FAILED(hr)) return hr;
            if (!BrushType(value->type)) return Fail(node.type + L".Foreground type " + value->type, E_INVALIDARG);
            node.foregroundNode = value;
            node.hasForeground = true;
            continue;
        }
        if (node.type == L"LinearGradientBrush") {
            Node* holder = nullptr;
            auto stopSpaces = spaces;
            if (child->space == Presentation && child->type == L"LinearGradientBrush.GradientStops") {
                if (node.explicitStops || !node.stops.empty())
                    return Fail(L"GradientStops multiple values", E_INVALIDARG);
                for (const auto& attr : child->attributes) {
                    if (attr.space != Xmlns) return Fail(L"GradientStops." + attr.name);
                    stopSpaces[attr.name] = attr.value;
                }
                node.explicitStops = true;
                holder = child.get();
            } else if (node.explicitStops)
                return Fail(L"GradientStops mixed values", E_INVALIDARG);
            auto prepareStop = [&](Node& entry) -> HRESULT {
                auto hr = Prepare(entry, stopSpaces, ignoreDesign, names, connections, false);
                if (FAILED(hr)) return hr;
                if (entry.type != L"GradientStop") return Fail(L"GradientStops type " + entry.type, E_INVALIDARG);
                node.stops.push_back(&entry);
                return S_OK;
            };
            if (holder) {
                for (auto& entry : holder->children) {
                    auto hr = prepareStop(*entry);
                    if (FAILED(hr)) return hr;
                }
            } else {
                auto hr = prepareStop(*child);
                if (FAILED(hr)) return hr;
            }
            continue;
        }
        if (node.type == L"GradientStop" || node.type == L"SolidColorBrush") return Fail(node.type + L".Children");
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
        if (animation || node.type == L"Image" || node.type == L"ProgressRing" || node.type == L"TextBlock" ||
            node.type == L"MediaElement" || node.type == L"ContentPresenter")
            return Fail(node.type + L".Children");
        if (node.type == L"Grid") {
            Node* holder = nullptr;
            auto childSpaces = spaces;
            if (child->space == Presentation && child->type == L"Grid.Children") {
                if (node.explicitVisuals || !node.visuals.empty())
                    return Fail(L"Grid.Children multiple values", E_INVALIDARG);
                for (const auto& attr : child->attributes) {
                    if (attr.space != Xmlns) return Fail(L"Grid.Children." + attr.name);
                    childSpaces[attr.name] = attr.value;
                }
                node.explicitVisuals = true;
                holder = child.get();
            } else {
                if (child->space == Presentation && child->type.find(L'.') != std::wstring::npos)
                    return Fail(child->type);
                if (node.explicitVisuals) return Fail(L"Grid.Children mixed values", E_INVALIDARG);
            }
            auto prepareVisual = [&](Node& entry) -> HRESULT {
                auto hr = Prepare(entry, childSpaces, ignoreDesign, names, connections, false);
                if (FAILED(hr)) return hr;
                if (AnimationType(entry.type) || BrushDataType(entry.type))
                    return Fail(L"Grid.Children nonvisual value", E_INVALIDARG);
                node.visuals.push_back(&entry);
                return S_OK;
            };
            if (holder) {
                for (auto& entry : holder->children) {
                    auto hr = prepareVisual(*entry);
                    if (FAILED(hr)) return hr;
                }
            } else {
                auto hr = prepareVisual(*child);
                if (FAILED(hr)) return hr;
            }
            continue;
        }
        Node* content = child.get();
        auto contentSpaces = spaces;
        const auto property = node.type + (node.type == L"Viewbox" ? L".Child" : L".Content");
        if (child->space == Presentation && child->type == property) {
            if (child->children.size() != 1) return Fail(property, E_INVALIDARG);
            if (node.type == L"Viewbox") {
                for (const auto& attr : child->attributes) {
                    if (attr.space != Xmlns) return Fail(property + L"." + attr.name);
                    contentSpaces[attr.name] = attr.value;
                }
            } else if (!child->attributes.empty())
                return Fail(property, E_INVALIDARG);
            content = child->children[0].get();
        }
        if (node.content) return Fail(property + L" multiple values", E_INVALIDARG);
        auto hr = Prepare(*content, contentSpaces, ignoreDesign, names, connections, false);
        if (FAILED(hr)) return hr;
        if ((node.type == L"Page" || node.type == L"Viewbox") &&
            (AnimationType(content->type) || BrushDataType(content->type)))
            return Fail(property + L" nonvisual value", E_INVALIDARG);
        node.content = content;
    }
    return S_OK;
}
HRESULT Build(Node& node);
HRESULT BindPresenters(Node& node, const std::shared_ptr<TemplateLink>& link) {
    if (node.type == L"ContentPresenter") return BindContentPresenter(node.value.Get(), link);
    for (auto* visual : node.visuals) {
        auto hr = BindPresenters(*visual, link);
        if (FAILED(hr)) return hr;
    }
    return node.content ? BindPresenters(*node.content, link) : S_OK;
}
HRESULT CreateStyle(Node& node) {
    std::vector<StyleSetter> setters;
    if (node.templateNode) {
        auto hr = Build(*node.templateNode);
        ComPtr<IControlTemplate> value;
        if (SUCCEEDED(hr)) hr = node.templateNode->value.As(&value);
        if (FAILED(hr)) return hr;
        setters.push_back([value](IInspectable* target) -> HRESULT {
            ComPtr<IControl> control;
            auto hr = target->QueryInterface(IID_PPV_ARGS(&control));
            return FAILED(hr) ? hr : control->put_Template(value.Get());
        });
    }
    ComPtr<IStyle> style;
    auto hr = MakeSourceStyle(ControlType(node.targetType), std::move(setters), &style);
    return FAILED(hr) ? hr : style.As(&node.value);
}
// Each instantiation rebuilds the prepared content, giving every templated parent its own elements.
HRESULT CreateTemplate(Node& node) {
    auto content = node.templateContent;
    ComPtr<IControlTemplate> value;
    auto hr = MakeSourceControlTemplate(
        node.targetType.empty() ? std::wstring() : ControlType(node.targetType),
        [content](const std::shared_ptr<TemplateLink>& link, ComPtr<IUIElement>& root) -> HRESULT {
            if (!content) return S_OK;
            try {
                auto result = Build(*content);
                if (SUCCEEDED(result)) result = BindPresenters(*content, link);
                return FAILED(result) ? result : content->value.As(&root);
            } catch (const std::bad_alloc&) {
                return E_OUTOFMEMORY;
            }
        },
        &value);
    return FAILED(hr) ? hr : value.As(&node.value);
}
HRESULT Create(Node& node) {
    if (node.type == L"Style") return CreateStyle(node);
    if (node.type == L"ControlTemplate") return CreateTemplate(node);
    if (node.type == L"MediaElement") return MakeMediaElement(&node.value);
    if (node.type == L"ContentPresenter") return MakeContentPresenter(&node.value);
    if (node.type == L"Image" || node.type == L"ProgressRing" || node.type == L"TextBlock" || node.type == L"Viewbox") {
        ComPtr<IActivationFactory> factory;
        auto name = L"Windows.UI.Xaml.Controls." + node.type;
        auto hr = ShimGetFactory(Wrappers::HStringReference(name.c_str()).Get(), &factory);
        return FAILED(hr) ? hr : factory->ActivateInstance(&node.value);
    }
    if (BrushDataType(node.type)) {
        auto name = L"Windows.UI.Xaml.Media." + node.type;
        ComPtr<IActivationFactory> factory;
        auto hr = ShimGetFactory(Wrappers::HStringReference(name.c_str()).Get(), &factory);
        return FAILED(hr) ? hr : factory->ActivateInstance(&node.value);
    }
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
    if (node.type == L"Grid") {
        if (node.background) {
            ComPtr<ABI::Windows::UI::Xaml::Media::IBrush> brush;
            auto hr = node.background->value.As(&brush);
            ComPtr<IPanel> panel;
            if (SUCCEEDED(hr)) hr = node.value.As(&panel);
            if (SUCCEEDED(hr)) hr = panel->put_Background(brush.Get());
            if (FAILED(hr)) return hr;
        }
        return SetGridChildren(node.value.Get(), node.visualValues);
    }
    if (node.type == L"Viewbox") {
        ComPtr<IUIElement> child;
        auto hr = node.content ? node.content->value.As(&child) : S_OK;
        return FAILED(hr) ? hr : SetSourceViewboxChild(node.value.Get(), child.Get());
    }
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
    if (BrushDataType(node.type)) return ConfigureBrushData(node.value.Get(), node.type, node.brushData);
    if (AnimationType(node.type))
        return ConfigureAnimation(node.value.Get(), node.hasDuration ? &node.duration : nullptr,
                                  node.hasTargetName ? &node.targetName : nullptr,
                                  node.hasThemeName ? &node.themeName : nullptr,
                                  node.hasTargetProperty ? &node.targetProperty : nullptr);
    if (node.type == L"Style" || node.type == L"ControlTemplate") return S_OK;
    ComPtr<IFrameworkElement> element;
    auto hr = node.value.As(&element);
    if (SUCCEEDED(hr) && !node.name.empty())
        hr = element->put_Name(Wrappers::HStringReference(node.name.c_str()).Get());
    if (SUCCEEDED(hr) && node.hasResources) hr = element->put_Resources(node.dictionary.Get());
    if (SUCCEEDED(hr) && node.hasWidth) hr = element->put_Width(node.width);
    if (SUCCEEDED(hr) && node.hasHeight) hr = element->put_Height(node.height);
    if (SUCCEEDED(hr) && node.hasMargin) hr = element->put_Margin(node.margin);
    if (SUCCEEDED(hr) && node.hasHorizontal) hr = element->put_HorizontalAlignment(node.horizontal);
    if (SUCCEEDED(hr) && node.hasVertical) hr = element->put_VerticalAlignment(node.vertical);
    if (SUCCEEDED(hr) && node.hasVisibility) {
        ComPtr<IUIElement> ui;
        hr = node.value.As(&ui);
        if (SUCCEEDED(hr)) hr = ui->put_Visibility(node.visibility);
    }
    if (node.type == L"Image") {
        ComPtr<IImage> image;
        if (SUCCEEDED(hr)) hr = node.value.As(&image);
        if (SUCCEEDED(hr) && node.hasImageSource) hr = image->put_Source(node.imageSource.Get());
        if (SUCCEEDED(hr) && node.hasStretch) hr = image->put_Stretch(node.stretch);
        return hr;
    }
    if (node.type == L"TextBlock") {
        ComPtr<ITextBlock> block;
        if (SUCCEEDED(hr)) hr = node.value.As(&block);
        if (SUCCEEDED(hr) && node.hasText)
            hr = block->put_Text(Wrappers::HStringReference(node.textValue.c_str()).Get());
        if (SUCCEEDED(hr) && node.hasFontSize) hr = block->put_FontSize(node.fontSize);
        if (SUCCEEDED(hr) && node.hasFontFamily) hr = block->put_FontFamily(node.fontFamily.Get());
        if (SUCCEEDED(hr) && node.hasForeground) hr = block->put_Foreground(node.foreground.Get());
        if (SUCCEEDED(hr) && node.hasWrapping) hr = block->put_TextWrapping(node.wrapping);
        if (SUCCEEDED(hr) && node.hasTextAlignment) hr = block->put_TextAlignment(node.textAlignment);
        return hr;
    }
    if (node.type == L"ProgressRing") {
        ComPtr<IProgressRing> ring;
        if (SUCCEEDED(hr)) hr = node.value.As(&ring);
        if (SUCCEEDED(hr) && node.hasActive) hr = ring->put_IsActive(node.active);
        if (SUCCEEDED(hr) && node.hasForeground) {
            ComPtr<IControl> control;
            hr = node.value.As(&control);
            if (SUCCEEDED(hr)) hr = control->put_Foreground(node.foreground.Get());
        }
        return hr;
    }
    if (node.type == L"MediaElement") {
        ComPtr<IMediaElement> media;
        if (SUCCEEDED(hr)) hr = node.value.As(&media);
        if (SUCCEEDED(hr) && node.hasAudioCategory) hr = media->put_AudioCategory(node.audioCategory);
        if (SUCCEEDED(hr) && node.hasAutoPlay) hr = media->put_AutoPlay(node.autoPlay);
        return hr;
    }
    if (node.type == L"ContentPresenter") return hr;
    if (node.type == L"Viewbox") {
        ComPtr<IViewbox> viewbox;
        if (SUCCEEDED(hr)) hr = node.value.As(&viewbox);
        if (SUCCEEDED(hr) && node.hasStretch) hr = viewbox->put_Stretch(node.stretch);
        if (SUCCEEDED(hr) && node.hasDirection) hr = viewbox->put_StretchDirection(node.direction);
    }
    return FAILED(hr) ? hr : SetContent(node);
}
HRESULT LoadSourceDictionary(const std::wstring& path, std::vector<std::wstring>& loading,
                             ComPtr<IResourceDictionary>& dictionary);
HRESULT BuildParts(Node& node) {
    if (node.type == L"TextBlock" && node.hasFontFamily) {
        auto hr =
            MakeFontFamily(Wrappers::HStringReference(node.familyName.c_str()).Get(), node.fontFamily.GetAddressOf());
        if (FAILED(hr)) return hr;
    }
    if ((node.type == L"ProgressRing" || node.type == L"TextBlock") && node.hasForeground) {
        HRESULT hr;
        if (node.foregroundNode) {
            hr = Build(*node.foregroundNode);
            if (SUCCEEDED(hr)) hr = node.foregroundNode->value.As(&node.foreground);
        } else
            hr = MakeSolidColorBrush(node.foregroundColor, node.foreground.GetAddressOf());
        if (FAILED(hr)) return hr;
    }
    if (node.type == L"Image" && node.hasImageSource) {
        auto hr = MakePackageImageSource(node.imageUri, node.imageSource.GetAddressOf());
        if (FAILED(hr)) return Fail(L"Image.Source", hr);
    }
    if (node.background) {
        auto hr = Build(*node.background);
        if (FAILED(hr)) return hr;
    }
    if (node.type == L"LinearGradientBrush") {
        std::vector<ComPtr<ABI::Windows::UI::Xaml::Media::IGradientStop>> stops;
        for (auto* stop : node.stops) {
            auto hr = Build(*stop);
            ComPtr<ABI::Windows::UI::Xaml::Media::IGradientStop> value;
            if (SUCCEEDED(hr)) hr = stop->value.As(&value);
            if (FAILED(hr)) return hr;
            stops.push_back(std::move(value));
        }
        auto hr = SetSourceGradientStops(node.value.Get(), std::move(stops));
        if (FAILED(hr)) return hr;
    }
    if (node.type == L"Grid") {
        auto values = std::make_shared<VisualChildren>();
        for (auto* visual : node.visuals) {
            auto hr = Build(*visual);
            ComPtr<IUIElement> child;
            if (SUCCEEDED(hr)) hr = visual->value.As(&child);
            if (FAILED(hr)) return hr;
            values->push_back(std::move(child));
        }
        node.visualValues = std::move(values);
        // Allocate the stable collection before root properties can change.
        ComPtr<IPanel> panel;
        auto hr = node.value.As(&panel);
        ComPtr<GridChildren> collection;
        if (SUCCEEDED(hr)) hr = panel->get_Children(&collection);
        if (FAILED(hr)) return hr;
    }
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
        if (node.hasMerged) {
            std::vector<ComPtr<IResourceDictionary>> merged;
            std::vector<std::wstring> loading;
            for (const auto& path : node.mergedSources) {
                ComPtr<IResourceDictionary> dictionary;
                hr = LoadSourceDictionary(path, loading, dictionary);
                if (FAILED(hr)) return hr;
                merged.push_back(std::move(dictionary));
            }
            hr = SetSourceDictionaryParts(node.dictionary.Get(), L"", std::move(merged), {});
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
    else if (!BrushDataType(node.type)) {
        ComPtr<INamedElementRegistry> registry;
        hr = node.value.As(&registry);
        if (SUCCEEDED(hr)) registry->SetSourceNameScope(scope, root);
    }
    if (FAILED(hr)) return hr;
    if (!root && !node.name.empty()) scope->elements[node.name] = node.value;
    if (node.foregroundNode) {
        hr = AttachNames(*node.foregroundNode, scope, false);
        if (FAILED(hr)) return hr;
    }
    if (node.background) {
        hr = AttachNames(*node.background, scope, false);
        if (FAILED(hr)) return hr;
    }
    for (auto* stop : node.stops) {
        hr = AttachNames(*stop, scope, false);
        if (FAILED(hr)) return hr;
    }
    for (auto* visual : node.visuals) {
        hr = AttachNames(*visual, scope, false);
        if (FAILED(hr)) return hr;
    }
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
    if (node.foregroundNode) {
        hr = Connect(*node.foregroundNode, connector);
        if (FAILED(hr)) return hr;
    }
    if (node.background) {
        hr = Connect(*node.background, connector);
        if (FAILED(hr)) return hr;
    }
    for (auto* stop : node.stops) {
        hr = Connect(*stop, connector);
        if (FAILED(hr)) return hr;
    }
    for (auto* visual : node.visuals) {
        hr = Connect(*visual, connector);
        if (FAILED(hr)) return hr;
    }
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
HRESULT ReadSource(const std::wstring& path, std::unique_ptr<Node>& root) {
    ComPtr<IStream> stream;
    auto hr = OpenPackageResource(CurrentPackage(), path, &stream);
    if (hr == S_FALSE) return S_FALSE;
    if (FAILED(hr)) return hr;
    ComPtr<IXmlReader> reader;
    hr = CreateXmlReader(IID_PPV_ARGS(&reader), nullptr);
    if (SUCCEEDED(hr)) hr = reader->SetProperty(XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit);
    if (SUCCEEDED(hr)) hr = reader->SetProperty(XmlReaderProperty_MaxElementDepth, 128);
    if (SUCCEEDED(hr)) hr = reader->SetInput(stream.Get());
    if (SUCCEEDED(hr)) hr = Read(reader.Get(), path, root);
    return hr;
}
HRESULT EntryKey(const Node& entry, std::wstring& key) {
    const Attribute* found = nullptr;
    for (const auto& attr : entry.attributes)
        if (attr.space == Language && attr.name == L"Key") found = &attr;
    for (const auto& attr : entry.attributes)
        if (!found && (attr.space == Language || attr.space.empty()) && attr.name == L"Name") found = &attr;
    if (!found) {
        bool targeted = std::any_of(entry.attributes.begin(), entry.attributes.end(),
                                    [](const Attribute& attr) { return attr.space.empty() && attr.name == L"TargetType"; });
        return targeted ? Fail(entry.type + L" implicit key") : Fail(entry.type + L" resource key", E_INVALIDARG);
    }
    if (found->value.empty()) return Fail(entry.type + L" resource key", E_INVALIDARG);
    if (found->value.front() == L'{') return Fail(entry.type + L" resource key markup");
    key = found->value;
    return S_OK;
}
// A source dictionary's own entries are prepared and built on first lookup,
// as Windows 8.1+ creates dictionary resources on demand; only its structure
// (keys, theme keys and merged sources) is validated when it loads.
HRESULT LoadSourceDictionary(const std::wstring& path, std::vector<std::wstring>& loading,
                             ComPtr<IResourceDictionary>& dictionary) {
    if (std::find(loading.begin(), loading.end(), path) != loading.end())
        return Fail(L"ResourceDictionary.Source cycle " + path, E_INVALIDARG);
    std::unique_ptr<Node> parsed;
    auto hr = ReadSource(path, parsed);
    if (hr == S_FALSE) return Fail(L"ResourceDictionary.Source " + path, HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
    if (FAILED(hr)) return hr;
    Log("TRACE", "source resource dictionary load %ls", path.c_str());
    std::shared_ptr<Node> document(std::move(parsed));
    if (document->space != Presentation || document->type != L"ResourceDictionary")
        return Fail(L"resource dictionary root " + document->type, E_INVALIDARG);
    if (!document->text.empty()) return Fail(L"text content");
    std::map<std::wstring, std::wstring> spaces;
    bool ignoreDesign = false;
    for (const auto& attr : document->attributes)
        if (attr.space == Xmlns) spaces[attr.name] = attr.value;
    for (const auto& attr : document->attributes) {
        if (attr.space == Xmlns || (ignoreDesign && attr.space == Design)) continue;
        if (attr.space == Compatibility && attr.name == L"Ignorable") {
            std::wistringstream tokens(attr.value);
            std::wstring prefix;
            while (tokens >> prefix) {
                if (spaces[prefix] != Design) return Fail(L"mc:Ignorable " + prefix);
                ignoreDesign = true;
            }
        } else if (attr.space != Design)
            return Fail(L"ResourceDictionary." + attr.name);
    }
    struct Deferred {
        std::shared_ptr<Node> document;
        Node* node;
        std::wstring key;
        std::map<std::wstring, std::wstring> spaces;
        bool ignoreDesign;
        HRESULT failure = S_OK;
    };
    std::vector<std::pair<std::wstring, Node*>> entries;
    std::set<std::wstring> keys, themeKeys;
    bool hasTheme = false;
    for (auto& child : document->children) {
        if (child->space == Presentation && child->type == L"ResourceDictionary.MergedDictionaries") {
            hr = RejectText(*child);
            if (SUCCEEDED(hr)) hr = PrepareMerged(*document, *child);
            if (FAILED(hr)) return hr;
            continue;
        }
        if (child->space == Presentation && child->type == L"ResourceDictionary.ThemeDictionaries") {
            if (hasTheme) return Fail(child->type + L" multiple values", E_INVALIDARG);
            hasTheme = true;
            if (!child->text.empty()) return Fail(L"text content");
            for (const auto& attr : child->attributes)
                if (attr.space != Xmlns) return Fail(child->type + L"." + attr.name);
            for (auto& theme : child->children) {
                std::wstring name;
                if (theme->space != Presentation || theme->type != L"ResourceDictionary")
                    return Fail(child->type + L" type " + theme->type, E_INVALIDARG);
                if (!theme->text.empty()) return Fail(L"text content");
                hr = EntryKey(*theme, name);
                if (FAILED(hr)) return hr;
                for (auto& entry : theme->children) {
                    hr = EntryKey(*entry, name);
                    if (FAILED(hr)) return hr;
                    themeKeys.insert(name);
                }
            }
            continue;
        }
        if (child->space == Presentation && child->type.find(L'.') != std::wstring::npos) return Fail(child->type);
        std::wstring key;
        hr = EntryKey(*child, key);
        if (FAILED(hr)) return hr;
        if (!keys.insert(key).second) return Fail(L"ResourceDictionary duplicate key " + key, E_INVALIDARG);
        entries.emplace_back(key, child.get());
    }
    std::vector<ComPtr<IResourceDictionary>> merged;
    loading.push_back(path);
    for (const auto& source : document->mergedSources) {
        ComPtr<IResourceDictionary> value;
        hr = LoadSourceDictionary(source, loading, value);
        if (FAILED(hr)) return hr;
        merged.push_back(std::move(value));
    }
    loading.pop_back();
    hr = MakeResourceDictionary(&dictionary);
    for (auto& entry : entries) {
        if (FAILED(hr)) break;
        auto state = std::make_shared<Deferred>(Deferred{document, entry.second, entry.first, spaces, ignoreDesign});
        hr = AddDeferredSourceResource(dictionary.Get(), entry.first, [state](ComPtr<IInspectable>& value) -> HRESULT {
            // Failures are kept: Prepare annotates the node, so it cannot run twice.
            if (FAILED(state->failure)) return state->failure;
            try {
                std::set<std::wstring> names;
                std::set<int> connections;
                auto result = RejectText(*state->node);
                if (SUCCEEDED(result))
                    result = Prepare(*state->node, state->spaces, state->ignoreDesign, names, connections, false, true);
                if (SUCCEEDED(result) && !connections.empty()) result = Fail(L"source resource connections");
                if (SUCCEEDED(result)) result = Build(*state->node);
                if (FAILED(result)) {
                    Log("FAIL", "source resource %ls in %ls hr=%08lx", state->key.c_str(),
                        state->document->document->c_str(), result);
                    state->failure = result;
                    return result;
                }
                value = state->node->value;
                return S_OK;
            } catch (const std::bad_alloc&) {
                return E_OUTOFMEMORY;
            }
        });
    }
    if (SUCCEEDED(hr)) hr = SetSourceDictionaryParts(dictionary.Get(), path, std::move(merged), std::move(themeKeys));
    if (FAILED(hr)) dictionary.Reset();
    return hr;
}
HRESULT PrepareApplication(Node& root, ApplicationTheme& theme, bool& hasTheme) {
    if (root.space != Presentation || root.type != L"Application") return Fail(L"application root", E_INVALIDARG);
    std::map<std::wstring, std::wstring> spaces;
    for (const auto& attr : root.attributes)
        if (attr.space == Xmlns)
            spaces[attr.name] = attr.value;
        else if (attr.space == Language && attr.name == L"Class")
            root.className = attr.value;
        else if (attr.space.empty() && attr.name == L"RequestedTheme") {
            if (attr.value == L"Dark")
                theme = ApplicationTheme_Dark;
            else if (attr.value == L"Light")
                theme = ApplicationTheme_Light;
            else
                return Fail(L"Application.RequestedTheme", E_INVALIDARG);
            hasTheme = true;
        } else
            return Fail(L"Application." + attr.name);
    std::set<std::wstring> names;
    std::set<int> connections;
    for (auto& child : root.children) {
        if (child->space != Presentation || child->type != L"Application.Resources") return Fail(child->type);
        auto hr = PrepareResources(root, *child, spaces, false, names, connections);
        if (FAILED(hr)) return hr;
    }
    if (!names.empty() || !connections.empty()) return Fail(L"application resource names/connections");
    return S_OK;
}
}
HRESULT ResolveSourceResourceUri(ABI::Windows::Foundation::IUriRuntimeClass* uri, std::wstring& path) {
    return uri ? ResourcePath(uri, path) : E_INVALIDARG;
}
HRESULT LoadApplicationSourceXaml(IInspectable* component, const std::wstring& path) {
    try {
        if (!component) return E_INVALIDARG;
        ComPtr<IApplicationSource> source;
        auto hr = component->QueryInterface(IID_PPV_ARGS(&source));
        if (SUCCEEDED(hr)) hr = source->CheckSourceThread();
        if (FAILED(hr)) return hr;
        ComPtr<IApplication> application;
        hr = component->QueryInterface(IID_PPV_ARGS(&application));
        if (FAILED(hr)) return hr;
        std::unique_ptr<Node> root;
        hr = ReadSource(path, root);
        if (hr == S_OK) hr = RejectText(*root);
        if (hr != S_OK) return hr;
        Log("TRACE", "application source XAML load %ls", path.c_str());
        ApplicationTheme theme{};
        bool hasTheme = false;
        hr = PrepareApplication(*root, theme, hasTheme);
        if (FAILED(hr)) return hr;
        if (!root->className.empty()) {
            Wrappers::HString name;
            hr = component->GetRuntimeClassName(name.GetAddressOf());
            if (FAILED(hr)) return hr;
            if (root->className != name.GetRawBuffer(nullptr)) return Fail(L"x:Class " + root->className, E_INVALIDARG);
        }
        // Build/validate the whole resource subtree before changing the application.
        hr = BuildParts(*root);
        if (FAILED(hr)) return hr;
        if (hasTheme) hr = application->put_RequestedTheme(theme);
        if (SUCCEEDED(hr) && root->hasResources) hr = application->put_Resources(root->dictionary.Get());
        if (SUCCEEDED(hr)) hr = source->CompleteSourceInitialization();
        return hr;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
}
HRESULT LoadSourceXaml(IInspectable* component, ABI::Windows::Foundation::IUriRuntimeClass* uri) {
    try {
        if (!component || !uri) return E_INVALIDARG;
        ComPtr<IApplication> application;
        auto applicationHr = component->QueryInterface(IID_PPV_ARGS(&application));
        if (SUCCEEDED(applicationHr)) {
            std::wstring path;
            auto hr = ResourcePath(uri, path);
            if (SUCCEEDED(hr)) hr = LoadApplicationSourceXaml(component, path);
            return hr == S_FALSE ? HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) : hr;
        }
        if (applicationHr != E_NOINTERFACE) return applicationHr;
        ComPtr<INamedElementRegistry> rootRegistry;
        auto hr = component->QueryInterface(IID_PPV_ARGS(&rootRegistry));
        if (SUCCEEDED(hr)) hr = rootRegistry->CheckSourceThread();
        if (FAILED(hr)) return hr;
        std::wstring path;
        hr = ResourcePath(uri, path);
        if (FAILED(hr)) return hr;
        Log("TRACE", "source XAML load %ls", path.c_str());
        std::unique_ptr<Node> root;
        hr = ReadSource(path, root);
        if (hr == S_FALSE) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        if (SUCCEEDED(hr)) hr = RejectText(*root);
        std::set<std::wstring> names;
        std::set<int> connections;
        if (SUCCEEDED(hr)) hr = Prepare(*root, {}, false, names, connections, true);
        if (FAILED(hr)) return hr;
        root->value = component;
        ComPtr<IFrameworkElement> element;
        hr = component->QueryInterface(IID_PPV_ARGS(&element));
        ComPtr<IInspectable> kind;
        if (SUCCEEDED(hr))
            hr = root->type == L"Page"           ? component->QueryInterface(__uuidof(IPage), &kind)
                 : root->type == L"Frame"        ? component->QueryInterface(__uuidof(IFrame), &kind)
                 : root->type == L"Image"        ? component->QueryInterface(__uuidof(IImage), &kind)
                 : root->type == L"Viewbox"      ? component->QueryInterface(__uuidof(IViewbox), &kind)
                 : root->type == L"TextBlock"    ? component->QueryInterface(__uuidof(ITextBlock), &kind)
                 : root->type == L"ProgressRing" ? component->QueryInterface(__uuidof(IProgressRing), &kind)
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
