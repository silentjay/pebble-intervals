#!/usr/bin/env bash
set -e
cd "$(dirname "$0")/.."
cc -std=c11 -Wall -Wextra -DHOST_TEST -fsanitize=undefined -I src/c \
   test/store_test.c src/c/store.c -o /tmp/store_test
/tmp/store_test
cc -std=c11 -Wall -Wextra -DHOST_TEST -fsanitize=undefined -I src/c \
   test/session_test.c src/c/session.c -o /tmp/session_test
/tmp/session_test

# Watch apps get a 2 KB stack; keep every frame small.
for unit in store session; do
  cc -std=c11 -Os -fstack-usage -DHOST_TEST -I src/c -c src/c/$unit.c -o /tmp/${unit}_frame.o
  awk -F'\t' '$2 > 512 { printf "stack frame too big: %s (%s bytes)\n", $1, $2; bad=1 } END { exit bad }' /tmp/${unit}_frame.su
done
echo "stack budget: ok"
