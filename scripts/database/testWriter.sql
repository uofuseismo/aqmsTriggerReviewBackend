--- Purpose: Exercises the database as the read_write backend role.
---
--- Run with ON_ERROR_STOP=1; any failed expectation raises and the run
--- stops there.  Reaching the end means everything passed.
---
--- Run as the WRITER, not as a superuser.  A superuser bypasses every
--- privilege check, so the negative tests below -- the ones asserting
--- that even the writer cannot read a password hash -- would pass
--- against a database whose grants were completely wrong.
---
--- A note on transactions, because it shapes the file.  NOW() is the
--- transaction timestamp: it does not advance inside a statement, so a
--- DO block that inserts a row, sleeps, and then checks that time has
--- passed will always fail.  psql runs each statement in its own
--- transaction, so anything testing the passage of time is deliberately
--- split across separate top-level statements.  Do not merge them.
---
--- Copyright: Ben Baker (UUSS) distributed under the MIT license.

\echo '=== writer tests ==='

--------------------------------------------------------------------------
---                       Direct table access                           ---
--------------------------------------------------------------------------
--- The headline property: the backend that writes everything still
--- cannot read a password hash.

DO $$
BEGIN
    PERFORM 1 FROM users LIMIT 1;
    RAISE EXCEPTION 'FAIL: writer could SELECT from users';
EXCEPTION WHEN insufficient_privilege THEN
    RAISE NOTICE 'ok: writer denied direct SELECT on users';
END $$;

DO $$
BEGIN
    UPDATE users SET password_hash = 'x' WHERE TRUE;
    RAISE EXCEPTION 'FAIL: writer could UPDATE users';
EXCEPTION WHEN insufficient_privilege THEN
    RAISE NOTICE 'ok: writer denied direct UPDATE on users';
END $$;

--------------------------------------------------------------------------
---                          Creating users                             ---
--------------------------------------------------------------------------

DO $$
BEGIN
    IF NOT admin_add_user('root', 'alice', 'hash-alice', 'read_write') THEN
        RAISE EXCEPTION 'FAIL: admin_add_user(alice) returned FALSE';
    END IF;
    RAISE NOTICE 'ok: add_user creates a user';

    IF admin_add_user('root', 'alice', 'hash-again') THEN
        RAISE EXCEPTION 'FAIL: duplicate admin_add_user(alice) returned TRUE';
    END IF;
    RAISE NOTICE 'ok: duplicate user rejected';

    --- Names are folded, so ALICE is the same person as alice.
    IF admin_add_user('root', 'ALICE', 'hash-again') THEN
        RAISE EXCEPTION 'FAIL: admin_add_user(ALICE) created a second alice';
    END IF;
    RAISE NOTICE 'ok: user names are case-folded';

    IF admin_add_user('root', '', 'hash') THEN
        RAISE EXCEPTION 'FAIL: add_user with empty name returned TRUE';
    END IF;
    IF admin_add_user('root', 'bob', '') THEN
        RAISE EXCEPTION 'FAIL: add_user with empty hash returned TRUE';
    END IF;
    RAISE NOTICE 'ok: empty name and empty hash rejected';

    --- Trips the CHECK; must come back as FALSE, not as an exception
    --- the backend has to know how to catch.
    IF admin_add_user('root', 'mallory', 'hash-m', 'superuser') THEN
        RAISE EXCEPTION 'FAIL: add_user accepted an unknown permission';
    END IF;
    RAISE NOTICE 'ok: unknown permission level rejected';
END $$;

DO $$
BEGIN
    IF get_password_hash('alice') <> 'hash-alice' THEN
        RAISE EXCEPTION 'FAIL: get_password_hash(alice) wrong';
    END IF;
    --- Whitespace and case are normalised on the way in.
    IF get_password_hash('  ALICE  ') <> 'hash-alice' THEN
        RAISE EXCEPTION 'FAIL: get_password_hash does not normalise input';
    END IF;
    IF get_password_hash('nobody') IS NOT NULL THEN
        RAISE EXCEPTION 'FAIL: get_password_hash(nobody) not NULL';
    END IF;
    RAISE NOTICE 'ok: get_password_hash';

    IF get_user_permission('alice') <> 'read_write' THEN
        RAISE EXCEPTION 'FAIL: alice has the wrong permission';
    END IF;
    IF get_user_permission('nobody') IS NOT NULL THEN
        RAISE EXCEPTION 'FAIL: get_user_permission(nobody) not NULL';
    END IF;
    RAISE NOTICE 'ok: get_user_permission';
END $$;

--------------------------------------------------------------------------
---                            Permissions                              ---
--------------------------------------------------------------------------

DO $$
BEGIN
    IF NOT admin_add_user('root', 'carol', 'hash-carol') THEN
        RAISE EXCEPTION 'FAIL: admin_add_user(carol) returned FALSE';
    END IF;
    --- The default matters: a user created without a stated level must
    --- come out read_only, never the more permissive one.
    IF get_user_permission('carol') <> 'read_only' THEN
        RAISE EXCEPTION 'FAIL: default permission is not read_only';
    END IF;
    RAISE NOTICE 'ok: permission defaults to read_only';

    IF NOT admin_set_user_permission('root', 'carol', 'read_write') THEN
        RAISE EXCEPTION 'FAIL: set_user_permission returned FALSE';
    END IF;
    IF get_user_permission('carol') <> 'read_write' THEN
        RAISE EXCEPTION 'FAIL: set_user_permission did not stick';
    END IF;
    RAISE NOTICE 'ok: set_user_permission';

    IF admin_set_user_permission('root', 'carol', 'wheel') THEN
        RAISE EXCEPTION 'FAIL: set_user_permission accepted a bad level';
    END IF;
    IF get_user_permission('carol') <> 'read_write' THEN
        RAISE EXCEPTION 'FAIL: rejected set_user_permission still changed it';
    END IF;
    IF admin_set_user_permission('root', 'nobody', 'read_only') THEN
        RAISE EXCEPTION 'FAIL: admin_set_user_permission(nobody) returned TRUE';
    END IF;
    RAISE NOTICE 'ok: bad permission level and unknown user rejected';
END $$;

--------------------------------------------------------------------------
---                              Logins                                 ---
--------------------------------------------------------------------------

DO $$
DECLARE v_last_login TIMESTAMPTZ;
BEGIN
    SELECT last_login INTO v_last_login FROM list_users() WHERE name = 'alice';
    IF v_last_login IS NOT NULL THEN
        RAISE EXCEPTION 'FAIL: a new user already has a last_login';
    END IF;
    IF NOT record_login('alice') THEN
        RAISE EXCEPTION 'FAIL: record_login(alice) returned FALSE';
    END IF;
    SELECT last_login INTO v_last_login FROM list_users() WHERE name = 'alice';
    IF v_last_login IS NULL THEN
        RAISE EXCEPTION 'FAIL: record_login did not set last_login';
    END IF;
    IF record_login('nobody') THEN
        RAISE EXCEPTION 'FAIL: record_login(nobody) returned TRUE';
    END IF;
    RAISE NOTICE 'ok: record_login';
END $$;

--------------------------------------------------------------------------
---                           Password change                           ---
--------------------------------------------------------------------------

DO $$
BEGIN
    IF NOT update_user_password('alice', 'hash-alice-2') THEN
        RAISE EXCEPTION 'FAIL: update_user_password returned FALSE';
    END IF;
    IF get_password_hash('alice') <> 'hash-alice-2' THEN
        RAISE EXCEPTION 'FAIL: password did not change';
    END IF;
    IF update_user_password('alice', '') THEN
        RAISE EXCEPTION 'FAIL: empty hash accepted';
    END IF;
    IF get_password_hash('alice') <> 'hash-alice-2' THEN
        RAISE EXCEPTION 'FAIL: rejected update still changed the password';
    END IF;
    IF update_user_password('nobody', 'hash') THEN
        RAISE EXCEPTION 'FAIL: update_user_password(nobody) returned TRUE';
    END IF;
    RAISE NOTICE 'ok: update_user_password';
END $$;

--------------------------------------------------------------------------
---                       Provisioning users                            ---
--------------------------------------------------------------------------

DO $$
DECLARE v_deadline TIMESTAMPTZ;
BEGIN
    IF NOT admin_add_provisional_user('root', 'dave', 'hash-dummy', INTERVAL '1 hour') THEN
        RAISE EXCEPTION 'FAIL: add_provisional_user returned FALSE';
    END IF;
    SELECT provisional_until INTO v_deadline
      FROM list_users() WHERE name = 'dave';
    IF v_deadline IS NULL THEN
        RAISE EXCEPTION 'FAIL: provisional user has no deadline';
    END IF;
    IF NOT user_must_change_password('dave') THEN
        RAISE EXCEPTION 'FAIL: provisional user not flagged';
    END IF;
    --- The dummy password has to actually work, or they cannot get in
    --- to replace it.
    IF get_password_hash('dave') <> 'hash-dummy' THEN
        RAISE EXCEPTION 'FAIL: provisional user cannot log in';
    END IF;
    RAISE NOTICE 'ok: add_provisional_user';

    IF admin_add_provisional_user('root', 'eve', 'hash', INTERVAL '0') THEN
        RAISE EXCEPTION 'FAIL: zero interval accepted';
    END IF;
    IF admin_add_provisional_user('root', 'eve', 'hash', INTERVAL '-1 hour') THEN
        RAISE EXCEPTION 'FAIL: negative interval accepted';
    END IF;
    IF admin_add_provisional_user('root', 'eve', 'hash', NULL) THEN
        RAISE EXCEPTION 'FAIL: NULL interval accepted';
    END IF;
    IF admin_add_provisional_user('root', 'alice', 'hash', INTERVAL '1 hour') THEN
        RAISE EXCEPTION 'FAIL: provisioned over an existing user';
    END IF;
    RAISE NOTICE 'ok: bad intervals and duplicate names rejected';

    --- An ordinary user must never look provisional.
    IF user_must_change_password('alice') THEN
        RAISE EXCEPTION 'FAIL: an activated user is flagged provisional';
    END IF;
    IF user_must_change_password('nobody') IS NOT NULL THEN
        RAISE EXCEPTION 'FAIL: user_must_change_password(nobody) not NULL';
    END IF;
    RAISE NOTICE 'ok: user_must_change_password';
END $$;

--- Changing the password is the activation; there is no second call.
DO $$
BEGIN
    IF NOT update_user_password('dave', 'hash-dave-real') THEN
        RAISE EXCEPTION 'FAIL: provisional user could not set a password';
    END IF;
    IF user_must_change_password('dave') THEN
        RAISE EXCEPTION 'FAIL: password change did not activate the account';
    END IF;
    IF (SELECT provisional_until FROM list_users() WHERE name = 'dave')
       IS NOT NULL THEN
        RAISE EXCEPTION 'FAIL: deadline survived activation';
    END IF;
    RAISE NOTICE 'ok: changing the password activates the account';
END $$;

--------------------------------------------------------------------------
---                     Provisional expiry and sweep                    ---
--------------------------------------------------------------------------
--- Separate statements from here down: NOW() is frozen per transaction,
--- so a sleep inside one DO block would not move the clock.

DO $$
BEGIN
    IF NOT admin_add_provisional_user('root', 'frank', 'hash-frank', INTERVAL '2 seconds') THEN
        RAISE EXCEPTION 'FAIL: could not provision frank';
    END IF;
    IF get_password_hash('frank') <> 'hash-frank' THEN
        RAISE EXCEPTION 'FAIL: frank cannot log in before his deadline';
    END IF;
    RAISE NOTICE 'ok: provisional user works before the deadline';
END $$;

SELECT pg_sleep(2.5);

--- The deadline binds on its own: frank is locked out here, before the
--- sweep has run.  If this passes only after the DELETE below, then the
--- cron interval is the real deadline and this property is missing.
DO $$
BEGIN
    IF get_password_hash('frank') IS NOT NULL THEN
        RAISE EXCEPTION 'FAIL: expired user can still log in before the sweep';
    END IF;
    IF update_user_password('frank', 'hash-too-late') THEN
        RAISE EXCEPTION 'FAIL: expired user could activate after the deadline';
    END IF;
    RAISE NOTICE 'ok: expired user is locked out before the sweep runs';
END $$;

DO $$
DECLARE n INTEGER;
    v_before INTEGER;
    v_after INTEGER;
BEGIN
    SELECT count(*) INTO v_before FROM list_users();
    n := delete_expired_provisional_users();
    IF n <> 1 THEN
        RAISE EXCEPTION 'FAIL: sweep deleted % rows, expected 1', n;
    END IF;
    SELECT count(*) INTO v_after FROM list_users();
    IF v_after <> v_before - 1 THEN
        RAISE EXCEPTION 'FAIL: sweep removed % users, expected 1',
                        v_before - v_after;
    END IF;
    IF EXISTS (SELECT 1 FROM list_users() WHERE name = 'frank') THEN
        RAISE EXCEPTION 'FAIL: frank survived the sweep';
    END IF;
    RAISE NOTICE 'ok: sweep deletes exactly the expired account';

    --- The safety property: run it again and again, it must never
    --- touch an activated user.
    n := delete_expired_provisional_users();
    IF n <> 0 THEN
        RAISE EXCEPTION 'FAIL: second sweep deleted % rows', n;
    END IF;
    IF NOT EXISTS (SELECT 1 FROM list_users() WHERE name = 'alice') THEN
        RAISE EXCEPTION 'FAIL: sweep deleted an activated user';
    END IF;
    IF NOT EXISTS (SELECT 1 FROM list_users() WHERE name = 'dave') THEN
        RAISE EXCEPTION 'FAIL: sweep deleted a recently activated user';
    END IF;
    RAISE NOTICE 'ok: sweep leaves activated users alone';
END $$;

--------------------------------------------------------------------------
---                        Admin enforcement                           ---
--------------------------------------------------------------------------
--- The grant lets the WRITER call these; the actor argument decides
--- whether the person behind the request may. Both have to hold, and
--- these tests are about the second one.

--- The ungated versions must be out of reach even for the writer.
--- If these ever pass, the admin layer is decoration: the backend can
--- skip straight past every actor check.
DO $$
BEGIN
    PERFORM add_user('bypass', 'hash-bypass');
    RAISE EXCEPTION 'FAIL: writer could call ungated add_user';
EXCEPTION WHEN insufficient_privilege THEN
    RAISE NOTICE 'ok: writer denied ungated add_user';
END $$;

DO $$
BEGIN
    PERFORM set_user_permission('alice', 'admin');
    RAISE EXCEPTION 'FAIL: writer could call ungated set_user_permission';
EXCEPTION WHEN insufficient_privilege THEN
    RAISE NOTICE 'ok: writer denied ungated set_user_permission';
END $$;

DO $$
BEGIN
    PERFORM remove_user('alice');
    RAISE EXCEPTION 'FAIL: writer could call ungated remove_user';
EXCEPTION WHEN insufficient_privilege THEN
    RAISE NOTICE 'ok: writer denied ungated remove_user';
END $$;

DO $$
BEGIN
    PERFORM add_provisional_user('bypass', 'hash', INTERVAL '1 hour');
    RAISE EXCEPTION 'FAIL: writer could call ungated add_provisional_user';
EXCEPTION WHEN insufficient_privilege THEN
    RAISE NOTICE 'ok: writer denied ungated add_provisional_user';
END $$;

DO $$
BEGIN
    PERFORM require_admin('root');
    RAISE EXCEPTION 'FAIL: writer could call require_admin directly';
EXCEPTION WHEN insufficient_privilege THEN
    RAISE NOTICE 'ok: writer denied require_admin';
END $$;

--- alice is read_write, which is not admin.  Every one of these must
--- raise rather than return FALSE: the backend has to be able to tell
--- 'you may not' from 'that did not work'.
DO $$
BEGIN
    PERFORM admin_add_user('alice', 'sneaky', 'hash-sneaky');
    RAISE EXCEPTION 'FAIL: a read_write user created an account';
EXCEPTION WHEN insufficient_privilege THEN
    RAISE NOTICE 'ok: non-admin denied admin_add_user';
END $$;

DO $$
BEGIN
    PERFORM admin_set_user_permission('alice', 'alice', 'admin');
    RAISE EXCEPTION 'FAIL: a read_write user promoted themselves';
EXCEPTION WHEN insufficient_privilege THEN
    RAISE NOTICE 'ok: non-admin cannot promote themselves';
END $$;

DO $$
BEGIN
    PERFORM admin_remove_user('carol', 'alice');
    RAISE EXCEPTION 'FAIL: a read_write user deleted someone';
EXCEPTION WHEN insufficient_privilege THEN
    RAISE NOTICE 'ok: non-admin denied admin_remove_user';
END $$;

DO $$
BEGIN
    PERFORM admin_reset_user_password('alice', 'carol', 'hash-x',
                                      INTERVAL '1 day');
    RAISE EXCEPTION 'FAIL: a read_write user reset someone''s password';
EXCEPTION WHEN insufficient_privilege THEN
    RAISE NOTICE 'ok: non-admin denied admin_reset_user_password';
END $$;

--- An actor who does not exist is not an administrator either.
DO $$
BEGIN
    PERFORM admin_add_user('nobody', 'sneaky', 'hash-sneaky');
    RAISE EXCEPTION 'FAIL: an unknown actor created an account';
EXCEPTION WHEN insufficient_privilege THEN
    RAISE NOTICE 'ok: unknown actor denied';
END $$;

--- Nor is an administrator who has not activated yet.  They hold a
--- password that arrived by e-mail; that is not authority.
DO $$
BEGIN
    IF NOT admin_add_provisional_user('root', 'pending_admin', 'hash-pending',
                                      INTERVAL '1 hour', 'admin') THEN
        RAISE EXCEPTION 'FAIL: could not provision an admin';
    END IF;
    IF user_is_admin('pending_admin') THEN
        RAISE EXCEPTION 'FAIL: a provisional admin counts as an admin';
    END IF;
    RAISE NOTICE 'ok: a provisional admin is not yet an admin';
END $$;

DO $$
BEGIN
    PERFORM admin_add_user('pending_admin', 'sneaky', 'hash-sneaky');
    RAISE EXCEPTION 'FAIL: a provisional admin created an account';
EXCEPTION WHEN insufficient_privilege THEN
    RAISE NOTICE 'ok: provisional admin denied until activated';
END $$;

--- Activating turns the authority on, with no separate grant step.
DO $$
BEGIN
    IF NOT update_user_password('pending_admin', 'hash-pending-real') THEN
        RAISE EXCEPTION 'FAIL: pending_admin could not activate';
    END IF;
    IF NOT user_is_admin('pending_admin') THEN
        RAISE EXCEPTION 'FAIL: activation did not confer admin';
    END IF;
    IF NOT admin_add_user('pending_admin', 'made_by_pending', 'hash-mbp') THEN
        RAISE EXCEPTION 'FAIL: activated admin still cannot act';
    END IF;
    PERFORM admin_remove_user('root', 'made_by_pending');
    RAISE NOTICE 'ok: activating a provisional admin confers authority';
END $$;

--------------------------------------------------------------------------
---                      The permission ordering                        ---
--------------------------------------------------------------------------

DO $$
BEGIN
    --- The point of ranking: an admin passes a read_write check without
    --- every call site having to remember to spell out the levels.
    IF NOT user_has_permission('root', 'read_write') THEN
        RAISE EXCEPTION 'FAIL: admin does not satisfy a read_write check';
    END IF;
    IF NOT user_has_permission('root', 'read_only') THEN
        RAISE EXCEPTION 'FAIL: admin does not satisfy a read_only check';
    END IF;
    IF NOT user_has_permission('alice', 'read_only') THEN
        RAISE EXCEPTION 'FAIL: read_write does not satisfy read_only';
    END IF;
    IF user_has_permission('alice', 'admin') THEN
        RAISE EXCEPTION 'FAIL: read_write satisfies an admin check';
    END IF;
    RAISE NOTICE 'ok: permission levels are ranked';

    --- A typo in the REQUIREMENT must deny, not admit.  The dangerous
    --- direction is a call site asking for 'readwrite' and being told
    --- yes by a NULL comparison.
    IF user_has_permission('root', 'readwrite') THEN
        RAISE EXCEPTION 'FAIL: an unknown requirement was satisfied';
    END IF;
    IF user_has_permission('nobody', 'read_only') THEN
        RAISE EXCEPTION 'FAIL: an unknown user satisfied a check';
    END IF;
    RAISE NOTICE 'ok: unknown levels and unknown users deny';
END $$;

--------------------------------------------------------------------------
---                       Last-admin protection                         ---
--------------------------------------------------------------------------
--- Locking the database out of its own user management is a one-call
--- mistake, and the way back in is a superuser session.  Note these
--- tests run while pending_admin is also an active admin, so the
--- protection is checked from both sides: it must NOT fire while a
--- second admin exists, and must fire once they are gone.

DO $$
BEGIN
    IF count_active_admins() <> 2 THEN
        RAISE EXCEPTION 'FAIL: expected 2 active admins, found %',
                        count_active_admins();
    END IF;
    --- Permitted: root is not the last one right now.
    IF NOT admin_set_user_permission('pending_admin', 'root', 'read_write') THEN
        RAISE EXCEPTION 'FAIL: could not demote an admin while another exists';
    END IF;
    IF user_is_admin('root') THEN
        RAISE EXCEPTION 'FAIL: demotion did not take';
    END IF;
    RAISE NOTICE 'ok: an admin can be demoted while another remains';

    --- And back, so the rest of the suite still has root.
    IF NOT admin_set_user_permission('pending_admin', 'root', 'admin') THEN
        RAISE EXCEPTION 'FAIL: could not restore root to admin';
    END IF;
END $$;

DO $$
BEGIN
    IF NOT admin_remove_user('root', 'pending_admin') THEN
        RAISE EXCEPTION 'FAIL: could not remove the second admin';
    END IF;
    IF count_active_admins() <> 1 THEN
        RAISE EXCEPTION 'FAIL: expected 1 active admin, found %',
                        count_active_admins();
    END IF;
    RAISE NOTICE 'ok: down to a single administrator';
END $$;

DO $$
BEGIN
    PERFORM admin_set_user_permission('root', 'root', 'read_write');
    RAISE EXCEPTION 'FAIL: the last admin demoted themselves';
EXCEPTION WHEN insufficient_privilege THEN
    RAISE NOTICE 'ok: refused to demote the last admin';
END $$;

DO $$
BEGIN
    PERFORM admin_remove_user('root', 'root');
    RAISE EXCEPTION 'FAIL: the last admin deleted themselves';
EXCEPTION WHEN insufficient_privilege THEN
    RAISE NOTICE 'ok: refused to delete the last admin';
END $$;

--- A reset makes the target provisional, and a provisional admin cannot
--- act -- so this is the same lockout wearing a different hat, and the
--- easiest one to miss.
DO $$
BEGIN
    PERFORM admin_reset_user_password('root', 'root', 'hash-reset',
                                      INTERVAL '1 day');
    RAISE EXCEPTION 'FAIL: the last admin reset themselves into a lockout';
EXCEPTION WHEN insufficient_privilege THEN
    RAISE NOTICE 'ok: refused to reset the last admin';
END $$;

DO $$
BEGIN
    --- The last admin must still be able to do everything else.
    IF NOT admin_set_user_permission('root', 'root', 'admin') THEN
        RAISE EXCEPTION 'FAIL: last admin cannot re-assert their own level';
    END IF;
    IF user_is_admin('root') IS NOT TRUE THEN
        RAISE EXCEPTION 'FAIL: root is no longer an admin';
    END IF;
    RAISE NOTICE 'ok: the last admin is otherwise unrestricted';
END $$;

--------------------------------------------------------------------------
---                            Hiring Tim                               ---
--------------------------------------------------------------------------
--- The whole lifecycle end to end, in the order it actually happens.

--- Hired.  Read-only while he trains, with a dummy password he has to
--- replace within the week.
DO $$
BEGIN
    IF NOT admin_add_provisional_user('root', 'tim', 'hash-tim-dummy',
                                      INTERVAL '7 days', 'read_only') THEN
        RAISE EXCEPTION 'FAIL: could not hire tim';
    END IF;
    IF NOT user_must_change_password('tim') THEN
        RAISE EXCEPTION 'FAIL: tim is not provisional';
    END IF;
    RAISE NOTICE 'ok: tim hired, provisional, read_only';
END $$;

--- First login: he replaces the dummy password, which activates him.
DO $$
BEGIN
    IF get_password_hash('tim') <> 'hash-tim-dummy' THEN
        RAISE EXCEPTION 'FAIL: tim cannot log in with his dummy password';
    END IF;
    IF NOT update_user_password('tim', 'hash-tim-real') THEN
        RAISE EXCEPTION 'FAIL: tim could not set his password';
    END IF;
    IF user_must_change_password('tim') THEN
        RAISE EXCEPTION 'FAIL: tim is still provisional after activating';
    END IF;
    IF NOT user_has_permission('tim', 'read_only') THEN
        RAISE EXCEPTION 'FAIL: tim cannot read';
    END IF;
    IF user_has_permission('tim', 'read_write') THEN
        RAISE EXCEPTION 'FAIL: tim can write during training';
    END IF;
    RAISE NOTICE 'ok: tim activated, still read_only';
END $$;

--- Training over.
DO $$
BEGIN
    IF NOT admin_set_user_permission('root', 'tim', 'read_write') THEN
        RAISE EXCEPTION 'FAIL: could not promote tim';
    END IF;
    IF NOT user_has_permission('tim', 'read_write') THEN
        RAISE EXCEPTION 'FAIL: tim cannot write after promotion';
    END IF;
    IF user_has_permission('tim', 'admin') THEN
        RAISE EXCEPTION 'FAIL: promoting tim made him an admin';
    END IF;
    RAISE NOTICE 'ok: tim promoted to read_write';
END $$;

--- Tim forgets his password.  The reset hands him a fresh dummy rather
--- than a password an administrator now knows, and he has a day to
--- replace it.
DO $$
BEGIN
    IF NOT admin_reset_user_password('root', 'tim', 'hash-tim-dummy-2',
                                     INTERVAL '1 day') THEN
        RAISE EXCEPTION 'FAIL: could not reset tim';
    END IF;
    IF NOT user_must_change_password('tim') THEN
        RAISE EXCEPTION 'FAIL: a reset did not make tim provisional again';
    END IF;
    --- The promotion survives the reset: he comes back as read_write,
    --- not demoted to the default.
    IF get_user_permission('tim') <> 'read_write' THEN
        RAISE EXCEPTION 'FAIL: the reset changed tim''s permission level';
    END IF;
    IF get_password_hash('tim') <> 'hash-tim-dummy-2' THEN
        RAISE EXCEPTION 'FAIL: tim cannot log in with the reset password';
    END IF;
    RAISE NOTICE 'ok: tim reset to a fresh provisional password';
END $$;

DO $$
BEGIN
    IF NOT update_user_password('tim', 'hash-tim-real-2') THEN
        RAISE EXCEPTION 'FAIL: tim could not recover his account';
    END IF;
    IF user_must_change_password('tim') THEN
        RAISE EXCEPTION 'FAIL: tim is still provisional';
    END IF;
    RAISE NOTICE 'ok: tim recovered his account';
END $$;

--- Tim leaves.

--------------------------------------------------------------------------
---                          Removing users                             ---
--------------------------------------------------------------------------

--------------------------------------------------------------------------
---                             list_users                              ---
--------------------------------------------------------------------------

DO $$
DECLARE n INTEGER;
BEGIN
    SELECT count(*) INTO n FROM list_users();
    IF n < 3 THEN
        RAISE EXCEPTION 'FAIL: list_users returned only % rows', n;
    END IF;
    RAISE NOTICE 'ok: list_users returns % users', n;
END $$;

--- The column that must never reach a frontend is not in the result
--- type at all, so this reference cannot resolve.  A column added
--- carelessly later turns this from an error into a pass, which is the
--- point of asserting it.
DO $$
BEGIN
    PERFORM password_hash FROM list_users();
    RAISE EXCEPTION 'FAIL: list_users exposes password_hash';
EXCEPTION WHEN undefined_column THEN
    RAISE NOTICE 'ok: list_users has no password_hash column';
END $$;

\echo '=== writer tests passed ==='
