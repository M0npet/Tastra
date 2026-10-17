# Word frequency lists

Derived from FrequencyWords by Hermit Dave (content/2018, 50k lists for en, de,
ru, uk), generated from OpenSubtitles 2018 (http://opus.nlpl.eu/).
https://github.com/hermitdave/FrequencyWords — content licensed CC BY-SA 4.0.

Adaptation: lowercased, counts removed, non-word tokens dropped; one word per
line in descending frequency. CC BY-SA 4.0 is one-way compatible with GPLv3
(Creative Commons, 2015); this project is GPL-3.0-or-later.

Words with an apostrophe (en: "don't", "I'm", "it's"…; uk: "п'ять", "м'ясо",
"зв'язок"…) were missing because FrequencyWords splits tokens at apostrophes.
They were added from wordfreq by Robyn Speer (https://github.com/rspeer/wordfreq,
v3; data CC BY-SA 4.0) with `tools/merge-apostrophe-words.py`, which also moves
fragments such as "don" or "ім" to wordfreq's rank and drops pure fragments
such as "isn". Only list positions were taken (no counts). wordfreq's data
includes SUBTLEX word lists by Marc Brysbaert et al.
(http://crr.ugent.be/programs-data/subtitle-frequencies; SUBTLEX is freely
available data), Google Books Ngrams (http://books.google.com/ngrams),
OpenSubtitles 2018 via OPUS, Wikipedia, ParaCrawl and the Leeds Internet
Corpus.

Russian words were dropped from the Ukrainian list with
`tools/drop-foreign-words.py` (15 503 of 50 771 words, e.g. "что", "как",
"конечно"): words the Russian Hunspell dictionary accepts and either the
Ukrainian one rejects or wordfreq finds at least ten times more common in
Russian, and words with ы, э, ъ or ё.

## es, fr, it, pt, pl, nl (0.7.19)

From the same FrequencyWords content/2018 50k lists (pt: the pt_br list), made
with `tools/import-language.py`: lowercased, counts removed, non-word tokens
and single letters other than each language's one-letter words dropped. Two
filters use wordfreq (Robyn Speer, v3, data CC BY-SA 4.0; only list
positions and the frequencies below were used, no counts were copied):

- forms written without their accents that wordfreq finds under 10 % as
  often as the accented word of the same letters ("tambien", "nao",
  "prosze"; "esta" and "está" both stay), because subtitles often leave
  accents out;
- words wordfreq finds at least ten times as often in English ("the",
  "you"), unless the language's Hunspell dictionary from LibreOffice
  (es_ES, it_IT, pt_BR, pl_PL, nl_NL at LibreOffice/dictionaries
  32b006a2; read with spylls, not distributed) knows them ("has", "come"
  in Spanish). French had no dictionary at hand: the frequency rule alone.

Apostrophe words for fr, it, nl were added with
`tools/merge-apostrophe-words.py` as for en and uk ("aujourd'hui",
"dell'anno", "zo'n"). French elisions ("c'est", "j'ai", "l'homme"), which
wordfreq splits too, were counted in the Common Voice sentences (CC0,
https://github.com/common-voice/common-voice b99fe2a, server/data/fr; not the
Wikipedia, address and single-word sets) and placed at the position of the
same frequency in wordfreq; the bare fragments ("c", "j", "qu") were dropped.
