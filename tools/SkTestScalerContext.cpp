/*
 * Copyright 2014 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkAdvancedTypefaceMetrics.h"
#include "SkBitmap.h"
#include "SkCanvas.h"
#include "SkDescriptor.h"
#include "SkFontDescriptor.h"
#include "SkGlyph.h"
#include "SkMakeUnique.h"
#include "SkMask.h"
#include "SkOTUtils.h"
#include "SkPaintPriv.h"
#include "SkScalerContext.h"
#include "SkTestScalerContext.h"
#include "SkTypefaceCache.h"
#include "SkUtils.h"

SkTestFont::SkTestFont(const SkTestFontData& fontData)
    : INHERITED()
    , fCharCodes(fontData.fCharCodes)
    , fCharCodesCount(fontData.fCharCodes ? fontData.fCharCodesCount : 0)
    , fWidths(fontData.fWidths)
    , fMetrics(fontData.fMetrics)
    , fName(fontData.fName)
    , fPaths(nullptr)
{
    init(fontData.fPoints, fontData.fVerbs);
}

SkTestFont::~SkTestFont() {
    for (unsigned index = 0; index < fCharCodesCount; ++index) {
        delete fPaths[index];
    }
    delete[] fPaths;
}

int SkTestFont::codeToIndex(SkUnichar charCode) const {
    for (unsigned index = 0; index < fCharCodesCount; ++index) {
        if (fCharCodes[index] == (unsigned) charCode) {
            return (int) index;
        }
    }
    return 0;
}

void SkTestFont::init(const SkScalar* pts, const unsigned char* verbs) {
    fPaths = new SkPath* [fCharCodesCount];
    for (unsigned index = 0; index < fCharCodesCount; ++index) {
        SkPath* path = new SkPath;
        SkPath::Verb verb;
        while ((verb = (SkPath::Verb) *verbs++) != SkPath::kDone_Verb) {
            switch (verb) {
                case SkPath::kMove_Verb:
                    path->moveTo(pts[0], pts[1]);
                    pts += 2;
                    break;
                case SkPath::kLine_Verb:
                    path->lineTo(pts[0], pts[1]);
                    pts += 2;
                    break;
                case SkPath::kQuad_Verb:
                    path->quadTo(pts[0], pts[1], pts[2], pts[3]);
                    pts += 4;
                    break;
                case SkPath::kCubic_Verb:
                    path->cubicTo(pts[0], pts[1], pts[2], pts[3], pts[4], pts[5]);
                    pts += 6;
                    break;
                case SkPath::kClose_Verb:
                    path->close();
                    break;
                default:
                    SkDEBUGFAIL("bad verb");
                    return;
            }
        }
        // This should make SkPath::getBounds() queries threadsafe.
        path->updateBoundsCache();
        fPaths[index] = path;
    }
}

SkTestTypeface::SkTestTypeface(sk_sp<SkTestFont> testFont, const SkFontStyle& style)
    : SkTypeface(style, false)
    , fTestFont(std::move(testFont)) {
}

void SkTestTypeface::getAdvance(SkGlyph* glyph) {
    // TODO(benjaminwagner): Update users to use floats.
    glyph->fAdvanceX = SkFixedToFloat(fTestFont->fWidths[glyph->getGlyphID()]);
    glyph->fAdvanceY = 0;
}

void SkTestTypeface::getFontMetrics(SkPaint::FontMetrics* metrics) {
    *metrics = fTestFont->fMetrics;
}

void SkTestTypeface::getMetrics(SkGlyph* glyph) {
    SkGlyphID glyphID = glyph->getGlyphID();
    glyphID = glyphID < fTestFont->fCharCodesCount ? glyphID : 0;

    // TODO(benjaminwagner): Update users to use floats.
    glyph->fAdvanceX = SkFixedToFloat(fTestFont->fWidths[glyphID]);
    glyph->fAdvanceY = 0;
}

void SkTestTypeface::getPath(SkGlyphID glyphID, SkPath* path) {
    glyphID = glyphID < fTestFont->fCharCodesCount ? glyphID : 0;
    *path = *fTestFont->fPaths[glyphID];
}

void SkTestTypeface::onFilterRec(SkScalerContextRec* rec) const {
    rec->setHinting(SkPaint::kNo_Hinting);
}

std::unique_ptr<SkAdvancedTypefaceMetrics> SkTestTypeface::onGetAdvancedMetrics() const { // pdf only
    std::unique_ptr<SkAdvancedTypefaceMetrics> info(new SkAdvancedTypefaceMetrics);
    info->fFontName.set(fTestFont->fName);
    int glyphCount = this->onCountGlyphs();

    SkTDArray<SkUnichar>& toUnicode = info->fGlyphToUnicode;
    toUnicode.setCount(glyphCount);
    SkASSERT(glyphCount == SkToInt(fTestFont->fCharCodesCount));
    for (int gid = 0; gid < glyphCount; ++gid) {
        toUnicode[gid] = SkToS32(fTestFont->fCharCodes[gid]);
    }
    return info;
}

void SkTestTypeface::onGetFontDescriptor(SkFontDescriptor* desc, bool* isLocal) const {
    desc->setFamilyName(fTestFont->fName);
    desc->setStyle(this->fontStyle());
    *isLocal = false;
}

int SkTestTypeface::onCharsToGlyphs(const void* chars, Encoding encoding,
                                    uint16_t glyphs[], int glyphCount) const {
    auto utf8  = (const      char*)chars;
    auto utf16 = (const  uint16_t*)chars;
    auto utf32 = (const SkUnichar*)chars;

    for (int i = 0; i < glyphCount; i++) {
        SkUnichar ch;
        switch (encoding) {
            case kUTF8_Encoding:  ch =  SkUTF8_NextUnichar(&utf8 ); break;
            case kUTF16_Encoding: ch = SkUTF16_NextUnichar(&utf16); break;
            case kUTF32_Encoding: ch =                    *utf32++; break;
        }
        glyphs[i] = fTestFont->codeToIndex(ch);
    }
    return glyphCount;
}

void SkTestTypeface::onGetFamilyName(SkString* familyName) const {
    *familyName = fTestFont->fName;
}

SkTypeface::LocalizedStrings* SkTestTypeface::onCreateFamilyNameIterator() const {
    SkString familyName(fTestFont->fName);
    SkString language("und"); //undetermined
    return new SkOTUtils::LocalizedStrings_SingleName(familyName, language);
}

class SkTestScalerContext : public SkScalerContext {
public:
    SkTestScalerContext(sk_sp<SkTestTypeface> face, const SkScalerContextEffects& effects,
                        const SkDescriptor* desc)
        : SkScalerContext(std::move(face), effects, desc)
    {
        fRec.getSingleMatrix(&fMatrix);
        this->forceGenerateImageFromPath();
    }

protected:
    SkTestTypeface* getTestTypeface() const {
        return static_cast<SkTestTypeface*>(this->getTypeface());
    }

    unsigned generateGlyphCount() override {
        return this->getTestTypeface()->onCountGlyphs();
    }

    uint16_t generateCharToGlyph(SkUnichar uni) override {
        uint16_t glyph;
        (void) this->getTestTypeface()->onCharsToGlyphs((const void *) &uni,
                                                        SkTypeface::kUTF32_Encoding, &glyph, 1);
        return glyph;
    }

    void generateAdvance(SkGlyph* glyph) override {
        this->getTestTypeface()->getAdvance(glyph);

        const SkVector advance = fMatrix.mapXY(SkFloatToScalar(glyph->fAdvanceX),
                                               SkFloatToScalar(glyph->fAdvanceY));
        glyph->fAdvanceX = SkScalarToFloat(advance.fX);
        glyph->fAdvanceY = SkScalarToFloat(advance.fY);
    }

    void generateMetrics(SkGlyph* glyph) override {
        this->getTestTypeface()->getMetrics(glyph);

        const SkVector advance = fMatrix.mapXY(SkFloatToScalar(glyph->fAdvanceX),
                                               SkFloatToScalar(glyph->fAdvanceY));
        glyph->fAdvanceX = SkScalarToFloat(advance.fX);
        glyph->fAdvanceY = SkScalarToFloat(advance.fY);

        SkPath path;
        this->getTestTypeface()->getPath(glyph->getGlyphID(), &path);
        path.transform(fMatrix);

        SkRect storage;
        const SkPaint paint;
        const SkRect& newBounds = paint.doComputeFastBounds(path.getBounds(),
                                                            &storage,
                                                            SkPaint::kFill_Style);
        SkIRect ibounds;
        newBounds.roundOut(&ibounds);
        glyph->fLeft = ibounds.fLeft;
        glyph->fTop = ibounds.fTop;
        glyph->fWidth = ibounds.width();
        glyph->fHeight = ibounds.height();
    }

    void generateImage(const SkGlyph& glyph) override {
        SkPath path;
        this->getTestTypeface()->getPath(glyph.getGlyphID(), &path);

        SkBitmap bm;
        bm.installPixels(SkImageInfo::MakeN32Premul(glyph.fWidth, glyph.fHeight),
                            glyph.fImage, glyph.rowBytes());
        bm.eraseColor(0);

        SkCanvas canvas(bm);
        canvas.translate(-SkIntToScalar(glyph.fLeft),
                            -SkIntToScalar(glyph.fTop));
        canvas.concat(fMatrix);
        SkPaint paint;
        paint.setAntiAlias(true);
        canvas.drawPath(path, paint);
    }

    void generatePath(SkGlyphID glyph, SkPath* path) override {
        this->getTestTypeface()->getPath(glyph, path);
        path->transform(fMatrix);
    }

    void generateFontMetrics(SkPaint::FontMetrics* metrics) override {
        this->getTestTypeface()->getFontMetrics(metrics);
        SkPaintPriv::ScaleFontMetrics(metrics, fMatrix.getScaleY());
    }

private:
    SkMatrix         fMatrix;
};

SkScalerContext* SkTestTypeface::onCreateScalerContext(
    const SkScalerContextEffects& effects, const SkDescriptor* desc) const
{
    return new SkTestScalerContext(sk_ref_sp(const_cast<SkTestTypeface*>(this)), effects, desc);
}
