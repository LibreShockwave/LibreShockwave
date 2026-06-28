#!/usr/bin/env python3
"""Standalone old Director XMED text renderer.

This is intentionally outside the root runtime. It is a small experiment for
inspecting XMED text payloads and rendering them before changing C++ renderer
behavior.
"""

from __future__ import annotations

import argparse
import json
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable

from PIL import Image, ImageDraw, ImageFont


REPO_ROOT = Path(__file__).resolve().parents[4]
DEFAULT_OUT_DIR = Path(__file__).resolve().parent / "out"


@dataclass
class StyleRun:
    offset: int
    style: int


@dataclass
class StyleRecord:
    font_candidate_index: int = -1
    font_size: int = -1
    bold: bool = False
    italic: bool = False
    underline: bool = False
    color_r: int = -1
    color_g: int = -1
    color_b: int = -1
    raw_fore_color_values: list[int] | None = None
    source_offset: int = -1


@dataclass
class StyledSpan:
    start_offset: int
    end_offset: int
    font_name: str
    font_size: int
    bold: bool
    italic: bool
    underline: bool
    color_r: int
    color_g: int
    color_b: int


@dataclass
class ParsedXmedText:
    text: str
    font_candidates: list[str]
    font_name: str
    font_size: int
    bold: bool
    italic: bool
    color_r: int
    color_g: int
    color_b: int
    spans: list[StyledSpan]
    section_count: dict[str, int]
    color_source: str


@dataclass
class MovieColorContext:
    ink: int
    fore_color_r: int
    fore_color_g: int
    fore_color_b: int
    has_fore_color: bool


@dataclass
class ResolvedColor:
    color_r: int
    color_g: int
    color_b: int
    source: str
    authored_color_r: int
    authored_color_g: int
    authored_color_b: int


PRESETS = {
    "gf_private_login_labels": [
        {
            "name": "forget_password",
            "xmed": "/opt/git/v1_assets/interface_gfx/raw_chunks/00762_Forget_your_password_1087_XMED.bin",
            "width": 206,
            "height": 12,
            "ink": 36,
            "fore_color": "000000",
        },
        {
            "name": "password_colon",
            "xmed": "/opt/git/v1_assets/interface_gfx/raw_chunks/00561_member_1127_XMED.bin",
            "width": 100,
            "height": 12,
            "ink": 36,
            "fore_color": "000000",
        },
        {
            "name": "loading_dot",
            "xmed": "/opt/git/v1_assets/interface_gfx/raw_chunks/00567_member_1135_XMED.bin",
            "width": 80,
            "height": 12,
            "ink": 36,
            "fore_color": "000000",
        },
    ],
}


def is_hex_digit(value: int) -> bool:
    return (
        ord("0") <= value <= ord("9")
        or ord("A") <= value <= ord("F")
        or ord("a") <= value <= ord("f")
    )


def to_ascii(data: bytes) -> str:
    return "".join(chr(byte) if 0x20 <= byte < 0x7F else "." for byte in data)


def parse_hex(text: str) -> int:
    if not text:
        return 0
    try:
        return int(text, 16)
    except ValueError:
        return 0


def parse_section_length(ascii_text: str, section_offset: int) -> int:
    if section_offset + 4 >= len(ascii_text):
        return 0
    return parse_hex(ascii_text[section_offset + 4 : min(section_offset + 12, len(ascii_text))])


def parse_section_count(ascii_text: str, section_offset: int) -> int:
    if section_offset + 12 >= len(ascii_text):
        return 0
    return parse_hex(ascii_text[section_offset + 12 : min(section_offset + 20, len(ascii_text))])


class Packer:
    """Minimal Paige/Director packed-number reader used by XMED style records."""

    def __init__(self, data: bytes):
        self.data = data
        self.pos = 0
        self.last_value = 0
        self.repeat_count = 0

    def remaining(self) -> int:
        return max(0, len(self.data) - self.pos)

    def unpack_num(self) -> int:
        if self.repeat_count > 0:
            self.repeat_count -= 1
            return self.last_value
        if self.pos >= len(self.data):
            return 0

        ctrl = self.data[self.pos]
        self.pos += 1
        if ctrl & 0x80:
            value = self.last_value
            if ctrl & 0x40 and self.pos < len(self.data):
                self.repeat_count = self.data[self.pos] - 1
                self.pos += 1
        else:
            start = self.pos
            while self.pos < len(self.data) and (
                is_hex_digit(self.data[self.pos]) or self.data[self.pos] == ord("-")
            ):
                self.pos += 1
            raw = self.data[start : self.pos].decode("ascii", errors="ignore")
            negative = raw.startswith("-")
            clean = raw[1:] if negative else raw
            value = parse_hex(clean)
            if negative:
                value = -value
            if (ctrl & 0x0F) == 1:
                value &= 0xFFFF

        self.last_value = value
        return value

    def unpack_refcon(self, doc_version: int) -> int:
        if doc_version == 65547 and self.pos < len(self.data) and self.data[self.pos] == 0:
            self.pos += 1
            start = self.pos
            while self.pos < len(self.data) and (
                is_hex_digit(self.data[self.pos]) or self.data[self.pos] == ord("-")
            ):
                self.pos += 1
            raw_size = self.data[start : self.pos].decode("ascii", errors="ignore")
            try:
                size = max(0, int(raw_size or "0", 10))
            except ValueError:
                size = 0
            if self.pos < len(self.data) and self.data[self.pos] == ord(","):
                self.pos += 1
            self.pos = min(len(self.data), self.pos + size)
            return size
        return self.unpack_num()


def find_section(data: bytes, tag: str) -> int | None:
    tag_bytes = tag.encode("ascii")
    for index in range(0, max(0, len(data) - len(tag_bytes))):
        if data[index] == 0x03 and data[index + 1 : index + 1 + len(tag_bytes)] == tag_bytes:
            return index + 1
    return None


def section_body(data: bytes, ascii_text: str, tag: str) -> bytes:
    section = find_section(data, tag)
    if section is None:
        return b""
    sec_start = section + 20
    sec_len = parse_section_length(ascii_text, section)
    sec_end = min(sec_start + sec_len, len(data)) if sec_len > 0 else len(data)
    return data[sec_start:sec_end]


def decode_mac_roman(raw: bytes) -> str:
    return raw.decode("mac_roman", errors="replace")


def extract_count_comma_text(data: bytes, pos: int) -> str | None:
    if pos >= len(data):
        return None
    end = len(data)
    for index in range(pos, len(data)):
        if data[index] == 0x03:
            end = index
            break
    if end <= pos:
        return None
    comma = data.find(b",", pos, end)
    if comma < 0 or comma + 1 >= end:
        return None
    return decode_mac_roman(data[comma + 1 : end])


def try_parse_text_at(data: bytes, pos: int) -> bool:
    comma = None
    for index in range(pos, min(pos + 10, len(data))):
        if data[index] == ord(","):
            comma = index
            break
        if not is_hex_digit(data[index]):
            return False
    return comma is not None and comma > pos and comma + 1 < len(data) and data[comma + 1] >= 0x20


def extract_text(data: bytes, ascii_text: str) -> str:
    tag = ascii_text.find("0002")
    if tag >= 0:
        for index in range(tag + 4, len(data)):
            if data[index] == 0:
                text = extract_count_comma_text(data, index + 1)
                if text is not None:
                    return text
    for index in range(0, max(0, len(data) - 5)):
        if data[index] == 0 and try_parse_text_at(data, index + 1):
            text = extract_count_comma_text(data, index + 1)
            if text is not None:
                return text
    return ""


def extract_font_names(data: bytes, ascii_text: str) -> list[str]:
    tag = ascii_text.find("0008")
    if tag < 0:
        return []
    fonts: list[str] = []
    index = tag + 20
    while index + 10 < len(data):
        if data[index] == 0x03 and data[index + 1 : index + 4] == b"000":
            break
        if data[index] == 0 and index + 4 < len(data) and data[index + 1 : index + 4] == b"40,":
            name_len = data[index + 4]
            if 0 < name_len < 64 and index + 5 + name_len <= len(data):
                font = decode_mac_roman(data[index + 5 : index + 5 + name_len]).rstrip()
                if font and font not in fonts:
                    fonts.append(font)
            index += 63
        index += 1
    return fonts


def select_primary_font(fonts: list[str]) -> str:
    if not fonts:
        return "Geneva"
    mac_font = fonts[0]
    for font in fonts[1:]:
        if font != mac_font:
            return font
    return mac_font


def extract_section_color(data: bytes, ascii_text: str) -> tuple[int, int, int] | None:
    section = find_section(data, "0003")
    if section is not None:
        for index in range(section + 20, max(section + 20, len(data) - 6)):
            if data[index] != 0:
                continue
            text = extract_count_comma_text(data, index + 1)
            if text is None:
                break
            parts = text.split(",")
            if len(parts) < 3:
                return None
            try:
                return tuple(max(0, min(255, int(part.strip()))) for part in parts[:3])  # type: ignore[return-value]
            except ValueError:
                return None
    return None


def skip_hex_field(data: bytes, start: int, end: int | None = None) -> int:
    limit = len(data) if end is None else min(end, len(data))
    pos = start
    while pos < limit and is_hex_digit(data[pos]):
        pos += 1
    return pos


def parse_hex_field(data: bytes, start: int, end: int) -> int:
    if start >= end:
        return 0
    return parse_hex(data[start:end].decode("ascii", errors="ignore"))


def extract_style_runs(data: bytes, ascii_text: str, text_len: int) -> list[StyleRun]:
    section = find_section(data, "0004")
    if section is None:
        return []
    sec_start = section + 20
    sec_len = parse_section_length(ascii_text, section)
    sec_end = min(sec_start + sec_len, len(data)) if sec_len > 0 else len(data)
    runs: list[StyleRun] = []
    index = sec_start
    while index < sec_end:
        if data[index] != 0x02:
            index += 1
            continue
        index += 1
        offset_start = index
        index = skip_hex_field(data, index, sec_end)
        if offset_start == index or index >= sec_end or data[index] != 0x01:
            index += 1
            continue
        offset_end = index
        index += 1
        style_start = index
        index = skip_hex_field(data, index, sec_end)
        if style_start == index:
            continue
        offset = parse_hex_field(data, offset_start, offset_end)
        style = parse_hex_field(data, style_start, index)
        if 0 <= offset <= text_len + 2:
            runs.append(StyleRun(min(offset, text_len), style))
    return sorted(runs, key=lambda run: run.offset)


def parse_doc_version(data: bytes, ascii_text: str) -> int:
    section = find_section(data, "0000")
    if section is None:
        return 262145
    body = section_body(data, ascii_text, "0000")
    return Packer(body).unpack_num() if body else 262145


def rgb_from_director_color(values: list[int]) -> tuple[int, int, int]:
    padded = (values + [0, 0, 0])[:3]
    return tuple(max(0, min(255, (value >> 8) & 0xFF)) for value in padded)  # type: ignore[return-value]


def extract_style_records(data: bytes, ascii_text: str) -> list[StyleRecord]:
    body = section_body(data, ascii_text, "0006")
    if not body:
        return []
    doc_version = parse_doc_version(data, ascii_text)
    packer = Packer(body)
    declared_count = packer.unpack_num()
    records: list[StyleRecord] = []
    max_records = max(0, min(declared_count + 8, 128))
    for _ in range(max_records):
        start = packer.pos
        if packer.remaining() < 2:
            break
        font_index = packer.unpack_num()
        packer.unpack_num()
        packer.unpack_num()
        packed_font_size = packer.unpack_num()
        packer.unpack_num()
        for _skip in range(4):
            packer.unpack_num()
        fore_color_values = [packer.unpack_num() for _index in range(4)]
        color = rgb_from_director_color(fore_color_values)
        for _index in range(4):
            packer.unpack_num()
        if doc_version < 65547:
            packer.unpack_num()
        paige_slots = [packer.unpack_num() for _index in range(11)]
        font_size = packed_font_size
        if len(paige_slots) > 1:
            real_font_size = paige_slots[1] // 65536
            if 6 <= real_font_size <= 200:
                font_size = real_font_size
        if doc_version < 65547:
            packer.unpack_num()
        if doc_version >= 65551:
            packer.unpack_num()
        packer.unpack_refcon(doc_version)
        packer.unpack_num()
        for _index in range(8):
            packer.unpack_num()
        gap2 = [packer.unpack_num() for _index in range(32 if doc_version >= 257 else 16)]
        if doc_version >= 65536:
            packer.unpack_num()
        if doc_version >= 65552:
            for _index in range(4):
                packer.unpack_num()
        if doc_version >= 65555:
            packer.unpack_num()
        records.append(
            StyleRecord(
                font_candidate_index=font_index,
                font_size=font_size if 6 <= font_size <= 200 else -1,
                bold=len(gap2) > 0 and gap2[0] != 0,
                italic=len(gap2) > 1 and gap2[1] != 0,
                underline=len(gap2) > 2 and gap2[2] != 0,
                color_r=color[0],
                color_g=color[1],
                color_b=color[2],
                raw_fore_color_values=fore_color_values,
                source_offset=start,
            )
        )
        if packer.pos <= start:
            break
    return records


def extract_style_record_font_sizes(data: bytes, sec_start: int, sec_end: int) -> list[int]:
    sizes: list[int] = []
    for index in range(sec_start, max(sec_start, sec_end - 6)):
        if data[index] != 0x02:
            continue
        field_end = skip_hex_field(data, index + 1, sec_end)
        raw = data[index + 1 : field_end].decode("ascii", errors="ignore")
        if len(raw) >= 5 and raw.endswith("0000") and field_end < sec_end and data[field_end] == 0x02:
            size = parse_hex(raw[:-4])
            if 6 <= size <= 200:
                sizes.append(size)
    return sizes


def choose_referenced_style_font_size(
    data: bytes,
    ascii_text: str,
    text_len: int,
    style_record_sizes: list[int],
) -> int:
    if not style_record_sizes or text_len <= 0:
        return -1
    runs = extract_style_runs(data, ascii_text, text_len)
    if not runs:
        return -1
    coverage_by_size: dict[int, int] = {}
    for index, run in enumerate(runs):
        if run.style < 0 or run.style >= len(style_record_sizes):
            continue
        start = max(0, min(run.offset, text_len))
        end = runs[index + 1].offset if index + 1 < len(runs) else text_len
        end = max(start, min(end, text_len))
        if end <= start:
            continue
        size = style_record_sizes[run.style]
        coverage_by_size[size] = coverage_by_size.get(size, 0) + (end - start)
    if not coverage_by_size:
        return -1
    return max(coverage_by_size.items(), key=lambda item: item[1])[0]


def extract_font_size_and_style(data: bytes, ascii_text: str, text_len: int) -> tuple[int, int]:
    style_records = extract_style_records(data, ascii_text)
    if style_records:
        runs = extract_style_runs(data, ascii_text, text_len)
        if runs:
            sizes_by_index = [record.font_size for record in style_records]
            referenced = choose_referenced_style_font_size(data, ascii_text, text_len, sizes_by_index)
            if referenced > 0:
                return (referenced, 0)
        first_size = next((record.font_size for record in style_records if record.font_size > 0), -1)
        if first_size > 0:
            return (first_size, 0)
    section = find_section(data, "0006")
    if section is None:
        return (9, 0)
    sec_start = section + 20
    sec_len = parse_section_length(ascii_text, section)
    sec_end = min(sec_start + sec_len, len(data)) if sec_len > 0 else len(data)
    style_record_sizes = extract_style_record_font_sizes(data, sec_start, sec_end)
    referenced = choose_referenced_style_font_size(data, ascii_text, text_len, style_record_sizes)
    if referenced > 0:
        return (referenced, 0)
    counts: dict[int, int] = {}
    first_size = -1
    for index in range(sec_start, max(sec_start, sec_end - 6)):
        if data[index] != 0x02:
            continue
        field_end = skip_hex_field(data, index + 1, sec_end)
        raw = data[index + 1 : field_end].decode("ascii", errors="ignore")
        if len(raw) >= 5 and raw.endswith("0000") and field_end < sec_end and data[field_end] == 0x02:
            size = parse_hex(raw[:-4])
            if 6 <= size <= 200:
                counts[size] = counts.get(size, 0) + 1
                if first_size < 0:
                    first_size = size
    if counts:
        return (max(counts.items(), key=lambda item: (item[1], -item[0]))[0], 0)
    if first_size > 0:
        return (first_size, 0)
    return (9, 0)


def resolve_span_font_name(font_candidates: list[str], fallback: str, record: StyleRecord | None) -> str:
    if record is None or record.font_candidate_index < 0:
        return fallback
    if record.font_candidate_index < len(font_candidates) and font_candidates[record.font_candidate_index]:
        return font_candidates[record.font_candidate_index]
    return fallback


def extract_spans(
    data: bytes,
    ascii_text: str,
    text_len: int,
    font_candidates: list[str],
    font_name: str,
    font_size: int,
    bold: bool,
    italic: bool,
    color: tuple[int, int, int],
) -> list[StyledSpan]:
    runs = extract_style_runs(data, ascii_text, text_len)
    records = extract_style_records(data, ascii_text)
    if not runs:
        return [StyledSpan(0, text_len, font_name, font_size, bold, italic, False, *color)]
    spans: list[StyledSpan] = []
    for index, run in enumerate(runs):
        start = max(0, min(run.offset, text_len))
        end = runs[index + 1].offset if index + 1 < len(runs) else text_len
        end = max(start, min(end, text_len))
        if start == end:
            continue
        record = records[run.style] if 0 <= run.style < len(records) else None
        span_font_name = resolve_span_font_name(font_candidates, font_name, record)
        span_font_size = record.font_size if record is not None and record.font_size > 0 else font_size
        span_color = (
            (record.color_r, record.color_g, record.color_b)
            if record is not None and record.color_r >= 0 and record.color_g >= 0 and record.color_b >= 0
            else color
        )
        spans.append(
            StyledSpan(
                start,
                end,
                span_font_name,
                span_font_size,
                bold or (record.bold if record is not None else False),
                italic or (record.italic if record is not None else False),
                record.underline if record is not None else False,
                *span_color,
            )
        )
    return spans or [StyledSpan(0, text_len, font_name, font_size, bold, italic, False, *color)]


def parse_xmed(data: bytes) -> ParsedXmedText:
    ascii_text = to_ascii(data)
    text = extract_text(data, ascii_text)
    font_candidates = extract_font_names(data, ascii_text)
    font_name = select_primary_font(font_candidates)
    font_size, font_style = extract_font_size_and_style(data, ascii_text, len(text))
    style_records = extract_style_records(data, ascii_text)
    selected_record: StyleRecord | None = None
    for run in extract_style_runs(data, ascii_text, len(text)):
        if 0 <= run.style < len(style_records):
            selected_record = style_records[run.style]
            break
    section_color = extract_section_color(data, ascii_text)
    if selected_record is not None and selected_record.color_r >= 0:
        color = (selected_record.color_r, selected_record.color_g, selected_record.color_b)
        color_source = (
            f"section 0006 style record at packed offset {selected_record.source_offset}; "
            f"raw foreColor={selected_record.raw_fore_color_values}"
        )
    elif section_color is not None:
        color = section_color
        color_source = "section 0003"
    else:
        color = (0, 0, 0)
        color_source = "no authored color found; fallback black"
    bold = bool(font_style & 1)
    italic = bool(font_style & 2)
    spans = extract_spans(data, ascii_text, len(text), font_candidates, font_name, font_size, bold, italic, color)
    sections = {}
    for tag in ("0002", "0003", "0004", "0005", "0006", "0008"):
        section = find_section(data, tag)
        if section is not None:
            sections[tag] = parse_section_count(ascii_text, section)
    return ParsedXmedText(text, font_candidates, font_name, font_size, bold, italic, *color, spans, sections, color_source)


def lower_ascii(value: str) -> str:
    return value.lower()


def font_candidates_for_name(font_name: str, bold: bool) -> Iterable[Path]:
    normalized = lower_ascii(font_name)
    if "volter" in normalized or "goldfish" in normalized:
        yield REPO_ROOT / "cpp/resources/fonts/volter/volter_bold.ttf" if ("bold" in normalized or bold) else REPO_ROOT / "cpp/resources/fonts/volter/volter.ttf"
    if "geneva" in normalized:
        yield REPO_ROOT / "cpp/resources/fonts/mac/Geneva-9.ttf"
        yield REPO_ROOT / "cpp/resources/fonts/mac/Geneva-12.ttf"
    if "arial" in normalized:
        yield REPO_ROOT / "cpp/resources/fonts/windows/Arial.ttf"
    yield REPO_ROOT / "cpp/resources/fonts/volter/volter_bold.ttf" if bold else REPO_ROOT / "cpp/resources/fonts/volter/volter.ttf"
    yield REPO_ROOT / "cpp/resources/fonts/mac/Geneva-9.ttf"
    yield REPO_ROOT / "cpp/resources/fonts/windows/Arial.ttf"


def resolve_font(font_name: str, font_size: int, bold: bool) -> tuple[ImageFont.FreeTypeFont | ImageFont.ImageFont, str]:
    size = max(1, font_size)
    for path in font_candidates_for_name(font_name, bold):
        if not path.exists():
            continue
        try:
            return ImageFont.truetype(str(path), size), str(path)
        except OSError:
            continue
    return ImageFont.load_default(), "Pillow default"


def line_break_chunks(text: str) -> list[str]:
    return text.replace("\r\n", "\r").replace("\n", "\r").split("\r")


def uses_director_pixel_styled_font(parsed: ParsedXmedText) -> bool:
    normalized = lower_ascii(parsed.font_name)
    if "bold" in normalized:
        return False
    return "volter" in normalized or "goldfish" in normalized


def parse_rgb(raw_color: str) -> tuple[int, int, int]:
    raw = raw_color.strip().lstrip("#")
    if len(raw) != 6:
        raise ValueError(f"Expected RRGGBB color, got {raw_color!r}")
    return tuple(int(raw[index : index + 2], 16) for index in (0, 2, 4))  # type: ignore[return-value]


def resolve_movie_text_color(parsed: ParsedXmedText, context: MovieColorContext | None) -> ResolvedColor:
    authored = (parsed.color_r, parsed.color_g, parsed.color_b)
    if context is None:
        return ResolvedColor(*authored, "authored XMED style color; no movie sprite context", *authored)
    fore_color = (context.fore_color_r, context.fore_color_g, context.fore_color_b)
    if (
        context.ink == 36
        and authored == (255, 255, 255)
        and fore_color == (0, 0, 0)
        and uses_director_pixel_styled_font(parsed)
    ):
        return ResolvedColor(
            *fore_color,
            "movie sprite foreColor override: backgroundTransparent ink, authored white XMED, Volter-style font",
            *authored,
        )
    if context.ink == 36 and authored == (255, 255, 255) and context.has_fore_color and fore_color != (255, 255, 255):
        return ResolvedColor(
            *fore_color,
            "movie sprite foreColor override: backgroundTransparent ink, authored white XMED, non-white sprite foreColor",
            *authored,
        )
    return ResolvedColor(*authored, "authored XMED style color retained by movie context", *authored)


def spans_with_resolved_color(parsed: ParsedXmedText, resolved: ResolvedColor) -> list[StyledSpan]:
    return [
        StyledSpan(
            span.start_offset,
            span.end_offset,
            span.font_name,
            span.font_size,
            span.bold,
            span.italic,
            span.underline,
            resolved.color_r,
            resolved.color_g,
            resolved.color_b,
        )
        for span in parsed.spans
    ]


def render(
    parsed: ParsedXmedText,
    width: int,
    height: int,
    out_path: Path,
    color_context: MovieColorContext | None = None,
    background: str | None = None,
) -> dict:
    width = max(1, width)
    height = max(1, height)
    background_color = parse_rgb(background) if background else None
    image = Image.new(
        "RGBA",
        (width, height),
        (*background_color, 255) if background_color else (0, 0, 0, 0),
    )
    draw = ImageDraw.Draw(image)
    chosen_fonts: dict[str, str] = {}
    resolved_color = resolve_movie_text_color(parsed, color_context)
    spans = spans_with_resolved_color(parsed, resolved_color)

    y = 0
    line_start = 0
    for line in line_break_chunks(parsed.text):
        x = 0
        line_end = line_start + len(line)
        line_spans = [
            span
            for span in spans
            if span.end_offset > line_start and span.start_offset < line_end
        ] or spans[:1]
        for span in line_spans:
            local_start = max(span.start_offset, line_start) - line_start
            local_end = min(span.end_offset, line_end) - line_start
            fragment = line[local_start:local_end]
            if not fragment:
                continue
            font, font_path = resolve_font(span.font_name, span.font_size, span.bold)
            chosen_fonts[f"{span.font_name}/{span.font_size}/{'bold' if span.bold else 'plain'}"] = font_path
            color = (span.color_r, span.color_g, span.color_b)
            draw.text((x, y), fragment, fill=(*color, 255), font=font)
            bbox = draw.textbbox((x, y), fragment, font=font)
            x = bbox[2]
        y += max(1, parsed.font_size + 1)
        line_start = line_end + 1

    out_path.parent.mkdir(parents=True, exist_ok=True)
    image.save(out_path)
    metadata = {
        "image": str(out_path),
        "width": width,
        "height": height,
        "parsed": {
            **asdict(parsed),
            "spans": [asdict(span) for span in parsed.spans],
        },
        "resolvedColor": asdict(resolved_color),
        "movieColorContext": asdict(color_context) if color_context is not None else None,
        "chosenFonts": chosen_fonts,
        "background": background,
    }
    json_path = out_path.with_suffix(".json")
    json_path.write_text(json.dumps(metadata, indent=2), encoding="utf-8")
    return metadata


def render_one(
    args: argparse.Namespace,
    xmed_path: Path,
    out_path: Path,
    width: int,
    height: int,
    name: str | None = None,
    preset_context: MovieColorContext | None = None,
) -> dict:
    data = xmed_path.read_bytes()
    parsed = parse_xmed(data)
    context = preset_context or color_context_from_args(args)
    metadata = render(parsed, width, height, out_path, context, args.background)
    metadata["source"] = str(xmed_path)
    metadata["name"] = name or xmed_path.stem
    out_path.with_suffix(".json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
    return metadata


def color_context_from_args(args: argparse.Namespace) -> MovieColorContext | None:
    if args.ink is None and args.fore_color is None:
        return None
    fore = parse_rgb(args.fore_color or "000000")
    return MovieColorContext(args.ink if args.ink is not None else 36, *fore, args.fore_color is not None)


def main() -> int:
    parser = argparse.ArgumentParser(description="Render old Director XMED text chunks to PNG.")
    parser.add_argument("--xmed", type=Path, help="Path to a raw XMED chunk.")
    parser.add_argument("--width", type=int, default=200)
    parser.add_argument("--height", type=int, default=20)
    parser.add_argument("--out", type=Path, help="Output PNG path.")
    parser.add_argument("--preset", choices=sorted(PRESETS), help="Render a known fixture set.")
    parser.add_argument("--ink", type=int, help="Movie sprite ink code for final color resolution, e.g. 36.")
    parser.add_argument("--fore-color", help="Movie sprite foreColor as RRGGBB for final color resolution.")
    parser.add_argument("--background", help="Optional opaque RGB background, e.g. ffffff.")
    args = parser.parse_args()

    rendered = []
    if args.preset:
        for item in PRESETS[args.preset]:
            xmed_path = Path(item["xmed"])
            out_path = DEFAULT_OUT_DIR / f"{item['name']}.png"
            fore = parse_rgb(str(item.get("fore_color", "000000")))
            context = MovieColorContext(int(item.get("ink", 36)), *fore, "fore_color" in item)
            rendered.append(
                render_one(
                    args,
                    xmed_path,
                    out_path,
                    int(item["width"]),
                    int(item["height"]),
                    str(item["name"]),
                    context,
                )
            )
    else:
        if args.xmed is None:
            parser.error("--xmed is required when --preset is not used")
        out_path = args.out or (DEFAULT_OUT_DIR / f"{args.xmed.stem}.png")
        rendered.append(render_one(args, args.xmed, out_path, args.width, args.height))

    print(json.dumps({"rendered": rendered}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
