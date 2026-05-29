package com.libreshockwave.lookup;

import com.libreshockwave.chunks.CastMemberChunk;
import com.libreshockwave.chunks.ScriptChunk;
import com.libreshockwave.chunks.ScriptContextChunk;

import java.util.List;
import java.util.ArrayList;

/**
 * Provides script lookup functionality.
 * Encapsulates the logic for finding scripts by context ID and getting script types.
 */
public final class ScriptLookup {

    private final List<ScriptChunk> scripts;
    private final List<ScriptContextChunk> scriptContexts;
    private final List<CastMemberChunk> castMembers;

    public ScriptLookup(List<ScriptChunk> scripts, List<ScriptContextChunk> scriptContexts,
                        List<CastMemberChunk> castMembers) {
        this.scripts = scripts;
        this.scriptContexts = scriptContexts;
        this.castMembers = castMembers;
    }

    /**
     * Get a script by its context ID (the scriptId stored in cast members).
     * This uses the ScriptContextChunk (Lctx) to map scriptId to chunk ID.
     * @param scriptId The script ID from the cast member
     * @return The script chunk, or null if not found
     */
    public ScriptChunk getByContextId(int scriptId) {
        List<ScriptChunk> matches = getAllByContextId(scriptId);
        if (!matches.isEmpty()) {
            return matches.get(0);
        }

        return null;
    }

    /**
     * Get all scripts matching a context ID across all Lctx chunks.
     * Older Afterburner movies can carry multiple script contexts whose local
     * indices overlap, so a cast member's scriptId is not globally unique.
     */
    public List<ScriptChunk> getAllByContextId(int scriptId) {
        List<ScriptChunk> matches = new ArrayList<>();
        // scriptId from cast members is 1-based, Lctx entries are 0-based
        int index = scriptId - 1;

        // Search through all script contexts (there can be one per cast library)
        for (ScriptContextChunk ctx : scriptContexts) {
            if (index >= 0 && index < ctx.entries().size()) {
                var entry = ctx.entries().get(index);
                if (entry.id().value() > 0) {
                    for (ScriptChunk script : scripts) {
                        if (script.id().equals(entry.id())) {
                            matches.add(script);
                            break;
                        }
                    }
                }
            }
        }

        // Fallback: try direct match by ID (scriptId is 1-based context index)
        for (ScriptChunk script : scripts) {
            if (script.id().value() == scriptId) {
                matches.add(script);
                break;
            }
        }

        return matches;
    }

    /**
     * Get the reliable script type for a script by looking up its associated cast member.
     * The script type stored in the cast member's specificData is the authoritative source.
     * @param script The script chunk to get the type for
     * @return The script type from the cast member, or null if not found
     */
    public ScriptChunk.ScriptType getScriptType(ScriptChunk script) {
        if (script == null) return null;

        // Find the cast member that references this script
        // We need to find which Lctx index maps to this script's chunk ID,
        // then find the cast member with that scriptId
        for (int ctxIdx = 0; ctxIdx < scriptContexts.size(); ctxIdx++) {
            ScriptContextChunk ctx = scriptContexts.get(ctxIdx);
            for (int i = 0; i < ctx.entries().size(); i++) {
                if (ctx.entries().get(i).id().equals(script.id())) {
                    // Found the entry - scriptId is 1-based
                    int scriptId = i + 1;

                    // Find the cast member with this scriptId
                    for (CastMemberChunk member : castMembers) {
                        if (member.isScript() && member.scriptId() == scriptId) {
                            return member.getScriptType();
                        }
                    }
                }
            }
        }

        return null;
    }
}
