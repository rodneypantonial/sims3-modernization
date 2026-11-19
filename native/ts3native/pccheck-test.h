// TestPcCheck ([PcCheck], pccheck.cpp, docs/299). Included by selftest.cpp. Host checks of the verdict with fixture numbers,
// the fix hint order and the top-3 process list.
static void TestPcCheck() {
    char v[1024];
    PcFacts f = {};
    f.logical = 16, f.physical = 8, f.affinityCpus = 16, f.maxMhz = 2300, f.busySamples = 100, f.busyThreads = 2, f.ourThreads = 6;
    assert(PcVerdict(f, v, sizeof(v)) == 0 && strstr(v, "CPU: no throttle seen") && strstr(v, "PC: no problem found"));
    // clock limited 40% of busy samples: a problem with the limit in the line
    PcFacts t = f;
    t.limitSamples = 40, t.minLimitMhz = 1200;
    int fl = PcVerdict(t, v, sizeof(v));
    assert(fl == PC_THROTTLE && strstr(v, "clock limited 40% of the time (limit 1200 MHz of 2300)"));
    t.limitSamples = 20;  // 20% is not more than 20%
    assert(PcVerdict(t, v, sizeof(v)) == 0);
    t.limitSamples = 0, t.slowSamples = 30;  // slow clock alone counts too
    assert(PcVerdict(t, v, sizeof(v)) == PC_THROTTLE);
    t.busySamples = 2, t.slowSamples = 2;  // too few busy samples: no verdict
    assert(PcVerdict(t, v, sizeof(v)) == 0 && strstr(v, "too few busy samples"));
    // the four-thread i5 with a saver plan on battery, affinity 2, efficiency mode, other programs at 30%
    PcFacts m = {};
    m.logical = 4, m.physical = 4, m.affinityCpus = 2, m.maxMhz = 3000, m.busySamples = 50, m.busyThreads = 1.5, m.otherCpuPct = 30, m.ourThreads = 5;
    m.powerSaver = m.onBattery = m.efficiency = m.lowPriority = true;
    fl = PcVerdict(m, v, sizeof(v));
    assert(fl == (PC_POWERSAVER | PC_BATTERY | PC_AFFINITY | PC_EFFICIENCY | PC_PRIORITY | PC_OTHERLOAD | PC_FEWCORES));
    assert(strstr(v, "affinity limits TS3W to 2 of 4 cores") && strstr(v, "other programs used 30% CPU") && strstr(v, "2 threads for 1.5 busy game threads"));
    assert(!strstr(v, "no problem found"));
    // few cores alone is advice, not a problem
    PcFacts four = {};
    four.logical = 4, four.affinityCpus = 4, four.busySamples = 10, four.busyThreads = 1;
    fl = PcVerdict(four, v, sizeof(v));
    assert(fl == PC_FEWCORES && !(fl & kPcProblems) && strstr(v, "PC: no problem found"));
    // hint order: battery first, throttle last; none for advice only
    assert(PcHint(PC_THROTTLE | PC_BATTERY) && strstr(PcHint(PC_THROTTLE | PC_BATTERY), "battery"));
    assert(strstr(PcHint(PC_THROTTLE), "slows down") && !PcHint(PC_OTHERLOAD | PC_FEWCORES) && !PcHint(0));
    // top 3 other processes, % of all cores
    PcAcc a = {};
    a.wallS = 10;
    PcProcAdd(a, "chrome.exe", 4000), PcProcAdd(a, "x.exe", 120), PcProcAdd(a, "CHROME.EXE", 4000), PcProcAdd(a, "av.exe", 2000), PcProcAdd(a, "y.exe", 50);
    char top[200];
    PcTop3(a, 4, top, sizeof(top));
    assert(!strcmp(top, "chrome.exe 20.0%, av.exe 5.0%, x.exe 0.3%"));
    PcAcc e = {};
    PcTop3(e, 4, top, sizeof(top));
    assert(!strcmp(top, "-"));
    PcAcc full = {};  // full table: a bigger newcomer replaces the smallest
    char nm[16];
    for (int i = 0; i < 64; i++) { sprintf_s(nm, "p%d", i); PcProcAdd(full, nm, 10 + i); }
    PcProcAdd(full, "big", 1000), PcProcAdd(full, "tiny", 1);
    assert(full.topN == 64);
    bool big = false, tiny = false, p0 = false;
    for (int i = 0; i < 64; i++) big |= !strcmp(full.top[i].name, "big"), tiny |= !strcmp(full.top[i].name, "tiny"), p0 |= !strcmp(full.top[i].name, "p0");
    assert(big && !tiny && !p0);
}
