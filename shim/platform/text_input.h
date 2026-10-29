#pragma once
#include <windows.h>
namespace appx {
// A WM_CHAR on the game window, as text for the focused CoreTextEditContext.
// Returns whether that context took it, in which case CoreWindow does not get it.
bool TextInputCharacter(WPARAM character) noexcept;
// A WM_KEYDOWN on the game window: Backspace deletes in the focused context, as
// Windows' text service does. Returns whether CoreWindow should not get the key.
bool TextInputKey(WPARAM key) noexcept;
}
