--- Purpose: Exercises the database as the read_only backend role.
---
--- Almost every test here is a negative one.  The writer suite proves
--- the functions work; this suite proves the reader cannot reach the
--- ones it should not, which is the half that fails silently -- a
--- missing REVOKE does not break anything, it just quietly grants
--- everyone EXECUTE and nobody notices until it matters.
---
--- Run as the READER.  Run with ON_ERROR_STOP=1.
---
--- Copyright: Ben Baker (UUSS) distributed under the MIT license.

\echo '=== reader tests ==='

--------------------------------------------------------------------------
---                       Direct table access                           ---
--------------------------------------------------------------------------

DO $$
BEGIN
    PERFORM 1 FROM users LIMIT 1;
    RAISE EXCEPTION 'FAIL: reader could SELECT from users';
EXCEPTION WHEN insufficient_privilege THEN
    RAISE NOTICE 'ok: reader denied direct SELECT on users';
END $$;

--------------------------------------------------------------------------
---                     Functions the reader may call                   ---
--------------------------------------------------------------------------
--- Verifying a login is a read, so the reader gets the hash to check
--- against.  Recording that it happened is a write, and is denied below.

--------------------------------------------------------------------------
---                   Functions the reader may NOT call                 ---
--------------------------------------------------------------------------

--- Both the ungated function and its admin_ wrapper, because they fail
--- for different reasons and only one of them is a grant.  The reader
--- holding EXECUTE on an admin_ function would be a real hole even
--- though every call would still need an administrator as actor.
DO $$
BEGIN
    PERFORM add_user('intruder', 'hash-intruder');
    RAISE EXCEPTION 'FAIL: reader could add a user';
EXCEPTION WHEN insufficient_privilege THEN
    RAISE NOTICE 'ok: reader denied add_user';
END $$;

DO $$
BEGIN
    PERFORM admin_add_user('root', 'intruder', 'hash-intruder');
    RAISE EXCEPTION 'FAIL: reader could call admin_add_user';
EXCEPTION WHEN insufficient_privilege THEN
    RAISE NOTICE 'ok: reader denied admin_add_user';
END $$;

DO $$
BEGIN
    PERFORM remove_user('alice');
    RAISE EXCEPTION 'FAIL: reader could remove a user';
EXCEPTION WHEN insufficient_privilege THEN
    RAISE NOTICE 'ok: reader denied remove_user';
END $$;

DO $$
BEGIN
    PERFORM admin_remove_user('root', 'alice');
    RAISE EXCEPTION 'FAIL: reader could call admin_remove_user';
EXCEPTION WHEN insufficient_privilege THEN
    RAISE NOTICE 'ok: reader denied admin_remove_user';
END $$;

DO $$
BEGIN
    PERFORM admin_set_user_permission('root', 'alice', 'admin');
    RAISE EXCEPTION 'FAIL: reader could call admin_set_user_permission';
EXCEPTION WHEN insufficient_privilege THEN
    RAISE NOTICE 'ok: reader denied admin_set_user_permission';
END $$;

DO $$
BEGIN
    PERFORM admin_reset_user_password('root', 'alice', 'hash-x',
                                      INTERVAL '1 day');
    RAISE EXCEPTION 'FAIL: reader could call admin_reset_user_password';
EXCEPTION WHEN insufficient_privilege THEN
    RAISE NOTICE 'ok: reader denied admin_reset_user_password';
END $$;

DO $$
BEGIN
    PERFORM admin_add_provisional_user('root', 'intruder', 'hash',
                                       INTERVAL '1 hour');
    RAISE EXCEPTION 'FAIL: reader could call admin_add_provisional_user';
EXCEPTION WHEN insufficient_privilege THEN
    RAISE NOTICE 'ok: reader denied admin_add_provisional_user';
END $$;

DO $$
BEGIN
    PERFORM update_user_password('alice', 'hash-hijacked');
    RAISE EXCEPTION 'FAIL: reader could change a password';
EXCEPTION WHEN insufficient_privilege THEN
    RAISE NOTICE 'ok: reader denied update_user_password';
END $$;

DO $$
BEGIN
    PERFORM set_user_permission('alice', 'read_only');
    RAISE EXCEPTION 'FAIL: reader could change a permission';
EXCEPTION WHEN insufficient_privilege THEN
    RAISE NOTICE 'ok: reader denied set_user_permission';
END $$;

DO $$
BEGIN
    PERFORM record_login('alice');
    RAISE EXCEPTION 'FAIL: reader could record a login';
EXCEPTION WHEN insufficient_privilege THEN
    RAISE NOTICE 'ok: reader denied record_login';
END $$;

DO $$
BEGIN
    PERFORM add_provisional_user('intruder', 'hash', INTERVAL '1 hour');
    RAISE EXCEPTION 'FAIL: reader could provision a user';
EXCEPTION WHEN insufficient_privilege THEN
    RAISE NOTICE 'ok: reader denied add_provisional_user';
END $$;

--- The sweep deletes rows.  A reader holding this could empty the
--- pending-user list.
DO $$
BEGIN
    PERFORM delete_expired_provisional_users();
    RAISE EXCEPTION 'FAIL: reader could run the sweep';
EXCEPTION WHEN insufficient_privilege THEN
    RAISE NOTICE 'ok: reader denied delete_expired_provisional_users';
END $$;

--------------------------------------------------------------------------
---                        Creating objects                             ---
--------------------------------------------------------------------------
--- On PostgreSQL 14 and older, PUBLIC holds CREATE on the public schema
--- by default.  createDatabase.sql revokes it; this checks that it took,
--- because on 15+ the REVOKE is a no-op and the test would pass for the
--- wrong reason on the version where it matters least.

DO $$
BEGIN
    EXECUTE 'CREATE TABLE reader_should_not_have_this (x INTEGER)';
    EXECUTE 'DROP TABLE reader_should_not_have_this';
    RAISE EXCEPTION 'FAIL: reader could create a table in public';
EXCEPTION WHEN insufficient_privilege THEN
    RAISE NOTICE 'ok: reader denied CREATE in public';
END $$;

\echo '=== reader tests passed ==='
