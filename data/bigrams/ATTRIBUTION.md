# Word pairs (next-word suggestions)

`<lang>.txt` lists how often one word followed another (previous word, next
word, count) in the corpora below, built with `tools/build-bigrams.py`. Only
counts of adjacent words were taken: no sentences or other text. The test
parts of the corpora were left out. A pair counts only when both words are in
the bundled frequency list of the language; pairs seen once are dropped. For
German, a next word that was mostly capitalised after the previous one is
listed capitalised ("das Unternehmen").

These lists are an adaptation of the corpora and are licensed
CC BY-SA 4.0 (https://creativecommons.org/licenses/by-sa/4.0/). CC BY-SA 4.0
is one-way compatible with GPLv3 (Creative Commons, 2015); this project is
GPL-3.0-or-later.

| List | Corpus | Creators | Licence |
|---|---|---|---|
| en | UD English EWT (https://github.com/UniversalDependencies/UD_English-EWT, 4a4d77f) | Natalia Silveira, Timothy Dozat, Christopher Manning, Sebastian Schuster, Ethan Chi, John Bauer, Miriam Connor, Marie-Catherine de Marneffe, Nathan Schneider, Sam Bowman, Hanzhi Zhu, Daniel Galbraith | CC BY-SA 4.0 |
| en | UD English ESLSpok (https://github.com/UniversalDependencies/UD_English-ESLSpok, 3feb27b) | Kris Kyle, Masaki Eguchi, Aaron Miller, Ted Sither | CC BY-SA 4.0 |
| de | UD German GSD (https://github.com/UniversalDependencies/UD_German-GSD, ce54dbe) | Slav Petrov, Wolfgang Seeker, Ryan McDonald, Joakim Nivre, Daniel Zeman, Adriane Boyd, Verena Blaschke | CC BY-SA 4.0 |
| ru | UD Russian Taiga (https://github.com/UniversalDependencies/UD_Russian-Taiga, fcfd7db) | Olga Lyashevskaya, Olga Rudina, Natalia Vlasova, Anna Zhuravleva | CC BY-SA 4.0 |
| ru | UD Russian GSD (https://github.com/UniversalDependencies/UD_Russian-GSD, 0f34b73) | Ryan McDonald, Vitaly Nikolaev, Olga Lyashevskaya | CC BY-SA 4.0 |
| uk | UA-GEC 2.0, corrected texts, first annotator, train part (https://github.com/grammarly/ua-gec, 4757f72) | Oleksiy Syvokon, Olena Nahorna, Pavlo Kuchmiichuk, Nastasiia Osidach (Grammarly) | CC BY 4.0 |
| uk | UD Ukrainian ParlaMint (https://github.com/UniversalDependencies/UD_Ukrainian-ParlaMint, 7d3fcb7) | Maria Shvedova, Arsenii Lukashevskyi | CC BY-SA 4.0 |
| es, fr, it, pt, pl, nl | Common Voice sentences only (0.7.19), as below, also without the address sets and the Polish Europarl selection | Mozilla Common Voice contributors | CC0 1.0 |
| all | Common Voice sentences, `server/data/<lang>` (https://github.com/common-voice/common-voice, 43b7944): the sentence collector and contributors' own sets; not the Wikipedia sentences, word lists, German Europarl or Ukrainian ukrlib.com.ua sets; every tenth line left out | Mozilla Common Voice contributors | CC0 1.0 |

Corpora that were considered and not used because of their licence: UD
English GUM and LinES, UD Russian SynTagRus, UD Ukrainian IU, BRUK
(CC BY-NC-SA 4.0: non-commercial), UD German HDT (text for academic use only),
UD PUD treebanks (CC BY-SA 3.0), Google Books Ngrams (CC BY 3.0).
