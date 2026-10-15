#!/usr/bin/env bash
#
# Builds a throwaway database with create.sh and runs the test suites
# against it as the backend roles.
#
#   ./test.sh
#   ./test.sh --keep          # leave the database up for poking at
#
# The suites are run as aqmsdb_writer and aqmsdb_reader, NOT as a
# superuser, because a superuser bypasses every privilege check: the
# negative tests would pass against a database with no grants at all.
#
set -euo pipefail

###########################################################################
###                          CONFIGURATION                              ###
###########################################################################

# Deliberately not aqmsdb_test.  That is a real system whose name is one
# character away, and this script drops whatever it is pointed at.
TEST_DB="${TEST_DB:-aqmsdb_selftest}"

# Names this script refuses to touch no matter what.  Add every real
# deployment here; the cost of a wrong answer is the whole database.
PROTECTED_DBS=("aqmsdb_test" "aqmsdb_prod" "postgres" "template1")

ADMIN_DB="postgres"

# Roles are cluster-wide, so the test uses its own rather than rotating
# the password on the roles a running system is authenticating with.
TEST_RW_ROLE="aqmsdb_selftest_read_write"
TEST_RO_ROLE="aqmsdb_selftest_read_only"
TEST_RW_USER="aqmsdb_selftest_writer"
TEST_RO_USER="aqmsdb_selftest_reader"

# psql connection arguments for the SUPERUSER, matching PSQL_CONN in
# create.sh -- e.g. (--host=dbhost --port=5432 --username=postgres)
PSQL_CONN=()

# How the test roles connect.  This goes through TCP to localhost rather
# than the unix socket, because a socket connection typically lands on
# peer authentication, which maps to your OS user and cannot authenticate
# as aqmsdb_selftest_writer whatever password you supply.
TEST_CONN=(--host=localhost)

###########################################################################

SQL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KEEP=0
[[ ${1:-} == "--keep" ]] && KEEP=1

for protected in "${PROTECTED_DBS[@]}"; do
  if [[ ${TEST_DB} == "${protected}" ]]; then
    echo "ABORTING: refusing to run tests against '${TEST_DB}'." >&2
    echo "          This script drops the database it is given." >&2
    exit 1
  fi
done

# Throwaway passwords, regenerated per run and never written down.
TEST_RW_PASSWORD="selftest_rw_$RANDOM$RANDOM"
TEST_RO_PASSWORD="selftest_ro_$RANDOM$RANDOM"

cleanup() {
  local rc=$?
  if [[ ${KEEP} -eq 1 ]]; then
    echo
    echo "Left ${TEST_DB} in place (--keep)."
    echo "  psql ${TEST_CONN[*]} --dbname=${TEST_DB} --username=${TEST_RW_USER}"
    echo "  password: ${TEST_RW_PASSWORD}"
    return $rc
  fi
  echo
  echo "==> dropping ${TEST_DB}"
  psql --no-psqlrc --quiet "${PSQL_CONN[@]}" --dbname="${ADMIN_DB}" \
       -c "DROP DATABASE IF EXISTS ${TEST_DB};" >/dev/null 2>&1 || true
  # Roles are cluster-wide, so a leftover set would collide with the
  # next run's CREATE ROLE.  They own nothing once the database is gone.
  for r in "${TEST_RW_USER}" "${TEST_RO_USER}" "${TEST_RW_ROLE}" "${TEST_RO_ROLE}"; do
    psql --no-psqlrc --quiet "${PSQL_CONN[@]}" --dbname="${ADMIN_DB}" \
         -c "DROP ROLE IF EXISTS ${r};" >/dev/null 2>&1 || true
  done
  return $rc
}
trap cleanup EXIT

###########################################################################
###                             BUILD                                   ###
###########################################################################

echo "==> building ${TEST_DB}"
DROP_EXISTING=1 \
DROP_ROLES=1 \
RW_ROLE="${TEST_RW_ROLE}" \
RO_ROLE="${TEST_RO_ROLE}" \
RW_USER="${TEST_RW_USER}" \
RO_USER="${TEST_RO_USER}" \
AQMSDB_RW_PASSWORD="${TEST_RW_PASSWORD}" \
AQMSDB_RO_PASSWORD="${TEST_RO_PASSWORD}" \
  "${SQL_DIR}/create.sh" "${TEST_DB}"

###########################################################################
###                           BOOTSTRAP                                 ###
###########################################################################

# The first administrator cannot be created through the admin functions,
# because every one of them demands an existing administrator as actor.
# add_user is granted to no role precisely so that this is a superuser
# step -- which is also how a real deployment breaks the same cycle:
#
#     sudo -u postgres psql -d aqmsdb_prod \
#       -c "SELECT add_user('bbaker', '\$argon2id\$...', 'admin');"
#
# The suites then run as the writer and act on behalf of 'root'.
echo "==> bootstrapping the first administrator"
psql --no-psqlrc --quiet --set=ON_ERROR_STOP=1 \
     "${PSQL_CONN[@]}" --dbname="${TEST_DB}" \
     -c "SELECT add_user('root', 'hash-root', 'admin');" >/dev/null

###########################################################################
###                              RUN                                    ###
###########################################################################

# run_suite <file> <user> <password>
#
# ON_ERROR_STOP turns the first failed expectation into a non-zero exit,
# so a suite that prints 'passed' really did reach the end.
run_suite() {
  local file="$1" user="$2" password="$3"
  echo
  echo "==> ${file} as ${user}"
  PGPASSWORD="${password}" psql --no-psqlrc --quiet \
       --set=ON_ERROR_STOP=1 \
       "${TEST_CONN[@]}" \
       --dbname="${TEST_DB}" \
       --username="${user}" \
       --file="${SQL_DIR}/${file}"
}

# The writer suite runs first and leaves users and keys behind;
# the reader suite reads that state rather than building its own.  They
# are not independent and the order is not arbitrary.
run_suite testWriter.sql "${TEST_RW_USER}" "${TEST_RW_PASSWORD}"
run_suite testReader.sql "${TEST_RO_USER}" "${TEST_RO_PASSWORD}"

echo
echo "ALL TESTS PASSED"
