file (GLOB_RECURSE IMP_CC_FILES ${CMAKE_SOURCE_DIR}/src/*.cc)

file (GLOB_RECURSE ALL_SRC_FILES *.hh *.cc)

add_custom_target (format "clang-format" -i ${ALL_SRC_FILES} COMMENT "Formatting source code...")

foreach (tidy_target ${ALL_SRC_FILES})
  get_filename_component (basename ${tidy_target} NAME)
  get_filename_component (dirname ${tidy_target} DIRECTORY)
  get_filename_component (basedir ${dirname} NAME)
  set (tidy_target_name "${basedir}__${basename}")
  set (tidy_command clang-tidy-20 --quiet -header-filter=.* -p=${PROJECT_BINARY_DIR} ${tidy_target})
  add_custom_target (tidy_${tidy_target_name} ${tidy_command})
  list (APPEND ALL_TIDY_TARGETS tidy_${tidy_target_name})

  if (${tidy_target} IN_LIST IMP_CC_FILES)
    list (APPEND IMP_TIDY_TARGETS tidy_${tidy_target_name})
  endif ()
endforeach (tidy_target)

add_custom_target (tidy DEPENDS ${IMP_TIDY_TARGETS})

add_custom_target (tidy-all DEPENDS ${ALL_TIDY_TARGETS})
