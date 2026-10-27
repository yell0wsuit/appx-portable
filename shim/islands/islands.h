#pragma once
#include <windows.h>
#include <activation.h>
#include <windows.ui.core.h>
#include <windows.applicationmodel.activation.h>
namespace appx {
HRESULT IslandDispatcher(ABI::Windows::UI::Core::ICoreDispatcher** value);
bool IslandsMode() noexcept;
bool ManagedMode() noexcept;
// Borrowed HWND, valid on the island's UI thread while the view is open.
HWND IslandWindow() noexcept;
void IslandHostMessage(UINT message, WPARAM wparam, LPARAM lparam);
void CloseIslandView();
void ResizeIslandView(int width, int height);
void RaiseIslandSuspending(ABI::Windows::ApplicationModel::ISuspendingEventArgs* args);
bool IslandPreTranslate(MSG* message);
HRESULT MakeIslandFactory(const wchar_t* name, IActivationFactory** out);
HRESULT MakeIslandPanelFactory(IActivationFactory** out);
HRESULT MakeIslandSwapChainPanelFactory(IActivationFactory** out);
HRESULT MakeLaunchArgs(ABI::Windows::ApplicationModel::Activation::IActivatedEventArgs** out);
void DismissSplashScreen();
}
