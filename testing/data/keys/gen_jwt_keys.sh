#!/bin/bash
#
# Generates the ed25519 key pairs the JWT unit tests sign with.
#
#   ./gen_jwt_keys.sh                 # regenerates both pairs
#
# These are TEST keys and are committed to the repository on purpose, so
# the suite needs no setup step.  Nothing deployed may ever use them.
#
# Two pairs, not one: the suite mints a token with the second pair and
# checks the first authority rejects it, which is the property that a
# single pair cannot test.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

for suffix in "" "-2"; do
  private="ed25519-private-key${suffix}.pem"
  public="ed25519-public-key${suffix}.pem"
  openssl genpkey -algorithm ed25519 -out "${private}"
  openssl pkey -in "${private}" -pubout -out "${public}"
  chmod 644 "${private}" "${public}"
  echo "wrote ${private} and ${public}"
done
