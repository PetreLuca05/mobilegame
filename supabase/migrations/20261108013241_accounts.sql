-- M2: persistent accounts. Every auth user (anonymous guests included) gets
-- one profile row and one stats row from a trigger on sign-up. Linking Google
-- to a guest keeps the same user id, so name, skin and stats carry over.
--   profiles: name + skin, readable by everyone (other players draw them),
--             writable only by the owner
--   stats:    owner-only; the client accumulates locally and PATCHes the
--             totals about once a minute

create table public.profiles (
  id           uuid primary key references auth.users(id) on delete cascade,
  display_name text not null default 'Player' check (char_length(display_name) between 2 and 16),
  -- default skin: doctor palette, scrub cap, surgical mask (see game/avatar.c)
  skin         jsonb not null default '{"palette":0,"hair":1,"accessory":3}',
  created_at   timestamptz not null default now(),
  updated_at   timestamptz not null default now()
);

create table public.stats (
  user_id        uuid primary key references public.profiles(id) on delete cascade,
  sessions       int  not null default 0,
  play_seconds   int  not null default 0,
  distance_m     real not null default 0,
  elevator_rides int  not null default 0,
  rooms_joined   int  not null default 0,
  last_seen_at   timestamptz not null default now()
);

alter table public.profiles enable row level security;
alter table public.stats    enable row level security;

create policy "own profile" on public.profiles for all
  to authenticated
  using ((select auth.uid()) = id) with check ((select auth.uid()) = id);
create policy "read profiles" on public.profiles for select
  using (true);
create policy "own stats" on public.stats for all
  to authenticated
  using ((select auth.uid()) = user_id) with check ((select auth.uid()) = user_id);

grant select on public.profiles to anon, authenticated;
grant update on public.profiles to authenticated;
grant select, update on public.stats to authenticated;

-- one profile + stats row per new user
create function public.handle_new_user()
returns trigger
language plpgsql
security definer
set search_path = ''
as $$
begin
  insert into public.profiles (id) values (new.id) on conflict do nothing;
  insert into public.stats (user_id) values (new.id) on conflict do nothing;
  return new;
end;
$$;

create trigger on_auth_user_created
  after insert on auth.users
  for each row execute function public.handle_new_user();

create function public.touch_updated_at()
returns trigger
language plpgsql
set search_path = ''
as $$
begin
  new.updated_at = now();
  return new;
end;
$$;

create trigger profiles_updated_at
  before update on public.profiles
  for each row execute function public.touch_updated_at();

-- A reinstalled app starts as a fresh guest. When that guest signs in with a
-- Google account that already belongs to another user, the client calls this
-- (with the guest's token) before switching, so no orphan guest is left.
create function public.delete_anonymous_self()
returns void
language sql
security definer
set search_path = ''
as $$
  delete from auth.users where id = (select auth.uid()) and is_anonymous;
$$;

revoke execute on function public.delete_anonymous_self() from public, anon;
grant execute on function public.delete_anonymous_self() to authenticated;
revoke execute on function public.handle_new_user() from public, anon, authenticated;

-- users who signed up before this migration
insert into public.profiles (id) select id from auth.users on conflict do nothing;
insert into public.stats (user_id) select id from auth.users on conflict do nothing;
