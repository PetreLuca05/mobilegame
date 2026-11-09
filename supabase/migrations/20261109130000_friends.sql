-- FRIENDS-1: friends, player status and friend lobbies.
--   profiles.friend_code   6 characters, unique, set by a trigger
--   friendships            one row per pair, written only through RPCs
--   player_status          no client access at all: room_token must never leak
--   lobby_members          who the host let in
--   lobby_join_attempts    rate limit + proof that a guest really asked
-- Pushes go through realtime.send to the private topic user:<uid>.
-- Errors are plain `raise exception '<code>'`; PostgREST returns the code as
-- "message" and the client maps it to a string.

-- ---------------------------------------------------------------- profiles

-- Until now anon and authenticated held every table privilege (RLS was the
-- only guard). The code column must not be client-writable or scrapable.
revoke all on public.profiles from anon, authenticated;

alter table public.profiles add column friend_code text;

create function public.fr_new_code()
returns text
language plpgsql
volatile
security definer
set search_path = ''
as $$
declare
  k_alphabet constant text := '23456789ABCDEFGHJKMNPQRSTUVWXYZ';
  v_code text;
begin
  loop
    select string_agg(substr(k_alphabet, 1 + floor(random() * 31)::int, 1), '') into v_code
      from generate_series(1, 6);
    -- held until commit: a concurrent insert of the same code waits, then sees it
    perform pg_advisory_xact_lock(hashtext('friend_code:' || v_code));
    exit when not exists (select 1 from public.profiles p where p.friend_code = v_code);
  end loop;
  return v_code;
end;
$$;

do $$
declare r record;
begin
  for r in select id from public.profiles where friend_code is null loop
    update public.profiles set friend_code = public.fr_new_code() where id = r.id;
  end loop;
end;
$$;

alter table public.profiles alter column friend_code set not null;
alter table public.profiles add constraint profiles_friend_code_key unique (friend_code);

create function public.profiles_set_friend_code()
returns trigger
language plpgsql
security definer
set search_path = ''
as $$
begin
  new.friend_code := public.fr_new_code();
  return new;
end;
$$;

create trigger profiles_friend_code
  before insert on public.profiles
  for each row execute function public.profiles_set_friend_code();

grant select (id, display_name, skin, created_at, updated_at) on public.profiles to anon, authenticated;
grant update (display_name, skin) on public.profiles to authenticated;

create index profiles_name_prefix on public.profiles (lower(display_name) text_pattern_ops);

-- ------------------------------------------------------------------ tables

create table public.friendships (
  user_lo      uuid not null references public.profiles(id) on delete cascade,
  user_hi      uuid not null references public.profiles(id) on delete cascade,
  status       int2 not null default 0 check (status in (0, 1)), -- 0 pending, 1 accepted
  requested_by uuid not null references public.profiles(id) on delete cascade,
  created_at   timestamptz not null default now(),
  primary key (user_lo, user_hi),
  check (user_lo < user_hi),
  check (requested_by in (user_lo, user_hi))
);
create index friendships_hi on public.friendships (user_hi);
create index friendships_requested_by on public.friendships (requested_by);

create table public.player_status (
  user_id    uuid primary key references public.profiles(id) on delete cascade,
  state      int2 not null default 0 check (state in (0, 1)), -- 0 menu, 1 in game
  lobby_open boolean not null default true,
  room_token text,
  map_id     int2 not null default 0,
  players    int2 not null default 0,
  host_id    uuid references public.profiles(id) on delete set null,
  npc_seed   int4 not null default 0,
  start_tick int8 not null default 0,
  updated_at timestamptz not null default now()
);
create index player_status_token on public.player_status (room_token);
create index player_status_host on public.player_status (host_id);

create table public.lobby_members (
  host_id    uuid not null references public.profiles(id) on delete cascade,
  user_id    uuid not null references public.profiles(id) on delete cascade,
  granted_at timestamptz not null default now(),
  primary key (host_id, user_id)
);
create index lobby_members_user on public.lobby_members (user_id);

create table public.lobby_join_attempts (
  user_id  uuid not null references public.profiles(id) on delete cascade,
  host_id  uuid not null references public.profiles(id) on delete cascade,
  last_at  timestamptz not null default now(),
  answered boolean not null default false,
  primary key (user_id, host_id)
);
create index lobby_join_attempts_host on public.lobby_join_attempts (host_id);

alter table public.friendships enable row level security;
alter table public.player_status enable row level security;
alter table public.lobby_members enable row level security;
alter table public.lobby_join_attempts enable row level security;

revoke all on public.friendships, public.player_status, public.lobby_members, public.lobby_join_attempts
  from public, anon, authenticated;

create policy "own friendships" on public.friendships for select
  to authenticated
  using ((select auth.uid()) in (user_lo, user_hi));
grant select on public.friendships to authenticated;
-- player_status, lobby_members, lobby_join_attempts: no policy, RPCs only

-- ----------------------------------------------------------------- helpers

create function public.fr_me()
returns uuid
language plpgsql
stable
set search_path = ''
as $$
declare v_me uuid := auth.uid();
begin
  if v_me is null then
    raise exception 'not_authenticated';
  end if;
  return v_me;
end;
$$;

create function public.fr_push(p_to uuid, p_event text, p_payload jsonb)
returns void
language sql
security definer
set search_path = ''
as $$
  select realtime.send(p_payload, p_event, 'user:' || p_to::text, true);
$$;

create function public.fr_name(p_id uuid)
returns text
language sql
stable
security definer
set search_path = ''
as $$
  select display_name from public.profiles where id = p_id;
$$;

create function public.fr_are_friends(p_a uuid, p_b uuid)
returns boolean
language sql
stable
security definer
set search_path = ''
as $$
  select exists (select 1 from public.friendships f
                  where f.user_lo = least(p_a, p_b) and f.user_hi = greatest(p_a, p_b) and f.status = 1);
$$;

create function public.fr_friend_count(p_user uuid)
returns int
language sql
stable
security definer
set search_path = ''
as $$
  select count(*)::int from public.friendships f where f.status = 1 and p_user in (f.user_lo, f.user_hi);
$$;

-- A member's way out of the host's room: row gone, and the status stops saying "in another lobby".
create function public.fr_drop_member(p_host uuid, p_guest uuid)
returns boolean
language plpgsql
security definer
set search_path = ''
as $$
begin
  delete from public.lobby_members m where m.host_id = p_host and m.user_id = p_guest;
  if not found then
    return false;
  end if;
  update public.player_status s set host_id = null where s.user_id = p_guest and s.host_id = p_host;
  return true;
end;
$$;

-- Every member out at once (the host's session ended or started anew)
create function public.fr_close_room(p_host uuid)
returns void
language sql
security definer
set search_path = ''
as $$
  update public.player_status s set host_id = null where s.host_id = p_host;
  delete from public.lobby_members m where m.host_id = p_host;
$$;

revoke execute on function public.fr_new_code(), public.profiles_set_friend_code(), public.fr_me(),
  public.fr_push(uuid, text, jsonb), public.fr_name(uuid), public.fr_are_friends(uuid, uuid),
  public.fr_friend_count(uuid), public.fr_drop_member(uuid, uuid), public.fr_close_room(uuid)
  from public, anon, authenticated;

-- ----------------------------------------------------------------- friends

create function public.my_friend_code()
returns text
language sql
stable
security definer
set search_path = ''
as $$
  select p.friend_code from public.profiles p where p.id = auth.uid();
$$;

create function public.friend_respond(p_other uuid, p_accept boolean)
returns void
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_me uuid := public.fr_me();
begin
  perform 1 from public.friendships f
    where f.user_lo = least(v_me, p_other) and f.user_hi = greatest(v_me, p_other)
      and f.status = 0 and f.requested_by = p_other
    for update;
  if not found then
    raise exception 'no_request';
  end if;
  if not p_accept then
    delete from public.friendships f where f.user_lo = least(v_me, p_other) and f.user_hi = greatest(v_me, p_other);
    return;
  end if;
  if public.fr_friend_count(v_me) >= 100 or public.fr_friend_count(p_other) >= 100 then
    raise exception 'limit_friends';
  end if;
  update public.friendships f set status = 1
    where f.user_lo = least(v_me, p_other) and f.user_hi = greatest(v_me, p_other);
  perform public.fr_push(p_other, 'friend_accepted', jsonb_build_object('from', v_me, 'name', public.fr_name(v_me)));
end;
$$;

-- Returns 'requested', or 'accepted' when the other side had already asked.
create function public.friend_request_by_code(p_code text)
returns text
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_me uuid := public.fr_me();
  v_other uuid;
  v_row public.friendships;
begin
  select p.id into v_other from public.profiles p where p.friend_code = upper(btrim(p_code));
  if v_other is null then
    raise exception 'code_not_found';
  end if;
  if v_other = v_me then
    raise exception 'self';
  end if;
  select * into v_row from public.friendships f
    where f.user_lo = least(v_me, v_other) and f.user_hi = greatest(v_me, v_other);
  if found then
    if v_row.status = 1 then
      raise exception 'already_friends';
    elsif v_row.requested_by = v_me then
      raise exception 'already_pending';
    end if;
    perform public.friend_respond(v_other, true);
    return 'accepted';
  end if;
  if public.fr_friend_count(v_me) >= 100 then
    raise exception 'limit_friends';
  end if;
  if (select count(*) from public.friendships f where f.requested_by = v_me and f.status = 0) >= 20 then
    raise exception 'limit_outgoing';
  end if;
  insert into public.friendships (user_lo, user_hi, status, requested_by)
    values (least(v_me, v_other), greatest(v_me, v_other), 0, v_me);
  perform public.fr_push(v_other, 'friend_request', jsonb_build_object('from', v_me, 'name', public.fr_name(v_me)));
  return 'requested';
end;
$$;

create function public.friend_search(p_name_prefix text)
returns table (display_name text, friend_code text, skin jsonb)
language plpgsql
stable
security definer
set search_path = ''
as $$
declare
  v_me uuid := public.fr_me();
  v_prefix text := lower(left(btrim(p_name_prefix), 16));
begin
  if char_length(v_prefix) < 2 then
    return;
  end if;
  v_prefix := replace(replace(replace(v_prefix, '\', '\\'), '%', '\%'), '_', '\_');
  return query
    select p.display_name, p.friend_code, p.skin
      from public.profiles p
     where lower(p.display_name) like v_prefix || '%' and p.id <> v_me
     order by lower(p.display_name), p.friend_code
     limit 10;
end;
$$;

-- Also how an outgoing request is cancelled.
create function public.friend_remove(p_other uuid)
returns void
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_me uuid := public.fr_me();
begin
  delete from public.friendships f where f.user_lo = least(v_me, p_other) and f.user_hi = greatest(v_me, p_other);
  if public.fr_drop_member(v_me, p_other) then
    perform public.fr_push(p_other, 'kicked', jsonb_build_object('host', v_me));
  end if;
  if public.fr_drop_member(p_other, v_me) then
    perform public.fr_push(v_me, 'kicked', jsonb_build_object('host', p_other));
  end if;
end;
$$;

create function public.friends_list()
returns table (user_id uuid, display_name text, skin jsonb, friend_code text, relation text, status text, players int)
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
               when m.relation <> 'friend' or s.user_id is null or s.updated_at < now() - interval '90 seconds' then 'offline'
               when s.state = 0 then 'menu'
               when s.host_id is not null then 'in_other_lobby'
               when not s.lobby_open then 'locked'
               when s.players >= 4 or (select count(*) from public.lobby_members lm where lm.host_id = m.other) >= 3 then 'full'
               else 'open'
             end as status,
             s.players as players
        from mine m
        left join public.player_status s on s.user_id = m.other
    )
    select p.id, p.display_name, p.skin, p.friend_code, v.relation, v.status,
           case when v.status in ('open', 'locked', 'full', 'in_other_lobby') then v.players::int else 0 end
      from seen v
      join public.profiles p on p.id = v.other
     order by (v.relation = 'friend') desc, v.created_at desc
     limit 120;
end;
$$;

-- ------------------------------------------------------------------ status

-- Returns the caller's room token (null when state = 0). A token lives for a
-- session: it is replaced when a session starts, and a status_clear shorter
-- than two minutes (backgrounding) does not end the session.
create function public.status_heartbeat(p_state int, p_lobby_open boolean, p_map_id int, p_players int, p_npc_seed int, p_start_tick bigint)
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
  if p_state not in (0, 1) then
    raise exception 'bad_state';
  end if;
  select * into v_row from public.player_status s where s.user_id = v_me for update;
  v_new_session := p_state = 1 and (v_row.user_id is null or v_row.room_token is null or v_row.state = 0
                                    or v_row.updated_at < now() - interval '2 minutes');
  if p_state = 0 or v_new_session then
    perform public.fr_close_room(v_me);
  end if;
  if v_new_session then
    v_token := substr(md5(gen_random_uuid()::text || gen_random_uuid()::text), 1, 16);
  elsif p_state = 1 then
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

-- Exit / background: friends see us offline at once (older than the 90 s rule).
create function public.status_clear()
returns void
language sql
security definer
set search_path = ''
as $$
  update public.player_status s set updated_at = now() - interval '91 seconds' where s.user_id = auth.uid();
$$;

-- ----------------------------------------------------------------- lobbies

create function public.lobby_join_request(p_host uuid)
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
  if v_host.user_id is null or v_host.updated_at < now() - interval '90 seconds' or v_host.state <> 1 then
    raise exception 'host_offline';
  end if;
  if v_host.host_id is not null then
    raise exception 'host_in_other_lobby';
  end if;
  if not v_host.lobby_open then
    raise exception 'lobby_locked';
  end if;
  if v_host.players >= 4 or (select count(*) from public.lobby_members m where m.host_id = p_host) >= 3 then
    raise exception 'lobby_full';
  end if;
  if exists (select 1 from public.lobby_members m where m.host_id = p_host and m.user_id = v_me) then
    raise exception 'already_member';
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

create function public.lobby_join_respond(p_guest uuid, p_accept boolean)
returns void
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_me uuid := public.fr_me();
  v_host public.player_status;
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
  select * into v_host from public.player_status s where s.user_id = v_me for update;
  if v_host.user_id is null or v_host.state <> 1 or v_host.room_token is null then
    raise exception 'host_offline';
  end if;
  if (select count(*) from public.lobby_members m where m.host_id = v_me) >= 3 then
    raise exception 'lobby_full';
  end if;
  insert into public.lobby_members (host_id, user_id) values (v_me, p_guest) on conflict do nothing;
  insert into public.player_status as s (user_id, host_id) values (p_guest, v_me)
    on conflict (user_id) do update set host_id = excluded.host_id;
  perform public.fr_push(p_guest, 'join_accepted', jsonb_build_object(
    'room_token', v_host.room_token, 'map_id', v_host.map_id, 'npc_seed', v_host.npc_seed,
    'start_tick', v_host.start_tick, 'host', v_me));
end;
$$;

create function public.lobby_kick(p_guest uuid)
returns void
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_me uuid := public.fr_me();
begin
  if not public.fr_drop_member(v_me, p_guest) then
    raise exception 'not_member';
  end if;
  perform public.fr_push(p_guest, 'kicked', jsonb_build_object('host', v_me));
end;
$$;

create function public.lobby_leave()
returns void
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_me uuid := public.fr_me();
  r record;
begin
  for r in select m.host_id from public.lobby_members m where m.user_id = v_me loop
    perform public.fr_drop_member(r.host_id, v_me);
  end loop;
  update public.player_status s set host_id = null where s.user_id = v_me;
end;
$$;

revoke execute on function public.my_friend_code(), public.friend_respond(uuid, boolean),
  public.friend_request_by_code(text), public.friend_search(text), public.friend_remove(uuid),
  public.friends_list(), public.status_heartbeat(int, boolean, int, int, int, bigint), public.status_clear(),
  public.lobby_join_request(uuid), public.lobby_join_respond(uuid, boolean), public.lobby_kick(uuid), public.lobby_leave()
  from public, anon;
grant execute on function public.my_friend_code(), public.friend_respond(uuid, boolean),
  public.friend_request_by_code(text), public.friend_search(text), public.friend_remove(uuid),
  public.friends_list(), public.status_heartbeat(int, boolean, int, int, int, bigint), public.status_clear(),
  public.lobby_join_request(uuid), public.lobby_join_respond(uuid, boolean), public.lobby_kick(uuid), public.lobby_leave()
  to authenticated;

-- ---------------------------------------------------------------- Realtime

-- Called from the policies, so as the joining user; it can see player_status only because it is a definer.
create function public.realtime_room_allowed(p_topic text)
returns boolean
language sql
stable
security definer
set search_path = ''
as $$
  select exists (
    select 1 from public.player_status s
     where s.room_token is not null and 'room:' || s.room_token = p_topic
       and (s.user_id = auth.uid()
            or exists (select 1 from public.lobby_members m where m.host_id = s.user_id and m.user_id = auth.uid())));
$$;
revoke execute on function public.realtime_room_allowed(text) from public, anon;
grant execute on function public.realtime_room_allowed(text) to authenticated;

-- The room:ABCD / world:01 policies from 0002 stay until the old multiplayer is
-- removed, so installed builds keep working; a later migration drops them. They
-- cannot open these rooms: a token is hex, 16 characters, and matches neither.

create policy "own user topic and lobby rooms can receive"
on realtime.messages
for select
to authenticated
using (
  realtime.messages.extension in ('broadcast', 'presence')
  and (
    (select realtime.topic()) = 'user:' || (select auth.uid())::text
    or ((select realtime.topic()) ~ '^room:[0-9a-f]{16}$' and (select public.realtime_room_allowed((select realtime.topic()))))
  )
);

create policy "lobby rooms can send"
on realtime.messages
for insert
to authenticated
with check (
  realtime.messages.extension in ('broadcast', 'presence')
  and (select realtime.topic()) ~ '^room:[0-9a-f]{16}$'
  and (select public.realtime_room_allowed((select realtime.topic())))
);
