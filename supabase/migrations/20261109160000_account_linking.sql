-- LINK-1: merge a guest into the existing account its Google / Apple identity belongs to.
-- The client never moves rows: it proves it is the guest (merge_ticket_create), signs in to the
-- existing account and hands over the one-time ticket (merge_ticket_redeem).

create table public.merge_tickets (
  ticket     text primary key,
  guest_id   uuid not null,   -- no foreign key: the row outlives the guest it merged
  created_at timestamptz not null default now(),
  used_at    timestamptz
);
create index merge_tickets_guest on public.merge_tickets (guest_id);
alter table public.merge_tickets enable row level security;
revoke all on public.merge_tickets from public, anon, authenticated;

create function public.merge_ticket_create()
returns text
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_me uuid := public.fr_me();
  v_ticket text;
begin
  if coalesce(auth.jwt()->>'is_anonymous', 'false') <> 'true' then
    raise exception 'not_anonymous';
  end if;
  delete from public.merge_tickets t where t.guest_id = v_me and t.used_at is null; -- one live ticket per guest
  v_ticket := md5(gen_random_uuid()::text || gen_random_uuid()::text);
  insert into public.merge_tickets (ticket, guest_id) values (v_ticket, v_me);
  return v_ticket;
end;
$$;

-- E = the caller (existing account), G = the guest. Every table with a user foreign key is named
-- here; supabase/tests/link_test.sql fails if a new one is not.
create function public.merge_ticket_redeem(p_ticket text)
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

  update public.merge_tickets t set used_at = now() where t.ticket = p_ticket;
  delete from auth.users u where u.id = v_g; -- cascades to G's profile and stats

  return jsonb_build_object('stats', 1, 'friends_moved', v_friends_moved, 'friends_dropped', v_friends_dropped, 'lobby_rows', v_lobby);
end;
$$;

revoke execute on function public.merge_ticket_create(), public.merge_ticket_redeem(text) from public, anon;
grant execute on function public.merge_ticket_create(), public.merge_ticket_redeem(text) to authenticated;

-- Nightly: guests unseen for 180 days (Supabase never removes them), and old tickets.
create function public.link_cleanup()
returns void
language sql
security definer
set search_path = ''
as $$
  delete from auth.users u using public.stats s
   where s.user_id = u.id and u.is_anonymous and s.last_seen_at < now() - interval '180 days';
  delete from public.merge_tickets t where t.created_at < now() - interval '5 minutes' and t.used_at is null;
  delete from public.merge_tickets t where t.used_at < now() - interval '30 days';
$$;
revoke execute on function public.link_cleanup() from public, anon, authenticated;

create extension if not exists pg_cron;
select cron.schedule('link-cleanup', '17 3 * * *', 'select public.link_cleanup()');
