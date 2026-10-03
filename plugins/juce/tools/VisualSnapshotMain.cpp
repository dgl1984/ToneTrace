#include <JuceHeader.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ui/ToneTraceDesignSystem.h"

#include <array>
#include <iostream>

namespace
{
struct SnapshotSize
{
    const char* name;
    int width;
    int height;
};

const std::array<SnapshotSize, 4> sizes {{
    { "minimum", tonetrace::ui::minimumWidth, tonetrace::ui::minimumHeight },
    { "default", tonetrace::ui::defaultWidth, tonetrace::ui::defaultHeight },
    { "wide", 1200, 900 },
    { "maximum", tonetrace::ui::maximumWidth, tonetrace::ui::maximumHeight }
}};

struct ImageVariation
{
    bool varied = false;
    juce::uint32 baselineArgb = 0;
    int samples = 0;
    int differingSamples = 0;
};

struct SnapshotResult
{
    bool ok = false;
    juce::String failureKind;
    juce::String detail;
};

ImageVariation analyseImageVariation(const juce::Image& image)
{
    ImageVariation result;
    if (! image.isValid() || image.getWidth() <= 0 || image.getHeight() <= 0)
        return result;

    result.baselineArgb = image.getPixelAt(0, 0).getARGB();
    const int stepX = juce::jmax(1, image.getWidth() / 80);
    const int stepY = juce::jmax(1, image.getHeight() / 80);

    for (int y = 0; y < image.getHeight(); y += stepY)
    {
        for (int x = 0; x < image.getWidth(); x += stepX)
        {
            ++result.samples;
            if (image.getPixelAt(x, y).getARGB() != result.baselineArgb)
            {
                result.varied = true;
                ++result.differingSamples;
            }
        }
    }

    return result;
}

int countVisibleDirectChildren(const juce::Component& component)
{
    int count = 0;
    for (int i = 0; i < component.getNumChildComponents(); ++i)
        if (const auto* child = component.getChildComponent(i); child != nullptr && child->isVisible())
            ++count;
    return count;
}

SnapshotResult renderAndWritePng(juce::Component& component,
                                 const juce::File& file,
                                 juce::String& diagnostics)
{
    const auto fail = [&](const juce::String& kind, const juce::String& detail)
    {
        diagnostics << "FAIL " << file.getFileName() << " | " << kind << " | " << detail << "\n";
        return SnapshotResult { false, kind, detail };
    };

    if (component.getWidth() <= 0 || component.getHeight() <= 0)
        return fail("COMPONENT_FAILURE", "root has non-positive size");

    // The renderer intentionally exercises the real component hierarchy without
    // modifying production editor code. A peer is not required by JUCE's
    // createComponentSnapshot() path, but peer/isShowing state is recorded so a
    // future hosting problem is immediately distinguishable from a render problem.
    component.setVisible(true);
    component.sendLookAndFeelChange();
    component.resized();

    const int visibleChildren = countVisibleDirectChildren(component);
    const bool hasPeer = component.getPeer() != nullptr;
    diagnostics << "STATE " << file.getFileName()
                << " | size=" << component.getWidth() << "x" << component.getHeight()
                << " | visible-direct-children=" << visibleChildren
                << " | peer=" << (hasPeer ? "present" : "none-offscreen-supported")
                << " | isShowing=" << (component.isShowing() ? "true" : "false") << "\n";

    if (component.isOnDesktop() && ! hasPeer)
        return fail("PEER_FAILURE", "component reports desktop attachment but has no ComponentPeer");

    if (visibleChildren <= 0)
        return fail("LAYOUT_FAILURE", "root has no visible direct children after resized()");

    // JUCE 8.0.12 implements createComponentSnapshot() by creating an Image,
    // painting the complete component hierarchy into it, then returning only
    // after the internal Graphics context has been destroyed. Explicitly request
    // SoftwareImageType so Windows Direct2D image backing cannot be read while a
    // GPU Graphics context is still active (an earlier black-image failure).
    auto snapshot = component.createComponentSnapshot(component.getLocalBounds(),
                                                      true,
                                                      1.0f,
                                                      juce::SoftwareImageType{});
    if (! snapshot.isValid())
        return fail("SNAPSHOT_FAILURE", "createComponentSnapshot() returned an invalid image");

    if (snapshot.getWidth() != component.getWidth() || snapshot.getHeight() != component.getHeight())
    {
        return fail("SNAPSHOT_FAILURE",
                    "snapshot dimensions " + juce::String(snapshot.getWidth()) + "x"
                    + juce::String(snapshot.getHeight()) + " did not match component dimensions");
    }

    // Composite onto the Tone Trace background. This matters for Options, whose
    // content component intentionally leaves dialog chrome/background to JUCE.
    // Keep Graphics in a nested scope so all drawing is complete before any pixel
    // inspection or PNG encoding begins.
    juce::Image composited(juce::Image::ARGB,
                           snapshot.getWidth(),
                           snapshot.getHeight(),
                           true,
                           juce::SoftwareImageType{});
    {
        juce::Graphics g(composited);
        g.fillAll(tonetrace::ui::colour::background);
        g.drawImageAt(snapshot, 0, 0);
    }

    const auto variation = analyseImageVariation(composited);
    diagnostics << "IMAGE " << file.getFileName()
                << " | baseline-argb=0x" << juce::String::toHexString(static_cast<int>(variation.baselineArgb))
                << " | samples=" << variation.samples
                << " | differing-samples=" << variation.differingSamples << "\n";

    if (! variation.varied)
        return fail("UNIFORM_IMAGE_FAILURE", "rendered image contains no sampled visual variation");

    if (file.existsAsFile() && ! file.deleteFile())
        return fail("PNG_WRITE_FAILURE", "could not remove an existing PNG before rewrite");

    auto stream = file.createOutputStream();
    if (stream == nullptr)
        return fail("PNG_WRITE_FAILURE", "could not create output stream");

    juce::PNGImageFormat png;
    if (! png.writeImageToStream(composited, *stream))
        return fail("PNG_WRITE_FAILURE", "PNG encoder returned false");

    stream->flush();
    stream.reset();

    if (! file.existsAsFile() || file.getSize() <= 0)
        return fail("PNG_WRITE_FAILURE", "encoder reported success but file is missing or empty");

    diagnostics << "PASS " << file.getFileName() << " | bytes=" << file.getSize() << "\n";
    return SnapshotResult { true, {}, {} };
}

juce::String componentLabel(const juce::Component& component)
{
    if (component.getTitle().isNotEmpty()) return component.getTitle();
    if (component.getName().isNotEmpty()) return component.getName();
    if (const auto* button = dynamic_cast<const juce::Button*>(&component))
        if (button->getButtonText().isNotEmpty()) return button->getButtonText();
    return "<unnamed>";
}

void inspectFocusableChildren(juce::Component& root,
                              const juce::String& context,
                              juce::String& manifest,
                              bool& ok)
{
    juce::Array<juce::Component*> focusables;
    for (auto* child : root.getChildren())
    {
        if (child == nullptr || !child->isVisible() || !child->getWantsKeyboardFocus()) continue;
        focusables.add(child);
        const auto bounds = child->getBounds();
        const bool boundsOk = bounds.getWidth() > 0 && bounds.getHeight() > 0
            && root.getLocalBounds().contains(bounds);
        const auto label = componentLabel(*child);
        const int order = child->getExplicitFocusOrder();
        const bool labelled = label != "<unnamed>";
        const bool orderOk = order > 0;
        ok = ok && boundsOk && labelled && orderOk;
        manifest << (boundsOk && labelled && orderOk ? "PASS " : "FAIL ")
                 << context << " | focus " << order << " | " << label
                 << " | bounds " << bounds.getX() << "," << bounds.getY() << ","
                 << bounds.getWidth() << "x" << bounds.getHeight() << "\n";
    }

    for (int i = 0; i < focusables.size(); ++i)
    {
        for (int j = i + 1; j < focusables.size(); ++j)
        {
            const auto intersection = focusables[i]->getBounds().getIntersection(focusables[j]->getBounds());
            if (!intersection.isEmpty())
            {
                ok = false;
                manifest << "FAIL " << context << " | overlapping focusable controls | "
                         << componentLabel(*focusables[i]) << " <> " << componentLabel(*focusables[j])
                         << " | overlap " << intersection.getWidth() << "x" << intersection.getHeight() << "\n";
            }
        }
    }
}

juce::String pageName(int pageId)
{
    if (pageId == 1) return "capture-transfer";
    if (pageId == 2) return "correction-controls";
    return "bands-" + juce::String(pageId - 2);
}
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    const auto cwd = juce::File::getCurrentWorkingDirectory();
    juce::File output = cwd.getChildFile("visual-snapshots");
    if (argc > 1)
    {
        const juce::String requested = juce::String::fromUTF8(argv[1]);
        output = juce::File::isAbsolutePath(requested) ? juce::File(requested) : cwd.getChildFile(requested);
    }

    if (output.exists()) output.deleteRecursively();
    if (!output.createDirectory())
    {
        std::cerr << "Could not create snapshot output directory: " << output.getFullPathName() << "\n";
        return 2;
    }

    ToneTraceProcessor processor;
    processor.setRateAndBufferSizeDetails(48000.0, 512);
    processor.prepareToPlay(48000.0, 512);
    ToneTraceEditor editor(processor);

    juce::String manifest;
    juce::String focusManifest;
    juce::String diagnostics;
    focusManifest << "Tone Trace 1.1 focus/layout manifest\n";
    focusManifest << "Visible keyboard-focusable direct children must be named, ordered, in bounds, and non-overlapping.\n\n";
    manifest << "Tone Trace 1.1 visual snapshots\n";
    manifest << "These are JUCE createComponentSnapshot() renders of the real ToneTraceEditor on this machine.\n";
    manifest << "They use SoftwareImageType for deterministic readback and test logical layout rather than platform DPI scaling.\n\n";
    diagnostics << "Tone Trace 1.1 snapshot diagnostics\n";
    diagnostics << "peer=none-offscreen-supported is expected for Component::createComponentSnapshot().\n";
    diagnostics << "Failure kinds: COMPONENT_FAILURE, PEER_FAILURE, LAYOUT_FAILURE, SNAPSHOT_FAILURE, UNIFORM_IMAGE_FAILURE, PNG_WRITE_FAILURE.\n\n";

    bool ok = true;
    for (const auto& size : sizes)
    {
        editor.setSize(size.width, size.height);
        const int pages = editor.getSnapshotPageCount();
        for (int page = 1; page <= pages; ++page)
        {
            editor.setSnapshotPage(page);
            const auto name = juce::String(size.name) + "_" + juce::String(size.width) + "x"
                + juce::String(size.height) + "_" + pageName(page) + ".png";
            const auto file = output.getChildFile(name);
            const auto result = renderAndWritePng(editor, file, diagnostics);
            ok = ok && result.ok;
            manifest << (result.ok ? "PASS " : "FAIL ") << name;
            if (! result.ok)
                manifest << " | " << result.failureKind << " | " << result.detail;
            manifest << "\n";
            inspectFocusableChildren(editor, name, focusManifest, ok);
        }
    }

    if (auto options = editor.createOptionsSnapshotComponent())
    {
        const auto file = output.getChildFile("options-dialog_470x330.png");
        const auto result = renderAndWritePng(*options, file, diagnostics);
        ok = ok && result.ok;
        manifest << (result.ok ? "PASS " : "FAIL ") << file.getFileName();
        if (! result.ok)
            manifest << " | " << result.failureKind << " | " << result.detail;
        manifest << "\n";
        inspectFocusableChildren(*options, file.getFileName(), focusManifest, ok);
    }
    else
    {
        ok = false;
        manifest << "FAIL options-dialog_470x330.png | COMPONENT_FAILURE | Options snapshot component was not created\n";
        diagnostics << "FAIL options-dialog_470x330.png | COMPONENT_FAILURE | Options snapshot component was not created\n";
    }

    output.getChildFile("SNAPSHOT_MANIFEST.txt").replaceWithText(manifest);
    output.getChildFile("UI_FOCUS_LAYOUT_MANIFEST.txt").replaceWithText(focusManifest);
    output.getChildFile("SNAPSHOT_DIAGNOSTICS.txt").replaceWithText(diagnostics);
    std::cout << "Visual snapshots: " << output.getFullPathName() << "\n";
    std::cout << manifest << std::endl;
    return ok ? 0 : 3;
}
