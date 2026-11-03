using System;
using System.Collections;
using System.Collections.Generic;

namespace Windows.Data.Xml.Dom
{
    // The CLR projects XmlNodeList and XmlNamedNodeMap as IReadOnlyList<IXmlNode>
    // (their IVectorView/IIterable<IXmlNode>); notification code enumerates both.
    public sealed partial class XmlNodeList : IReadOnlyList<IXmlNode>
    {
        public int Count { get { return checked((int)Length); } }
        IXmlNode IReadOnlyList<IXmlNode>.this[int index]
        {
            get
            {
                if (index < 0 || index >= Count) throw new ArgumentOutOfRangeException("index");
                return Item((uint)index);
            }
        }
        public IEnumerator<IXmlNode> GetEnumerator() { return AppxPortable.WinRT.XmlNodes.Enumerate(this); }
        IEnumerator IEnumerable.GetEnumerator() { return GetEnumerator(); }
    }

    public sealed partial class XmlNamedNodeMap : IReadOnlyList<IXmlNode>
    {
        public int Count { get { return checked((int)Length); } }
        IXmlNode IReadOnlyList<IXmlNode>.this[int index]
        {
            get
            {
                if (index < 0 || index >= Count) throw new ArgumentOutOfRangeException("index");
                return Item((uint)index);
            }
        }
        public IEnumerator<IXmlNode> GetEnumerator() { return AppxPortable.WinRT.XmlNodes.Enumerate(this); }
        IEnumerator IEnumerable.GetEnumerator() { return GetEnumerator(); }
    }
}

namespace AppxPortable.WinRT
{
    internal static class XmlNodes
    {
        // A snapshot of the length, as WinRT's vector view iterator reads it once.
        internal static IEnumerator<Windows.Data.Xml.Dom.IXmlNode> Enumerate(IReadOnlyList<Windows.Data.Xml.Dom.IXmlNode> list)
        {
            int count = list.Count;
            for (int i = 0; i < count; ++i) yield return list[i];
        }
    }
}
