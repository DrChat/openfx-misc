/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2013-2016 INRIA
 *
 * openfx-misc is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * openfx-misc is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with openfx-misc.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
 * ***** END LICENSE BLOCK ***** */

#ifdef DEBUG // not yet ready for release

/*
 * OFX PIK plugin.
 */
/*
 IBK tutorials:
 

 Nuke doc tutorial:
 http://help.thefoundry.co.uk/nuke/content/getting_started/tutorial3/image_based_keying.html

 Video tutorial by Steve Wright:
 https://www.youtube.com/watch?v=-GmMC0AYXJ4

 Advanced tutorial:
 https://compositingmentor.com/2014/07/19/advanced-keying-breakdown-alpha-1-4-ibk-stacked-technique/
 
*/
/*
 http://tiraoka.blogspot.fr/2015/07/secret-of-ibk.html

 secret of IBK


 There are some nice tutorials about IBK in the following link,

 http://compositingmentor.com

 This is pretty much amazing! Thank you pretty much, Tony.

 IBK node which IBK Colour and IBK Gizmo have it inside themselves can be broken down into the following formula.

 alpha = (Ag-Ar*rw-Ab*gbw)<=0?1:clamp(1-(Ag-Ar*rw-Ab*gbw)/(Bg-Br*rw-Bb*gbw))

 A is pfg and B is c. and this is the the case of "Green" keying, I mean we choose "Green" on IBK.
 rw is the value of "red weight" and gbw is the value of "green/blur weight".

 So, When preparing clean plate with IBK Colour, we need to tweak the value of the "darks" and the "lights" on itself. The "darks" is the "offset" of the Grade node which affects on input plate in IBK Colour. The "lights" is the "multiple" of the Grade node as well.

 Anyway, we compare green and red + blue. If the pixel goes "green > red + blue", the pixel would be remained. Or if the pixel goes to "green < red + blue", the pixel would be turned to black. I mean IBK Colour-wise.

 When the green in the green screen looks saturated, I usually take the red or the blue value of "lights" up.
 When the green in the green screen doesn't so saturated, I usually take the red or the blue value of "lights" down. This is the case of Green screen.


 the top node(IBK Colour) of IBK Stack, this is the case of "Saturated"
 the top node(IBK Colour) of IBK Stack, this is the case of "Less Saturated"
 the clean plate which is the resulted of IBK stack.

 checking for key extract

 And "use bkg luminance" on "IBK Gizmo" works like "Additive Keyer"
 See http://www.nukepedia.com/written-tutorials/additive-keyer/
 This is also awesome great tutorial.
 
 */

/*
http://www.jahshaka.com/forums/archive/index.php/t-16044.html

 Keylight is definitely not a chroma keyer. It's a color difference keyer that uses a mix (in Shake-speak) i.e. blend i.e. dissolve operation instead of a max operation to combine the non-backing screen channels. For a green screen the math would be g-(r*c+b*(1-c)) where c controls the mix between the red and the blue channel. This approach generally gives better results for transparent objects, hair, motion blur, defocus, etc. compared to keyers which use max, but it's biggest problem is that it produces a weaker core matte. It's especially sensitive to secondary colors which contain the backing screen color (i.e. yellow and cyan for a green screen). 
 
 IBK is a color difference keyer with a very simple basic algorithm. In case of a green screen the math is g-(r*rw+b*bw), where rw is the red weight and bw is the blue weight with a default value of 0.5 for both. What makes it sophisticated (among other things) is the way it uses another image to scale the result of the above mentioned equation.

 Every keyer scales (normalizes) the result of it's basic algorithm so that, on one end, you get 1 for the pixels that match the chosen screen color, and 0, on the other end, for the pixels that contain no or little of the primary color of the backing screen (this is afterward inverted so you end up with black for the transparent parts of the image and white for the opaque parts).

 Keylight, for example, scales the result of it's basic algorithm (which is g-(r*0.5+b*0.5), the same as IBK by default) by dividing it with the the result of gc-(rc*0.5+bc*0.5), where rc,gc and gc are the red, green and blue values of the chosen screen color. IBK does the same if you set "pick" as the screen type and select the backing screen color. If you set screen type to "C-green" or "C-blue" instead of using a single value for normalizing the result of the basic equation (i.e. the unscaled matte image), it processes a "control" image with the gc-(rc*rw+bc*bw) formula pixel by pixel, and then divides the unscaled matte image with the processed control image.
 */
/*
 about keying in general:
 https://bradwoodgate.files.wordpress.com/2011/06/i7824248innovations.pdf
 
 how to shoot a good keyable greenscreen:
 http://vfxio.com/PDFs/Screaming_at_the_Greenscreen.pdf
 */

#include <cmath>
#include <cfloat> // DBL_MAX
#include <limits>
#include <algorithm>
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#include <windows.h>
#endif

#include "ofxsProcessing.H"
#include "ofxsMacros.h"
#include "ofxsMaskMix.h"

#ifndef M_PI
#define M_PI        3.14159265358979323846264338327950288   /* pi             */
#endif

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "PIK"
#define kPluginGrouping "Keyer"
#define kPluginDescription \
    "A keyer that works by generating a clean plate from the green/blue screen sequences. Inspired by Nuke's IBK by Paul Lambert and Fusion's KAK by Pieter Van Houte.\n" \
"\n" \
"There are 2 basic approaches to pull a key with PIK. One is to use PIKColor to extract automatically a clean plate from the images and use it as the the C input, and the other is to pick a color which best represents the area you are trying to key.\n" \
"\n" \
"The blue- or greenscreen image should be used as the Fg input. If that image contains significant noise, a denoised version should be used as the PFg input. The C input should either be a clean plate or the outupt of PIKColor, and is used as the screen color if the 'Screen Type' is not 'Pick'. The Bg image is used in calculating fine edge detail when either 'Use Bg Luminance' or 'Use Bg Chroma' is checked.\n" \
"\n" \
"The color weights deal with the hardness of the matte. If you view the output (with screen subtraction ticked on) you will typically see areas where edges have a slight discoloration due to the background not being fully removed from the original plate. This is not spill but a result of the matte being too strong. Lowering one of the weights will correct that particular edge. If it's a red foreground image with an edge problem bring down the red weight - same idea for the other weight. This may affect other edges so the use of multiple PIKs with different weights split with KeyMixes is recommended.\n" \
"\n" \
"The 'Luminance Match' feature adds a luminance factor to the keying algorithm which helps to capture transparent areas of the foreground which are brighter than the backing screen. It will also allow you to lessen some of the garbage area noise by bringing down the screen range - pushing this control too far will also eat into some of your foreground blacks. 'Luminance Level' allows you to make the overall effect stronger or weaker.\n" \
"\n" \
"'Autolevels' will perform a color correction before the image is pulled so that hard edges from a foreground subject with saturated colors are reduced. The same can be achieved with the weights but here only those saturated colors are affected whereas the use of weights will affect the entire image. When using this feature it's best to have this as a separate node which you can then split with other PIKs as the weights will no longer work as expected. You can override some of the logic for when you actually have particular foreground colors you want to keep.\n" \
"For example when you have a saturated red subject against bluescreen you'll get a magenta transition area. Autolevels will eliminate this but if you have a magenta foreground object then this control will make the magenta more red unless you check the magenta box to keep it.\n" \
"\n" \
"'Screen Subtraction' will remove the backing from the rgb via a subtraction process. Unchecking this will simply premultiply the original Fg with the generated matte.\n" \
"\n" \
"'Use Bkg Luminance' and 'Use Bkg Chroma' allow you to affect the output rgb by the new bg. These controls are best used with the 'Luminance Match' sliders above. This feature can also sometimes really help with screens that exhibit some form of fringing artifact - usually a darkening or lightening of an edge on one of the color channels on the screen. You can offset the effect by grading the bg input up or down with a grade node just before input. If it's just an area which needs help then just bezier that area and locally grade the bg input up or down to remove the artifact.\n" \


#define kPluginIdentifier "net.sf.openfx.PIK"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kClipFg "Fg"
#define kClipFgHint "The blue- or greenscreen image. Used to compute the output color."
#define kClipPFg "PFg"
#define kClipPFgHint "(optional) The preprocessed/denoised blue- or greenscreen image. Used to compute the output key (alpha). A denoised image usually gives a less noisy key. If not connected, the Fg input is used instead."
#define kClipC "C"
#define kClipCHint "(optional) A clean plate, or the output of PIKColor"
#define kClipBg "Bg"
#define kClipBgHint "(optional) The background image. This is used in calculating fine edge detail when the 'Use Bg Luminance' or 'Use Bg Chroma' options are checked."

#define kParamScreenType "screenType"
#define kParamScreenTypeLabel "Screen Type"
#define kParamScreenTypeHint "The type of background screen used for the key."
#define kParamScreenTypeOptionGreen "C-Green"
#define kParamScreenTypeOptionBlue "C-Blue"
#define kParamScreenTypeOptionPick "Pick"
enum ScreenTypeEnum {
  eScreenTypeGreen = 0,
    eScreenTypeBlue,
    eScreenTypePick,
};
#define kParamScreenTypeDefault eScreenTypeBlue

#define kParamColor "color"
#define kParamColorLabel "Color"
#define kParamColorHint "The screen color in case 'Pick' was chosen as the 'Screen Type'."

#define kParamRedWeight "redWeight"
#define kParamRedWeightLabel "Red Weight"
#define kParamRedWeightHint "Determines how the red channel and complement channel (blue for a green screen, green for a blue screen) are weighted in the keying calculation."
#define kParamRedWeightDefault 0.5 // 1 in IBK, 0.5 in IBKGizmo

#define kParamBlueGreenWeight "blueGreenWeight"
#define kParamBlueGreenWeightLabel "Blue/Green Weight"
#define kParamBlueGreenWeightHint "Determines how the red channel and complement channel (blue for a green screen, green for a blue screen) are weighted in the keying calculation."
#define kParamBlueGreenWeightDefault 0.5 // 0 in IBK, 0.5 in IBKGizmo

#define kParamLMEnable "lmEnable"
#define kParamLMEnableLabel "Luminance Match Enable"
#define kParamLMEnableHint "Adds a luminance factor to the color difference algorithm."
#define kParamLMEnableDefault false

#define kParamLevel "level"
#define kParamLevelLabel "Screen Range"
#define kParamLevelHint "Helps retain blacks and shadows."
#define kParamLevelDefault 1

#define kParamLuma "luma"
#define kParamLumaLabel "Luminance Level"
#define kParamLumaHint "Makes the matte more additive."
#define kParamLumaDefault 0 // 0.5 in IBK, 0 in IBKGizmo

#define kParamLLEnable "llEnable"
#define kParamLLEnableLabel "Enable"
#define kParamLLEnableHint "Disable the luminance level when us bg influence."
#define kParamLLEnableDefault false

#define kParamAutolevels "autolevels"
#define kParamAutolevelsLabel "Autolevels"
#define kParamAutolevelsHint "Removes hard edges from the matte."
#define kParamAutolevelsDefault false

#define kParamYellow "yellow"
#define kParamYellowLabel "Yellow"
#define kParamYellowHint "Override autolevel with yellow component."
#define kParamYellowDefault false

#define kParamCyan "cyan"
#define kParamCyanLabel "Cyan"
#define kParamCyanHint "Override autolevel with cyan component."
#define kParamCyanDefault false

#define kParamMagenta "magenta"
#define kParamMagentaLabel "Magenta"
#define kParamMagentaHint "Override autolevel with magenta component."
#define kParamMagentaDefault false

#define kParamSS "ss"
#define kParamSSLabel "Screen Subtraction"
#define kParamSSHint "Have the keyer subtract the foreground or just premult."
#define kParamSSDefault true

#define kParamClampAlpha "clampAlpha"
#define kParamClampAlphaLabel "Clamp"
#define kParamClampAlphaHint "Clamp matte to 0-1."
#define kParamClampAlphaDefault true

#define kParamRGBAL "rgbal"
#define kParamRGBALLabel "RGBA Legal"
#define kParamRGBALHint "Legalize rgba relationship."
#define kParamRGBALDefault false

#define kParamNoKey "noKey"
#define kParamNoKeyLabel "No Key"
#define kParamNoKeyHint "Apply background luminance and chroma to Fg rgba input - no key is pulled."
#define kParamNoKeyDefault false

#define kParamUBL "ubl"
#define kParamUBLLabel "Use Bg Luminance"
#define kParamUBLHint "Have the output rgb be biased by the difference between the bg luminance and the c luminance). Luminance math is Rec.709." // only applied where the key is transparent
#define kParamUBLDefault false

#define kParamUBC "ubc"
#define kParamUBCLabel "Use Bg Chroma"
#define kParamUBCHint "Have the output rgb be biased by the bg chroma."
#define kParamUBCDefault false

// This is for Rec.709
// see http://www.poynton.com/notes/colour_and_gamma/GammaFAQ.html#luminance
static inline
double
rgb2luminance_rec709(double r,
                     double g,
                     double b)
{
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

class PIKProcessorBase
    : public OFX::ImageProcessor
{
protected:
    const OFX::Image *_fgImg;
    const OFX::Image *_pfgImg;
    const OFX::Image *_cImg;
    const OFX::Image *_bgImg;
    ScreenTypeEnum _screenType; // Screen Type: The type of background screen used for the key.
    float _color[3];
    bool _useColor;
    double _redWeight; // Red Weight: Determines how the red channel and complement channel (blue for a green screen, green for a blue screen) are weighted in the keying calculation.
    double _blueGreenWeight; // Blue/Green Weight: Determines how the red channel and complement channel (blue for a green screen, green for a blue screen) are weighted in the keying calculation.
    bool _lmEnable; // Luminane Match Enable: Luminance Match Enable: Adds a luminance factor to the color difference algorithm.
    double _level; // Screen Range: Helps retain blacks and shadows.
    double _luma; // Luminance Level: Makes the matte more additive.
    bool _llEnable; // Luminance Level Enable: Disable the luminance level when us bg influence.
    bool _autolevels; // Autolevels: Removes hard edges from the matte.
    bool _yellow; // Yellow: Override autolevel with yellow component.
    bool _cyan; // Cyan: Override autolevel with cyan component.
    bool _magenta; // Magenta: Override autolevel with magenta component.
    bool _ss; // Screen Subtraction: Have the keyer subtract the foreground or just premult.
    bool _clampAlpha; // Clamp: Clamp matte to 0-1.
    bool _rgbal; // Legalize rgba relationship.
    bool _noKey; // No Key: Apply background luminance and chroma to Fg rgba input - no key is pulled.
    bool _ubl; // Use Bg Lum: Have the output rgb be biased by the bg luminance.
    bool _ubc; // Use Bg Chroma: Have the output rgb be biased by the bg chroma.

public:

    PIKProcessorBase(OFX::ImageEffect &instance)
        : OFX::ImageProcessor(instance)
        , _fgImg(0)
        , _pfgImg(0)
        , _cImg(0)
        , _screenType(kParamScreenTypeDefault)
        , _useColor(false)
        , _redWeight(kParamRedWeightDefault)
        , _blueGreenWeight(kParamBlueGreenWeightDefault)
        , _lmEnable(kParamLMEnableDefault)
        , _level(kParamLevelDefault)
        , _luma(kParamLumaDefault)
        , _llEnable(kParamLLEnableDefault)
        , _autolevels(kParamAutolevelsDefault)
        , _yellow(kParamYellowDefault)
        , _cyan(kParamCyanDefault)
        , _magenta(kParamMagentaDefault)
        , _ss(kParamSSDefault)
        , _clampAlpha(kParamClampAlphaDefault)
        , _rgbal(kParamRGBALDefault)
        , _noKey(kParamNoKeyDefault)
        , _ubl(kParamUBLDefault)
        , _ubc(kParamUBCDefault)
    {
        _color[0] = _color[1] = _color[2] = 0.;
    }

    void setSrcImgs(const OFX::Image *fgImg,
                    const OFX::Image *pfgImg,
                    const OFX::Image *cImg,
                    const OFX::Image *bgImg)
    {
        _fgImg = fgImg;
        _pfgImg = pfgImg;
        _cImg = cImg;
        _bgImg = bgImg;
    }

    void setValues(ScreenTypeEnum screenType, // Screen Type: The type of background screen used for the key.
                   const OfxRGBColourD& color,
                   double redWeight, // Red Weight: Determines how the red channel and complement channel (blue for a green screen, green for a blue screen) are weighted in the keying calculation.
                   double blueGreenWeight, // Blue/Green Weight: Determines how the red channel and complement channel (blue for a green screen, green for a blue screen) are weighted in the keying calculation.
                   bool lmEnable, // Luminane Match Enable: Luminance Match Enable: Adds a luminance factor to the color difference algorithm.
                   double level, // Screen Range: Helps retain blacks and shadows.
                   double luma, // Luminance Level: Makes the matte more additive.
                   bool llEnable, // Luminance Level Enable: Disable the luminance level when us bg influence.
                   bool autolevels, // Autolevels: Removes hard edges from the matte.
                   bool yellow, // Yellow: Override autolevel with yellow component.
                   bool cyan, // Cyan: Override autolevel with cyan component.
                   bool magenta, // Magenta: Override autolevel with magenta component.
                   bool ss, // Screen Subtraction: Have the keyer subtract the foreground or just premult.
                   bool clampAlpha, // Clamp: Clamp matte to 0-1.
                   bool rgbal, // Legalize rgba relationship.
                   bool noKey, // No Key: Apply background luminance and chroma to Fg rgba input - no key is pulled.
                   bool ubl, // Use Bg Lum: Have the output rgb be biased by the bg luminance.
                   bool ubc) // Use Bg Chroma: Have the output rgb be biased by the bg chroma.
    {
        if (screenType == eScreenTypePick) {
            screenType = color.g > color.r ? eScreenTypeGreen: eScreenTypeBlue;
            _color[0] = color.r;
            _color[1] = color.g;
            _color[2] = color.b;
            _useColor = true;
        } else {
            _screenType = screenType;
            _useColor = false;
        }
        _redWeight = redWeight;
        _blueGreenWeight = blueGreenWeight;
        _lmEnable = lmEnable;
        _level = level;
        _luma = luma;
        _llEnable = llEnable;
        _autolevels = autolevels;
        _yellow = yellow;
        _cyan = cyan;
        _magenta = magenta;
        _ss = ss;
        _clampAlpha = clampAlpha;
        _rgbal = rgbal;
        _noKey = noKey;
        _ubl = ubl;
        _ubc = ubc;
    }
};


template<class PIX, int maxValue>
static float
sampleToFloat(PIX value)
{
    return (maxValue == 1) ? value : (value / (float)maxValue);
}

template<class PIX, int maxValue>
static PIX
floatToSample(float value)
{
    if (maxValue == 1) {
        return PIX(value);
    }
    if (value <= 0) {
        return 0;
    } else if (value >= 1.) {
        return maxValue;
    }

    return PIX(value * maxValue + 0.5f);
}

template<class PIX, int maxValue>
static PIX
floatToSample(double value)
{
    if (maxValue == 1) {
        return PIX(value);
    }
    if (value <= 0) {
        return 0;
    } else if (value >= 1.) {
        return maxValue;
    }

    return PIX(value * maxValue + 0.5);
}

template <class PIX, int nComponents, int maxValue>
class PIKProcessor
    : public PIKProcessorBase
{
public:
    PIKProcessor(OFX::ImageEffect &instance)
        : PIKProcessorBase(instance)
    {
    }

private:
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        assert(nComponents == 4);
        assert(!_fgImg || _fgImg->getPixelComponents() == ePixelComponentRGBA || _fgImg->getPixelComponents() == ePixelComponentRGB);
        assert(!_pfgImg || _pfgImg->getPixelComponents() == ePixelComponentRGBA || _pfgImg->getPixelComponents() == ePixelComponentRGB);
        assert(!_cImg || _cImg->getPixelComponents() == ePixelComponentRGBA || _cImg->getPixelComponents() == ePixelComponentRGB);
        assert(!_bgImg || _bgImg->getPixelComponents() == ePixelComponentRGBA || _bgImg->getPixelComponents() == ePixelComponentRGB);
        //assert(_fgImg); // crashes with Nuke
        const int fgComponents = _fgImg ? (_fgImg->getPixelComponents() == ePixelComponentRGBA ? 4 : 3) : 0;
        const int pfgComponents = _pfgImg ? (_pfgImg->getPixelComponents() == ePixelComponentRGBA ? 4 : 3) : 0;
        const int cComponents = _cImg ? (_cImg->getPixelComponents() == ePixelComponentRGBA ? 4 : 3) : 0;
        const int bgComponents = _bgImg ? (_bgImg->getPixelComponents() == ePixelComponentRGBA ? 4 : 3) : 0;

        float c[4] = {_color[0], _color[1], _color[2], 1.};

        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
            assert(dstPix);

            for (int x = procWindow.x1; x < procWindow.x2; ++x, dstPix += nComponents) {
                const PIX *fgPix = (const PIX *)  (_fgImg ? _fgImg->getPixelAddress(x, y) : 0);
                const PIX *pfgPix = (const PIX *)  (_pfgImg ? _pfgImg->getPixelAddress(x, y) : 0);
                const PIX *cPix = (const PIX *)  (_cImg ? _cImg->getPixelAddress(x, y) : 0);
                const PIX *bgPix = (const PIX *)  (_bgImg ? _bgImg->getPixelAddress(x, y) : 0);

                float fg[4] = {0., 0., 0., 1.};
                float pfg[4] = {0., 0., 0., 1.};
                float bg[4] = {0., 0., 0., 1.};

                if (fgPix) {
                    for (int i = 0; i < fgComponents; ++i) {
                        fg[i] = sampleToFloat<PIX, maxValue>(fgPix[i]);
                    }
                }
                if (pfgPix) {
                    for (int i = 0; i < pfgComponents; ++i) {
                        pfg[i] = sampleToFloat<PIX, maxValue>(pfgPix[i]);
                    }
                }
                if (cPix && !_useColor) {
                    for (int i = 0; i < cComponents; ++i) {
                        c[i] = sampleToFloat<PIX, maxValue>(cPix[i]);
                    }
                }

                if (bgPix && (_ubc || _ubl)) {
                    for (int i = 0; i < cComponents; ++i) {
                        bg[i] = sampleToFloat<PIX, maxValue>(bgPix[i]);
                    }
                }

                float alpha = 0.;
                if (_screenType == eScreenTypeGreen) {
                    if (c[1] <= 0.) {
                        alpha = 1.;
                    } else {
                        //alpha = (Ag-Ar*rw-Ab*gbw)<=0?1:clamp(1-(Ag-Ar*rw-Ab*gbw)/(Bg-Br*rw-Bb*gbw))
                        //A is pfg and B is c.
                        double pfgKey = pfg[1] - pfg[0] * _redWeight - pfg[2] * _blueGreenWeight;
                        if (pfgKey <= 0.) {
                            alpha = 1.;
                        } else {
                            double cKey = c[1] - c[0] * _redWeight - c[2] * _blueGreenWeight;
                            if (cKey <= 0) {
                                alpha = 1.;
                            } else {
                                alpha = 1. - pfgKey / cKey;
#ifdef RGBAL_IMPLEMENTED
                                if (_rgbal) {
                                    float k[3] = {0., 0., 0.};
                                    for (int i = 0; i < 3; ++i) {
                                        if (c[i] > 0) {
                                            k[i] = pfg[i] / c[i];
                                        }
                                    }
                                    double kmax = -DBL_MAX;
                                    for (int i = 0; i < 3; ++i) {
                                        if (k[i] > kmax) {
                                            kmax = k[i];
                                        }
                                    }
                                    float kKey = pfgKey / cKey;
                                    if (kKey > kmax && kKey > 1.) {
                                        alpha = 0.; // the "zero zone" is OK
                                    } else {
                                        // the second part ((kmax - kKey) / (50*kKey)) is wrong, but that's
                                        // the closest I could get to IBK
                                        alpha = std::max((double)alpha, std::min((kmax - kKey) / (50*kKey), 1.));
                                    }
                                }
#endif
                            }
                        }
                    }
                } else if (_screenType == eScreenTypeBlue) {
                    if (c[2] <= 0.) {
                        alpha = 1.;
                    } else {
                        //alpha = (Ag-Ar*rw-Ab*gbw)<=0?1:clamp(1-(Ag-Ar*rw-Ab*gbw)/(Bg-Br*rw-Bb*gbw))
                        //A is pfg and B is c.
                        double pfgKey = pfg[2] - pfg[0] * _redWeight - pfg[1] * _blueGreenWeight;
                        if (pfgKey <= 0.) {
                            alpha = 1.;
                        } else {
                            double cKey = c[2] - c[0] * _redWeight - c[1] * _blueGreenWeight;
                            if (cKey <= 0) {
                                alpha = 1.;
                            } else {
                                alpha = 1. - pfgKey / cKey;
#ifdef RGBAL_IMPLEMENTED
                                if (_rgbal) {
                                    float k[3] = {0., 0., 0.};
                                    for (int i = 0; i < 3; ++i) {
                                        if (c[i] > 0) {
                                            k[i] = pfg[i] / c[i];
                                        }
                                    }
                                    double kmax = -DBL_MAX;
                                    for (int i = 0; i < 3; ++i) {
                                        if (k[i] > kmax) {
                                            kmax = k[i];
                                        }
                                    }
                                    float kKey = pfgKey / cKey;
                                    if (kKey > kmax && kKey > 1.) {
                                        alpha = 0.; // the "zero zone" is OK
                                    } else {
                                        // the second part ((kmax - kKey) / (50*kKey)) is wrong, but that's
                                        // the closest I could get to IBK
                                        alpha = std::max((double)alpha, std::min((kmax - kKey) / (50*kKey), 1.));
                                    }
                                }
#endif
                            }
                        }
                    }
                }


                if (_noKey) {
                    // TODO: use Bg Lum, use BG Chroma
                    for (int i = 0; i < 3; ++i) {
                        dstPix[i] = fgPix[i];
                    }
                } else if (_ss) {
                    if (alpha >= 1) {
                        for (int i = 0; i < 3; ++i) {
                            dstPix[i] = fgPix[i];
                        }
                    } else {
                        for (int i = 0; i < 3; ++i) {
                            float v = fg[i] + c[i] * (alpha - 1.);
                            dstPix[i] = v < 0. ? 0 : floatToSample<PIX, maxValue>(v);
                        }
                    }
                    /*
                } else if (_rgbal) {
                    double alphamin = DBL_MAX;
                    for (int i = 0; i < 3; ++i) {
                        if (c[i] > 0) {
                            double a = 1. - pfg[i] / c[i];
                            if (a < alphamin) {
                                alphamin = a;
                            }
                        }
                    }
                    // alphamin, which corresponds to black is mapped to alpha=0
                    // alpha = alphamin -> 0.
                    // alpha = 1 -> 1.
                    alpha = (alpha - alphamin) / (1. - alphamin);
                    if (alpha <= 0.) {
                        alpha = 0.;
                        dstPix[0] = dstPix[1] = dstPix[2] = 0;
                        dstPix[0] = dstPix[1] = dstPix[2] = 1;alpha=1;
                    } else {
                        for (int i = 0; i < 3; ++i) {
                            dstPix[i] = floatToSample<PIX, maxValue>(fg[i]*alpha);
                        }
                    }
                     */
                } else {
                    if (alpha <= 0.) {
                        dstPix[0] = dstPix[1] = dstPix[2] = 0;
                    } else {
                        for (int i = 0; i < 3; ++i) {
                            dstPix[i] = floatToSample<PIX, maxValue>(fg[i]*alpha);
                        }
                    }
                }
                if (_clampAlpha && alpha < 0.) {
                    alpha = 0.;
                }
                dstPix[3] = floatToSample<PIX, maxValue>(alpha);
            }
        }
    } // multiThreadProcessImages
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class PIKPlugin
    : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    PIKPlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(0)
        , _fgClip(0)
        , _pfgClip(0)
        , _cClip(0)
        , _bgClip(0)
        , _screenType(0)
        , _color(0)
        , _redWeight(0)
        , _blueGreenWeight(0)
        , _lmEnable(0)
        , _level(0)
        , _luma(0)
        , _llEnable(0)
        , _autolevels(0)
        , _yellow(0)
        , _cyan(0)
        , _magenta(0)
        , _ss(0)
        , _clampAlpha(0)
        , _rgbal(0)
        , _noKey(0)
        , _ubl(0)
        , _ubc(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentRGBA) );
        _fgClip = fetchClip(kClipFg);
        assert( ( _fgClip && (!_fgClip->isConnected() || _fgClip->getPixelComponents() ==  ePixelComponentRGB ||
                              _fgClip->getPixelComponents() == ePixelComponentRGBA) ) );
        _pfgClip = fetchClip(kClipPFg);
        assert( ( _pfgClip && (!_pfgClip->isConnected() || _pfgClip->getPixelComponents() ==  ePixelComponentRGB ||
                               _pfgClip->getPixelComponents() == ePixelComponentRGBA) ) );
        _cClip = fetchClip(kClipC);
        assert( ( _cClip && (!_cClip->isConnected() || _cClip->getPixelComponents() ==  ePixelComponentRGB ||
                               _cClip->getPixelComponents() == ePixelComponentRGBA) ) );
        _bgClip = fetchClip(kClipBg);
        assert( _bgClip && (!_bgClip->isConnected() || _bgClip->getPixelComponents() == ePixelComponentRGB || _bgClip->getPixelComponents() == ePixelComponentRGBA) );

        _screenType = fetchChoiceParam(kParamScreenType); // Screen Type: The type of background screen used for the key.
        _color = fetchRGBParam(kParamColor); // Screen Type: The type of background screen used for the key.
        _redWeight = fetchDoubleParam(kParamRedWeight); // Red Weight: Determines how the red channel and complement channel (blue for a green screen, green for a blue screen) are weighted in the keying calculation.
        _blueGreenWeight = fetchDoubleParam(kParamBlueGreenWeight); // Blue/Green Weight: Determines how the red channel and complement channel (blue for a green screen, green for a blue screen) are weighted in the keying calculation.
        _lmEnable = fetchBooleanParam(kParamLMEnable); // Luminane Match Enable: Luminance Match Enable: Adds a luminance factor to the color difference algorithm.
        _level = fetchDoubleParam(kParamLevel); // Screen Range: Helps retain blacks and shadows.
        _luma = fetchDoubleParam(kParamLuma); // Luminance Level: Makes the matte more additive.
        _llEnable = fetchBooleanParam(kParamLLEnable); // Luminance Level Enable: Disable the luminance level when us bg influence.
        _autolevels = fetchBooleanParam(kParamAutolevels); // Autolevels: Removes hard edges from the matte.
        _yellow = fetchBooleanParam(kParamYellow); // Yellow: Override autolevel with yellow component.
        _cyan = fetchBooleanParam(kParamCyan); // Cyan: Override autolevel with cyan component.
        _magenta = fetchBooleanParam(kParamMagenta); // Magenta: Override autolevel with magenta component.
        _ss = fetchBooleanParam(kParamSS); // Screen Subtraction: Have the keyer subtract the foreground or just premult.
        _clampAlpha = fetchBooleanParam(kParamClampAlpha); // Clamp: Clamp matte to 0-1.
        _rgbal = fetchBooleanParam(kParamRGBAL); // Legalize rgba relationship.
        _noKey = fetchBooleanParam(kParamNoKey); // No Key: Apply background luminance and chroma to Fg rgba input - no key is pulled.
        _ubl = fetchBooleanParam(kParamUBL); // Use Bg Lum: Have the output rgb be biased by the bg luminance.
        _ubc = fetchBooleanParam(kParamUBC); // Use Bg Chroma: Have the output rgb be biased by the bg chroma.

        updateEnabled();
    }

private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    /** @brief get the clip preferences */
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(PIKProcessorBase &, const OFX::RenderArguments &args);

    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    void updateEnabled()
    {
        ScreenTypeEnum screenType = (ScreenTypeEnum)_screenType->getValue();
        bool noKey = _noKey->getValue();
        bool lmEnable = _lmEnable->getValue();
        bool llEnable = _llEnable->getValue();
        bool autolevels = _autolevels->getValue();

        _screenType->setEnabled(!noKey);
        _color->setEnabled(!noKey && screenType == eScreenTypePick);
        _redWeight->setEnabled(!noKey);
        _blueGreenWeight->setEnabled(!noKey);
        _lmEnable->setEnabled(!noKey);
        _level->setEnabled(!noKey && lmEnable);
        _llEnable->setEnabled(!noKey && lmEnable);
        _luma->setEnabled(!noKey && lmEnable && llEnable);
        _autolevels->setEnabled(!noKey);
        _yellow->setEnabled(!noKey && autolevels);
        _cyan->setEnabled(!noKey && autolevels);
        _magenta->setEnabled(!noKey && autolevels);
        _ss->setEnabled(!noKey);
        _clampAlpha->setEnabled(!noKey);
        _rgbal->setEnabled(!noKey);
    }

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    OFX::Clip *_fgClip;
    OFX::Clip *_pfgClip;
    OFX::Clip *_cClip;
    OFX::Clip *_bgClip;
    ChoiceParam* _screenType; // Screen Type: The type of background screen used for the key.
    RGBParam* _color;
    DoubleParam* _redWeight; // Red Weight: Determines how the red channel and complement channel (blue for a green screen, green for a blue screen) are weighted in the keying calculation.
    DoubleParam* _blueGreenWeight; // Blue/Green Weight: Determines how the red channel and complement channel (blue for a green screen, green for a blue screen) are weighted in the keying calculation.
    BooleanParam* _lmEnable; // Luminane Match Enable: Luminance Match Enable: Adds a luminance factor to the color difference algorithm.
    DoubleParam* _level; // Screen Range: Helps retain blacks and shadows.
    DoubleParam* _luma; // Luminance Level: Makes the matte more additive.
    BooleanParam* _llEnable; // Luminance Level Enable: Disable the luminance level when us bg influence.
    BooleanParam* _autolevels; // Autolevels: Removes hard edges from the matte.
    BooleanParam* _yellow; // Yellow: Override autolevel with yellow component.
    BooleanParam* _cyan; // Cyan: Override autolevel with cyan component.
    BooleanParam* _magenta; // Magenta: Override autolevel with magenta component.
    BooleanParam* _ss; // Screen Subtraction: Have the keyer subtract the foreground or just premult.
    BooleanParam* _clampAlpha; // Clamp: Clamp matte to 0-1.
    BooleanParam* _rgbal; // Legalize rgba relationship.
    BooleanParam* _noKey; // No Key: Apply background luminance and chroma to Fg rgba input - no key is pulled.
    BooleanParam* _ubl; // Use Bg Lum: Have the output rgb be biased by the bg luminance.
    BooleanParam* _ubc; // Use Bg Chroma: Have the output rgb be biased by the bg chroma.
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
PIKPlugin::setupAndProcess(PIKProcessorBase &processor,
                             const OFX::RenderArguments &args)
{
    const double time = args.time;
    std::auto_ptr<OFX::Image> dst( _dstClip->fetchImage(time) );

    if ( !dst.get() ) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    OFX::BitDepthEnum dstBitDepth    = dst->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();
    if ( ( dstBitDepth != _dstClip->getPixelDepth() ) ||
         ( dstComponents != _dstClip->getPixelComponents() ) ) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if ( (dst->getRenderScale().x != args.renderScale.x) ||
         ( dst->getRenderScale().y != args.renderScale.y) ||
         ( ( dst->getField() != OFX::eFieldNone) /* for DaVinci Resolve */ && ( dst->getField() != args.fieldToRender) ) ) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    ScreenTypeEnum screenType = (ScreenTypeEnum)_screenType->getValueAtTime(time);
    OfxRGBColourD color = {0., 0., 1.};
    _color->getValueAtTime(time, color.r, color.g, color.b);
    double redWeight = _redWeight->getValueAtTime(time);
    double blueGreenWeight = _blueGreenWeight->getValueAtTime(time);
    bool lmEnable = _lmEnable->getValueAtTime(time);
    double level = _level->getValueAtTime(time);
    double luma = _luma->getValueAtTime(time);
    bool llEnable = _llEnable->getValueAtTime(time);
    bool autolevels = _autolevels->getValueAtTime(time);
    bool yellow = _yellow->getValueAtTime(time);
    bool cyan = _cyan->getValueAtTime(time);
    bool magenta = _magenta->getValueAtTime(time);
    bool ss = _ss->getValueAtTime(time);
    bool clampAlpha = _clampAlpha->getValueAtTime(time);
    bool rgbal = _rgbal->getValueAtTime(time);
    bool noKey = _noKey->getValueAtTime(time);
    bool ubl = _ubl->getValueAtTime(time);
    bool ubc = _ubc->getValueAtTime(time);
    std::auto_ptr<const OFX::Image> fg( ( _fgClip && _fgClip->isConnected() ) ?
                                       _fgClip->fetchImage(time) : 0 );
    std::auto_ptr<const OFX::Image> pfg( !noKey && ( _pfgClip && _pfgClip->isConnected() ) ?
                                       _pfgClip->fetchImage(time) : 0 );
    std::auto_ptr<const OFX::Image> c( !noKey && screenType != eScreenTypePick && ( _cClip && _cClip->isConnected() ) ?
                                       _cClip->fetchImage(time) : 0 );
    std::auto_ptr<const OFX::Image> bg( (ubl || ubc) && ( _bgClip && _bgClip->isConnected() ) ?
                                        _bgClip->fetchImage(time) : 0 );
    if ( fg.get() ) {
        if ( (fg->getRenderScale().x != args.renderScale.x) ||
            ( fg->getRenderScale().y != args.renderScale.y) ||
            ( ( fg->getField() != OFX::eFieldNone) /* for DaVinci Resolve */ && ( fg->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        OFX::BitDepthEnum fgBitDepth      = fg->getPixelDepth();
        //OFX::PixelComponentEnum fgComponents = fg->getPixelComponents();
        if (fgBitDepth != dstBitDepth /* || fgComponents != dstComponents*/) { // Keyer outputs RGBA but may have RGB input
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    } else {
        // Nuke sometimes returns NULL when render is interrupted
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    if ( pfg.get() ) {
        if ( (pfg->getRenderScale().x != args.renderScale.x) ||
            ( pfg->getRenderScale().y != args.renderScale.y) ||
            ( ( pfg->getField() != OFX::eFieldNone) /* for DaVinci Resolve */ && ( pfg->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        OFX::BitDepthEnum pfgBitDepth      = pfg->getPixelDepth();
        //OFX::PixelComponentEnum pfgComponents = pfg->getPixelComponents();
        if (pfgBitDepth != dstBitDepth /* || pfgComponents != dstComponents*/) { // Keyer outputs RGBA but may have RGB input
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    if ( c.get() ) {
        if ( (c->getRenderScale().x != args.renderScale.x) ||
            ( c->getRenderScale().y != args.renderScale.y) ||
            ( ( c->getField() != OFX::eFieldNone) /* for DaVinci Resolve */ && ( c->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        OFX::BitDepthEnum cBitDepth      = c->getPixelDepth();
        //OFX::PixelComponentEnum cComponents = c->getPixelComponents();
        if (cBitDepth != dstBitDepth /* || cComponents != dstComponents*/) { // Keyer outputs RGBA but may have RGB input
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    if ( bg.get() ) {
        if ( (bg->getRenderScale().x != args.renderScale.x) ||
             ( bg->getRenderScale().y != args.renderScale.y) ||
             ( ( bg->getField() != OFX::eFieldNone) /* for DaVinci Resolve */ && ( bg->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        OFX::BitDepthEnum srcBitDepth      = bg->getPixelDepth();
        //OFX::PixelComponentEnum srcComponents = bg->getPixelComponents();
        if (srcBitDepth != dstBitDepth /* || srcComponents != dstComponents*/) {  // Keyer outputs RGBA but may have RGB input
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    processor.setValues(screenType, color, redWeight, blueGreenWeight, lmEnable, level, luma, llEnable, autolevels, yellow, cyan, magenta, ss, clampAlpha, rgbal, noKey, ubl, ubc);
    processor.setDstImg( dst.get() );
    processor.setSrcImgs( fg.get(), ( !noKey && !( _pfgClip && _pfgClip->isConnected() ) ) ? fg.get() : pfg.get(), c.get(), bg.get() );
    processor.setRenderWindow(args.renderWindow);

    processor.process();
} // PIKPlugin::setupAndProcess

// the overridden render function
void
PIKPlugin::render(const OFX::RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || !_fgClip || !_fgClip->isConnected() || _fgClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_fgClip || !_fgClip->isConnected() || _fgClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert( kSupportsMultipleClipPARs   || !_pfgClip || !_pfgClip->isConnected() || _pfgClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_pfgClip || !_pfgClip->isConnected() || _pfgClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert( kSupportsMultipleClipPARs   || !_cClip || !_cClip->isConnected() || _cClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_cClip || !_cClip->isConnected() || _cClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert( kSupportsMultipleClipPARs   || !_bgClip || !_bgClip->isConnected() || _bgClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_bgClip || !_bgClip->isConnected() || _bgClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    if (dstComponents != OFX::ePixelComponentRGBA) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host dit not take into account output components");
        OFX::throwSuiteStatusException(kOfxStatErrImageFormat);

        return;
    }

    switch (dstBitDepth) {
    //case OFX::eBitDepthUByte: {
    //    PIKProcessor<unsigned char, 4, 255> fred(*this);
    //    setupAndProcess(fred, args);
    //    break;
    //}
    case OFX::eBitDepthUShort: {
        PIKProcessor<unsigned short, 4, 65535> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case OFX::eBitDepthFloat: {
        PIKProcessor<float, 4, 1> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    default:
        OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

/* Override the clip preferences */
void
PIKPlugin::getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences)
{
    // set the premultiplication of _dstClip
    bool noKey = _noKey->getValue();

    if (noKey) {
        clipPreferences.setOutputPremultiplication(_fgClip->getPreMultiplication());
    } else {
        clipPreferences.setOutputPremultiplication(eImagePreMultiplied);
    }

    // Output is RGBA
    clipPreferences.setClipComponents(*_dstClip, ePixelComponentRGBA);
    // note: Keyer handles correctly inputs with different components: it only uses RGB components from both clips
}

void
PIKPlugin::changedParam(const OFX::InstanceChangedArgs &/*args*/,
                          const std::string &paramName)
{
    //const double time = args.time;

    if ( paramName == kParamScreenType ||
        paramName == kParamNoKey ||
        paramName == kParamLMEnable ||
        paramName == kParamLLEnable ||
        paramName == kParamAutolevels) {
        updateEnabled();
    }
}

mDeclarePluginFactory(PIKPluginFactory, {}, {});
void
PIKPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    //desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);

    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(OFX::ePixelComponentNone);
#endif
}

void
PIKPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
                                      OFX::ContextEnum /*context*/)
{
    {
        ClipDescriptor* clip = desc.defineClip(kClipFg);
        clip->setHint(kClipFgHint);
        clip->addSupportedComponent( OFX::ePixelComponentRGBA );
        clip->addSupportedComponent( OFX::ePixelComponentRGB );
        clip->setTemporalClipAccess(false);
        clip->setSupportsTiles(kSupportsTiles);
        clip->setOptional(false);
    }
    {
        ClipDescriptor* clip = desc.defineClip(kClipPFg);
        clip->setHint(kClipPFgHint);
        clip->addSupportedComponent( OFX::ePixelComponentRGBA );
        clip->addSupportedComponent( OFX::ePixelComponentRGB );
        clip->setTemporalClipAccess(false);
        clip->setSupportsTiles(kSupportsTiles);
        clip->setOptional(true);
    }
    {
        ClipDescriptor* clip = desc.defineClip(kClipC);
        clip->setHint(kClipCHint);
        clip->addSupportedComponent( OFX::ePixelComponentRGBA );
        clip->addSupportedComponent( OFX::ePixelComponentRGB );
        clip->setTemporalClipAccess(false);
        clip->setSupportsTiles(kSupportsTiles);
        clip->setOptional(true);
    }
    {
        ClipDescriptor* clip = desc.defineClip(kClipBg);
        clip->setHint(kClipBgHint);
        clip->addSupportedComponent( OFX::ePixelComponentRGBA );
        clip->addSupportedComponent( OFX::ePixelComponentRGB );
        clip->setTemporalClipAccess(false);
        clip->setSupportsTiles(kSupportsTiles);
        clip->setOptional(true);
    }

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->setSupportsTiles(kSupportsTiles);


    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // screenType
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamScreenType);
        param->setLabel(kParamScreenTypeLabel);
        param->setHint(kParamScreenTypeHint);
        assert(param->getNOptions() == (int)eScreenTypeGreen);
        param->appendOption(kParamScreenTypeOptionGreen);
        assert(param->getNOptions() == (int)eScreenTypeBlue);
        param->appendOption(kParamScreenTypeOptionBlue);
        assert(param->getNOptions() == (int)eScreenTypePick);
        param->appendOption(kParamScreenTypeOptionPick);
        param->setDefault( (int)kParamScreenTypeDefault );
        if (page) {
            page->addChild(*param);
        }
    }

    {
        RGBParamDescriptor* param = desc.defineRGBParam(kParamColor);
        param->setLabel(kParamColorLabel);
        param->setHint(kParamColorHint);
        param->setDefault(0., 0., 1.);
        param->setAnimates(true);
        param->setLayoutHint(eLayoutHintDivider);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamRedWeight);
        param->setLabel(kParamRedWeightLabel);
        param->setHint(kParamRedWeightHint);
        param->setRange(-DBL_MAX, DBL_MAX);
        param->setDisplayRange(0., 1.);
        param->setDefault(kParamRedWeightDefault);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamBlueGreenWeight);
        param->setLabel(kParamBlueGreenWeightLabel);
        param->setHint(kParamBlueGreenWeightHint);
        param->setRange(-DBL_MAX, DBL_MAX);
        param->setDisplayRange(0., 1.);
        param->setDefault(kParamBlueGreenWeightDefault);
        param->setAnimates(true);
        param->setLayoutHint(eLayoutHintDivider);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamLMEnable);
        param->setLabel(kParamLMEnableLabel);
        param->setHint(kParamLMEnableHint);
        param->setDefault(kParamLMEnableDefault);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamLevel);
        param->setLabel(kParamLevelLabel);
        param->setHint(kParamLevelHint);
        param->setRange(-DBL_MAX, DBL_MAX);
        param->setDisplayRange(0., 1.);
        param->setDefault(kParamLevelDefault);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamLuma);
        param->setLabel(kParamLumaLabel);
        param->setHint(kParamLumaHint);
        param->setRange(-DBL_MAX, DBL_MAX);
        param->setDisplayRange(0., 1.);
        param->setDefault(kParamLumaDefault);
        param->setAnimates(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamLLEnable);
        param->setLabel(kParamLLEnableLabel);
        param->setHint(kParamLLEnableHint);
        param->setDefault(kParamLLEnableDefault);
        param->setAnimates(false);
        param->setLayoutHint(eLayoutHintDivider);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamAutolevels);
        param->setLabel(kParamAutolevelsLabel);
        param->setHint(kParamAutolevelsHint);
        param->setDefault(kParamAutolevelsDefault);
        param->setAnimates(false);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamYellow);
        param->setLabel(kParamYellowLabel);
        param->setHint(kParamYellowHint);
        param->setDefault(kParamYellowDefault);
        param->setAnimates(false);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamCyan);
        param->setLabel(kParamCyanLabel);
        param->setHint(kParamCyanHint);
        param->setDefault(kParamCyanDefault);
        param->setAnimates(false);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamMagenta);
        param->setLabel(kParamMagentaLabel);
        param->setHint(kParamMagentaHint);
        param->setDefault(kParamMagentaDefault);
        param->setAnimates(false);
        param->setLayoutHint(eLayoutHintDivider);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamSS);
        param->setLabel(kParamSSLabel);
        param->setHint(kParamSSHint);
        param->setDefault(kParamSSDefault);
        param->setAnimates(false);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamClampAlpha);
        param->setLabel(kParamClampAlphaLabel);
        param->setHint(kParamClampAlphaHint);
        param->setDefault(kParamClampAlphaDefault);
        param->setAnimates(false);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamRGBAL);
        param->setLabel(kParamRGBALLabel);
        param->setHint(kParamRGBALHint);
        param->setDefault(kParamRGBALDefault);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamNoKey);
        param->setLabel(kParamNoKeyLabel);
        param->setHint(kParamNoKeyHint);
        param->setDefault(kParamNoKeyDefault);
        param->setAnimates(false);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamUBL);
        param->setLabel(kParamUBLLabel);
        param->setHint(kParamUBLHint);
        param->setDefault(kParamUBLDefault);
        param->setAnimates(false);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamUBC);
        param->setLabel(kParamUBCLabel);
        param->setHint(kParamUBCHint);
        param->setDefault(kParamUBCDefault);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }
} // PIKPluginFactory::describeInContext

OFX::ImageEffect*
PIKPluginFactory::createInstance(OfxImageEffectHandle handle,
                                   OFX::ContextEnum /*context*/)
{
    return new PIKPlugin(handle);
}

static PIKPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT

#endif // DEBUG
