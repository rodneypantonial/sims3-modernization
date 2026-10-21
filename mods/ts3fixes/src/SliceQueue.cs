using System;
using System.Collections.Generic;

namespace Ts3Fixes
{
    // FIFO of deferred work that one script task drains a slice at a time (FewerPlans.cs, stuckcheckasync). No game types,
    // so tools/selftest/slicequeue checks it offline. Times are in the caller's clock (sim ticks in the game).
    public sealed class SliceQueue<T>
    {
        struct Item { public T Value; public long At; }
        readonly Queue<Item> mItems = new Queue<Item>();
        public int MaxDepth;  // most items waiting at once
        public long MaxWait;  // longest time an item waited before Next handed it out (a clock that went back counts 0)

        public int Count { get { return mItems.Count; } }

        public void Add(T value, long now)
        {
            Item i;
            i.Value = value;
            i.At = now;
            mItems.Enqueue(i);
            if (mItems.Count > MaxDepth) MaxDepth = mItems.Count;
        }

        public bool Next(long now, out T value)
        {
            if (mItems.Count == 0) { value = default(T); return false; }
            Item i = mItems.Dequeue();
            if (now - i.At > MaxWait) MaxWait = now - i.At;
            value = i.Value;
            return true;
        }

        // drops everything still waiting; returns how many
        public int Clear()
        {
            int n = mItems.Count;
            mItems.Clear();
            return n;
        }
    }
}
