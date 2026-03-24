package com.libreshockwave.editor;

import com.google.gson.*;

import java.io.IOException;
import java.nio.file.*;
import java.util.*;

/**
 * Editor preferences persisted to ~/.libreshockwave/preferences.json.
 */
public class Preferences {

    private static final Path DIR = Path.of(System.getProperty("user.home"), ".libreshockwave");
    private static final Path FILE = DIR.resolve("preferences.json");
    private static final Gson GSON = new GsonBuilder().setPrettyPrinting().create();
    private static final int MAX_RECENT_PROJECTS = 10;

    private static Preferences instance;

    private String lastOpenDirectory;
    private final List<String> recentProjects = new ArrayList<>();
    private final Map<String, Map<String, String>> movieParams = new LinkedHashMap<>();

    public static Preferences get() {
        if (instance == null) {
            instance = load();
        }
        return instance;
    }

    public String getLastOpenDirectory() {
        return lastOpenDirectory;
    }

    public void setLastOpenDirectory(String path) {
        this.lastOpenDirectory = path;
        save();
    }

    public List<String> getRecentProjects() {
        return Collections.unmodifiableList(recentProjects);
    }

    public void addRecentProject(String path) {
        recentProjects.remove(path);
        recentProjects.addFirst(path);
        while (recentProjects.size() > MAX_RECENT_PROJECTS) {
            recentProjects.removeLast();
        }
        save();
    }

    public Map<String, String> getMovieParams(String moviePath) {
        Map<String, String> params = movieParams.get(moviePath);
        return params != null ? new LinkedHashMap<>(params) : null;
    }

    public void setMovieParams(String moviePath, Map<String, String> params) {
        if (params == null || params.isEmpty()) {
            movieParams.remove(moviePath);
        } else {
            movieParams.put(moviePath, new LinkedHashMap<>(params));
        }
        save();
    }

    private void save() {
        try {
            Files.createDirectories(DIR);
            JsonObject json = new JsonObject();
            if (lastOpenDirectory != null) {
                json.addProperty("lastOpenDirectory", lastOpenDirectory);
            }
            if (!recentProjects.isEmpty()) {
                JsonArray arr = new JsonArray();
                for (String p : recentProjects) {
                    arr.add(p);
                }
                json.add("recentProjects", arr);
            }
            if (!movieParams.isEmpty()) {
                JsonObject paramsObj = new JsonObject();
                for (var entry : movieParams.entrySet()) {
                    JsonObject movieObj = new JsonObject();
                    for (var param : entry.getValue().entrySet()) {
                        movieObj.addProperty(param.getKey(), param.getValue());
                    }
                    paramsObj.add(entry.getKey(), movieObj);
                }
                json.add("movieParams", paramsObj);
            }
            Files.writeString(FILE, GSON.toJson(json));
        } catch (IOException e) {
            System.err.println("Failed to save preferences: " + e.getMessage());
        }
    }

    private static Preferences load() {
        Preferences prefs = new Preferences();
        if (!Files.exists(FILE)) return prefs;
        try {
            String content = Files.readString(FILE);
            JsonObject json = JsonParser.parseString(content).getAsJsonObject();
            if (json.has("lastOpenDirectory")) {
                prefs.lastOpenDirectory = json.get("lastOpenDirectory").getAsString();
            }
            if (json.has("recentProjects")) {
                for (JsonElement el : json.getAsJsonArray("recentProjects")) {
                    prefs.recentProjects.add(el.getAsString());
                }
            }
            if (json.has("movieParams")) {
                JsonObject paramsObj = json.getAsJsonObject("movieParams");
                for (String key : paramsObj.keySet()) {
                    Map<String, String> params = new LinkedHashMap<>();
                    JsonObject movieObj = paramsObj.getAsJsonObject(key);
                    for (String paramKey : movieObj.keySet()) {
                        params.put(paramKey, movieObj.get(paramKey).getAsString());
                    }
                    prefs.movieParams.put(key, params);
                }
            }
        } catch (Exception e) {
            System.err.println("Failed to load preferences: " + e.getMessage());
        }
        return prefs;
    }
}
