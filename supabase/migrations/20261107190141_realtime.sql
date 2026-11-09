-- M1: private Realtime channels for room codes.
-- Clients join topic "realtime:room:ABCD" with config.private = true and an
-- access_token from an anonymous sign-in. realtime.topic() sees "room:ABCD".
-- Any authenticated user (anonymous users are role "authenticated" too) may
-- receive and send Broadcast + Presence on topics named room:<4 letters>.

create policy "room members can receive broadcast and presence"
on realtime.messages
for select
to authenticated
using (
  (select realtime.topic()) ~ '^room:[A-Z]{4}$'
  and realtime.messages.extension in ('broadcast', 'presence')
);

create policy "room members can send broadcast and presence"
on realtime.messages
for insert
to authenticated
with check (
  (select realtime.topic()) ~ '^room:[A-Z]{4}$'
  and realtime.messages.extension in ('broadcast', 'presence')
);
