#include "Theme.h"

namespace lavc
{
//==============================================================================
namespace Hardware
{
/** Tileable brushed-metal grain: horizontal streaks, each line slightly lighter or darker. */
const juce::Image& brushedTexture()
{
    static const juce::Image texture = []
    {
        constexpr int w = 512, h = 256;
        juce::Image im (juce::Image::ARGB, w, h, true);
        juce::Random rng (0x1a5eed);
        juce::Image::BitmapData data (im, juce::Image::BitmapData::writeOnly);
        float carry = 0.0f;
        for (int y = 0; y < h; ++y)
        {
            // Neighbouring lines are correlated a little so the grain has streaks of different widths
            carry = carry * 0.3f + (rng.nextFloat() - 0.5f) * 0.8f;
            const float line = carry + (rng.nextFloat() < 0.03f ? 0.45f : 0.0f);
            const float p1 = rng.nextFloat() * juce::MathConstants<float>::twoPi;
            const float p2 = rng.nextFloat() * juce::MathConstants<float>::twoPi;
            const float k1 = (float) (1 + rng.nextInt (3)), k2 = (float) (5 + rng.nextInt (7));
            for (int x = 0; x < w; ++x)
            {
                // Whole periods along the line, so the texture tiles without a seam
                const float t = (float) x / (float) w * juce::MathConstants<float>::twoPi;
                float v = line + 0.12f * std::sin (k1 * t + p1) + 0.08f * std::sin (k2 * t + p2) + (rng.nextFloat() - 0.5f) * 0.12f;
                v = juce::jlimit (-1.0f, 1.0f, v);
                data.setPixelColour (x, y, v >= 0.0f ? juce::Colours::white.withAlpha (v * 0.11f)
                                                     : juce::Colours::black.withAlpha (-v * 0.32f));
            }
        }
        return im;
    }();
    return texture;
}

namespace
{
juce::Path rounded (juce::Rectangle<float> r, float corner)
{
    juce::Path p;
    p.addRoundedRectangle (r, corner);
    return p;
}
} // namespace

void drawBrushedMetal (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour base, float corner)
{
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (rounded (area, corner));

    g.setGradientFill (juce::ColourGradient (base.brighter (0.12f), area.getX(), area.getY(),
                                             base.darker (0.3f), area.getX(), area.getBottom(), false));
    g.fillRect (area);

    g.setTiledImageFill (brushedTexture(), (int) area.getX() * 7, (int) area.getY() * 3, 0.45f);
    g.fillRect (area);

    // A broad, soft diagonal sheen, as if lit from the top left
    juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.0f), area.getX(), area.getY(),
                                juce::Colours::white.withAlpha (0.0f), area.getRight(), area.getBottom(), false);
    sheen.addColour (0.3, juce::Colours::white.withAlpha (0.025f));
    sheen.addColour (0.45, juce::Colours::white.withAlpha (0.055f));
    sheen.addColour (0.62, juce::Colours::white.withAlpha (0.02f));
    g.setGradientFill (sheen);
    g.fillRect (area);
}

void drawScrew (juce::Graphics& g, juce::Point<float> c, float r)
{
    // Countersink
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.fillEllipse (juce::Rectangle<float> (r * 2.0f + 2.4f, r * 2.0f + 2.4f).withCentre (c));
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawEllipse (juce::Rectangle<float> (r * 2.0f + 2.4f, r * 2.0f + 2.4f).withCentre (c.translated (0.0f, 0.6f)), 0.6f);

    // Head
    juce::ColourGradient head (juce::Colour (0xff9a9da2), c.x - r * 0.45f, c.y - r * 0.55f,
                               juce::Colour (0xff222326), c.x + r * 0.6f, c.y + r * 0.9f, true);
    head.addColour (0.55, juce::Colour (0xff4c4f54));
    g.setGradientFill (head);
    g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));

    // Slot, at an angle that depends on where the screw is (each one sits a little differently)
    const float a = std::fmod (c.x * 0.37f + c.y * 0.71f, juce::MathConstants<float>::pi);
    const juce::Point<float> d (std::cos (a) * r * 0.78f, std::sin (a) * r * 0.78f);
    g.setColour (juce::Colour (0xff0d0d0e));
    g.drawLine ({ c - d, c + d }, juce::jmax (1.0f, r * 0.34f));
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.drawLine ({ c - d + juce::Point<float> (0.0f, r * 0.22f), c + d + juce::Point<float> (0.0f, r * 0.22f) }, 0.5f);
}

void drawPlate (juce::Graphics& g, juce::Rectangle<float> area, float corner, bool screws)
{
    // Drop shadow onto the chassis
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (area.translated (0.0f, 1.5f).expanded (0.5f), corner);

    drawBrushedMetal (g, area, Colours::panel, corner);

    // Bevel: lit top edge, shaded bottom edge
    juce::ColourGradient bevel (juce::Colours::white.withAlpha (0.2f), area.getX(), area.getY(),
                                juce::Colours::black.withAlpha (0.5f), area.getX(), area.getBottom(), false);
    bevel.addColour (0.12, juce::Colours::white.withAlpha (0.05f));
    bevel.addColour (0.85, juce::Colours::black.withAlpha (0.15f));
    g.setGradientFill (bevel);
    g.drawRoundedRectangle (area.reduced (0.75f), corner, 1.5f);
    g.setColour (juce::Colours::black.withAlpha (0.85f));
    g.drawRoundedRectangle (area.expanded (0.25f), corner + 0.25f, 0.8f);

    if (screws && area.getWidth() > 80.0f && area.getHeight() > 80.0f)
    {
        const float in = 7.5f, r = 3.0f;
        for (auto p : { area.getTopLeft() + juce::Point<float> (in, in), area.getTopRight() + juce::Point<float> (-in, in),
                        area.getBottomLeft() + juce::Point<float> (in, -in), area.getBottomRight() + juce::Point<float> (-in, -in) })
            drawScrew (g, p, r);
    }
}

void drawChassis (juce::Graphics& g, juce::Rectangle<int> bounds, int headerHeight)
{
    const auto area = bounds.toFloat();
    g.fillAll (Colours::background);
    drawBrushedMetal (g, area, Colours::background.brighter (0.04f));

    // Header: a separate, lighter plate across the top, with a trim line in the unit's colour
    const auto header = area.withHeight ((float) headerHeight);
    drawBrushedMetal (g, header, Colours::panel.brighter (0.06f));
    juce::ColourGradient lit (juce::Colours::white.withAlpha (0.14f), 0.0f, header.getY(),
                              juce::Colours::white.withAlpha (0.0f), 0.0f, header.getY() + 6.0f, false);
    g.setGradientFill (lit);
    g.fillRect (header.withHeight (6.0f));

    g.setColour (juce::Colours::black.withAlpha (0.9f));
    g.fillRect (header.getX(), header.getBottom() - 1.0f, header.getWidth(), 2.0f);
    g.setColour (Colours::signature.withAlpha (0.75f));
    g.fillRect (header.getX(), header.getBottom() + 1.0f, header.getWidth(), 1.5f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRect (header.getX(), header.getBottom() + 2.5f, header.getWidth(), 1.0f);
}

void drawScreen (juce::Graphics& g, juce::Rectangle<float> area, float corner)
{
    // Bezel: a dark recess, lit along its bottom lip
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff030304), area.getX(), area.getY(),
                                             juce::Colour (0xff1c1d20), area.getX(), area.getBottom(), false));
    g.fillRoundedRectangle (area, corner);

    // Glass, faintly tinted with the unit's colour
    const auto glass = area.reduced (2.0f);
    const auto tint = Colours::graphBg.interpolatedWith (Colours::signature, 0.025f);
    g.setGradientFill (juce::ColourGradient (tint.brighter (0.25f), glass.getX(), glass.getY(),
                                             tint, glass.getX(), glass.getY() + glass.getHeight() * 0.6f, false));
    g.fillRoundedRectangle (glass, juce::jmax (1.0f, corner - 2.0f));

    // Fine raster lines, like a display panel
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (rounded (glass, corner - 2.0f));
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    for (float y = glass.getY() + 1.0f; y < glass.getBottom(); y += 3.0f)
        g.fillRect (glass.getX(), y, glass.getWidth(), 1.0f);
}

void drawGlass (juce::Graphics& g, juce::Rectangle<float> area, float corner)
{
    const auto glass = area.reduced (2.0f);
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (rounded (glass, corner - 2.0f));

        // Inner shadow under the bezel
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.6f), 0.0f, glass.getY(),
                                                 juce::Colours::transparentBlack, 0.0f, glass.getY() + 9.0f, false));
        g.fillRect (glass.withHeight (9.0f));
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.35f), glass.getX(), 0.0f,
                                                 juce::Colours::transparentBlack, glass.getX() + 6.0f, 0.0f, false));
        g.fillRect (glass.withWidth (6.0f));
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.35f), glass.getRight(), 0.0f,
                                                 juce::Colours::transparentBlack, glass.getRight() - 6.0f, 0.0f, false));
        g.fillRect (glass.withLeft (glass.getRight() - 6.0f));

        // Reflection: a soft diagonal band across the top-left of the glass
        juce::ColourGradient glare (juce::Colours::white.withAlpha (0.07f), glass.getX(), glass.getY(),
                                    juce::Colours::white.withAlpha (0.0f), glass.getX() + glass.getWidth() * 0.55f,
                                    glass.getY() + glass.getHeight() * 0.9f, false);
        glare.addColour (0.48, juce::Colours::white.withAlpha (0.025f));
        glare.addColour (0.5, juce::Colours::white.withAlpha (0.0f));
        g.setGradientFill (glare);
        g.fillRect (glass);
    }

    // Bezel lip over the edge of the glass
    g.setColour (juce::Colour (0xff050506));
    g.drawRoundedRectangle (area.reduced (1.25f), corner - 1.0f, 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.1f));
    g.drawLine (area.getX() + corner, area.getBottom() - 0.5f, area.getRight() - corner, area.getBottom() - 0.5f, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.drawLine (area.getX() + corner, area.getY() + 0.5f, area.getRight() - corner, area.getY() + 0.5f, 1.0f);
}

void drawLed (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colour, bool lit)
{
    const auto c = r.getCentre();
    const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;
    if (lit)
    {
        juce::ColourGradient glow (colour.withAlpha (0.45f), c.x, c.y, colour.withAlpha (0.0f), c.x + rad * 2.4f, c.y, true);
        g.setGradientFill (glow);
        g.fillEllipse (juce::Rectangle<float> (rad * 4.8f, rad * 4.8f).withCentre (c));
    }
    g.setColour (juce::Colour (0xff060607));
    g.fillEllipse (juce::Rectangle<float> (rad * 2.0f + 2.0f, rad * 2.0f + 2.0f).withCentre (c));
    const auto body = lit ? colour : colour.withMultipliedSaturation (0.5f).darker (2.2f);
    juce::ColourGradient lens (lit ? body.brighter (0.9f) : body.brighter (0.4f), c.x - rad * 0.3f, c.y - rad * 0.35f,
                               lit ? body.darker (0.15f) : body.darker (0.4f), c.x + rad, c.y + rad, true);
    g.setGradientFill (lens);
    g.fillEllipse (juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (c));
    g.setColour (juce::Colours::white.withAlpha (lit ? 0.6f : 0.18f));
    g.fillEllipse (juce::Rectangle<float> (rad * 0.7f, rad * 0.5f).withCentre (c.translated (-rad * 0.3f, -rad * 0.4f)));
}

void drawRecess (juce::Graphics& g, juce::Rectangle<float> r, float corner)
{
    g.setColour (juce::Colour (0xff040405));
    g.fillRoundedRectangle (r, corner);
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (rounded (r, corner));
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.8f), 0.0f, r.getY(),
                                             juce::Colours::transparentBlack, 0.0f, r.getY() + 5.0f, false));
    g.fillRect (r.withHeight (5.0f));
    g.setColour (juce::Colours::white.withAlpha (0.09f));
    g.drawLine (r.getX() + corner, r.getBottom() - 0.5f, r.getRight() - corner, r.getBottom() - 0.5f, 1.0f);
}
} // namespace Hardware

//==============================================================================
VocalChainLookAndFeel::VocalChainLookAndFeel()
{
    const auto lcdText = Colours::text.interpolatedWith (Colours::signature, 0.45f);
    setColour (juce::ResizableWindow::backgroundColourId, Colours::background);
    setColour (juce::ComboBox::backgroundColourId, Colours::graphBg);
    setColour (juce::ComboBox::outlineColourId, Colours::edge);
    setColour (juce::ComboBox::textColourId, lcdText);
    setColour (juce::ComboBox::arrowColourId, Colours::signature);
    setColour (juce::PopupMenu::backgroundColourId, Colours::panel);
    setColour (juce::PopupMenu::textColourId, Colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::signature.withAlpha (0.35f));
    setColour (juce::PopupMenu::highlightedTextColourId, Colours::text);
    setColour (juce::TextButton::buttonColourId, Colours::panelLight);
    setColour (juce::TextButton::buttonOnColourId, Colours::accent);
    setColour (juce::TextButton::textColourOffId, Colours::text);
    setColour (juce::TextButton::textColourOnId, Colours::background);
    setColour (juce::Label::textColourId, Colours::text);
    setColour (juce::Slider::rotarySliderFillColourId, Colours::accent);
    setColour (juce::Slider::trackColourId, Colours::accent);
    setColour (juce::TooltipWindow::backgroundColourId, Colours::panel);
    setColour (juce::TooltipWindow::textColourId, Colours::text);
    setColour (juce::TooltipWindow::outlineColourId, Colours::edge);
}

void VocalChainLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                         float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (1.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto colour = slider.findColour (juce::Slider::rotarySliderFillColourId);
    const bool enabled = slider.isEnabled();
    const float angle = startAngle + pos * (endAngle - startAngle);
    auto polar = [centre] (float a, float r) { return centre + juce::Point<float> (std::sin (a) * r, -std::cos (a) * r); };

    // ---- Printed scale ------------------------------------------------------------------------------
    const float tickLen = juce::jmax (2.5f, radius * 0.1f);
    for (int i = 0; i <= 10; ++i)
    {
        const float a = startAngle + (float) i / 10.0f * (endAngle - startAngle);
        const bool major = i == 0 || i == 5 || i == 10;
        g.setColour (Colours::signature.withAlpha (enabled ? (major ? 0.9f : 0.55f) : 0.3f));
        g.drawLine ({ polar (a, radius - (major ? tickLen * 1.6f : tickLen)), polar (a, radius) }, major ? 1.4f : 1.0f);
    }

    // ---- Lit value ring in a dark groove ------------------------------------------------------------
    const float ringW = juce::jmax (2.0f, radius * 0.075f);
    const float ringR = radius - tickLen * 1.6f - ringW * 0.5f - 1.5f;
    juce::Path groove;
    groove.addCentredArc (centre.x, centre.y, ringR, ringR, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.strokePath (groove, juce::PathStrokeType (ringW + 1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const bool bipolar = slider.getProperties().getWithDefault ("bipolar", false);
    const float fromAngle = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, ringR, ringR, 0.0f, juce::jmin (fromAngle, angle), juce::jmax (fromAngle, angle), true);
    const auto lit = enabled ? colour : colour.withSaturation (0.0f).withAlpha (0.4f);
    if (enabled)
    {
        g.setColour (lit.withAlpha (0.22f));
        g.strokePath (value, juce::PathStrokeType (ringW * 2.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour (lit);
    g.strokePath (value, juce::PathStrokeType (ringW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // ---- Knob: knurled skirt and a machined cap -----------------------------------------------------
    const float bodyR = ringR - ringW * 0.5f - juce::jmax (2.0f, radius * 0.07f);
    if (bodyR < 4.0f)
        return;
    const auto body = juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (centre);

    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.fillEllipse (body.expanded (2.5f).translated (0.0f, bodyR * 0.16f));
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillEllipse (body.expanded (0.8f).translated (0.0f, bodyR * 0.07f));

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3d4044), centre.x, body.getY(),
                                             juce::Colour (0xff0f1011), centre.x, body.getBottom(), false));
    g.fillEllipse (body);

    const int ridges = bodyR > 18.0f ? 40 : 28;
    for (int i = 0; i < ridges; ++i)
    {
        const float a = angle + (float) i / (float) ridges * juce::MathConstants<float>::twoPi;
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawLine ({ polar (a, bodyR * 0.8f), polar (a, bodyR - 0.6f) }, 1.0f);
        const float lightness = 0.04f + 0.07f * (0.5f - 0.5f * std::cos (a)); // ridges facing up catch more light
        g.setColour (juce::Colours::white.withAlpha (lightness));
        g.drawLine ({ polar (a + 0.06f, bodyR * 0.82f), polar (a + 0.06f, bodyR - 0.8f) }, 0.7f);
    }
    g.setColour (juce::Colours::black);
    g.drawEllipse (body, 1.0f);

    const float capR = bodyR * 0.74f;
    const auto cap = juce::Rectangle<float> (capR * 2.0f, capR * 2.0f).withCentre (centre);
    juce::ColourGradient capFill (juce::Colour (0xff5a5d62), cap.getX(), cap.getY(), juce::Colour (0xff1a1b1d), cap.getRight(),
                                  cap.getBottom(), false);
    capFill.addColour (0.45, juce::Colour (0xff2d2f32));
    capFill.addColour (0.7, juce::Colour (0xff35373b));
    g.setGradientFill (capFill);
    g.fillEllipse (cap);

    // Concentric machining rings
    for (float rr = capR * 0.3f; rr < capR; rr += juce::jmax (1.6f, capR * 0.12f))
    {
        g.setColour (juce::Colours::white.withAlpha (0.035f));
        g.drawEllipse (juce::Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (centre), 0.6f);
    }
    juce::Path rim;
    rim.addCentredArc (centre.x, centre.y, capR - 0.5f, capR - 0.5f, 0.0f, -2.2f, 0.6f, true);
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.strokePath (rim, juce::PathStrokeType (0.9f));
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawEllipse (cap, 0.8f);

    // Pointer line, engraved and filled
    const float pw = juce::jmax (1.8f, bodyR * 0.1f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawLine ({ polar (angle, capR * 0.3f).translated (0.0f, 0.8f), polar (angle, bodyR * 0.96f).translated (0.0f, 0.8f) }, pw);
    g.setColour (enabled ? Colours::text : Colours::textDim);
    juce::Path pointer;
    pointer.startNewSubPath (polar (angle, capR * 0.3f));
    pointer.lineTo (polar (angle, bodyR * 0.96f));
    g.strokePath (pointer, juce::PathStrokeType (pw, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
}

void VocalChainLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                         float, float, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, 0, 0, style, slider);
        return;
    }

    const auto r = juce::Rectangle<int> (x, y, width, height).toFloat();
    const auto track = r.withSizeKeepingCentre (r.getWidth(), 6.0f);
    Hardware::drawRecess (g, track, 3.0f);
    g.setColour (slider.findColour (juce::Slider::trackColourId).withAlpha (slider.isEnabled() ? 0.9f : 0.35f));
    g.fillRoundedRectangle (track.reduced (1.0f).withRight (sliderPos), 2.0f);

    // Fader cap
    const auto cap = juce::Rectangle<float> (12.0f, juce::jmin (r.getHeight(), 18.0f)).withCentre ({ sliderPos, r.getCentreY() });
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (cap.translated (0.0f, 1.5f), 2.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4d52), 0.0f, cap.getY(), juce::Colour (0xff1a1b1d), 0.0f,
                                             cap.getBottom(), false));
    g.fillRoundedRectangle (cap, 2.0f);
    g.setColour (Colours::text);
    g.fillRect (cap.getCentreX() - 0.75f, cap.getY() + 3.0f, 1.5f, cap.getHeight() - 6.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (cap, 2.0f, 0.8f);
}

void VocalChainLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                             bool over, bool down)
{
    const auto r = button.getLocalBounds().toFloat().reduced (1.5f);
    const float corner = 3.0f;
    const bool on = button.getToggleState();
    const auto onColour = button.findColour (juce::TextButton::buttonOnColourId);

    // The opening in the panel the button sits in
    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.fillRoundedRectangle (r.expanded (1.0f), corner + 1.0f);
    const auto cap = down ? r.translated (0.0f, 0.5f) : r;

    if (on)
    {
        // Illuminated cap
        g.setColour (onColour.withAlpha (0.16f));
        g.fillRoundedRectangle (r.expanded (2.5f), corner + 2.5f);
        auto lit = over ? onColour.brighter (0.1f) : onColour;
        g.setGradientFill (juce::ColourGradient (lit.brighter (0.35f), 0.0f, cap.getY(), lit.darker (0.35f), 0.0f, cap.getBottom(), false));
        g.fillRoundedRectangle (cap, corner);
        juce::ColourGradient diffuse (juce::Colours::white.withAlpha (0.3f), cap.getCentreX(), cap.getCentreY(),
                                      juce::Colours::white.withAlpha (0.0f), cap.getCentreX(), cap.getY() - cap.getHeight() * 0.3f, true);
        g.setGradientFill (diffuse);
        g.fillRoundedRectangle (cap, corner);
    }
    else
    {
        auto base = button.findColour (juce::TextButton::buttonColourId);
        if (over)
            base = base.brighter (0.15f);
        g.setGradientFill (juce::ColourGradient (base.brighter (0.25f), 0.0f, cap.getY(), base.darker (0.45f), 0.0f, cap.getBottom(), false));
        g.fillRoundedRectangle (cap, corner);
        g.setTiledImageFill (Hardware::brushedTexture(), 0, 0, 0.5f);
        g.fillRoundedRectangle (cap, corner);
    }

    g.setColour (juce::Colours::white.withAlpha (on ? 0.4f : 0.16f));
    g.drawLine (cap.getX() + corner, cap.getY() + 0.75f, cap.getRight() - corner, cap.getY() + 0.75f, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.9f));
    g.drawRoundedRectangle (cap, corner, 0.8f);
}

void VocalChainLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool down)
{
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.setColour (button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId
                                                            : juce::TextButton::textColourOffId)
                     .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (4, 2).translated (0, down ? 1 : 0),
                      juce::Justification::centred, 1);
}

juce::Font VocalChainLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return juce::Font (juce::FontOptions (juce::jmin (13.0f, (float) buttonHeight * 0.55f), juce::Font::bold).withKerningFactor (0.03f));
}

void VocalChainLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int,
                                     juce::ComboBox& box)
{
    // A small backlit read-out window
    const auto r = juce::Rectangle<int> (0, 0, width, height).toFloat();
    Hardware::drawRecess (g, r, 3.0f);
    const auto glass = r.reduced (2.0f);
    const auto tint = box.findColour (juce::ComboBox::backgroundColourId).interpolatedWith (Colours::signature, 0.05f);
    g.setGradientFill (juce::ColourGradient (tint.brighter (0.35f), 0.0f, glass.getY(), tint, 0.0f, glass.getBottom(), false));
    g.fillRoundedRectangle (glass, 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.04f));
    g.fillRoundedRectangle (glass.withHeight (glass.getHeight() * 0.45f), 2.0f);

    juce::Path arrow;
    const float ax = (float) width - 13.0f, ay = (float) height * 0.5f;
    arrow.startNewSubPath (ax - 4.0f, ay - 2.0f);
    arrow.lineTo (ax, ay + 2.5f);
    arrow.lineTo (ax + 4.0f, ay - 2.0f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId).withAlpha (box.isEnabled() ? 0.9f : 0.4f));
    g.strokePath (arrow, juce::PathStrokeType (1.5f));
}

void VocalChainLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (6, 1, box.getWidth() - 26, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void VocalChainLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    Hardware::drawBrushedMetal (g, juce::Rectangle<int> (width, height).toFloat(), Colours::panel);
    g.setColour (Colours::signature.withAlpha (0.5f));
    g.drawRect (0, 0, width, height, 1);
}

void VocalChainLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto r = juce::Rectangle<int> (width, height).toFloat();
    g.setColour (Colours::panel);
    g.fillRect (r);
    g.setColour (Colours::signature.withAlpha (0.6f));
    g.drawRect (r, 1.0f);
    g.setColour (Colours::text);
    g.setFont (uiFont (13.0f));
    g.drawFittedText (text, r.toNearestInt().reduced (6, 3), juce::Justification::centredLeft, 6);
}
} // namespace lavc
