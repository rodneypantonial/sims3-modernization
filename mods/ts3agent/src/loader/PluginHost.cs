using System;
using System.Collections.Generic;
using System.Reflection;

namespace Ts3Agent
{
    // Hot reload: the commands live in a separate plugin assembly (src/plugin). The package ships one copy, and
    // tools/harness/reload-agent.ts can push a newer build through the mailbox at any time; its PluginEntry.Register
    // then replaces the registered commands. Mono 1.2 cannot unload assemblies, so each reload keeps the old copy
    // in memory (~100 KB); fine for development, restart the game now and then.
    public static class PluginHost
    {
        const string EntryType = "Ts3Agent.PluginEntry";
        static Assembly sActive;
        static string[] sChunks;
        static int sReloads;

        public static void Init()
        {
            Dispatcher.Register("plugin_status", Status);
            Dispatcher.Register("plugin_begin", Begin);
            Dispatcher.Register("plugin_chunk", Chunk);
            Dispatcher.Register("plugin_load", Load);
            // At startup, use the plugin the game loaded from our package.
            foreach (Assembly asm in AppDomain.CurrentDomain.GetAssemblies())
                if (asm.GetType(EntryType) != null) { Activate(asm); break; }
        }

        static void Activate(Assembly asm)
        {
            Type entry = asm.GetType(EntryType);
            if (entry == null) throw new McpException("assembly " + asm.GetName().Name + " has no " + EntryType);
            entry.GetMethod("Register", BindingFlags.Public | BindingFlags.Static).Invoke(null, null);
            sActive = asm;
        }

        static object Status(Args a)
        {
            Dictionary<string, object> r = new Dictionary<string, object>();
            r["plugin"] = sActive != null ? sActive.GetName().Name : null;
            r["reloads"] = sReloads;
            return r;
        }

        static object Begin(Args a)
        {
            sChunks = new string[(int)a.Double("chunks", 0)];
            Dictionary<string, object> r = new Dictionary<string, object>();
            r["expecting"] = sChunks.Length;
            return r;
        }

        static object Chunk(Args a)
        {
            if (sChunks == null) throw new McpException("call plugin_begin first");
            int i = (int)a.Double("index", -1);
            if (i < 0 || i >= sChunks.Length) throw new McpException("chunk index out of range");
            sChunks[i] = a.Str("data");
            Dictionary<string, object> r = new Dictionary<string, object>();
            r["received"] = i;
            return r;
        }

        static object Load(Args a)
        {
            if (sChunks == null) throw new McpException("nothing uploaded");
            foreach (string c in sChunks) if (c == null) throw new McpException("missing chunks");
            byte[] raw = Convert.FromBase64String(string.Concat(sChunks));
            sChunks = null;
            Assembly asm = Assembly.Load(raw);
            Activate(asm);
            sReloads++;
            return Status(a);
        }
    }
}
