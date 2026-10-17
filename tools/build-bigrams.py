#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the bundled word pairs (bigrams) for next-word suggestions.

Gboard and LatinIME ship a language model with the keyboard, so "I" is
followed by "am", "have", "don't" before the user has typed anything. Tastra
learns pairs from the user's own typing; these lists give it a start. Only
corpora whose text is under a licence compatible with the GPL-3.0 are used
(CC BY-SA 4.0, CC BY 4.0 and CC0; CC licences below 4.0 and NC ones are not):

  en  UD English EWT (web text: reviews, e-mail, blogs, answers)   CC BY-SA 4.0
      UD English ESLSpok (spoken English of learners)              CC BY-SA 4.0
  de  UD German GSD (news, reviews, wiki)                          CC BY-SA 4.0
  ru  UD Russian Taiga (blogs, social media, fiction, news)        CC BY-SA 4.0
      UD Russian GSD (wiki)                                        CC BY-SA 4.0
  uk  UA-GEC, corrected texts (essays, posts, translations)       CC BY 4.0
      UD Ukrainian ParlaMint (Verkhovna Rada transcripts)          CC BY-SA 4.0
  all Common Voice sentences (sentence collector and contributors' CC0
      own sets: everyday sentences, public-domain prose); for es, fr,
      it, pt, pl and nl (0.7.19) the only source

The test parts of the corpora are left out (they are for measuring). A pair
counts when both words are in the bundled frequency list of the language,
within a sentence; commas, colons and dashes keep the pair (the keyboard
keeps the previous word across them), anything else breaks it. Pairs seen
fewer than twice are dropped.

    git clone --depth 1 https://github.com/UniversalDependencies/UD_English-EWT  (and the others)
    git clone --depth 1 https://github.com/grammarly/ua-gec
    git clone --depth 1 --filter=blob:none --sparse https://github.com/common-voice/common-voice cv
    git -C cv sparse-checkout set server/data/en server/data/de server/data/ru server/data/uk
    python3 tools/build-bigrams.py CORPORA_DIR data [LANGUAGE ...]
"""
import collections
import glob
import os
import re
import sys

SOURCES = {
    'en': [('ud', 'UD_English-EWT'), ('ud', 'UD_English-ESLSpok'), ('cv', 'en')],
    'de': [('ud', 'UD_German-GSD'), ('cv', 'de')],
    'ru': [('ud', 'UD_Russian-Taiga'), ('ud', 'UD_Russian-GSD'), ('cv', 'ru')],
    'uk': [('uagec', 'ua-gec'), ('ud', 'UD_Ukrainian-ParlaMint'), ('cv', 'uk')],
    'es': [('cv', 'es')],
    'fr': [('cv', 'fr')],
    'it': [('cv', 'it')],
    'pt': [('cv', 'pt')],
    'pl': [('cv', 'pl')],
    'nl': [('cv', 'nl')],
}
CREDITS = {
    'en': 'UD English EWT and ESLSpok (CC BY-SA 4.0) and Common Voice sentences (CC0)',
    'de': 'UD German GSD (CC BY-SA 4.0) and Common Voice sentences (CC0)',
    'ru': 'UD Russian Taiga and GSD (CC BY-SA 4.0) and Common Voice sentences (CC0)',
    'uk': 'UA-GEC by Grammarly (CC BY 4.0), UD Ukrainian ParlaMint (CC BY-SA 4.0) and Common Voice sentences (CC0)',
    'es': 'Common Voice sentences (CC0)',
    'fr': 'Common Voice sentences (CC0)',
    'it': 'Common Voice sentences (CC0)',
    'pt': 'Common Voice sentences (CC0)',
    'pl': 'Common Voice sentences (CC0)',
    'nl': 'Common Voice sentences (CC0)',
}
# Common Voice files left out: Wikipedia sentences (encyclopaedic, and they
# would outweigh everything else), word lists, and the German Europarl and
# Ukrainian ukrlib.com.ua sentences (parliament speeches and older prose:
# measured, they made suggestions for people's own writing worse while
# multiplying the pairs). Every tenth line is kept back for measuring.
CV_SKIP = ('wiki', 'singleword', 'countries-and-cities', 'german-cities', 'europarl', 'ukrlib',
           'selected-europarl', 'addresses')
MIN_COUNT = 2
# Words of one letter (as the keyboard's lexicon knows them); other single
# letters are initials and abbreviations cut at their dots ("z. B.").
ONE_LETTER_WORDS = {'en': 'ai', 'de': '', 'ru': 'авикосуя', 'uk': 'авзійоуяє',
                    'es': 'aeouy', 'fr': 'aày', 'it': 'aeèio', 'pt': 'aeoàé', 'pl': 'aiouwz', 'nl': 'u'}

APOSTROPHES = str.maketrans({'’': "'", 'ʼ': "'", '‘': "'", '`': "'"})
# Letters with inner apostrophes or hyphens ("don't", "кто-то", "м'ясо").
TOKEN = re.compile(r"[^\W\d_]+(?:['-][^\W\d_]+)*|[,;:–—-]|\S", re.UNICODE)
KEEPS_PAIR = set(',;:-–—')


def ud_texts(directory):
    for path in sorted(glob.glob(os.path.join(directory, '*.conllu'))):
        if '-test.' in os.path.basename(path):
            continue
        with open(path, encoding='utf-8') as f:
            for line in f:
                if line.startswith('# text = '):
                    yield line[len('# text = '):].strip()


def uagec_texts(directory):
    # One corrected version per document (several annotators corrected some).
    seen = set()
    for path in sorted(glob.glob(os.path.join(directory, 'data/gec-fluency/train/target/*.txt'))):
        doc = os.path.basename(path).split('.')[0]
        if doc in seen:
            continue
        seen.add(doc)
        with open(path, encoding='utf-8') as f:
            yield from f.read().splitlines()


def cv_texts(directory):
    for path in sorted(glob.glob(os.path.join(directory, '*.txt'))):
        if os.path.basename(path).startswith(CV_SKIP):
            continue
        with open(path, encoding='utf-8') as f:
            for i, line in enumerate(f):
                if i % 10:
                    yield line.strip()


def sentences(text):
    # Sentence ends inside a line break the pairs too. Stress marks (U+0301,
    # U+0300: "мя́со") are not letters of the word.
    text = text.translate(APOSTROPHES).replace('\u0301', '').replace('\u0300', '')
    yield from re.split(r'[.!?\u2026]+', text)


def count_pairs(texts, known, pairs, capitals, one_letter):
    for text in texts:
        for sentence in sentences(text):
            previous = None
            for token in TOKEN.findall(sentence):
                if token in KEEPS_PAIR:
                    continue
                word = token.lower()
                if word not in known or (len(word) == 1 and word not in one_letter):
                    previous = None
                    continue
                if previous is not None:
                    pairs[(previous, word)] += 1
                    if token[0].isupper():
                        capitals[(previous, word)] += 1
                previous = word


def main():
    corpora, data = sys.argv[1], sys.argv[2]
    chosen = sys.argv[3:] or list(SOURCES)
    for language, sources in SOURCES.items():
        if language not in chosen:
            continue
        with open(os.path.join(data, 'frequency', language + '.txt'), encoding='utf-8') as f:
            known = {line.strip() for line in f if line.strip()}
        pairs, capitals = collections.Counter(), collections.Counter()
        for kind, name in sources:
            if kind == 'cv':
                texts = cv_texts(os.path.join(corpora, 'cv/server/data', name))
            else:
                directory = os.path.join(corpora, name)
                texts = ud_texts(directory) if kind == 'ud' else uagec_texts(directory)
            count_pairs(texts, known, pairs, capitals, ONE_LETTER_WORDS[language])
        kept = [(p, n) for p, n in pairs.items() if n >= MIN_COUNT]
        kept.sort(key=lambda item: (item[0][0], -item[1], item[0][1]))
        os.makedirs(os.path.join(data, 'bigrams'), exist_ok=True)
        out = os.path.join(data, 'bigrams', language + '.txt')
        with open(out, 'w', encoding='utf-8') as f:
            f.write(f'# Word pairs for next-word suggestions: previous word, next word, count.\n')
            f.write(f'# Built by tools/build-bigrams.py from {CREDITS[language]}.\n')
            for (previous, word), n in kept:
                # German nouns that are also other words: "das Unternehmen",
                # but "wir unternehmen". (Names are capitalised by the
                # dictionary; a capital "Вам" is a matter of style.)
                capital = language == 'de' and capitals[(previous, word)] * 2 > n
                shown = word[0].upper() + word[1:] if capital else word
                f.write(f'{previous} {shown} {n}\n')
        print(f'{language}: {len(kept)} pairs (of {len(pairs)}), {sum(pairs.values())} counted', file=sys.stderr)


if __name__ == '__main__':
    main()
