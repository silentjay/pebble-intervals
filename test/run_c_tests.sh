#!/usr/bin/env bash
set -e
cd "$(dirname "$0")/.."
cc -std=c11 -Wall -Wextra -fsanitize=undefined -I src/c \
   test/store_test.c src/c/store.c -o /tmp/store_test
/tmp/store_test
cc -std=c11 -Wall -Wextra -fsanitize=undefined -I src/c \
   test/session_test.c src/c/session.c -o /tmp/session_test
/tmp/session_test
