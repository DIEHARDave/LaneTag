#!/bin/sh
# capture.sh HOST OUTDIR SECONDS: save every voice attempt's WAV.
host=$1; out=$2; secs=$3; mkdir -p "$out"
last() { curl -s -m 4 "http://$host/api/voice" | python -I -c "import json,sys
d=json.load(sys.stdin); l=d.get('last',{})
a=l.get('action','')
print(l.get('id',0), ('train_'+l.get('heard','').replace(' ','')) if a.startswith('Take') else 'test', l.get('frames',0), (l.get('heard') or '-').replace(' ','_'), a.replace(' ','_'))" 2>/dev/null; }
seen=$(last | cut -d' ' -f1); end=$(( $(date +%s) + secs )); n=0
while [ $(date +%s) -lt $end ] && [ ! -f "$out/STOP" ]; do
  set -- $(last)
  if [ -n "$1" ] && [ "$1" != "$seen" ]; then
    seen=$1; f=$(printf '%s/%03d_%s.wav' "$out" "$1" "$2")
    curl -s -m 15 -o "$f" "http://$host/api/voice/last.wav" && n=$((n+1)) && echo "$f $3 frames, heard $4: $5"
  fi
  sleep 0.7
done
echo "captured $n"
