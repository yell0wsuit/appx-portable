#pragma once
#include <dxgi.h>
namespace appx {
// Blends the newest managed-XAML frame (ShimSubmitXamlFrame) over the swap
// chain's back buffer, restoring the game's pipeline state afterwards.
void ComposeXamlLayer(IDXGISwapChain* swap);
}
