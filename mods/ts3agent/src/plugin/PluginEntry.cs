namespace Ts3Agent
{
    // Called by the loader's PluginHost at startup and after every hot reload.
    public static class PluginEntry
    {
        public static void Register()
        {
            Handlers.RegisterAll();
        }
    }
}
