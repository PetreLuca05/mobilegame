-- FRIENDS-1 A1: the friend pushes carry the skin, so the panel can show the head without a lookup.

create or replace function public.friend_respond(p_other uuid, p_accept boolean)
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
  perform public.fr_push(p_other, 'friend_accepted',
    (select jsonb_build_object('from', v_me, 'name', p.display_name, 'skin', p.skin) from public.profiles p where p.id = v_me));
end;
$$;

create or replace function public.friend_request_by_code(p_code text)
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
  perform public.fr_push(v_other, 'friend_request',
    (select jsonb_build_object('from', v_me, 'name', p.display_name, 'skin', p.skin) from public.profiles p where p.id = v_me));
  return 'requested';
end;
$$;
