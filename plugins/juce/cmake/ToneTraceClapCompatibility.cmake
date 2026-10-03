# Keep the pinned adapter untouched. Generate a wrapper with the small host
# compatibility changes needed by the released Tone Trace CLAP contract.
file(READ "${CLAP_JUCE_EXTENSIONS_DIR}/src/wrapper/clap-juce-wrapper.cpp" wrapper)
function(tonetrace_patch_clap before after)
  string(FIND "${wrapper}" "${before}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "Pinned CLAP adapter changed: compatibility patch no longer applies")
  endif()

  # Every compatibility edit is intentionally anchored to one exact block in
  # the pinned adapter. Refuse to configure if an upstream/source-layout change
  # makes an anchor ambiguous instead of silently replacing multiple sites.
  string(LENGTH "${before}" before_length)
  math(EXPR remainder_start "${position} + ${before_length}")
  string(SUBSTRING "${wrapper}" ${remainder_start} -1 remainder)
  string(FIND "${remainder}" "${before}" second_position)
  if(NOT second_position EQUAL -1)
    message(FATAL_ERROR "Pinned CLAP adapter changed: compatibility patch anchor is no longer unique")
  endif()

  string(REPLACE "${before}" "${after}" wrapper "${wrapper}")
  set(wrapper "${wrapper}" PARENT_SCOPE)
endfunction()
tonetrace_patch_clap("#include <clap-juce-extensions/clap-juce-extensions.h>"
  "#include <clap-juce-extensions/clap-juce-extensions.h>\n#include \"ToneTraceProductCatalog.h\"\nbool toneTraceRestoreClapState(juce::AudioProcessor&, const void*, int);")
tonetrace_patch_clap("auto clapID = static_cast<uint32_t>(juceParamID.hashCode());"
  "for (const auto& spec : tonetrace::product::kReleasedParameters)\n            if (juceParamID == spec.key) return spec.releasedNumericId;\n        auto clapID = static_cast<uint32_t>(juceParamID.hashCode());")
tonetrace_patch_clap("auto *cpc = paramVariant.clapExtParameter;"
  "if (const auto* spec = tonetrace::product::findReleasedParameterById(paramID))\n        {\n            if (spec->readOnly) info->flags |= CLAP_PARAM_IS_READONLY;\n            if (spec->stepped) info->flags |= CLAP_PARAM_IS_STEPPED;\n        }\n        auto *cpc = paramVariant.clapExtParameter;")
tonetrace_patch_clap("if (written < 0)" "if (written <= 0 || static_cast<uint64_t>(written) > sz - sofar)")
tonetrace_patch_clap("chunkMemory.append(block, (size_t)rd);"
  "{\n                if (rd > blockSize || chunkMemory.getSize() + static_cast<size_t>(rd) > 32 * 1024 * 1024) return false;\n                chunkMemory.append(block, (size_t)rd);\n                }")
tonetrace_patch_clap("processor->setStateInformation(chunkMemory.getData(), (int)chunkMemory.getSize());\n        chunkMemory.reset();\n        return true;"
  "const bool restored = toneTraceRestoreClapState(*processor, chunkMemory.getData(), (int)chunkMemory.getSize());\n        chunkMemory.reset();\n        if (restored && _host.canUseParams()) _host.paramsRescan(CLAP_PARAM_RESCAN_VALUES | CLAP_PARAM_RESCAN_TEXT);\n        return restored;")
tonetrace_patch_clap("auto pbi = paramPtrByClapID[paramId];\n        value = (double)getNormalisedParameterValue(pbi, (float)value);"
  "for (const auto& control : tonetrace::product::kImplementedContinuousControls)\n            if (control.parameter->releasedNumericId == paramId)\n            {\n                if (size == 0 || !std::isfinite(value)) return false;\n                const auto text = tonetrace::product::formatContinuous(control, value, true);\n                snprintf(display, size, \"%s\", text.c_str());\n                return true;\n            }\n        auto pbi = paramPtrByClapID[paramId];\n        value = (double)getNormalisedParameterValue(pbi, (float)value);")
tonetrace_patch_clap("auto pbi = paramPtrByClapID[paramId];\n        *value = (double)getUnNormalisedParameterValue("
  "for (const auto& control : tonetrace::product::kImplementedContinuousControls)\n            if (control.parameter->releasedNumericId == paramId)\n                return tonetrace::product::parseContinuous(control, display, *value);\n        auto pbi = paramPtrByClapID[paramId];\n        *value = (double)getUnNormalisedParameterValue(")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ToneTraceClapWrapper.cpp" "${wrapper}")
set_property(TARGET clap_juce_sources PROPERTY INTERFACE_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/ToneTraceClapWrapper.cpp")
