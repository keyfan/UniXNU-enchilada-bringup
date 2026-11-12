// SPDX-License-Identifier: BSD-2-Clause-Patent
// AdrenoMetalPublishV1 host test: 7 groups, CHECK() accumulates failures.
#include <cstdint>
#include <cstdio>
#include "AdrenoMetalPublishV1.h"

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { ++failures; \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static uint32_t litlen(const char *s)
{
    uint32_t n = 0;
    while (s[n]) ++n;
    return n;
}

static void testPublishConstants()
{
    CHECK(kEnchiladaPublishPluginOpenType == 0x4147u);
    CHECK(kEnchiladaPublishTunnelMagic == 0x4e544e4cu);
    CHECK(kEnchiladaPublishHandoffBase == 0x7000u);
    CHECK(kEnchiladaPublishHandoffLast == 0x7003u);
    CHECK(kEnchiladaPublishScanoutType == 0x5343u);
    CHECK(kEnchiladaPublishHandoffOpenType == 0x4148u);
    CHECK(kEnchiladaPublishExportMethod == 0x7100u);
    CHECK(kEnchiladaPublishExportMaxPiece == 3968u);
    CHECK(kEnchiladaPublishMailboxType == 0xc0deu);
    CHECK(kEnchiladaPublishMailboxMagic == 0x4d41494cu);
    CHECK(kEnchiladaPublishMaxClients == 4u);
    CHECK(kEnchiladaPublishMaxQueues == 8u);
    CHECK(kEnchiladaPublishClientCap == 8u);
    CHECK(kEnchiladaPublishPerObjectBytesShift == 28u);
    CHECK(kEnchiladaPublishA630WindowsSpecifiedMask == 0u);
    CHECK(kEnchiladaPublishTodayBlockers == 0x3fu);
}

static void testOpenTypeRouting()
{
    CHECK(enchilada_publish_open_type_is_plugin(0x4147u));
    CHECK(!enchilada_publish_open_type_is_plugin(0x4148u));
    CHECK(enchilada_publish_open_type_is_handoff(0x4148u));
    CHECK(!enchilada_publish_open_type_is_handoff(0x4147u));
    /* P1:577 marker-only open: (type>>16)==0x4147 && low16 != 0. */
    CHECK(enchilada_publish_open_type_is_marker(0x41470001u));
    CHECK(enchilada_publish_open_type_is_marker(0x4147ffffu));
    CHECK(!enchilada_publish_open_type_is_marker(0x41470000u));
    CHECK(!enchilada_publish_open_type_is_marker(0x4148u));
    CHECK(litlen(enchilada_publish_open_type_name(0x4147u)) == 6u);   /* plugin */
    CHECK(litlen(enchilada_publish_open_type_name(0x4148u)) == 7u);   /* handoff */
    CHECK(litlen(enchilada_publish_open_type_name(0x41470001u)) == 6u); /* marker */
    CHECK(litlen(enchilada_publish_open_type_name(0x1u)) == 11u);     /* unsupported */
    /* P1:689 render server keeps limit; anyone else is capped at 8. */
    CHECK(enchilada_publish_client_cap(1, 100u) == 100u);
    CHECK(enchilada_publish_client_cap(1, 0u) == 0u);
    CHECK(enchilada_publish_client_cap(0, 100u) == 8u);
    CHECK(enchilada_publish_client_cap(0, 3u) == 3u);
    CHECK(enchilada_publish_client_cap(0, 0u) == 8u);
    /* P1:243 per-slot queue range base. */
    CHECK(enchilada_publish_queue_base(0u) == 0u);
    CHECK(enchilada_publish_queue_base(1u) == 8u);
    CHECK(enchilada_publish_queue_base(3u) == 24u);
}

static void testRenderServerAndName()
{
    const char rs[] = "backboardd";
    CHECK(enchilada_publish_is_render_server(rs, litlen(rs)));
    CHECK(!enchilada_publish_is_render_server("backboard", litlen("backboard")));
    CHECK(!enchilada_publish_is_render_server("BackBoardD", litlen("BackBoardD")));
    CHECK(!enchilada_publish_is_render_server(rs, 3u));
    CHECK(!enchilada_publish_is_render_server(nullptr, 10u));

    /* P4:39 client-name validation: alnum or one of ._-* */
    const char ok1[] = "com.apple.WebKit.*";
    const char ok2[] = "iconservicesagent";
    const char bad1[] = "has space";
    const char bad2[] = "a/b";
    CHECK(enchilada_publish_client_name_valid(ok1, litlen(ok1)));
    CHECK(enchilada_publish_client_name_valid(ok2, litlen(ok2)));
    CHECK(!enchilada_publish_client_name_valid(bad1, litlen(bad1)));
    CHECK(!enchilada_publish_client_name_valid(bad2, litlen(bad2)));
    CHECK(!enchilada_publish_client_name_valid(nullptr, 3u));
    CHECK(!enchilada_publish_client_name_valid(ok2, 0u));
}

static void testAllowList()
{
    /* "*" names every process (P2:153). */
    const char star[] = "*";
    CHECK(enchilada_publish_client_listed(star, litlen(star), "anything", 8u));
    /* exact match over commas / blanks / tabs / newlines (P2:152). */
    const char list[] = "PosterBoard, iconservicesagent\tWidgetRenderer_Default\n";
    CHECK(enchilada_publish_client_listed(list, litlen(list), "iconservicesagent", 17u));
    CHECK(enchilada_publish_client_listed(list, litlen(list), "PosterBoard", 11u));
    CHECK(!enchilada_publish_client_listed(list, litlen(list), "backboardd", 10u));
    /* trailing-star prefix (P2:154-155): "com.apple.WebKit.*". */
    const char pref[] = "com.apple.WebKit.* ,SpringBoard";
    CHECK(enchilada_publish_client_listed(pref, litlen(pref), "com.apple.WebKit.WebContent", 27u));
    CHECK(enchilada_publish_client_listed(pref, litlen(pref), "SpringBoard", 11u));
    CHECK(!enchilada_publish_client_listed(pref, litlen(pref), "com.apple.foo", 13u));
    /* edge cases: empty list, null self, zero self length. */
    const char empty[] = "   ,,\t\n";
    CHECK(!enchilada_publish_client_listed(empty, litlen(empty), "x", 1u));
    CHECK(!enchilada_publish_client_listed(list, litlen(list), nullptr, 3u));
    CHECK(!enchilada_publish_client_listed(list, litlen(list), "x", 0u));
    CHECK(!enchilada_publish_client_listed(nullptr, 0u, "x", 1u));
}

static void testWindowHonestyGate()
{
    struct enchilada_publish_window w[4];
    uint32_t mask = 0xdeadbeefu;
    enchilada_publish_windows_a630(w, &mask);
    CHECK(mask == 0u);
    for (unsigned i = 0; i < 4u; ++i) {
        CHECK(w[i].start == 0u);
        CHECK(w[i].bytes == 0u);
    }
    CHECK(enchilada_publish_windows_consistent(w, 0u));
    CHECK(enchilada_publish_windows_are_board_derived());
    /* A fabricated window, a wrong mask, or a NULL array must be refused. */
    w[2].start = 0x20000000ull;
    w[2].bytes = 0x40000000ull;
    CHECK(!enchilada_publish_windows_consistent(w, 0u));
    struct enchilada_publish_window z[4];
    enchilada_publish_windows_a630(z, nullptr);
    CHECK(!enchilada_publish_windows_consistent(z, 1u));
    CHECK(!enchilada_publish_windows_consistent(nullptr, 0u));
    /* NULL outputs are tolerated (no crash, no libc). */
    enchilada_publish_windows_a630(nullptr, nullptr);
}

static void testShengReference()
{
    uint64_t s = 0, b = 0;
    /* Provenance-only: sheng slot0 = 1 GiB at 0x20000000. */
    CHECK(enchilada_publish_sheng_reference(0u, &s, &b));
    CHECK(s == 0x20000000ull);
    CHECK(b == 0x40000000ull);
    /* Slots 1-3 = 0x0AAA9000 (~170.6 MiB), NOT the docs' "256 MiB". */
    CHECK(enchilada_publish_sheng_reference(1u, &s, &b));
    CHECK(s == 0x60000000ull);
    CHECK(b == 0x0aaa9000ull);
    CHECK(enchilada_publish_sheng_reference(2u, &s, &b));
    CHECK(s == 0x6aaa9000ull);
    CHECK(enchilada_publish_sheng_reference(3u, &s, &b));
    CHECK(s == 0x75552000ull);
    /* Out of range / NULL refused. */
    CHECK(!enchilada_publish_sheng_reference(4u, &s, &b));
    CHECK(!enchilada_publish_sheng_reference(0u, nullptr, &b));
    /* 256 MiB is the PER-OBJECT cap (shift 28), a distinct concept. */
    CHECK(enchilada_publish_per_object_cap_bytes_shift() == 28u);
    CHECK((1ull << 28) == 0x10000000ull);
    CHECK(0x0aaa9000ull != 0x10000000ull);
}

static void testEstablishmentChain()
{
    CHECK(kEnchiladaPublishStageCount == 9u);
    CHECK(litlen(enchilada_publish_stage_name(0u)) > 0u);
    CHECK(litlen(enchilada_publish_stage_name(8u)) > 0u);
    CHECK(litlen(enchilada_publish_stage_name(99u)) == 7u); /* "unknown" */
    /* The portable allow-list stage has no blockers. */
    CHECK(enchilada_publish_stage_blockers(kEnchiladaPublishStageClientAllowList) == 0u);
    /* OR over all 9 stages = 0x3f (all six blockers) today. */
    uint32_t bits = 0u;
    for (uint32_t s = 0; s < kEnchiladaPublishStageCount; ++s) {
        bits |= enchilada_publish_stage_blockers(s);
    }
    CHECK(bits == 0x3fu);
    CHECK(enchilada_publish_establishment_blockers() == 0x3fu);
    CHECK(litlen(enchilada_publish_blocker_name(1u)) > 0u);
    CHECK(litlen(enchilada_publish_blocker_name(32u)) > 0u);
    CHECK(litlen(enchilada_publish_blocker_name(64u)) == 7u); /* "unknown" */
    /* Seven prerequisites. enchilada today: compiler=1, others 0 -> refused. */
    CHECK(!enchilada_publish_frontend_allowed(0, 1, 0, 0, 0, 0, 0));
    CHECK(!enchilada_publish_frontend_allowed(1, 1, 1, 1, 1, 1, 0));
    CHECK(enchilada_publish_frontend_allowed(1, 1, 1, 1, 1, 1, 1));
}

int main()
{
    testPublishConstants();
    testOpenTypeRouting();
    testRenderServerAndName();
    testAllowList();
    testWindowHonestyGate();
    testShengReference();
    testEstablishmentChain();
    if (failures) {
        std::printf("FAIL: enchilada A630 Metal publish/discovery policy contract (%d failures)\n", failures);
        return 1;
    }
    std::printf("PASS: enchilada A630 Metal publish/discovery policy contract (7 test groups, 0 failures)\n");
    return 0;
}
