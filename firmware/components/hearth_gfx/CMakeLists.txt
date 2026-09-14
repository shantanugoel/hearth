idf_component_register(
    SRCS "hearth_canvas.cc"
    INCLUDE_DIRS "include" "font"
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
