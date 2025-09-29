# shaders/manifest.cmake
# Explicitly register shaders that can't be guessed by name.
cg_add_hlsl(FILE ThermalOutflowCS.hlsl STAGE compute ENTRY CSMain)
cg_add_hlsl(FILE ThermalApplyCS.hlsl   STAGE compute ENTRY CSMain)

# Example for a pixel shader with a nonstandard name:
# cg_add_hlsl(FILE post/water_tiny.hlsl STAGE pixel ENTRY PSMain OUTPUT water_tiny_ps)
# Example for a vertex shader:
# cg_add_hlsl(FILE terrain/terrain_splat_example.hlsl STAGE vertex ENTRY VSMain OUTPUT terrain_splat_vs)
