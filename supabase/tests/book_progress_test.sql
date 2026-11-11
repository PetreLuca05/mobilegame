-- BOOK-ENGINE SQL tests (book_progress). One DO block that always ends by raising, so everything is rolled back.
--   ALL BOOK PROGRESS SQL TESTS PASSED   every check held
--   TEST FAIL: <what>                    the first check that did not
do $test$
declare
  a uuid := gen_random_uuid();
  b uuid := gen_random_uuid();
  n int; j jsonb; v bytea; t text;
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

  -- the OR helper
  if public.bytea_or('\x0102'::bytea, '\x10'::bytea) <> '\x1102'::bytea then raise exception 'TEST FAIL: bytea_or pads the shorter'; end if;

  -- first write: the row is created as sent
  perform pg_temp.as_user(a);
  j := public.book_progress_merge('gastro', '\x0100'::bytea, '\x01'::bytea, 1000);
  if decode(j->>'facts', 'base64') <> '\x0100'::bytea or decode(j->>'cases', 'base64') <> '\x01'::bytea then
    raise exception 'TEST FAIL: first merge %', j;
  end if;

  -- another device: the fact bits add up; an older case state does not win
  j := public.book_progress_merge('gastro', '\x0002'::bytea, '\x03'::bytea, 500);
  if decode(j->>'facts', 'base64') <> '\x0102'::bytea then raise exception 'TEST FAIL: facts not ORed %', j; end if;
  if decode(j->>'cases', 'base64') <> '\x01'::bytea then raise exception 'TEST FAIL: an older case state won %', j; end if;

  -- a newer case state wins (a case closed later)
  j := public.book_progress_merge('gastro', '\x0000'::bytea, '\x03'::bytea, 2000);
  if decode(j->>'cases', 'base64') <> '\x03'::bytea or decode(j->>'facts', 'base64') <> '\x0102'::bytea then
    raise exception 'TEST FAIL: newer case state %', j;
  end if;

  -- a future clock counts as now
  j := public.book_progress_merge('gastro', '\x0000'::bytea, '\x03'::bytea, 99999999999999);
  if (j->>'updated_ms')::bigint > (extract(epoch from now()) * 1000)::bigint + 1 then raise exception 'TEST FAIL: future clock kept'; end if;

  -- bad values
  perform pg_temp.rejects($q$select public.book_progress_merge('', '\x00'::bytea, '\x00'::bytea, 1)$q$, 'bad_value');
  perform pg_temp.rejects($q$select public.book_progress_merge('gastro', null, '\x00'::bytea, 1)$q$, 'bad_value');
  perform pg_temp.rejects($q$select public.book_progress_merge('gastro', decode(repeat('00', 5000), 'hex'), '\x00'::bytea, 1)$q$, 'bad_value');

  -- owner-only: b sees nothing of a, cannot write a's row directly, and the helper is not callable
  perform pg_temp.as_user(b);
  select count(*) into n from public.book_progress;
  if n <> 0 then raise exception 'TEST FAIL: b sees % rows', n; end if;
  perform pg_temp.rejects($q$insert into public.book_progress (user_id, book, facts, cases) values (gen_random_uuid(), 'x', '\x00', '\x00')$q$, 'permission denied');
  perform pg_temp.rejects($q$select public.bytea_or('\x00'::bytea, '\x00'::bytea)$q$, 'permission denied');
  perform public.book_progress_merge('gastro', '\x8000'::bytea, '\x00'::bytea, 1000);
  perform pg_temp.as_user(a);
  select count(*) into n from public.book_progress;
  if n <> 1 then raise exception 'TEST FAIL: a sees % rows (want 1, its own)', n; end if;
  select facts into v from public.book_progress where user_id = a;
  if v <> '\x0102'::bytea then raise exception 'TEST FAIL: b''s write touched a (%)', v; end if;

  -- Erase progress: a's row goes, b's stays
  t := public.book_progress_reset('gastro');
  if t <> 'ok' then raise exception 'TEST FAIL: reset %', t; end if;
  t := public.book_progress_reset('gastro');
  if t <> 'none' then raise exception 'TEST FAIL: second reset %', t; end if;
  reset role;
  select count(*) into n from public.book_progress where user_id in (a, b);
  if n <> 1 then raise exception 'TEST FAIL: after a''s reset % rows (want b''s only)', n; end if;

  -- the account goes: its row goes with it
  delete from auth.users u where u.id = b;
  select count(*) into n from public.book_progress where user_id = b;
  if n <> 0 then raise exception 'TEST FAIL: a deleted account keeps its book progress'; end if;

  raise exception 'ALL BOOK PROGRESS SQL TESTS PASSED';
end;
$test$;
