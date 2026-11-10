-- WORLD-2: the island-trip shuffle bag per account, so a reinstall keeps the "no repeats" guarantee.
-- Owner-only: read your own row; write through ride_state_set, where the newer state wins.

create table public.ride_state (
  user_id uuid primary key references public.profiles (id) on delete cascade,
  state jsonb not null check (jsonb_typeof(state) = 'object' and pg_column_size(state) < 1024),
  updated_at timestamptz not null default now()
);

alter table public.ride_state enable row level security;
create policy "own ride state" on public.ride_state for select to authenticated using (user_id = (select auth.uid()));
revoke all on public.ride_state from public, anon, authenticated;
grant select on public.ride_state to authenticated;

-- p_at_ms: when the bag changed (device clock, ms); a clock in the future counts as now.
create function public.ride_state_set(p_state jsonb, p_at_ms bigint)
returns text
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_me uuid := public.fr_me();
  v_at timestamptz := least(to_timestamp(p_at_ms / 1000.0), now());
  v_n int;
begin
  if p_state is null or jsonb_typeof(p_state) <> 'object' or pg_column_size(p_state) >= 1024 then
    raise exception 'bad_value';
  end if;
  insert into public.ride_state as r (user_id, state, updated_at) values (v_me, p_state, v_at)
  on conflict (user_id) do update set state = excluded.state, updated_at = excluded.updated_at
    where r.updated_at < excluded.updated_at;
  get diagnostics v_n = row_count;
  return case when v_n > 0 then 'ok' else 'older' end;
end;
$$;

revoke execute on function public.ride_state_set(jsonb, bigint) from public, anon;
grant execute on function public.ride_state_set(jsonb, bigint) to authenticated;

-- ------------------------------------------------------------- account link

create or replace function public.merge_ticket_redeem(p_ticket text)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_e uuid := public.fr_me();
  v_g uuid;
  v_created timestamptz;
  v_used timestamptz;
  v_friends_moved int := 0;
  v_friends_dropped int := 0;
  v_lobby int := 0;
  r record;
  v_other uuid;
begin
  if coalesce(auth.jwt()->>'is_anonymous', 'false') = 'true'
     or (select u.is_anonymous from auth.users u where u.id = v_e) then
    raise exception 'caller_anonymous';
  end if;
  select t.guest_id, t.created_at, t.used_at into v_g, v_created, v_used
    from public.merge_tickets t where t.ticket = p_ticket for update;
  if v_g is null or v_used is not null or v_created < now() - interval '5 minutes' then
    raise exception 'ticket_invalid';
  end if;
  if v_g = v_e then
    raise exception 'same_user';
  end if;
  if not coalesce((select u.is_anonymous from auth.users u where u.id = v_g), false) then
    raise exception 'guest_not_anonymous';
  end if;

  -- public.profiles: E wins entirely; G's row goes with the user below.

  -- public.stats: the counters add up.
  update public.stats e set
    sessions = e.sessions + g.sessions,
    play_seconds = e.play_seconds + g.play_seconds,
    distance_m = e.distance_m + g.distance_m,
    elevator_rides = e.elevator_rides + g.elevator_rides,
    rooms_joined = e.rooms_joined + g.rooms_joined,
    last_seen_at = now()
    from public.stats g
   where e.user_id = v_e and g.user_id = v_g;

  -- public.friendships: G's friends become E's, except a pair E already has and E with itself.
  for r in select f.* from public.friendships f where v_g in (f.user_lo, f.user_hi) for update loop
    v_other := case when r.user_lo = v_g then r.user_hi else r.user_lo end;
    if v_other = v_e or exists (select 1 from public.friendships x
                                 where x.user_lo = least(v_e, v_other) and x.user_hi = greatest(v_e, v_other)) then
      v_friends_dropped := v_friends_dropped + 1;
    else
      insert into public.friendships (user_lo, user_hi, status, requested_by, created_at)
        values (least(v_e, v_other), greatest(v_e, v_other), r.status,
                case when r.requested_by = v_g then v_e else r.requested_by end, r.created_at);
      v_friends_moved := v_friends_moved + 1;
    end if;
  end loop;
  delete from public.friendships f where v_g in (f.user_lo, f.user_hi);

  -- public.lobby_members, public.player_status, public.lobby_join_attempts: session state, G's is gone.
  delete from public.lobby_members m where v_g in (m.host_id, m.user_id);
  get diagnostics v_lobby = row_count;
  update public.player_status s set host_id = null where s.host_id = v_g;
  delete from public.player_status s where s.user_id = v_g;
  delete from public.lobby_join_attempts a where v_g in (a.user_id, a.host_id);
  -- public.lobby_invites: session state too, G's is gone.
  delete from public.lobby_invites i where v_g in (i.host_id, i.guest_id);

  -- public.book_state: per book, the newer choice wins; G's rows go.
  insert into public.book_state (user_id, book_id, last_level, updated_at)
    select v_e, g.book_id, g.last_level, g.updated_at from public.book_state g where g.user_id = v_g
  on conflict (user_id, book_id) do update set last_level = excluded.last_level, updated_at = excluded.updated_at
    where public.book_state.updated_at < excluded.updated_at;
  delete from public.book_state b where b.user_id = v_g;

  -- public.ride_state: the newer shuffle bag wins; G's row goes.
  insert into public.ride_state (user_id, state, updated_at)
    select v_e, g.state, g.updated_at from public.ride_state g where g.user_id = v_g
  on conflict (user_id) do update set state = excluded.state, updated_at = excluded.updated_at
    where public.ride_state.updated_at < excluded.updated_at;
  delete from public.ride_state rs where rs.user_id = v_g;

  update public.merge_tickets t set used_at = now() where t.ticket = p_ticket;
  delete from auth.users u where u.id = v_g; -- cascades to G's profile and stats

  return jsonb_build_object('stats', 1, 'friends_moved', v_friends_moved, 'friends_dropped', v_friends_dropped, 'lobby_rows', v_lobby);
end;
$$;
