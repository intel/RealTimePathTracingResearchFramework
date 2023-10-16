# v1.0.0

Note: This is a public pre-release limited to the core components
required for reproducing some of our research papers.

- Path tracer variants for multiple styles of Vulkan RT usage,
  stack-/tail-recursive, iterative, megakernel(s) inline and asyc
- GLTF BSDF with normal maps, PBR specular and transmission layers
- basic indirect ray differentials and mipmapping
- multiple RNG variants
- basic temporal accumulation & TAA resolve
- basic support for triangle emitters
- composable renderer architecture for testing many feature
  combinations across many implementation variants
- auto-serialized ImGui and application state, persistent state on
  application restarts, extensive pre-configuration+automation options
- arena-based, block-fused GPU memory allocations
