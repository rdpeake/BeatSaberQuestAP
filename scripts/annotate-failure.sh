#!/usr/bin/env bash
# runs a command; on failure, surfaces its error lines as a GitHub annotation
title="$1"
shift
log_file="$(mktemp)"
"$@" 2>&1 | tee "$log_file"
status=${PIPESTATUS[0]}
if [ "$status" -ne 0 ]; then
    summary="$( (grep -iE "error|fatal|failed|undefined|cannot|no matching" "$log_file" | head -n 80; echo "--- last lines ---"; tail -n 40 "$log_file") | cut -c1-400 | sed -e 's/%/%25/g' | sed -e ':a;N;$!ba;s/\n/%0A/g')"
    echo "::error title=${title} failed::${summary}"
fi
exit "$status"
