package com.libreshockwave.vm.builtin.sprite;

import com.libreshockwave.vm.datum.Datum;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * Lightweight sprite event-broker support for sprite ref method dispatch.
 */
public final class SpriteEventBrokerSupport {

    public static final String SYNTHETIC_BROKER_FLAG = "__spriteEventBroker__";

    private static final AtomicInteger NEXT_SYNTHETIC_INSTANCE_ID = new AtomicInteger(1_000_000);

    private SpriteEventBrokerSupport() {}

    public static Datum dispatchSpriteMethod(int channel, String methodName, List<Datum> args) {
        SpritePropertyProvider provider = SpritePropertyProvider.getProvider();
        if (provider == null || channel <= 0 || methodName == null) {
            return Datum.VOID;
        }

        String method = methodName.toLowerCase(Locale.ROOT);
        return switch (method) {
            case "registerprocedure" -> registerProcedure(provider, channel, args);
            case "removeprocedure" -> removeProcedure(provider, channel, args);
            case "setid" -> setId(provider, channel, args);
            case "getid" -> getBrokerProperty(provider, channel, "id");
            case "setlink" -> setLink(provider, channel, args);
            case "getlink" -> getBrokerProperty(provider, channel, "pLink");
            case "setcursor" -> setCursor(provider, channel, args);
            case "getcursor" -> provider.getSpriteProp(channel, "cursor");
            case "setmember" -> setMember(provider, channel, args);
            case "getmember" -> provider.getSpriteProp(channel, "member");
            default -> Datum.VOID;
        };
    }

    public static List<Datum> getOrCreateBrokerScriptList(SpritePropertyProvider provider, int channel) {
        List<Datum> existing = provider.getScriptInstanceList(channel);
        if (existing != null && !existing.isEmpty()) {
            return existing;
        }

        Datum.ScriptInstance synthetic = createSyntheticBroker(channel);
        List<Datum> list = new ArrayList<>();
        list.add(synthetic);
        provider.setSpriteProp(channel, "scriptinstancelist", new Datum.List(list));
        return provider.getScriptInstanceList(channel);
    }

    private static Datum registerProcedure(SpritePropertyProvider provider, int channel, List<Datum> args) {
        Datum.ScriptInstance broker = getPrimaryBroker(provider, channel);
        if (broker == null) {
            return Datum.ZERO;
        }

        Datum methodDatum = args.size() > 0 ? args.get(0) : Datum.VOID;
        Datum clientId = args.size() > 1 ? args.get(1) : Datum.ZERO;
        Datum eventDatum = args.size() > 2 ? args.get(2) : Datum.VOID;

        Datum.PropList procList = ensureProcList(broker);

        if (eventDatum.isVoid() && methodDatum.isVoid()) {
            for (int i = 0; i < procList.size(); i++) {
                String key = procList.getKey(i);
                procList.put(key, true, new Datum.List(List.of(new Datum.Symbol(key), clientId)));
            }
            return Datum.TRUE;
        }

        if (eventDatum.isVoid()) {
            for (int i = 0; i < procList.size(); i++) {
                String key = procList.getKey(i);
                procList.put(key, true, new Datum.List(List.of(methodDatum, clientId)));
            }
            return Datum.TRUE;
        }

        Datum resolvedMethod = methodDatum.isVoid() ? eventDatum : methodDatum;
        String eventKey = eventDatum.toKeyName();
        procList.put(eventKey, true, new Datum.List(List.of(resolvedMethod, clientId)));
        return Datum.TRUE;
    }

    private static Datum removeProcedure(SpritePropertyProvider provider, int channel, List<Datum> args) {
        Datum.ScriptInstance broker = getPrimaryBroker(provider, channel);
        if (broker == null) {
            return Datum.ZERO;
        }

        Datum.PropList procList = ensureProcList(broker);
        Datum eventDatum = args.isEmpty() ? Datum.VOID : args.get(0);
        if (eventDatum.isVoid()) {
            broker.properties().put("pProcList", createProcListTemplate());
            return Datum.TRUE;
        }

        String eventKey = eventDatum.toKeyName();
        procList.put(eventKey, true, new Datum.List(List.of(Datum.symbol("null"), Datum.ZERO)));
        return Datum.TRUE;
    }

    private static Datum setId(SpritePropertyProvider provider, int channel, List<Datum> args) {
        Datum.ScriptInstance broker = getPrimaryBroker(provider, channel);
        if (broker == null || args.isEmpty()) {
            return Datum.ZERO;
        }
        broker.properties().put("id", args.get(0));
        return Datum.TRUE;
    }

    private static Datum setLink(SpritePropertyProvider provider, int channel, List<Datum> args) {
        Datum.ScriptInstance broker = getPrimaryBroker(provider, channel);
        if (broker == null || args.isEmpty()) {
            return Datum.ZERO;
        }
        broker.properties().put("pLink", args.get(0));
        return Datum.TRUE;
    }

    private static Datum setCursor(SpritePropertyProvider provider, int channel, List<Datum> args) {
        if (args.isEmpty()) {
            return Datum.ZERO;
        }
        provider.setSpriteProp(channel, "cursor", args.get(0));
        return Datum.TRUE;
    }

    private static Datum setMember(SpritePropertyProvider provider, int channel, List<Datum> args) {
        if (args.isEmpty()) {
            return Datum.ZERO;
        }
        provider.setSpriteMember(channel, args.get(0));
        return Datum.TRUE;
    }

    private static Datum getBrokerProperty(SpritePropertyProvider provider, int channel, String prop) {
        Datum.ScriptInstance broker = getPrimaryBroker(provider, channel);
        if (broker == null) {
            return Datum.VOID;
        }
        return broker.properties().getOrDefault(prop, Datum.VOID);
    }

    private static Datum.ScriptInstance getPrimaryBroker(SpritePropertyProvider provider, int channel) {
        List<Datum> scripts = getOrCreateBrokerScriptList(provider, channel);
        if (scripts == null || scripts.isEmpty()) {
            return null;
        }
        for (Datum script : scripts) {
            if (script instanceof Datum.ScriptInstance instance) {
                return instance;
            }
        }
        return null;
    }

    private static Datum.PropList ensureProcList(Datum.ScriptInstance broker) {
        Datum procListDatum = broker.properties().get("pProcList");
        if (procListDatum instanceof Datum.PropList procList) {
            return procList;
        }
        Datum.PropList created = createProcListTemplate();
        broker.properties().put("pProcList", created);
        return created;
    }

    private static Datum.ScriptInstance createSyntheticBroker(int channel) {
        Map<String, Datum> properties = new LinkedHashMap<>();
        properties.put("spritenum", Datum.of(channel));
        properties.put("pProcList", createProcListTemplate());
        properties.put("pLink", Datum.VOID);
        properties.put(SYNTHETIC_BROKER_FLAG, Datum.TRUE);
        return new Datum.ScriptInstance(NEXT_SYNTHETIC_INSTANCE_ID.incrementAndGet(), properties);
    }

    private static Datum.PropList createProcListTemplate() {
        Datum.PropList list = new Datum.PropList();
        addProcEntry(list, "mouseEnter");
        addProcEntry(list, "mouseLeave");
        addProcEntry(list, "mouseWithin");
        addProcEntry(list, "mouseDown");
        addProcEntry(list, "mouseUp");
        addProcEntry(list, "mouseUpOutSide");
        addProcEntry(list, "keyDown");
        addProcEntry(list, "keyUp");
        return list;
    }

    private static void addProcEntry(Datum.PropList list, String eventName) {
        list.put(eventName, true, new Datum.List(List.of(Datum.symbol("null"), Datum.ZERO)));
    }
}
