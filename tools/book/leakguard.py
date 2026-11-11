#!/usr/bin/env python3
"""Leak guard: no file in the (public) repository may contain a run of 12 or more consecutive words of the book.

  leakguard.py build PACK                 write PACK/leakguard.txt: truncated SHA-1 of every 12-word shingle of
                                          cleaned.txt (hashes only, no text; safe to commit)
  leakguard.py scan GUARD FILE...         fail if any file has a 12-word run whose hash is in GUARD
  leakguard.py repo GUARD REPO [--history] scan every tracked file (and, with --history, every blob of every commit
                                          on the current branch)
Words: lower-cased runs of letters / digits (diacritics kept, ş/ţ folded to ș/ț); punctuation and markup ignored.
"""
import hashlib, os, re, subprocess, sys

N = 12
WORD = re.compile(r'[0-9a-zăâîșțáéíóúöüßçñµα-ω]+', re.I)


def words(text):
    text = text.replace('ş', 'ș').replace('ţ', 'ț').replace('Ş', 'Ș').replace('Ţ', 'Ț').replace('­', '')
    return [w.lower() for w in WORD.findall(text)]


def h(seq):
    return hashlib.sha1(' '.join(seq).encode('utf-8')).hexdigest()[:16]


def build(pack):
    w = words(open(os.path.join(pack, 'cleaned.txt'), encoding='utf-8').read())
    hs = sorted(set(h(w[i:i + N]) for i in range(len(w) - N + 1)))
    open(os.path.join(pack, 'leakguard.txt'), 'w').write('# 12-word shingles of the book (sha1[:16]); see tools/book/leakguard.py\n' + '\n'.join(hs) + '\n')
    print('leakguard.txt', len(hs), 'shingles')


def load(guard):
    return set(l.strip() for l in open(guard) if l.strip() and not l.startswith('#'))


PRIVATE = ('content_private/',)       # the real pack and figures live there on the build machine: never staged, never tracked


def private(names):
    """paths under content_private/ are refused whatever they hold"""
    bad = [n for n in names if n and any(n.startswith(p) or ('/' + p) in n for p in PRIVATE)]
    for n in bad:
        print(f'leakguard: {n}: under content_private/ (the real content never enters the repository)', file=sys.stderr)
    return len(bad)


SKIP = ('third_party/', 'shaders/compiled/', 'assets/fonts/', '.png', '.jpg', '.mgf', '.ttf', '.otf', '.pdf', '.zip', '.ico')


def hits(text, guard):
    w = words(text)
    if len(w) < N:
        return []
    out = []
    i = 0
    while i <= len(w) - N:
        if h(w[i:i + N]) in guard:
            j = i + N
            while j < len(w) and h(w[j - N + 1:j + 1]) in guard:
                j += 1
            out.append(' '.join(w[i:j]))
            i = j
        else:
            i += 1
    return out


def scan_blobs(guard, named):
    bad = 0
    for name, data in named:
        try:
            text = data.decode('utf-8')
        except UnicodeDecodeError:
            continue
        for run in hits(text, guard):
            bad += 1
            print(f'LEAK {name}: {len(run.split())} words: {run[:120]}...')
    return bad


def main():
    cmd = sys.argv[1]
    if cmd == 'build':
        return build(sys.argv[2])
    guard = load(sys.argv[2])
    if cmd == 'scan':
        bad = scan_blobs(guard, [(f, open(f, 'rb').read()) for f in sys.argv[3:]])
    elif cmd == 'staged':
        repo = sys.argv[3]
        names = subprocess.run(['git', '-C', repo, 'diff', '--cached', '--name-only', '--diff-filter=ACMR', '-z'],
                               capture_output=True).stdout.split(b'\0')
        priv = private([f.decode() for f in names])
        named = []
        for f in names:
            if f and not f.endswith(b'leakguard.txt') and not any(x in f.decode() for x in SKIP):
                data = subprocess.run(['git', '-C', repo, 'show', ':' + f.decode()], capture_output=True).stdout
                if len(data) < 5_000_000:
                    named.append((f.decode(), data))
        bad = scan_blobs(guard, named)
    elif cmd == 'repo':
        repo = sys.argv[3]
        files = subprocess.run(['git', '-C', repo, 'ls-files', '-z'], capture_output=True).stdout.split(b'\0')
        priv = private([f.decode() for f in files])
        named = []
        for f in files:
            if f and not f.endswith(b'leakguard.txt') and not any(x in f.decode() for x in SKIP):
                p = os.path.join(repo, f.decode())
                if os.path.isfile(p) and os.path.getsize(p) < 5_000_000:
                    named.append((f.decode(), open(p, 'rb').read()))
        if '--history' in sys.argv:
            revs = subprocess.run(['git', '-C', repo, 'rev-list', 'HEAD', '--not', '--remotes=origin/main'], capture_output=True, text=True).stdout.split()
            for rev in revs:
                tree = subprocess.run(['git', '-C', repo, 'ls-tree', '-r', '-z', rev], capture_output=True).stdout.split(b'\0')
                for ent in tree:
                    if not ent:
                        continue
                    meta, path = ent.split(b'\t', 1)
                    _, typ, sha = meta.split()
                    if typ != b'blob' or path.endswith(b'leakguard.txt') or any(x in path.decode() for x in SKIP):
                        continue
                    data = subprocess.run(['git', '-C', repo, 'cat-file', '-p', sha.decode()], capture_output=True).stdout
                    if len(data) < 5_000_000:
                        named.append((f'{rev[:8]}:{path.decode()}', data))
        bad = scan_blobs(guard, named)
    bad += locals().get('priv', 0)
    print('leakguard:', 'FAIL' if bad else 'clean', f'({bad} runs of >= {N} book words or private paths)')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
