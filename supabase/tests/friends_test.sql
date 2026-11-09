-- FRIENDS-1 SQL tests. Paste into the SQL editor or psql, or send through the
-- Supabase MCP execute_sql. One DO block: it always ends by raising, so
-- everything it created is rolled back.
--   ALL FRIENDS SQL TESTS PASSED   every check held
--   TEST FAIL: <what>              the first check that did not
do $test$
declare
  a uuid := gen_random_uuid(); -- host
  b uuid := gen_random_uuid(); -- friend
  c uuid := gen_random_uuid(); -- stranger
  d uuid := gen_random_uuid();
  e uuid := gen_random_uuid();
  f uuid := gen_random_uuid();
  a_code text; b_code text; token text; n int; v text;
begin
  -- helpers live only for this session
  create function pg_temp.as_user(u uuid) returns void language plpgsql as $f$
  begin
    reset role;
    perform set_config('request.jwt.claims', json_build_object('sub', u, 'role', 'authenticated')::text, true);
    set local role authenticated;
  end;
  $f$;
  create function pg_temp.as_admin() returns void language plpgsql as $f$ begin reset role; end; $f$;
  -- runs `q`; it must fail with exactly `expected` in the message
  create function pg_temp.rejects(q text, expected text) returns void language plpgsql as $f$
  declare ok boolean := false;
  begin
    begin
      execute q;
      ok := true;
    exception when others then
      if position(expected in sqlerrm) = 0 then
        raise exception 'TEST FAIL: % -> wanted %, got %', q, expected, sqlerrm;
      end if;
    end;
    if ok then
      raise exception 'TEST FAIL: % should have been rejected (%)', q, expected;
    end if;
  end;
  $f$;

  insert into auth.users (id, aud, role, is_anonymous)
    select u, 'authenticated', 'authenticated', true from unnest(array[a, b, c, d, e, f]) u;

  -- codes: set by the trigger, right alphabet, unique
  select count(*) into n from public.profiles p
    where p.id in (a, b, c, d, e, f) and p.friend_code ~ '^[23456789ABCDEFGHJKMNPQRSTUVWXYZ]{6}$';
  if n <> 6 then raise exception 'TEST FAIL: friend codes not set (%)', n; end if;
  select friend_code into a_code from public.profiles where id = a;
  select friend_code into b_code from public.profiles where id = b;

  -- nobody can write the code or read it through the table
  perform pg_temp.as_user(a);
  perform pg_temp.rejects(format('update public.profiles set friend_code = %L where id = %L', 'ZZZZZZ', a), 'permission denied');
  perform pg_temp.rejects('select friend_code from public.profiles limit 1', 'permission denied');
  if public.my_friend_code() <> a_code then raise exception 'TEST FAIL: my_friend_code'; end if;

  -- no direct client writes
  perform pg_temp.rejects(format('insert into public.friendships (user_lo, user_hi, status, requested_by) values (%L, %L, 1, %L)', least(a, c), greatest(a, c), a), 'permission denied');

  -- status: nobody reads it, not even their own row
  perform public.status_heartbeat(1, true, 0, 1, 7, 1000);
  perform pg_temp.rejects('select * from public.player_status', 'permission denied');
  perform pg_temp.rejects('select * from public.lobby_members', 'permission denied');
  perform pg_temp.as_admin();
  select room_token into token from public.player_status where user_id = a;
  if token !~ '^[0-9a-f]{16}$' then raise exception 'TEST FAIL: room token %', token; end if;

  -- a non-friend cannot ask to join
  perform pg_temp.as_user(c);
  perform pg_temp.rejects(format('select public.lobby_join_request(%L)', a), 'not_friends');

  -- request by code, then the friend's list shows it, then accepted
  perform pg_temp.as_user(b);
  if public.friend_request_by_code(lower(a_code)) <> 'requested' then raise exception 'TEST FAIL: request by code'; end if;
  perform pg_temp.rejects(format('select public.friend_request_by_code(%L)', a_code), 'already_pending');
  perform pg_temp.rejects(format('select public.friend_request_by_code(%L)', b_code), 'self');
  perform pg_temp.rejects(format('select public.lobby_join_request(%L)', a), 'not_friends'); -- pending is not friends
  if (select l.relation from public.friends_list() l where l.user_id = a) <> 'outgoing' then raise exception 'TEST FAIL: outgoing'; end if;
  perform pg_temp.as_user(a);
  if (select l.relation from public.friends_list() l where l.user_id = b) <> 'incoming' then raise exception 'TEST FAIL: incoming'; end if;
  perform public.friend_respond(b, true);
  if (select l.relation from public.friends_list() l where l.user_id = b) <> 'friend' then raise exception 'TEST FAIL: friend'; end if;

  -- friends_list never carries the token
  if exists (select 1 from public.friends_list() l where to_jsonb(l)::text like '%' || token || '%') then
    raise exception 'TEST FAIL: friends_list leaks the room token';
  end if;

  -- a stranger cannot accept someone else's request or see the friendship
  perform pg_temp.as_user(c);
  perform pg_temp.rejects(format('select public.friend_respond(%L, true)', a), 'no_request');
  select count(*) into n from public.friendships;
  if n <> 0 then raise exception 'TEST FAIL: stranger sees % friendships', n; end if;
  select count(*) into n from public.friend_search('Pl');
  if n > 10 then raise exception 'TEST FAIL: friend_search returned %', n; end if;

  -- open lobby: the friend sees it; the rate limit holds
  perform pg_temp.as_user(b);
  if (select l.status from public.friends_list() l where l.user_id = a) <> 'open' then raise exception 'TEST FAIL: not open'; end if;
  perform public.lobby_join_request(a);
  perform pg_temp.rejects(format('select public.lobby_join_request(%L)', a), 'rate_limited');
  perform pg_temp.as_admin();
  update public.lobby_join_attempts set last_at = now() - interval '11 seconds' where user_id = b;

  -- locked: rejected on the server, whatever the client thinks
  perform pg_temp.as_user(a);
  perform public.status_heartbeat(1, false, 0, 1, 7, 1000);
  perform pg_temp.as_user(b);
  if (select l.status from public.friends_list() l where l.user_id = a) <> 'locked' then raise exception 'TEST FAIL: not locked'; end if;
  perform pg_temp.rejects(format('select public.lobby_join_request(%L)', a), 'lobby_locked');

  -- a stale host is offline
  perform pg_temp.as_user(a);
  perform public.status_heartbeat(1, true, 0, 1, 7, 1000);
  perform public.status_clear();
  perform pg_temp.as_user(b);
  if (select l.status from public.friends_list() l where l.user_id = a) <> 'offline' then raise exception 'TEST FAIL: not offline'; end if;
  perform pg_temp.rejects(format('select public.lobby_join_request(%L)', a), 'host_offline');
  perform pg_temp.as_user(a);
  if public.status_heartbeat(1, true, 0, 1, 7, 1000) <> token then raise exception 'TEST FAIL: a short background changed the token'; end if;

  -- only someone who asked can be let in, and only by the host
  perform pg_temp.rejects(format('select public.lobby_join_respond(%L, true)', c), 'no_request');
  perform pg_temp.as_user(b);
  perform public.lobby_join_request(a);
  perform pg_temp.as_user(c);
  perform pg_temp.rejects(format('select public.lobby_join_respond(%L, true)', b), 'no_request');

  -- Realtime, the way the server checks a join: one message, read back as the user
  perform pg_temp.as_admin();
  perform set_config('realtime.topic', 'room:' || token, true);
  insert into realtime.messages (topic, extension, event, payload, private)
    values ('room:' || token, 'broadcast', 'probe', '{}', true);

  -- before the grant: the guest cannot subscribe or send
  perform pg_temp.as_user(b);
  select count(*) into n from realtime.messages where event = 'probe';
  if n <> 0 then raise exception 'TEST FAIL: non-member reads the room'; end if;
  perform pg_temp.rejects($q$insert into realtime.messages (topic, extension, event, payload, private) values ('x', 'broadcast', 'e', '{}', true)$q$, 'row-level security');

  -- the host can
  perform pg_temp.as_user(a);
  select count(*) into n from realtime.messages where event = 'probe';
  if n <> 1 then raise exception 'TEST FAIL: host cannot read the room'; end if;
  insert into realtime.messages (topic, extension, event, payload, private) values ('room:' || token, 'broadcast', 'host_probe', '{}', true);

  -- accepted: the guest can, a stranger still cannot
  perform public.lobby_join_respond(b, true);
  perform pg_temp.rejects(format('select public.lobby_join_respond(%L, true)', b), 'no_request'); -- one answer per request
  perform pg_temp.as_user(b);
  select count(*) into n from realtime.messages where event = 'probe';
  if n <> 1 then raise exception 'TEST FAIL: member cannot read the room'; end if;
  perform pg_temp.rejects(format('select public.lobby_join_request(%L)', a), 'already_member');
  perform pg_temp.as_user(c);
  select count(*) into n from realtime.messages where event = 'probe';
  if n <> 0 then raise exception 'TEST FAIL: stranger reads the room'; end if;

  -- user: topics, own only and never writable
  perform pg_temp.as_admin();
  perform set_config('realtime.topic', 'user:' || a, true);
  insert into realtime.messages (topic, extension, event, payload, private)
    values ('user:' || a, 'broadcast', 'user_probe', '{}', true);
  perform pg_temp.as_user(c);
  select count(*) into n from realtime.messages where event = 'user_probe';
  if n <> 0 then raise exception 'TEST FAIL: read someone else''s user topic'; end if;
  perform pg_temp.as_user(a);
  select count(*) into n from realtime.messages where event = 'user_probe';
  if n <> 1 then raise exception 'TEST FAIL: cannot read my user topic'; end if;
  perform pg_temp.rejects($q$insert into realtime.messages (topic, extension, event, payload, private) values ('x', 'broadcast', 'e', '{}', true)$q$, 'row-level security');

  -- a real grant reaches the guest's topic, with the token
  perform pg_temp.as_admin();
  select count(*) into n from realtime.messages
    where topic = 'user:' || b and event = 'join_accepted' and payload->>'room_token' = token;
  if n <> 1 then raise exception 'TEST FAIL: join_accepted not sent (%)', n; end if;

  -- kick: out of the room at once
  perform pg_temp.as_user(c);
  perform pg_temp.rejects(format('select public.lobby_kick(%L)', b), 'not_member');
  perform pg_temp.as_user(a);
  perform public.lobby_kick(b);
  perform pg_temp.as_admin();
  perform set_config('realtime.topic', 'room:' || token, true);
  perform pg_temp.as_user(b);
  select count(*) into n from realtime.messages where event = 'probe';
  if n <> 0 then raise exception 'TEST FAIL: kicked guest still reads the room'; end if;

  -- full: host + 3 members rejects the next one, on the count, not on what the client reports
  perform pg_temp.as_admin();
  insert into public.friendships (user_lo, user_hi, status, requested_by)
    select least(a, u), greatest(a, u), 1, a from unnest(array[c, d, e, f]) u;
  insert into public.lobby_members (host_id, user_id) select a, u from unnest(array[c, d, e]) u;
  perform pg_temp.as_user(f);
  if (select l.status from public.friends_list() l where l.user_id = a) <> 'full' then raise exception 'TEST FAIL: not full'; end if;
  perform pg_temp.rejects(format('select public.lobby_join_request(%L)', a), 'lobby_full');

  -- a removed friend cannot ask any more, and is out of the room
  perform pg_temp.as_user(a);
  perform public.friend_remove(c);
  perform pg_temp.as_admin();
  select count(*) into n from public.lobby_members where host_id = a and user_id = c;
  if n <> 0 then raise exception 'TEST FAIL: removed friend still a member'; end if;
  perform pg_temp.as_user(c);
  perform pg_temp.rejects(format('select public.lobby_join_request(%L)', a), 'not_friends');

  -- a new session: new token, old members out
  perform pg_temp.as_user(a);
  perform public.status_heartbeat(0, true, 0, 0, 0, 0);
  v := public.status_heartbeat(1, true, 0, 1, 8, 2000);
  if v = token or v is null then raise exception 'TEST FAIL: token not replaced'; end if;
  perform pg_temp.as_admin();
  select count(*) into n from public.lobby_members where host_id = a;
  if n <> 0 then raise exception 'TEST FAIL: members survived the session'; end if;

  -- anon can do none of it
  reset role;
  perform set_config('request.jwt.claims', '{"role":"anon"}', true);
  set local role anon;
  perform pg_temp.rejects('select public.friends_list()', 'permission denied');
  reset role;

  raise exception 'ALL FRIENDS SQL TESTS PASSED';
end;
$test$;
