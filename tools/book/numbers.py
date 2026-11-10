#!/usr/bin/env python3
"""numbers.csv: every token with a digit, %, unit, dose, gene/allele, drug, abbreviation or proper/author name,
with its page and the surrounding sentence, for the author's tick-off.  Usage: numbers.py PACK"""
import csv, json, os, re, sys

UNITS = r'(?:%|mg|g|µg|μg|kg|ml|mL|l|L|cm|mm|m|ore|oră|h|zile|zi|săptămâni|săptămână|luni|lună|ani|an|min|minute|mmHg|mmol|µmol|U|UI|mg/kg|mg/zi|g/zi|g/dL|g/L|mm/h|µg/g|μg/g|ori)'
DRUG_SUFFIX = re.compile(r'\b\w+(?:prazol|tidină|tidina|mab|cilină|cilina|micină|micina|ciclină|ciclina|nidazol|salazină|salazina|prină|prina|'
                         r'olon|sonă|zonă|rizon|tizon|xacină|xacina|floxacină|cină|stol|pramid|peridonă|tinib|limus|sporină|sporina|trexat|'
                         r'zidă|alazină|ozid|onid|bismut\w*|alginat\w*|sucralfat\w*|lactulo\w+|loperamid\w*|codein\w*)\b', re.I)
DRUGS = {'amoxicilină', 'metronidazol', 'claritromicină', 'tetraciclină', 'levofloxacină', 'ciclosporină', 'tacrolimus',
         'azatioprină', 'mercaptopurină', 'metotrexat', 'budesonid', 'prednisolon', 'prednison', 'hidrocortizon', 'mesalazină',
         'sulfasalazină', 'balsalazidă', 'olsalazină', 'infliximab', 'adalimumab', 'vedolizumab', 'ustekinumab', 'tofacitinib',
         'golimumab', 'certolizumab', 'natalizumab', 'misoprostol', 'omeprazol', 'lansoprazol', 'pantoprazol', 'rabeprazol',
         'esomeprazol', 'dexlansoprazol', 'metoclopramid', 'domperidonă', 'eritromicină', 'cimetidină', 'ranitidină', 'famotidină',
         'nizatidină', 'clorhexidină', 'fenitoin', 'nifedipină', 'litiu', 'bismut', 'ciprofloxacină', 'rifaximină', 'aspirină',
         'colestiramină', 'teduglutid', 'etrolizumab', 'ozanimod', 'filgotinib', 'upadacitinib', 'risankizumab', 'mirikizumab'}
STOP_CAPS = {'În', 'La', 'Cu', 'De', 'Pe', 'Se', 'Este', 'Sunt', 'Acest', 'Aceasta', 'Această', 'Acesta', 'Aceste', 'Acestea',
             'Ei', 'Un', 'O', 'Unii', 'Unele', 'Dacă', 'Când', 'Din', 'Prin', 'Fig', 'Cadranul', 'Tratament', 'Pacienții',
             'Pacienţii', 'Nu', 'Deși', 'Totuși', 'Astfel', 'Iar', 'Dar', 'Sau', 'Și', 'Ca', 'Cel', 'Cea', 'Cele', 'Cei', 'Alte',
             'Alți', 'Aproximativ', 'Majoritatea', 'Există', 'Mai', 'Rolul', 'Atunci', 'Uneori', 'Pentru', 'Între', 'După', 'Înainte'}


def main(pack):
    atoms = [json.loads(l) for l in open(os.path.join(pack, 'atoms.jsonl'), encoding='utf-8')]
    rows = []
    for a in atoms:
        t = a['text']
        spans = a['sent_spans'] or [[0, len(t)]]
        for s0, s1 in spans:
            sent = t[s0:s1]
            found = []
            for m in re.finditer(r'[^\s(),;:„”"]*\d[^\s(),;:„”"]*(?:\s' + UNITS + r'\b)?', sent):
                tok = m.group(0).rstrip('.')
                cat = 'dose/unit' if re.search(r'\s' + UNITS + r'$', tok) or tok.endswith('%') else 'number'
                if re.search(r'[A-Za-z].*\d|\d.*[A-Za-z]', tok) and not re.search(r'\s', tok):
                    cat = 'code/gene/allele' if not re.fullmatch(r'\d+[.,]?\d*%?', tok) else cat
                found.append((cat, tok))
            for m in re.finditer(r'\b[\wăâîșțĂÂÎȘȚ\-]+\b', sent):
                w = m.group(0)
                lw = w.lower()
                if lw in DRUGS or DRUG_SUFFIX.fullmatch(w):
                    found.append(('drug', w))
                elif re.fullmatch(r'[A-ZĂÂÎȘȚ][A-ZĂÂÎȘȚ0-9\-]{1,}', w) and not re.search(r'\d', w):
                    found.append(('abbreviation', w))
            if a['kind'] == 'reference':
                m = re.match(r'(.+?\b[A-Z]{1,3})\.\s', sent)
                if m:
                    found.append(('author', m.group(1)))
            elif a['kind'] == 'byline':
                found.append(('author', sent))
            else:
                for m in re.finditer(r'(?<=[\w,;:)] )([A-ZĂÂÎȘȚ][a-zăâîșțöüé]+(?:[ -][A-ZĂÂÎȘȚ][a-zăâîșțöüé]+)*)', sent):
                    w = m.group(1)
                    if w.split()[0] not in STOP_CAPS:
                        found.append(('name', w))
            seen = set()
            for cat, tok in found:
                if (cat, tok) in seen:
                    continue
                seen.add((cat, tok))
                rows.append([cat, tok, a['pdf_page'], a['page'], a['id'], a['layer'], 'yes' if a['unverified_ocr'] else '',
                             ' > '.join(a['path'][-2:]), re.sub(r'\s+', ' ', sent)])
    with open(os.path.join(pack, 'numbers.csv'), 'w', encoding='utf-8', newline='') as f:
        w = csv.writer(f)
        w.writerow(['n', 'category', 'token', 'pdf_page', 'page', 'atom', 'layer', 'unverified_ocr', 'section', 'sentence', 'ok'])
        for i, r in enumerate(rows, 1):
            w.writerow([i] + r + [''])
    print('numbers.csv rows', len(rows))


if __name__ == '__main__':
    main(sys.argv[1])
