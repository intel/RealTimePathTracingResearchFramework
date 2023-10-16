// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef GPU_PROGRAMS_EMBEDDED
#define GPU_PROGRAMS_EMBEDDED

struct GpuModuleDefine {
    char const* name;
    char const* value;
};

struct GpuModuleOption {
    char const* name;
    char const* const* values;
};

enum GpuProgramFeatures {
    GPU_PROGRAM_FEATURE_MEGAKERNEL = 0x1,
    GPU_PROGRAM_FEATURE_EXTENDED_HIT = 0x2,
};
struct GpuModuleUnit {
    char const* id;
    char const* name;
    char const* type;
    char const* srcpath;
    char const* cmdpath;
    char const* cachepath;
    char const* source_to_build_path;
    struct GpuModuleDefine const* defines;
    unsigned feature_flags;
};

struct GpuModule {
    char const* id;
    char const* name;
    char const* type; // e.g. rgen, rhit, rmiss, comp, rast
    struct GpuModuleUnit const* const* units;
    unsigned feature_flags;
};

enum GpuProgramType {
    GPU_PROGRAM_TYPE_COMPUTE,
    GPU_PROGRAM_TYPE_RAYTRACING,
    GPU_PROGRAM_TYPE_RASTERIZATION,
    GPU_PROGRAM_TYPE_MODULE
};
struct GpuProgram {
    char const* id;
    char const* name;
    enum GpuProgramType type;
    struct GpuModule const* const* modules;
    unsigned feature_flags;
};

/* @GPU_EMBEDDED_ASTERISK@/
#cmakedefine CMAKE_GENERATE_GPU_PROGRAM_LIST
#cmakedefine CMAKE_GENERATE_GPU_PROGRAM
#cmakedefine CMAKE_GENERATE_GPU_MODULE
#cmakedefine CMAKE_GENERATE_GPU_MODULE_UNIT
/@GPU_EMBEDDED_ASTERISK@ */


#ifdef CMAKE_GENERATE_GPU_PROGRAM_LIST

// reference programs
extern struct GpuProgram const @GPU_EMBEDDED_PROGRAMS@ NULL_ID;

// todo: want some @GPU_EMBEDDED_PROGRAMS_PREFIX@ in all module definition variables!
struct GpuProgram const* const @GPU_EMBEDDED_PROGRAMS_PREFIX@_gpu_programs[] = {
      @GPU_EMBEDDED_PROGRAM_POINTERS@ 0
};
struct GpuProgram const* const @GPU_EMBEDDED_PROGRAMS_PREFIX@_integrators[] = {
      @GPU_EMBEDDED_INTEGRATOR_POINTERS@ 0
};
struct GpuProgram const* const @GPU_EMBEDDED_PROGRAMS_PREFIX@_computational_raytracers[] = {
      @GPU_EMBEDDED_COMPUTATIONAL_RAYTRACER_POINTERS@ 0
};
struct GpuProgram const* const @GPU_EMBEDDED_PROGRAMS_PREFIX@_raytracers[] = {
      @GPU_EMBEDDED_INTEGRATOR_POINTERS@
      @GPU_EMBEDDED_COMPUTATIONAL_RAYTRACER_POINTERS@
      0
};

#endif


#ifdef CMAKE_GENERATE_GPU_PROGRAM

// programs reference modules
extern struct GpuModule const @GPU_EMBEDDED_PROGRAM_MODULES@ NULL_ID;

struct GpuModule const* const @program_namespace_@@GPU_EMBEDDED_PROGRAM_ID@_modules[] = {
      @GPU_EMBEDDED_PROGRAM_MODULE_POINTERS@ 0
};

struct GpuProgram const @program_namespace_@@GPU_EMBEDDED_PROGRAM_ID@ = {
      "@GPU_EMBEDDED_PROGRAM_ID@"
    , "@GPU_EMBEDDED_PROGRAM_NAME@"
    , GPU_PROGRAM_TYPE_@GPU_EMBEDDED_PROGRAM_TYPE@
    , @program_namespace_@@GPU_EMBEDDED_PROGRAM_ID@_modules
    , @GPU_EMBEDDED_PROGRAM_FEATURE_FLAGS@ 0
};

#endif


#ifdef CMAKE_GENERATE_GPU_MODULE

// modules reference objects
extern struct GpuModuleUnit const @GPU_EMBEDDED_MODULE_OBJECTS@ NULL_ID;

struct GpuModuleUnit const* const @program_namespace_@@GPU_EMBEDDED_MODULE_ID@_objects[] = {
      @GPU_EMBEDDED_MODULE_OBJECT_POINTERS@ 0
};

struct GpuModule const @program_namespace_@@GPU_EMBEDDED_MODULE_ID@ = {
      "@GPU_EMBEDDED_MODULE_ID@"
    , "@GPU_EMBEDDED_MODULE_NAME@"
    , "@GPU_EMBEDDED_MODULE_TYPE@"
    , @program_namespace_@@GPU_EMBEDDED_MODULE_ID@_objects
    , @GPU_EMBEDDED_MODULE_FEATURE_FLAGS@ 0
};

#endif


#ifdef CMAKE_GENERATE_GPU_MODULE_UNIT

// objects describe binaries and their sources
struct GpuModuleDefine const @program_namespace_@@GPU_EMBEDDED_OBJECT_ID@_defines[] = {
      @GPU_EMBEDDED_OBJECT_DEFINES@ 0 // { "name", "value" }, ...
};

struct GpuModuleUnit const @program_namespace_@@GPU_EMBEDDED_OBJECT_ID@ = {
      "@GPU_EMBEDDED_OBJECT_ID@"
    , "@GPU_EMBEDDED_OBJECT_NAME@"
    , "@GPU_EMBEDDED_OBJECT_TYPE@"
    , "@GPU_EMBEDDED_OBJECT_SRCPATH@"
    , "@GPU_EMBEDDED_OBJECT_CMDPATH@"
    , "@GPU_EMBEDDED_OBJECT_CACHE_DIR@"
    , "@GPU_EMBEDDED_OBJECT_SOUCE_TO_BUILD_PATH@"
    , @program_namespace_@@GPU_EMBEDDED_OBJECT_ID@_defines
    , @GPU_EMBEDDED_OBJECT_FEATURE_FLAGS@ 0
};

#endif


#endif // GPU_PROGRAMS_EMBEDDED
