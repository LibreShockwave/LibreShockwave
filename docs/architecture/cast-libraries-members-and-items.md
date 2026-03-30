# Cast Libraries, Members, And Items

## 1. Why This Subsystem Matters

In Director-style runtimes, casts are not just asset folders. They are the boundary where scripts, media, text, palettes, shapes, and metadata become addressable runtime objects.

LibreShockwave reflects that. The cast subsystem is one of the most important connective layers in the emulator because it sits between:

- parsed file resources
- script-visible member references
- runtime sprite binding
- dynamic member creation
- external cast loading
- item metadata such as `.props` text resources

## 2. Cast Library Lifecycle

`CastLibManager` is responsible for constructing and resolving cast libraries. A cast library can exist in different practical states:

- internal and already available from the main movie
- external but not yet fetched
- being fetched
- loaded and integrated into runtime state

The manager does more than index libraries. It also:

- resolves lookup by cast library and member identity
- stores raw external cast bytes for reuse
- coordinates external cast retargeting
- exposes field-text and script lookup helpers

This is important because cast visibility can change over time as external assets arrive.

## 3. Stable Registry Versus Runtime Namespace

One subtle but important design detail in the current implementation is the separation between a stable registry view and broader runtime lookup behavior.

In plain terms:

- some lookups should only consider members that are stably visible as named resources
- other lookups can consider runtime-retargeted or dynamically injected state

That distinction prevents temporary cast retargeting from leaking into movie-global resource resolution in places where a stable named member is expected.

This is a professional design choice. Without it, dynamic retargeting would cause hard-to-debug name collisions and stale resource visibility.

## 4. Member Model

`CastMember` acts as the runtime wrapper around both file-backed and runtime-created members.

Current responsibilities include:

- exposing bitmap, text, palette, script, shape, and xtra-like content
- holding runtime text overrides
- exposing live image references for bitmap members
- tracking palette overrides and palette revision state
- participating in dynamic member retirement and cleanup

One especially useful behavior is that `member.image` can remain a live reference for mutable bitmap content. That means scripts can modify image-bearing members without forcing the runtime to replace every reference site manually.

## 5. Dynamic Members

LibreShockwave does not treat the cast as fully static. It supports runtime-created dynamic members.

The current model includes:

- dynamic slot allocation at high member numbers
- reuse of retired dynamic slots where safe
- preservation of dynamic members across external cast reloads
- explicit retirement and cleanup when a dynamic member is erased

This is central to Director compatibility because many movies create or repurpose members at runtime, especially for generated images, temporary text, UI states, and item visuals.

## 6. Sprite Binding To Members

A sprite is not the same object as its cast member. The sprite references a member, but runtime state can temporarily override or detach that binding.

Important current behaviors include:

- score-backed defaults establish the authored member relationship
- dynamic member overrides can replace the effective member without rewriting authored score data
- sprite state can be rebound when the score identity changes
- cleanup paths clear stale bindings when a dynamic member slot is retired

This separation is why a sprite can preserve some runtime state while still following timeline ownership rules.

## 7. Item Metadata And `.props`

Items, furniture definitions, and similar movie-specific metadata are not implemented as one dedicated "items engine". In the current architecture, they are mostly represented through normal cast/member mechanisms.

In practice, this means:

- item metadata often lives in text field members such as `.props` resources
- field text can be read as plain text and then parsed into Lingo values
- scripts can interpret those property lists to decide how an item should render or behave

This is an important architectural observation: item behavior is mostly data-driven and script-driven on top of cast resources, not a hardcoded Java subsystem with bespoke item classes for every furniture type.

## 8. External Cast Retargeting

The runtime supports retargeting a cast library's backing file. That creates tricky lifetime problems:

- previously visible members may no longer be valid
- cached resources may now describe the wrong file
- dynamic members should often survive even if file-backed content changes

The current implementation addresses this by invalidating the old file-backed binding while preserving runtime-created dynamic members when appropriate.

This is one of the more sophisticated parts of the cast system because it handles the difference between authored external assets and movie-generated runtime assets.

## 9. Empty And Placeholder Members

The member layer also contains compatibility-driven cleanup rules for empty placeholders. For example, empty placeholder text members should not accidentally behave like real named resources.

That prevents bugs where:

- `memberExists()` returns true for placeholder slots
- registry lookups resolve blank members as if they were intentional assets
- item/property scans observe garbage names that only exist because of placeholder serialization

Those details matter more than they first appear, especially in movies that use large resource tables with sparse occupancy.

## 10. Test-Backed Behavior

This part of the emulator benefits from meaningful test evidence.

Current tests indicate support for behaviors such as:

- `.props` fields loading as text resources and being parseable into property lists
- `.props` values being consumable through normal Lingo value parsing rather than a custom Java-only item parser
- external cast retargeting clearing stale property visibility until reload
- dynamic member lifecycle cleanup
- sprite transform and member reset behavior when bindings change
- public-room ink behavior around room assets and transparency

That makes the cast/item subsystem more trustworthy than a purely theoretical reading would suggest. It also reinforces the architectural conclusion that items are mostly an interaction between cast content, sprite state, and Lingo interpretation rather than a missing Java "item engine".

## 11. Practical Summary

The cleanest way to think about items in LibreShockwave is:

- casts define addressable resources
- members wrap those resources in runtime-visible objects
- sprites bind to members and can temporarily override them
- `.props` and related metadata are script-consumed cast content
- dynamic members let the movie create new runtime assets without patching the authored file

So "items" are real, but they mostly emerge from the interaction of cast libraries, text metadata, sprites, and Lingo rather than from a single Java package named after furniture.

## Confidence Score

- Cast library lifecycle: `9.0/10`
- Dynamic member and rebinding model: `8.9/10`
- Item and `.props` interpretation model: `8.8/10`

Reason for score: the cast and member flow is strongly evidenced by `CastLibManager`, `CastLib`, `CastMember`, sprite lifecycle code, and targeted tests around props, retargeting, and runtime member cleanup. Confidence is still slightly lower around item semantics because many final behaviors depend on movie scripts interpreting the metadata, but the test coverage makes that subsystem more concrete than a purely inferential reading.
