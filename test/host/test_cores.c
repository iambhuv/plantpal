#include <assert.h>
#include <string.h>
#include "eventlog.h"
#include "wifi_pick.h"
#include "policy.h"
#include "json_depth.h"

static void test_eventlog(void)
{
    eventlog_t log = {0};
    for (uint32_t i = 0; i < 20; i++) eventlog_push(&log, i, EV_SETTINGS, i, "x");
    event_t out[EVENTLOG_CAP];
    assert(eventlog_recent(&log, out, 16) == 16);
    assert(out[0].n == 19);
    assert(out[15].n == 4);
    assert(eventlog_recent(&log, out, 3) == 3 && out[2].n == 17);

    eventlog_t log2 = {0};
    char long_s[50];
    memset(long_s, 'a', sizeof long_s - 1);
    long_s[49] = '\0';
    eventlog_push(&log2, 1, EV_WIFI_JOINED, 0, long_s);
    eventlog_push(&log2, 2, EV_WATER_AUTO, 5, NULL);
    assert(eventlog_recent(&log2, out, 16) == 2);
    assert(strcmp(out[0].s, "") == 0 && out[0].kind == EV_WATER_AUTO);
    assert(strlen(out[1].s) == 32 && out[1].s[32] == '\0');

    assert(strcmp(eventlog_kind_name(EV_WATER_AUTO), "water_auto") == 0);
    assert(strcmp(eventlog_kind_name(EV_SETTINGS), "settings") == 0);
}

static void test_wifi_pick(void)
{
    wifi_saved_t saved[3] = {{"A", "", false}, {"B", "", false}, {"C", "", false}};
    wifi_seen_t seen[3] = {{"B", -70}, {"C", -50}, {"X", -30}};
    assert(wifi_pick(saved, 3, seen, 3, -1) == 2);
    saved[2].bad_pass = true;
    assert(wifi_pick(saved, 3, seen, 3, -1) == 1);
    assert(wifi_pick(saved, 3, seen, 3, 1) == -1);
    assert(wifi_pick(saved, 3, seen, 0, -1) == -1);

    /* duplicate B entries: strongest (-60) beats A (-65) */
    wifi_saved_t s2[2] = {{"A", "", false}, {"B", "", false}};
    wifi_seen_t dup[3] = {{"B", -80}, {"A", -65}, {"B", -60}};
    assert(wifi_pick(s2, 2, dup, 3, -1) == 1);

    /* bad_pass only on the second auth failure in a row for the same entry */
    wifi_strikes_t k = { -1, 0 };
    assert(!wifi_auth_strike(&k, 0, true));
    assert(wifi_auth_strike(&k, 0, true));
    k = (wifi_strikes_t){ -1, 0 };
    assert(!wifi_auth_strike(&k, 0, true));
    assert(!wifi_auth_strike(&k, 0, false));   /* other reason breaks the run */
    assert(!wifi_auth_strike(&k, 0, true));
    assert(!wifi_auth_strike(&k, 1, true));    /* other entry starts over */
    assert(wifi_auth_strike(&k, 1, true));
}

static void test_policy(void)
{
    assert(policy_clamp(0, 1, 10) == 1);
    assert(policy_clamp(99, 1, 10) == 10);
    assert(policy_clamp(5, 1, 10) == 5);
}

static void test_json_depth(void)
{
#define OK(str, max) json_depth_ok(str, sizeof(str) - 1, max)
    assert(OK("{\"a\":[1,{\"b\":2}]}", 4));
    assert(!OK("{\"a\":[[[[1]]]]}", 4));
    assert(OK("{\"s\":\"[[[[[{{{{\"}", 1));     /* brackets in strings ignored */
    assert(OK("{\"s\":\"\\\"[[[[[\"}", 1));    /* escaped quote stays in string */
    assert(!OK("]][[[[[[", 4));                     /* negative depth rejected */
    assert(OK("", 4));
#undef OK
}

int main(void)
{
    test_eventlog();
    test_wifi_pick();
    test_policy();
    test_json_depth();
    return 0;
}
