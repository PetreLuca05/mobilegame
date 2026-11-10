#!/usr/bin/env python3
"""Copy the git-safe part of the pack and the pipeline into the (public) repository, then run the leak guard on it.
  export_repo.py PACK REPO
Never copies book text: cleaned.txt, atoms, facts, items, page fixes, notes, figures, topic sources and errata data stay in
the zip. gastro-quests.json is exported with its heading lists as atom ids (the titles are book text)."""
import json, os, re, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
PACK_FILES = ['README.md', 'STATE.md', 'QUESTIONS.md', 'LAYOUT_GUIDE.md', 'AUTHORING_GUIDE.md', 'check.py', 'institute.map',
              'rules.json', 'topic_rules.json', 'leakguard.txt', 'ENGINE_PROMPT.md']
TOOL_EXT = ('.py', '.sh', '.html')


def main(pack, repo):
    dst = os.path.join(repo, 'content', 'gastro')
    os.makedirs(os.path.join(dst, 'layout'), exist_ok=True)
    for f in PACK_FILES:
        if os.path.exists(os.path.join(pack, f)):
            shutil.copy2(os.path.join(pack, f), os.path.join(dst, f))
    for f in sorted(os.listdir(os.path.join(pack, 'layout'))):
        shutil.copy2(os.path.join(pack, 'layout', f), os.path.join(dst, 'layout', f))
    # quest table: heading titles -> atom ids
    q = json.load(open(os.path.join(pack, 'gastro-quests.json'), encoding='utf-8'))
    atoms = [json.loads(l) for l in open(os.path.join(pack, 'atoms.jsonl'), encoding='utf-8')]
    for ch in q['chapters']:
        for st in ch['steps']:
            st['heading_atoms'] = [a['id'] for a in atoms if a['topic'] == st['topic'] and a['kind'] in ('heading', 'figure_caption')]
            st.pop('headings', None)
    q['note'] = 'public copy: heading titles are listed as atom ids (heading_atoms); the titles travel in the content-pack zip'
    json.dump(q, open(os.path.join(dst, 'gastro-quests.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    tdst = os.path.join(repo, 'tools', 'book')
    os.makedirs(tdst, exist_ok=True)
    for f in sorted(os.listdir(HERE)):
        if f.endswith(TOOL_EXT) and os.path.isfile(os.path.join(HERE, f)):
            shutil.copy2(os.path.join(HERE, f), os.path.join(tdst, f))
    r = subprocess.run([sys.executable, '-I', os.path.join(HERE, 'leakguard.py'), 'scan', os.path.join(pack, 'leakguard.txt')]
                       + [os.path.join(dp, f) for dp, _, fs in os.walk(dst) for f in fs if f != 'leakguard.txt']
                       + [os.path.join(tdst, f) for f in os.listdir(tdst) if os.path.isfile(os.path.join(tdst, f))]
                       + [os.path.join(repo, 'CLAUDE.md')], capture_output=True, text=True)
    print(r.stdout.strip().splitlines()[-1] if r.stdout.strip() else r.stderr)
    if r.returncode:
        print(r.stdout)
        sys.exit(1)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
