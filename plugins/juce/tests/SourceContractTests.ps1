$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
function Read-Source { param([string]$Relative) Get-Content -LiteralPath (Join-Path $root $Relative) -Raw }
function Require-Text { param([string]$Haystack,[string]$Needle,[string]$Message) if (-not $Haystack.Contains($Needle)) { throw $Message } }
function Reject-Text { param([string]$Haystack,[string]$Needle,[string]$Message) if ($Haystack.Contains($Needle)) { throw $Message } }
function Require-Sha256 {
    param([string]$Relative,[string]$Expected)
    $path = Join-Path $root $Relative
    if (-not (Test-Path -LiteralPath $path)) { throw ('Missing hash-locked release file: ' + $Relative) }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $Expected.ToLowerInvariant()) {
        throw ('Released file changed: ' + $Relative + '; expected SHA-256 ' + $Expected + ', got ' + $actual)
    }
}

$cmake = Read-Source 'CMakeLists.txt'
$editor = Read-Source 'src\PluginEditor.cpp'
$editorHeader = Read-Source 'src\PluginEditor.h'
$processor = Read-Source 'src\PluginProcessor.cpp'
$processorHeader = Read-Source 'src\PluginProcessor.h'
$catalog = Read-Source 'src\ToneTraceProductCatalog.h'
$capture = Read-Source 'src\capture\ToneTraceCaptureModel.cpp'
$captureHeader = Read-Source 'src\capture\ToneTraceCaptureModel.h'
$bandPages = Read-Source 'src\ui\ToneTraceBandPageLayout.cpp'
$bandPageTests = Read-Source 'tests\BandPageLayoutTests.cpp'
$controller = Read-Source 'src\correction\ToneTraceCorrectionController.cpp'
$controllerHeader = Read-Source 'src\correction\ToneTraceCorrectionController.h'
$controllerTests = Read-Source 'tests\CorrectionControllerTests.cpp'
$slider = Read-Source 'src\controls\ToneTraceParameterSlider.cpp'
$sliderHeader = Read-Source 'src\controls\ToneTraceParameterSlider.h'
$bandSlider = Read-Source 'src\controls\ToneTraceBandSlider.cpp'
$bandSliderHeader = Read-Source 'src\controls\ToneTraceBandSlider.h'
$accessibleCombo = Read-Source 'src\controls\ToneTraceAccessibleComboBox.cpp'
$accessibleComboHeader = Read-Source 'src\controls\ToneTraceAccessibleComboBox.h'
$pageSelector = Read-Source 'src\controls\ToneTracePageSelector.cpp'
$pageSelectorHeader = Read-Source 'src\controls\ToneTracePageSelector.h'
$choice = Read-Source 'src\controls\ToneTraceChoiceControl.cpp'
$choiceHeader = Read-Source 'src\controls\ToneTraceChoiceControl.h'
$keyboardTests = Read-Source 'tests\KeyboardControlTests.cpp'
$stateTests = Read-Source 'tests\ProcessorStateTests.cpp'
$snapshotMain = Read-Source 'tools\VisualSnapshotMain.cpp'
$commandHeader = Read-Source 'src\controls\ToneTraceCommandButton.h'
$readonly = Read-Source 'src\controls\ToneTraceReadOnlyField.cpp'
$readonlyHeader = Read-Source 'src\controls\ToneTraceReadOnlyField.h'
$toggle = Read-Source 'src\controls\ToneTraceToggleControl.cpp'
$toggleHeader = Read-Source 'src\controls\ToneTraceToggleControl.h'
$tone = Read-Source 'src\tone\ToneTraceNotificationTone.cpp'
$toneHeader = Read-Source 'src\tone\ToneTraceNotificationTone.h'
$bypass = Read-Source 'src\tone\ToneTraceOutputBypass.cpp'
$bypassHeader = Read-Source 'src\tone\ToneTraceOutputBypass.h'
$toneTests = Read-Source 'tests\NotificationToneTests.cpp'
$bypassTests = Read-Source 'tests\OutputBypassTests.cpp'
$graph = Read-Source 'src\ui\ToneTraceGraphSurface.cpp'
$design = Read-Source 'src\ui\ToneTraceDesignSystem.h'
$stoppedTransportTone = Read-Source 'src\platform\ToneTraceStoppedTransportTone.cpp'
$stoppedTransportToneHeader = Read-Source 'src\platform\ToneTraceStoppedTransportTone.h'
$clapCompatibility = Read-Source 'cmake\ToneTraceClapCompatibility.cmake'

# Exact 1.0.5 release provenance: these bytes are not JUCE rewrites.
Require-Sha256 'vendor\tonetrace-1.0.5\include\tonetrace\tonetrace_engine.h' '780501c603678d4d55afd827f1ab079bf90f606b551f2e08a54c6d4e86837987'
Require-Sha256 'vendor\tonetrace-1.0.5\src\tonetrace_engine.cpp' 'a4b7be60159dd2b0ed5cad2028cdf33ce81130c0165e8a1b825b929888dc9435'
Require-Sha256 'vendor\tonetrace-1.0.5\include\tonetrace\tonetrace_realtime.h' '64d8147398afc1a1704425d549f2117c39e67592750b46ad4e62ca7d082f2d93'
Require-Sha256 'vendor\tonetrace-1.0.5\src\tonetrace_realtime.cpp' '4a4419a7f0d58e57f64a083d469c9359f5e531a4d0b18695e9e4b6523a2648c8'
Require-Sha256 'vendor\tonetrace-1.0.5\include\tonetrace\tonetrace_describe.h' 'b9ef67ef25450a43ff7944c0511a99c2200cf19471d718806d31aa442a42a86d'
Require-Sha256 'vendor\tonetrace-1.0.5\src\tonetrace_describe.cpp' '554b2d6bdad6c2ce2417678e925a12ed83e141f669dde6741be8eac36ad217f6'
Require-Sha256 'tests\ReleasedEngineTests.cpp' 'b00ede00c8dd8aa25de2cac93a539c3e245fc935e09490d35b7e03ccc8d7c149'
Require-Sha256 'tests\ReleasedRealtimeTests.cpp' '87df8eafd0ccd7c6d9288f386c8f3f7d0eb6b14b980ac0efadaca9db3812921f'
Require-Text $cmake 'ToneTraceReleasedEngineTests' 'Exact released engine tests must be part of the build.'
Require-Text $cmake 'ToneTraceReleasedRealtimeTests' 'Exact released realtime tests must be part of the build.'
Require-Text $cmake 'ToneTraceCorrectionControllerTests' 'JUCE bridge correction controller must have an independent test target.'
Require-Text $cmake 'ToneTraceNotificationToneTests' 'Notification tone engine must have an independent test target.'
Require-Text $cmake 'ToneTraceOutputBypassTests' 'Complete-output bypass must have an independent test target.'

# Frozen JUCE 01a accessibility/control boundary.
Require-Text $sliderHeader 'public juce::Slider' 'Continuous control must remain a real JUCE Slider.'
Require-Text $accessibleComboHeader 'public juce::ComboBox' 'Shared keyboard-aware choice control must remain a real JUCE ComboBox.'
Require-Text $choiceHeader 'public ToneTraceAccessibleComboBox' 'Match Mode must use the shared accessible ComboBox keyboard path.'
Require-Text $toggleHeader 'public juce::ToggleButton' 'Tone Notifications and Bypass must remain real JUCE ToggleButtons.'
Require-Text $commandHeader 'public juce::TextButton' 'Workflow controls must remain real JUCE TextButtons.'
Require-Text $readonlyHeader 'public juce::TextEditor' 'Status must remain a real JUCE TextEditor.'
Require-Text $slider 'setAccessible(true);' 'Slider must remain explicitly accessible.'
Reject-Text $slider 'createAccessibilityHandler' 'Do not replace JUCE Slider accessibility with a custom provider.'
Reject-Text $choice 'createAccessibilityHandler' 'Do not replace JUCE ComboBox accessibility with a custom provider.'
Reject-Text $accessibleCombo 'createAccessibilityHandler' 'Shared ComboBox must retain JUCE accessibility.'
Reject-Text $toggle 'createAccessibilityHandler' 'Do not replace JUCE ToggleButton accessibility with a custom provider.'
Reject-Text $slider 'mouseDown(' 'Pointer mouseDown must remain JUCE-owned.'
Reject-Text $slider 'mouseDrag(' 'Pointer drag must remain JUCE-owned.'
Reject-Text $slider 'mouseWheelMove(' 'Pointer wheel must remain JUCE-owned.'
Reject-Text $slider 'mouseDoubleClick(' 'Pointer double-click must remain JUCE-owned.'
Require-Text $slider 'return juce::Slider::keyPressed(key);' 'Uncustomized slider keys must fall through to JUCE.'
Require-Text $slider 'juce::KeyPress::homeKey' 'Continuous slider Home-to-minimum shortcut missing.'
Require-Text $slider 'spec_.parameter->minimum' 'Continuous slider Home must select the public minimum.'
Require-Text $slider 'juce::KeyPress::endKey' 'Continuous slider End-to-maximum shortcut missing.'
Require-Text $slider 'spec_.parameter->maximum' 'Continuous slider End must select the public maximum.'
Require-Text $accessibleCombo 'juce::KeyPress::homeKey' 'ComboBox Home-to-first shortcut missing.'
Require-Text $accessibleCombo 'juce::KeyPress::endKey' 'ComboBox End-to-last shortcut missing.'
Require-Text $pageSelectorHeader 'explicit ToneTracePageSelector(juce::Component& parent);' 'The selector must manage direct page-button children of the editor.'
Require-Text $pageSelectorHeader 'public juce::TextButton' 'Each page selector item must be a standard JUCE TextButton.'
Require-Text $editorHeader 'ToneTracePageSelector pageSelector_' 'The editor must own one authoritative page selector.'
Require-Text $editor 'pageSelector_(*this)' 'The page selector must attach its standard buttons directly to the editor.'
Reject-Text $editorHeader 'viewSelector_' 'Do not reintroduce the obsolete View-page ComboBox.'
Reject-Text $pageSelectorHeader 'TabbedButtonBar' 'Do not reintroduce the failed TabbedButtonBar page-selector abstraction.'
Reject-Text $pageSelectorHeader 'TabBarButton' 'Do not reintroduce the failed TabBarButton page-selector abstraction.'
Reject-Text $pageSelector 'createAccessibilityHandler' 'Page buttons must use JUCE native Button accessibility rather than a custom handler.'
Require-Text $pageSelector 'parent_.addAndMakeVisible(*button);' 'Page buttons must remain direct editor children for normal JUCE focus/accessibility traversal.'
Require-Text $pageSelector 'setClickingTogglesState(true);' 'Page buttons must use ordinary JUCE toggle state.'
Require-Text $pageSelector 'setRadioGroupId(kRadioGroupId, juce::dontSendNotification);' 'Page buttons must expose JUCE radio-button semantics through a real radio group.'
Require-Text $pageSelector 'setWantsKeyboardFocus(selected);' 'Page buttons must use one roving keyboard Tab stop.'
Require-Text $pageSelector 'selectedIndex_ = index;' 'The page selector must have one authoritative selected-page state.'
Require-Text $pageSelector 'onPageChanged(selectedIndex_, userInitiated);' 'Mouse and keyboard selection must converge on one page-change callback.'
Require-Text $editor 'pageSelector_.selectRelativeFromUser(modifiers.isShiftDown() ? -1 : 1)' 'Ctrl+Tab and Ctrl+Shift+Tab must route through the authoritative page selector.'
Require-Text $editor 'ToneTraceEditor::toggleTrace' 'Released global Trace Curve shortcut is required.'
Require-Text $keyboardTests 'parameter slider Home did not select minimum' 'Keyboard regression test for continuous Home is missing.'
Require-Text $keyboardTests 'parameter slider End did not select maximum' 'Keyboard regression test for continuous End is missing.'
Require-Text $keyboardTests 'combo Home did not select first item' 'Keyboard regression test for ComboBox Home is missing.'
Require-Text $keyboardTests 'combo End did not select last item' 'Keyboard regression test for ComboBox End is missing.'
Require-Text $keyboardTests 'pageParent.addToDesktop(juce::ComponentPeer::windowIsTemporary);' 'Accessibility regression must exercise page buttons under a real JUCE native peer.'
Require-Text $keyboardTests 'accessibility test harness did not create a native JUCE peer' 'Accessibility regression must fail explicitly if its native peer prerequisite is unavailable.'
Require-Text $keyboardTests 'page selector button is not exposed as a radio button to accessibility clients' 'Compiled accessibility-role regression for the page selector is missing.'
Require-Text $keyboardTests 'page selector does not use one roving keyboard Tab stop' 'Compiled roving-focus regression for the page selector is missing.'
Require-Text $keyboardTests 'page Right key did not select exactly one next page' 'Compiled one-page navigation regression is missing.'
Require-Text $keyboardTests 'page Left key did not select exactly one previous page' 'Compiled reverse one-page navigation regression is missing.'
Require-Text $keyboardTests 'page button click did not synchronize the authoritative page state' 'Compiled mouse/click-origin synchronization regression is missing.'
Require-Text $keyboardTests 'relative page navigation did not continue from the clicked visible page' 'Compiled clicked-page navigation-origin regression is missing.'
Require-Text $keyboardTests 'relative page navigation did not wrap exactly one page' 'Compiled Ctrl+Tab helper regression is missing.'
Require-Text $cmake 'ToneTraceKeyboardControlTests' 'Keyboard control regression target must be part of the build.'
Require-Text $cmake 'ToneTraceProcessorStateTests' 'Processor state round-trip regression target must be part of the build.'
Require-Text $stateTests 'restore before prepareToPlay' 'State regression must cover restore-before-prepare host lifecycle.'
Require-Text $stateTests 'restore after prepareToPlay' 'State regression must cover restore-after-prepare host lifecycle.'
Require-Text $stateTests 'manual band count expected' 'State regression must verify manual band persistence.'
Require-Text $stateTests 'band navigation tone was muted while Frozen' 'Processor regression must prove band navigation remains audible while Frozen.'
Require-Text $stateTests 'page navigation sweep was muted while Frozen' 'Processor regression must prove page sweeps remain audible while Frozen.'
Require-Text $stateTests 'Tone Notifications Off did not suppress navigation tone' 'Processor regression must prove Tone Notifications gates navigation tones.'
Require-Text $stateTests 'Bypass did not suppress navigation tone' 'Processor regression must prove Bypass gates navigation tones.'
Require-Text $stateTests 'offline rendering did not suppress navigation tone' 'Processor regression must prove offline render gates navigation tones.'
Require-Text $stateTests 'stopped-transport page sweep leaked into resumed audio' 'Processor regression must prove stopped page sweeps do not become stale audio-thread sweeps.'
Require-Text $stateTests 'stopped-transport band tone lost released resume parity' 'Processor regression must preserve the released stopped-band-tone queue behavior.'
Require-Text $processorHeader 'void reset() override;' 'VST3 processing-stop parity must use the JUCE processor lifecycle hook.'
Require-Text $processor 'hostProcessingActive_.store(false, std::memory_order_release);' 'Stopped host processing must publish through one atomic flag.'
Require-Text $processor 'hostProcessingActive_.store(true, std::memory_order_release);' 'The next real process block must mark host processing active again.'
Require-Text $processor 'stoppedTransportTone_.requestTone(frequencyHz);' 'Stopped single-band navigation must use the isolated released-style fallback.'
Require-Text $processor 'stoppedTransportTone_.requestSweep(fromHz, toHz, bandCount);' 'Stopped page navigation must use the isolated released-style fallback.'
Require-Text $cmake 'src/platform/ToneTraceStoppedTransportTone.cpp' 'Stopped-transport parity backend must be compiled into the product.'
Require-Text $stoppedTransportToneHeader 'class StoppedTransportTone final' 'Stopped-transport fallback must stay behind one small platform adapter.'
Require-Text $stoppedTransportTone '#define NOMINMAX' 'Windows fallback must protect std::min/std::max before including windows.h.'
Require-Text $stoppedTransportTone '#include <windows.h>' 'Windows fallback implementation is missing its isolated Win32 backend.'
Require-Text $stoppedTransportTone 'Beep(static_cast<DWORD>(std::clamp(toneFrequency, 37.0, 32767.0)), 125);' 'Stopped band tone must preserve released 125 ms Beep behavior.'
Require-Text $stoppedTransportTone 'const int totalBeeps = 2 * count - 1;' 'Stopped page sweep must preserve the released edge/center Beep count.'
Require-Text $stoppedTransportTone 'Beep(static_cast<DWORD>(std::clamp(frequencyHz, 37.0, 32767.0)), 60);' 'Stopped page sweep must preserve released 60 ms Beep steps.'
Require-Text $stoppedTransportTone 'std::thread thread;' 'Stopped Beep fallback must remain off the UI thread.'

Require-Text $cmake 'juce_add_console_app(ToneTraceKeyboardControlTests' 'Keyboard control tests must be a JUCE console-app target so JuceHeader generation is valid.'
Reject-Text $cmake 'add_executable(ToneTraceKeyboardControlTests' 'Do not use plain add_executable for a target that calls juce_generate_juce_header.'
Require-Text $keyboardTests 'juce::Component& firstPageComponent = *firstPage;' 'Keyboard page-selector test must enter through JUCE Component public keyPressed API.'
Require-Text $keyboardTests 'firstPageComponent.keyPressed(juce::KeyPress(juce::KeyPress::rightKey))' 'Keyboard page-selector test must still exercise the real virtual PageButton key handler.'
Reject-Text $keyboardTests 'firstPage->keyPressed(' 'Do not call JUCE Button::keyPressed directly from the regression test; the override is protected under MSVC/JUCE 8.0.12.'
Require-Text $readonly 'setTabKeyUsedAsCharacter(false);' 'Read-only status must leave Tab/Shift+Tab available.'
Reject-Text $editor 'keyboardFocusContainer' 'Do not reintroduce the failed focus-container experiments.'
Reject-Text $editor 'createAccessibilityHandler' 'Do not add a custom editor accessibility provider.'
Reject-Text $editor 'WM_SETFOCUS' 'Do not reintroduce REAPER-specific focus subclassing.'
Reject-Text $processor '#include <windows.h>' 'Do not leak Win32 headers into the JUCE processor; stopped-transport fallback must stay isolated.'
Reject-Text $processor 'Beep(' 'Win32 Beep fallback must not live in PluginProcessor.cpp.'

# Exact entry stays transient and tied to the invoking slider.
Require-Text $editorHeader 'ToneTraceParameterSlider* exactTarget_ = nullptr;' 'Exact entry must track the invoking slider.'
Require-Text $editor 'beginExactEntry(ToneTraceParameterSlider& target)' 'Exact entry must be one reusable path.'
Require-Text $editor 'target->parseExactText' 'Exact entry must use descriptor parsing.'
Require-Text $editor 'target->setFromExactEntry(parsed);' 'Exact commit must return through the slider path.'
Require-Text $editor 'exactEditor_->grabKeyboardFocus();' 'User-invoked exact entry must receive focus.'
Require-Text $editor 'target->grabKeyboardFocus();' 'Commit/cancel must return focus to the invoking slider.'
Require-Text $editor 'removeChildComponent(exactEditor_.get());' 'Exact editor must leave hierarchy on close.'
Require-Text $editor 'AccessibilityHandler::postAnnouncement' 'Invalid exact input must announce recovery.'

# Workflow UI is real and screen-reader reachable.
Require-Text $editorHeader 'ToneTraceCommandButton captureReferenceButton_' 'Capture Reference button missing.'
Require-Text $editorHeader 'ToneTraceCommandButton learnTargetButton_' 'Learn Target button missing.'
Require-Text $editorHeader 'ToneTraceCommandButton correctTargetButton_' 'Correct Target button missing.'
Require-Text $editorHeader 'ToneTraceCommandButton freezeCorrectionButton_' 'Freeze Correction button missing.'
Require-Text $editor 'processor_.setWorkflowFromUi(3);' 'Correct Target must use the single Workflow path.'
Require-Text $editor 'processor_.setWorkflowFromUi(4);' 'Freeze Correction must use the single Workflow path.'
Require-Text $editor 'correctTargetButton_.setExplicitFocusOrder' 'Correct Target must be in normal Tab order.'
Require-Text $editor 'freezeCorrectionButton_.setExplicitFocusOrder' 'Freeze Correction must be in normal Tab order.'
Require-Text $editor 'statusField_.hasKeyboardFocus(true)' 'Rapid telemetry updates must pause while status is being read.'

# Released notification controls use the same reusable JUCE paths.
Require-Text $catalog 'kConfidenceToneVolumeIndex = 8' 'Confidence Tone Volume must be an implemented continuous control.'
Require-Text $catalog 'kToneNotificationsIndex = 1' 'Tone Notifications toggle mapping missing.'
Require-Text $catalog 'kBypassIndex = 2' 'Bypass toggle mapping missing.'
Require-Text $catalog 'spec.releasedNumericId == 250 && value <= -59.5' 'Released -60 dB = Off formatting missing.'
Require-Text $editorHeader 'std::vector<std::unique_ptr<ToneTraceToggleControl>> toggleControls_' 'Tone toggle controls must be descriptor-driven/reusable.'
Require-Text $editor 'processor_.toggleParameter(index).addListener(this);' 'Custom toggle controls must stay synchronized with host parameters.'
Require-Text $editor 'for (auto& control : toggleControls_) control->setExplicitFocusOrder' 'Tone toggles must be in normal Tab order.'

# Visual graph is real but remains a visual duplicate of accessible product state.
Require-Text $design 'minimumWidth = 720' 'Minimum editor width must remain explicit.'
Require-Text $design 'defaultWidth = 960' 'Default editor width must remain explicit.'
Require-Text $graph 'setAccessible(false);' 'Graph must remain outside the accessibility tree; bands expose the editable curve.'
Require-Text $graph 'setWantsKeyboardFocus(false);' 'Graph must not become a Tab stop.'
Require-Text $graph 'evaluateCorrectionAt' 'Graph must render the same released correction evaluator used by the FIR path.'
Require-Text $graph 'manualGainDbAt' 'Graph must render manual-only correction when no learned profile exists.'
Require-Text $editor 'graphSurface_.updateFromState' 'Editor must update the graph from processor-owned product state.'

# Band pages are real JUCE controls with released keyboard/exact-entry behavior.
Require-Text $bandSliderHeader 'public juce::Slider' 'Each correction band must be a real JUCE Slider.'
Require-Text $bandSlider 'setAccessible(true);' 'Band sliders must be explicitly accessible.'
Require-Text $bandSlider 'juce::KeyPress::pageUpKey' 'Band Page Up +6 dB shortcut missing.'
Require-Text $bandSlider 'juce::KeyPress::pageDownKey' 'Band Page Down -6 dB shortcut missing.'
Require-Text $bandSlider 'juce::KeyPress::homeKey' 'Band Home-to-maximum shortcut missing.'
Require-Text $bandSlider 'setFromKeyboard(limitDb_)' 'Band Home must preserve released CLAP maximum/top behavior.'
Require-Text $bandSlider 'juce::KeyPress::endKey' 'Band End-to-minimum shortcut missing.'
Require-Text $bandSlider 'setFromKeyboard(-limitDb_)' 'Band End must preserve released CLAP minimum/bottom behavior.'
Require-Text $bandSlider 'onNeutralize' 'Band 0/N neutralization path missing.'
Require-Text $bandSlider 'std::strtod' 'Band exact entry must reject nonnumeric input rather than coercing it to zero.'
Require-Text $editor 'toneTraceBandPageSizes' 'Band pages must use the shared released paging layout.'
Require-Text $editor 'const bool pagesChanged = rebuildViewPages(true);' 'Resolution changes must detect a band-page rebuild.'
Require-Text $editor 'if (pagesChanged) resized();' 'Resolution changes must relayout newly rebuilt band pages immediately.'
Require-Text $editor '(void) rebuildViewPages(true);' 'Window resizing must recompute width-dependent band pagination.'
Require-Text $bandSliderHeader 'std::function<void(std::size_t)> onFocus' 'Band focus must expose one navigation-tone callback.'
Require-Text $bandSlider 'void ToneTraceBandSlider::focusGained' 'Band navigation tone must originate from actual JUCE keyboard focus.'
Require-Text $editor 'processor_.requestNavigationTone(processor_.bandFrequencyHz(band))' 'Band focus must request the exact current band-center frequency.'
Require-Text $editor 'processor_.requestNavigationSweep(lowEdge, highEdge, count, 1200.0);' 'Band-page selection must request the released 1.2 second page sweep.'
Require-Text $bandPages 'toneTraceBandPageSweepEdges' 'Page sweep edges must live in the shared format-neutral band-page helper.'
Require-Text $bandPages 'std::sqrt(frequencyAt(firstBand - 1) * firstFrequency)' 'Page sweep low edge must use the released geometric midpoint to the previous band.'
Require-Text $bandPages 'std::sqrt(lastFrequency * frequencyAt(lastBand + 1))' 'Page sweep high edge must use the released geometric midpoint to the next band.'
Require-Text $bandPageTests 'first/second page sweeps do not meet at one edge' 'Compiled page-sweep adjacency regression is missing.'
Require-Text $editor 'beginBandExactEntry' 'Bands must support the same explicit exact-entry workflow.'

# Import/export is part of the product, not an editor-only stub.
Require-Text $editorHeader 'juce::TextButton exportButton_ { "Export..." };' 'Single Export menu button missing.'
Require-Text $editorHeader 'juce::TextButton importButton_ { "Import..." };' 'Single Import menu button missing.'
Reject-Text $editorHeader 'exportButtons_' 'Per-format Export button vector must not return.'
Reject-Text $editorHeader 'importButtons_' 'Per-format Import button vector must not return.'
Require-Text $editor 'showExportMenu()' 'Export popup menu path missing.'
Require-Text $editor 'showImportMenu()' 'Import popup menu path missing.'
Require-Text $editor 'exportButton_.onClick = [this] { showExportMenu(); };' 'Export button is present but not wired to its popup menu.'
Require-Text $editor 'importButton_.onClick = [this] { showImportMenu(); };' 'Import button is present but not wired to its popup menu.'
Require-Text $editor 'Export Impulse Response (WAV)...' 'Released IR export menu item missing.'
Require-Text $editor 'Export Reference Curve (TTS)...' 'Released Reference export menu item missing.'
Require-Text $editor 'Export Target Curve (TTS)...' 'Released Target export menu item missing.'
Require-Text $editor 'Export Correction Model (TTM)...' 'Released model export menu item missing.'
Require-Text $editor 'Import Reference Curve (TTS)...' 'Released Reference import menu item missing.'
Require-Text $editor 'Import Target Curve (TTS)...' 'Released Target import menu item missing.'
Require-Text $editor 'Import Correction Model (TTM)...' 'Released model import menu item missing.'
Require-Text $editor '.withTargetComponent(&exportButton_)' 'Export menu must be anchored to the invoking button.'
Require-Text $editor '.withTargetComponent(&importButton_)' 'Import menu must be anchored to the invoking button.'
Require-Text $editor '.withParentComponent(this)' 'Transfer menus must stay inside the plug-in editor peer.'
Require-Text $editor '.withInitiallySelectedItem(1)' 'Transfer menus must initially select their first command so screen readers announce a real menu item on open.'
Require-Text $editor 'void focusInitialControl()' 'Options dialog must expose an explicit initial-focus path.'
Require-Text $editor 'fullRange_.grabKeyboardFocus();' 'Options dialog must initially focus the first released option.'
Require-Text $editor 'optionsContent->focusInitialControl();' 'Options dialog launch must move keyboard focus into the dialog immediately.'
Require-Text $editor 'curveReadout_.setText(text, juce::dontSendNotification);' 'Transfer results must be visible as well as announced.'
Require-Text $editor 'case 4: safe->chooseExport(3); break;' 'Export model menu selection must reach the model exporter.'
Require-Text $editor 'case 3: safe->chooseImport(2); break;' 'Import model menu selection must reach the model importer.'
Require-Text $editor 'A Tone Trace file chooser is already open.' 'Transfer actions must reject overlapping file choosers explicitly.'
Require-Text $editor 'if (file.getFileExtension().isEmpty())' 'Export must only apply the released default extension when the user omitted one.'
Require-Text $editor 'withFileExtension(kind == 0 ? ".wav" : (kind == 3 ? ".ttm" : ".tts"))' 'Export must supply the released default WAV/TTS/TTM extension.'
Require-Text $editor 'Impulse response exported.' 'IR export must retain released type-specific success feedback.'
Require-Text $editor 'Reference curve exported.' 'Reference export must retain released type-specific success feedback.'
Require-Text $editor 'Target curve exported.' 'Target export must retain released type-specific success feedback.'
Require-Text $editor 'Correction model exported.' 'Model export must retain released type-specific success feedback.'
Require-Text $editor 'Reference curve imported. Capture or import the target.' 'Reference import must retain released next-step feedback.'
Require-Text $editor 'Target curve imported.' 'Target import must retain released type-specific success feedback.'
Require-Text $editor 'Correction model imported.' 'Model import must retain released type-specific success feedback.'
Require-Text $editorHeader 'Options...' 'Options button missing from the JUCE editor.'
Require-Text $editor 'DialogWindow::LaunchOptions' 'Options must use the shared JUCE dialog path.'
Require-Text $editor 'Full Correction Range' 'Options dialog is missing Full Correction Range.'
Require-Text $editor 'Tone Notifications' 'Options dialog is missing Tone Notifications.'
Require-Text $editor 'Confidence Tone Volume' 'Options dialog is missing Confidence Tone Volume.'
Require-Text $editor 'Reset Tone Trace...' 'Options dialog is missing protected Reset.'
Require-Text $processor 'if (step == 5)' 'Workflow Arm Reset is not implemented.'
Require-Text $processor 'if (step == 6)' 'Workflow Confirm Reset is not implemented.'
Require-Text $processor 'if (step == 7)' 'Workflow Cancel Reset is not implemented.'
Require-Text $processor 'performConfirmedReset' 'Confirmed Reset must use one processor-owned transaction.'
Require-Text $processor 'void ToneTraceProcessor::cancelActiveTones() noexcept' 'Released workflow/import transitions must retain one shared tone-cancellation path.'
Require-Text $processorHeader 'pendingGeneratedToneStop_' 'Generated-tone stop ownership flag missing.'
Require-Text $processorHeader 'pendingCoreAudioReset_' 'Host reset must keep a separate correction-core audio reset flag.'
Require-Text $processor 'pendingCoreAudioReset_.store(true, std::memory_order_release);' 'AudioProcessor reset must request the released correction-core audio reset.'
Reject-Text $clapCompatibility 'tonetrace_patch_clap("Plugin::stopProcessing();"' 'CLAP stop_processing must not be rewritten to AudioProcessor::reset; released 1.0.5 resets convolution only from the CLAP reset callback.'
$cancelToneBody = [regex]::Match($processor, 'void ToneTraceProcessor::cancelActiveTones\(\) noexcept(?s:.*?)(?=void ToneTraceProcessor::startConfidenceTone)').Value
if ($cancelToneBody -match 'resetAudio|pendingCoreAudioReset_') { throw 'Released stopTones workflow transitions must not reset correction convolution history.' }
if (([regex]::Matches($processor, 'cancelActiveTones\(\);')).Count -lt 7) { throw 'Workflow/import parity lost one or more released stopTones transitions.' }
Require-Text $controller 'CorrectionController::reset()' 'Released-core reset seam missing.'
Require-Text $editor '#if JUCE_WINDOWS' 'Filesystem path conversion must remain platform-aware.'
Require-Text $editor '#else' 'Filesystem path conversion must include a non-Windows path.'
Require-Text $processor 'ToneTraceProcessor::importReference' 'Reference import processor transaction missing.'
Require-Text $processor 'ToneTraceProcessor::importTarget' 'Target import processor transaction missing.'
Require-Text $processor 'ToneTraceProcessor::importModel' 'Model import processor transaction missing.'
Require-Text $processor 'currentTransferRangeHz(20.0, 20000.0)' 'Spectrum import compatibility must follow the active correction range.'
Require-Text $processor 'currentTransferRangeHz(10.0, 30000.0)' 'Model import compatibility must follow the active correction range.'
Require-Text $processor 'setStatus(tonetrace::capture::Status::ImportedTarget);' 'Imported Target must retain the released pre-analysis status.'
Require-Text $processor 'queueAnalysisJob(AnalysisJobKind::AnalyzeCaptured);' 'Imported Target must use the shared background analysis path.'
Require-Text $processor ': tonetrace::capture::Status::InvalidCapture);' 'Rejected capture analysis must retain the released Invalid Capture status rather than reporting an internal analysis failure.'
if ([regex]::IsMatch($processor, 'if \(!jobStillCurrent\(job\)\)\s*\{\s*analysisBusy_\.store\(false')) { throw 'A stale analysis job must never clear the busy flag owned by a newer transaction.' }
Require-Text $processor 'importedTarget_.reset();' 'Learn Target must clear an older imported Target before live recapture.'
Require-Text $processorHeader 'ToneTraceBandDisplayState' 'Band UI refresh must have one coherent processor-side display snapshot.'
Require-Text $processor 'ToneTraceProcessor::bandDisplayState() const' 'Band display snapshot implementation missing.'
Require-Text $editor 'const auto bands = processor_.bandDisplayState();' 'Band page refresh must consume one coherent display snapshot instead of repeatedly copying the profile.'
Require-Text $editor 'if (pageSelector_.selectedPage() >= 2) updateBandControls();' 'Hidden band pages must not be recomputed for capture telemetry updates.'
Require-Text $stateTests 'testBandDisplaySnapshotConsistency();' 'Band display snapshot parity regression missing.'
Require-Text $controller 'const SpectrumCapture* importedTarget' 'Shared correction analysis must accept an imported Target without a second synchronous matcher.'
Require-Text $processor 'Build the complete candidate first' 'Model import must remain transactional.'
Require-Text $editor 'NativeMessageBox::showOkCancelBox' 'Manual-only IR export must retain the accessible native OK/Cancel warning.'
Require-Text $editor 'Reset Tone Trace EQ?' 'Reset confirmation must retain the released product title.'
Require-Text $editor 'manual band edits, Correction Gain, and current capture state.' 'Reset confirmation must disclose every released destructive reset effect.'
Require-Text $editor 'No learned match is available. Export the impulse response of the manually created curve?' 'Manual-only IR warning title must contain the complete decision.'
Require-Text $editor 'No matching curve is available to export yet.' 'Flat unmatched IR export must retain the no-curve error.'


# Released capture semantics remain frozen from human-passed Slice 02.
Require-Text $captureHeader 'kStandardCaptureSeconds = 30.0' 'Standard capture duration changed.'
Require-Text $captureHeader 'kCustomMaximumCaptureSeconds = 60.0' 'Custom Max capture duration changed.'
Require-Text $capture 'const double gateDb = mode == MatchMode::Voice ? -55.0 : -65.0;' 'Released accepted-audio gate changed.'
Require-Text $capture 'return validSeconds(sampleRate) >= 0.35 && updateCounter >= 3;' 'Reference readiness rule changed.'
Require-Text $capture 'workflowStepFromHostValue' 'REAPER/OSARA fractional Workflow behavior must remain explicit.'
Require-Text $processor 'preserveLiveCapture' 'Same-rate host lifecycle must preserve live capture.'
Reject-Text $processor 'referenceCapture_.release();' 'Host lifecycle must not discard a live Reference.'
Require-Text $processor 'referenceReady_.load' 'Learn Target must use the published Reference readiness fact.'
Require-Text $processor 'targetCorrectable_.load' 'Correct must use an atomic Target eligibility fact, not mutable capture memory.'
Require-Text $processor 'Status::CannotSaveYet' 'Premature Learn accessible status missing.'
Require-Text $processor 'setupLockedNotice_' 'Match Mode capture lock path missing.'

# Correct handoff: phase closes capture, writer count proves quiescence, worker copies later.
Require-Text $processorHeader 'captureWriterCount_' 'Capture writer-count handoff is required.'
Require-Text $processor 'captureWriterCount_.fetch_add' 'Audio block must enter capture ownership before reading phase.'
Require-Text $processor 'captureWriterCount_.fetch_sub' 'Audio block must leave capture ownership deterministically.'
Require-Text $processor 'phase_.store(tonetrace::capture::WorkflowPhase::Preview' 'Correct must leave capture phase before analysis.'
Require-Text $processor 'while (captureWriterCount_.load' 'Worker must wait for the last pre-transition capture block.'
Require-Text $processor 'const std::lock_guard<std::mutex> lock(captureControlMutex_);' 'Worker raw-audio copy must serialize with control-plane buffer resizing.'
Require-Text $processorHeader 'std::thread analysisThread_' 'Heavy analysis must have a non-audio worker.'
Require-Text $processor 'correctionController_.analyze' 'Correct must reach the released format-neutral engine.'
Require-Text $processor 'correctionController_.rebuild' 'Post-match Match Mode/Sharpness must rebuild retained spectra/profile.'
Require-Text $processorHeader 'deferredWorkflowCommand_' 'Workflow commands requested during Correct must be retained.'
Require-Text $processor 'deferredWorkflowCommand_.store(step' 'Busy Correct must hold the latest workflow command instead of discarding it.'
Require-Text $processor 'deferredWorkflowCommand_.exchange(0' 'Held workflow commands must replay through the normal workflow path.'
Require-Text $processor 'if (analysisBusy_.load(std::memory_order_acquire)) return;' 'Busy-time post-match setting changes must remain unconsumed for a later rebuild.'

$timerStart = $processor.IndexOf('void ToneTraceProcessor::timerCallback')
$timerEnd = $processor.IndexOf('void ToneTraceProcessor::consumeMatchModeChange', $timerStart)
if ($timerStart -lt 0 -or $timerEnd -le $timerStart) { throw 'Could not isolate timerCallback.' }
$timerText = $processor.Substring($timerStart, $timerEnd - $timerStart)
$workflowObserve = $timerText.IndexOf('consumeWorkflowParameterChange();')
$deferredReplay = $timerText.IndexOf('consumeDeferredWorkflowCommand();')
$matchObserve = $timerText.IndexOf('consumeMatchModeChange();')
$sharpnessObserve = $timerText.IndexOf('consumeCorrectionSettingChanges();')
if ($workflowObserve -lt 0 -or $deferredReplay -le $workflowObserve -or $matchObserve -le $deferredReplay -or $sharpnessObserve -le $matchObserve) {
    throw 'Timer order must remain workflow observation -> deferred replay -> Match Mode -> Sharpness.'
}

# Released engine ownership seam and Correction Gain single ownership.
Require-Text $controller 'MatchEngine engine;' 'Controller must call the released MatchEngine.'
Require-Text $controller 'core_->commitCandidate(candidate)' 'Controller must commit through released HeadlessPluginCore.'
Require-Text $controller 'render.correctionGainDb = 0.0;' 'Profile renderer must not duplicate Correction Gain.'
Require-Text $processorHeader 'kGainSmoothingSeconds = 0.020' 'Correction Gain smoothing changed.'
Require-Text $controller 'render.correctionSharpness = settings.correctionSharpness;' 'Sharpness must now reach the real released renderer.'
Require-Text $processor 'queueAnalysisJob(AnalysisJobKind::RebuildProfile)' 'Post-match changes must use the coalesced rebuild path.'

# State: one JUCE parameter registry + exact released Ready/Frozen core state.
Require-Text $processorHeader 'kStateSchemaVersion = 4' 'Recovered product state schema must remain explicit.'
Require-Text $processor 'ToneTraceJuce11State' 'JUCE state identity changed unexpectedly.'
Require-Text $processor 'state.setProperty("coreState"' 'Released Ready/Frozen core state must be embedded in JUCE state.'
Require-Text $processor 'state.setProperty("manualGains"' 'Manual-only band state must persist independently of a learned profile.'
Require-Text $processor 'serializeManualGains' 'Manual-band persistence must use one explicit serialization path.'
Require-Text $processor 'tonetrace::deserializeProjectState(coreState)' 'Core state must be fully validated before apply.'
Require-Text $processor 'restored.reserve(stateParameters_.size())' 'JUCE parameter state must validate into a complete temporary set.'
Require-Text $processor 'if (!state.hasProperty(record.key))' 'Missing parameter state must reject load.'
Require-Text $processor 'if (!std::isfinite(normalised) || normalised < 0.0 || normalised > 1.0) return false;' 'Invalid normalized state must reject load.'
Reject-Text $processor 'stateParameters_.push_back({ juce::Identifier(workflowSpec.key)' 'Workflow Step must not persist resumable live capture.'
Require-Text $controller 'cachedProjectState_' 'State save must use the last validated profile cache.'
Require-Text $controller 'stateCacheMutex_' 'Cached state access must be serialized independently of long analysis.'
Require-Text $processor 'SetupChanged' 'Outer-control/newer-than-profile state must restore visibly and rebuild rather than disappear.'
Require-Text $processor 'constexpr std::size_t matchModeStateIndex = tonetrace::product::kImplementedContinuousControls.size();' 'State restore must address Match Mode explicitly after adding new parameters.'
Reject-Text $processor 'matchMode_->convertFrom0to1(restored.back())' 'State restore must not assume Match Mode is the last persisted parameter.'
Require-Text $controllerTests 'malformed restore changed the cached last-known-good state' 'Malformed-restore atomicity regression test missing.'
Require-Text $controllerTests 'concurrent save returned a partial/unparseable state' 'Concurrent cached-state regression test missing.'

$saveStateStart = $controller.IndexOf('std::string CorrectionController::saveProjectState() const')
$saveStateEnd = $controller.IndexOf('void CorrectionController::refreshCachedStateLocked', $saveStateStart)
if ($saveStateStart -lt 0 -or $saveStateEnd -le $saveStateStart) { throw 'Could not isolate CorrectionController::saveProjectState.' }
$saveStateText = $controller.Substring($saveStateStart, $saveStateEnd - $saveStateStart)
Require-Text $saveStateText 'stateCacheMutex_' 'State save must acquire only the independent cache lock.'
Require-Text $saveStateText 'cachedProjectState_' 'State save must return the cached complete state.'
foreach ($forbidden in @('controlMutex_', 'serializeProjectState', 'core_->saveProjectState', 'MatchEngine', 'renderMinimumPhaseIr')) {
    Reject-Text $saveStateText $forbidden ('State save reintroduced long/heavy work: ' + $forbidden)
}

# Audio-thread gate. No analysis, allocation, locks, host calls, or state work in processBlock.
$processStart = $processor.IndexOf('void ToneTraceProcessor::processBlock')
$nextMethod = $processor.IndexOf('bool ToneTraceProcessor::statusTonesAllowed', $processStart)
if ($processStart -lt 0 -or $nextMethod -le $processStart) { throw 'Could not isolate processBlock.' }
$processText = $processor.Substring($processStart, $nextMethod - $processStart)
Require-Text $processText 'gainSmoother_.getNextValue();' 'Human-passed 20 ms JUCE Correction Gain stage must remain in the chunked output path.'
foreach ($forbidden in @('new ', 'make_unique', 'std::vector', 'std::mutex', 'lock_guard', 'ScopedLock', 'sleep_for', 'Thread::sleep', 'MessageManager', 'setValueNotifyingHost', 'beginChangeGesture', 'endChangeGesture', 'resize(', 'assign(', 'MatchEngine', 'renderMinimumPhaseIr', 'serializeProjectState', 'deserializeProjectState')) {
    Reject-Text $processText $forbidden ('Realtime processBlock contains forbidden operation: ' + $forbidden)
}
$transactionPosition = $processText.IndexOf('executePendingAudioCommand();')
$capturePosition = $processText.IndexOf('captureInputSlice(buffer);')
$corePosition = $processText.IndexOf('correctionController_.process')
$gainPosition = $processText.IndexOf('gainSmoother_.getNextValue();')
$tonePosition = $processText.IndexOf('renderGeneratedTone(outputs, channels')
$bypassPosition = $processText.IndexOf('applyGlobalBypass(outputs, dry, channels')
if ($transactionPosition -lt 0 -or $capturePosition -le $transactionPosition -or $corePosition -le $capturePosition -or $gainPosition -le $corePosition -or $tonePosition -le $gainPosition -or $bypassPosition -le $tonePosition) {
    throw 'processBlock order must remain transaction -> input capture -> released correction -> Correction Gain -> generated tone -> complete-output bypass.'
}

# Import/export path conversion must use APIs present in JUCE 8.0.12.
Require-Text $editor 'toWideCharPointer()' 'Windows transfer paths must use JUCE 8 toWideCharPointer for Unicode-safe std::filesystem conversion.'
Require-Text $editor 'toStdString()' 'Non-Windows transfer paths must use JUCE 8 UTF-8 std::string conversion.'
Reject-Text $editor 'toStdWString()' 'JUCE 8.0.12 does not provide juce::String::toStdWString().'

# Notification synthesis and complete-output bypass are audio-thread-safe, fixed-state operations.
Require-Text $tone 'frequencyStart_ * std::pow(frequencyEnd_ / frequencyStart_, fraction)' 'Notification sweep must use the released logarithmic glide.'
Require-Text $tone '0.020 * static_cast<double>(sampleRate)' 'Notification fade envelope must remain 20 ms.'
Require-Text $toneTests '180.0, 400.0, 200.0' 'Low confidence released sweep regression missing.'
Require-Text $toneTests '350.0, 1200.0, 340.0' 'High confidence released sweep regression missing.'
Require-Text $toneTests 'warning.fromHz, 420.0' 'Warning sweep start-frequency regression missing.'
Require-Text $toneTests 'warning.toHz, 180.0' 'Warning sweep end-frequency regression missing.'
Require-Text $toneTests '57600' '1.2 second navigation-sweep timing regression missing at 48 kHz.'
Require-Text $bypass 'static_cast<double>(sampleRate) * 0.010' 'Complete-output bypass must retain the released 10 ms transition.'
Require-Text $bypassTests 'transitionFrames = 480' '48 kHz 10 ms bypass regression missing.'
foreach ($toneRealtime in @($tone, $bypass)) {
    foreach ($forbidden in @('std::mutex', 'lock_guard', 'unique_lock', 'new ', 'make_unique', 'sleep_for', 'Thread::sleep')) {
        Reject-Text $toneRealtime $forbidden ('Tone/bypass realtime helper contains forbidden operation: ' + $forbidden)
    }
}
Require-Text $processorHeader 'void setNonRealtime(bool isNonRealtime) noexcept override;' 'JUCE offline-render state must have an explicit wrapper hook.'
Require-Text $processor 'offlineRendering_.store(offline' 'Offline render mode must publish through an atomic audio-thread gate.'
Require-Text $processor 'return !offlineRendering_.load(std::memory_order_acquire)' 'Generated tones must be suppressed for offline/non-realtime rendering.'
Require-Text $processor 'return navigationTonesAllowed()' 'Status-tone policy must build on the shared generated-tone gate.'
Require-Text $processor 'phase_.load(std::memory_order_acquire) != tonetrace::capture::WorkflowPhase::Frozen' 'Capture/confidence tones must be muted while Frozen.'
Require-Text $processor 'bool ToneTraceProcessor::navigationTonesAllowed() const noexcept' 'Navigation tones need a distinct policy that remains available while Frozen.'
Require-Text $processor 'pendingWarningTone_.store(true' 'Workflow/analysis warnings must cross to the audio thread through one atomic warning request.'
Require-Text $processor 'generatedTone_.active()' 'Confidence threshold chirps must coalesce rather than restart an active generated tone.'
Require-Text $processor 'dryBuffer_.copyFrom' 'Complete-output bypass requires a preallocated dry branch.'
Require-Text $processText 'const int count = std::min(scratchSamples, numSamples - offset);' 'Variable JUCE host block sizes must be processed in bounded preallocated chunks.'
Reject-Text $processText 'dryBuffer_.setSize' 'Dry scratch storage must never resize on the audio thread.'
Require-Text $processor 'outputBypass_.process' 'Bypass must cover correction, separate Correction Gain, and tones in one output stage.'

$controllerProcessStart = $controller.IndexOf('void CorrectionController::process')
$controllerCollectStart = $controller.IndexOf('void CorrectionController::collectRetiredKernels', $controllerProcessStart)
if ($controllerProcessStart -lt 0 -or $controllerCollectStart -le $controllerProcessStart) { throw 'Could not isolate CorrectionController::process.' }
$controllerProcess = $controller.Substring($controllerProcessStart, $controllerCollectStart - $controllerProcessStart)
foreach ($forbidden in @('mutex', 'lock_guard', 'unique_lock', 'new ', 'make_unique', 'sleep', 'serialize', 'MatchEngine')) {
    Reject-Text $controllerProcess $forbidden ('CorrectionController audio process contains forbidden operation: ' + $forbidden)
}
Require-Text $controllerProcess 'core->process' 'Audio controller must delegate only to released HeadlessPluginCore::process.'

$appendStart = $capture.IndexOf('void CaptureBuffer::append')
$appendEnd = $capture.IndexOf('double CaptureBuffer::validSeconds', $appendStart)
if ($appendStart -lt 0 -or $appendEnd -le $appendStart) { throw 'Could not isolate CaptureBuffer::append.' }
$appendText = $capture.Substring($appendStart, $appendEnd - $appendStart)
foreach ($forbidden in @('resize(', 'assign(', 'new ', 'std::mutex', 'lock_guard', 'ScopedLock', 'sleep')) {
    Reject-Text $appendText $forbidden ('Audio-thread capture append contains forbidden operation: ' + $forbidden)
}


# 04a lifecycle regression: same-rate transport suspension and an equivalent
# host state echo must never behave like Reset during Reference/Target capture.
Require-Text $captureHeader 'shouldPreserveCaptureAcrossReprepare' 'Capture lifecycle preservation policy is missing.'
Require-Text $captureHeader 'shouldPreserveCaptureForEquivalentStateEcho' 'Equivalent state-echo preservation policy is missing.'
Require-Text $processor 'shouldPreserveCaptureAcrossReprepare' 'prepareToPlay must use the shared lifecycle preservation policy.'
Require-Text $processor 'shouldPreserveCaptureForEquivalentStateEcho' 'setStateInformation must protect live capture from an equivalent host state echo.'
$echoPos = $processor.IndexOf('shouldPreserveCaptureForEquivalentStateEcho')
$restoreBusyPos = $processor.IndexOf('stateRestoreBusy_.store(true', $echoPos)
if ($echoPos -lt 0 -or $restoreBusyPos -le $echoPos) { throw 'Equivalent state echo must be rejected before state restore mutates live workflow.' }

# Temporary visual-review harness must render the real editor, not a mockup.
# JUCE 8.0.12 Component::createComponentSnapshot() is the supported snapshot path;
# explicitly use SoftwareImageType so the harness never reads a Direct2D-backed
# image while its Graphics context is still active.
Require-Text $cmake 'ToneTraceVisualSnapshots' 'Visual snapshot target missing.'
Require-Text $snapshotMain 'ToneTraceEditor editor(processor);' 'Snapshot tool must instantiate the real editor.'
Require-Text $snapshotMain 'createComponentSnapshot' 'Snapshot tool must use JUCE Component::createComponentSnapshot().'
Require-Text $snapshotMain 'juce::SoftwareImageType{}' 'Snapshot tool must force deterministic software-image readback.'
Reject-Text $snapshotMain 'component.paintEntireComponent' 'Snapshot tool must not reintroduce direct manual painting into a live Image Graphics context.'
Require-Text $snapshotMain 'analyseImageVariation' 'Snapshot tool must reject blank or uniform renders instead of reporting PASS.'
Require-Text $snapshotMain 'g.fillAll(tonetrace::ui::colour::background)' 'Snapshot tool must render transparent dialog space against the Tone Trace background.'
Require-Text $snapshotMain 'UNIFORM_IMAGE_FAILURE' 'Snapshot diagnostics must distinguish uniform-image failure.'
Require-Text $snapshotMain 'PNG_WRITE_FAILURE' 'Snapshot diagnostics must distinguish PNG-write failure.'
Require-Text $snapshotMain 'COMPONENT_FAILURE' 'Snapshot diagnostics must distinguish component failure.'
Require-Text $snapshotMain 'PEER_FAILURE' 'Snapshot diagnostics must distinguish an inconsistent native-peer state.'
Require-Text $snapshotMain 'LAYOUT_FAILURE' 'Snapshot diagnostics must distinguish layout failure.'
Require-Text $snapshotMain 'peer=none-offscreen-supported' 'Snapshot diagnostics must record that a peer is optional for the supported offscreen path.'
Require-Text $snapshotMain 'SNAPSHOT_DIAGNOSTICS.txt' 'Snapshot tool must emit detailed render diagnostics.'
Require-Text $snapshotMain 'Tone Trace 1.1 visual snapshots' 'Snapshot manifests must identify the Tone Trace 1.1 validation output.'
Require-Text $snapshotMain 'minimumWidth' 'Snapshot tool must cover minimum editor size.'
Require-Text $snapshotMain 'defaultWidth' 'Snapshot tool must cover default editor size.'
Require-Text $snapshotMain 'maximumWidth' 'Snapshot tool must cover maximum editor size.'
Require-Text $snapshotMain 'createOptionsSnapshotComponent' 'Snapshot tool must include Options content.'
Require-Text $snapshotMain 'inspectFocusableChildren' 'Snapshot tool must inspect visible keyboard-focusable layout.'
Require-Text $snapshotMain 'getExplicitFocusOrder' 'Snapshot layout manifest must record explicit focus order.'
Require-Text $snapshotMain 'overlapping focusable controls' 'Snapshot layout audit must detect focusable-control overlap.'
Require-Text $snapshotMain 'UI_FOCUS_LAYOUT_MANIFEST.txt' 'Snapshot tool must emit a machine-readable focus/layout manifest.'
Require-Text $editor 'Capture a Reference. Learn a Target. Refine the correction.' 'Editor workflow guidance is missing.'

# Released dynamic-band paging contract. The JUCE editor consumes this helper
# directly; do not regress to a one-page layout.
Require-Text $bandPages 'constexpr int preferredMaximum = 10;' 'Band pages must never exceed the released ten-band preference.'
Require-Text $bandPages 'const int pageCount = (bandCount + capacity - 1) / capacity;' 'Band page count must derive from capped page capacity.'
Require-Text $bandPageTests 'expect(30, 30, {10, 10, 10});' '30-band 10/10/10 paging regression is missing.'
Require-Text $bandPageTests 'expect(31, 30, {8, 8, 8, 7});' 'Balanced odd-resolution paging regression is missing.'

Write-Host 'PASS: Tone Trace 1.1 source contract satisfied - JUCE radio-group page selector, one page-selection state, band/page navigation tones, Frozen navigation policy, processor state round-trip, released DSP, bypass, transfers, visual snapshots, and realtime guards present.'
