--- Purpose: Creates the cluster roles and the database.  Everything
--- lives in the default 'public' schema; there are no other schemas.
--- Run against the maintenance database (e.g., postgres) as a superuser.
---
--- Expects these psql variables (set by create.sh):
---   db_name, db_tablespace, drop_existing, drop_roles,
---   rw_role, ro_role, rw_user, ro_user, rw_password, ro_password
---
--- Note on style: role/database names are interpolated as :"name"
--- (quoted identifier) rather than :name, so a mixed-case or otherwise
--- awkward deployment name does not silently fold or break.  Variables
--- are never referenced inside dollar-quoted blocks -- psql does not
--- expand them there -- so idempotent DDL is done with \gexec instead.
---
--- Copyright: Ben Baker (UUSS) distributed under the MIT license.

--------------------------------------------------------------------------
---                             Teardown                                ---
--------------------------------------------------------------------------

--- Guard so a stray run in production cannot nuke the database.
\if :drop_existing
DROP DATABASE IF EXISTS :"db_name";
\endif

--- Roles are cluster-wide, so dropping them is opt-in and separate:
--- another database on this server may still be using them.  PostgreSQL
--- refuses the drop if anything still depends on the role, which is the
--- behaviour you want.
\if :drop_roles
DROP ROLE IF EXISTS :"rw_user";
DROP ROLE IF EXISTS :"ro_user";
DROP ROLE IF EXISTS :"rw_role";
DROP ROLE IF EXISTS :"ro_role";
\endif

--------------------------------------------------------------------------
---                               Roles                                 ---
--------------------------------------------------------------------------
--- Two group roles carry the privileges; two login roles are members of
--- them.  Privileges are then granted once, to the groups, and a new
--- login (a second writer, say) is one GRANT rather than a re-run of
--- grantPrivileges.sql.

SELECT format('CREATE ROLE %I NOLOGIN', :'rw_role')
 WHERE NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = :'rw_role')
\gexec

SELECT format('CREATE ROLE %I NOLOGIN', :'ro_role')
 WHERE NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = :'ro_role')
\gexec

SELECT format('CREATE ROLE %I LOGIN', :'rw_user')
 WHERE NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = :'rw_user')
\gexec

SELECT format('CREATE ROLE %I LOGIN', :'ro_user')
 WHERE NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = :'ro_user')
\gexec

--- Set the passwords unconditionally, so re-running the installer with a
--- new password rotates it.  %L quotes the literal; the value arrived on
--- stdin rather than argv and so was never visible in ps.
SELECT format('ALTER ROLE %I WITH LOGIN PASSWORD %L', :'rw_user', :'rw_password')
\gexec

SELECT format('ALTER ROLE %I WITH LOGIN PASSWORD %L', :'ro_user', :'ro_password')
\gexec

SELECT format('GRANT %I TO %I', :'rw_role', :'rw_user')
\gexec

SELECT format('GRANT %I TO %I', :'ro_role', :'ro_user')
\gexec

--------------------------------------------------------------------------
---                             Database                                ---
--------------------------------------------------------------------------

CREATE DATABASE :"db_name" TABLESPACE :"db_tablespace";

\connect :"db_name"

--- By default every role in the cluster can connect to every database.
--- Revoke that here; grantPrivileges.sql hands CONNECT back to the two
--- backend roles only.
REVOKE CONNECT ON DATABASE :"db_name" FROM PUBLIC;

--- Likewise, PUBLIC can create objects in 'public' on PostgreSQL 14 and
--- older.  Harmless to repeat on 15+, where it is already the default.
REVOKE CREATE ON SCHEMA public FROM PUBLIC;
