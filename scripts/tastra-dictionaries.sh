#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Installs the Hunspell dictionaries for the Tastra languages.
# Runs at install time only; the keyboard process itself never uses the network.
#
#   tastra-dictionaries            install whatever is missing
#   tastra-dictionaries --status   only report what is found
#
# The languages: English, German, Ukrainian, Russian and those turned on in
# Settings → Languages (es, fr, it, pt, pl, nl since 0.7.19). Override the
# list with TASTRA_LANGUAGES="en de uk ru".
set -euo pipefail

enabled_languages() {
    local conf="${XDG_CONFIG_HOME:-$HOME/.config}/tastra/tastra.conf" line
    [[ -f "$conf" ]] || return 0
    line="$(grep -m1 '^enabledLanguages=' "$conf" || true)"
    line="${line#enabledLanguages=}"
    tr ', ' '\n\n' <<< "$line" | grep -E '^[a-z]{2}$' || true
}
LANGS="${TASTRA_LANGUAGES:-$(printf '%s\n' en de uk ru $(enabled_languages) | awk '!seen[$0]++' | tr '\n' ' ')}"
USER_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/tastra/dictionaries"
IFS=: read -r -a SYSTEM_DIRS <<< "${TASTRA_SYSTEM_DICT_DIRS:-/usr/share/hunspell:/usr/share/myspell/dicts}"

# Ukrainian and Brazilian Portuguese are not packaged in the official Arch
# repositories. Use the LibreOffice upstream dictionaries (uk_UA: MPL-1.1,
# based on brown-uk/dict_uk; pt_BR: VERO, LGPLv3/MPL), pinned to one commit
# and verified by SHA-256.
UK_COMMIT="32b006a2c22a4ac7e8ed3f03346f7b3d85a970a4"
UK_BASE="https://raw.githubusercontent.com/LibreOffice/dictionaries/${UK_COMMIT}/uk_UA"
UK_AFF_SHA="2219dd15e9802adebc45722c60943b1472640260491af38dd3e43b07e75585e6"
UK_DIC_SHA="2e5a9e67be63bdb089b3459addb5d71113319d13768e277bcae20f3cc1ad5a93"
PT_BASE="https://raw.githubusercontent.com/LibreOffice/dictionaries/${UK_COMMIT}/pt_BR"
PT_AFF_SHA="21d8ad2a769a60e17e2b5ea4ef11d4d593a58b9e2a82d642ef82d6a4c5523865"
PT_DIC_SHA="a38bfb26b68ece2834e79fe83e48d5792652970ace12db89d1b9674bf9933183"

names_for() {
    case "$1" in
        en) echo "en_US en_GB" ;;
        de) echo "de_DE de_DE_frami de_AT de_CH" ;;
        uk) echo "uk_UA" ;;
        ru) echo "ru_RU" ;;
        es) echo "es_ES es_MX es_AR es_CO es_US es_ANY" ;;
        fr) echo "fr_FR fr fr-classique fr_CA fr_BE fr_CH" ;;
        it) echo "it_IT it_CH" ;;
        pt) echo "pt_BR pt_PT" ;;
        pl) echo "pl_PL" ;;
        nl) echo "nl_NL nl" ;;
        *)  echo "" ;;
    esac
}

package_for() {
    case "$1" in
        en) echo "hunspell-en_us" ;;
        de) echo "hunspell-de" ;;
        ru) echo "hunspell-ru" ;;
        es) echo "hunspell-es_es" ;;
        fr) echo "hunspell-fr-comprehensive" ;;
        it) echo "hunspell-it" ;;
        pl) echo "hunspell-pl" ;;
        nl) echo "hunspell-nl" ;;
        *)  echo "" ;;
    esac
}

find_dict() {
    local lang="$1" dir name
    for name in $(names_for "$lang"); do
        for dir in "$USER_DIR" "${SYSTEM_DIRS[@]}"; do
            if [[ -f "$dir/$name.dic" && -f "$dir/$name.aff" ]]; then
                echo "$dir/$name"
                return 0
            fi
        done
    done
    return 1
}

status() {
    local lang path rc=0
    for lang in $LANGS; do
        if path="$(find_dict "$lang")"; then
            printf '  %-3s OK       %s\n' "$lang" "$path"
        else
            printf '  %-3s MISSING  (autocorrect limited to built-in typo list)\n' "$lang"
            rc=1
        fi
    done
    return $rc
}

install_uk() {
    local tmp
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' RETURN
    echo "Downloading pinned Ukrainian dictionary (LibreOffice ${UK_COMMIT:0:12}, MPL-1.1)..."
    curl -fsSL --proto '=https' --tlsv1.2 -o "$tmp/uk_UA.aff" "$UK_BASE/uk_UA.aff"
    curl -fsSL --proto '=https' --tlsv1.2 -o "$tmp/uk_UA.dic" "$UK_BASE/uk_UA.dic"
    echo "$UK_AFF_SHA  $tmp/uk_UA.aff" | sha256sum -c --quiet -
    echo "$UK_DIC_SHA  $tmp/uk_UA.dic" | sha256sum -c --quiet -
    install -d "$USER_DIR"
    install -m644 "$tmp/uk_UA.aff" "$tmp/uk_UA.dic" "$USER_DIR/"
    echo "Installed: $USER_DIR/uk_UA.{aff,dic}"
}

install_pt() {
    local tmp
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' RETURN
    echo "Downloading pinned Brazilian Portuguese dictionary (LibreOffice ${UK_COMMIT:0:12}, LGPLv3/MPL)..."
    curl -fsSL --proto '=https' --tlsv1.2 -o "$tmp/pt_BR.aff" "$PT_BASE/pt_BR.aff"
    curl -fsSL --proto '=https' --tlsv1.2 -o "$tmp/pt_BR.dic" "$PT_BASE/pt_BR.dic"
    echo "$PT_AFF_SHA  $tmp/pt_BR.aff" | sha256sum -c --quiet -
    echo "$PT_DIC_SHA  $tmp/pt_BR.dic" | sha256sum -c --quiet -
    install -d "$USER_DIR"
    install -m644 "$tmp/pt_BR.aff" "$tmp/pt_BR.dic" "$USER_DIR/"
    echo "Installed: $USER_DIR/pt_BR.{aff,dic}"
}

if [[ "${1:-}" == "--status" ]]; then
    echo "Tastra dictionaries:"
    status
    exit $?
fi

packages=()
need_uk=0
need_pt=0
for lang in $LANGS; do
    find_dict "$lang" >/dev/null && continue
    pkg="$(package_for "$lang")"
    if [[ -n "$pkg" ]]; then
        packages+=("$pkg")
    elif [[ "$lang" == "uk" ]]; then
        need_uk=1
    elif [[ "$lang" == "pt" ]]; then
        need_pt=1
    else
        echo "WARN: no known dictionary source for language '$lang'"
    fi
done

if ((${#packages[@]})); then
    if command -v pacman >/dev/null 2>&1; then
        echo "Installing: ${packages[*]}"
        sudo pacman -S --needed "${packages[@]}"
    else
        echo "WARN: pacman not found; install these with your package manager: ${packages[*]}"
    fi
fi
if ((need_uk)); then
    install_uk
fi
if ((need_pt)); then
    install_pt
fi

echo "Tastra dictionaries:"
status || true
