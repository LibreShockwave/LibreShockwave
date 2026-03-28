package com.libreshockwave.vm.builtin.cast;

import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.LingoVM;

import java.util.List;
import java.util.Map;
import java.util.function.BiFunction;

/**
 * Cast library builtin functions for Lingo.
 * Similar to dirplayer-rs player/handlers/cast.rs.
 *
 * Provides:
 * - castLib(number) or castLib("name") - get cast library reference
 * - member(number) or member("name") - get cast member reference
 * - member(number, castLib) - get member from specific cast library
 */
public final class CastLibBuiltins {

    private CastLibBuiltins() {}

    public static void register(Map<String, BiFunction<LingoVM, List<Datum>, Datum>> builtins) {
        builtins.put("castlib", CastLibBuiltins::castLib);
        builtins.put("member", CastLibBuiltins::member);
        builtins.put("field", CastLibBuiltins::field);
        // NOTE: Do NOT register getmemnum/memberExists as builtins.
        // The fuse_client defines these as Lingo movie script handlers that
        // delegate to the Resource Manager Class, which tracks members via
        // pAllMemNumList (a runtime registry). The Java builtins would bypass
        // this registry and return different results, causing null crashes.
    }

    /**
     * castLib(number) or castLib("name")
     * Returns a cast library reference.
     */
    private static Datum castLib(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) {
            return Datum.VOID;
        }

        CastLibProvider provider = CastLibProvider.getProvider();
        if (provider == null) {
            return Datum.VOID;
        }

        Datum arg = args.get(0);
        int castLibNumber;

        if (arg.isInt() || arg.isFloat()) {
            castLibNumber = provider.getCastLibByNumber(arg.toInt());
        } else if (arg.isString() || arg.isSymbol()) {
            castLibNumber = provider.getCastLibByName(arg.toStr());
        } else {
            return Datum.VOID;
        }

        if (castLibNumber < 0) {
            return Datum.VOID;
        }

        return Datum.CastLibRef.of(castLibNumber);
    }

    /**
     * member(number) - get member from default cast
     * member("name") - find member by name in all casts
     * member(number, castLib) - get member from specific cast
     * member("name", castLib) - find member by name in specific cast
     */
    private static Datum member(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) {
            return Datum.VOID;
        }

        CastLibProvider provider = CastLibProvider.getProvider();
        if (provider == null) {
            // Return a placeholder CastMemberRef
            return Datum.CastMemberRef.of(1, args.get(0).toInt());
        }

        Datum memberArg = args.get(0);
        int castLibNumber = 0; // 0 = search all or use default

        castLibNumber = resolveCastLibArg(provider, args.size() > 1 ? args.get(1) : Datum.VOID);

        // Get member by number or name
        if (memberArg.isInt() || memberArg.isFloat()) {
            int memberNumber = memberArg.toInt();
            if (castLibNumber > 0) {
                return provider.getMember(castLibNumber, memberNumber);
            }

            // Check if this is a slot number (castLib << 16 | memberNum) from member.number
            int encodedCast = (memberNumber >> 16) & 0xFFFF;
            int encodedMember = memberNumber & 0xFFFF;
            if (encodedCast > 0 && encodedMember > 0) {
                // Decode slot number: direct lookup in the encoded cast lib
                if (provider.memberExists(encodedCast, encodedMember)) {
                    return provider.getMember(encodedCast, encodedMember);
                }
            }

            // No cast lib specified — search all casts for the member
            int totalCasts = provider.getCastLibCount();
            for (int i = 1; i <= totalCasts; i++) {
                if (provider.memberExists(i, memberNumber)) {
                    return provider.getMember(i, memberNumber);
                }
            }
            // Not found in any cast — return ref in cast 1 (Director fallback)
            return provider.getMember(1, memberNumber);
        } else if (memberArg.isString() || memberArg.isSymbol()) {
            return provider.getMemberByName(castLibNumber, memberArg.toStr());
        }

        return Datum.VOID;
    }

    /**
     * field(nameOrNum) or field(nameOrNum, castLib)
     * Returns the text content of a field/text cast member.
     * In Director, field("name") is equivalent to member("name").text
     */
    private static Datum field(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) {
            return Datum.EMPTY_STRING;
        }

        CastLibProvider provider = CastLibProvider.getProvider();
        if (provider == null) {
            return Datum.EMPTY_STRING;
        }

        Datum fieldArg = args.get(0);
        int castId = 0; // 0 = search all casts

        castId = resolveCastLibArg(provider, args.size() > 1 ? args.get(1) : Datum.VOID);

        Object identifier = fieldArg instanceof Datum.Str s ? s.value()
                : fieldArg instanceof Datum.Int i ? i.value()
                : fieldArg.toStr();

        String fieldValue = provider.getFieldValue(identifier, castId);
        return Datum.of(fieldValue);
    }

    /**
     * getmemnum("name") - returns the encoded slot number of a member.
     * In Director, this returns (castLib << 16) | memberNum, which can be
     * passed to member() to get the cast member reference.
     * Returns 0 if the member is not found.
     */
    private static Datum getMemNum(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) {
            return Datum.of(0);
        }

        CastLibProvider provider = CastLibProvider.getProvider();
        if (provider == null) {
            return Datum.of(0);
        }

        String memberName = args.get(0).toStr();
        Datum ref = provider.getMemberByName(0, memberName);

        if (ref instanceof Datum.CastMemberRef cmr) {
            return Datum.of((cmr.castLibNum() << 16) | cmr.memberNum());
        }

        return Datum.of(0);
    }

    /**
     * memberExists("name") - checks if a member with the given name exists.
     * Returns TRUE (1) or FALSE (0).
     */
    private static Datum memberExistsBuiltin(LingoVM vm, List<Datum> args) {
        if (args.isEmpty()) {
            return Datum.of(0);
        }

        CastLibProvider provider = CastLibProvider.getProvider();
        if (provider == null) {
            return Datum.of(0);
        }

        String memberName = args.get(0).toStr();
        Datum ref = provider.getMemberByName(0, memberName);

        if (ref instanceof Datum.CastMemberRef) {
            return Datum.of(1);
        }

        return Datum.of(0);
    }

    private static int resolveCastLibArg(CastLibProvider provider, Datum castArg) {
        if (provider == null || castArg == null || castArg.isVoid()) {
            return 0;
        }
        if (castArg instanceof Datum.CastLibRef clr) {
            return clr.castLibNum();
        }
        if (castArg.isInt() || castArg.isFloat()) {
            int castLibNumber = provider.getCastLibByNumber(castArg.toInt());
            return castLibNumber >= 0 ? castLibNumber : 0;
        }
        if (castArg.isString() || castArg.isSymbol()) {
            int castLibNumber = provider.getCastLibByName(castArg.toStr());
            return castLibNumber >= 0 ? castLibNumber : 0;
        }
        return 0;
    }

}
