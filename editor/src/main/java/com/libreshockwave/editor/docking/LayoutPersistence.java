package com.libreshockwave.editor.docking;

import com.google.gson.*;

import javax.swing.*;
import java.io.*;
import java.nio.file.*;
import java.util.List;

/**
 * Saves and loads the docking layout to/from ~/.libreshockwave/layout.json.
 * <p>
 * The tree is serialized as nested JSON:
 * <pre>
 * { "type": "split", "orientation": "horizontal", "fraction": 0.15,
 *   "first": { "type": "leaf", "tabs": ["Tool Palette"] },
 *   "second": { "type": "center", "tabs": ["Script"] } }
 * </pre>
 */
public class LayoutPersistence {

    private static final Path LAYOUT_DIR = Path.of(System.getProperty("user.home"), ".libreshockwave");
    private static final Path LAYOUT_FILE = LAYOUT_DIR.resolve("layout.json");
    private static final Gson GSON = new GsonBuilder().setPrettyPrinting().create();

    /** Serialize the dock tree to JSON and save to disk. */
    public static void save(DockNode root) {
        try {
            Files.createDirectories(LAYOUT_DIR);
            JsonObject json = serializeNode(root);
            Files.writeString(LAYOUT_FILE, GSON.toJson(json));
        } catch (IOException e) {
            System.err.println("Failed to save layout: " + e.getMessage());
        }
    }

    /** Load the layout from disk and replay it into the DockingManager. */
    public static boolean load(DockingManager manager) {
        if (!Files.exists(LAYOUT_FILE)) return false;
        try {
            String json = Files.readString(LAYOUT_FILE);
            JsonObject root = JsonParser.parseString(json).getAsJsonObject();
            replayNode(root, manager);
            return true;
        } catch (Exception e) {
            System.err.println("Failed to load layout: " + e.getMessage());
            return false;
        }
    }

    // ---- Serialization ----

    private static JsonObject serializeNode(DockNode node) {
        JsonObject obj = new JsonObject();

        if (node instanceof DockSplit split) {
            obj.addProperty("type", "split");
            obj.addProperty("orientation",
                split.getOrientation() == JSplitPane.HORIZONTAL_SPLIT ? "horizontal" : "vertical");
            obj.addProperty("fraction", split.getDividerFraction());
            obj.add("first", serializeNode(split.getFirst()));
            obj.add("second", serializeNode(split.getSecond()));

        } else if (node instanceof DockLeaf leaf) {
            obj.addProperty("type", "leaf");
            JsonArray tabs = new JsonArray();
            for (String id : leaf.getPanelIds()) {
                tabs.add(id);
            }
            obj.add("tabs", tabs);

        } else if (node instanceof DockCenter center) {
            obj.addProperty("type", "center");
            if (center.hasCenterTabs()) {
                JsonArray tabs = new JsonArray();
                for (String id : center.getCenterPanelIds()) {
                    tabs.add(id);
                }
                obj.add("centerTabs", tabs);
            }
        }

        return obj;
    }

    // ---- Deserialization (replay-based) ----

    /**
     * Replay the saved layout by walking the tree and issuing dock commands.
     * This is more robust than trying to reconstruct the tree directly,
     * since it uses the same code paths as user actions.
     */
    private static void replayNode(JsonObject root, DockingManager manager) {
        // First pass: collect all docked panel names and their positions
        // Then replay the docking operations in order

        // Undock everything first
        manager.undockAll();

        // Replay the tree structure
        replayRecursive(root, manager, null, null);
    }

    /**
     * Recursively walk the serialized tree and replay docking operations.
     * The approach: find all leaves, determine their edge relative to center,
     * and dock them in the right order.
     *
     * For simplicity, we collect all dock operations from the tree and execute them.
     */
    private static void replayRecursive(JsonObject node, DockingManager manager,
                                         String parentEdge, Double parentFraction) {
        String type = node.get("type").getAsString();

        switch (type) {
            case "center" -> {
                // Restore center-docked tabs if any
                if (node.has("centerTabs")) {
                    JsonArray tabs = node.getAsJsonArray("centerTabs");
                    for (int i = 0; i < tabs.size(); i++) {
                        manager.dockCenter(tabs.get(i).getAsString());
                    }
                }
            }
            case "leaf" -> {
                // This leaf needs to be docked at the edge determined by traversal
                JsonArray tabs = node.getAsJsonArray("tabs");
                if (tabs == null || tabs.isEmpty()) return;

                // The first tab determines the position, rest are added as tabs
                for (int i = 0; i < tabs.size(); i++) {
                    String title = tabs.get(i).getAsString();
                    if (parentEdge != null) {
                        manager.dockAtEdge(title, DockingManager.Edge.valueOf(parentEdge));
                    }
                }
            }
            case "split" -> {
                String orientation = node.get("orientation").getAsString();
                double fraction = node.get("fraction").getAsDouble();
                JsonObject first = node.getAsJsonObject("first");
                JsonObject second = node.getAsJsonObject("second");

                // Determine which child contains the center
                boolean centerInFirst = containsCenter(first);

                // The non-center side is the docked side
                // Determine the edge based on orientation and position
                String edge;
                if (orientation.equals("horizontal")) {
                    edge = centerInFirst ? "RIGHT" : "LEFT";
                } else {
                    edge = centerInFirst ? "BOTTOM" : "TOP";
                }

                if (centerInFirst) {
                    // Center is in first → process first (recurse into splits), then dock second
                    replayRecursive(first, manager, parentEdge, parentFraction);
                    replayDockSubtree(second, manager, edge);
                } else {
                    // Center is in second → dock first, then process second
                    replayDockSubtree(first, manager, edge);
                    replayRecursive(second, manager, parentEdge, parentFraction);
                }
            }
        }
    }

    /**
     * Dock all panels in a subtree. The first leaf's first tab uses dockAtEdge,
     * subsequent leaves in the subtree use dockAtEdgeNew (creating splits).
     */
    private static void replayDockSubtree(JsonObject node, DockingManager manager, String edge) {
        String type = node.get("type").getAsString();

        switch (type) {
            case "leaf" -> {
                JsonArray tabs = node.getAsJsonArray("tabs");
                if (tabs == null || tabs.isEmpty()) return;
                for (int i = 0; i < tabs.size(); i++) {
                    String title = tabs.get(i).getAsString();
                    manager.dockAtEdge(title, DockingManager.Edge.valueOf(edge));
                }
            }
            case "split" -> {
                // This is a split within a dock zone (e.g., right zone split into top-right/bottom-right)
                JsonObject first = node.getAsJsonObject("first");
                JsonObject second = node.getAsJsonObject("second");

                // Dock first subtree
                replayDockSubtree(first, manager, edge);

                // Dock second subtree — these need to go as new splits
                dockSubtreeAsNewSplit(second, manager, edge);
            }
            case "center" -> {
                // Center can't be docked
            }
        }
    }

    private static void dockSubtreeAsNewSplit(JsonObject node, DockingManager manager, String edge) {
        String type = node.get("type").getAsString();

        if (type.equals("leaf")) {
            JsonArray tabs = node.getAsJsonArray("tabs");
            if (tabs == null || tabs.isEmpty()) return;
            // First tab creates a new split, rest tab onto it
            String firstTitle = tabs.get(0).getAsString();
            manager.dockAtEdge(firstTitle, DockingManager.Edge.valueOf(edge));
            for (int i = 1; i < tabs.size(); i++) {
                manager.dockAtEdge(tabs.get(i).getAsString(), DockingManager.Edge.valueOf(edge));
            }
        } else if (type.equals("split")) {
            JsonObject first = node.getAsJsonObject("first");
            JsonObject second = node.getAsJsonObject("second");
            dockSubtreeAsNewSplit(first, manager, edge);
            dockSubtreeAsNewSplit(second, manager, edge);
        }
    }

    private static boolean containsCenter(JsonObject node) {
        String type = node.get("type").getAsString();
        if (type.equals("center")) return true;
        if (type.equals("split")) {
            return containsCenter(node.getAsJsonObject("first"))
                || containsCenter(node.getAsJsonObject("second"));
        }
        return false;
    }

    /** Delete the saved layout file. */
    public static void delete() {
        try {
            Files.deleteIfExists(LAYOUT_FILE);
        } catch (IOException ignored) {}
    }
}
