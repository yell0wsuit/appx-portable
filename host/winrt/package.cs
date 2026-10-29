using System;

namespace AppxPortable.WinRT
{
    public enum ProcessorArchitecture { X86 = 0, Arm = 5, X64 = 9, Neutral = 11, Arm64 = 12 }

    public sealed class Package : IDisposable
    {
        private readonly NativeObject native;
        private Package(NativeObject native) { this.native = native; }
        public static Package Current
        {
            get
            {
                // IPackageStatics::get_Current, after IInspectable.
                using (NativeObject factory = NativeObject.Factory("Windows.ApplicationModel.Package",
                    new Guid("4e534bdf-2960-4878-97a4-9624deb72f2d")))
                    return new Package(factory.Object(6));
            }
        }
        // IPackage::get_Id, as declared by windows.applicationmodel.h.
        public PackageId Id { get { return new PackageId(native.Object(6)); } }
        public void Dispose() { native.Dispose(); }
    }

    public sealed class PackageId : IDisposable
    {
        private readonly NativeObject native;
        internal PackageId(NativeObject native) { this.native = native; }
        // IPackageId vtable, as declared by windows.applicationmodel.h.
        public string Name { get { return native.String(6); } }
        public string Publisher { get { return native.String(10); } }
        public string ResourceId { get { return native.String(9); } }
        public ProcessorArchitecture Architecture { get { return (ProcessorArchitecture)native.UInt32(8); } }
        public void Dispose() { native.Dispose(); }
    }
}
