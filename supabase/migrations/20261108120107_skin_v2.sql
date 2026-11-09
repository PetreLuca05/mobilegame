-- M4: character creator skin v2. profiles.skin stays jsonb, now
--   {"body","skin","hair","hair_color","suit","accessory","palette"}
-- (clients read missing keys as 0, so old clients keep working). M2 rows
-- only had {palette, hair, accessory}: map the palette preset onto a suit,
-- skin colour and hair colour.

alter table public.profiles
  add constraint skin_is_object check (jsonb_typeof(skin) = 'object');

-- M2 palette index -> (suit, skin colour, hair colour)
update public.profiles p
set skin = p.skin || jsonb_build_object(
      'body', 0,
      'suit', (array[3, 0, 3, 4, 6, 1, 2, 7])[coalesce((p.skin->>'palette')::int, 0) + 1],
      'skin', (array[0, 2, 4, 5, 0, 2, 0, 4])[coalesce((p.skin->>'palette')::int, 0) + 1],
      'hair_color', (array[1, 1, 0, 0, 0, 5, 2, 0])[coalesce((p.skin->>'palette')::int, 0) + 1])
where not (p.skin ? 'suit')
  and coalesce((p.skin->>'palette')::int, 0) between 0 and 7;

-- new players: the default doctor (white coat, short dark hair, mask)
alter table public.profiles
  alter column skin set default
  '{"body":0,"skin":1,"hair":1,"hair_color":1,"suit":3,"accessory":3,"palette":0}';
