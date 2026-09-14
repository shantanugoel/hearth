"""PCM16 mono WAV helpers. No third-party audio libraries."""

from __future__ import annotations

import struct

PCM_RATE = 16000
PCM_WIDTH = 2
PCM_CHANNELS = 1


def wav_header(data_bytes: int, rate: int = PCM_RATE) -> bytes:
    byte_rate = rate * PCM_CHANNELS * PCM_WIDTH
    block_align = PCM_CHANNELS * PCM_WIDTH
    return struct.pack(
        "<4sI4s4sIHHIIHH4sI",
        b"RIFF",
        36 + data_bytes,
        b"WAVE",
        b"fmt ",
        16,
        1,
        PCM_CHANNELS,
        rate,
        byte_rate,
        block_align,
        16,
        b"data",
        data_bytes,
    )


def wrap_pcm16(pcm: bytes, rate: int = PCM_RATE) -> bytes:
    return wav_header(len(pcm), rate) + pcm


def parse_wav(blob: bytes) -> tuple[bytes, int]:
    """Return (pcm16le, sample_rate) from a PCM WAV. Rejects non-PCM."""
    if len(blob) < 44 or blob[0:4] != b"RIFF" or blob[8:12] != b"WAVE":
        raise ValueError("not a RIFF/WAVE file")
    offset = 12
    fmt = None
    data = None
    while offset + 8 <= len(blob):
        chunk_id = blob[offset : offset + 4]
        (chunk_size,) = struct.unpack_from("<I", blob, offset + 4)
        start = offset + 8
        payload = blob[start : start + chunk_size]
        if chunk_id == b"fmt ":
            fmt = payload
        elif chunk_id == b"data":
            data = payload
        offset = start + chunk_size
        if chunk_size % 2:
            offset += 1
    if fmt is None or data is None:
        raise ValueError("WAVE missing fmt or data")
    audio_format, channels, rate, _byte_rate, _align, bits = struct.unpack_from(
        "<HHIIHH", fmt
    )
    if audio_format != 1 or channels != 1 or bits != 16:
        raise ValueError(
            f"need PCM16 mono, got format={audio_format} ch={channels} bits={bits}"
        )
    return data, rate


def clean_asr_text(text: str) -> str:
    """Qwen3-ASR wraps transcripts as `language En<asr_text>hello`."""
    marker = "<asr_text>"
    if marker in text:
        text = text.split(marker, 1)[1]
    text = text.replace("</asr_text>", "")
    text = text.strip()
    lowered = text.lower()
    if lowered in ("", "none", "null"):
        return ""
    return text
