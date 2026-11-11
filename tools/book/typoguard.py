"""Typo guard: misprinted tokens (typo errata) must never be what a candidate produces or picks as correct."""
import importlib.util, os, re

HERE = os.path.dirname(os.path.abspath(__file__))


def load_tokens():
    spec = importlib.util.spec_from_file_location('tt', os.path.join(HERE, 'data', 'typo_tokens.py'))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m.T


def find(tok, txt):
    return re.search(r'(?<!\w)' + re.escape(tok) + r'(?!\w)', txt, re.I)


def answer_fields(it):
    """(field path, text) for every text the candidate must produce or pick as correct"""
    t = it['type']
    out = []
    if t == 'cloze':
        out = [(('answers', i), a) for i, a in enumerate(it['answers'])]
    elif t in ('simplu', 'multiplu'):
        out = [(('options', i, 't'), o['t']) for i, o in enumerate(it['options']) if o.get('ok')]
    elif t == 'card':
        out = [(('answer',), it['answer'])]
    elif t == 'sort':
        out = ([(('entries', i, 't'), e['t']) for i, e in enumerate(it['entries'])] + [(('groups', i), g) for i, g in enumerate(it['groups'])]
               + [(('entries', i, 'group'), e['group']) for i, e in enumerate(it['entries'])])
    elif t == 'match':
        out = [(('pairs', i, 'left'), p['left']) for i, p in enumerate(it['pairs'])] + [(('pairs', i, 'right'), p['right']) for i, p in enumerate(it['pairs'])]
    elif t == 'order':
        out = [(('sequence', i), s) for i, s in enumerate(it['sequence'])]
    elif t == 'label':
        out = [(('labels', i, 't'), l['t']) for i, l in enumerate(it['labels'])]
    return out


def violations(it, tokens):
    v = []
    for path, txt in answer_fields(it):
        for eid, toks in tokens.items():
            for tok, cor in toks:
                if find(tok, txt):
                    v.append((path, eid, tok, cor))
    return v
