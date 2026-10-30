// TestPlayed (played.cpp, docs/14 section 8.6). Included by selftest.cpp. Pure helpers only: key check, id parse, file
// text parse (comments, blanks, CRLF, duplicates, corrupt lines, overflow, cap).
static void TestPlayed() {
    assert(PlKeyOk("MultiGen-Test-BP.sims3"));
    assert(PlKeyOk("unsaved_SunsetValley"));
    assert(!PlKeyOk(""));
    assert(!PlKeyOk(".hidden"));
    assert(!PlKeyOk("a b"));
    assert(!PlKeyOk("..\\x"));
    assert(!PlKeyOk("a/b"));
    char longKey[120];
    memset(longKey, 'a', 101);
    longKey[101] = 0;
    assert(!PlKeyOk(longKey));

    u64 id = 0;
    const char* s = "  1234567890123 \r";
    assert(PlParseId(s, s + strlen(s), &id) && id == 1234567890123ULL);
    s = "18446744073709551615";
    assert(PlParseId(s, s + strlen(s), &id) && id == ~0ULL);
    s = "18446744073709551616";  // overflow
    assert(!PlParseId(s, s + strlen(s), &id));
    s = "0";
    assert(!PlParseId(s, s + strlen(s), &id));
    s = "12x";
    assert(!PlParseId(s, s + strlen(s), &id));

    u64 ids[4];
    int bad = -1;
    const char* txt = "# header\r\n# save X\r\n\r\n42\r\n 7 \n42\nnot an id\n99999999999999999999999\n8";
    int n = PlParse(txt, strlen(txt), ids, 4, &bad);
    assert(n == 3 && ids[0] == 42 && ids[1] == 7 && ids[2] == 8 && bad == 2);
    n = PlParse("", 0, ids, 4, &bad);
    assert(n == 0 && bad == 0);
    txt = "1\n2\n3\n4\n5\n";
    n = PlParse(txt, strlen(txt), ids, 4, &bad);  // cap: the rest dropped, not counted as bad
    assert(n == 4 && bad == 0);
    static const char junk[] = "\xFF\xFE\0x\n5";  // binary garbage: skipped, never a crash
    n = PlParse(junk, sizeof(junk) - 1, ids, 4, &bad);
    assert(n == 1 && ids[0] == 5 && bad == 1);
}
