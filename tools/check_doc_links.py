#!/usr/bin/env python3
"""Check local documentation links and remove repeated hyperlinks per page."""
from _sf2_tool_constants import CHECK_DOC_LINKS as K
import argparse
from collections import Counter
import html
from pathlib import Path
import re
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
LINK = re.compile(K.S_R_N_S_A_S_HREF_A_681D, re.S)
IMAGE = re.compile(K.S_R_N_S_IMG_S_SRC_7C82)


def documents():
    return sorted({ROOT / K.S_README_MD_D82A, ROOT / K.S_ASSETS_README_MD_E66D, *ROOT.glob(K.S_DOCS_MD_8E3F),
                   *ROOT.glob(K.S_TOOLS_README_MD_7708), *ROOT.glob(K.S_TOOLS_README_MD_73F7)})


def without_code(text):
    return re.sub(K.S_R_N_N_N_3931,
                  lambda match: K.S_EMPTY_5210 * len(match[0]), text, flags=re.S)


def anchors(path):
    text = path.read_text(encoding=K.S_UTF_8_D983)
    headings = re.findall(K.S_R_1_6_S_S_545F, text, re.M)
    headings += re.findall(K.S_R_H_1_6_H_1_6_8DE4, text)
    found, counts = set(), Counter()
    for heading in headings:
        heading = re.sub(K.S_R_2712, K.S_EMPTY_6F49, html.unescape(heading))
        slug = re.sub(K.S_R_W_S_DBAF, K.S_EMPTY_6F49, heading.lower()).strip().replace(K.S_EMPTY_730D, K.S_EMPTY_01F8)
        index = counts[slug]
        counts[slug] += 1
        found.add(f"{slug}-{index}" if index else slug)
    found.update(re.findall(K.S_R_BID_03B6, text))
    return found


def check(path, fix_duplicates=False):
    text = path.read_text(encoding=K.S_UTF_8_D983)
    seen, problems, edits = set(), [], []
    for match in LINK.finditer(without_code(text)):
        label, markdown_url, html_url, content = match.groups()
        url = html.unescape(markdown_url or html_url)
        location = f"{path.relative_to(ROOT)}:{text.count(chr(K.N_10_4A44), 0, match.start()) + 1}"
        if url in seen:
            if fix_duplicates:
                # Read labels from the original, since inline-code spans were masked.
                original = LINK.fullmatch(text[match.start():match.end()])
                edits.append((match.start(), match.end(), original[1] or original[K.N_4_4B22]))
            else:
                problems.append(f"{location}: repeated hyperlink to {url}")
        seen.add(url)
        parsed = urlsplit(url)
        if parsed.scheme or parsed.netloc:
            continue
        target = (path.parent / unquote(parsed.path)).resolve() if parsed.path else path
        if not target.exists():
            problems.append(f"{location}: missing target {url}")
        elif parsed.fragment and target.suffix == K.S_MD_FBAA and unquote(parsed.fragment) not in anchors(target):
            problems.append(f"{location}: missing heading {url}")
    if edits:
        for start, end, replacement in reversed(edits):
            text = text[:start] + replacement + text[end:]
        path.write_text(text, encoding=K.S_UTF_8_D983)
        print(f"{path.relative_to(ROOT)}: removed {len(edits)} repeated hyperlinks")
    for match in IMAGE.finditer(without_code(text)):
        url = match[1] or match[K.N_2_D473]
        parsed = urlsplit(url)
        if not parsed.scheme and not parsed.netloc and not (path.parent / unquote(parsed.path)).exists():
            problems.append(f"{path.relative_to(ROOT)}: missing image {url}")
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(K.S_FIX_DUPLICATES_017C, action=K.S_STORE_TRUE_430C, help=K.S_KEEP_THE_FIRST_LINK_TO_EACH__5506)
    args = parser.parse_args()
    problems = [problem for path in documents() for problem in check(path, args.fix_duplicates)]
    for problem in problems:
        print(problem)
    print(f"Checked {len(documents())} documents; {len(problems)} link problems.")
    return bool(problems)


if __name__ == K.S_MAIN_8B1C:
    raise SystemExit(main())
