# Scope size optimization to this DLL, including when embedded in the player.
# Keep exception handling, security checks and the legacy runtime unchanged.
set(TTPBROWSER_INLINE_LEVEL "2" CACHE STRING "Release inlining level (0, 1 or 2)")
set_property(CACHE TTPBROWSER_INLINE_LEVEL PROPERTY STRINGS 0 1 2)
if(NOT TTPBROWSER_INLINE_LEVEL MATCHES "^[012]$")
  message(FATAL_ERROR "TTPBROWSER_INLINE_LEVEL must be 0, 1 or 2")
endif()
function(ttpbrowser_optimize_size target)
  set_target_properties(${target} PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
  target_compile_options(${target} PRIVATE
    "$<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:CXX>>:/O1;/Ob${TTPBROWSER_INLINE_LEVEL};/Os;/Gy;/Gw;/GF;/GR->")
  target_link_options(${target} PRIVATE
    "$<$<CONFIG:Release>:/OPT:REF;/OPT:ICF;/INCREMENTAL:NO;/DEBUG:NONE>")
endfunction()
