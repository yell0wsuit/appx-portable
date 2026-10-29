using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

using Windows.Storage;
class StorageProjectionTest
{
    [DllImport("combase.dll")] static extern int RoInitialize(uint apartment);
    static readonly Guid marker = new Guid("2e7cd45b-af7e-4f02-94c1-c5c9b8818c26");
    static void Check(bool value, string message) { if (!value) throw new Exception(message); }
    [MethodImpl(MethodImplOptions.NoInlining)]
    static void Run(bool write)
    {
        var data = ApplicationData.Current;
        Check(ReferenceEquals(data, ApplicationData.Current), "ApplicationData identity");
        var local = data.LocalSettings;
        Check(local.Name == "" && local.Locality == ApplicationDataLocality.Local, "local container metadata");
        IDictionary<string, object> values = local.Values;
        Check(ReferenceEquals(values, local.Values), "values identity");
        if (write)
        {
            values.Clear();
            values.Add("text-đá\0key", "💎\0tail");
            values["number"] = int.MinValue;
            values["wide"] = long.MinValue;
            values["flag"] = true;
            values["real"] = -123.125;
            values["id"] = marker;
            values["replace"] = "old";
            try { values.Add("replace", "wrong"); throw new Exception("duplicate key accepted"); }
            catch (ArgumentException) { }
            Check((string)values["replace"] == "old", "duplicate Add preserves original value");
            values["replace"] = "new";
            Check(!values.Remove("missing") && values.Remove("replace"), "Remove result");
            try { values["bad"] = new object(); throw new Exception("invalid settings value accepted"); }
            catch (ArgumentException) { }
            Check(!values.ContainsKey("bad"), "invalid setting is not inserted");
            data.RoamingSettings.Values["separate"] = "roaming";
            var child = local.CreateContainer("child-đá", ApplicationDataCreateDisposition.Always);
            child.Values["nested"] = 123;
            Check(ReferenceEquals(child, local.CreateContainer("child-đá", ApplicationDataCreateDisposition.Existing)), "child identity");
        }
        Check(values.Count == 6, "persisted Count");
        Check((string)values["text-đá\0key"] == "💎\0tail", "Unicode settings key/value");
        Check((int)values["number"] == int.MinValue && (long)values["wide"] == long.MinValue, "signed scalar widths");
        Check((bool)values["flag"] && (double)values["real"] == -123.125 && (Guid)values["id"] == marker, "boxed setting types");
        object absent;
        Check(!values.TryGetValue("absent", out absent) && absent == null, "TryGetValue missing");
        try { object unused = values["absent"]; throw new Exception("missing lookup accepted"); }
        catch (KeyNotFoundException) { }
        try { values.ContainsKey(null); throw new Exception("null key accepted"); }
        catch (ArgumentNullException) { }
        Check(values.Keys.IsReadOnly && values.Values.IsReadOnly && values.Keys.Contains("wide") && values.Values.Contains(true), "key/value views");
        int count = 0;
        foreach (var pair in values) { Check(values.Contains(pair), "enumerated pair"); ++count; }
        Check(count == values.Count, "native enumeration Count");
        var copy = new KeyValuePair<string, object>[values.Count + 1];
        values.CopyTo(copy, 1);
        Check(copy[1].Key != null, "CopyTo");
        Check((string)data.RoamingSettings.Values["separate"] == "roaming" && !values.ContainsKey("separate"), "local/roaming isolation");
        var restored = local.CreateContainer("child-đá", ApplicationDataCreateDisposition.Existing);
        Check(restored.Name == "child-đá" && (int)restored.Values["nested"] == 123, "persisted child container");
        Console.WriteLine("PASS: managed settings " + (write ? "write" : "read") + " through the native persistent map");
    }
    [MTAThread]
    static int Main(string[] args)
    {
        try { Marshal.ThrowExceptionForHR(RoInitialize(1)); Run(args[0] == "write"); return 0; }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }
}
