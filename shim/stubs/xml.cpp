#include "xml.h"
#include "generated/xml.h"
#include <wrl.h>
#include <string>
#include <utility>
#include <vector>
using namespace Microsoft::WRL;
using namespace ABI::Windows::Data::Xml::Dom;
using ABI::Windows::UI::Notifications::TileTemplateType;
using ABI::Windows::UI::Notifications::ToastTemplateType;
namespace appx {
namespace {
class Node;
// Private identity check: only nodes from this DOM can be linked into it.
struct __declspec(uuid("3c5e8f2a-9d41-4b7e-8f06-2a1d7c94e5b3")) INodeImpl : public IInspectable {
    virtual Node* STDMETHODCALLTYPE Impl() = 0;
};
std::wstring Text(HSTRING value) {
    UINT32 length{};
    const auto* raw = WindowsGetStringRawBuffer(value, &length);
    return std::wstring(raw, length);
}
HRESULT Copy(const std::wstring& text, HSTRING* value) {
    if (!value) return E_POINTER;
    return WindowsCreateString(text.data(), static_cast<UINT32>(text.size()), value);
}
std::wstring Escape(const std::wstring& text, bool attribute) {
    std::wstring out;
    for (auto c : text) {
        if (c == L'&')
            out += L"&amp;";
        else if (c == L'<')
            out += L"&lt;";
        else if (c == L'>')
            out += L"&gt;";
        else if (attribute && c == L'"')
            out += L"&quot;";
        else
            out += c;
    }
    return out;
}
using NodeView = ABI::Windows::Foundation::Collections::IVectorView<IXmlNode*>;
using NodeIterable = ABI::Windows::Foundation::Collections::IIterable<IXmlNode*>;
using NodeIteratorType = ABI::Windows::Foundation::Collections::IIterator<IXmlNode*>;
class NodeIterator final : public RuntimeClass<RuntimeClassFlags<WinRt>, NodeIteratorType, FtmBase> {
    InspectableClass(L"Windows.Foundation.Collections.IIterator`1<Windows.Data.Xml.Dom.IXmlNode>", BaseTrust);
    std::vector<ComPtr<IXmlNode>> items_;
    UINT32 index_ = 0;

public:
    explicit NodeIterator(std::vector<ComPtr<IXmlNode>> items) : items_(std::move(items)) {}
    HRESULT STDMETHODCALLTYPE get_Current(IXmlNode** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return index_ < items_.size() ? items_[index_].CopyTo(value) : E_BOUNDS;
    }
    HRESULT STDMETHODCALLTYPE get_HasCurrent(boolean* value) override {
        if (!value) return E_POINTER;
        *value = index_ < items_.size();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE MoveNext(boolean* value) override {
        if (index_ < items_.size()) ++index_;
        return get_HasCurrent(value);
    }
    HRESULT STDMETHODCALLTYPE GetMany(UINT32 capacity, IXmlNode** values, UINT32* actual) override {
        if (!actual) return E_POINTER;
        *actual = 0;
        if (capacity && !values) return E_POINTER;
        while (*actual < capacity && index_ < items_.size()) items_[index_++].CopyTo(&values[(*actual)++]);
        return S_OK;
    }
};
// XmlNodeList also projects as IVectorView/IIterable<IXmlNode>; C++/CX uses
// those for GetAt and range-for.
class NodeList final
    : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultIXmlNodeList, NodeView, NodeIterable, FtmBase> {
    InspectableClass(L"Windows.Data.Xml.Dom.XmlNodeList", BaseTrust);
    APPX_LOG_MISSING_INTERFACES("NodeList")
    std::vector<ComPtr<IXmlNode>> items_;

public:
    explicit NodeList(std::vector<ComPtr<IXmlNode>> items) : items_(std::move(items)) {}
    HRESULT STDMETHODCALLTYPE get_Length(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = static_cast<UINT32>(items_.size());
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Item(UINT32 index, IXmlNode** node) override {
        if (!node) return E_POINTER;
        *node = nullptr;
        return index < items_.size() ? items_[index].CopyTo(node) : S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetAt(UINT32 index, IXmlNode** node) override {
        if (!node) return E_POINTER;
        *node = nullptr;
        return index < items_.size() ? items_[index].CopyTo(node) : E_BOUNDS;
    }
    HRESULT STDMETHODCALLTYPE get_Size(UINT32* value) override { return get_Length(value); }
    HRESULT STDMETHODCALLTYPE IndexOf(IXmlNode* node, UINT32* index, boolean* found) override {
        if (!index || !found) return E_POINTER;
        *index = 0;
        *found = false;
        for (UINT32 i = 0; i < items_.size(); ++i)
            if (items_[i].Get() == node) {
                *index = i;
                *found = true;
                break;
            }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetMany(UINT32 start, UINT32 capacity, IXmlNode** values, UINT32* actual) override {
        if (!actual) return E_POINTER;
        *actual = 0;
        if (capacity && !values) return E_POINTER;
        if (start > items_.size()) return E_BOUNDS;
        for (UINT32 i = start; i < items_.size() && *actual < capacity; ++i) items_[i].CopyTo(&values[(*actual)++]);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE First(NodeIteratorType** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto iterator = Make<NodeIterator>(items_);
        return iterator ? iterator.CopyTo(value) : E_OUTOFMEMORY;
    }
};
HRESULT MakeAttributes(IXmlNode* element, IXmlNamedNodeMap** value);
HRESULT Select(Node* context, const std::wstring& path, bool single, std::vector<ComPtr<IXmlNode>>& found);
// Shared tree state. Children are owned; the parent link is borrowed and
// cleared when the parent goes away.
class Node {
public:
    NodeType type_;
    std::wstring name_, data_;
    std::vector<std::pair<std::wstring, std::wstring>> attributes_;
    std::vector<ComPtr<IXmlNode>> children_;
    Node* parent_ = nullptr;
    IXmlNode* self_ = nullptr;
    Node(NodeType type, std::wstring name) : type_(type), name_(std::move(name)) {}
    virtual ~Node() {
        for (auto& child : children_)
            if (auto* node = From(child.Get())) node->parent_ = nullptr;
    }
    static Node* From(IUnknown* value) {
        ComPtr<INodeImpl> impl;
        return value && SUCCEEDED(value->QueryInterface(IID_PPV_ARGS(&impl))) ? impl->Impl() : nullptr;
    }
    void Detach() {
        if (!parent_) return;
        auto& siblings = parent_->children_;
        for (auto it = siblings.begin(); it != siblings.end(); ++it)
            if (it->Get() == self_) {
                parent_ = nullptr;
                siblings.erase(it);
                return;
            }
        parent_ = nullptr;
    }
    void Collect(const std::wstring& tag, std::vector<ComPtr<IXmlNode>>& found) const {
        for (const auto& child : children_) {
            auto* node = From(child.Get());
            if (node && node->type_ == NodeType_ElementNode) {
                if (tag == L"*" || node->name_ == tag) found.push_back(child);
                node->Collect(tag, found);
            }
        }
    }
    std::wstring InnerText() const {
        if (type_ == NodeType_TextNode) return data_;
        std::wstring text;
        for (const auto& child : children_)
            if (auto* node = From(child.Get())) text += node->InnerText();
        return text;
    }
    std::wstring Xml() const {
        if (type_ == NodeType_TextNode) return Escape(data_, false);
        std::wstring inner;
        for (const auto& child : children_)
            if (auto* node = From(child.Get())) inner += node->Xml();
        if (type_ == NodeType_DocumentNode) return inner;
        std::wstring xml = L"<" + name_;
        for (const auto& attribute : attributes_)
            xml += L" " + attribute.first + L"=\"" + Escape(attribute.second, true) + L"\"";
        return inner.empty() ? xml + L"/>" : xml + L">" + inner + L"</" + name_ + L">";
    }
};
template <class Derived, class... Interfaces>
class NodeBase : public RuntimeClass<RuntimeClassFlags<WinRt>, Interfaces..., DefaultIXmlNode, DefaultIXmlNodeSelector,
                                     DefaultIXmlNodeSerializer, INodeImpl, FtmBase>,
                 public Node {
    friend Derived;
    NodeBase(NodeType type, std::wstring name) : Node(type, std::move(name)) { self_ = static_cast<IXmlNode*>(this); }

public:
    Node* STDMETHODCALLTYPE Impl() override { return this; }
    HRESULT STDMETHODCALLTYPE get_NodeName(HSTRING* value) override {
        return Copy(type_ == NodeType_TextNode       ? L"#text"
                    : type_ == NodeType_DocumentNode ? L"#document"
                                                     : name_,
                    value);
    }
    HRESULT STDMETHODCALLTYPE get_NodeType(NodeType* value) override {
        if (!value) return E_POINTER;
        *value = type_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ParentNode(IXmlNode** value) override {
        if (!value) return E_POINTER;
        *value = parent_ ? parent_->self_ : nullptr;
        if (*value) (*value)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_ChildNodes(IXmlNodeList** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto list = Make<NodeList>(children_);
        return list ? list.CopyTo(value) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE get_FirstChild(IXmlNode** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return children_.empty() ? S_OK : children_.front().CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE get_LastChild(IXmlNode** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return children_.empty() ? S_OK : children_.back().CopyTo(value);
    }
    HRESULT STDMETHODCALLTYPE HasChildNodes(boolean* value) override {
        if (!value) return E_POINTER;
        *value = !children_.empty();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE AppendChild(IXmlNode* child, IXmlNode** appended) override {
        if (!appended) return E_POINTER;
        *appended = nullptr;
        auto* node = From(child);
        if (!node || node->type_ == NodeType_DocumentNode || type_ == NodeType_TextNode) return E_INVALIDARG;
        for (auto* ancestor = static_cast<Node*>(this); ancestor; ancestor = ancestor->parent_)
            if (ancestor == node) return E_INVALIDARG;
        ComPtr<IXmlNode> retained(child);
        node->Detach();
        children_.push_back(retained);
        node->parent_ = this;
        return retained.CopyTo(appended);
    }
    HRESULT STDMETHODCALLTYPE RemoveChild(IXmlNode* child, IXmlNode** removed) override {
        if (!removed) return E_POINTER;
        *removed = nullptr;
        auto* node = From(child);
        if (!node || node->parent_ != this) return E_INVALIDARG;
        ComPtr<IXmlNode> retained(child);
        node->Detach();
        return retained.CopyTo(removed);
    }
    HRESULT STDMETHODCALLTYPE get_InnerText(HSTRING* value) override { return Copy(InnerText(), value); }
    HRESULT STDMETHODCALLTYPE GetXml(HSTRING* value) override { return Copy(Xml(), value); }
    // Elements expose live attribute nodes; other nodes have no attribute map.
    HRESULT STDMETHODCALLTYPE get_Attributes(IXmlNamedNodeMap** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return type_ == NodeType_ElementNode ? MakeAttributes(self_, value) : S_OK;
    }
    HRESULT STDMETHODCALLTYPE SelectSingleNode(HSTRING xpath, IXmlNode** node) override {
        if (!node) return E_POINTER;
        *node = nullptr;
        std::vector<ComPtr<IXmlNode>> found;
        auto hr = Select(this, Text(xpath), true, found);
        return FAILED(hr) || found.empty() ? hr : found.front().CopyTo(node);
    }
    HRESULT STDMETHODCALLTYPE SelectNodes(HSTRING xpath, IXmlNodeList** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        std::vector<ComPtr<IXmlNode>> found;
        auto hr = Select(this, Text(xpath), false, found);
        if (FAILED(hr)) return hr;
        auto list = Make<NodeList>(std::move(found));
        return list ? list.CopyTo(value) : E_OUTOFMEMORY;
    }
};
class TextNode final : public NodeBase<TextNode, DefaultIXmlText, DefaultIXmlCharacterData> {
    InspectableClass(L"Windows.Data.Xml.Dom.XmlText", BaseTrust);
    APPX_LOG_MISSING_INTERFACES("TextNode")
public:
    explicit TextNode(std::wstring data) : NodeBase(NodeType_TextNode, L"") { data_ = std::move(data); }
    HRESULT STDMETHODCALLTYPE get_Data(HSTRING* value) override { return Copy(data_, value); }
    HRESULT STDMETHODCALLTYPE put_Data(HSTRING value) override {
        data_ = Text(value);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Length(UINT32* value) override {
        if (!value) return E_POINTER;
        *value = static_cast<UINT32>(data_.size());
        return S_OK;
    }
};
class Element final : public NodeBase<Element, DefaultIXmlElement> {
    InspectableClass(L"Windows.Data.Xml.Dom.XmlElement", BaseTrust);
    APPX_LOG_MISSING_INTERFACES("Element")
public:
    explicit Element(std::wstring name) : NodeBase(NodeType_ElementNode, std::move(name)) {}
    HRESULT STDMETHODCALLTYPE get_TagName(HSTRING* value) override { return Copy(name_, value); }
    HRESULT STDMETHODCALLTYPE GetAttribute(HSTRING name, HSTRING* value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto key = Text(name);
        for (auto& attribute : attributes_)
            if (attribute.first == key) return Copy(attribute.second, value);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetAttribute(HSTRING name, HSTRING value) override {
        auto key = Text(name);
        if (key.empty()) return E_INVALIDARG;
        for (auto& attribute : attributes_)
            if (attribute.first == key) {
                attribute.second = Text(value);
                return S_OK;
            }
        attributes_.emplace_back(key, Text(value));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetElementsByTagName(HSTRING tag, IXmlNodeList** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        std::vector<ComPtr<IXmlNode>> found;
        Collect(Text(tag), found);
        auto list = Make<NodeList>(std::move(found));
        return list ? list.CopyTo(value) : E_OUTOFMEMORY;
    }
};
class Document final : public NodeBase<Document, DefaultIXmlDocument> {
    InspectableClass(L"Windows.Data.Xml.Dom.XmlDocument", BaseTrust);
    APPX_LOG_MISSING_INTERFACES("Document")
public:
    Document() : NodeBase(NodeType_DocumentNode, L"") {}
    HRESULT STDMETHODCALLTYPE CreateElement(HSTRING tag, IXmlElement** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto name = Text(tag);
        if (name.empty()) return E_INVALIDARG;
        auto element = Make<Element>(name);
        return element ? element.CopyTo(value) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE CreateTextNode(HSTRING data, IXmlText** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        auto text = Make<TextNode>(Text(data));
        return text ? text.CopyTo(value) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE get_DocumentElement(IXmlElement** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        for (auto& child : children_)
            if (SUCCEEDED(child.CopyTo(value))) return S_OK;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetElementsByTagName(HSTRING tag, IXmlNodeList** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        std::vector<ComPtr<IXmlNode>> found;
        Collect(Text(tag), found);
        auto list = Make<NodeList>(std::move(found));
        return list ? list.CopyTo(value) : E_OUTOFMEMORY;
    }
};
// An element's attribute as a node: its value lives in the element, so edits
// through the node are seen by GetAttribute and serialization.
class Attribute final : public NodeBase<Attribute, DefaultIXmlAttribute> {
    InspectableClass(L"Windows.Data.Xml.Dom.XmlAttribute", BaseTrust);
    APPX_LOG_MISSING_INTERFACES("Attribute")
    ComPtr<IXmlNode> owner_;
    std::pair<std::wstring, std::wstring>* Entry() const {
        auto* element = From(owner_.Get());
        for (auto& attribute : element->attributes_)
            if (attribute.first == name_) return &attribute;
        return nullptr;
    }

public:
    Attribute(IXmlNode* owner, std::wstring name) : NodeBase(NodeType_AttributeNode, std::move(name)), owner_(owner) {}
    HRESULT STDMETHODCALLTYPE get_Name(HSTRING* value) override { return Copy(name_, value); }
    HRESULT STDMETHODCALLTYPE get_Specified(boolean* value) override {
        if (!value) return E_POINTER;
        *value = Entry() != nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_Value(HSTRING* value) override {
        auto* entry = Entry();
        return Copy(entry ? entry->second : std::wstring(), value);
    }
    HRESULT STDMETHODCALLTYPE put_Value(HSTRING value) override {
        auto* entry = Entry();
        if (entry)
            entry->second = Text(value);
        else
            From(owner_.Get())->attributes_.emplace_back(name_, Text(value));
        return S_OK;
    }
};
// XmlNamedNodeMap also projects as IVectorView/IIterable<IXmlNode>.
class NamedNodeMap final
    : public RuntimeClass<RuntimeClassFlags<WinRt>, DefaultIXmlNamedNodeMap, NodeView, NodeIterable, FtmBase> {
    InspectableClass(L"Windows.Data.Xml.Dom.XmlNamedNodeMap", BaseTrust);
    APPX_LOG_MISSING_INTERFACES("NamedNodeMap")
    ComPtr<NodeList> items_;

public:
    explicit NamedNodeMap(ComPtr<NodeList> items) : items_(std::move(items)) {}
    HRESULT STDMETHODCALLTYPE get_Length(UINT32* value) override { return items_->get_Length(value); }
    HRESULT STDMETHODCALLTYPE Item(UINT32 index, IXmlNode** node) override { return items_->Item(index, node); }
    HRESULT STDMETHODCALLTYPE GetNamedItem(HSTRING name, IXmlNode** node) override {
        if (!node) return E_POINTER;
        *node = nullptr;
        UINT32 size{};
        items_->get_Size(&size);
        auto key = Text(name);
        for (UINT32 i = 0; i < size; ++i) {
            ComPtr<IXmlNode> item;
            items_->GetAt(i, &item);
            auto* attribute = Node::From(item.Get());
            if (attribute && attribute->name_ == key) return item.CopyTo(node);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetAt(UINT32 index, IXmlNode** node) override { return items_->GetAt(index, node); }
    HRESULT STDMETHODCALLTYPE get_Size(UINT32* value) override { return items_->get_Size(value); }
    HRESULT STDMETHODCALLTYPE IndexOf(IXmlNode* node, UINT32* index, boolean* found) override {
        return items_->IndexOf(node, index, found);
    }
    HRESULT STDMETHODCALLTYPE GetMany(UINT32 start, UINT32 capacity, IXmlNode** values, UINT32* actual) override {
        return items_->GetMany(start, capacity, values, actual);
    }
    HRESULT STDMETHODCALLTYPE First(NodeIteratorType** value) override { return items_->First(value); }
};
HRESULT MakeAttributes(IXmlNode* element, IXmlNamedNodeMap** value) {
    std::vector<ComPtr<IXmlNode>> nodes;
    for (const auto& attribute : Node::From(element)->attributes_) {
        auto node = Make<Attribute>(element, attribute.first);
        if (!node) return E_OUTOFMEMORY;
        nodes.emplace_back(node);
    }
    auto list = Make<NodeList>(std::move(nodes));
    if (!list) return E_OUTOFMEMORY;
    auto map = Make<NamedNodeMap>(list);
    return map ? map.CopyTo(value) : E_OUTOFMEMORY;
}
// The location paths notification code uses: "/a/b", "a/b", "//a" and "*".
// Predicates, axes and functions fail loudly instead of matching nothing.
HRESULT Select(Node* context, const std::wstring& path, bool single, std::vector<ComPtr<IXmlNode>>& found) {
    if (path.empty() || path.find_first_of(L"[]@()=:.|") != std::wstring::npos) {
        Log("UNIMPL", "XmlNodeSelector path %ls", path.c_str());
        return E_NOTIMPL;
    }
    std::vector<Node*> current{context};
    size_t at = 0;
    bool descendant = false;
    if (path[0] == L'/') {
        while (current[0]->parent_) current[0] = current[0]->parent_;
        at = 1;
        if (path.size() > 1 && path[1] == L'/') {
            descendant = true;
            at = 2;
        }
    }
    std::vector<ComPtr<IXmlNode>> matched;
    while (at <= path.size()) {
        size_t end = path.find(L'/', at);
        auto step = path.substr(at, end == std::wstring::npos ? std::wstring::npos : end - at);
        if (step.empty()) return E_INVALIDARG;
        matched.clear();
        for (auto* node : current) {
            if (descendant)
                node->Collect(step, matched);
            else
                for (const auto& child : node->children_) {
                    auto* element = Node::From(child.Get());
                    if (element && element->type_ == NodeType_ElementNode && (step == L"*" || element->name_ == step))
                        matched.push_back(child);
                }
        }
        current.clear();
        for (const auto& node : matched) current.push_back(Node::From(node.Get()));
        descendant = false;
        if (end == std::wstring::npos) break;
        at = end + 1;
        if (at < path.size() && path[at] == L'/') {
            descendant = true;
            ++at;
        }
    }
    if (single && matched.size() > 1) matched.resize(1);
    found = std::move(matched);
    return S_OK;
}
HRESULT Append(IXmlNode* parent, const wchar_t* name,
               std::initializer_list<std::pair<const wchar_t*, const wchar_t*>> attributes, ComPtr<IXmlNode>& child) {
    auto element = Make<Element>(name);
    if (!element) return E_OUTOFMEMORY;
    for (const auto& attribute : attributes) element->attributes_.emplace_back(attribute.first, attribute.second);
    ComPtr<IXmlNode> node;
    auto hr = element.As(&node);
    if (FAILED(hr)) return hr;
    ComPtr<IXmlNode> appended;
    hr = parent->AppendChild(node.Get(), &appended);
    if (FAILED(hr)) return hr;
    child = std::move(node);
    return S_OK;
}
}
namespace {
// Builds <ROOT><visual><binding template=NAME> with images then texts, numbered from 1.
HRESULT MakeTemplate(const wchar_t* root, const wchar_t* name, int images, int texts, IXmlDocument** value) {
    auto document = Make<Document>();
    if (!document) return E_OUTOFMEMORY;
    ComPtr<IXmlNode> top, element, visual, binding, child;
    auto hr = document.As(&top);
    if (SUCCEEDED(hr)) hr = Append(top.Get(), root, {}, element);
    if (SUCCEEDED(hr)) hr = Append(element.Get(), L"visual", {}, visual);
    if (SUCCEEDED(hr)) hr = Append(visual.Get(), L"binding", {{L"template", name}}, binding);
    for (int i = 1; SUCCEEDED(hr) && i <= images; ++i) {
        auto id = std::to_wstring(i);
        hr = Append(binding.Get(), L"image", {{L"id", id.c_str()}, {L"src", L""}}, child);
    }
    for (int i = 1; SUCCEEDED(hr) && i <= texts; ++i) {
        auto id = std::to_wstring(i);
        hr = Append(binding.Get(), L"text", {{L"id", id.c_str()}}, child);
    }
    return SUCCEEDED(hr) ? document.CopyTo(value) : hr;
}
}
// Windows 8 tile template catalog (TileTemplateType 0-45): image and text slots.
// The tile itself is never shown; apps fill these documents before Update.
HRESULT MakeTileTemplate(TileTemplateType type, IXmlDocument** value) {
    if (!value) return E_POINTER;
    *value = nullptr;
    struct Shape {
        const wchar_t* name;
        int images, texts;
    };
    static const Shape shapes[] = {
        {L"TileSquareImage", 1, 0}, {L"TileSquareBlock", 0, 2}, {L"TileSquareText01", 0, 4},
        {L"TileSquareText02", 0, 2}, {L"TileSquareText03", 0, 4}, {L"TileSquareText04", 0, 1},
        {L"TileSquarePeekImageAndText01", 1, 4}, {L"TileSquarePeekImageAndText02", 1, 2},
        {L"TileSquarePeekImageAndText03", 1, 4}, {L"TileSquarePeekImageAndText04", 1, 1},
        {L"TileWideImage", 1, 0}, {L"TileWideImageCollection", 5, 0}, {L"TileWideImageAndText01", 1, 1},
        {L"TileWideImageAndText02", 1, 2}, {L"TileWideBlockAndText01", 0, 6}, {L"TileWideBlockAndText02", 0, 3},
        {L"TileWidePeekImageCollection01", 5, 2}, {L"TileWidePeekImageCollection02", 5, 5},
        {L"TileWidePeekImageCollection03", 5, 1}, {L"TileWidePeekImageCollection04", 5, 1},
        {L"TileWidePeekImageCollection05", 6, 2}, {L"TileWidePeekImageCollection06", 6, 1},
        {L"TileWidePeekImageAndText01", 1, 1}, {L"TileWidePeekImageAndText02", 1, 5},
        {L"TileWidePeekImage01", 1, 2}, {L"TileWidePeekImage02", 1, 5}, {L"TileWidePeekImage03", 1, 1},
        {L"TileWidePeekImage04", 1, 1}, {L"TileWidePeekImage05", 2, 2}, {L"TileWidePeekImage06", 2, 1},
        {L"TileWideSmallImageAndText01", 1, 1}, {L"TileWideSmallImageAndText02", 1, 5},
        {L"TileWideSmallImageAndText03", 1, 1}, {L"TileWideSmallImageAndText04", 1, 2},
        {L"TileWideSmallImageAndText05", 1, 2}, {L"TileWideText01", 0, 5}, {L"TileWideText02", 0, 9},
        {L"TileWideText03", 0, 1}, {L"TileWideText04", 0, 1}, {L"TileWideText05", 0, 5},
        {L"TileWideText06", 0, 10}, {L"TileWideText07", 0, 9}, {L"TileWideText08", 0, 10},
        {L"TileWideText09", 0, 2}, {L"TileWideText10", 0, 9}, {L"TileWideText11", 0, 10},
    };
    auto index = static_cast<size_t>(type);
    if (index >= sizeof(shapes) / sizeof(shapes[0])) {
        Log("UNIMPL", "tile template %d", static_cast<int>(type));
        return E_INVALIDARG;
    }
    return MakeTemplate(L"tile", shapes[index].name, shapes[index].images, shapes[index].texts, value);
}
HRESULT MakeToastTemplate(ToastTemplateType type, IXmlDocument** value) {
    using namespace ABI::Windows::UI::Notifications;
    if (!value) return E_POINTER;
    *value = nullptr;
    // Legacy Windows 8.1 toast template shapes: optional image plus 1-3 text lines.
    const wchar_t* name;
    bool image = false;
    int texts;
    switch (type) {
    case ToastTemplateType_ToastImageAndText01:
        name = L"ToastImageAndText01";
        image = true;
        texts = 1;
        break;
    case ToastTemplateType_ToastImageAndText02:
        name = L"ToastImageAndText02";
        image = true;
        texts = 2;
        break;
    case ToastTemplateType_ToastImageAndText03:
        name = L"ToastImageAndText03";
        image = true;
        texts = 2;
        break;
    case ToastTemplateType_ToastImageAndText04:
        name = L"ToastImageAndText04";
        image = true;
        texts = 3;
        break;
    case ToastTemplateType_ToastText01:
        name = L"ToastText01";
        texts = 1;
        break;
    case ToastTemplateType_ToastText02:
        name = L"ToastText02";
        texts = 2;
        break;
    case ToastTemplateType_ToastText03:
        name = L"ToastText03";
        texts = 2;
        break;
    case ToastTemplateType_ToastText04:
        name = L"ToastText04";
        texts = 3;
        break;
    default:
        return E_INVALIDARG;
    }
    auto document = Make<Document>();
    if (!document) return E_OUTOFMEMORY;
    ComPtr<IXmlNode> root, toast, visual, binding, child;
    auto hr = document.As(&root);
    if (FAILED(hr)) return hr;
    hr = Append(root.Get(), L"toast", {}, toast);
    if (FAILED(hr)) return hr;
    hr = Append(toast.Get(), L"visual", {}, visual);
    if (FAILED(hr)) return hr;
    hr = Append(visual.Get(), L"binding", {{L"template", name}}, binding);
    if (FAILED(hr)) return hr;
    if (image) {
        hr = Append(binding.Get(), L"image", {{L"id", L"1"}, {L"src", L""}}, child);
        if (FAILED(hr)) return hr;
    }
    static const wchar_t* ids[] = {L"1", L"2", L"3"};
    for (int i = 0; i < texts; ++i) {
        hr = Append(binding.Get(), L"text", {{L"id", ids[i]}}, child);
        if (FAILED(hr)) return hr;
    }
    return document.CopyTo(value);
}
}
