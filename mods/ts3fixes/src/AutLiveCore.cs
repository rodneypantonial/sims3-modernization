using System;

namespace Ts3Fixes
{
    // docs/124 §16-§17 go-live consume, the part with no game types (selftest: node tools/selftest/autlive/run.ts):
    //  - Heap: the game's BestScores queue as the meta phase uses it (BestScores.Add with AllowZeroScore off: scores > 0 only;
    //    PriorityQueue capacity 10: add while not full, else replace the smallest only when strictly greater) [verified,
    //    decompiled BestScores.Add / PriorityQueue.Add]. Used by the dry compare (G7) to replay the game's own C1 calls.
    //  - Order: the worker's passing pairs by worker score, highest first, ties in request order (B13 object order, then
    //    MetaAds order).
    //  - Bands: the §16.1 rule. Band 1 = the first TopN of Order (sent at B14). Band k+1 fires when (a) fewer than 10 entries
    //    and an unsent passing pair remains, or (b) w x (1 + drift + margin) >= s10 (w = best unsent worker score, s10 = the
    //    lowest live entry, drift = largest relative drop worker -> live over the pairs sent so far, a live fail = 1). After
    //    Bands - 1 extra bands every remaining pair goes (full).
    public static class AutLiveCore
    {
        public const int kCap = 10;

        public sealed class Heap
        {
            public readonly int[] Id;
            public readonly float[] S;
            public int N;
            readonly int mCap;
            public Heap(int cap) { mCap = cap; Id = new int[cap]; S = new float[cap]; }
            public void Clear() { N = 0; }
            public void Add(int id, float s)
            {
                if (!(s > 0f)) return;
                if (N < mCap) { Id[N] = id; S[N] = s; N++; return; }
                int m = MinAt();
                if (s > S[m]) { Id[m] = id; S[m] = s; }
            }
            public int MinAt() { int m = 0; for (int i = 1; i < N; i++) if (S[i] < S[m]) m = i; return m; }
            public float Min() { return N == 0 ? 0f : S[MinAt()]; }
            // id of the best entry (ties: the first in slot order); int.MinValue when empty
            public int TopId() { if (N == 0) return int.MinValue; int m = 0; for (int i = 1; i < N; i++) if (S[i] > S[m]) m = i; return Id[m]; }
            public bool Has(int id) { for (int i = 0; i < N; i++) if (Id[i] == id) return true; return false; }
            public bool SameSet(Heap o)
            {
                if (o.N != N) return false;
                for (int i = 0; i < N; i++) if (!o.Has(Id[i])) return false;
                return true;
            }
        }

        // 1 = count rule (a), 2 = margin rule (b), 0 = no band
        public static int Fires(int cnt, int cap, float w, float drift, float margin, float s10)
        {
            if (cnt < cap) return 1;
            return w * (1f + drift + margin) >= s10 ? 2 : 0;
        }

        // relative drop worker -> live; a live fail (score 0, as C2 answers a failed Test) counts 1, a rise 0
        public static float Drift(float worker, float live)
        {
            if (!(live > 0f)) return 1f;
            if (!(worker > 0f)) return 0f;
            float d = (worker - live) / worker;
            return d > 0f ? d : 0f;
        }
        // histogram bins: 0 exact, < 1%, < 5%, < 20%, more
        public static int DriftBin(float d) { return d <= 0f ? 0 : d < 0.01f ? 1 : d < 0.05f ? 2 : d < 0.2f ? 3 : 4; }

        // candidate pair indexes by score, highest first, ties by index; returns the count (insertion sort: 5-40 a search)
        public static int Order(float[] score, bool[] cand, int n, int[] order)
        {
            int k = 0;
            for (int i = 0; i < n; i++)
            {
                if (!cand[i]) continue;
                int at = k++;
                while (at > 0 && score[order[at - 1]] < score[i]) { order[at] = order[at - 1]; at--; }
                order[at] = i;
            }
            return k;
        }

        public sealed class Bands
        {
            public int TopN = 10, MaxBands = 3, OrderN, Next, Fired, LastWhy;
            public float Margin, DriftMax;
            public bool Full;
            public int[] Order = new int[64];
            public void Start(int topN, int maxBands, float margin, int orderN)
            {
                TopN = topN; MaxBands = maxBands; Margin = margin; OrderN = orderN;
                Next = Math.Min(topN, orderN); Fired = 0; LastWhy = 0; DriftMax = 0f; Full = false;
            }
            public bool InBand1(int rank) { return rank < TopN; }
            public void Saw(float worker, float live) { float d = Drift(worker, live); if (d > DriftMax) DriftMax = d; }
            // the next band's pairs (eligible and unsent, from Order[Next..]) into outIdx; 0 = no band fires
            public int Step(int cnt, int cap, float s10, bool[] eligible, bool[] sent, float[] w, int[] outIdx)
            {
                if (Full) return 0;
                int j = Next;
                while (j < OrderN && (!eligible[Order[j]] || sent[Order[j]])) j++;
                if (j >= OrderN) return 0;
                int why = Fires(cnt, cap, w[Order[j]], DriftMax, Margin, s10);
                if (why == 0) return 0;
                LastWhy = why;
                bool all = Fired >= MaxBands - 1;
                int n = 0;
                for (; j < OrderN && (all || n < TopN); j++)
                {
                    int p = Order[j];
                    if (eligible[p] && !sent[p]) outIdx[n++] = p;
                }
                Next = j; Fired++; Full = all;
                return n;
            }
        }

        // replay a C1 sequence into h: only the calls with send[i] (all when send is null)
        public static void Replay(Heap h, int n, int[] id, float[] score, bool[] send)
        {
            for (int i = 0; i < n; i++) if (send == null || send[i]) h.Add(id[i], score[i]);
        }

        // the bands on a simulated queue (dry compare): live[p] is the game's own score of pair p when liveHas[p]; a pair the
        // game never scored is not offered (its object was not visited). Returns the bands fired.
        public static int RunBands(Heap h, Bands b, bool[] eligible, bool[] sent, float[] w, float[] live, bool[] liveHas, int[] buf)
        {
            while (true)
            {
                int n = b.Step(h.N, kCap, h.Min(), eligible, sent, w, buf);
                if (n == 0) break;
                for (int i = 0; i < n; i++)
                {
                    int p = buf[i];
                    sent[p] = true;
                    if (!liveHas[p]) continue;
                    h.Add(p, live[p]);
                    b.Saw(w[p], live[p]);
                }
                if (b.Full) break;
            }
            return b.Fired;
        }
    }
}
