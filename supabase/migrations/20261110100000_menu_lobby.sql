-- FRIENDS-2: the main menu is the lobby.
--   player_status.state  0 offline/none, 1 menu lobby, 2 in game (was 0 menu, 1 game)
--   lobby_invites        host-initiated, valid 60 s, a pre-approval: the invited friend gets in without the
--                        host's Accept panel and even when the lobby is locked
--   RPCs                 lobby_invite, invite_respond, lobby_return_to_menu; lobby_join_request honours an invite

alter table public.player_status drop constraint player_status_state_check;
update public.player_status set state = state + 1;
alter table public.player_status add constraint player_status_state_check check (state in (0, 1, 2));

create table public.lobby_invites (
  host_id    uuid not null references public.profiles(id) on delete cascade,
  guest_id   uuid not null references public.profiles(id) on delete cascade,
  created_at timestamptz not null default now(),
  expires_at timestamptz not null,
  primary key (host_id, guest_id)
);
create index lobby_invites_guest on public.lobby_invites (guest_id);
alter table public.lobby_invites enable row level security;
revoke all on public.lobby_invites from anon, authenticated;

-- ------------------------------------------------------------------ helpers

-- Lets a guest in: the same for an accepted request and for an invite.
create function public.fr_admit(p_host uuid, p_guest uuid)
returns void
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_host public.player_status;
begin
  select * into v_host from public.player_status s where s.user_id = p_host for update;
  if v_host.user_id is null or v_host.state not in (1, 2) or v_host.room_token is null then
    raise exception 'host_offline';
  end if;
  if (select count(*) from public.lobby_members m where m.host_id = p_host) >= 3 then
    raise exception 'lobby_full';
  end if;
  insert into public.lobby_members (host_id, user_id) values (p_host, p_guest) on conflict do nothing;
  insert into public.player_status as s (user_id, host_id) values (p_guest, p_host)
    on conflict (user_id) do update set host_id = excluded.host_id;
  perform public.fr_push(p_guest, 'join_accepted', jsonb_build_object(
    'room_token', v_host.room_token, 'map_id', v_host.map_id, 'npc_seed', v_host.npc_seed,
    'start_tick', v_host.start_tick, 'host', p_host, 'in_menu', v_host.state = 1));
end;
$$;
revoke execute on function public.fr_admit(uuid, uuid) from public, anon, authenticated;

create or replace function public.fr_close_room(p_host uuid)
returns void
language sql
security definer
set search_path = ''
as $$
  update public.player_status s set host_id = null where s.host_id = p_host;
  delete from public.lobby_members m where m.host_id = p_host;
  delete from public.lobby_invites i where i.host_id = p_host;
$$;

-- ------------------------------------------------------------------ status

drop function public.friends_list();
create function public.friends_list()
returns table (user_id uuid, display_name text, skin jsonb, friend_code text, relation text, status text, players int, in_menu boolean)
language plpgsql
stable
security definer
set search_path = ''
as $$
declare
  v_me uuid := public.fr_me();
begin
  return query
    with mine as (
      select case when f.user_lo = v_me then f.user_hi else f.user_lo end as other,
             case when f.status = 1 then 'friend' when f.requested_by = v_me then 'outgoing' else 'incoming' end as relation,
             f.created_at
        from public.friendships f
       where v_me in (f.user_lo, f.user_hi)
    ), seen as (
      select m.other, m.relation, m.created_at,
             case
               when m.relation <> 'friend' or s.user_id is null or s.state = 0 or s.updated_at < now() - interval '90 seconds' then 'offline'
               when s.host_id is not null then 'in_other_lobby'
               when not s.lobby_open then 'locked'
               when s.players >= 4 or (select count(*) from public.lobby_members lm where lm.host_id = m.other) >= 3 then 'full'
               else 'open'
             end as status,
             s.players as players, s.state as state
        from mine m
        left join public.player_status s on s.user_id = m.other
    )
    select p.id, p.display_name, p.skin, p.friend_code, v.relation, v.status,
           case when v.status <> 'offline' then v.players::int else 0 end,
           v.status <> 'offline' and v.state = 1
      from seen v
      join public.profiles p on p.id = v.other
     order by (v.relation = 'friend') desc, v.created_at desc
     limit 120;
end;
$$;
revoke execute on function public.friends_list() from public, anon;
grant execute on function public.friends_list() to authenticated;

-- A token lives from the menu through the game; it is replaced only when a session starts anew.
create or replace function public.status_heartbeat(p_state int, p_lobby_open boolean, p_map_id int, p_players int, p_npc_seed int, p_start_tick bigint)
returns text
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_me uuid := public.fr_me();
  v_row public.player_status;
  v_token text;
  v_new_session boolean;
begin
  if p_state not in (0, 1, 2) then
    raise exception 'bad_state';
  end if;
  select * into v_row from public.player_status s where s.user_id = v_me for update;
  v_new_session := p_state <> 0 and (v_row.user_id is null or v_row.room_token is null or v_row.state = 0
                                     or v_row.updated_at < now() - interval '2 minutes');
  if p_state = 0 or v_new_session then
    perform public.fr_close_room(v_me);
  end if;
  if v_new_session then
    v_token := substr(md5(gen_random_uuid()::text || gen_random_uuid()::text), 1, 16);
  elsif p_state <> 0 then
    v_token := v_row.room_token;
  end if;
  insert into public.player_status as s (user_id, state, lobby_open, room_token, map_id, players, npc_seed, start_tick, updated_at)
    values (v_me, p_state, p_lobby_open, v_token, p_map_id, greatest(least(p_players, 4), 0), p_npc_seed, p_start_tick, now())
    on conflict (user_id) do update
      set state = excluded.state, lobby_open = excluded.lobby_open, room_token = excluded.room_token,
          map_id = excluded.map_id, players = excluded.players, npc_seed = excluded.npc_seed,
          start_tick = excluded.start_tick, updated_at = excluded.updated_at,
          -- only a guest still let in somewhere stays "in another lobby"
          host_id = case when exists (select 1 from public.lobby_members m where m.host_id = s.host_id and m.user_id = v_me)
                         then s.host_id end;
  return v_token;
end;
$$;

-- ------------------------------------------------------------------ lobbies

create or replace function public.lobby_join_request(p_host uuid)
returns void
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_me uuid := public.fr_me();
  v_host public.player_status;
  v_last timestamptz;
begin
  if not public.fr_are_friends(v_me, p_host) then
    raise exception 'not_friends';
  end if;
  select * into v_host from public.player_status s where s.user_id = p_host;
  if v_host.user_id is null or v_host.updated_at < now() - interval '90 seconds' or v_host.state not in (1, 2) then
    raise exception 'host_offline';
  end if;
  if v_host.host_id is not null then
    raise exception 'host_in_other_lobby';
  end if;
  if exists (select 1 from public.lobby_members m where m.host_id = p_host and m.user_id = v_me) then
    raise exception 'already_member';
  end if;
  -- invited: straight in, whatever the switch says
  if exists (select 1 from public.lobby_invites i where i.host_id = p_host and i.guest_id = v_me and i.expires_at > now()) then
    perform public.fr_admit(p_host, v_me);
    delete from public.lobby_invites i where i.host_id = p_host and i.guest_id = v_me;
    perform public.fr_push(p_host, 'member_joined',
      (select jsonb_build_object('user', v_me, 'name', p.display_name, 'skin', p.skin) from public.profiles p where p.id = v_me));
    return;
  end if;
  if not v_host.lobby_open then
    raise exception 'lobby_locked';
  end if;
  if v_host.players >= 4 or (select count(*) from public.lobby_members m where m.host_id = p_host) >= 3 then
    raise exception 'lobby_full';
  end if;
  select a.last_at into v_last from public.lobby_join_attempts a
    where a.user_id = v_me and a.host_id = p_host for update;
  if v_last is not null and v_last > now() - interval '10 seconds' then
    raise exception 'rate_limited';
  end if;
  insert into public.lobby_join_attempts as a (user_id, host_id, last_at, answered)
    values (v_me, p_host, now(), false)
    on conflict (user_id, host_id) do update set last_at = excluded.last_at, answered = false;
  perform public.fr_push(p_host, 'join_request',
    (select jsonb_build_object('from', v_me, 'name', p.display_name, 'skin', p.skin) from public.profiles p where p.id = v_me));
end;
$$;

create or replace function public.lobby_join_respond(p_guest uuid, p_accept boolean)
returns void
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_me uuid := public.fr_me();
begin
  update public.lobby_join_attempts a set answered = true
    where a.user_id = p_guest and a.host_id = v_me and not a.answered and a.last_at > now() - interval '60 seconds';
  if not found then
    raise exception 'no_request';
  end if;
  if not p_accept then
    perform public.fr_push(p_guest, 'join_declined', jsonb_build_object('host', v_me));
    return;
  end if;
  if not public.fr_are_friends(v_me, p_guest) then
    raise exception 'not_friends';
  end if;
  perform public.fr_admit(v_me, p_guest);
end;
$$;

-- Returns 'ok' or what the UI shows: 'full', 'busy', 'offline', 'rate_limited'. A caller who is no host raises.
create function public.lobby_invite(p_guest uuid)
returns text
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_me uuid := public.fr_me();
  v_host public.player_status;
  v_guest public.player_status;
  v_last timestamptz;
begin
  if not public.fr_are_friends(v_me, p_guest) then
    raise exception 'not_friends';
  end if;
  select * into v_host from public.player_status s where s.user_id = v_me;
  if v_host.user_id is null or v_host.state not in (1, 2) or v_host.room_token is null or v_host.host_id is not null then
    raise exception 'not_host';
  end if;
  if v_host.players >= 4 or (select count(*) from public.lobby_members m where m.host_id = v_me) >= 3 then
    return 'full';
  end if;
  select * into v_guest from public.player_status s where s.user_id = p_guest;
  if v_guest.user_id is null or v_guest.state = 0 or v_guest.updated_at < now() - interval '90 seconds' then
    return 'offline';
  end if;
  if v_guest.state <> 1 or v_guest.host_id is not null or v_guest.players > 1 then
    return 'busy';
  end if;
  select i.created_at into v_last from public.lobby_invites i
    where i.host_id = v_me and i.guest_id = p_guest for update;
  if v_last is not null and v_last > now() - interval '10 seconds' then
    return 'rate_limited';
  end if;
  insert into public.lobby_invites as i (host_id, guest_id, created_at, expires_at)
    values (v_me, p_guest, now(), now() + interval '60 seconds')
    on conflict (host_id, guest_id) do update set created_at = excluded.created_at, expires_at = excluded.expires_at;
  perform public.fr_push(p_guest, 'lobby_invite',
    (select jsonb_build_object('from', v_me, 'name', p.display_name, 'skin', p.skin, 'players', v_host.players)
       from public.profiles p where p.id = v_me));
  return 'ok';
end;
$$;

-- The invited friend's answer. Accepting is the invited path of lobby_join_request; declining tells the host.
create function public.invite_respond(p_host uuid, p_accept boolean)
returns void
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_me uuid := public.fr_me();
begin
  if not exists (select 1 from public.lobby_invites i where i.host_id = p_host and i.guest_id = v_me and i.expires_at > now()) then
    raise exception 'invite_expired';
  end if;
  if p_accept then
    perform public.lobby_join_request(p_host);
    return;
  end if;
  delete from public.lobby_invites i where i.host_id = p_host and i.guest_id = v_me;
  perform public.fr_push(p_host, 'invite_declined',
    (select jsonb_build_object('from', v_me, 'name', p.display_name) from public.profiles p where p.id = v_me));
end;
$$;

-- "Save & back to menu": the host and everyone with them are back in the menu lobby.
create function public.lobby_return_to_menu()
returns void
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_me uuid := public.fr_me();
  v_host public.player_status;
begin
  select * into v_host from public.player_status s where s.user_id = v_me for update;
  if v_host.user_id is null or v_host.state not in (1, 2) or v_host.room_token is null or v_host.host_id is not null then
    raise exception 'not_host';
  end if;
  update public.player_status s set state = 1, updated_at = now()
   where s.user_id = v_me or s.user_id in (select m.user_id from public.lobby_members m where m.host_id = v_me);
end;
$$;

revoke execute on function public.lobby_invite(uuid), public.invite_respond(uuid, boolean), public.lobby_return_to_menu() from public, anon;
grant execute on function public.lobby_invite(uuid), public.invite_respond(uuid, boolean), public.lobby_return_to_menu() to authenticated;

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

  update public.merge_tickets t set used_at = now() where t.ticket = p_ticket;
  delete from auth.users u where u.id = v_g; -- cascades to G's profile and stats

  return jsonb_build_object('stats', 1, 'friends_moved', v_friends_moved, 'friends_dropped', v_friends_dropped, 'lobby_rows', v_lobby);
end;
$$;


-- ------------------------------------------------------------------ cleanup

create or replace function public.link_cleanup()
returns void
language sql
security definer
set search_path = ''
as $$
  delete from auth.users u using public.stats s
   where s.user_id = u.id and u.is_anonymous and s.last_seen_at < now() - interval '180 days';
  delete from public.merge_tickets t where t.created_at < now() - interval '5 minutes' and t.used_at is null;
  delete from public.merge_tickets t where t.used_at < now() - interval '30 days';
  delete from public.lobby_invites i where i.expires_at < now();
$$;
