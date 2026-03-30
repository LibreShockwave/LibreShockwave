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
    private static final String MEMBER_ALIAS_INDEX = "memberalias.index";
    private static final Map<Datum.ScriptInstance, Map<Integer, String>> persistentAliasTextByRegistry =
            java.util.Collections.synchronizedMap(new IdentityHashMap<>());

    private MemberRegistryMethodDispatcher() {}

    static DispatchResult prefill(Datum.ScriptInstance instance, String methodName, List<Datum> args) {
        Datum.PropList registry = getRegistry(instance);
        if (registry == null || methodName == null || methodName.isEmpty()) {
            return NOT_HANDLED;
        }

        switch (methodName.toLowerCase(Locale.ROOT)) {
            case "getmemnum", "exists", "memberexists", "getmember" ->
                    resolveRegisteredMemberSlot(instance, registry, args, true);
            default -> {
                return NOT_HANDLED;
            }
        }
        return NOT_HANDLED;
    }

    static DispatchResult dispatch(Datum.ScriptInstance instance, String methodName, List<Datum> args) {
        Datum.PropList registry = getRegistry(instance);
        if (registry == null || methodName == null || methodName.isEmpty()) {
            return NOT_HANDLED;
        }

        return switch (methodName.toLowerCase(Locale.ROOT)) {
            case "getmemnum" -> new DispatchResult(true, Datum.of(resolveRegisteredMemberSlot(instance, registry, args, true)));
            case "exists", "memberexists" -> new DispatchResult(
                    true,
                    Math.abs(resolveRegisteredMemberSlot(instance, registry, args, true)) > 0 ? Datum.TRUE : Datum.FALSE);
            case "getmember" -> new DispatchResult(true, resolveRegisteredMember(instance, registry, args));
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
                targetName -> resolveTargetMemberNumber(registry, castLibNumber, targetName));
        return new DispatchResult(true, Datum.of(imported));
    }

    private static Datum.PropList getRegistry(Datum.ScriptInstance instance) {
        if (instance == null) {
            return null;
        }
        Datum registryDatum = AncestorChainWalker.getProperty(instance, "pAllMemNumList");
        return registryDatum instanceof Datum.PropList registry ? registry : null;
    }

    private static int resolveRegisteredMemberSlot(
            Datum.ScriptInstance instance,
            Datum.PropList registry,
            List<Datum> args,
            boolean allowDefinitionBootstrapLookup) {
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
            int registeredSlot = registered.toInt();
            if (isRegisteredRegistrySlotLive(registeredSlot)) {
                return registeredSlot;
            }
            registry.remove(memberName);
        }

        refreshAvailableAliasTexts(instance);
        int rememberedAliasSlot = resolveRememberedAliasSlot(instance, registry, memberName);
        if (rememberedAliasSlot != 0) {
            registry.putTyped(memberName, false, Datum.of(rememberedAliasSlot));
            return rememberedAliasSlot;
        }

        int resolvedSlot = resolveMemberSlotByName(memberName, allowDefinitionBootstrapLookup);
        if (resolvedSlot > 0) {
            registry.putTyped(memberName, false, Datum.of(resolvedSlot));
        }
        return resolvedSlot;
    }

    private static Datum resolveRegisteredMember(
            Datum.ScriptInstance instance,
            Datum.PropList registry,
            List<Datum> args) {
        int slotValue = resolveRegisteredMemberSlot(instance, registry, args, true);
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

    private static int resolveMemberSlotByName(String memberName, boolean allowDefinitionBootstrapLookup) {
        CastLibProvider provider = CastLibProvider.getProvider();
        if (provider == null || memberName == null || memberName.isEmpty()) {
            return 0;
        }

        Datum memberRef = provider.getRegistryMemberByName(0, memberName);
        if (memberRef instanceof Datum.CastMemberRef cmr && cmr.castLibNum() >= 1 && cmr.memberNum() >= 1) {
            return SlotId.of(cmr.castLibNum(), cmr.memberNum()).value();
        }

        if (!allowDefinitionBootstrapLookup) {
            return 0;
        }

        Datum bootstrapRef = provider.getMemberByName(0, memberName);
        if (!(bootstrapRef instanceof Datum.CastMemberRef cmr) || cmr.castLibNum() < 1 || cmr.memberNum() < 1) {
            return 0;
        }
        if (!isBootstrapDefinitionMember(provider, cmr.castLibNum(), cmr.memberNum())) {
            return 0;
        }
        return SlotId.of(cmr.castLibNum(), cmr.memberNum()).value();
    }

    private static boolean isBootstrapDefinitionMember(CastLibProvider provider, int castLibNumber, int memberNumber) {
        if (provider == null || castLibNumber <= 0 || memberNumber <= 0) {
            return false;
        }

        Datum type = provider.getMemberProp(castLibNumber, memberNumber, "type");
        if (!(type instanceof Datum.Symbol symbol)) {
            return false;
        }
        String typeName = symbol.name();
        return "script".equalsIgnoreCase(typeName)
                || "field".equalsIgnoreCase(typeName);
    }

    private static boolean isRegisteredRegistrySlotLive(int slotValue) {
        if (slotValue == 0) {
            return false;
        }

        CastLibProvider provider = CastLibProvider.getProvider();
        if (provider == null) {
            return true;
        }

        int absValue = Math.abs(slotValue);
        SlotId slotId = new SlotId(absValue);
        if (slotId.castLib() >= 1 && slotId.member() >= 1) {
            // Combined SlotId with explicit cast library — fast path
            return provider.isRegistryVisibleMember(slotId.castLib(), slotId.member());
        }

        // Raw member number without a cast-library component.
        // Director's preIndexMembers stores plain member.number values which
        // only encode the cast library when there are multiple casts.
        // Scan all cast libraries for the raw member number.
        if (absValue >= 1) {
            int castLibCount = provider.getCastLibCount();
            for (int cl = 1; cl <= castLibCount; cl++) {
                if (provider.isRegistryVisibleMember(cl, absValue)) {
                    return true;
                }
            }
        }
        return false;
    }

    private static int resolveRememberedAliasSlot(
            Datum.ScriptInstance instance,
            Datum.PropList registry,
            String memberName) {
        if (instance == null || registry == null || memberName == null || memberName.isEmpty()) {
            return 0;
        }

        synchronized (persistentAliasTextByRegistry) {
            Map<Integer, String> aliasTexts = persistentAliasTextByRegistry.get(instance);
            if (aliasTexts == null || aliasTexts.isEmpty()) {
                return 0;
            }
            for (var entry : aliasTexts.entrySet()) {
                int castLibNumber = entry.getKey();
                String aliasText = entry.getValue();
                int resolved = resolveAliasSlot(aliasText, memberName,
                        targetName -> resolveTargetMemberNumber(registry, castLibNumber, targetName));
                if (resolved != 0) {
                    return resolved;
                }
            }
        }
        return 0;
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
                        targetName -> resolveTargetMemberNumber(registry, castLibNumber, targetName));
            }
        }
        return imported;
    }

    public static int reapplyAllPersistentAliases() {
        int imported = 0;
        synchronized (persistentAliasTextByRegistry) {
            for (var entry : persistentAliasTextByRegistry.entrySet()) {
                Datum.ScriptInstance registryOwner = entry.getKey();
                if (registryOwner == null) {
                    continue;
                }
                Datum registryDatum = AncestorChainWalker.getProperty(registryOwner, "pAllMemNumList");
                if (!(registryDatum instanceof Datum.PropList registry)) {
                    continue;
                }
                for (var aliasEntry : entry.getValue().entrySet()) {
                    int castLibNumber = aliasEntry.getKey();
                    String aliasText = aliasEntry.getValue();
                    if (aliasText == null || aliasText.isEmpty()) {
                        continue;
                    }
                    imported += applyAliasMappings(registry, aliasText,
                            targetName -> resolveTargetMemberNumber(registry, castLibNumber, targetName));
                }
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

    private static int resolveAliasSlot(String aliasText, String requestedAlias, Function<String, Integer> resolver) {
        if (aliasText == null || aliasText.isEmpty() || requestedAlias == null || requestedAlias.isEmpty() || resolver == null) {
            return 0;
        }

        for (String rawLine : aliasText.split("\\r\\n|\\r|\\n")) {
            if (rawLine == null || rawLine.length() <= 2) {
                continue;
            }

            int delimiter = rawLine.indexOf('=');
            if (delimiter <= 0 || delimiter >= rawLine.length() - 1) {
                continue;
            }

            String aliasName = rawLine.substring(0, delimiter);
            if (!aliasName.equalsIgnoreCase(requestedAlias)) {
                continue;
            }

            String targetName = rawLine.substring(delimiter + 1);
            boolean mirrored = targetName.charAt(targetName.length() - 1) == '*';
            if (mirrored) {
                targetName = targetName.substring(0, targetName.length() - 1);
            }
            if (targetName.isEmpty()) {
                return 0;
            }

            int resolvedNumber = resolver.apply(targetName);
            if (resolvedNumber <= 0) {
                return 0;
            }
            return mirrored ? -resolvedNumber : resolvedNumber;
        }
        return 0;
    }

    private static void rememberAliasText(Datum.ScriptInstance instance, int castLibNumber, String aliasText) {
        if (instance == null || castLibNumber <= 0 || aliasText == null || aliasText.isEmpty()) {
            return;
        }
        synchronized (persistentAliasTextByRegistry) {
            Map<Integer, String> aliasTexts = persistentAliasTextByRegistry
                    .computeIfAbsent(instance, ignored -> new java.util.LinkedHashMap<>());
            aliasTexts.put(castLibNumber, aliasText);
        }
    }

    private static void refreshAvailableAliasTexts(Datum.ScriptInstance instance) {
        if (instance == null) {
            return;
        }

        CastLibProvider provider = CastLibProvider.getProvider();
        if (provider == null) {
            return;
        }

        int castLibCount = provider.getCastLibCount();
        for (int castLibNumber = 1; castLibNumber <= castLibCount; castLibNumber++) {
            Datum aliasMember = provider.getMemberByName(castLibNumber, MEMBER_ALIAS_INDEX);
            if (!(aliasMember instanceof Datum.CastMemberRef)) {
                synchronized (persistentAliasTextByRegistry) {
                    Map<Integer, String> aliasTexts = persistentAliasTextByRegistry.get(instance);
                    if (aliasTexts != null) {
                        aliasTexts.remove(castLibNumber);
                    }
                }
                continue;
            }

            Datum aliasField = provider.getFieldDatum(MEMBER_ALIAS_INDEX, castLibNumber);
            if (aliasField == null || aliasField.isVoid()) {
                synchronized (persistentAliasTextByRegistry) {
                    Map<Integer, String> aliasTexts = persistentAliasTextByRegistry.get(instance);
                    if (aliasTexts != null) {
                        aliasTexts.remove(castLibNumber);
                    }
                }
                continue;
            }

            String aliasText = aliasField.toStr();
            if (aliasText == null || aliasText.isEmpty()) {
                synchronized (persistentAliasTextByRegistry) {
                    Map<Integer, String> aliasTexts = persistentAliasTextByRegistry.get(instance);
                    if (aliasTexts != null) {
                        aliasTexts.remove(castLibNumber);
                    }
                }
                continue;
            }

            rememberAliasText(instance, castLibNumber, aliasText);
        }
    }

    private static int resolveTargetMemberNumber(Datum.PropList registry, int aliasCastLibNumber, String targetName) {
        Datum existing = registry.get(targetName);
        if (existing != null && !existing.isVoid()) {
            int slotValue = Math.abs(existing.toInt());
            if (isRegisteredRegistrySlotLive(slotValue)) {
                return slotValue;
            }
            registry.remove(targetName);
        }

        CastLibProvider provider = CastLibProvider.getProvider();
        if (provider == null) {
            return 0;
        }

        if (aliasCastLibNumber > 0) {
            Datum sourceCastRef = provider.getMemberByName(aliasCastLibNumber, targetName);
            if (sourceCastRef instanceof Datum.CastMemberRef cmr
                    && cmr.castLibNum() >= 1
                    && cmr.memberNum() >= 1
                    && provider.memberExists(cmr.castLibNum(), cmr.memberNum())) {
                int slotValue = SlotId.of(cmr.castLibNum(), cmr.memberNum()).value();
                if (provider.isRegistryVisibleMember(cmr.castLibNum(), cmr.memberNum())) {
                    registry.putTyped(targetName, false, Datum.of(slotValue));
                }
                return slotValue;
            }
        }

        Datum memberRef = provider.getRegistryMemberByName(0, targetName);
        if (!(memberRef instanceof Datum.CastMemberRef cmr)
                || cmr.castLibNum() < 1
                || cmr.memberNum() < 1) {
            return 0;
        }

        int slotValue = SlotId.of(cmr.castLibNum(), cmr.memberNum()).value();
        if (!isRegisteredRegistrySlotLive(slotValue)) {
            return 0;
        }

        registry.putTyped(targetName, false, Datum.of(slotValue));
        return slotValue;
    }

    record DispatchResult(boolean handled, Datum value) {}
}
