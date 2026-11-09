-- FRIENDS-1 step 4: the old multiplayer is gone. room:ABCD and world:01 were
-- open to any signed-in user; only user:<uid> and room:<token> remain.
drop policy if exists "room members can receive broadcast and presence" on realtime.messages;
drop policy if exists "room members can send broadcast and presence" on realtime.messages;
