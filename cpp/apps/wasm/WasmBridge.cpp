#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

#include "libreshockwave/DirectorFile.hpp"
#include "libreshockwave/bitmap/Bitmap.hpp"
#include "libreshockwave/cast/CastMember.hpp"
#include "libreshockwave/lingo/vm/OpcodeRegistry.hpp"
#include "libreshockwave/player/InputHandler.hpp"
#include "libreshockwave/player/Player.hpp"
#include "libreshockwave/player/PlayerState.hpp"
#include "libreshockwave/player/audio/QueuedAudioBackend.hpp"
#include "libreshockwave/player/event/EventDispatcher.hpp"
#include "libreshockwave/player/input/DirectorKeyCodes.hpp"
#include "libreshockwave/player/render/pipeline/RenderSprite.hpp"
#include "libreshockwave/player/net/QueuedNetProvider.hpp"
#include "libreshockwave/player/xtra/QueuedMultiuserBridge.hpp"
#include "libreshockwave/util/FileUtil.hpp"

namespace {

using libreshockwave::DirectorFile;
using libreshockwave::bitmap::Bitmap;
using libreshockwave::player::InputHandler;
using libreshockwave::player::Player;
using libreshockwave::player::PlayerEvent;
using libreshockwave::player::PlayerState;
using libreshockwave::player::handlerName;
using libreshockwave::player::audio::QueuedAudioBackend;
using libreshockwave::player::input::DirectorKeyCodes;
using libreshockwave::player::net::QueuedNetProvider;
using libreshockwave::player::render::pipeline::RenderSprite;
using libreshockwave::player::xtra::QueuedMultiuserBridge;

// Fetch completion can synchronously finish Director cast-load callbacks.
// Some legacy movies do real work there, including Habbo's sec.cct crypto setup,
// so the default must be a guardrail rather than a one-frame budget.
constexpr int DEFAULT_WASM_SCRIPT_DEADLINE_MS = 60000;
constexpr int SLOW_WASM_OPERATION_WARNING_MS = 1000;

struct WasmPlayerContext {
    std::shared_ptr<DirectorFile> file;
    std::unique_ptr<QueuedNetProvider> netProvider;
    std::unique_ptr<QueuedMultiuserBridge> multiuserBridge;
    std::unique_ptr<QueuedAudioBackend> audioBackend;
    std::unique_ptr<Player> player;
    std::vector<std::pair<std::string, std::string>> externalParams;
    std::vector<std::uint8_t> frameRgba;
    int frameWidth{0};
    int frameHeight{0};
    int frameBackground{0};
    bool tempoOverrideEnabled{false};
    int tempoOverride{0};
    bool preloadCasts{true};
    std::string movieSource;
    std::string lastError;
    std::string lastStatus;
    std::string scratch;
    bool debugPlaybackEnabled{false};
    int slowHandlerWarningMs{SLOW_WASM_OPERATION_WARNING_MS};
    std::vector<std::pair<std::string, std::string>> debugMessages;
};

std::unordered_map<int, std::unique_ptr<WasmPlayerContext>> contexts;
int nextHandle = 1;

WasmPlayerContext* getContext(int handle) {
    const auto found = contexts.find(handle);
    return found == contexts.end() ? nullptr : found->second.get();
}

std::string trim(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return std::string(value.substr(begin, end - begin));
}

bool startsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

std::string toLower(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return result;
}

bool equalsIgnoreCase(std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(lhs[index])) !=
            std::tolower(static_cast<unsigned char>(rhs[index]))) {
            return false;
        }
    }
    return true;
}

std::string sourceDirectory(std::string_view source) {
    if (source.empty()) {
        return {};
    }
    const std::string value(source);
    const auto query = value.find_first_of("?#");
    const std::string clean = query == std::string::npos ? value : value.substr(0, query);
    const auto slash = clean.find_last_of('/');
    if (slash == std::string::npos) {
        return {};
    }
    return clean.substr(0, slash + 1);
}

std::uint8_t blendChannel(std::uint8_t foreground, std::uint8_t background, std::uint8_t alpha) {
    return static_cast<std::uint8_t>(
        (static_cast<int>(foreground) * alpha + static_cast<int>(background) * (255 - alpha) + 127) / 255);
}

std::string jsonEscape(std::string_view value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (const unsigned char ch : value) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (ch < 0x20) {
                    constexpr char hex[] = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(hex[(ch >> 4U) & 0xFU]);
                    out.push_back(hex[ch & 0xFU]);
                } else {
                    out.push_back(static_cast<char>(ch));
                }
                break;
        }
    }
    return out;
}

void appendJsonString(std::ostringstream& out, std::string_view value) {
    out << '"' << jsonEscape(value) << '"';
}

void queueDebugMessage(WasmPlayerContext& ctx, std::string level, std::string message) {
    if (message.empty()) {
        return;
    }
    constexpr std::size_t kMaxDebugMessages = 500;
    if (ctx.debugMessages.size() >= kMaxDebugMessages) {
        ctx.debugMessages.erase(ctx.debugMessages.begin());
    }
    ctx.debugMessages.emplace_back(std::move(level), std::move(message));
}

std::int64_t elapsedMs(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
}

void queueSlowOperationMessage(WasmPlayerContext& ctx,
                               std::string_view operation,
                               std::chrono::steady_clock::time_point start) {
    const auto elapsed = elapsedMs(start);
    if (elapsed < SLOW_WASM_OPERATION_WARNING_MS) {
        return;
    }
    queueDebugMessage(ctx,
                      "warning",
                      std::string(operation) + " took " + std::to_string(elapsed) + "ms");
}

std::string debugMessagesJson(const WasmPlayerContext& ctx) {
    std::ostringstream out;
    out << '[';
    bool first = true;
    for (const auto& [level, message] : ctx.debugMessages) {
        if (!first) {
            out << ',';
        }
        first = false;
        out << "{\"level\":";
        appendJsonString(out, level);
        out << ",\"message\":";
        appendJsonString(out, message);
        out << '}';
    }
    out << ']';
    return out.str();
}

std::string base64Encode(const std::uint8_t* bytes, std::size_t length) {
    constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((length + 2U) / 3U) * 4U);
    for (std::size_t index = 0; index < length; index += 3U) {
        const std::uint32_t b0 = bytes[index];
        const std::uint32_t b1 = index + 1U < length ? bytes[index + 1U] : 0;
        const std::uint32_t b2 = index + 2U < length ? bytes[index + 2U] : 0;
        const std::uint32_t triple = (b0 << 16U) | (b1 << 8U) | b2;
        out.push_back(table[(triple >> 18U) & 0x3FU]);
        out.push_back(table[(triple >> 12U) & 0x3FU]);
        out.push_back(index + 1U < length ? table[(triple >> 6U) & 0x3FU] : '=');
        out.push_back(index + 2U < length ? table[triple & 0x3FU] : '=');
    }
    return out;
}

std::string base64Encode(const std::vector<std::uint8_t>& bytes) {
    return bytes.empty() ? std::string() : base64Encode(bytes.data(), bytes.size());
}

std::vector<std::uint8_t> bitmapToRgba(const Bitmap& bitmap, int backgroundRgb) {
    std::vector<std::uint8_t> rgba;
    if (bitmap.width() <= 0 || bitmap.height() <= 0 ||
        bitmap.pixels().size() != static_cast<std::size_t>(bitmap.width() * bitmap.height())) {
        return rgba;
    }

    const auto backgroundRed = static_cast<std::uint8_t>((backgroundRgb >> 16) & 0xFF);
    const auto backgroundGreen = static_cast<std::uint8_t>((backgroundRgb >> 8) & 0xFF);
    const auto backgroundBlue = static_cast<std::uint8_t>(backgroundRgb & 0xFF);

    rgba.reserve(bitmap.pixels().size() * 4U);
    for (const std::uint32_t argb : bitmap.pixels()) {
        const auto alpha = static_cast<std::uint8_t>((argb >> 24) & 0xFF);
        const auto red = static_cast<std::uint8_t>((argb >> 16) & 0xFF);
        const auto green = static_cast<std::uint8_t>((argb >> 8) & 0xFF);
        const auto blue = static_cast<std::uint8_t>(argb & 0xFF);
        rgba.push_back(blendChannel(red, backgroundRed, alpha));
        rgba.push_back(blendChannel(green, backgroundGreen, alpha));
        rgba.push_back(blendChannel(blue, backgroundBlue, alpha));
        rgba.push_back(0xFF);
    }
    return rgba;
}

std::uint32_t fnv1a32(const std::vector<std::uint8_t>& bytes) {
    std::uint32_t hash = 2166136261U;
    for (const auto byte : bytes) {
        hash ^= static_cast<std::uint32_t>(byte);
        hash *= 16777619U;
    }
    return hash;
}

std::string cursorCssForCode(int code) {
    switch (code) {
        case libreshockwave::player::CursorManager::IBEAM_CURSOR:
            return "text";
        case libreshockwave::player::CursorManager::WAIT_CURSOR:
            return "wait";
        case libreshockwave::player::CursorManager::POINTER_CURSOR:
            return "pointer";
        case libreshockwave::player::CursorManager::ARROW_CURSOR:
        case libreshockwave::player::CursorManager::DEFAULT_CURSOR:
        default:
            return "default";
    }
}

std::vector<std::pair<std::string, std::string>> parseParams(const char* text) {
    std::vector<std::pair<std::string, std::string>> params;
    if (text == nullptr || *text == '\0') {
        return params;
    }

    std::string input(text);
    for (char& ch : input) {
        if (ch == '&' || ch == '\r') {
            ch = '\n';
        }
    }

    std::size_t offset = 0;
    while (offset <= input.size()) {
        const auto next = input.find('\n', offset);
        const auto token = trim(input.substr(offset, next == std::string::npos ? std::string::npos : next - offset));
        if (!token.empty()) {
            if (const auto equals = token.find('='); equals != std::string::npos) {
                const std::string key = trim(std::string_view(token).substr(0, equals));
                const std::string value = trim(std::string_view(token).substr(equals + 1));
                if (!key.empty()) {
                    params.emplace_back(key, value);
                }
            }
        }
        if (next == std::string::npos) {
            break;
        }
        offset = next + 1;
    }
    return params;
}

bool isAlreadyLoadedCastRequest(const WasmPlayerContext& ctx, std::string_view url) {
    if (ctx.player == nullptr) {
        return false;
    }

    const std::string fileName = libreshockwave::util::getFileName(url);
    const std::string lowerFileName = toLower(fileName);
    const bool castLikeRequest = lowerFileName.ends_with(".cct") ||
                                 lowerFileName.ends_with(".cst") ||
                                 lowerFileName.find('.') == std::string::npos;
    if (!castLikeRequest) {
        return false;
    }

    const std::string baseName = libreshockwave::util::getFileNameWithoutExtension(fileName);
    if (baseName.empty()) {
        return false;
    }

    for (const auto& [number, castLib] : ctx.player->castLibManager().castLibs()) {
        (void)number;
        if (castLib == nullptr) {
            continue;
        }

        const bool nameMatches = !castLib->name().empty() && equalsIgnoreCase(castLib->name(), baseName);
        const std::string castFileBaseName =
            libreshockwave::util::getFileNameWithoutExtension(libreshockwave::util::getFileName(castLib->fileName()));
        const bool fileMatches = !castFileBaseName.empty() && equalsIgnoreCase(castFileBaseName, baseName);
        if (!nameMatches && !fileMatches) {
            continue;
        }

        if (!castLib->isExternal() && castLib->isLoaded()) {
            return true;
        }
        if (castLib->isExternal() && castLib->isFetched()) {
            return true;
        }
    }
    return false;
}

void applyTempo(WasmPlayerContext& ctx) {
    if (ctx.player == nullptr) {
        return;
    }
    const int baseTempo = ctx.file != nullptr && ctx.file->tempo() > 0 ? ctx.file->tempo() : 15;
    ctx.player->setTempo(ctx.tempoOverrideEnabled ? ctx.tempoOverride : baseTempo);
}

void setError(WasmPlayerContext& ctx, std::string message) {
    ctx.lastError = std::move(message);
    ctx.lastStatus = ctx.lastError;
    queueDebugMessage(ctx, "error", ctx.lastError);
}

void clearError(WasmPlayerContext& ctx) {
    ctx.lastError.clear();
}

int renderCurrentFrame(WasmPlayerContext& ctx) {
    if (ctx.player == nullptr) {
        ctx.frameRgba.clear();
        ctx.frameWidth = 0;
        ctx.frameHeight = 0;
        ctx.frameBackground = 0;
        return 0;
    }

    const auto snapshot = ctx.player->frameSnapshot();
    auto frame = snapshot.renderFrame();
    InputHandler::applyEditableFieldOverlay(frame, ctx.player->inputHandler().editableFieldOverlay());
    ctx.frameRgba = bitmapToRgba(frame, snapshot.backgroundColor);
    ctx.frameWidth = frame.width();
    ctx.frameHeight = frame.height();
    ctx.frameBackground = snapshot.backgroundColor;
    return ctx.frameRgba.empty() ? 0 : 1;
}

std::string frameInfoJson(WasmPlayerContext& ctx) {
    std::ostringstream out;
    const int currentFrame = ctx.player != nullptr ? ctx.player->currentFrame() : 0;
    const int frameCount = ctx.player != nullptr ? ctx.player->frameCount() : 0;
    const int tempo = ctx.player != nullptr ? std::max(1, ctx.player->tempo()) : 15;
    const int baseTempo = ctx.player != nullptr ? ctx.player->baseTempo() : 15;
    const std::string state = ctx.player != nullptr ? std::string(libreshockwave::player::name(ctx.player->state())) : "NONE";
    int cursorCode = libreshockwave::player::CursorManager::ARROW_CURSOR;
    std::optional<std::array<int, 2>> cursorReg;
    std::optional<Bitmap> cursorBitmap;
    if (ctx.player != nullptr) {
        cursorCode = ctx.player->cursorManager().getCursorAtMouse();
        if (cursorCode == libreshockwave::player::CursorManager::CUSTOM_BITMAP_CURSOR) {
            cursorReg = ctx.player->cursorManager().getCursorRegPoint();
            cursorBitmap = ctx.player->cursorManager().getCursorBitmap();
        }
    }

    out << '{'
        << "\"width\":" << ctx.frameWidth
        << ",\"height\":" << ctx.frameHeight
        << ",\"checksum\":" << fnv1a32(ctx.frameRgba)
        << ",\"background\":" << ctx.frameBackground
        << ",\"currentFrame\":" << currentFrame
        << ",\"frameCount\":" << frameCount
        << ",\"tempo\":" << tempo
        << ",\"baseTempo\":" << baseTempo
        << ",\"tempoOverride\":" << (ctx.tempoOverrideEnabled ? ctx.tempoOverride : 0)
        << ",\"state\":";
    appendJsonString(out, state);
    out << ",\"status\":";
    appendJsonString(out, ctx.lastStatus);
    out << ",\"error\":";
    appendJsonString(out, ctx.lastError);
    out << ",\"cursor\":{\"code\":" << cursorCode << ",\"css\":";
    appendJsonString(out, cursorCssForCode(cursorCode));
    if (cursorReg.has_value()) {
        out << ",\"hotX\":" << (*cursorReg)[0] << ",\"hotY\":" << (*cursorReg)[1];
    }
    if (cursorBitmap.has_value()) {
        const auto rgba = bitmapToRgba(*cursorBitmap, 0xFFFFFF);
        if (!rgba.empty()) {
            out << ",\"width\":" << cursorBitmap->width()
                << ",\"height\":" << cursorBitmap->height()
                << ",\"pixels\":\"" << base64Encode(rgba) << '"';
        }
    }
    out << "}}";
    return out.str();
}

std::string frameSpritesJson(WasmPlayerContext& ctx) {
    struct ColorBounds {
        int count = 0;
        int minX = 0;
        int minY = 0;
        int maxX = 0;
        int maxY = 0;
    };

    std::ostringstream out;
    auto appendBitmapSummary = [](std::ostringstream& stream, const char* key, const Bitmap& bitmap) {
        int minX = bitmap.width();
        int minY = bitmap.height();
        int maxX = -1;
        int maxY = -1;
        int opaquePixels = 0;
        int translucentPixels = 0;
        std::unordered_map<std::uint32_t, ColorBounds> colorBounds;
        for (int y = 0; y < bitmap.height(); ++y) {
            for (int x = 0; x < bitmap.width(); ++x) {
                const auto pixel = bitmap.getPixel(x, y);
                const int alpha = static_cast<int>((pixel >> 24U) & 0xFFU);
                if (alpha == 0) {
                    continue;
                }
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
                if (alpha == 255) {
                    ++opaquePixels;
                } else {
                    ++translucentPixels;
                }
                const std::uint32_t rgb = pixel & 0x00FFFFFFU;
                auto& bounds = colorBounds[rgb];
                if (bounds.count == 0) {
                    bounds.minX = bounds.maxX = x;
                    bounds.minY = bounds.maxY = y;
                } else {
                    bounds.minX = std::min(bounds.minX, x);
                    bounds.minY = std::min(bounds.minY, y);
                    bounds.maxX = std::max(bounds.maxX, x);
                    bounds.maxY = std::max(bounds.maxY, y);
                }
                ++bounds.count;
            }
        }
        stream << ",\"" << key << "\":{\"width\":" << bitmap.width()
               << ",\"height\":" << bitmap.height()
               << ",\"bitDepth\":" << bitmap.bitDepth()
               << ",\"paletteIndices\":" << (bitmap.paletteIndices().has_value() ? "true" : "false")
               << ",\"nativeAlpha\":" << (bitmap.isNativeAlpha() ? "true" : "false")
               << ",\"scriptModified\":" << (bitmap.isScriptModified() ? "true" : "false")
               << ",\"scriptFillBacking\":" << (bitmap.hasScriptFillBacking() ? "true" : "false")
               << ",\"preserveScriptFillBacking\":" << (bitmap.preservesScriptFillBacking() ? "true" : "false")
               << ",\"rectangularMedia\":" << (bitmap.isRectangularMedia() ? "true" : "false")
               << ",\"textRendered\":" << (bitmap.isTextRendered() ? "true" : "false")
               << ",\"transparent\":" << (bitmap.hasTransparentPixels() ? "true" : "false")
               << ",\"translucent\":" << (bitmap.hasTranslucentPixels() ? "true" : "false")
               << ",\"opaquePixels\":" << opaquePixels
               << ",\"translucentPixels\":" << translucentPixels;
        if (maxX >= 0) {
            stream << ",\"alphaBounds\":{\"left\":" << minX
                   << ",\"top\":" << minY
                   << ",\"right\":" << (maxX + 1)
                   << ",\"bottom\":" << (maxY + 1)
                   << '}';
        } else {
            stream << ",\"alphaBounds\":null";
        }
        std::vector<std::pair<std::uint32_t, ColorBounds>> colors(colorBounds.begin(), colorBounds.end());
        std::sort(colors.begin(), colors.end(), [](const auto& lhs, const auto& rhs) {
            if (lhs.second.count != rhs.second.count) {
                return lhs.second.count > rhs.second.count;
            }
            return lhs.first < rhs.first;
        });
        stream << ",\"topColors\":[";
        const std::size_t colorLimit = std::min<std::size_t>(colors.size(), 16);
        for (std::size_t index = 0; index < colorLimit; ++index) {
            if (index > 0) {
                stream << ',';
            }
            const auto& [rgb, bounds] = colors[index];
            stream << "{\"rgb\":" << rgb
                   << ",\"count\":" << bounds.count
                   << ",\"bounds\":{\"left\":" << bounds.minX
                   << ",\"top\":" << bounds.minY
                   << ",\"right\":" << (bounds.maxX + 1)
                   << ",\"bottom\":" << (bounds.maxY + 1)
                   << "}}";
        }
        stream << ']';
        const std::array<std::pair<int, int>, 12> probePoints{{
            {0, 0}, {1, 1}, {4, 1}, {5, 1}, {5, 2}, {5, 3},
            {5, 4}, {5, 5}, {bitmap.width() / 2, 1}, {bitmap.width() / 2, 2},
            {std::max(0, bitmap.width() - 6), 2}, {std::max(0, bitmap.width() - 2), 1}
        }};
        stream << ",\"probePixels\":[";
        bool firstProbe = true;
        const auto paletteIndices = bitmap.paletteIndices();
        for (const auto& [x, y] : probePoints) {
            if (x < 0 || y < 0 || x >= bitmap.width() || y >= bitmap.height()) {
                continue;
            }
            if (!firstProbe) {
                stream << ',';
            }
            firstProbe = false;
            const auto pixel = bitmap.getPixel(x, y);
            stream << "{\"x\":" << x
                   << ",\"y\":" << y
                   << ",\"argb\":" << pixel;
            if (paletteIndices.has_value()) {
                const auto offset = static_cast<std::size_t>(y * bitmap.width() + x);
                if (offset < paletteIndices->size()) {
                    stream << ",\"paletteIndex\":" << static_cast<int>((*paletteIndices)[offset]);
                }
            }
            stream << '}';
        }
        stream << "]}";
    };

    out << "{\"sprites\":[";
    if (ctx.player != nullptr) {
        const auto& sprites = ctx.player->stageRenderer().lastBakedSprites();
        bool first = true;
        for (const auto& sprite : sprites) {
            if (!first) {
                out << ',';
            }
            first = false;
            const auto baked = sprite.bakedBitmap();
            out << "{\"channel\":" << sprite.channel()
                << ",\"x\":" << sprite.x()
                << ",\"y\":" << sprite.y()
                << ",\"width\":" << sprite.width()
                << ",\"height\":" << sprite.height()
                << ",\"locZ\":" << sprite.locZ()
                << ",\"visible\":" << (sprite.isVisible() ? "true" : "false")
                << ",\"type\":";
            appendJsonString(out, std::string(libreshockwave::player::render::pipeline::name(sprite.type())));
            out << ",\"castMemberId\":" << sprite.castMemberId()
                << ",\"memberName\":";
            if (auto memberName = sprite.memberName(); memberName.has_value()) {
                appendJsonString(out, *memberName);
            } else {
                out << "null";
            }
            out << ",\"ink\":" << sprite.ink()
                << ",\"blend\":" << sprite.blend()
                << ",\"flipH\":" << (sprite.isFlipH() ? "true" : "false")
                << ",\"flipV\":" << (sprite.isFlipV() ? "true" : "false")
                << ",\"rotation\":" << sprite.rotation()
                << ",\"skew\":" << sprite.skew()
                << ",\"directorMirrorH\":" << (sprite.hasDirectorHorizontalMirror() ? "true" : "false")
                << ",\"foreColor\":" << sprite.foreColor()
                << ",\"backColor\":" << sprite.backColor();
            const auto spriteState = ctx.player->stageRenderer().spriteRegistry().get(sprite.channel());
            out << ",\"scriptInstanceCount\":" << (spriteState != nullptr ? spriteState->scriptInstanceList().size() : 0)
                << ",\"hasKeyDownHandler\":"
                << (ctx.player->eventDispatcher().spriteHasHandler(sprite.channel(), handlerName(PlayerEvent::KeyDown)) ? "true" : "false")
                << ",\"hasMouseDownHandler\":"
                << (ctx.player->eventDispatcher().spriteHasHandler(sprite.channel(), handlerName(PlayerEvent::MouseDown)) ? "true" : "false")
                << ",\"hasMouseUpHandler\":"
                << (ctx.player->eventDispatcher().spriteHasHandler(sprite.channel(), handlerName(PlayerEvent::MouseUp)) ? "true" : "false")
                << ",\"hasBakedBitmap\":" << (baked != nullptr ? "true" : "false");
            if (baked != nullptr) {
                int minX = baked->width();
                int minY = baked->height();
                int maxX = -1;
                int maxY = -1;
                int opaquePixels = 0;
                int translucentPixels = 0;
                std::unordered_map<std::uint32_t, ColorBounds> colorBounds;
                for (int y = 0; y < baked->height(); ++y) {
                    for (int x = 0; x < baked->width(); ++x) {
                        const auto pixel = baked->getPixel(x, y);
                        const int alpha = static_cast<int>((pixel >> 24U) & 0xFFU);
                        if (alpha == 0) {
                            continue;
                        }
                        minX = std::min(minX, x);
                        minY = std::min(minY, y);
                        maxX = std::max(maxX, x);
                        maxY = std::max(maxY, y);
                        if (alpha == 255) {
                            ++opaquePixels;
                        } else {
                            ++translucentPixels;
                        }
                        const std::uint32_t rgb = pixel & 0x00FFFFFFU;
                        auto& bounds = colorBounds[rgb];
                        if (bounds.count == 0) {
                            bounds.minX = bounds.maxX = x;
                            bounds.minY = bounds.maxY = y;
                        } else {
                            bounds.minX = std::min(bounds.minX, x);
                            bounds.minY = std::min(bounds.minY, y);
                            bounds.maxX = std::max(bounds.maxX, x);
                            bounds.maxY = std::max(bounds.maxY, y);
                        }
                        ++bounds.count;
                    }
                }
                out << ",\"bitmap\":{\"width\":" << baked->width()
                    << ",\"height\":" << baked->height()
                    << ",\"bitDepth\":" << baked->bitDepth()
                    << ",\"paletteIndices\":" << (baked->paletteIndices().has_value() ? "true" : "false")
                    << ",\"nativeAlpha\":" << (baked->isNativeAlpha() ? "true" : "false")
                    << ",\"scriptModified\":" << (baked->isScriptModified() ? "true" : "false")
                    << ",\"scriptFillBacking\":" << (baked->hasScriptFillBacking() ? "true" : "false")
                    << ",\"preserveScriptFillBacking\":" << (baked->preservesScriptFillBacking() ? "true" : "false")
                    << ",\"rectangularMedia\":" << (baked->isRectangularMedia() ? "true" : "false")
                    << ",\"textRendered\":" << (baked->isTextRendered() ? "true" : "false")
                    << ",\"transparent\":" << (baked->hasTransparentPixels() ? "true" : "false")
                    << ",\"translucent\":" << (baked->hasTranslucentPixels() ? "true" : "false")
                    << ",\"opaquePixels\":" << opaquePixels
                    << ",\"translucentPixels\":" << translucentPixels;
                if (maxX >= 0) {
                    out << ",\"alphaBounds\":{\"left\":" << minX
                        << ",\"top\":" << minY
                        << ",\"right\":" << (maxX + 1)
                        << ",\"bottom\":" << (maxY + 1)
                        << '}';
                } else {
                    out << ",\"alphaBounds\":null";
                }
                std::vector<std::pair<std::uint32_t, ColorBounds>> colors(colorBounds.begin(), colorBounds.end());
                std::sort(colors.begin(), colors.end(), [](const auto& lhs, const auto& rhs) {
                    if (lhs.second.count != rhs.second.count) {
                        return lhs.second.count > rhs.second.count;
                    }
                    return lhs.first < rhs.first;
                });
                out << ",\"topColors\":[";
                const std::size_t colorLimit = std::min<std::size_t>(colors.size(), 16);
                for (std::size_t index = 0; index < colorLimit; ++index) {
                    if (index > 0) {
                        out << ',';
                    }
                    const auto& [rgb, bounds] = colors[index];
                    out << "{\"rgb\":" << rgb
                        << ",\"count\":" << bounds.count
                        << ",\"bounds\":{\"left\":" << bounds.minX
                        << ",\"top\":" << bounds.minY
                        << ",\"right\":" << (bounds.maxX + 1)
                        << ",\"bottom\":" << (bounds.maxY + 1)
                        << "}}";
                }
                out << ']';
                const std::array<std::pair<int, int>, 12> probePoints{{
                    {0, 0}, {1, 1}, {4, 1}, {5, 1}, {5, 2}, {5, 3},
                    {5, 4}, {5, 5}, {baked->width() / 2, 1}, {baked->width() / 2, 2},
                    {std::max(0, baked->width() - 6), 2}, {std::max(0, baked->width() - 2), 1}
                }};
                out << ",\"probePixels\":[";
                bool firstProbe = true;
                const auto paletteIndices = baked->paletteIndices();
                for (const auto& [x, y] : probePoints) {
                    if (x < 0 || y < 0 || x >= baked->width() || y >= baked->height()) {
                        continue;
                    }
                    if (!firstProbe) {
                        out << ',';
                    }
                    firstProbe = false;
                    const auto pixel = baked->getPixel(x, y);
                    out << "{\"x\":" << x
                        << ",\"y\":" << y
                        << ",\"argb\":" << pixel;
                    if (paletteIndices.has_value()) {
                        const auto offset = static_cast<std::size_t>(y * baked->width() + x);
                        if (offset < paletteIndices->size()) {
                            out << ",\"paletteIndex\":" << static_cast<int>((*paletteIndices)[offset]);
                        }
                    }
                    out << '}';
                }
                out << ']';
                out << '}';
            }
            if (auto authoredMember = sprite.castMember(); authoredMember != nullptr && authoredMember->isTextXtra()) {
                out << ",\"authoredXmed\":";
                auto* file = authoredMember->file();
                if (file != nullptr) {
                    auto styled = const_cast<DirectorFile*>(file)->getXmedStyledTextForMember(
                        std::const_pointer_cast<libreshockwave::chunks::CastMemberChunk>(authoredMember));
                    if (styled.has_value()) {
                        out << "{\"alignment\":";
                        appendJsonString(out, styled->alignment);
                        out << ",\"paragraphStyleIndex\":" << styled->primaryParagraphStyleIndex
                            << ",\"paragraphAlignmentCode\":" << styled->primaryParagraphAlignmentCode
                            << ",\"paragraphStyleCount\":" << styled->paragraphStyleCount
                            << ",\"fontName\":";
                        appendJsonString(out, styled->fontName);
                        out << ",\"fontSize\":" << styled->fontSize
                            << ",\"fontStyle\":";
                        appendJsonString(out, styled->fontStyleString());
                        out << ",\"width\":" << styled->width
                            << ",\"height\":" << styled->height
                            << ",\"wordWrap\":" << (styled->wordWrap ? "true" : "false")
                            << ",\"antialias\":" << (styled->antialias ? "true" : "false")
                            << ",\"fixedLineSpace\":" << styled->fixedLineSpace
                            << ",\"text\":";
                        appendJsonString(out, styled->text);
                        out << '}';
                    } else {
                        out << "null";
                    }
                } else {
                    out << "null";
                }
            }
            if (auto dynamicMember = sprite.dynamicMember(); dynamicMember != nullptr) {
                out << ",\"dynamicMember\":{\"id\":" << dynamicMember->id()
                    << ",\"castLib\":" << dynamicMember->castLib()
                    << ",\"memberNum\":" << dynamicMember->memberNum()
                    << ",\"name\":";
                appendJsonString(out, dynamicMember->name());
                out << ",\"width\":" << dynamicMember->width()
                    << ",\"height\":" << dynamicMember->height()
                    << ",\"regX\":" << dynamicMember->regX()
                    << ",\"regY\":" << dynamicMember->regY()
                    << ",\"runtimeDynamic\":" << (dynamicMember->isRuntimeDynamic() ? "true" : "false")
                    << ",\"hasRuntimeBitmap\":" << (dynamicMember->runtimeBitmap() != nullptr ? "true" : "false");
                if (dynamicMember->isTextLike()) {
                    out << ",\"text\":";
                    appendJsonString(out,
                                     ctx.player->castLibManager()
                                         .getMemberProp(dynamicMember->castLib(), dynamicMember->memberNum(), "text")
                                         .stringValue());
                    out << ",\"textFont\":";
                    appendJsonString(out, dynamicMember->textFont());
                    out << ",\"textFontSize\":" << dynamicMember->textFontSize()
                        << ",\"textFontStyle\":";
                    appendJsonString(out, dynamicMember->textFontStyle());
                    out << ",\"textAlignment\":";
                    appendJsonString(out, dynamicMember->textAlignment());
                    out << ",\"textRect\":{\"left\":" << dynamicMember->textRectLeft()
                        << ",\"top\":" << dynamicMember->textRectTop()
                        << ",\"right\":" << dynamicMember->textRectRight()
                        << ",\"bottom\":" << dynamicMember->textRectBottom()
                        << "},\"textWordWrap\":" << (dynamicMember->textWordWrap() ? "true" : "false")
                        << ",\"textAntialias\":" << (dynamicMember->textAntialias() ? "true" : "false")
                        << ",\"textFixedLineSpace\":" << dynamicMember->textFixedLineSpace()
                        << ",\"textTopSpacing\":" << dynamicMember->textTopSpacing();
                }
                out << ",\"memberType\":";
                appendJsonString(out, std::string(::libreshockwave::cast::name(dynamicMember->memberType())));
                out << '}';
                if (auto live = dynamicMember->runtimeBitmap(); live != nullptr) {
                    appendBitmapSummary(out, "liveBitmap", *live);
                }
            } else {
                out << ",\"dynamicMember\":null";
            }
            out << '}';
        }
    }
    out << "]}";
    return out.str();
}

std::string fetchRequestsJson(const std::vector<QueuedNetProvider::PendingRequest>& requests) {
    std::ostringstream out;
    out << '[';
    bool first = true;
    for (const auto& request : requests) {
        if (!first) {
            out << ',';
        }
        first = false;
        out << "{\"taskId\":" << request.taskId << ",\"url\":";
        appendJsonString(out, request.url);
        out << ",\"method\":";
        appendJsonString(out, request.method);
        out << ",\"postData\":";
        if (request.postData.has_value()) {
            appendJsonString(out, *request.postData);
        } else {
            out << "null";
        }
        out << ",\"fallbacks\":[";
        for (std::size_t index = 0; index < request.fallbacks.size(); ++index) {
            if (index > 0) {
                out << ',';
            }
            appendJsonString(out, request.fallbacks[index]);
        }
        out << "]}";
    }
    out << ']';
    return out.str();
}

std::string multiuserRequestsJson(const std::vector<QueuedMultiuserBridge::PendingRequest>& requests) {
    std::ostringstream out;
    out << '[';
    bool first = true;
    for (const auto& request : requests) {
        if (!first) {
            out << ',';
        }
        first = false;
        out << "{\"type\":" << request.type << ",\"instanceId\":" << request.instanceId;
        if (request.type == QueuedMultiuserBridge::REQ_CONNECT) {
            out << ",\"host\":";
            appendJsonString(out, request.host);
            out << ",\"port\":" << request.port;
        } else if (request.type == QueuedMultiuserBridge::REQ_SEND) {
            out << ",\"senderID\":";
            appendJsonString(out, request.senderID);
            out << ",\"subject\":";
            appendJsonString(out, request.subject);
            out << ",\"content\":";
            appendJsonString(out, request.content);
            out << ",\"bytes\":\"" << base64Encode(request.wireBytes()) << '"';
        }
        out << '}';
    }
    out << ']';
    return out.str();
}

std::string audioCommandsJson(const std::vector<QueuedAudioBackend::SoundCommand>& commands) {
    std::ostringstream out;
    out << '[';
    bool first = true;
    for (const auto& command : commands) {
        if (!first) {
            out << ',';
        }
        first = false;
        out << "{\"action\":";
        appendJsonString(out, command.action);
        out << ",\"channel\":" << command.channelNum
            << ",\"loopCount\":" << command.loopCount
            << ",\"volume\":" << command.volume;
        if (command.format.has_value()) {
            out << ",\"format\":";
            appendJsonString(out, *command.format);
        }
        if (command.audioData.has_value()) {
            out << ",\"byteLength\":" << command.audioData->size()
                << ",\"data\":\"" << base64Encode(*command.audioData) << '"';
        }
        out << '}';
    }
    out << ']';
    return out.str();
}

void processQueuedInput(WasmPlayerContext& ctx) {
    if (ctx.player == nullptr) {
        return;
    }
    (void)ctx.player->inputHandler().processInputEvents();
}

const char* scratch(WasmPlayerContext& ctx, std::string value) {
    ctx.scratch = std::move(value);
    return ctx.scratch.c_str();
}

class WasmScriptDeadlineGuard {
public:
    explicit WasmScriptDeadlineGuard(Player* player)
        : player_(player) {
        if (player_ == nullptr) {
            return;
        }
        previousDeadlineMs_ = player_->vm().tickDeadlineMs();
        previousDeadline_ = player_->vm().tickDeadline();
        if (previousDeadlineMs_ == 0) {
            player_->vm().setTickDeadlineMs(DEFAULT_WASM_SCRIPT_DEADLINE_MS);
            changedDeadlineMs_ = true;
        }
        if (previousDeadline_ == 0) {
            player_->vm().armTickDeadline();
            changedDeadline_ = true;
        }
    }

    ~WasmScriptDeadlineGuard() {
        if (player_ == nullptr) {
            return;
        }
        if (changedDeadline_) {
            player_->vm().setTickDeadline(previousDeadline_);
        }
        if (changedDeadlineMs_) {
            player_->vm().setTickDeadlineMs(previousDeadlineMs_);
        }
    }

private:
    Player* player_{nullptr};
    std::int64_t previousDeadlineMs_{0};
    std::int64_t previousDeadline_{0};
    bool changedDeadlineMs_{false};
    bool changedDeadline_{false};
};

void processHostXtraCallbacks(WasmPlayerContext& ctx) {
    if (ctx.player == nullptr || ctx.player->isVmRunning()) {
        return;
    }
    const auto started = std::chrono::steady_clock::now();
    WasmScriptDeadlineGuard scriptDeadline(ctx.player.get());
    ctx.player->xtraManager().tickAll();
    ctx.player->vm().flushDeferredTasks();
    queueSlowOperationMessage(ctx, "host xtra callbacks", started);
}

std::string operationError(std::string_view operation, std::string_view detail) {
    std::string message(operation);
    if (!detail.empty()) {
        message += ": ";
        message += detail;
    }
    return message;
}

template <typename Callback>
void guardedVoid(WasmPlayerContext& ctx, std::string_view operation, Callback&& callback) {
    const auto start = std::chrono::steady_clock::now();
    try {
        callback();
        queueSlowOperationMessage(ctx, operation, start);
    } catch (const std::exception& error) {
        setError(ctx, operationError(operation, error.what()));
    } catch (...) {
        setError(ctx, operationError(operation, "unknown error"));
    }
}

template <typename Result, typename Callback>
Result guardedResult(WasmPlayerContext& ctx,
                     std::string_view operation,
                     Result fallback,
                     Callback&& callback) {
    const auto start = std::chrono::steady_clock::now();
    try {
        auto result = callback();
        queueSlowOperationMessage(ctx, operation, start);
        return result;
    } catch (const std::exception& error) {
        setError(ctx, operationError(operation, error.what()));
    } catch (...) {
        setError(ctx, operationError(operation, "unknown error"));
    }
    return fallback;
}

} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE int lsw_create() {
    const int handle = nextHandle++;
    contexts.emplace(handle, std::make_unique<WasmPlayerContext>());
    return handle;
}

EMSCRIPTEN_KEEPALIVE void lsw_destroy(int handle) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->player != nullptr) {
        guardedVoid(*ctx, "destroy", [&]() {
            ctx->player->shutdown();
        });
    }
    contexts.erase(handle);
}

EMSCRIPTEN_KEEPALIVE int lsw_load_movie(int handle,
                                        const char* source,
                                        const std::uint8_t* bytes,
                                        int byteCount,
                                        const char* paramsText) {
    auto* ctx = getContext(handle);
    if (ctx == nullptr || bytes == nullptr || byteCount <= 0) {
        return 0;
    }

    try {
        if (ctx->player != nullptr) {
            ctx->player->shutdown();
        }
        ctx->player.reset();
        ctx->audioBackend.reset();
        ctx->multiuserBridge.reset();
        ctx->netProvider.reset();
        ctx->file.reset();
        ctx->frameRgba.clear();
        ctx->frameWidth = 0;
        ctx->frameHeight = 0;
        clearError(*ctx);

        ctx->movieSource = source != nullptr ? std::string(source) : std::string();
        if (paramsText != nullptr) {
            ctx->externalParams = parseParams(paramsText);
        }

        std::vector<std::uint8_t> data(bytes, bytes + byteCount);
        auto file = DirectorFile::load(data);
        file->setBasePath(sourceDirectory(ctx->movieSource));

        auto netProvider = std::make_unique<QueuedNetProvider>(ctx->movieSource);
        auto multiuserBridge = std::make_unique<QueuedMultiuserBridge>();
        auto audioBackend = std::make_unique<QueuedAudioBackend>();
        auto player = std::make_unique<Player>(file, netProvider.get());
        player->registerMultiuserXtra(*multiuserBridge);
        player->setAudioBackend(audioBackend.get());
        player->setExternalParams(ctx->externalParams);
        player->builtinContext().outputHandler = [ctx](std::string_view kind, const std::string& text) {
            if (ctx != nullptr) {
                queueDebugMessage(*ctx, std::string(kind), text);
            }
        };
        player->setErrorListener([ctx](std::string_view message, std::string_view detail) {
            std::string error(message);
            if (!detail.empty()) {
                error += ": ";
                error += detail;
            }
            setError(*ctx, std::move(error));
        });
        player->movieProperties().setGotoNetPageHandler([ctx](const std::string& url, const std::string& target) {
            (void)target;
            if (ctx != nullptr && ctx->netProvider != nullptr) {
                (void)ctx->netProvider->beginMovieNavigation(url);
            }
        });
        player->movieProperties().setGotoNetMovieHandler([ctx](const std::string& url) {
            if (ctx == nullptr || ctx->netProvider == nullptr) {
                return -1;
            }
            return ctx->netProvider->beginMovieNavigation(url);
        });
        netProvider->setFetchCompleteCallback([ctx](const std::string& url,
                                                    const std::vector<std::uint8_t>& data) {
            if (ctx != nullptr && ctx->player != nullptr) {
                ctx->player->onNetFetchComplete(url, data);
            }
        });
        netProvider->setSatisfiedFetchPredicate([ctx](std::string_view url) {
            return ctx != nullptr && isAlreadyLoadedCastRequest(*ctx, url);
        });

        ctx->file = std::move(file);
        ctx->netProvider = std::move(netProvider);
        ctx->multiuserBridge = std::move(multiuserBridge);
        ctx->audioBackend = std::move(audioBackend);
        ctx->player = std::move(player);
        ctx->player->setDebugEnabled(ctx->debugPlaybackEnabled);
        ctx->player->setSlowHandlerWarningThresholdMs(ctx->slowHandlerWarningMs);
        applyTempo(*ctx);

        if (ctx->preloadCasts) {
            (void)ctx->player->preloadAllCasts();
        }
        ctx->player->play();
        ctx->lastStatus = "Loaded " + ctx->movieSource;
        (void)renderCurrentFrame(*ctx);
        return 1;
    } catch (const std::exception& error) {
        setError(*ctx, error.what());
        return 0;
    } catch (...) {
        setError(*ctx, "Unknown load error");
        return 0;
    }
}

EMSCRIPTEN_KEEPALIVE void lsw_set_external_params(int handle, const char* paramsText) {
    auto* ctx = getContext(handle);
    if (ctx == nullptr) {
        return;
    }
    guardedVoid(*ctx, "set external params", [&]() {
        ctx->externalParams = parseParams(paramsText);
        if (ctx->player != nullptr) {
            ctx->player->setExternalParams(ctx->externalParams);
        }
    });
}

EMSCRIPTEN_KEEPALIVE void lsw_set_preload_casts(int handle, int preloadCasts) {
    if (auto* ctx = getContext(handle)) {
        ctx->preloadCasts = preloadCasts != 0;
    }
}

EMSCRIPTEN_KEEPALIVE void lsw_set_debug_playback_enabled(int handle, int enabled) {
    if (auto* ctx = getContext(handle)) {
        ctx->debugPlaybackEnabled = enabled != 0;
        if (ctx->player != nullptr) {
            guardedVoid(*ctx, "set debug playback", [&]() {
                ctx->player->setDebugEnabled(ctx->debugPlaybackEnabled);
            });
        }
    }
}

EMSCRIPTEN_KEEPALIVE void lsw_set_slow_handler_warning_ms(int handle, int milliseconds) {
    if (auto* ctx = getContext(handle)) {
        ctx->slowHandlerWarningMs = std::max(0, milliseconds);
        if (ctx->player != nullptr) {
            guardedVoid(*ctx, "set slow handler warning threshold", [&]() {
                ctx->player->setSlowHandlerWarningThresholdMs(ctx->slowHandlerWarningMs);
            });
        }
    }
}

EMSCRIPTEN_KEEPALIVE void lsw_set_tempo_override(int handle, int tempo) {
    auto* ctx = getContext(handle);
    if (ctx == nullptr) {
        return;
    }
    guardedVoid(*ctx, "set tempo override", [&]() {
        ctx->tempoOverrideEnabled = tempo > 0;
        ctx->tempoOverride = tempo > 0 ? tempo : 0;
        applyTempo(*ctx);
    });
}

EMSCRIPTEN_KEEPALIVE int lsw_tempo(int handle) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->player != nullptr) {
        return std::max(1, ctx->player->tempo());
    }
    return 15;
}

EMSCRIPTEN_KEEPALIVE int lsw_base_tempo(int handle) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->player != nullptr) {
        return std::max(1, ctx->player->baseTempo());
    }
    return 15;
}

EMSCRIPTEN_KEEPALIVE void lsw_play(int handle) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->player != nullptr) {
        guardedVoid(*ctx, "play", [&]() {
            if (ctx->player->state() == PlayerState::Stopped) {
                ctx->player->play();
            } else {
                ctx->player->resume();
            }
        });
    }
}

EMSCRIPTEN_KEEPALIVE void lsw_pause(int handle) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->player != nullptr) {
        guardedVoid(*ctx, "pause", [&]() {
            ctx->player->pause();
        });
    }
}

EMSCRIPTEN_KEEPALIVE void lsw_stop(int handle) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->player != nullptr) {
        guardedVoid(*ctx, "stop", [&]() {
            ctx->player->stop();
            (void)renderCurrentFrame(*ctx);
        });
    }
}

EMSCRIPTEN_KEEPALIVE int lsw_tick(int handle) {
    auto* ctx = getContext(handle);
    if (ctx == nullptr || ctx->player == nullptr) {
        return 0;
    }
    try {
        clearError(*ctx);
        const bool keepGoing = ctx->player->tick();
        (void)renderCurrentFrame(*ctx);
        return keepGoing ? 1 : 0;
    } catch (const std::exception& error) {
        setError(*ctx, error.what());
        return 0;
    } catch (...) {
        setError(*ctx, "Unknown tick error");
        return 0;
    }
}

EMSCRIPTEN_KEEPALIVE int lsw_render_frame(int handle) {
    auto* ctx = getContext(handle);
    if (ctx == nullptr) {
        return 0;
    }
    try {
        return renderCurrentFrame(*ctx);
    } catch (const std::exception& error) {
        setError(*ctx, error.what());
        return 0;
    } catch (...) {
        setError(*ctx, "Unknown render error");
        return 0;
    }
}

EMSCRIPTEN_KEEPALIVE const std::uint8_t* lsw_frame_pixels(int handle) {
    auto* ctx = getContext(handle);
    if (ctx == nullptr || ctx->frameRgba.empty()) {
        return nullptr;
    }
    return ctx->frameRgba.data();
}

EMSCRIPTEN_KEEPALIVE int lsw_frame_byte_length(int handle) {
    auto* ctx = getContext(handle);
    return ctx == nullptr ? 0 : static_cast<int>(ctx->frameRgba.size());
}

EMSCRIPTEN_KEEPALIVE int lsw_frame_width(int handle) {
    auto* ctx = getContext(handle);
    return ctx == nullptr ? 0 : ctx->frameWidth;
}

EMSCRIPTEN_KEEPALIVE int lsw_frame_height(int handle) {
    auto* ctx = getContext(handle);
    return ctx == nullptr ? 0 : ctx->frameHeight;
}

EMSCRIPTEN_KEEPALIVE const char* lsw_frame_info_json(int handle) {
    auto* ctx = getContext(handle);
    if (ctx == nullptr) {
        return "";
    }
    try {
        return scratch(*ctx, frameInfoJson(*ctx));
    } catch (const std::exception& error) {
        setError(*ctx, error.what());
        std::ostringstream out;
        out << "{\"width\":" << ctx->frameWidth
            << ",\"height\":" << ctx->frameHeight
            << ",\"tempo\":" << (ctx->player != nullptr ? std::max(1, ctx->player->tempo()) : 15)
            << ",\"error\":";
        appendJsonString(out, ctx->lastError);
        out << ",\"cursor\":{\"code\":-1,\"css\":\"default\"}}";
        return scratch(*ctx, out.str());
    } catch (...) {
        setError(*ctx, "Unknown frame info error");
        std::ostringstream out;
        out << "{\"width\":" << ctx->frameWidth
            << ",\"height\":" << ctx->frameHeight
            << ",\"tempo\":" << (ctx->player != nullptr ? std::max(1, ctx->player->tempo()) : 15)
            << ",\"error\":";
        appendJsonString(out, ctx->lastError);
        out << ",\"cursor\":{\"code\":-1,\"css\":\"default\"}}";
        return scratch(*ctx, out.str());
    }
}

EMSCRIPTEN_KEEPALIVE const char* lsw_frame_sprites_json(int handle) {
    auto* ctx = getContext(handle);
    if (ctx == nullptr) {
        return "{\"sprites\":[]}";
    }
    try {
        return scratch(*ctx, frameSpritesJson(*ctx));
    } catch (const std::exception& error) {
        setError(*ctx, error.what());
        return "{\"sprites\":[]}";
    } catch (...) {
        setError(*ctx, "Unknown frame sprites error");
        return "{\"sprites\":[]}";
    }
}

EMSCRIPTEN_KEEPALIVE const char* lsw_image_operation_trace_json(int handle) {
    auto* ctx = getContext(handle);
    if (ctx == nullptr) {
        return "{\"events\":[]}";
    }
    try {
        return scratch(*ctx, libreshockwave::lingo::vm::imageOperationTraceJson());
    } catch (const std::exception& error) {
        setError(*ctx, error.what());
        return "{\"events\":[]}";
    } catch (...) {
        setError(*ctx, "Unknown image operation trace error");
        return "{\"events\":[]}";
    }
}

EMSCRIPTEN_KEEPALIVE void lsw_clear_image_operation_trace(int handle) {
    if (getContext(handle) != nullptr) {
        libreshockwave::lingo::vm::clearImageOperationTrace();
    }
}

EMSCRIPTEN_KEEPALIVE int lsw_fire_test_error(int handle, const char* message) {
    auto* ctx = getContext(handle);
    if (ctx == nullptr || ctx->player == nullptr) {
        return 0;
    }
    return guardedResult(*ctx, "fire test error", 0, [&]() {
        const std::string text = message != nullptr && *message != '\0'
            ? std::string(message)
            : std::string("LibreShockwave test error");
        return ctx->player->fireTestError(text) ? 1 : 0;
    });
}

EMSCRIPTEN_KEEPALIVE const char* lsw_last_error(int handle) {
    auto* ctx = getContext(handle);
    return ctx == nullptr ? "" : ctx->lastError.c_str();
}

EMSCRIPTEN_KEEPALIVE const char* lsw_poll_debug_messages(int handle) {
    auto* ctx = getContext(handle);
    return ctx == nullptr ? "[]" : scratch(*ctx, debugMessagesJson(*ctx));
}

EMSCRIPTEN_KEEPALIVE void lsw_drain_debug_messages(int handle) {
    if (auto* ctx = getContext(handle)) {
        ctx->debugMessages.clear();
    }
}

EMSCRIPTEN_KEEPALIVE const char* lsw_poll_fetch_requests(int handle) {
    auto* ctx = getContext(handle);
    if (ctx == nullptr || ctx->netProvider == nullptr) {
        return "[]";
    }
    try {
        return scratch(*ctx, fetchRequestsJson(ctx->netProvider->pendingRequests()));
    } catch (const std::exception& error) {
        setError(*ctx, error.what());
        return "[]";
    } catch (...) {
        setError(*ctx, "Unknown fetch request poll error");
        return "[]";
    }
}

EMSCRIPTEN_KEEPALIVE void lsw_drain_fetch_requests(int handle) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->netProvider != nullptr) {
        guardedVoid(*ctx, "drain fetch requests", [&]() {
            ctx->netProvider->drainPendingRequests();
        });
    }
}

EMSCRIPTEN_KEEPALIVE const char* lsw_poll_movie_navigation_requests(int handle) {
    auto* ctx = getContext(handle);
    if (ctx == nullptr || ctx->netProvider == nullptr) {
        return "[]";
    }
    try {
        return scratch(*ctx, fetchRequestsJson(ctx->netProvider->pendingMovieNavigationRequests()));
    } catch (const std::exception& error) {
        setError(*ctx, error.what());
        return "[]";
    } catch (...) {
        setError(*ctx, "Unknown movie navigation poll error");
        return "[]";
    }
}

EMSCRIPTEN_KEEPALIVE void lsw_movie_navigation_complete(int handle, int taskId) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->netProvider != nullptr) {
        guardedVoid(*ctx, "complete movie navigation", [&]() {
            ctx->netProvider->onMovieNavigationComplete(taskId);
        });
    }
}

EMSCRIPTEN_KEEPALIVE void lsw_fetch_complete(int handle,
                                             int taskId,
                                             const std::uint8_t* bytes,
                                             int byteCount) {
    auto* ctx = getContext(handle);
    if (ctx == nullptr || ctx->netProvider == nullptr || bytes == nullptr || byteCount < 0) {
        return;
    }
    guardedVoid(*ctx, "complete fetch", [&]() {
        WasmScriptDeadlineGuard scriptDeadline(ctx->player.get());
        ctx->netProvider->onFetchComplete(taskId, std::vector<std::uint8_t>(bytes, bytes + byteCount));
    });
}

EMSCRIPTEN_KEEPALIVE void lsw_fetch_error(int handle, int taskId, int status) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->netProvider != nullptr) {
        guardedVoid(*ctx, "fail fetch", [&]() {
            ctx->netProvider->onFetchError(taskId, status);
        });
    }
}

EMSCRIPTEN_KEEPALIVE const char* lsw_poll_multiuser_requests(int handle) {
    auto* ctx = getContext(handle);
    if (ctx == nullptr || ctx->multiuserBridge == nullptr) {
        return "[]";
    }
    try {
        return scratch(*ctx, multiuserRequestsJson(ctx->multiuserBridge->pendingRequests()));
    } catch (const std::exception& error) {
        setError(*ctx, error.what());
        return "[]";
    } catch (...) {
        setError(*ctx, "Unknown multiuser poll error");
        return "[]";
    }
}

EMSCRIPTEN_KEEPALIVE void lsw_drain_multiuser_requests(int handle) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->multiuserBridge != nullptr) {
        guardedVoid(*ctx, "drain multiuser requests", [&]() {
            ctx->multiuserBridge->drainPendingRequests();
        });
    }
}

EMSCRIPTEN_KEEPALIVE const char* lsw_poll_audio_commands(int handle) {
    auto* ctx = getContext(handle);
    if (ctx == nullptr || ctx->audioBackend == nullptr) {
        return "[]";
    }
    try {
        return scratch(*ctx, audioCommandsJson(ctx->audioBackend->pendingCommands()));
    } catch (const std::exception& error) {
        setError(*ctx, error.what());
        return "[]";
    } catch (...) {
        setError(*ctx, "Unknown audio command poll error");
        return "[]";
    }
}

EMSCRIPTEN_KEEPALIVE void lsw_drain_audio_commands(int handle) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->audioBackend != nullptr) {
        guardedVoid(*ctx, "drain audio commands", [&]() {
            ctx->audioBackend->drainPending();
        });
    }
}

EMSCRIPTEN_KEEPALIVE void lsw_audio_stopped(int handle, int channelNum) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->audioBackend != nullptr) {
        guardedVoid(*ctx, "audio stopped", [&]() {
            ctx->audioBackend->notifyStopped(channelNum);
        });
    }
}

EMSCRIPTEN_KEEPALIVE void lsw_multiuser_connected(int handle, int instanceId) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->multiuserBridge != nullptr) {
        guardedVoid(*ctx, "multiuser connected", [&]() {
            ctx->multiuserBridge->notifyConnected(instanceId);
            processHostXtraCallbacks(*ctx);
        });
    }
}

EMSCRIPTEN_KEEPALIVE void lsw_multiuser_disconnected(int handle, int instanceId) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->multiuserBridge != nullptr) {
        guardedVoid(*ctx, "multiuser disconnected", [&]() {
            ctx->multiuserBridge->notifyDisconnected(instanceId);
            processHostXtraCallbacks(*ctx);
        });
    }
}

EMSCRIPTEN_KEEPALIVE void lsw_multiuser_error(int handle, int instanceId, int errorCode) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->multiuserBridge != nullptr) {
        guardedVoid(*ctx, "multiuser error", [&]() {
            ctx->multiuserBridge->notifyError(instanceId, errorCode);
            processHostXtraCallbacks(*ctx);
        });
    }
}

EMSCRIPTEN_KEEPALIVE void lsw_multiuser_message_bytes(int handle,
                                                      int instanceId,
                                                      const std::uint8_t* bytes,
                                                      int byteCount) {
    auto* ctx = getContext(handle);
    if (ctx == nullptr || ctx->multiuserBridge == nullptr || bytes == nullptr || byteCount < 0) {
        return;
    }
    guardedVoid(*ctx, "multiuser message", [&]() {
        const auto deliverStarted = std::chrono::steady_clock::now();
        ctx->multiuserBridge->deliverMessageBytes(
            instanceId,
            std::vector<std::uint8_t>(bytes, bytes + byteCount));
        queueSlowOperationMessage(*ctx, "multiuser message delivery", deliverStarted);
        processHostXtraCallbacks(*ctx);
    });
}

EMSCRIPTEN_KEEPALIVE int lsw_director_key_from_browser(int browserKeyCode) {
    return DirectorKeyCodes::fromBrowserKeyCode(browserKeyCode);
}

EMSCRIPTEN_KEEPALIVE int lsw_mouse_move(int handle, int stageX, int stageY) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->player != nullptr) {
        int resultFlags = 0;
        guardedVoid(*ctx, "mouse move", [&]() {
            const int spriteRevisionBefore = ctx->player->stageRenderer().spriteRegistry().revision();
            const int rolloverBefore = ctx->player->inputState().rolloverSprite();
            const bool wasMouseDown = ctx->player->inputState().isMouseDown();
            ctx->player->inputHandler().onMouseMove(stageX, stageY);
            processQueuedInput(*ctx);
            const int spriteRevisionAfter = ctx->player->stageRenderer().spriteRegistry().revision();
            if (wasMouseDown || spriteRevisionAfter != spriteRevisionBefore) {
                if (renderCurrentFrame(*ctx)) {
                    resultFlags |= 1;
                }
            }
            if (ctx->player->inputState().rolloverSprite() != rolloverBefore) {
                resultFlags |= 2;
            }
        });
        return resultFlags;
    }
    return 0;
}

EMSCRIPTEN_KEEPALIVE void lsw_mouse_down(int handle, int stageX, int stageY, int rightButton) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->player != nullptr) {
        guardedVoid(*ctx, "mouse down", [&]() {
            ctx->player->inputHandler().onMouseDown(stageX, stageY, rightButton != 0);
            processQueuedInput(*ctx);
            (void)renderCurrentFrame(*ctx);
        });
    }
}

EMSCRIPTEN_KEEPALIVE void lsw_mouse_up(int handle, int stageX, int stageY, int rightButton) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->player != nullptr) {
        guardedVoid(*ctx, "mouse up", [&]() {
            ctx->player->inputHandler().onMouseUp(stageX, stageY, rightButton != 0);
            processQueuedInput(*ctx);
            (void)renderCurrentFrame(*ctx);
        });
    }
}

EMSCRIPTEN_KEEPALIVE void lsw_blur(int handle) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->player != nullptr) {
        guardedVoid(*ctx, "blur", [&]() {
            ctx->player->inputHandler().onBlur();
            processQueuedInput(*ctx);
            (void)renderCurrentFrame(*ctx);
        });
    }
}

EMSCRIPTEN_KEEPALIVE void lsw_key_down(int handle,
                                       int browserKeyCode,
                                       const char* keyText,
                                       int shift,
                                       int ctrl,
                                       int alt) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->player != nullptr) {
        guardedVoid(*ctx, "key down", [&]() {
            ctx->player->inputHandler().onKeyDown(DirectorKeyCodes::fromBrowserKeyCode(browserKeyCode),
                                                  keyText != nullptr ? std::string(keyText) : std::string(),
                                                  shift != 0,
                                                  ctrl != 0,
                                                  alt != 0);
            processQueuedInput(*ctx);
            (void)renderCurrentFrame(*ctx);
        });
    }
}

EMSCRIPTEN_KEEPALIVE void lsw_key_up(int handle,
                                     int browserKeyCode,
                                     const char* keyText,
                                     int shift,
                                     int ctrl,
                                     int alt) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->player != nullptr) {
        guardedVoid(*ctx, "key up", [&]() {
            ctx->player->inputHandler().onKeyUp(DirectorKeyCodes::fromBrowserKeyCode(browserKeyCode),
                                                keyText != nullptr ? std::string(keyText) : std::string(),
                                                shift != 0,
                                                ctrl != 0,
                                                alt != 0);
            processQueuedInput(*ctx);
            (void)renderCurrentFrame(*ctx);
        });
    }
}

EMSCRIPTEN_KEEPALIVE void lsw_paste_text(int handle, const char* text) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->player != nullptr && text != nullptr) {
        guardedVoid(*ctx, "paste text", [&]() {
            ctx->player->inputHandler().onPasteText(text);
            processQueuedInput(*ctx);
            (void)renderCurrentFrame(*ctx);
        });
    }
}

EMSCRIPTEN_KEEPALIVE const char* lsw_selected_text(int handle) {
    auto* ctx = getContext(handle);
    if (ctx == nullptr || ctx->player == nullptr) {
        return "";
    }
    return guardedResult<const char*>(*ctx, "selected text", "", [&]() {
        const auto text = ctx->player->inputHandler().getSelectedText();
        return scratch(*ctx, text.value_or(std::string()));
    });
}

EMSCRIPTEN_KEEPALIVE void lsw_select_all(int handle) {
    if (auto* ctx = getContext(handle); ctx != nullptr && ctx->player != nullptr) {
        guardedVoid(*ctx, "select all", [&]() {
            ctx->player->inputHandler().selectAll();
            (void)renderCurrentFrame(*ctx);
        });
    }
}

EMSCRIPTEN_KEEPALIVE const char* lsw_cut_selected_text(int handle) {
    auto* ctx = getContext(handle);
    if (ctx == nullptr || ctx->player == nullptr) {
        return "";
    }
    return guardedResult<const char*>(*ctx, "cut selected text", "", [&]() {
        const auto text = ctx->player->inputHandler().cutSelectedText();
        (void)renderCurrentFrame(*ctx);
        return scratch(*ctx, text.value_or(std::string()));
    });
}

} // extern "C"
