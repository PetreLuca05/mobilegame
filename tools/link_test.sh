#!/bin/zsh
# LINK-1 end to end on desktop, with email + password standing in for the ID-token grant
# (cmake -DMG_TEST_AUTH=ON). Creates test accounts, drives --autotest friends l1..l5, checks the
# database after each case and removes the accounts. Every run uses new profiles, so each role
# starts as a brand-new guest. Usage: tools/link_test.sh OUTDIR SUPABASE_CLI
cd "$(dirname "$0")/.."
OUT=${1:-/tmp/mg_lt_out}; SB=${2:-supabase}
export MG_FT_SYNC=/tmp/mg_lt
GAME=build/desktop_link/mobilegame
PREFS="$HOME/Library/Application Support/docvex/mobilegame"
RUN=$(date +%s)
E_MAIL="seed$RUN@mgtest.invalid"; NEW_MAIL="new$RUN@mgtest.invalid"
mkdir -p "$OUT" docs/autotest/link "$MG_FT_SYNC"
find "$MG_FT_SYNC" -maxdepth 1 -type f -delete
FAILS=0

sql() { # prints the value of the query's single column, or the error
  print -r -- "$1" > $OUT/q.sql
  $SB db query --remote --yes --output json --file $OUT/q.sql </dev/null 2>&1 | python3 -c '
import sys,json
t=sys.stdin.read()
try:
    d=json.loads(t[t.find("{"):]); r=d.get("rows") or [{}]; print(list(r[0].values())[0] if r[0] else "")
except Exception: print("ERR", t[-300:])'
}
check() { if [ "$2" = "$3" ]; then echo "PASS $1"; else echo "FAIL $1: got [$2] wanted [$3]"; FAILS=$((FAILS+1)); fi }
play() { # role
  MG_WINDOW=iphone $GAME --profile lt_${RUN}_$1 --frontend settings --autotest friends $1 --window-pos 100,60 --quit-after 200 > $OUT/$1.log 2>&1
  if grep -q "AUTOTEST PASSED" $OUT/$1.log; then echo "PASS role $1"; else echo "FAIL role $1: $(grep -E 'FAILED at' $OUT/$1.log | head -1 | cut -c28-170)"; FAILS=$((FAILS+1)); fi
}

# sessions: each launch counts one, on top of the merged totals (5 + 2, then +1 for the run that merged)
E_ID=$(sql "select gen_random_uuid()")
sql "do \$\$ begin
  insert into auth.users (id, instance_id, aud, role, email, encrypted_password, email_confirmed_at, is_anonymous, confirmation_token, recovery_token, email_change, email_change_token_new, email_change_token_current, phone_change, phone_change_token, reauthentication_token, raw_app_meta_data, raw_user_meta_data, created_at, updated_at)
    values ('$E_ID', '00000000-0000-0000-0000-000000000000', 'authenticated', 'authenticated', '$E_MAIL', extensions.crypt('mgtest-password-1', extensions.gen_salt('bf')), now(), false, '', '', '', '', '', '', '', '', '{\"provider\":\"email\",\"providers\":[\"email\"]}', '{}', now(), now());
  insert into auth.identities (provider_id, user_id, identity_data, provider, last_sign_in_at, created_at, updated_at) values ('$E_ID', '$E_ID', jsonb_build_object('sub', '$E_ID', 'email', '$E_MAIL'), 'email', now(), now(), now());
  update public.profiles set display_name = 'Seeded', skin = '{\"hair\":3,\"accessory\":1}' where id = '$E_ID';
  update public.stats set sessions = 5, play_seconds = 100, distance_m = 10, elevator_rides = 2, rooms_joined = 1 where user_id = '$E_ID';
end \$\$; select 1" > /dev/null
echo "seeded $E_ID"

# what the server does on a real link, for the one case the email stand-in cannot do itself
( while [ ! -f $MG_FT_SYNC/convert_request ]; do sleep 0.5; done
  UID_=$(cut -d' ' -f1 $MG_FT_SYNC/convert_request)
  sql "do \$\$ begin
    update auth.users set is_anonymous = false, email = '$NEW_MAIL', email_confirmed_at = now(), encrypted_password = extensions.crypt('mgtest-password-1', extensions.gen_salt('bf')),
      confirmation_token = '', recovery_token = '', email_change = '', email_change_token_new = '', email_change_token_current = '', phone_change = '', phone_change_token = '', reauthentication_token = '' where id = '$UID_';
    insert into auth.identities (provider_id, user_id, identity_data, provider, last_sign_in_at, created_at, updated_at) values ('$UID_', '$UID_', jsonb_build_object('sub', '$UID_', 'email', '$NEW_MAIL'), 'email', now(), now(), now());
  end \$\$; select 1" > /dev/null
  touch $MG_FT_SYNC/convert_done ) &

MG_LT_EMAIL=$NEW_MAIL play l1
U1=$(cat $MG_FT_SYNC/uid_l1)
check "A: same user is permanent" "$(sql "select is_anonymous::text from auth.users where id = '$U1'")" "false"
check "A: stats intact" "$(sql "select sessions || '/' || play_seconds from public.stats where user_id = '$U1'")" "3/50"

MG_LT_EMAIL=$E_MAIL play l2
U2=$(cat $MG_FT_SYNC/uid_l2)
check "B: guest deleted" "$(sql "select count(*) from auth.users where id = '$U2'")" "0"
check "B: guest profile deleted" "$(sql "select count(*) from public.profiles where id = '$U2'")" "0"
check "B: counters summed" "$(sql "select sessions || '/' || play_seconds || '/' || elevator_rides || '/' || rooms_joined from public.stats where user_id = '$E_ID'")" "8/150/3/4"
check "B: the account keeps its name" "$(sql "select display_name from public.profiles where id = '$E_ID'")" "Seeded"
check "B: rejoined on the new topic" "$(grep -c 'user channel joined' $OUT/l2.log | tr -d ' ')" "2"

MG_LT_EMAIL=$E_MAIL play l3
U3=$(cat $MG_FT_SYNC/uid_l3)
check "Cancel: guest untouched" "$(sql "select is_anonymous::text from auth.users where id = '$U3'")" "true"
check "Cancel: no ticket" "$(sql "select count(*) from public.merge_tickets where guest_id = '$U3'")" "0"

MG_TEST_AUTH_FAIL=redeem MG_LT_EMAIL=$E_MAIL play l4
U4=$(cat $MG_FT_SYNC/uid_l4)
check "Cut: guest still there" "$(sql "select is_anonymous::text from auth.users where id = '$U4'")" "true"
check "Cut: ticket not used" "$(sql "select count(*) from public.merge_tickets where guest_id = '$U4' and used_at is not null")" "0"
check "Cut: the account's numbers unchanged" "$(sql "select sessions from public.stats where user_id = '$E_ID'")" "8"

MG_LT_EMAIL=$E_MAIL play l5
check "Reinstall: numbers still there" "$(sql "select sessions from public.stats where user_id = '$E_ID'")" "9"

for f in "$PREFS"/friends_l*.png; do cp "$f" docs/autotest/link/ 2>/dev/null; done
sql "do \$\$ begin
  delete from auth.users where email like '%@mgtest.invalid';
  delete from auth.users where id in ('$U1', '$U3', '$U4') and is_anonymous;
end \$\$; select 1" > /dev/null
exit $FAILS
