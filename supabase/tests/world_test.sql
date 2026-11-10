-- WORLD-1 SQL tests (book_state). One DO block that always ends by raising, so everything is rolled back.
--   ALL WORLD SQL TESTS PASSED   every check held
--   TEST FAIL: <what>            the first check that did not
do $test$
declare
  a uuid := gen_random_uuid();
  b uuid := gen_random_uuid();
  n int; v text;
begin
  create function pg_temp.as_user(u uuid) returns void language plpgsql as $f$
  begin
    reset role;
    perform set_config('request.jwt.claims', json_build_object('sub', u, 'role', 'authenticated', 'is_anonymous', true)::text, true);
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

  insert into auth.users (id, aud, role, is_anonymous) values
    (a, 'authenticated', 'authenticated', true), (b, 'authenticated', 'authenticated', true);

  -- writes only through the RPC
  perform pg_temp.as_user(a);
  perform pg_temp.rejects(format('insert into public.book_state (user_id, book_id, last_level) values (%L, %L, %L)', a, 'kumar', 'gastro'), 'permission denied');
  v := public.book_state_set('kumar', 'gastro', (extract(epoch from now()) * 1000)::bigint - 60000);
  if v <> 'ok' then raise exception 'TEST FAIL: first set %', v; end if;
  perform pg_temp.rejects(format('update public.book_state set last_level = %L', 'neuro'), 'permission denied');
  perform pg_temp.rejects(format('delete from public.book_state'), 'permission denied');

  -- newest wins: an older choice (an offline device) does not overwrite
  v := public.book_state_set('kumar', 'cardio', (extract(epoch from now()) * 1000)::bigint - 120000);
  if v <> 'older' then raise exception 'TEST FAIL: older set %', v; end if;
  v := public.book_state_set('kumar', 'hepato', (extract(epoch from now()) * 1000)::bigint - 1000);
  if v <> 'ok' then raise exception 'TEST FAIL: newer set %', v; end if;
  if (select last_level from public.book_state where user_id = a and book_id = 'kumar') <> 'hepato' then raise exception 'TEST FAIL: not the newest'; end if;
  -- a clock in the future is clamped to now, so it cannot lock the row
  v := public.book_state_set('kumar', 'neuro', (extract(epoch from now()) * 1000)::bigint + 86400000);
  if (select updated_at from public.book_state where user_id = a and book_id = 'kumar') > now() then raise exception 'TEST FAIL: future timestamp kept'; end if;
  perform pg_temp.rejects(format('select public.book_state_set(%L, %L, 0)', 'Kumar!', 'gastro'), 'bad_value');
  perform pg_temp.rejects(format('select public.book_state_set(%L, %L, 0)', 'kumar', ''), 'bad_value');

  -- owner only: B sees nothing of A's, and cannot write A's row
  perform pg_temp.as_user(b);
  select count(*) into n from public.book_state;
  if n <> 0 then raise exception 'TEST FAIL: B sees % rows', n; end if;
  v := public.book_state_set('kumar', 'pneumo', (extract(epoch from now()) * 1000)::bigint);
  select count(*) into n from public.book_state;
  if n <> 1 then raise exception 'TEST FAIL: B sees % own rows', n; end if;
  reset role;
  if (select count(*) from public.book_state where user_id = a) <> 1 or (select last_level from public.book_state where user_id = a) <> 'neuro' then
    raise exception 'TEST FAIL: A row changed by B';
  end if;

  -- anon: nothing
  reset role;
  perform set_config('request.jwt.claims', '{"role":"anon"}', true);
  set local role anon;
  perform pg_temp.rejects('select * from public.book_state', 'permission denied');
  perform pg_temp.rejects(format('select public.book_state_set(%L, %L, 0)', 'kumar', 'gastro'), 'permission denied');
  reset role;

  raise exception 'ALL WORLD SQL TESTS PASSED';
end;
$test$;
