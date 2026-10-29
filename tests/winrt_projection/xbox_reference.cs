using System.Reflection;
[assembly: AssemblyVersion("255.255.255.255")]
namespace Microsoft.Xbox
{
    public sealed class User
    {
        public User() { throw new global::System.NotImplementedException("Microsoft.Xbox.User..ctor"); }
        public Foundation.UserIdentity Identity { get { throw new global::System.NotImplementedException("Microsoft.Xbox.User.Identity"); } }
    }
    public sealed class UserProfile
    {
        public string Gamertag { get { throw new global::System.NotImplementedException("Microsoft.Xbox.UserProfile.Gamertag"); } }
    }
    public static class XboxLIVEService
    {
        public static Foundation.ServiceClient ServiceClient { get { throw new global::System.NotImplementedException("Microsoft.Xbox.XboxLIVEService.ServiceClient"); } }
        public static Foundation.UserIdentity SignedInUserIdentity { get { throw new global::System.NotImplementedException("Microsoft.Xbox.XboxLIVEService.SignedInUserIdentity"); } }
    }
}
namespace Microsoft.Xbox.Foundation
{
    public sealed class UserIdentity { }
    public sealed class ServiceClient
    {
        public string Token { get { throw new global::System.NotImplementedException("Microsoft.Xbox.Foundation.ServiceClient.Token"); } }
        public UserIdentity SignedInUserIdentity { get { throw new global::System.NotImplementedException("Microsoft.Xbox.Foundation.ServiceClient.SignedInUserIdentity"); } }
    }
}
