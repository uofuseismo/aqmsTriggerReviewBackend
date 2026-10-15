--- Purpose: Grants privileges.  Run after all objects exist.
---
--- Philosophy:
---   * The users table is reachable ONLY through
---     the auth functions.  Backend roles get EXECUTE, never direct
---     table access, so even the read_write backend cannot SELECT
---     password hashes.
---   * Those functions are therefore SECURITY DEFINER (they run with
---     the owner's privileges, not the caller's), with search_path
---     pinned, which is the standard hardening for definer functions.
---   * PostgreSQL grants EXECUTE on new functions to PUBLIC by default.
---     Revoke wholesale, then re-grant deliberately.
---
--- Everything below is in 'public', so the auth functions and the event
--- objects are neighbours.  The separation between them is now carried
--- entirely by which grants are issued, not by schema membership --
--- which is why the SECURITY DEFINER list below is written out by name
--- rather than swept up with a catalog query.
---
--- Copyright: Ben Baker (UUSS) distributed under the MIT license.

--- Let the backend roles in the front door.
GRANT CONNECT ON DATABASE :"db_name" TO :"rw_role", :"ro_role";

--- USAGE lets a role reference objects in the schema; it does not by
--- itself allow reading any table.
GRANT USAGE ON SCHEMA public TO :"rw_role", :"ro_role";

--- Kill the EXECUTE-to-PUBLIC default on everything before re-granting.
REVOKE ALL ON ALL FUNCTIONS IN SCHEMA public FROM PUBLIC;
REVOKE ALL ON ALL TABLES IN SCHEMA public FROM PUBLIC;

--------------------------------------------------------------------------
---                        Auth: SECURITY DEFINER                       ---
--------------------------------------------------------------------------
--- Named one at a time on purpose.  The schema-wide loop this replaces
--- would now also catch the event functions and silently run them as
--- the owner.  A function added later is NOT covered until it is added
--- here -- which is the point: it should be a decision, not a default.
---
--- The owner is whoever ran the installer (a superuser), so these
--- functions execute with that authority.  Their bodies touch only the
--- auth tables and take no dynamic SQL, which is what keeps that safe.

ALTER FUNCTION add_user(TEXT, TEXT, TEXT)
    SECURITY DEFINER SET search_path = public, pg_temp;
ALTER FUNCTION remove_user(TEXT)
    SECURITY DEFINER SET search_path = public, pg_temp;
ALTER FUNCTION update_user_password(TEXT, TEXT)
    SECURITY DEFINER SET search_path = public, pg_temp;
ALTER FUNCTION record_login(TEXT)
    SECURITY DEFINER SET search_path = public, pg_temp;
ALTER FUNCTION get_password_hash(TEXT)
    SECURITY DEFINER SET search_path = public, pg_temp;
ALTER FUNCTION set_user_permission(TEXT, TEXT)
    SECURITY DEFINER SET search_path = public, pg_temp;
ALTER FUNCTION get_user_permission(TEXT)
    SECURITY DEFINER SET search_path = public, pg_temp;
ALTER FUNCTION user_has_permission(TEXT, TEXT)
    SECURITY DEFINER SET search_path = public, pg_temp;
ALTER FUNCTION require_admin(TEXT)
    SECURITY DEFINER SET search_path = public, pg_temp;
ALTER FUNCTION user_is_admin(TEXT)
    SECURITY DEFINER SET search_path = public, pg_temp;
ALTER FUNCTION count_active_admins()
    SECURITY DEFINER SET search_path = public, pg_temp;
ALTER FUNCTION forbid_last_admin_removal(TEXT)
    SECURITY DEFINER SET search_path = public, pg_temp;
ALTER FUNCTION admin_add_user(TEXT, TEXT, TEXT, TEXT)
    SECURITY DEFINER SET search_path = public, pg_temp;
ALTER FUNCTION admin_add_provisional_user(TEXT, TEXT, TEXT, INTERVAL, TEXT)
    SECURITY DEFINER SET search_path = public, pg_temp;
ALTER FUNCTION admin_set_user_permission(TEXT, TEXT, TEXT)
    SECURITY DEFINER SET search_path = public, pg_temp;
ALTER FUNCTION admin_reset_user_password(TEXT, TEXT, TEXT, INTERVAL)
    SECURITY DEFINER SET search_path = public, pg_temp;
ALTER FUNCTION admin_remove_user(TEXT, TEXT)
    SECURITY DEFINER SET search_path = public, pg_temp;
ALTER FUNCTION add_provisional_user(TEXT, TEXT, INTERVAL, TEXT)
    SECURITY DEFINER SET search_path = public, pg_temp;
ALTER FUNCTION user_must_change_password(TEXT)
    SECURITY DEFINER SET search_path = public, pg_temp;
ALTER FUNCTION delete_expired_provisional_users()
    SECURITY DEFINER SET search_path = public, pg_temp;
ALTER FUNCTION list_users()
    SECURITY DEFINER SET search_path = public, pg_temp;

--------------------------------------------------------------------------
---                          Auth: EXECUTE                              ---
--------------------------------------------------------------------------

--- User management: read_write backend only, and every one of these
--- takes the acting user as its first argument and refuses a caller who
--- is not an administrator.  The grant says which BACKEND may ask; the
--- actor argument says on whose behalf.  Both checks matter -- the
--- reader must not reach these at all, and the writer must not be able
--- to create an account just because it asked nicely.
GRANT EXECUTE ON FUNCTION
    admin_add_user(TEXT, TEXT, TEXT, TEXT),
    admin_add_provisional_user(TEXT, TEXT, TEXT, INTERVAL, TEXT),
    admin_set_user_permission(TEXT, TEXT, TEXT),
    admin_reset_user_password(TEXT, TEXT, TEXT, INTERVAL),
    admin_remove_user(TEXT, TEXT)
    TO :"rw_role";

--- Everything else the writer mutates: a user's own password and the
--- login stamp.  Neither is an administrator's business, so neither
--- takes an actor.
GRANT EXECUTE ON FUNCTION
    update_user_password(TEXT, TEXT),
    record_login(TEXT),
    delete_expired_provisional_users()
    TO :"rw_role";

--- NOT granted to either backend, deliberately: add_user, remove_user,
--- set_user_permission, add_provisional_user, require_admin,
--- forbid_last_admin_removal.  The first four are the ungated versions
--- the admin_ functions wrap -- reachable only by a superuser, which is
--- how the first administrator gets created:
---
---     sudo -u postgres psql -d aqmsdb_prod \
---       -c "SELECT add_user('bbaker', '\$argon2id\$...', 'admin');"
---
--- The last two are internal; they run as the owner from inside the
--- definer functions and need no EXECUTE of their own.

--- Read/verify functions: both backends.  Note get_password_hash is
--- here because verifying a login IS a read; recording it is not.
--- list_users is safe for the reader because it cannot return a hash.
GRANT EXECUTE ON FUNCTION
    get_password_hash(TEXT),
    get_user_permission(TEXT),
    user_has_permission(TEXT, TEXT),
    user_is_admin(TEXT),
    count_active_admins(),
    user_must_change_password(TEXT),
    list_users()
    TO :"rw_role", :"ro_role";

--- Deliberately NO table grants on users, to either role.

