#pragma once
#include <windows.h>
#include <string>
namespace appx {
// The IID Windows assigns a parameterized interface instance, from its type
// signature, e.g. "pinterface({9fc2b0bb-e446-44e2-aa61-9cab8f636af2};i4)"
// for IAsyncOperation<Int32>: a version 5 UUID (SHA-1) in the WinRT
// namespace. Lets the shim implement instances it has no header for.
GUID ParameterizedIid(const std::string& signature);
// The signature of IAsyncOperation<T> for a runtime class T with the given
// default interface: "pinterface({9fc2b0bb-...};rc(Name;{iid}))".
std::string AsyncOperationOfClass(const char* runtimeClass, const GUID& defaultInterface);
std::string GuidSignature(const GUID& guid);
}
