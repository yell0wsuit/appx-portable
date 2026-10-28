#pragma once
#include "package.h"
#include <objidl.h>
namespace appx {
// Loose package files take precedence. Otherwise read an embedded PRI candidate
// through Windows' resource manager, including its normal qualifier selection.
// S_FALSE means the resource is absent; the package is never changed.
HRESULT OpenPackageResource(const PackageInfo& package, const std::wstring& relative, IStream** value);
}
