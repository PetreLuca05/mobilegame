-- LINK-1: did this ticket's merge commit? For the one ambiguous moment, when the connection
-- dies during merge_ticket_redeem and the client cannot tell whether it went through.
create function public.merge_ticket_used(p_ticket text)
returns boolean
language sql
stable
security definer
set search_path = ''
as $$
  select coalesce((select t.used_at is not null from public.merge_tickets t where t.ticket = p_ticket), false);
$$;
revoke execute on function public.merge_ticket_used(text) from public, anon;
grant execute on function public.merge_ticket_used(text) to authenticated;
