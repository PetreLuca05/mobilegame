-- LINK-1 SQL tests. One DO block that always ends by raising, so everything is rolled back.
--   ALL LINK SQL TESTS PASSED   every check held
--   TEST FAIL: <what>           the first check that did not
do $test$
declare
  e uuid := gen_random_uuid(); -- the existing account
  g uuid := gen_random_uuid(); -- the guest
  f uuid := gen_random_uuid(); -- a friend of both
  h uuid := gen_random_uuid(); -- a friend of the guest only
  p uuid := gen_random_uuid(); -- another permanent account
  t text; v jsonb; n int;
begin
  create function pg_temp.as_user(u uuid, anon boolean) returns void language plpgsql as $f$
  begin
    reset role;
    perform set_config('request.jwt.claims', json_build_object('sub', u, 'role', 'authenticated', 'is_anonymous', anon)::text, true);
    set local role authenticated;
  end;
  $f$;
  create function pg_temp.rejects(q text, expected text) returns void language plpgsql as $f$
  declare ok boolean := false;
  begin
    begin execute q; ok := true;
    exception when others then
      if position(expected in sqlerrm) = 0 then raise exception 'TEST FAIL: % -> wanted %, got %', q, expected, sqlerrm; end if;
    end;
    if ok then raise exception 'TEST FAIL: % should have been rejected (%)', q, expected; end if;
  end;
  $f$;

  -- coverage guard: every table with a user foreign key is named in the merge function
  select count(*) into n
    from (select distinct k.relname
            from pg_constraint c
            join pg_class k on k.oid = c.conrelid
            join pg_namespace ns on ns.oid = k.relnamespace
           where c.contype = 'f' and ns.nspname = 'public' and c.confrelid in ('auth.users'::regclass, 'public.profiles'::regclass)
             and position('public.' || k.relname in pg_get_functiondef('public.merge_ticket_redeem(text)'::regprocedure)) = 0) missing;
  if n <> 0 then raise exception 'TEST FAIL: % user tables are not named in merge_ticket_redeem', n; end if;

  insert into auth.users (id, aud, role, is_anonymous) values
    (e, 'authenticated', 'authenticated', false), (g, 'authenticated', 'authenticated', true),
    (f, 'authenticated', 'authenticated', true), (h, 'authenticated', 'authenticated', true), (p, 'authenticated', 'authenticated', false);
  update public.profiles set display_name = 'Existing', skin = '{"hair":3}' where id = e;
  update public.profiles set display_name = 'Guest', skin = '{"hair":1}' where id = g;
  update public.stats set sessions = 5, play_seconds = 100, distance_m = 10, elevator_rides = 2, rooms_joined = 1 where user_id = e;
  update public.stats set sessions = 2, play_seconds = 50, distance_m = 4, elevator_rides = 1, rooms_joined = 3 where user_id = g;
  insert into public.friendships (user_lo, user_hi, status, requested_by) values
    (least(e, f), greatest(e, f), 1, e),        -- E already has f
    (least(g, f), greatest(g, f), 1, g),        -- duplicate: dropped
    (least(g, h), greatest(g, h), 0, g),        -- moves, and the requester becomes E
    (least(e, g), greatest(e, g), 1, e);        -- E with itself: dropped
  insert into public.player_status (user_id, state) values (g, 1), (h, 1);
  update public.player_status set host_id = g where user_id = h;
  insert into public.lobby_members (host_id, user_id) values (g, h), (h, g);
  insert into public.lobby_join_attempts (user_id, host_id) values (g, h);

  -- tickets: guests only, redeemed by permanent accounts only
  perform pg_temp.as_user(e, false);
  perform pg_temp.rejects('select public.merge_ticket_create()', 'not_anonymous');
  perform pg_temp.rejects('select * from public.merge_tickets', 'permission denied');
  perform pg_temp.as_user(g, true);
  t := public.merge_ticket_create();
  if t !~ '^[0-9a-f]{32}$' then raise exception 'TEST FAIL: ticket %', t; end if;
  perform pg_temp.rejects(format('select public.merge_ticket_redeem(%L)', t), 'caller_anonymous');
  if public.merge_ticket_create() = t then raise exception 'TEST FAIL: not a new ticket'; end if;
  reset role;
  select count(*) into n from public.merge_tickets where guest_id = g and used_at is null;
  if n <> 1 then raise exception 'TEST FAIL: % live tickets', n; end if;
  perform pg_temp.as_user(e, false);
  perform pg_temp.rejects(format('select public.merge_ticket_redeem(%L)', t), 'ticket_invalid'); -- the replaced one

  -- a claim that lies is caught by the row
  perform pg_temp.as_user(g, false);
  perform pg_temp.rejects(format('select public.merge_ticket_redeem(%L)', 'x'), 'caller_anonymous');

  -- too old
  perform pg_temp.as_user(g, true);
  t := public.merge_ticket_create();
  reset role;
  update public.merge_tickets set created_at = now() - interval '6 minutes' where ticket = t;
  perform pg_temp.as_user(e, false);
  perform pg_temp.rejects(format('select public.merge_ticket_redeem(%L)', t), 'ticket_invalid');

  -- the "guest" must still be anonymous
  perform pg_temp.as_user(g, true);
  t := public.merge_ticket_create();
  reset role;
  update auth.users set is_anonymous = false where id = g;
  perform pg_temp.as_user(e, false);
  perform pg_temp.rejects(format('select public.merge_ticket_redeem(%L)', t), 'guest_not_anonymous');
  reset role;
  update auth.users set is_anonymous = true where id = g;

  -- the merge
  perform pg_temp.as_user(e, false);
  v := public.merge_ticket_redeem(t);
  if (v->>'friends_moved')::int <> 1 or (v->>'friends_dropped')::int <> 2 then raise exception 'TEST FAIL: summary %', v; end if;
  reset role;
  if exists (select 1 from auth.users where id = g) then raise exception 'TEST FAIL: guest still exists'; end if;
  if exists (select 1 from public.profiles where id = g) then raise exception 'TEST FAIL: guest profile still exists'; end if;
  if (select display_name from public.profiles where id = e) <> 'Existing' then raise exception 'TEST FAIL: E name'; end if;
  if (select skin->>'hair' from public.profiles where id = e) <> '3' then raise exception 'TEST FAIL: E skin'; end if;
  if (select row(sessions, play_seconds, distance_m::int, elevator_rides, rooms_joined)::text from public.stats where user_id = e) <> '(7,150,14,3,4)' then
    raise exception 'TEST FAIL: stats %', (select row(sessions, play_seconds, distance_m, elevator_rides, rooms_joined)::text from public.stats where user_id = e);
  end if;
  select count(*) into n from public.friendships x where g in (x.user_lo, x.user_hi);
  if n <> 0 then raise exception 'TEST FAIL: guest friendships remain'; end if;
  if (select count(*) from public.friendships x where e in (x.user_lo, x.user_hi)) <> 2 then raise exception 'TEST FAIL: E should have 2 friendships'; end if;
  if (select requested_by from public.friendships x where x.user_lo = least(e, h) and x.user_hi = greatest(e, h)) <> e then raise exception 'TEST FAIL: requester not moved to E'; end if;
  if (select status from public.friendships x where x.user_lo = least(e, f) and x.user_hi = greatest(e, f)) <> 1 then raise exception 'TEST FAIL: E existing friendship changed'; end if;
  if exists (select 1 from public.lobby_members m where g in (m.host_id, m.user_id)) then raise exception 'TEST FAIL: lobby rows remain'; end if;
  if exists (select 1 from public.lobby_join_attempts a where g in (a.user_id, a.host_id)) then raise exception 'TEST FAIL: attempts remain'; end if;
  if (select host_id from public.player_status where user_id = h) is not null then raise exception 'TEST FAIL: host_id not cleared'; end if;

  -- twice
  perform pg_temp.as_user(e, false);
  perform pg_temp.rejects(format('select public.merge_ticket_redeem(%L)', t), 'ticket_invalid');

  -- cleanup: an old guest goes, a recent one and a permanent one stay
  reset role;
  update public.stats set last_seen_at = now() - interval '181 days' where user_id = h;
  update public.stats set last_seen_at = now() - interval '181 days' where user_id = p;
  perform public.link_cleanup();
  if exists (select 1 from auth.users where id = h) then raise exception 'TEST FAIL: old guest kept'; end if;
  if not exists (select 1 from auth.users where id = f) then raise exception 'TEST FAIL: recent guest removed'; end if;
  if not exists (select 1 from auth.users where id = p) then raise exception 'TEST FAIL: permanent user removed'; end if;

  raise exception 'ALL LINK SQL TESTS PASSED';
end;
$test$;
