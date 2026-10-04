if(CONFIG_ZMK_LAYER_ART)
  # Reuse the stock nice!view widget helpers (battery icon, canvas rotation)
  set(NICE_VIEW_WIDGETS ${CMAKE_SOURCE_DIR}/boards/shields/nice_view/widgets)

  zephyr_library()
  zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include ${NICE_VIEW_WIDGETS})
  zephyr_library_sources(
    layer_art.c
    images.c
    ${NICE_VIEW_WIDGETS}/util.c
    ${NICE_VIEW_WIDGETS}/bolt.c
  )
endif()
