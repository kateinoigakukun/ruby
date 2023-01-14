set(UNICODE_VERSION "15.0.0")
string(REGEX REPLACE "\\.0$" "" UNICODE_EMOJI_VERSION ${UNICODE_VERSION})
set(UNICODE_HDR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${UNICODE_VERSION}")
set(UNICODE_SRC_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data/${UNICODE_VERSION}/ucd")
set(UNICODE_SRC_EMOJI_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data/emoji/${UNICODE_EMOJI_VERSION}")
set(UNICODE_BETA NO)

set(UNICODE_DOWNLOADER
  "${BASERUBY}" "${RUBY_TOOLDIR}/downloader.rb"
  "--cache-dir=${RUBY_DOWNLOAD_CACHE_DIR}"
  --exist --always unicode --unicode-beta=${UNICODE_BETA}
)

set(UNICODE_FILES
  "${UNICODE_SRC_DATA_DIR}/UnicodeData.txt"
  "${UNICODE_SRC_DATA_DIR}/CompositionExclusions.txt"
  "${UNICODE_SRC_DATA_DIR}/NormalizationTest.txt"
  "${UNICODE_SRC_DATA_DIR}/CaseFolding.txt"
  "${UNICODE_SRC_DATA_DIR}/SpecialCasing.txt"
)

set(UNICODE_PROPERTY_FILES
  "${UNICODE_SRC_DATA_DIR}/Blocks.txt"
  "${UNICODE_SRC_DATA_DIR}/DerivedAge.txt"
  "${UNICODE_SRC_DATA_DIR}/DerivedCoreProperties.txt"
  "${UNICODE_SRC_DATA_DIR}/PropList.txt"
  "${UNICODE_SRC_DATA_DIR}/PropertyAliases.txt"
  "${UNICODE_SRC_DATA_DIR}/PropertyValueAliases.txt"
  "${UNICODE_SRC_DATA_DIR}/Scripts.txt"
)

set(UNICODE_AUXILIARY_FILES
  "${UNICODE_SRC_DATA_DIR}/auxiliary/GraphemeBreakProperty.txt"
  "${UNICODE_SRC_DATA_DIR}/auxiliary/GraphemeBreakTest.txt"
)

set(UNICODE_UCD_EMOJI_FILES
  "${UNICODE_SRC_DATA_DIR}/emoji/emoji-data.txt"
  "${UNICODE_SRC_DATA_DIR}/emoji/emoji-variation-sequences.txt"
)

set(UNICODE_EMOJI_FILES
  "${UNICODE_SRC_EMOJI_DATA_DIR}/emoji-sequences.txt"
  "${UNICODE_SRC_EMOJI_DATA_DIR}/emoji-test.txt"
  "${UNICODE_SRC_EMOJI_DATA_DIR}/emoji-zwj-sequences.txt"
)

add_custom_command(
  OUTPUT ${UNICODE_FILES} ${UNICODE_PROPERTY_FILES}
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${UNICODE_SRC_DATA_DIR}"
  COMMAND ${UNICODE_DOWNLOADER}
    -d "${UNICODE_SRC_DATA_DIR}" -p "${UNICODE_VERSION}/ucd"
    ${UNICODE_FILES} ${UNICODE_PROPERTY_FILES}
  COMMENT "Downloading Unicode ${UNICODE_VERSION} data and property files..."
)

add_custom_command(
  OUTPUT ${UNICODE_AUXILIARY_FILES}
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${UNICODE_SRC_DATA_DIR}/auxiliary"
  COMMAND ${UNICODE_DOWNLOADER} -d "${UNICODE_SRC_DATA_DIR}/auxiliary" -p "${UNICODE_VERSION}/ucd/auxiliary" ${UNICODE_AUXILIARY_FILES}
  COMMENT "Downloading Unicode ${UNICODE_VERSION} auxiliary files..."
)

add_custom_command(
  OUTPUT ${UNICODE_UCD_EMOJI_FILES}
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${UNICODE_SRC_DATA_DIR}/emoji"
  COMMAND ${UNICODE_DOWNLOADER} -d "${UNICODE_SRC_DATA_DIR}/emoji" -p "${UNICODE_VERSION}/ucd/emoji" ${UNICODE_UCD_EMOJI_FILES}
  COMMENT "Downloading Unicode UCD emoji ${UNICODE_EMOJI_VERSION} files..."
)

add_custom_command(
  OUTPUT ${UNICODE_EMOJI_FILES}
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${UNICODE_SRC_EMOJI_DATA_DIR}"
  COMMAND ${UNICODE_DOWNLOADER} -d "${UNICODE_SRC_EMOJI_DATA_DIR}" -p "emoji/${UNICODE_EMOJI_VERSION}" ${UNICODE_EMOJI_FILES}
  COMMENT "Downloading Unicode UCD emoji ${UNICODE_EMOJI_VERSION} files..."
)

add_custom_target(unicode-up-casefold_h
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${UNICODE_HDR_DIR}"
  COMMAND "${BASERUBY}" "${RUBY_TOOLDIR}/enc-case-folding.rb"
    "--output-file=${UNICODE_HDR_DIR}/casefold.h"
    "--mapping-data-directory=${UNICODE_SRC_DATA_DIR}"
  DEPENDS
    "${UNICODE_SRC_DATA_DIR}/UnicodeData.txt"
    "${UNICODE_SRC_DATA_DIR}/SpecialCasing.txt"
    "${UNICODE_SRC_DATA_DIR}/CaseFolding.txt"
)

add_custom_target(unicode-up-name2ctype_h
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${UNICODE_HDR_DIR}"
  COMMAND ${BOOTSTRAPRUBY} "${RUBY_TOOLDIR}/enc-unicode.rb"
    --header "${UNICODE_SRC_DATA_DIR}" "${UNICODE_SRC_EMOJI_DATA_DIR}"
    > "${UNICODE_HDR_DIR}/name2ctype.h"
  DEPENDS
    "${UNICODE_SRC_DATA_DIR}/UnicodeData.txt"
    ${UNICODE_AUXILIARY_FILES}
    ${UNICODE_PROPERTY_FILES}
    ${UNICODE_UCD_EMOJI_FILES}
    ${UNICODE_EMOJI_FILES}
)
add_custom_target(unicode-up)
add_dependencies(unicode-up
  unicode-up-casefold_h unicode-up-name2ctype_h)
