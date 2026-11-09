-- Open-world mode: public shards are private Realtime channels named
-- world:<2 digits>. Same rules as party rooms: any authenticated (incl.
-- anonymous) user may receive/send Broadcast and Presence.

drop policy if exists "room members can receive broadcast and presence" on realtime.messages;
drop policy if exists "room members can send broadcast and presence" on realtime.messages;

create policy "room members can receive broadcast and presence"
on realtime.messages
for select
to authenticated
using (
  (select realtime.topic()) ~ '^(room:[A-Z]{4}|world:[0-9]{2})$'
  and realtime.messages.extension in ('broadcast', 'presence')
);

create policy "room members can send broadcast and presence"
on realtime.messages
for insert
to authenticated
with check (
  (select realtime.topic()) ~ '^(room:[A-Z]{4}|world:[0-9]{2})$'
  and realtime.messages.extension in ('broadcast', 'presence')
);
