#!/bin/sh
# Host tests for pure-C cores. Run from repo root: sh test/host/run.sh
cc -std=gnu99 -Wall -Wextra -Werror -fsanitize=address,undefined -Imain/include test/host/test_cores.c main/eventlog.c main/wifi_pick.c -o /tmp/pp_test && /tmp/pp_test && echo PASS
