#!/usr/bin/env bash
#
# Installs an AQMS TRP Backend database.
#
#   ./create.sh aqmsdb_test
#   AQMSDB_RW_PASSWORD=... AQMSDB_RO_PASSWORD=... ./create.sh aqmsdb_prod
#
# There is one database per system.  Run this once per system, with a
# different name each time; nothing is shared between them except the
# cluster roles.  Note that this means users, keys, and permissions are
# per-system too: adding a user to test does not add them to prod.
#
# Everything site-specific lives in the CONFIGURATION block below.  The
# .sql files take psql variables and contain no names, passwords, or
# intervals.
#
set -euo pipefail

###########################################################################
###                          CONFIGURATION                              ###
###########################################################################

# The database name is the one thing that changes between systems, so it
# is an argument rather than an edit.  DB_NAME in the environment works
# too; the argument wins.
DB_NAME="${1:-${DB_NAME:-}}"
DB_TABLESPACE="${DB_TABLESPACE:-pg_default}"

# Connect as this superuser to create the database/roles.
ADMIN_DB="postgres"          # maintenance database to connect to first
PSQL_CONN=()                 # e.g. (--host=dbhost --port=5432 --username=postgres)

# Set to 0 once you are in production so a stray run cannot nuke the database.
DROP_EXISTING="${DROP_EXISTING:-1}"

# Roles are cluster-wide and are NOT dropped by default, because the other
# system's database on this server shares them.  Set to 1 only for a clean
# rebuild of every database that uses them; PostgreSQL will refuse the drop
# if anything else still holds grants, which is the behaviour you want.
DROP_ROLES="${DROP_ROLES:-0}"

RW_ROLE="${RW_ROLE:-aqmsdb_read_write}"
RO_ROLE="${RO_ROLE:-aqmsdb_read_only}"
RW_USER="${RW_USER:-aqmsdb_writer}"
RO_USER="${RO_USER:-aqmsdb_reader}"

# Passwords.  Two sources, checked in this order:
#
#   1. RW_PASSWORD / RO_PASSWORD below, for when you want the value in the
#      file or assembled by something that edits this file.
#   2. The environment variable NAMED in RW_PASSWORD_ENV / RO_PASSWORD_ENV.
#
# For (2) the variable has to be EXPORTED into this script's environment.
# A variable merely assigned in your shell is not, and the script cannot
# tell that apart from the variable not existing -- so it says which case
# it hit and stops.  These work:
#
#     AQMSDB_RW_PASSWORD=... ./create.sh aqmsdb_test
#     export AQMSDB_RW_PASSWORD=...; ./create.sh aqmsdb_test
#
# This does NOT, and is the usual way to end up with placeholder passwords:
#
#     AQMSDB_RW_PASSWORD=...
#     ./create.sh aqmsdb_test
#
RW_PASSWORD=""
RO_PASSWORD=""
RW_PASSWORD_ENV="AQMSDB_RW_PASSWORD"
RO_PASSWORD_ENV="AQMSDB_RO_PASSWORD"

# Placeholder passwords abort the install.  Set to 1 for a throwaway sandbox
# where you genuinely do not care.
ALLOW_PLACEHOLDER_PASSWORDS="${ALLOW_PLACEHOLDER_PASSWORDS:-0}"

###########################################################################
###                    END CONFIGURATION                                ###
###########################################################################

SQL_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ -z ${DB_NAME} ]]; then
  echo "usage: $0 <db_name>        # e.g. $0 aqmsdb_test" >&2
  exit 1
fi

# Secrets for the current run_sql call.  Set once, below, and read on
# stdin by every invocation.
SECRET_RW_PASSWORD=""
SECRET_RO_PASSWORD=""

# Escape a value for psql's \set '...' string syntax.
psql_escape() {
  local s=$1
  s=${s//\\/\\\\}
  s=${s//\'/\\\'}
  printf '%s' "$s"
}

# run_sql <file> <dbname> [extra --set args...]
#
# Secrets are injected via \set on stdin.  Non-secret values go through
# --set, which keeps the invocation readable; argv is world-visible in ps,
# stdin is not.
run_sql() {
  local file="$1"; shift
  local dbname="$1"; shift

  echo "==> ${file}  (db=${dbname})${*:+  $*}"
  psql --no-psqlrc --quiet \
       --set=ON_ERROR_STOP=1 \
       --dbname="${dbname}" \
       "${PSQL_CONN[@]}" \
       --set=db_name="${DB_NAME}" \
       --set=db_tablespace="${DB_TABLESPACE}" \
       --set=drop_existing="${DROP_EXISTING}" \
       --set=drop_roles="${DROP_ROLES}" \
       --set=rw_role="${RW_ROLE}" \
       --set=ro_role="${RO_ROLE}" \
       --set=rw_user="${RW_USER}" \
       --set=ro_user="${RO_USER}" \
       "$@" <<EOF
\set rw_password '$(psql_escape "${SECRET_RW_PASSWORD}")'
\set ro_password '$(psql_escape "${SECRET_RO_PASSWORD}")'
\i ${SQL_DIR}/${file}
EOF
}

# resolve_password <RW|RO> <direct value> <env var name> <user>
#
# Prints the password, or CHANGE plus a diagnosis on stderr.  ${!name+x} is
# what separates "not exported" from "exported but empty"; ${!name:-CHANGE}
# collapses both into the placeholder and tells you nothing.
resolve_password() {
  local label=$1 direct=$2 env_name=$3 user=$4

  if [[ -n ${direct} ]]; then
    printf '%s' "${direct}"
    return
  fi

  if [[ -z ${env_name} ]]; then
    echo "WARNING: no ${label} password source configured." >&2
    printf 'CHANGE'
    return
  fi

  if [[ -z ${!env_name+x} ]]; then
    echo "WARNING: \$${env_name} is not set in this script's" >&2
    echo "         environment.  If you assigned it in your shell, it needs to be" >&2
    echo "         exported: 'export ${env_name}=...' or '${env_name}=... $0'." >&2
    echo "         Alternatively set ${label}_PASSWORD in create.sh." >&2
    echo "         ${user} would get a placeholder password." >&2
    printf 'CHANGE'
    return
  fi

  if [[ -z ${!env_name} ]]; then
    echo "WARNING: \$${env_name} is exported but empty." >&2
    echo "         ${user} would get a placeholder password." >&2
    printf 'CHANGE'
    return
  fi

  printf '%s' "${!env_name}"
}

###########################################################################
###                           VALIDATION                                ###
###########################################################################

SECRET_RW_PASSWORD=$(resolve_password RW "${RW_PASSWORD}" \
                       "${RW_PASSWORD_ENV}" "${RW_USER}")
SECRET_RO_PASSWORD=$(resolve_password RO "${RO_PASSWORD}" \
                       "${RO_PASSWORD_ENV}" "${RO_USER}")

if [[ ${SECRET_RW_PASSWORD} == "CHANGE" || ${SECRET_RO_PASSWORD} == "CHANGE" ]]; then
  if [[ ${ALLOW_PLACEHOLDER_PASSWORDS} -ne 1 ]]; then
    echo >&2
    echo "ABORTING: at least one role would get the placeholder password." >&2
    echo "          Fix the warnings above, or set" >&2
    echo "          ALLOW_PLACEHOLDER_PASSWORDS=1 for a throwaway sandbox." >&2
    exit 1
  fi
  echo "WARNING: continuing with placeholder passwords." >&2
fi

###########################################################################
###                            INSTALL                                  ###
###########################################################################

# 1. Roles and database.  Runs against the maintenance database and
#    \connect's into the new one; CREATE DATABASE cannot run inside a
#    transaction, so this has to be its own invocation.
run_sql createDatabase.sql "${ADMIN_DB}"

# 2. Tables and functions.
run_sql createTables.sql "${DB_NAME}"

# 3. Privileges.  Last, because it names objects created in step 2.
run_sql grantPrivileges.sql "${DB_NAME}"

echo
echo "Installed ${DB_NAME}"
echo "  writer=${RW_USER} (${RW_PASSWORD_ENV})  reader=${RO_USER}"
