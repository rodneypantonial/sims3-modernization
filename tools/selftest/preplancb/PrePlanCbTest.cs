// Offline self-check of preplancb (mods/ts3fixes/src/PrePlanCbCore.cs, docs/185 §5). FakeRoute.Orig has the order of EA's
// ScriptCore.Route.Plan (dest check, AboutToPlanCallback, search, PostPlanCallback); FakeRoute.Hook has the order of
// RouteBackoff.cs PlanAsync / AsyncKindStart with PreCb (pre-call, START, wait, snapshot, original, restore).
using System;
using System.Collections.Generic;
using Ts3Fixes;

static class PrePlanCbTest
{
    static int fails;
    static void Check(bool ok, string what) { if (!ok) { fails++; Console.WriteLine("FAIL " + what); } }

    class FakeRoute
    {
        public List<string> Log = new List<string>();
        public List<string> LockedDoors = new List<string>(); // SimRoutingComponent.LockedDoorsDuringPlan
        public Action<FakeRoute, string> AboutToPlan;         // Route.AboutToPlanCallback (multicast)
        public bool DestInvalid, StartOk = true, Collected = true;
        public string JobDoor = "frontdoor";                  // door the search finds locked (event 59 answer), null = none
        public string SyncDoor = "frontdoor";
        public int PreCalls, Restores;

        void Search(bool async)
        {
            if (async && Collected) { Log.Add("collect"); return; } // IcCollect: stored job result, no new door answers
            Log.Add("sync");                                       // sync IC (START refused, or job uncollected: SyncIc)
            if (SyncDoor != null) LockedDoors.Add(SyncDoor);
        }

        public void Orig(int kind, bool async)
        {
            if (DestInvalid) { Log.Add("destinvalid"); return; }
            if (AboutToPlan != null) AboutToPlan(this, PrePlanCbCore.Name(kind));
            Search(async);
            Log.Add("post");
        }

        bool PreCb(bool on, int kind, ref bool pre)
        {
            if (pre || !on) return true;
            pre = true;
            if (AboutToPlan == null || DestInvalid) return true;
            AboutToPlan(this, PrePlanCbCore.Name(kind));
            PreCalls++;
            return true;
        }

        public void Hook(int kind, bool on, bool eligible)
        {
            bool pre = false;
            bool started = eligible && PreCb(on, kind, ref pre) && Start();
            if (!started) { Orig(kind, false); return; }
            Log.Add("wait");
            if (JobDoor != null) LockedDoors.Add(JobDoor); // the job's door answers on the sim thread during AsyncWait
            List<string> doors = pre ? PrePlanCbCore.Snapshot(LockedDoors) : null;
            Orig(kind, true);
            if (doors != null && PrePlanCbCore.Restore(LockedDoors, doors)) { Restores++; Log.Add("restore"); }
        }

        bool Start() { Log.Add(StartOk ? "START" : "START-refused"); return StartOk; }
    }

    static FakeRoute NewRoute()
    {
        FakeRoute r = new FakeRoute();
        // EA SimRoutingComponent.AboutToPlanRouteCallback: clears the door list, sets options (idempotent)
        r.AboutToPlan += delegate(FakeRoute x, string name) { x.LockedDoors.Clear(); x.Log.Add("cb:" + name); };
        // a mod added after EA (GoHere DoorPortalComponentEx): runs on both calls too
        r.AboutToPlan += delegate(FakeRoute x, string name) { x.Log.Add("mod"); };
        return r;
    }

    static string Join(List<string> l) { return string.Join(",", l.ToArray()); }

    static void Main()
    {
        Check(PrePlanCbCore.Name(0) == "Plan" && PrePlanCbCore.Name(2) == "Plan", "Plan / PlanFromPoint pass Plan");
        Check(PrePlanCbCore.Name(1) == "Replan" && PrePlanCbCore.Name(3) == "ReplanFromPoint", "replan names");

        // switch off: today's order, the callback after the job empties the job's doors (docs/185 §2, I-319-4)
        FakeRoute r = NewRoute();
        r.Hook(0, false, true);
        Check(Join(r.Log) == "START,wait,cb:Plan,mod,collect,post", "off order: " + Join(r.Log));
        Check(r.LockedDoors.Count == 0 && r.PreCalls == 0 && r.Restores == 0, "off: doors lost");

        // switch on, START taken: pre-call before START, second call kept, one restore
        r = NewRoute();
        r.Hook(0, true, true);
        Check(Join(r.Log) == "cb:Plan,mod,START,wait,cb:Plan,mod,collect,post,restore", "on order: " + Join(r.Log));
        Check(r.Log.IndexOf("cb:Plan") < r.Log.IndexOf("START"), "pre-call before START");
        Check(r.PreCalls == 1 && r.Restores == 1, "one pre-call, one restore");
        Check(r.LockedDoors.Count == 1 && r.LockedDoors[0] == "frontdoor", "job door kept");

        // replan kinds: the name the original passes
        r = NewRoute();
        r.Hook(3, true, true);
        Check(r.Log[0] == "cb:ReplanFromPoint" && r.Restores == 1, "ReplanFromPoint pre-call: " + Join(r.Log));

        // START refused after the pre-call: sync plan, callback twice (vanilla end state), sync doors, no restore
        r = NewRoute();
        r.StartOk = false;
        r.Hook(0, true, true);
        Check(Join(r.Log) == "cb:Plan,mod,START-refused,cb:Plan,mod,sync,post", "refused order: " + Join(r.Log));
        Check(r.LockedDoors.Count == 1 && r.Restores == 0, "refused: sync doors, no restore");

        // not eligible for a START: no pre-call at all (vanilla)
        r = NewRoute();
        r.Hook(0, true, false);
        Check(Join(r.Log) == "cb:Plan,mod,sync,post" && r.PreCalls == 0, "ineligible: " + Join(r.Log));

        // job uncollected (re-run sync by IcCollect): the sync search refilled the list, nothing restored
        r = NewRoute();
        r.Collected = false;
        r.SyncDoor = "backdoor";
        r.Hook(0, true, true);
        Check(r.Restores == 0 && Join(r.LockedDoors) == "backdoor", "uncollected: sync doors win");

        // job found no locked door: nothing to restore
        r = NewRoute();
        r.JobDoor = null;
        r.Hook(0, true, true);
        Check(r.Restores == 0 && r.LockedDoors.Count == 0, "no door, no restore");

        // destination object gone: the original returns before its callback, so does the pre-call
        r = NewRoute();
        r.DestInvalid = true;
        r.Hook(0, true, true);
        Check(r.PreCalls == 0 && Join(r.Log) == "START,wait,destinvalid", "dest invalid: " + Join(r.Log));

        // no subscriber: START still sent, nothing counted
        r = NewRoute();
        r.AboutToPlan = null;
        r.Hook(0, true, true);
        Check(r.PreCalls == 0 && r.Log[0] == "START", "no callback");

        // core edge cases
        Check(PrePlanCbCore.Snapshot<string>(null) == null && PrePlanCbCore.Snapshot(new List<string>()) == null, "snapshot empty");
        Check(!PrePlanCbCore.Restore(null, new List<string> { "a" }), "restore null live");
        Check(!PrePlanCbCore.Restore(new List<string>(), null), "restore null copy");
        Check(!PrePlanCbCore.Restore(new List<string> { "b" }, new List<string> { "a" }), "restore non-empty live");
        List<string> src = new List<string> { "a" };
        List<string> copy = PrePlanCbCore.Snapshot(src);
        src.Clear();
        Check(copy.Count == 1, "snapshot is a copy");

        Console.WriteLine(fails == 0 ? "preplancb selftest ok" : "preplancb selftest: " + fails + " failure(s)");
        Environment.Exit(fails == 0 ? 0 : 1);
    }
}
