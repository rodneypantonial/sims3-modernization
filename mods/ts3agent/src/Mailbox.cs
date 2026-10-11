using System;
using System.Collections.Generic;
using System.Text;

namespace Ts3Agent
{
    // Shared-memory mailbox polled by the external MCP server through
    // ReadProcessMemory/WriteProcessMemory. Layout must match
    // server/sims3_mcp/mailbox.py.
    public static class Mailbox
    {
        public const int Version = 1;
        public const uint CheckSalt = 0x53334D43;
        public const int Header = 64;
        public const int ReqCap = 64 * 1024;
        public const int Capacity = 1024 * 1024;

        const int OffVersion = 16, OffNonce = 20, OffReqSeq = 24, OffRespSeq = 28,
                  OffReqLen = 32, OffRespLen = 36, OffCapacity = 40, OffCheck = 44,
                  OffHeartbeat = 48, OffAllocTick = 52, OffState = 56;

        // Strong static reference keeps the buffer alive; Mono's Boehm GC
        // never moves objects, so the external process can cache its address.
        static byte[] sBuffer;

        public static bool Active { get { return sBuffer != null; } }

        public static void Open()
        {
            if (sBuffer != null) return;
            byte[] buf = new byte[Capacity];
            uint nonce = (uint)new Random().Next() ^ (uint)Environment.TickCount;
            PutU32(buf, OffVersion, Version);
            PutU32(buf, OffNonce, nonce);
            PutU32(buf, OffCapacity, Capacity);
            PutU32(buf, OffCheck, (uint)Version ^ nonce ^ (uint)Capacity ^ CheckSalt);
            PutU32(buf, OffAllocTick, (uint)Environment.TickCount);
            // Magic last, so scanners never see a half-initialized header. It is
            // assembled at runtime so the literal in this DLL never matches.
            byte[] magic = MagicBytes();
            Buffer.BlockCopy(magic, 0, buf, 0, magic.Length);
            sBuffer = buf;
        }

        public static void Close()
        {
            byte[] buf = sBuffer;
            sBuffer = null;
            if (buf != null) Array.Clear(buf, 0, Header);  // invalidate for scanners
        }

        public static void SetWorldLoaded(bool loaded)
        {
            if (sBuffer != null) PutU32(sBuffer, OffState, loaded ? 1u : 0u);
        }

        static byte[] MagicBytes()
        {
            // "TS3AGENT-MBOX-01" XOR 0x5A (own magic, so it never collides with an installed Sims3-MCP)
            byte[] enc = { 0x0E, 0x09, 0x69, 0x1B, 0x1D, 0x1F, 0x14, 0x0E, 0x77, 0x17, 0x18, 0x15, 0x02, 0x77, 0x6A, 0x6B };
            byte[] m = new byte[enc.Length];
            for (int i = 0; i < enc.Length; i++) m[i] = (byte)(enc[i] ^ 0x5A);
            return m;
        }

        // Called once per simulator tick on the game thread.
        public static void Poll()
        {
            byte[] buf = sBuffer;
            if (buf == null) return;
            PutU32(buf, OffHeartbeat, GetU32(buf, OffHeartbeat) + 1);
            uint req = GetU32(buf, OffReqSeq);
            uint resp = GetU32(buf, OffRespSeq);
            if (req == resp) return;

            string reply;
            try
            {
                int len = (int)GetU32(buf, OffReqLen);
                if (len < 0 || len > ReqCap) throw new InvalidOperationException("bad request length " + len);
                string text = Encoding.UTF8.GetString(buf, Header, len);
                reply = Dispatcher.Handle(text);
            }
            catch (Exception e)
            {
                reply = Dispatcher.ErrorReply(0, e);
            }

            byte[] data = Encoding.UTF8.GetBytes(reply);
            int max = Capacity - Header - ReqCap;
            if (data.Length > max)
            {
                data = Encoding.UTF8.GetBytes(Dispatcher.ErrorReply(0, new InvalidOperationException(
                    "response too large (" + data.Length + " bytes); narrow the query")));
            }
            Buffer.BlockCopy(data, 0, buf, Header + ReqCap, data.Length);
            PutU32(buf, OffRespLen, (uint)data.Length);
            PutU32(buf, OffRespSeq, req);  // publish last
        }

        static uint GetU32(byte[] b, int o)
        {
            return (uint)(b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24));
        }

        static void PutU32(byte[] b, int o, uint v)
        {
            b[o] = (byte)v;
            b[o + 1] = (byte)(v >> 8);
            b[o + 2] = (byte)(v >> 16);
            b[o + 3] = (byte)(v >> 24);
        }
    }
}
