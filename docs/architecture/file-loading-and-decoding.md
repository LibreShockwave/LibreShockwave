# File Loading And Asset Decoding

## 1. Why This Layer Matters

The emulator does not start with sprites or Lingo handlers. It starts with raw movie bytes. The quality of everything downstream depends on how those bytes are classified, indexed, decoded, and retained.

In LibreShockwave, that responsibility is centered in `sdk`, with `DirectorFile` as the main runtime-facing entry point.

## 2. `DirectorFile` As The Parse Boundary

`DirectorFile` is not just a convenience wrapper. It is the bridge between on-disk Director/Shockwave data and the rest of the emulator.

Current responsibilities include:

- loading `.dir`, `.dxr`, `.dcr`, and `.cst` style payloads
- determining endian and container format
- parsing core chunks such as config, key tables, casts, scripts, score, and frame labels
- exposing runtime lookups for cast members, scripts, score metadata, palettes, text, and bitmaps
- keeping enough raw data around to lazily reparse heavyweight chunks later

That last point is especially important. The parser is designed as a long-lived runtime data source, not as a one-shot importer that throws all structural knowledge away after load.

## 3. Raw Director Versus Afterburner

The loader supports both uncompressed movie files and Afterburner-compressed content.

The high-level split is:

- normal files
  - parsed directly from raw chunk layout
- Afterburner files
  - routed through `AfterburnerReader` before individual chunk access behaves like a normal file

Architecturally, that is the correct division. The emulator should not care whether a movie came from a raw or packed container once the chunk map is established.

## 4. Afterburner Reader Responsibilities

`AfterburnerReader` handles the packed `.dcr` path by parsing the Afterburner container structure. The current implementation explicitly reads:

- `Fver`
  - file version metadata
- `Fcdr`
  - compression directory and compression type table
- `ABMP`
  - resource map and chunk metadata
- `FGEI` and the embedded ILS data
  - initial load segment used to recover chunk payloads

Important behaviors:

- zlib decompression is applied where required
- chunk metadata is retained in a map keyed by resource ID
- ILS chunk bodies are cached after extraction
- later chunk requests can rehydrate data on demand from the cached decompressed segment or from offsets into the remaining file body

That means the loader is not forced to decompress everything up front just to serve a later bitmap or text request.

## 5. Lazy Reparsing And Memory Discipline

`DirectorFile` keeps raw file bytes in `dataStore` and stores chunk metadata so that some chunks can be reparsed only when needed.

This is a deliberate tradeoff:

- keep enough source bytes to avoid losing access to evicted chunks
- avoid permanently retaining every decoded object in memory

The runtime also exposes `releaseNonEssentialChunks()`, which currently allows non-essential audio and raw chunks to be discarded while keeping the data needed for rendering and script execution.

So the parser layer is not purely about correctness. It is also one of the emulator's memory-management mechanisms.

## 6. Runtime Lookup Helpers

The rest of the emulator does not usually walk chunk tables directly. `DirectorFile` exposes targeted helpers for high-value queries such as:

- cast member lookup by score index
- cast member lookup by member number
- script lookup by context ID
- script type lookup through authoritative cast metadata
- stage width and height
- score tempo for a specific frame
- total channel count
- XMED styled-text resolution
- bitmap decode helpers

These helpers are important because they centralize file-format knowledge in one layer instead of spreading it across the VM and player.

## 7. Bitmap Decode Path

Bitmap decoding is not a single-format operation.

The current decode path supports at least these cases:

- indexed/palette-backed bitmaps through `BitmapDecoder`
- 32-bit `ediM` data through a pluggable JPEG decoder
- optional `ALFA` data merged into decoded 32-bit content

This design is notable for two reasons:

- JPEG decode is injected through a runtime-provided decoder instead of hardcoding a desktop-only implementation
- palette resolution is kept explicit so the renderer can invalidate processed outputs when palette state changes

That makes the decode layer usable in both JVM and browser-style environments.

## 8. Text And Styled Text Resources

The parser layer also resolves text-bearing resources, not just images.

In particular:

- standard text chunks are available for classic text members
- XMED lookups combine XMED data with cast-member metadata to build a styled text record

This is why `SpriteBaker` can later choose between dynamic runtime text, STXT-like file text, and XMED-backed styled text without needing its own container parser.

## 9. Base Paths And External Resources

`DirectorFile` retains a `basePath`, which matters for external casts and externally addressed resources.

This is more than a convenience string. It is the shared anchor used later by the network/resource layer when a movie references:

- external cast files
- root-relative resources
- authoring-machine paths that need to be normalized into host-usable URLs or filenames

That means file loading and network loading are not separate worlds. The parser establishes identity and base-path context, and the player/network layers use that identity to fetch and install missing resources.

## 10. Parse Safety

The parser also exposes a global parse deadline check for WASM safety. Hot loops can consult that deadline and stop before a hostile or unexpectedly large file monopolizes execution.

This is another sign that `sdk` is written as runtime infrastructure, not as a naive offline parser.

## 11. Practical Summary

The cleanest mental model is:

- `DirectorFile` owns structural truth about the movie file
- `AfterburnerReader` normalizes packed files into chunk-addressable content
- lookup helpers expose stable access points for the player and VM
- bitmap and text decode stay format-aware
- raw data retention and chunk eviction give the runtime a workable memory strategy

Without this layer, the emulator would either become tightly coupled to the raw chunk format everywhere or lose the ability to reason about resources once startup finished.

## Confidence Score

- Parse boundary and chunk model: `9.1/10`
- Afterburner handling: `8.8/10`
- Asset decode and lazy reparse model: `8.8/10`

Reason for score: these conclusions come directly from `DirectorFile` and `AfterburnerReader`, including concrete support for raw and packed files, lazy reparsing, staged chunk extraction, and pluggable bitmap decode behavior. Confidence is slightly lower around total file-format coverage because Director variants and edge-case chunk combinations are broad.
