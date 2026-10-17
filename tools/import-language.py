#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Bring in the word data of another keyboard language.

    python3 tools/import-language.py LANG FREQUENCYWORDS_50K LDNOOBW CLDR_ANNOTATIONS_JSON [CV_DIR [HUNSPELL_BASE]]

writes data/frequency/LANG.txt, data/blocklist/LANG.txt and data/emoji/LANG.tsv
the way the first four languages were made (see the ATTRIBUTION.md files):

  frequency  FrequencyWords 2018 50k list (Hermit Dave, OpenSubtitles; CC BY-SA
             4.0): lowercased, counts removed, tokens that are not letters
             (with inner hyphens or apostrophes) dropped, single letters other
             than the language's one-letter words dropped, first occurrence kept;
             forms written without their accents that wordfreq finds under 10 %
             as often as the accented word ("tambien", "nao") dropped, and words
             wordfreq finds ten times as often in English ("the", "you") unless
             the language's Hunspell dictionary (HUNSPELL_BASE, read with
             spylls) knows them ("has", "come" in Spanish).
             Then, for languages that write apostrophes inside words, the
             apostrophe words from wordfreq (tools/merge-apostrophe-words.py),
             and for French the elisions (c'est, j'ai, l'homme), which wordfreq
             splits too, counted in the Common Voice sentences (CC0) of CV_DIR
             and placed by wordfreq's frequency of the same mass; the bare
             elision fragments (c, j, l, qu ...) go.
  blocklist  LDNOOBW (CC BY 4.0): single words, lowercased, sorted.
  emoji      Unicode CLDR annotations (cldr-json, Unicode License V3): for each
             emoji of data/emoji/en.tsv, the language's name first, then its
             keywords, lowercased, joined by "|".
"""
import importlib.util
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LETTERS = re.compile(r"^[^\W\d_]+(?:[-'][^\W\d_]+)*$")
APOSTROPHE_LANGUAGES = {'fr', 'it', 'nl'}
# French elision prefixes (with the apostrophe) and the words that stay.
FRENCH_ELISIONS = re.compile(r"^(?:c|d|j|l|m|n|s|t|qu|jusqu|lorsqu|puisqu|quoiqu|presqu)'[^\W\d_]+$")
FRENCH_FRAGMENTS = {'c', 'd', 'j', 'l', 'm', 'n', 's', 't', 'qu', 'jusqu', 'lorsqu', 'puisqu', 'quoiqu',
                    'presqu', 'aujourd'}
CV_SKIP = ('wiki', 'singleword', 'addresses', 'countries-and-cities')
# The one-letter words of each language; other single letters in the
# subtitle lists are initials and spelled-out letters.
ONE_LETTER_WORDS = {'es': 'aeouy', 'fr': 'aày', 'it': 'aeèio', 'pt': 'aeoàé', 'pl': 'aiouwz', 'nl': 'u'}


def frequency(language, source):
    words, seen = [], set()
    for line in source.read_text(encoding='utf-8').splitlines():
        if not line.strip():
            continue
        token = line.split()[0].lower().replace('’', "'")
        if len(token) == 1 and token not in ONE_LETTER_WORDS.get(language, token):
            continue
        if LETTERS.match(token) and token not in seen:
            seen.add(token)
            words.append(token)
    return words


def strip_accents(word):
    import unicodedata
    word = word.replace('ł', 'l').replace('ø', 'o')
    return ''.join(c for c in unicodedata.normalize('NFD', word) if not unicodedata.combining(c))


def drop_unaccented_misspellings(language, words):
    """Subtitles often leave accents out ("tambien", "nao", "prosze"); listed,
    such a form counts as a word and blocks the accent correction. It goes
    when wordfreq, which also counts edited text, finds it under 10 % as often
    as the accented word of the same letters ("esta" / "está" both stay)."""
    import wordfreq
    accented = {}
    for w in words:
        bare = strip_accents(w)
        if bare != w and bare not in accented:
            accented[bare] = w
    kept, dropped = [], []
    for w in words:
        sibling = accented.get(w)
        if sibling and strip_accents(w) == w \
                and wordfreq.word_frequency(w, language) < 0.1 * wordfreq.word_frequency(sibling, language):
            dropped.append(w)
            continue
        kept.append(w)
    print(f'{language}: {len(dropped)} unaccented misspellings dropped, e.g. {dropped[:12]}', file=sys.stderr)
    return kept


def drop_english_words(language, words, hunspell_base=None):
    """Subtitles quote English ("the", "you", "baby"): a word wordfreq finds at
    least ten times as often in English as in the language goes (as Russian
    words went from the Ukrainian list)."""
    import wordfreq
    dictionary = None
    if hunspell_base:
        from spylls.hunspell import Dictionary
        dictionary = Dictionary.from_files(hunspell_base)
    kept, dropped = [], []
    for w in words:
        own = wordfreq.word_frequency(w, language)
        if "'" not in w and wordfreq.word_frequency(w, 'en') >= 10 * max(own, 1e-9) \
                and not (dictionary and dictionary.lookup(w)):
            dropped.append(w)
            continue
        kept.append(w)
    print(f'{language}: {len(dropped)} English words dropped, e.g. {dropped[:15]}', file=sys.stderr)
    return kept


def merge_apostrophes(path, language):
    spec = importlib.util.spec_from_file_location('merge', ROOT / 'tools' / 'merge-apostrophe-words.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    added, moved, dropped = module.merge(path, language)
    print(f'{language}: +{added} apostrophe words, {moved} re-ranked, {dropped} fragments dropped', file=sys.stderr)


def french_elisions(path, cv_dir):
    import collections
    import wordfreq
    words = [w for w in path.read_text(encoding='utf-8').splitlines() if w]
    counts, total = collections.Counter(), 0
    token = re.compile(r"[^\W\d_]+(?:['-][^\W\d_]+)*")
    for file in sorted(Path(cv_dir).glob('*.txt')):
        if file.name.startswith(CV_SKIP):
            continue
        for line in file.read_text(encoding='utf-8').splitlines():
            for t in token.findall(line.replace('’', "'").lower()):
                total += 1
                if FRENCH_ELISIONS.match(t):
                    counts[t] += 1
    elisions = [(w, n) for w, n in counts.items() if n >= 30]
    kept = [w for w in words if w not in FRENCH_FRAGMENTS]
    known = sorted((wordfreq.word_frequency(w, 'fr') for w in kept), reverse=True)
    import bisect
    negated = [-f for f in known]
    inserts = sorted(((bisect.bisect_right(negated, -(n / total)), w) for w, n in elisions), key=lambda x: x[0])
    result, k = [], 0
    for position, w in enumerate(kept):
        while k < len(inserts) and inserts[k][0] <= position:
            result.append(inserts[k][1])
            k += 1
        result.append(w)
    result.extend(w for _, w in inserts[k:])
    seen, unique = set(), []
    for w in result:
        if w not in seen:
            seen.add(w)
            unique.append(w)
    path.write_text('\n'.join(unique) + '\n', encoding='utf-8')
    print(f'fr: +{len(elisions)} elisions from {total} Common Voice words, '
          f'{len(words) - len(kept)} fragments dropped', file=sys.stderr)


def blocklist(source):
    words = set()
    for line in source.read_text(encoding='utf-8').splitlines():
        w = line.strip().lower()
        if w and ' ' not in w:
            words.add(w)
    return sorted(words)


def emoji(language, annotations_json):
    data = json.loads(annotations_json.read_text(encoding='utf-8'))
    table = data['annotations']['annotations']
    lines = []
    for line in (ROOT / 'data' / 'emoji' / 'en.tsv').read_text(encoding='utf-8').splitlines():
        glyph = line.split('\t')[0]
        entry = table.get(glyph) or table.get(glyph + '️')
        if not entry:
            continue
        names = [n.lower() for n in entry.get('tts', [])]
        keywords = [k.lower() for k in entry.get('default', []) if k.lower() not in names]
        if names or keywords:
            lines.append(glyph + '\t' + '|'.join(names + keywords))
    return lines


def main():
    if len(sys.argv) < 5:
        print(__doc__)
        return 2
    language = sys.argv[1]
    path = ROOT / 'data' / 'frequency' / f'{language}.txt'
    hunspell_base = sys.argv[6] if len(sys.argv) > 6 else None
    words = drop_english_words(language, drop_unaccented_misspellings(language, frequency(language, Path(sys.argv[2]))),
                               hunspell_base)
    path.write_text('\n'.join(words) + '\n', encoding='utf-8')
    if language in APOSTROPHE_LANGUAGES:
        merge_apostrophes(path, language)
    if language == 'fr' and len(sys.argv) > 5:
        french_elisions(path, sys.argv[5])
    (ROOT / 'data' / 'blocklist' / f'{language}.txt').write_text('\n'.join(blocklist(Path(sys.argv[3]))) + '\n',
                                                                 encoding='utf-8')
    lines = emoji(language, Path(sys.argv[4]))
    (ROOT / 'data' / 'emoji' / f'{language}.tsv').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print(f'{language}: {len(path.read_text(encoding="utf-8").splitlines())} words, {len(lines)} emoji',
          file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main())
