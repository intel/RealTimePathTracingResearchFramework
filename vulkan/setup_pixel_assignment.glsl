// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef SETUP_PIXEL_ASSIGNMENT
#define SETUP_PIXEL_ASSIGNMENT

#ifndef WORKGROUP_SIZE_X

#define gl_GlobalInvocationID gl_LaunchIDEXT
// note: these are not actually consecutive for consecutive subgroup invocations,
// this could be enforced with the subgroup extension if needed
#define gl_GlobalInvocationIndex (gl_LaunchSizeEXT.x * gl_LaunchIDEXT.x + gl_LaunchIDEXT.x)

#define gl_GlobalInvocationLayer gl_LaunchIDEXT.z

#else

#define gl_GlobalInvocationID uvec3( (gl_GlobalInvocationID.xy & uvec2(~0x18u, ~0x3)) \
    + uvec2((gl_GlobalInvocationID.y & 0x3) << 3, (gl_GlobalInvocationID.x & 0x18u) >> 3) \
    , gl_GlobalInvocationID.z)
#define gl_GlobalInvocationIndex (gl_LocalInvocationIndex + \
        + (gl_WorkGroupID.x + gl_WorkGroupID.y * gl_NumWorkGroups.x) * gl_WorkGroupSize.x * gl_WorkGroupSize.y)

#define gl_GlobalInvocationLayer gl_GlobalInvocationID.z

#endif

#endif
