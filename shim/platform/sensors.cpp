// Windows.Devices.Sensors.Accelerometer for Wine, which has none. Where's My
// Water asks for it while making its page and stopped there under Wine. A PC
// without one gets a null default sensor, which games handle.
#include "com/factory.h"
#include "registry.h"
#include <windows.devices.sensors.h>
#include <wrl.h>
using namespace Microsoft::WRL;
using namespace ABI::Windows::Devices::Sensors;
namespace appx {
namespace {
class AccelerometerFactory final : public Factory<IAccelerometerStatics> {
public:
    AccelerometerFactory() : Factory(L"Windows.Devices.Sensors.Accelerometer") {}
    HRESULT STDMETHODCALLTYPE GetDefault(IAccelerometer** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        return S_OK;
    }
};
HRESULT MakeAccelerometerFactory(IActivationFactory** value) {
    auto factory = Make<AccelerometerFactory>();
    return factory ? factory.CopyTo(value) : E_OUTOFMEMORY;
}
}
const ShimClass accelerometerClass{L"Windows.Devices.Sensors.Accelerometer", MakeAccelerometerFactory,
                                   ShimPolicy::FillMissing};
}
