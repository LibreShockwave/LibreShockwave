package com.libreshockwave.player;

import com.libreshockwave.bitmap.Bitmap;
import com.libreshockwave.cast.BitmapInfo;
import com.libreshockwave.player.cast.CastLib;
import com.libreshockwave.cast.MemberType;
import com.libreshockwave.chunks.CastMemberChunk;
import com.libreshockwave.player.cast.CastLibManager;
import com.libreshockwave.player.cast.CastMember;
import com.libreshockwave.player.render.SpriteRegistry;
import com.libreshockwave.player.sprite.SpriteState;
import com.libreshockwave.vm.datum.Datum;
import com.libreshockwave.vm.builtin.sprite.SpritePropertyProvider;

/**
 * Provides sprite property access for Lingo scripts.
 * Implements sprite property expressions like:
 * - the locH of sprite 1
 * - the visible of sprite 5
 * - set the locV of sprite 3 to 100
 * - sprite(1).member = member("logo")
 */
public class SpriteProperties implements SpritePropertyProvider {

    private final SpriteRegistry registry;
    private CastLibManager castLibManager;

    public SpriteProperties(SpriteRegistry registry) {
        this.registry = registry;
    }

    public void setCastLibManager(CastLibManager clm) {
        this.castLibManager = clm;
    }

    @Override
    public java.util.List<Datum> getScriptInstanceList(int spriteNum) {
        SpriteState sprite = registry.get(spriteNum);
        if (sprite == null) return null;
        java.util.List<Datum> list = sprite.getScriptInstanceList();
        return (list != null && !list.isEmpty()) ? list : null;
    }

    @Override
    public Datum getSpriteProp(int spriteNum, String propName) {
        SpriteState sprite = registry.get(spriteNum);
        if (sprite == null) {
            // Return defaults for non-existent sprites
            String prop = propName.toLowerCase();
            return switch (prop) {
                case "puppet" -> Datum.of(0);
                case "visible" -> Datum.of(0);
                case "loch", "locv", "width", "height", "left", "top", "ink",
                     "castnum", "membernum", "blend", "stretch", "locz",
                     "forecolor", "backcolor" -> Datum.ZERO;
                case "loc" -> new Datum.Point(0, 0);
                case "rect" -> new Datum.Rect(0, 0, 0, 0);
                case "spritenum" -> Datum.of(spriteNum);
                case "type" -> Datum.of(0);
                case "member" -> Datum.VOID;
                case "ilk" -> Datum.symbol("sprite");
                default -> Datum.VOID;
            };
        }

        String prop = propName.toLowerCase();
        return switch (prop) {
            case "loch" -> Datum.of(sprite.getLocH());
            case "locv" -> Datum.of(sprite.getLocV());
            case "locz" -> Datum.of(sprite.getLocZ());
            case "loc" -> new Datum.Point(sprite.getLocH(), sprite.getLocV());
            case "width" -> Datum.of(sprite.getWidth());
            case "height" -> Datum.of(sprite.getHeight());
            case "visible" -> Datum.of(sprite.isVisible() ? 1 : 0);
            case "puppet" -> Datum.of(sprite.isPuppet() ? 1 : 0);
            case "ink" -> Datum.of(sprite.getInk());
            case "blend" -> Datum.of(sprite.getBlend());
            case "stretch" -> Datum.of(sprite.getStretch());
            case "forecolor" -> Datum.of(sprite.getForeColor());
            case "backcolor" -> Datum.of(sprite.getBackColor());
            case "left" -> Datum.of(sprite.getLocH());
            case "top" -> Datum.of(sprite.getLocV());
            case "right" -> Datum.of(sprite.getLocH() + sprite.getWidth());
            case "bottom" -> Datum.of(sprite.getLocV() + sprite.getHeight());
            case "rect" -> new Datum.Rect(
                sprite.getLocH(),
                sprite.getLocV(),
                sprite.getLocH() + sprite.getWidth(),
                sprite.getLocV() + sprite.getHeight()
            );
            case "spritenum" -> Datum.of(spriteNum);
            case "type" -> Datum.of(1);  // 1 = bitmap default
            case "castnum", "membernum" -> Datum.of(sprite.getEffectiveCastMember());
            case "castlibnum" -> Datum.of(sprite.getEffectiveCastLib());
            case "member" -> {
                int cl = sprite.getEffectiveCastLib();
                int cm = sprite.getEffectiveCastMember();
                yield cm > 0 ? Datum.CastMemberRef.of(cl, cm) : Datum.VOID;
            }
            case "image" -> {
                // Director's sprite(n).image returns the sprite's member's image.
                // For bitmap members this is a live reference (modifications persist).
                // For text members this is a rendered snapshot.
                if (castLibManager != null) {
                    int cl = sprite.getEffectiveCastLib();
                    int cm = sprite.getEffectiveCastMember();
                    CastMember member = castLibManager.getDynamicMember(cl, cm);
                    if (member != null) {
                        yield member.getProp("image");
                    }
                    // sprite.image lookup failed: member not found for this sprite
                }
                yield Datum.VOID;
            }
            case "ilk" -> Datum.symbol("sprite");
            case "rotation" -> Datum.of(sprite.getRotation());
            case "skew" -> Datum.of(sprite.getSkew());
            case "fliph" -> Datum.of(sprite.isFlipH() ? 1 : 0);
            case "flipv" -> Datum.of(sprite.isFlipV() ? 1 : 0);
            case "moveable", "moveablesprite" -> Datum.of(0);
            case "editable", "editabletext" -> Datum.of(0);
            case "trails" -> Datum.of(sprite.getTrails());
            case "cursor" -> Datum.of(sprite.getCursor());
            case "scriptinstancelist" -> new Datum.List(new java.util.ArrayList<>(sprite.getScriptInstanceList()));
            default -> {
                // Don't spam for common properties
                if (!prop.equals("name") && !prop.equals("constraint")
                        && !prop.equals("tweened") && !prop.equals("scriptnum")) {
                    System.err.println("[SpriteProperties] Unknown sprite property get: " + propName);
                }
                yield Datum.VOID;
            }
        };
    }

    @Override
    public boolean setSpriteProp(int spriteNum, String propName, Datum value) {
        // Auto-create dynamic sprite if it doesn't exist
        SpriteState sprite = registry.getOrCreateDynamic(spriteNum);

        String prop = propName.toLowerCase();
        // Bump revision so SoftwareRenderer cache invalidates for single-frame movies
        registry.bumpRevision();
        switch (prop) {
            case "loch" -> {
                sprite.setLocH(value.toInt());
                return true;
            }
            case "locv" -> {
                sprite.setLocV(value.toInt());
                return true;
            }
            case "locz" -> {
                sprite.setLocZ(value.toInt());
                return true;
            }
            case "loc" -> {
                if (value instanceof Datum.Point p) {
                    sprite.setLocH(p.x());
                    sprite.setLocV(p.y());
                    return true;
                }
                return false;
            }
            case "rect" -> {
                if (value instanceof Datum.Rect r) {
                    sprite.setLocH(r.left());
                    sprite.setLocV(r.top());
                    sprite.setWidth(r.right() - r.left());
                    sprite.setHeight(r.bottom() - r.top());
                    return true;
                }
                return false;
            }
            case "visible" -> {
                sprite.setVisible(value.isTruthy());
                return true;
            }
            case "puppet" -> {
                sprite.setPuppet(value.isTruthy());
                return true;
            }
            case "ink" -> {
                // Director ignores VOID values - keeps the current ink
                if (!value.isVoid()) {
                    sprite.setInk(value.toInt());
                }
                return true;
            }
            case "blend" -> {
                // Director ignores VOID values - keeps the current blend (default 100)
                if (!value.isVoid()) {
                    sprite.setBlend(value.toInt());
                }
                return true;
            }
            case "stretch" -> {
                sprite.setStretch(value.toInt());
                return true;
            }
            case "trails" -> {
                sprite.setTrails(value.toInt());
                return true;
            }
            case "width" -> {
                sprite.setWidth(value.toInt());
                return true;
            }
            case "height" -> {
                sprite.setHeight(value.toInt());
                return true;
            }
            case "image" -> {
                // Director's sprite(n).image = img sets the sprite's member's image.
                if (castLibManager != null) {
                    int cl = sprite.getEffectiveCastLib();
                    int cm = sprite.getEffectiveCastMember();
                    CastMember member = castLibManager.getDynamicMember(cl, cm);
                    if (member != null) {
                        return member.setProp("image", value);
                    }
                    // Sprite has no member — auto-create a bitmap member and assign it.
                    // Director allows sprite.image = img even when no member is set;
                    // it implicitly creates a bitmap member to hold the image.
                    if (value instanceof Datum.ImageRef imgRef && imgRef.bitmap() != null) {
                        Bitmap bmp = imgRef.bitmap();
                        int targetCastLib = cl > 0 ? cl : 1;
                        CastLib castLib = castLibManager.getCastLib(targetCastLib);
                        if (castLib != null) {
                            CastMember newMember = castLib.createDynamicMember("bitmap");
                            if (newMember != null) {
                                newMember.setProp("image", value);
                                sprite.setDynamicMember(targetCastLib, newMember.getMemberNumber());
                                sprite.setWidth(bmp.getWidth());
                                sprite.setHeight(bmp.getHeight());
                            }
                        }
                    }
                }
                return true;
            }
            case "forecolor" -> {
                if (!value.isVoid()) {
                    sprite.setForeColor(value.toInt());
                }
                return true;
            }
            case "backcolor" -> {
                if (!value.isVoid()) {
                    sprite.setBackColor(value.toInt());
                }
                return true;
            }
            case "member" -> {
                if (value instanceof Datum.CastMemberRef cmr) {
                    sprite.setDynamicMember(cmr.castLibNum(), cmr.memberNum());
                    autoSizeSprite(sprite, cmr.castLibNum(), cmr.memberNum());
                } else if (value.isString() && castLibManager != null) {
                    // Director resolves string member names: sprite.member = "name"
                    Datum ref = castLibManager.getMemberByName(0, value.toStr());
                    if (ref instanceof Datum.CastMemberRef cmr) {
                        sprite.setDynamicMember(cmr.castLibNum(), cmr.memberNum());
                        autoSizeSprite(sprite, cmr.castLibNum(), cmr.memberNum());
                    }
                } else {
                    sprite.setDynamicMember(0, value.toInt());
                    autoSizeSprite(sprite, 0, value.toInt());
                }
                return true;
            }
            case "castnum", "membernum" -> {
                int num = value.toInt();
                // Decode encoded slot numbers: (castLib << 16) | memberNum
                // These come from preIndexMembers → member.number in Director
                int encodedCast = (num >> 16) & 0xFFFF;
                int encodedMember = num & 0xFFFF;
                if (encodedCast > 0 && encodedMember > 0) {
                    sprite.setDynamicMember(encodedCast, encodedMember);
                    autoSizeSprite(sprite, encodedCast, encodedMember);
                } else {
                    int cl = sprite.getEffectiveCastLib();
                    sprite.setDynamicMember(cl, num);
                    autoSizeSprite(sprite, cl, num);
                }
                return true;
            }
            case "color" -> {
                setColorValue(value, sprite::setForeColor);
                return true;
            }
            case "bgcolor" -> {
                setColorValue(value, sprite::setBackColor);
                return true;
            }
            case "rotation" -> {
                sprite.setRotation(value.toDouble());
                return true;
            }
            case "skew" -> {
                sprite.setSkew(value.toDouble());
                return true;
            }
            case "fliph" -> {
                sprite.setFlipH(value.isTruthy());
                return true;
            }
            case "flipv" -> {
                sprite.setFlipV(value.isTruthy());
                return true;
            }
            case "scriptinstancelist" -> {
                if (value instanceof Datum.List list) {
                    // Store spriteNum on each ScriptInstance so behavior scripts
                    // can access me.spriteNum (Director built-in for behaviors)
                    for (Datum item : list.items()) {
                        if (item instanceof Datum.ScriptInstance si) {
                            si.properties().put("spritenum", Datum.of(spriteNum));
                        }
                    }
                    sprite.setScriptInstanceList(list.items());
                } else {
                    sprite.setScriptInstanceList(java.util.List.of());
                }
                return true;
            }
            case "cursor" -> {
                // Director cursor: -1=arrow, 0=default, 1=ibeam, 2=crosshair, 3=crossbar, 4=wait
                // Can also be a list [memberRef, maskMemberRef] for bitmap cursors
                if (value instanceof Datum.List list && list.items().size() >= 2) {
                    int memberNum = encodeCursorMember(list.items().get(0));
                    int maskNum = encodeCursorMember(list.items().get(1));
                    sprite.setCursorMembers(memberNum, maskNum);
                } else {
                    sprite.setCursor(value.toInt());
                }
                return true;
            }
            // Silently accept but don't do anything special
            case "moveable", "moveablesprite", "editable", "editabletext",
                 "tweened", "constraint", "scriptnum", "type", "id" -> {
                return true;
            }
            default -> {
                // Don't spam for common no-op properties
                if (!prop.equals("name") && !prop.equals("scorecolor")) {
                    System.err.println("[SpriteProperties] Unknown sprite property set: " + propName);
                }
                return false;
            }
        }
    }

    private static void setColorValue(Datum value, java.util.function.IntConsumer setter) {
        if (!value.isVoid()) {
            if (value instanceof Datum.Color c) {
                setter.accept((c.r() << 16) | (c.g() << 8) | c.b());
            } else {
                setter.accept(value.toInt());
            }
        }
    }

    /**
     * Auto-size the sprite to match its member's natural dimensions.
     * Director automatically adjusts sprite width/height when member is assigned.
     * This enables Lingo-created sprites (e.g. Logo during loading) to render
     * at their correct size without explicit width/height setting.
     */
    private void autoSizeSprite(SpriteState sprite, int castLib, int memberNum) {
        if (castLibManager == null || memberNum <= 0) return;

        // Try file-loaded member first (CastMemberChunk)
        CastMemberChunk chunk = castLibManager.getCastMember(castLib, memberNum);
        if (chunk != null) {
            if (chunk.isBitmap() && chunk.specificData() != null && chunk.specificData().length >= 10) {
                BitmapInfo bi = BitmapInfo.parse(chunk.specificData());
                sprite.applyIntrinsicSize(bi.width(), bi.height());
                return;
            }
            // For text/button members, use member type specific dimensions
            if (chunk.memberType() == MemberType.TEXT || chunk.memberType() == MemberType.BUTTON) {
                CastMember dm = castLibManager.getDynamicMember(castLib, memberNum);
                if (dm != null) {
                    sprite.applyIntrinsicSize(dm.getProp("width").toInt(), dm.getProp("height").toInt());
                }
            }
            return;
        }

        // Try dynamic member (created at runtime via new(#type, castLib))
        CastMember dm = castLibManager.getDynamicMember(castLib, memberNum);
        if (dm != null) {
            sprite.applyIntrinsicSize(dm.getProp("width").toInt(), dm.getProp("height").toInt());
        }
    }

    /**
     * Encode a cursor member datum into the (castLib << 16 | memberNum) format.
     * Handles both CastMemberRef datums (from member() calls) and raw integers.
     */
    private static int encodeCursorMember(Datum d) {
        if (d instanceof Datum.CastMemberRef ref) {
            return (ref.castLibNum() << 16) | ref.memberNum();
        }
        return d.toInt();
    }
}
