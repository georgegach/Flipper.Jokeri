#!/bin/sh
# Run ufbt, then copy any produced .fap into /app/dist so the host can find it.
ufbt "$@"
rc=$?
if [ -d /ufbt-cache/build ]; then
    mkdir -p /app/dist
    find /ufbt-cache/build -maxdepth 1 -name '*.fap' -exec cp -f {} /app/dist/ \; 2>/dev/null || true
fi
exit $rc
