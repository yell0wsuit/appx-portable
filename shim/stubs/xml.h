#pragma once
#include <windows.data.xml.dom.h>
#include <windows.ui.notifications.h>
namespace appx {
// Minimal Windows.Data.Xml.Dom tree (Wine has none) holding a legacy toast
// template, so offline toast code can fill and schedule it without failing.
// Windows 8 tile templates in the same tree; Wine has no TileUpdateManager.
HRESULT MakeTileTemplate(ABI::Windows::UI::Notifications::TileTemplateType type,
                         ABI::Windows::Data::Xml::Dom::IXmlDocument** value);
HRESULT MakeToastTemplate(ABI::Windows::UI::Notifications::ToastTemplateType type,
                          ABI::Windows::Data::Xml::Dom::IXmlDocument** value);
}
