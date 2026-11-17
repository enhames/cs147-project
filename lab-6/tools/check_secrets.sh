#!/usr/bin/env bash
# Pre-push check: make sure no credential is anywhere in the files Git tracks.
# Usage (from the project folder):  bash tools/check_secrets.sh
# It asks for each value without echoing it, so the password never lands in your shell history.
# Optional: install as a Git hook so it runs on every push:
#   cp tools/check_secrets.sh .git/hooks/pre-push && chmod +x .git/hooks/pre-push
set -u
found=0
for label in "Wi-Fi password" "MQTT password" "MQTT username"; do
  read -r -s -p "$label (Enter to skip): " value </dev/tty; echo
  [ -z "$value" ] && continue
  # Same as: git grep -n <password>   (-F = literal text, -I = skip binary files)
  if git grep -n -I -F -e "$value" -- . ; then
    echo "FOUND the $label in a tracked file (see above). Remove it before you push."
    found=1
  fi
done
if git grep -n -I -e 'setInsecure' -- 'src/*' 'include/*' ; then
  echo "FOUND setInsecure(): the broker certificate must be verified."
  found=1
fi
[ $found -eq 0 ] && echo "OK: no credentials found in tracked files."
exit $found
