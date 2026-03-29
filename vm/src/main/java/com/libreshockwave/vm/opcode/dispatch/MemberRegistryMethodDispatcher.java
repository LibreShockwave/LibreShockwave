package com.libreshockwave.vm.opcode.dispatch;

import com.libreshockwave.id.SlotId;
import com.libreshockwave.vm.LingoVM;
import com.libreshockwave.vm.builtin.cast.CastLibProvider;
import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.util.AncestorChainWalker;

import java.util.IdentityHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.function.Function;

public final class MemberRegistryMethodDispatcher {

    static final DispatchResult NOT_HANDLED = new DispatchResult(false, Datum.VOID);
    private static final Map<Datum.ScriptInstance, Map<Integer, String>> persistentAliasTextByRegistry =
            java.util.Collections.synchronizedMap(new IdentityHashMap<>());

    private MemberRegistryMethodDispatcher() {}

    static DispatchResult dispatch(Datum.ScriptInstance instance, String methodName, List<Datum> args) {
        Datum.PropList registry = getRegistry(instance);
        if (registry == null || methodName == null || methodName.isEmpty()) {
            return NOT_HANDLED;
        }

        return switch (methodName.toLowerCase(Locale.ROOT)) {
            case "getmemnum" -> new DispatchResult(true, Datum.of(resolveRegisteredMemberSlot(registry, args)));
            case "exists", "memberexists" -> new DispatchResult(
                    true,
                    Math.abs(resolveRegisteredMemberSlot(registry, args)) > 0 ? Datum.TRUE : Datum.FALSE);
            case "getmember" -> new DispatchResult(true, resolveRegisteredMember(registry, args));
            case "readaliasindexesfromfield" -> dispatchReadAliasIndexesFromField(instance, registry, args);
            default -> NOT_HANDLED;
        };
    }

    private static DispatchResult dispatchReadAliasIndexesFromField(
            Datum.ScriptInstance instance,
            Datum.PropList registry,
            List<Datum> args) {
        CastLibProvider provider = CastLibProvider.getProvider();
        if (provider == null || args.size() < 2) {
            return new DispatchResult(true, Datum.ZERO);
        }

        Object fieldIdentifier = args.get(0) instanceof Datum.Int i ? i.value() : args.get(0).toStr();
        int castLibNumber = args.get(1).toInt();
        Datum fieldDatum = provider.getFieldDatum(fieldIdentifier, castLibNumber);
        if (fieldDatum == null || fieldDatum.isVoid()) {
            return new DispatchResult(true, Datum.ZERO);
        }

        rememberAliasText(instance, castLibNumber, fieldDatum.toStr());
        int imported = applyAliasMappings(registry, fieldDatum.toStr(),
                targetName -> resolveTargetMemberNumber(registry, targetName));
        return new DispatchResult(true, Datum.of(imported));
    }

    private static Datum.PropList getRegistry(Datum.ScriptInstance instance) {
        if (instance == null) {
            return null;
        }
        Datum registryDatum = AncestorChainWalker.getProperty(instance, "pAllMemNumList");
        return registryDatum instanceof Datum.PropList registry ? registry : null;
    }

    private static int resolveRegisteredMemberSlot(Datum.PropList registry, List<Datum> args) {
        if (registry == null || args == null || args.isEmpty()) {
            return 0;
        }

        Datum memberIdentifier = args.get(0);
        if (memberIdentifier == null || memberIdentifier.isVoid()) {
            return 0;
        }

        if (memberIdentifier.isInt() || memberIdentifier.isFloat()) {
            return memberIdentifier.toInt();
        }

        String memberName = memberIdentifier.toStr();
        if (memberName.isEmpty()) {
            return 0;
        }

        Datum registered = registry.get(memberName);
        if (registered != null && !registered.isVoid()) {
            return registered.toInt();
        }

        int resolvedSlot = resolveMemberSlotByName(memberName);
        if (resolvedSlot > 0) {
            registry.putTyped(memberName, false, Datum.of(resolvedSlot));
        }
        return resolvedSlot;
    }

    private static Datum resolveRegisteredMember(Datum.PropList registry, List<Datum> args) {
        int slotValue = resolveRegisteredMemberSlot(registry, args);
        if (slotValue == 0) {
            LingoVM vm = LingoVM.getCurrentVM();
            if (vm != null) {
                return vm.callBuiltin("member", List.of(Datum.ZERO));
            }
            return Datum.CastMemberRef.of(1, 0);
        }

        int normalizedSlot = Math.abs(slotValue);
        LingoVM vm = LingoVM.getCurrentVM();
        if (vm != null) {
            return vm.callBuiltin("member", List.of(Datum.of(normalizedSlot)));
        }

        SlotId slotId = new SlotId(normalizedSlot);
        CastLibProvider provider = CastLibProvider.getProvider();
        if (provider == null) {
            return Datum.CastMemberRef.of(slotId.castLib(), slotId.member());
        }
        return provider.getMember(slotId.castLib(), slotId.member());
    }

    private static int resolveMemberSlotByName(String memberName) {
        CastLibProvider provider = CastLibProvider.getProvider();
        if (provider == null || memberName == null || memberName.isEmpty()) {
            return 0;
        }

        Datum memberRef = provider.getMemberByName(0, memberName);
        if (!(memberRef instanceof Datum.CastMemberRef cmr) || cmr.castLibNum() < 1 || cmr.memberNum() < 1) {
            return 0;
        }
        return SlotId.of(cmr.castLibNum(), cmr.memberNum()).value();
    }

    public static int reapplyPersistentAliases(int castLibNumber) {
        if (castLibNumber <= 0) {
            return 0;
        }
        int imported = 0;
        synchronized (persistentAliasTextByRegistry) {
            for (var entry : persistentAliasTextByRegistry.entrySet()) {
                Datum.ScriptInstance registryOwner = entry.getKey();
                if (registryOwner == null) {
                    continue;
                }
                String aliasText = entry.getValue().get(castLibNumber);
                if (aliasText == null || aliasText.isEmpty()) {
                    continue;
                }
                Datum registryDatum = AncestorChainWalker.getProperty(registryOwner, "pAllMemNumList");
                if (!(registryDatum instanceof Datum.PropList registry)) {
                    continue;
                }
                imported += applyAliasMappings(registry, aliasText,
                        targetName -> resolveTargetMemberNumber(registry, targetName));
            }
        }
        return imported;
    }

    static void clearRememberedAliases() {
        persistentAliasTextByRegistry.clear();
    }

    static int applyAliasMappings(Datum.PropList registry, String aliasText, Function<String, Integer> resolver) {
        if (registry == null || aliasText == null || aliasText.isEmpty() || resolver == null) {
            return 0;
        }

        int imported = 0;
        for (String rawLine : aliasText.split("\\r\\n|\\r|\\n")) {
            if (rawLine == null || rawLine.length() <= 2) {
                continue;
            }

            int delimiter = rawLine.indexOf('=');
            if (delimiter <= 0 || delimiter >= rawLine.length() - 1) {
                continue;
            }

            String aliasName = rawLine.substring(0, delimiter);
            String targetName = rawLine.substring(delimiter + 1);
            if (aliasName.isEmpty() || targetName.isEmpty()) {
                continue;
            }

            boolean mirrored = targetName.charAt(targetName.length() - 1) == '*';
            if (mirrored) {
                targetName = targetName.substring(0, targetName.length() - 1);
            }
            if (targetName.isEmpty()) {
                continue;
            }

            int resolvedNumber = resolver.apply(targetName);
            if (resolvedNumber <= 0) {
                continue;
            }

            registry.putTyped(aliasName, false, Datum.of(mirrored ? -resolvedNumber : resolvedNumber));
            imported++;
        }
        return imported;
    }

    private static void rememberAliasText(Datum.ScriptInstance instance, int castLibNumber, String aliasText) {
        if (instance == null || castLibNumber <= 0 || aliasText == null || aliasText.isEmpty()) {
            return;
        }
        synchronized (persistentAliasTextByRegistry) {
            persistentAliasTextByRegistry
                    .computeIfAbsent(instance, ignored -> new java.util.LinkedHashMap<>())
                    .put(castLibNumber, aliasText);
        }
    }

    private static int resolveTargetMemberNumber(Datum.PropList registry, String targetName) {
        Datum existing = registry.get(targetName);
        if (existing == null || existing.isVoid()) {
            return 0;
        }
        return Math.abs(existing.toInt());
    }

    record DispatchResult(boolean handled, Datum value) {}
}
