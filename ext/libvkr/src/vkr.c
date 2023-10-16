// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "vkr.h"

#ifdef VKR_BUILD_TOOLS

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_FAILURE_STRINGS
#define STB_IMAGE_STATIC // Avoid symbol collisions!
#include "stb_image.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize.h"

#define STB_DXT_IMPLEMENTATION
#include "stb_dxt.h"

// Comment in to dump generated mip levels to mip_level_xx.png.
//#define VKR_VKT_DEBUG_MIP_LEVELS 1
#if defined(VKR_VKT_DEBUG_MIP_LEVELS)
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC // Avoid symbol collisions!
#include "stb_image_write.h"
#endif

#include <meshoptimizer.h>

#endif // VKR_BUILD_TOOLS

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <inttypes.h>

#define VKR_MAGIC_NUMBER 0xABCABC
#define VKR_MIN_VERSION 1
#define VKR_MAX_VERSION 4

// Version 2: Instanced multi-mesh format extension

#define VKR_TEXTURE_MAGIC_NUMBER 0xBC1BC1
#define VKR_MIN_TEXTURE_VERSION 1
#define VKR_MAX_TEXTURE_VERSION 1

#define VKR_TENSOR_MAGIC_NUMBER 0xFE1FE1
#define VKR_MIN_TENSOR_VERSION 1
#define VKR_MAX_TENSOR_VERSION 1

#define VKR_MAX_MIP_LEVELS 31 // Because the maximum resolution ix 0x40000000.

#define VKR_TEXTURE_DIR_POSTFIX                 "_textures"
#define VKR_TEXTURE_EXTENSION                   ".vkt"
#define VKR_TEXTURE_PARAM_EXTENSION             ".txt"
#define VKR_TEXTURE_TENSOR_EXTENSION            ".vktensor"

#define VKR_TEXTURE_NAME_BASE_COLOR                   "BaseColor"
#define VKR_TEXTURE_NAME_NORMAL                       "Normal"
#define VKR_TEXTURE_NAME_SPECULAR_ROUGHNESS_METALNESS "Specular"
#define VKR_TEXTURE_NAME_EMISSION_INTENSITY           "EmissionIntensity"
#define VKR_TEXTURE_NAME_TRANSMISSION                 "SpecularTransmission"
#define VKR_TEXTURE_NAME_EXTENDED                     "Ex"
#define VKR_TEXTURE_NAME_FORMAT_FEATURE               "Feature%u"
#define VKR_TEXTURE_NAME_FORMAT_TENSOR                "Tensor%u"
#define VKR_TEXTURE_NAME_BOUND_FEATURE                sizeof(VKR_TEXTURE_NAME_FORMAT_FEATURE) + 32
#define VKR_TEXTURE_NAME_BOUND_TENSOR                 sizeof(VKR_TEXTURE_NAME_FORMAT_TENSOR) + 32

#define VKR_TEXTURE_1BIT_ALPHA_THRESHOLD 128

float luminance(const float *c)
{
    return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
}

const char *buildTextureDir(const char *sceneFile)
{
  if (!sceneFile) {
    return NULL;
  }

  const char *dot = strrchr(sceneFile, '.');
  const size_t baseLen = dot ? dot-sceneFile : strlen(sceneFile);

  const char *textureDirPostfix = VKR_TEXTURE_DIR_POSTFIX;
  const size_t postfixLen = strlen(textureDirPostfix);
  const size_t totalLen = baseLen + postfixLen + 2;

  char *buf = malloc(totalLen);
  if (buf) {
    char *tgt = buf;

    memcpy(tgt, sceneFile, baseLen);
    tgt += baseLen;

    memcpy(tgt, textureDirPostfix, postfixLen);
    tgt += postfixLen;

    *tgt = '/';
    tgt++;
    *tgt = '\0';
  }

  return buf;
}

const char *strcat4(const char *s1,
                    const char *s2,
                    const char *s3,
                    const char *s4)
{
  if (!s1 || !s2 || !s3 || !s4) {
    return NULL;
  }

  const char *s[] = { s1, s2, s3, s4 };
  size_t l[4];
  size_t totalLen = 1; // For the trailing 0.
  for (int i = 0; i < 4; ++i) {
    l[i] = strlen(s[i]);
    totalLen += l[i];
  }

  char *buf = malloc(totalLen);
  if (buf) {
    char *tgt = buf;
    for (int i = 0; i < 4; ++i) {
      memcpy(tgt, s[i], l[i]);
      tgt += l[i];
    }
    *tgt = '\0';
  }

  return buf;
}

const char *strcat5(const char *s1,
                    const char *s2,
                    const char *s3,
                    const char *s4,
                    const char *s5)
{
  if (!s1 || !s2 || !s3 || !s4 || !s5) {
    return NULL;
  }

  const char *s[] = { s1, s2, s3, s4, s5 };
  size_t l[5];
  size_t totalLen = 1; // For the trailing 0.
  for (int i = 0; i < 5; ++i) {
    l[i] = strlen(s[i]);
    totalLen += l[i];
  }

  char *buf = malloc(totalLen);
  if (buf) {
    char *tgt = buf;
    for (int i = 0; i < 5; ++i) {
      memcpy(tgt, s[i], l[i]);
      tgt += l[i];
    }
    *tgt = '\0';
  }

  return buf;
}

VkrResult reportError(VkrErrorHandler eh,
                      VkrResult result,
                      const char *fmt,
                      ...)
{
  if (eh) {
    va_list args1;
    va_list args2;
    va_start(args1, fmt);
    va_copy(args2, args1);
    const int len = vsnprintf(NULL, 0, fmt, args1);
    va_end(args1);
    char *buf = (char *) malloc(len+1);
    if (buf) {
      vsnprintf(buf, len+1, fmt, args2);
      eh(result, buf);
      free(buf);
    } else {
      eh(result, "Unknown error");
    }
    va_end(args2);
  }
  return result;
}

uint64_t vkr_get_transform_offset(
    uint32_t transformIndex,
    uint64_t numStaticTransforms,
    uint64_t numAnimatedTransforms,
    uint64_t frameIndex)
{
  if (transformIndex < numStaticTransforms) {
    return transformIndex;
  }
  return numStaticTransforms
       + (transformIndex - numStaticTransforms)
       + frameIndex * numAnimatedTransforms;
}

VkrResult vkr_open_texture(
    const char *filename,
    VkrTexture *t,
    VkrErrorHandler eh)
{
  if (!t || !filename) {
    return reportError(eh, VKR_INVALID_ARGUMENT,
        "Invalid argument to vkr_open_texture");
  }
  memset(t, 0, sizeof(VkrTexture));

  const size_t fnSize = strlen(filename)+1;
  t->filename = malloc(fnSize);

  if (!t->filename) {
    return reportError(eh, VKR_ALLOCATION_ERROR,
        "Failed to allocate texture filename.");
  }
  memcpy((char*)t->filename, filename, fnSize);

  FILE *f = fopen(t->filename, "rb");
  if (!f) {
    vkr_close_texture(t);
    return VKR_INVALID_FILE_NAME;
  }

  int32_t magic = 0;
  if ((fread(&magic, sizeof(int32_t), 1, f) != 1)
   || (magic != VKR_TEXTURE_MAGIC_NUMBER))
  {
    vkr_close_texture(t);
    fclose(f);
    return reportError(eh, VKR_INVALID_FILE_FORMAT,
        "%s is not a " VKR_TEXTURE_EXTENSION " file.", filename);
  }

  if ((fread(&t->version, sizeof(int32_t), 1, f) != 1)
   || (t->version < VKR_MIN_TEXTURE_VERSION)
   || (t->version > VKR_MAX_TEXTURE_VERSION))
  {
    const int32_t v = t->version;
    vkr_close_texture(t);
    fclose(f);
    return reportError(eh, VKR_INVALID_FILE_FORMAT,
        "Unsupported file version %d in %s\n", v, filename);
  }

  if ((fread(&t->numMipLevels, sizeof(int32_t), 1, f) != 1)
   || (fread(&t->width,     sizeof(int32_t), 1, f) != 1)
   || (fread(&t->height,    sizeof(int32_t), 1, f) != 1)
   || (fread(&t->format,    sizeof(int32_t), 1, f) != 1)
   || (fread(&t->dataSize,  sizeof(uint64_t), 1, f) != 1))
  {
    vkr_close_texture(t);
    fclose(f);
    return reportError(eh, VKR_INVALID_FILE_FORMAT,
        "Failed to read texture file header.");
  }

  VkrMipLevel *mipLevels = NULL;
  if (t->numMipLevels > 0) {
    mipLevels = (VkrMipLevel *) calloc(t->numMipLevels, sizeof(VkrMipLevel));
    if (!mipLevels) {
      vkr_close_texture(t);
      fclose(f);
      return reportError(eh, VKR_ALLOCATION_ERROR,
          "Failed to allocate mip level array.");
    }
    t->mipLevels = mipLevels;
  }

  for (int32_t i = 0; i < t->numMipLevels; ++i) {
    VkrMipLevel *l = mipLevels + i;
    if ((fread(&l->width, sizeof(int32_t), 1, f) != 1)
     || (fread(&l->height, sizeof(int32_t), 1, f) != 1)
     || (fread(&l->dataSize, sizeof(uint64_t), 1, f) != 1)
     || (fread(&l->dataOffset, sizeof(int64_t), 1, f) != 1))
    {
      vkr_close_texture(t);
      fclose(f);
      return reportError(eh, VKR_INVALID_FILE_FORMAT,
          "Failed to read mip level header.");
    }
  }

  t->dataOffset = ftell(f);
  fclose(f);

  if (t->dataOffset < 0) {
    vkr_close_texture(t);
    return reportError(eh, VKR_INVALID_FILE_FORMAT,
        "Texture file I/O error.");
  }

  return VKR_SUCCESS;
}

void vkr_close_texture(VkrTexture *t)
{
  if (t) {
    free((void *)t->filename);
    free((void *)t->mipLevels);
    memset(t, 0, sizeof(VkrTexture));
  }
}

VkrResult vkr_load_string(const char** target, FILE* f,
    char const* property_name, const char *filename, VkrErrorHandler eh)
{
  if (!property_name) {
    *target = (char *) calloc(1, 1);
    return VKR_SUCCESS;
  }

  uint64_t len = 0;
  if (fread(&len, sizeof(uint64_t), 1, f) != 1) {
    return reportError(eh, VKR_INVALID_FILE_FORMAT,
        "Failed to read %s string length from %s.",
        property_name, filename);
  }

  char *name = (char *) malloc(len+1);
  if (!name) {
    return reportError(eh, VKR_ALLOCATION_ERROR,
        "Failed to allocate string %s of length %" PRIu64 ".", property_name,
        len+1);
  }

  *target = (const char *)name;
  if (fread(name, sizeof(char), len+1, f) != len+1) {
    return reportError(eh, VKR_INVALID_FILE_FORMAT,
        "Failed to read %s string from %s.",
        property_name, filename);
  }

  return VKR_SUCCESS;
}

VkrResult vkr_read_text_file(const char *filename, char **content,
    VkrErrorHandler eh)
{
  FILE* f = fopen(filename, "r");
  if (!f) {
    return VKR_INVALID_FILE_NAME;
  }

  const size_t blockSize = 1024;
  size_t bufSize = blockSize;
  char *buf = NULL;

  size_t totalBytesRead = 0;
  while (!ferror(f) && !feof(f))
  {
    bufSize += blockSize;

    char *newBuf = (char *)malloc(bufSize);
    if (!newBuf) {
      if (buf)
        free(buf);
      fclose(f);
      return reportError(eh, VKR_ALLOCATION_ERROR,
        "Failed to allocate buffer for %s", filename);
    }
    if (buf) {
      memcpy(newBuf, buf, totalBytesRead);
      free(buf);
    }
    buf = newBuf;

    /* Leave 1 byte at the end for \0. */
    totalBytesRead += fread(buf + totalBytesRead, 1, blockSize-1, f);
    if (ferror(f)) {
      free(buf);
      return reportError(eh, VKR_INVALID_FILE_FORMAT,
        "Failed to read %s", filename);
    }
  }

  fclose(f);

  buf[totalBytesRead] = '\0';
  *content = buf;
  return VKR_SUCCESS;
}

/*
 * The material parameter file format is very simple. It's a text file where
 * each line contains a single (float) value.
 *
 * There cannot be multiple values per line, or any additional characters that
 * are not part of the value.
 *
 * There should be any additional whitespace or empty lines.
 *
 * This format is designed such that it can be parsed with (f)scanf("%f%f%f"),
 * exploiting how scanf will eat whitespace quite greedily. However, note
 * that scanf will not check the additional constraints we impose.
 * For example, a file with the string "1, 2, 3" will be parsed by scanf as
 * a single float with the value 1.
 */
VkrResult vkr_parse_material_param_file(
    const char *filename,
    size_t *numValues,
    float *values,
    VkrErrorHandler eh)
{
  if (!filename || !numValues || *numValues == 0 || !values)
  {
    return reportError(eh, VKR_INVALID_ARGUMENT,
        "Invalid argument to vkr_load_material_param");
  }

  FILE* f = fopen(filename, "r");
  if (!f)
    return VKR_INVALID_FILE_NAME;

  size_t maxNumValues = *numValues;
  size_t i = 0;
  for (; i < maxNumValues; ++i) {
    if (1 != fscanf(f, "%f", &values[i])) {
      if (feof(f))
        break;
      else {
        fclose(f);
        return reportError(eh, VKR_INVALID_FILE_FORMAT,
          "Invalid number format in vkr_parse_material_param_file");
      }
    }

    int validation_delim = fgetc(f);
    if (validation_delim == '\r')
      validation_delim = fgetc(f);
    if (validation_delim != '\n' && validation_delim != EOF) {
      fclose(f);
      return reportError(eh, VKR_INVALID_FILE_FORMAT,
        "Values should be delimited by newlines in vkr_parse_material_param_file");
    }
  }
  *numValues = i;

  fclose(f);
  return VKR_SUCCESS;
}

VkrResult vkr_load_material_param(const char *textureDir,
    const char *materialName, const char *paramName, size_t *numComponents,
    float *v, VkrErrorHandler eh)
{
  const char *filename = strcat5(textureDir, materialName, "_", paramName,
    VKR_TEXTURE_PARAM_EXTENSION);

  const VkrResult r = vkr_parse_material_param_file(filename, numComponents,
      v, eh);
  free((void*)filename);

  if (r == VKR_INVALID_FILE_NAME)
    *numComponents = 0;
  else if (r != VKR_SUCCESS)
    return r;

  return VKR_SUCCESS;
}

VkrResult vkr_load_material_texture(const char *textureDir,
    const char *materialName, const char *textureName,
    VkrTexture *texture, VkrErrorHandler eh)
{
  const char *filename = strcat5(textureDir, materialName, "_",
      textureName, VKR_TEXTURE_EXTENSION);

  const VkrResult r = vkr_open_texture(filename, texture, eh);
  free((void *)filename);

  if (r != VKR_SUCCESS && r != VKR_INVALID_FILE_NAME)
    return r;

  return VKR_SUCCESS;
}

VkrResult vkr_load_material_tensor(const char *textureDir,
    const char *materialName, const char *tensorName,
    VkrTensor *tensor, VkrErrorHandler eh)
{
  const char *filename = strcat5(textureDir, materialName, "_",
      tensorName, VKR_TEXTURE_TENSOR_EXTENSION);

  const VkrResult r = vkr_open_tensor(filename, tensor, eh);
  free((void *)filename);

  if (r != VKR_SUCCESS && r != VKR_INVALID_FILE_NAME)
    return r;

  return VKR_SUCCESS;
}

void vkr_initialize_material_defaults(VkrMaterial *material)
{
  material->emitterBaseColor[0] = 0.f;
  material->emitterBaseColor[1] = 0.f;
  material->emitterBaseColor[2] = 0.f;
  material->emissionIntensity = 0.f;
  material->specularTransmission = 0.f;
  material->iorEta = 1.5f;
  material->iorK = 0.f;
  material->translucency = 0.f;
}

/*
 * material->name must be set.
 */
VkrResult vkr_load_material(const char *textureDir, VkrMaterial *material,
    VkrErrorHandler eh)
{
  vkr_initialize_material_defaults(material);

  {
    const char *filename = strcat5(textureDir, material->name, "_", VKR_TEXTURE_NAME_EXTENDED, ".txt");
    char* extended_name = NULL;
    const VkrResult r = vkr_read_text_file(filename, &extended_name, eh);
    free((void*)filename);
    material->extended_name = extended_name;
    if (r == VKR_INVALID_FILE_NAME)
      material->extended_name = material->name;
    else if (r != VKR_SUCCESS)
      return r;
  }
  int is_extended_material = (material->extended_name != material->name)
    || strstr(material->name, "_SHADER")
    || strstr(material->name, "_EX");

  {
    float emissionValues[4] = { material->emissionIntensity, material->emitterBaseColor[0], material->emitterBaseColor[1], material->emitterBaseColor[2] };
    size_t numValues = 4;
    const VkrResult r = vkr_load_material_param(textureDir, material->name,
          VKR_TEXTURE_NAME_EMISSION_INTENSITY, &numValues, emissionValues, eh);
    if (r != VKR_SUCCESS)
      return r;
    if (numValues == 1) {
      // could accept this as is with default unspecified base color
      // legacy: try to read base color from separate file
      size_t numColorValues = 3;
      const VkrResult r = vkr_load_material_param(textureDir, material->name,
          VKR_TEXTURE_NAME_BASE_COLOR, &numColorValues, &emissionValues[1], eh);
      if (r != VKR_SUCCESS)
        return r;
      // accept default base color or 3-component base color
      if (numColorValues != 0 && numColorValues != 3)
        return reportError(eh, VKR_INVALID_FILE_FORMAT,
          "Three color components expected for emission base color");
    }
    // accept default zero emission or 4-component emission
    else if (numValues != 0 && numValues != 4)
      return reportError(eh, VKR_INVALID_FILE_FORMAT,
          "One or four components expected for emission intensity + base color");

    material->emissionIntensity = emissionValues[0];
    memcpy(material->emitterBaseColor, &emissionValues[1], sizeof(material->emitterBaseColor));
  }

  {
    float transmissionIorEtaKTranslucency[4] = { material->specularTransmission, material->iorEta, material->iorK, material->translucency };
    size_t numValues = 4;
    const VkrResult r = vkr_load_material_param(textureDir, material->name,
          VKR_TEXTURE_NAME_TRANSMISSION, &numValues, transmissionIorEtaKTranslucency, eh);
    if (r != VKR_SUCCESS)
      return r;

    material->specularTransmission = transmissionIorEtaKTranslucency[0];
    material->iorEta = transmissionIorEtaKTranslucency[1];
    material->iorK = transmissionIorEtaKTranslucency[2];
    material->translucency = transmissionIorEtaKTranslucency[3];
  }

  void* materialTextures[][3] = {
    { VKR_TEXTURE_NAME_BASE_COLOR,                   &material->texBaseColor },
    { VKR_TEXTURE_NAME_NORMAL,                       &material->texNormal },
    { VKR_TEXTURE_NAME_SPECULAR_ROUGHNESS_METALNESS, &material->texSpecularRoughnessMetalness }
  };
  const size_t numMaterialTextures = sizeof(materialTextures) / sizeof(materialTextures[0]);

  for (size_t i = 0; i < numMaterialTextures; ++i)
  {
    void* const* mt = materialTextures[i];
    const VkrResult r = vkr_load_material_texture(textureDir, material->name,
          (const char *)mt[0], (VkrTexture *)mt[1], eh);
    if (r != VKR_SUCCESS)
      return r;
  }

  if (is_extended_material) {
    for (uint32_t i = 0; i < VkrMaterialMaxFeatureTextures; ++i) {
      char featureTexName[VKR_TEXTURE_NAME_BOUND_FEATURE];
      sprintf(featureTexName, VKR_TEXTURE_NAME_FORMAT_FEATURE, i);
      const VkrResult r = vkr_load_material_texture(textureDir, material->name,
          featureTexName, material->features + i, eh);
      if (r == VKR_INVALID_FILE_NAME)
        break;
      else if (r != VKR_SUCCESS)
        return r;
    }

    for (uint32_t i = 0; i < VkrMaterialMaxTensors; ++i) {
      char tensorTexName[VKR_TEXTURE_NAME_BOUND_TENSOR];
      sprintf(tensorTexName, VKR_TEXTURE_NAME_FORMAT_TENSOR, i);
      const VkrResult r = vkr_load_material_tensor(textureDir, material->name,
          tensorTexName, material->tensors + i, eh);
      if (r == VKR_INVALID_FILE_NAME)
        break;
      else if (r != VKR_SUCCESS)
        return r;
    }
  }

  return VKR_SUCCESS;
}

VkrResult vkr_open_tensor(
    const char *filename,
    VkrTensor *t,
    VkrErrorHandler eh)
{
  if (!t || !filename) {
    return reportError(eh, VKR_INVALID_ARGUMENT,
        "Invalid argument to vkr_open_tensor");
  }
  memset(t, 0, sizeof(VkrTensor));

  FILE *f = fopen(filename, "rb");
  if (!f)
    return VKR_INVALID_FILE_NAME;

  int32_t magic = 0;
  if ((fread(&magic, sizeof(int32_t), 1, f) != 1)
   || (magic != VKR_TENSOR_MAGIC_NUMBER))
  {
    fclose(f);
    return reportError(eh, VKR_INVALID_FILE_FORMAT,
        "%s is not a " VKR_TEXTURE_TENSOR_EXTENSION " file.", filename);
  }

  int32_t version = 0;
  if ((fread(&version, sizeof(int32_t), 1, f) != 1)
   || (version < VKR_MIN_TENSOR_VERSION)
   || (version > VKR_MAX_TENSOR_VERSION))
  {
    fclose(f);
    return reportError(eh, VKR_INVALID_FILE_FORMAT,
        "Unsupported tensor file version %d in %s\n", version, filename);
  }

  uint64_t customDataSize = 0;
  uint64_t reserved[16];
  if ((fread(&t->dimensionality,       sizeof(uint64_t), 1, f) != 1)
   || (fread(&t->dimensions,           sizeof(uint64_t), t->dimensionality, f) != t->dimensionality)
   || (fread(&t->format,               sizeof(int32_t),  1, f) != 1) // 16 uint64_t from here on
   || (fread(&t->flags,                sizeof(int32_t),  1, f) != 1)
   || (fread(&t->numInputs,            sizeof(uint64_t), 1, f) != 1)
   || (fread(&t->numInputLayerBlocks,  sizeof(uint64_t), 1, f) != 1)
   || (fread(&t->numOutputs,           sizeof(uint64_t), 1, f) != 1)
   || (fread(&t->numOutputLayerBlocks, sizeof(uint64_t), 1, f) != 1)
   || (fread(&customDataSize,          sizeof(uint64_t), 1, f) != 1)
   || (fread(&t->storageDescriptor,    sizeof(uint64_t), 1, f) != 1)
   || (fread(&t->componentsDescriptor, sizeof(uint64_t), 1, f) != 1)
   || (fread(&t->ratioDescriptor,      sizeof(double),   1, f) != 1)
   || (fread(reserved,                 sizeof(uint64_t), 16 - 9, f) != 16 - 9))
  {
    fclose(f);
    return reportError(eh, VKR_INVALID_FILE_FORMAT,
        "Failed to read tensor file header.");
  }

  if (t->flags & VKR_TENSOR_FLAGS_INPUT_OUTPUT_SPEC) {
    if (t->numInputs < t->numInputLayerBlocks
     || t->numOutputs < t->numOutputLayerBlocks) {
      fclose(f);
      return reportError(eh, VKR_INVALID_FILE_FORMAT,
          "Tensor input/output spec likely corrupted.");
    }
  } else if (t->numInputs != 0 || t->numInputLayerBlocks != 0
          || t->numOutputs != 0 || t->numOutputLayerBlocks != 0) {
    fclose(f);
    return reportError(eh, VKR_INVALID_FILE_FORMAT,
        "Tensor provides an input/output spec without VKR_TENSOR_FLAGS_INPUT_OUTPUT_SPEC.");
  }

  uint64_t dimensionality = t->dimensionality;
  uint64_t numValues = 1;
  uint64_t dataSize = 0;
  if (dimensionality <= VkrTensorMaxDimensionality) {
    for (uint64_t i = 0; i < dimensionality; ++i)
      numValues *= t->dimensions[i];
    if (t->format == VKR_TENSOR_FORMAT_HALF_FLOAT)
      dataSize = 2;
    else if (t->format == VKR_TENSOR_FORMAT_FLOAT)
      dataSize = 4;
    else if (t->format == VKR_TENSOR_FORMAT_INT8)
      dataSize = 1;
  }
  if (t->flags & VKR_TENSOR_FLAGS_CUSTOM_DATA_LAYOUT)
    dataSize = customDataSize;
  else
    dataSize *= numValues;
  if (!dataSize) {
    fclose(f);
    return reportError(eh, VKR_INVALID_FILE_FORMAT,
        "Invalid tensor format.");
  }

  void *data = calloc(dataSize, 1);
  if (!data) {
      fclose(f);
      return reportError(eh, VKR_ALLOCATION_ERROR, "Failed to allocate tensor array.");
  }
  t->values = data;
  t->dataSize = dataSize;
  t->numValues = numValues;

  if (fread(data, 1, dataSize, f) != dataSize) {
    vkr_close_tensor(t);
    fclose(f);
    return reportError(eh, VKR_INVALID_FILE_FORMAT,
        "Failed to read tensor array.");
  }

  fclose(f);

  return VKR_SUCCESS;
}

void vkr_close_tensor(VkrTensor *t)
{
  if (t) {
    free((void *)t->values);
    memset(t, 0, sizeof(VkrTensor));
  }
}

VkrResult vkr_load_materials(FILE* f, VkrScene *v, const char *filename, VkrErrorHandler eh)
{
  v->textureDir = buildTextureDir(filename);
  if (!v->textureDir)
    return reportError(eh, VKR_ALLOCATION_ERROR,
        "Failed to allocate texture directory name.");

  for (uint64_t i = 0; i < v->numMaterials; ++i)
  {
    VkrMaterial *mat = v->materials + i;
    VkrResult r = vkr_load_string(&mat->name, f, "material name", filename, eh);
    if (r != VKR_SUCCESS)
      return r;

    r = vkr_load_material(v->textureDir, mat, eh);
    if (r != VKR_SUCCESS)
      return r;
  }

  return VKR_SUCCESS;
}


VkrResult vkr_load_scene(FILE *f, VkrScene *v, char const* filename,
    VkrErrorHandler eh)
{
  if (!v || !f) {
    return reportError(eh, VKR_INVALID_ARGUMENT,
        "Invalid argument to vkr_open_scene.");
  }
  memset(v, 0, sizeof(VkrScene));

  int32_t magic = 0;
  if ((fread(&magic, sizeof(int32_t), 1, f) != 1)
   || (magic != VKR_MAGIC_NUMBER))
    return reportError(eh, VKR_INVALID_FILE_FORMAT,
        "%s is not a .vks file.", filename);

  int32_t version = 0;
  if ((fread(&version, sizeof(int32_t), 1, f) != 1)
   || (version < VKR_MIN_VERSION)
   || (version > VKR_MAX_VERSION))
    return reportError(eh, VKR_INVALID_FILE_FORMAT,
        "Unsupported version %d in %s.", version, filename);
  v->version = version;

  int readFailure = 0;
  if (version >= 3) {
    uint64_t flags = 0;
    readFailure |= fread(&flags, sizeof(uint64_t), 1, f) != 1;
    v->flags = (uint32_t) flags;
    readFailure |= fread(&v->headerSize, sizeof(uint64_t), 1, f) != 1;
    readFailure |= fread(&v->dataOffset, sizeof(uint64_t), 1, f) != 1;

    if (readFailure)
      return reportError(eh, VKR_INVALID_FILE_FORMAT,
          "Failed to read header structure from %s.", filename);
    int validHeaderSize = v->headerSize > 0
     && v->dataOffset >= v->headerSize;
    if (!validHeaderSize)
      return reportError(eh, VKR_INVALID_FILE_FORMAT,
          "Failed to read header size & data offset from %s.", filename);
  }

  v->numMeshes = 1;
  v->numInstances = 1;
  if (version >= 2) {
    readFailure |= fread(&v->numMeshes, sizeof(uint64_t), 1, f) != 1;
    readFailure |= fread(&v->numInstances, sizeof(uint64_t), 1, f) != 1;
  }
  readFailure |= fread(&v->numMaterials, sizeof(uint64_t), 1, f) != 1;
  readFailure |= fread(&v->numTriangles, sizeof(uint64_t), 1, f) != 1;

  uint64_t numInstanceGroups = v->numInstances;
  if (version >= 3) {
    readFailure |= fread(&numInstanceGroups, sizeof(uint64_t), 1, f) != 1;
  }

  v->numLodGroups = 1;
  int64_t lodGroupsOffset = 0;
  if (version >= 4) {
    readFailure |= fread(&v->numLodGroups, sizeof(uint64_t), 1, f) != 1;
    readFailure |= fread(&lodGroupsOffset, sizeof(int64_t), 1, f) != 1;

    readFailure |= fread(&v->numBoneIndexTuples, sizeof(uint64_t), 1, f) != 1;
    readFailure |= fread(&v->boneIndexTuplesOffset, sizeof(int64_t), 1, f) != 1;
    readFailure |= fread(&v->animationStart, sizeof(float), 1, f) != 1;
    readFailure |= fread(&v->animationStep, sizeof(float), 1, f) != 1;
    readFailure |= fread(&v->numFrames, sizeof(uint64_t), 1, f) != 1;
    readFailure |= fread(&v->numStaticTransforms, sizeof(uint64_t), 1, f) != 1;
    readFailure |= fread(&v->numAnimatedTransforms, sizeof(uint64_t), 1, f) != 1;
    readFailure |= fread(&v->animationOffset, sizeof(int64_t), 1, f) != 1;
  }
  else {
    // Pretend that it is an animated scene with static transforms only (one
    // per instance)
    v->numFrames = 1;
    v->numStaticTransforms = v->numInstances;
    v->animationData = (unsigned char*) malloc(VKR_QUANTIZED_TRANSFORM_SIZE
        * v->numStaticTransforms);
  }

  if (readFailure
   || v->numMeshes == 0
   || v->numInstances == 0
   || numInstanceGroups == 0
   || v->numLodGroups == 0)
    return reportError(eh, VKR_INVALID_FILE_FORMAT,
        "Failed to read valid object counts from %s.", filename);

  v->meshes = (VkrMesh *) calloc(
    v->numMeshes, sizeof(VkrMesh));
  v->instances = (VkrInstance *) calloc(v->numInstances, sizeof(VkrInstance));
  if (v->numMaterials > 0) {
    v->materials = (VkrMaterial *) calloc(v->numMaterials, sizeof(VkrMaterial));
  }
  v->lodGroups = (VkrLodGroup *) calloc(v->numLodGroups, sizeof(VkrLodGroup));
  if (!v->meshes
   || !v->instances
   || !v->materials && v->numMaterials > 0
   || !v->lodGroups)
    return reportError(eh, VKR_ALLOCATION_ERROR,
        "Failed to allocate arrays for %" PRIu64 " meshes, %"
        PRIu64 " instances, %"
        PRIu64 " materials and %",
        PRIu64 " lod groups",
        v->numMeshes, v->numInstances, v->numMaterials, v->numLodGroups);

  if (version <= 2)
    v->headerSize = ftell(f);
  else if (v->headerSize != ftell(f))
    return reportError(eh, VKR_INVALID_FILE_FORMAT,
      "Mismatching header size in %s.", filename);

  for (uint64_t i = 0; i < v->numMeshes; ++i)
  {
    VkrMesh *mesh = v->meshes + i;

    // sorry, this should always have stayed here
    if (version != 2) {
      readFailure |= fread(&mesh->vertexScale, sizeof(float), 3, f) != 3;
      readFailure |= fread(&mesh->vertexOffset, sizeof(float), 3, f) != 3;
    }

    int64_t headerEnd = 0;
    if (version >= 3) {
      uint64_t flags = 0;
      readFailure |= fread(&flags, sizeof(uint64_t), 1, f) != 1;
      mesh->flags = (uint32_t) flags;
      readFailure |= fread(&headerEnd, sizeof(uint64_t), 1, f) != 1;
      readFailure |= fread(&mesh->vertexBufferOffset, sizeof(uint64_t), 1, f) != 1;
    }

    mesh->numSegments = 1;
    mesh->materialIdBufferBase = 0;
    mesh->numMaterialsInRange = v->numMaterials;
    mesh->numTriangles = v->numTriangles;
    // sorry, this should always have been here
    if (version >= 3) {
      readFailure |= fread(&mesh->numSegments, sizeof(uint64_t), 1, f) != 1;
      readFailure |= fread(&mesh->numTriangles, sizeof(uint64_t), 1, f) != 1;
      readFailure |= fread(&mesh->materialIdBufferBase, sizeof(uint32_t), 1, f) != 1;
      readFailure |= fread(&mesh->numMaterialsInRange, sizeof(uint32_t), 1, f) != 1;

      uint64_t reserved[8];
      int numStillReserved = 8-3;

      if (version >= 4) {
        readFailure |= fread(&mesh->lodGroup, sizeof(int64_t), 1, f) != 1;
        --numStillReserved;
      }

      readFailure |= fread(&reserved, sizeof(uint64_t), numStillReserved, f) != numStillReserved;
    }

    if (mesh->lodGroup >= v->numLodGroups)
      return reportError(eh, VKR_INVALID_FILE_FORMAT,
          "Invalid LoD group specified for mesh %" PRIu64 " from %s.", i, filename);

    if (readFailure)
      return reportError(eh, VKR_INVALID_FILE_FORMAT,
          "Failed to read header for mesh %" PRIu64 " from %s.", i, filename);

    mesh->segmentNumTriangles = calloc(mesh->numSegments, sizeof(uint64_t));
    mesh->segmentMaterialBaseOffsets = calloc(mesh->numSegments, sizeof(int32_t));
    if (!mesh->segmentNumTriangles || !mesh->segmentMaterialBaseOffsets)
      return reportError(eh, VKR_ALLOCATION_ERROR,
          "Failed to allocate arrays for %" PRIu64 " mesh segments.",
          mesh->numSegments);

    if (version >= 3) {
      for (uint64_t j = 0; j < mesh->numSegments; ++j)
        readFailure |= fread(&mesh->segmentNumTriangles[j], sizeof(uint64_t), 1, f) != 1;
      for (uint64_t j = 0; j < mesh->numSegments; ++j)
        readFailure |= fread(&mesh->segmentMaterialBaseOffsets[j], sizeof(int32_t), 1, f) != 1;
    }
    else {
      mesh->segmentNumTriangles[0] = mesh->numTriangles;
      mesh->segmentMaterialBaseOffsets[0] = 0;
    }

    if (readFailure)
      return reportError(eh, VKR_INVALID_FILE_FORMAT,
          "Failed to read header for mesh %" PRIu64 " from %s.", i, filename);

    int r = vkr_load_string(&mesh->name, f, version >= 2 ? "mesh name" : NULL, filename, eh);
    if (r != VKR_SUCCESS)
      return r;

    if (version == 2) { // catch deprecated v2 order
      readFailure |= fread(&mesh->materialIdBufferBase, sizeof(int32_t), 1, f) != 1;
      uint64_t numMaterialsInRange = 0;
      readFailure |= fread(&numMaterialsInRange, sizeof(uint64_t), 1, f) != 1;
      mesh->numMaterialsInRange = (uint32_t) numMaterialsInRange;
      readFailure |= fread(&mesh->numTriangles, sizeof(uint64_t), 1, f) != 1;

      mesh->segmentNumTriangles[0] = mesh->numTriangles;
      mesh->segmentMaterialBaseOffsets[0] = mesh->materialIdBufferBase;

      readFailure |= fread(&mesh->vertexScale, sizeof(float), 3, f) != 3;
      readFailure |= fread(&mesh->vertexOffset, sizeof(float), 3, f) != 3;
    }

    if (readFailure)
      return reportError(eh, VKR_INVALID_FILE_FORMAT,
          "Failed to read header for mesh %s from %s.", mesh->name, filename);


    if (version >= 3 && headerEnd != ftell(f))
      return reportError(eh, VKR_INVALID_FILE_FORMAT,
          "Mismatching header offset for mesh %" PRIu64 " from %s.", i, filename);
  }

  if (version >= 2) {
    uint32_t nextTransformIndex = 0;
    VkrInstance *instance = v->instances;
    for (uint64_t i = 0; i < numInstanceGroups; ++i)
    {
      // sorry, this should always have been here
      if (version != 2) {
        readFailure |= fread(&instance->flags, sizeof(uint32_t), 1, f) != 1;
        readFailure |= fread(&instance->meshId, sizeof(int32_t), 1, f) != 1;
      }

      int64_t headerEnd = 0, dataOffset = 0;
      if (version >= 3) {
        readFailure |= fread(&headerEnd, sizeof(uint64_t), 1, f) != 1;
        readFailure |= fread(&dataOffset, sizeof(uint64_t), 1, f) != 1;
      }

      uint64_t numInstancesInGroup = 1;
      if (version >= 3) {
        readFailure |= fread(&numInstancesInGroup, sizeof(uint64_t), 1, f) != 1;
      }

      if (readFailure)
        return reportError(eh, VKR_INVALID_FILE_FORMAT,
            "Failed to read instance group %" PRId64 " from %s.", i, filename);

      int r = vkr_load_string(&instance->name, f, "instance name", filename, eh);
      if (r != VKR_SUCCESS)
        return r;

      if (version == 2) { // catch deprecated v2 order
        readFailure |= fread(&instance->meshId, sizeof(int32_t), 1, f) != 1;
      }

      if (version >= 3 && dataOffset != ftell(f))
        return reportError(eh, VKR_INVALID_FILE_FORMAT,
            "Mismatching data offset for instance group %" PRIu64 " from %s.", i, filename);

      const VkrInstance *copy_instance = instance;
      for (uint64_t j = 0; j < numInstancesInGroup; ++j, ++instance) {
        // Make all instances be like instance 0
        if (j > 0)
          *instance = *copy_instance;
        // Read either the transformation index
        if (version >= 4)
          readFailure |= fread(&instance->transformIndex, sizeof(uint32_t), 1, f) != 1;
        // Or read the transform, quantize it and store it in the big table
        else {
          float transform[4][3];
          readFailure |= fread(transform, sizeof(float), 4*3, f) != 4*3;
          vkr_quantize_transform(v->animationData
              + VKR_QUANTIZED_TRANSFORM_SIZE * nextTransformIndex, transform);
          instance->transformIndex = nextTransformIndex;
          ++nextTransformIndex;
        }
      }

      if (readFailure)
        return reportError(eh, VKR_INVALID_FILE_FORMAT,
            "Failed to read instance %s from %s.", instance->name, filename);

      if (version >= 3 && headerEnd != ftell(f))
        return reportError(eh, VKR_INVALID_FILE_FORMAT,
            "Mismatching header offset for instance group %" PRIu64 " from %s.", i, filename);
    }
  }
  else {
    VkrInstance *instance = v->instances;
    instance->name = (char *) calloc(1, 1);
    instance->meshId = 0;
    float identity[4][3] = {
      { 1.0f, 0.0f, 0.0f },
      { 0.0f, 1.0f, 0.0f },
      { 0.0f, 0.0f, 1.0f },
      { 0.0f, 0.0f, 0.0f },
    };
    vkr_quantize_transform(v->animationData, identity);
  }

  /*
   * Note: in file version 3 and less, there is no LoD support. There will still
   * be a single, zero-initialized LoD group that we need not initialize further.
   */
  if (version >= 4) {
    if (lodGroupsOffset != ftell(f)) {
      return reportError(eh, VKR_INVALID_FILE_FORMAT,
        "Read invalid LoD group offset from %s.", filename);
    }

    for (uint64_t i = 0; i < v->numLodGroups; ++i) {
      VkrLodGroup *lodGroup = v->lodGroups + i;
      uint64_t numLod = 0;
      if (fread(&numLod, sizeof(uint64_t), 1, f) != 1) {
        return reportError(eh, VKR_INVALID_FILE_FORMAT,
          "Failed to read number of levels of detail for LoD group %" PRIu64 
          " from %s.", i, filename);
      }
      lodGroup->numLevelsOfDetail = numLod;
      if (numLod > 0) {
        lodGroup->meshIds = (int64_t *) calloc(numLod, sizeof(int64_t));
        lodGroup->detailReduction = (float *) calloc(numLod, sizeof(float));
        if (!lodGroup->meshIds || !lodGroup->detailReduction) {
          return reportError(eh, VKR_INVALID_FILE_FORMAT,
            "Failed to allocate memory for LoD group %" PRIu64
            " from %s.", i, filename);
        }
        if (fread(lodGroup->meshIds, sizeof(int64_t), numLod, f) != numLod
         || fread(lodGroup->detailReduction, sizeof(float), numLod, f) != numLod)
        {
          return reportError(eh, VKR_INVALID_FILE_FORMAT,
            "Failed to read LoD group %" PRIu64
            " from %s.", i, filename);
        }
      }
    }
  }

  if (version <= 2)
    v->dataOffset = ftell(f);
  else if (v->dataOffset != ftell(f))
    return reportError(eh, VKR_INVALID_FILE_FORMAT,
      "Mismatching body data offset %s.", filename);

  int materials_result = vkr_load_materials(f, v, filename, eh);
  if (materials_result != VKR_SUCCESS)
    return materials_result;

  int64_t offset = ftell(f);
  if (offset <= 0)
    return reportError(eh, VKR_INVALID_FILE_FORMAT,
        "File I/O error.");

  for (uint64_t i = 0; i < v->numMeshes; ++i)
  {
    VkrMesh *mesh = v->meshes + i;

    if (version >= 3 && mesh->vertexBufferOffset != offset)
      return reportError(eh, VKR_INVALID_FILE_FORMAT,
          "Mismatching data offset for mesh %" PRIu64 " from %s.", i, filename);

    mesh->vertexBufferOffset = offset;
    const uint64_t vertexBufferSize = sizeof(uint64_t) * 3 * mesh->numTriangles;
    offset += vertexBufferSize;

    mesh->normalUvBufferOffset = offset;
    const uint64_t normalUvBufferSize = sizeof(uint64_t) * 3 * mesh->numTriangles;
    offset += normalUvBufferSize;

    mesh->materialIdBufferOffset = offset;
    mesh->materialIdSize = (mesh->numMaterialsInRange <= 0xFF + 1 || mesh->numSegments > 1)
      ? VKR_MATERIAL_ID_8_BITS
      : VKR_MATERIAL_ID_16_BITS; // 16 bit material IDs will be deprecated
    const uint64_t materialIdBufferSize = mesh->materialIdSize * mesh->numTriangles;
    offset += materialIdBufferSize;

    if (mesh->flags & VKR_MESH_FLAGS_INDICES) {
      mesh->indexBufferOffset = offset;
      const uint64_t indexBufferSize = sizeof(uint32_t) * 3 * mesh->numTriangles;
      offset += indexBufferSize;
    }
  }

  return VKR_SUCCESS;
}

VkrResult vkr_open_scene(const char *filename, VkrScene *v, VkrErrorHandler eh)
{
  if (!v || !filename) {
    return reportError(eh, VKR_INVALID_ARGUMENT,
        "Invalid argument to vkr_open_scene.");
  }
  memset(v, 0, sizeof(VkrScene));

  FILE *f = fopen(filename, "rb");
  if (!f) {
    return reportError(eh, VKR_INVALID_FILE_NAME,
        "Failed to open %s.", filename);
  }

  int load_result = vkr_load_scene(f, v, filename, eh);
  fclose(f);

  if (load_result != VKR_SUCCESS) {
    vkr_close_scene(v);
    return load_result;
  }

  return VKR_SUCCESS;
}

void vkr_close_scene(VkrScene *v)
{
  if (v) {
    if (v->materials) {
      for (uint64_t i = 0; i < v->numMaterials; ++i) {
        VkrMaterial *mat = v->materials + i;
        free((void *)mat->name);
        vkr_close_texture(&mat->texBaseColor);
        vkr_close_texture(&mat->texNormal);
        vkr_close_texture(&mat->texSpecularRoughnessMetalness);
        for (uint64_t j = 0; j < VkrMaterialMaxFeatureTextures; ++j)
          vkr_close_texture(&mat->features[j]);
        for (uint64_t j = 0; j < VkrMaterialMaxTensors; ++j)
          vkr_close_tensor(&mat->tensors[j]);
      }
      free(v->materials);
    }
    if (v->meshes) {
      for (uint64_t i = 0; i < v->numMeshes; ++i) {
        VkrMesh *mesh = v->meshes + i;
        free((void *)mesh->name);
        free((void *)mesh->segmentNumTriangles);
        free((void *)mesh->segmentMaterialBaseOffsets);
      }
      free(v->meshes);
    }
    if (v->instances) {
      char const* lastName = NULL;
      for (uint64_t i = 0; i < v->numInstances; ++i) {
        VkrInstance *instance = v->instances + i;
        // note: consecutive instances may share the same name
        if (instance->name != lastName) {
          lastName = instance->name;
          free((void *)instance->name);
        }
      }
      free(v->instances);
    }
    if (v->lodGroups) {
      for (uint64_t i = 0; i < v->numLodGroups; ++i) {
        VkrLodGroup *lodGroup = v->lodGroups + i;
        free((void *)lodGroup->meshIds);
        free((void *)lodGroup->detailReduction);
      }
      free(v->lodGroups);
    }
    free(v->animationData);
    free((void *)v->textureDir);
    memset(v, 0, sizeof(VkrScene));
  }
}

void vkr_dequantize_vertices(
    const uint64_t *vq, const uint64_t numVertices, 
    const float *scale, const float *offset,
    float *v)
{
  // TODO: Vectorize and/or multithread this.
  for (uint64_t i = 0; i < numVertices; ++i) {
    const uint64_t q = vq[i];
    v[3*i]   =  (q         & 0x1FFFFF) * (-scale[0]) - offset[0];
    v[3*i+1] = ((q >> 42u) & 0x1FFFFF) * ( scale[2]) + offset[2];
    v[3*i+2] = ((q >> 21u) & 0x1FFFFF) * ( scale[1]) + offset[1];
  }
}

void vkr_dequantize_normal_uv(
    const uint64_t *nq, const uint64_t numNormals, 
    float *n, float *uv)
{
  // TODO: Vectorize and/or multithread this.
  for (uint64_t i = 0; i < numNormals; ++i) {
    const uint64_t q = nq[i];
    float nx = ((int)((q)        & 0xFFFF) - 0x8000) / (float)0x7FFFu;
    float ny = ((int)((q >> 16u) & 0xFFFF) - 0x8000) / (float)0x7FFFu;
    const float nl1 = fabs(nx) + fabs(ny);
    if (nl1 >= 1.f) {
      const float nfx = copysignf(1.f - fabs(ny), nx);
      const float nfy = copysignf(1.f - fabs(nx), ny);
      nx = nfx;
      ny = nfy;
    }
    n[3*i]    = -nx;
    n[3*i+1]  = 1.f - nl1;
    n[3*i+2]  = ny;
    uv[2*i]   = (8.f / 0xFFFFu) *        (int)((q >> 32u) & 0xFFFF);
    uv[2*i+1] = (8.f / 0xFFFFu) * (1.f - (int)((q >> 48u) & 0xFFFF));
  }
}


/*
 * Given a rotation matrix, this function outputs a normalized quaternion
 * describing the same rotation. Based on
 * http://www.j3d.org/matrix_faq/matrfaq_latest.html (Q55).
 */
static inline void matrix_to_quaternion(
    float quaternion[4],
    const float matrix[3][3])
{
  // There are four different ways to go about it, which are stable in
  // different cases. Different from the FAQ, we multiply everything by S and
  // normalize in the end.
  if (matrix[0][0] + matrix[1][1] + matrix[2][2] > 0.1f) {
    quaternion[0] = matrix[2][1] - matrix[1][2];
    quaternion[1] = matrix[0][2] - matrix[2][0];
    quaternion[2] = matrix[1][0] - matrix[0][1];
    quaternion[3] = 1.0f + matrix[0][0] + matrix[1][1] + matrix[2][2];
  }
  else if (matrix[0][0] > matrix[1][1] && matrix[0][0] > matrix[2][2]) {
    quaternion[0] = 1.0f + matrix[0][0] - matrix[1][1] - matrix[2][2];
    quaternion[1] = matrix[1][0] + matrix[0][1];
    quaternion[2] = matrix[0][2] + matrix[2][0];
    quaternion[3] = matrix[2][1] - matrix[1][2];
  }
  else if (matrix[1][1] > matrix[0][0] && matrix[1][1] > matrix[2][2]) {
    quaternion[0] = matrix[1][0] + matrix[0][1];
    quaternion[1] = 1.0f + matrix[1][1] - matrix[0][0] - matrix[2][2];
    quaternion[2] = matrix[2][1] + matrix[1][2];
    quaternion[3] = matrix[0][2] - matrix[2][0];
  }
  else {
    quaternion[0] = matrix[0][2] + matrix[2][0];
    quaternion[1] = matrix[2][1] + matrix[1][2];
    quaternion[2] = 1.0f + matrix[2][2] - matrix[0][0] - matrix[1][1];
    quaternion[3] = matrix[1][0] - matrix[0][1];
  }
  // Normalize
  float lengthSq = 0.0f;
  for (int i = 0; i != 4; ++i)
    lengthSq += quaternion[i] * quaternion[i];
  float invLength = 1.0f / sqrtf(lengthSq);
  for (int i = 0; i != 4; ++i)
    quaternion[i] *= invLength;
}


/*
 * Inverse of matrix_to_quaternion(). Q54 from the matrix and quaternion FAQ.
 */
static inline void quaternion_to_matrix(
    float matrix[3][3],
    const float quaternion[4])
{
  float xx = quaternion[0] * quaternion[0];
  float xy = quaternion[0] * quaternion[1];
  float xz = quaternion[0] * quaternion[2];
  float xw = quaternion[0] * quaternion[3];
  float yy = quaternion[1] * quaternion[1];
  float yz = quaternion[1] * quaternion[2];
  float yw = quaternion[1] * quaternion[3];
  float zz = quaternion[2] * quaternion[2];
  float zw = quaternion[2] * quaternion[3];
  matrix[0][0] = 1.0f - 2.0f * ( yy + zz );
  matrix[0][1] =        2.0f * ( xy - zw );
  matrix[0][2] =        2.0f * ( xz + yw );
  matrix[1][0] =        2.0f * ( xy + zw );
  matrix[1][1] = 1.0f - 2.0f * ( xx + zz );
  matrix[1][2] =        2.0f * ( yz - xw );
  matrix[2][0] =        2.0f * ( xz - yw );
  matrix[2][1] =        2.0f * ( yz + xw );
  matrix[2][2] = 1.0f - 2.0f * ( xx + yy );
}


// Returns the determinant of the given 3x3 matrix
static inline float determinant3(const float matrix[3][3]) {
  // Laplace expansion
  return
    + matrix[0][0] * (matrix[1][1] * matrix[2][2] - matrix[1][2] * matrix[2][1])
    - matrix[0][1] * (matrix[1][0] * matrix[2][2] - matrix[1][2] * matrix[2][0])
    + matrix[0][2] * (matrix[1][0] * matrix[2][1] - matrix[1][1] * matrix[2][0]);
}


void vkr_quantize_transform(
    unsigned char *quantized,
    const float matrix[4][3])
{
  // Extract translation
  float translation[3] = { matrix[3][0], matrix[3][1], matrix[3][2] };
  // Extract a (possibly negative) scaling
  float scaling = 0.0f;
  for (int i = 0; i != 3; ++i)
    scaling += matrix[0][i] * matrix[0][i];
  scaling = sqrtf(scaling);
  if (determinant3(matrix) < 0.0f)
    scaling = -scaling;
  // Normalize the matrix and get a quaternion
  float normed_matrix[3][3];
  float inv_scaling = 1.0f / scaling;
  for (int i = 0; i != 3; ++i)
    for (int j = 0; j != 3; ++j)
      normed_matrix[i][j] = matrix[i][j] * inv_scaling;
  float quaternion[4];
  matrix_to_quaternion(quaternion, normed_matrix);
  // Matrices are transposed here, so we have to flip the quaternion
  quaternion[3] = -quaternion[3];
  // Quantize the quaternion
  uint16_t quantized_quaternion[4];
  for (int i = 0; i != 4; ++i)
    quantized_quaternion[i] = (uint16_t) floorf((quaternion[i] * 0.5f + 0.5f) * ((float) 0xffff) - 0.5f);
  // Now pack it all
  memcpy(quantized, translation, sizeof(translation));
  quantized += sizeof(translation);
  memcpy(quantized, &scaling, sizeof(scaling));
  quantized += sizeof(scaling);
  memcpy(quantized, quantized_quaternion, sizeof(quantized_quaternion));
}


void vkr_dequantize_transform(
    float matrix[4][3],
    const unsigned char *quantized)
{
  // Unpack the data
  float translation[3];
  float scaling;
  uint16_t quantized_quaternion[4];
  memcpy(translation, quantized, sizeof(translation));
  quantized += sizeof(translation);
  memcpy(&scaling, quantized, sizeof(scaling));
  quantized += sizeof(scaling);
  memcpy(quantized_quaternion, quantized, sizeof(quantized_quaternion));
  // Dequantize the quaternion
  float quaternion[4];
  for (int i = 0; i != 4; ++i)
    quaternion[i] = quantized_quaternion[i] * (2.0f / ((float) 0xffff)) - 1.0f;
  // Convert the quaternion back into a matrix (using the conjugate because our
  // matrix is transposed)
  quaternion[3] = -quaternion[3];
  quaternion_to_matrix(matrix, quaternion);
  // Apply the scaling and translation
  for (int i = 0; i != 3; ++i) {
    matrix[3][i] = translation[i];
    for (int j = 0; j != 3; ++j)
      matrix[i][j] *= scaling;
  }
}

#ifdef VKR_BUILD_TOOLS

VkrResult vkr_optimize_mesh(uint64_t numTriangles,
                            uint32_t *indices,
                            uint64_t numVertices,
                            uint32_t *remapOut,
                            VkrErrorHandler eh)
{
  /* NOP if there is no mesh. */
  if (!indices || numTriangles == 0)
    return VKR_SUCCESS;

  const size_t numIndices = numTriangles * 3;
  for (size_t i = 0; i < numIndices; ++i) {
    if (indices[i] >= numVertices) {
      return reportError(eh, VKR_INVALID_ARGUMENT,
        "Invalid index buffer for the given number of vertices");
    }
  }

  const size_t indexBufferSize = sizeof(uint32_t) * numIndices;
  const size_t bufferSize = indexBufferSize;

  char *buffer = (char *)malloc(bufferSize);
  uint32_t *remapTable = remapOut ? remapOut : (uint32_t *)malloc(indexBufferSize);

  if (!buffer || !remapTable)
  {
    if (!remapOut)
      free(remapTable);
    free(buffer);
    return reportError(eh, VKR_ALLOCATION_ERROR,
      "Failed to allocate temporary buffers in vkr_optimize_mesh");
  }

  /* Reorder triangles such that adjacent triangles are nearby
   * in the index buffer. */
  memcpy(buffer, indices, indexBufferSize);
  meshopt_optimizeVertexCache(indices, remapTable, (uint32_t*)buffer,
      numIndices, numVertices);


  if (!remapOut)
    free(remapTable);
  free(buffer);
  return VKR_SUCCESS;
}

// May return a value less than the input if the input is too big!
int32_t next_power_of_two(int32_t i)
{
  const int pt[] = {
    0x00000001, 0x00000002, 0x00000004, 0x00000008,
    0x00000010, 0x00000020, 0x00000040, 0x00000080,
    0x00000100, 0x00000200, 0x00000400, 0x00000800,
    0x00001000, 0x00002000, 0x00004000, 0x00008000,
    0x00010000, 0x00020000, 0x00040000, 0x00080000,
    0x00100000, 0x00200000, 0x00400000, 0x00800000,
    0x01000000, 0x02000000, 0x04000000, 0x08000000,
    0x10000000, 0x20000000, 0x40000000
  };
  const int numPt = sizeof(pt) / sizeof(pt[0]);
  for (int j = 0; j < numPt; ++j)
  {
    if (pt[j] >= i)
      return pt[j];
  }
  return pt[numPt-1];
}

/*
 * sRGB to linear conversion as described in the Khronos Data Format Spec
 * 1.3, Section 13.3.1 EOTF
 */
float srgb_to_linear(float v)
{
  return (v <= 0.04045f)
    ? (v / 12.92f)
    : powf((v + 0.055f) / 1.055f, 2.4f);
}

/*
 * Linear to sRGB conversion as described in the Khronos Data Format Spec
 * 1.3, Section 13.3.2 EOTF^-1
 */
float linear_to_srgb(float v)
{
  return (v <= 0.0031308f) 
    ? (v * 12.92f)
    : (1.055f * powf(v, 1.f/2.4f) - 0.055f);
}

typedef struct {
  int32_t magic;
  int32_t version;
  int32_t numMipLevels;
  int32_t width;
  int32_t height;
  int32_t format;
  uint64_t dataSize;
} VktHeader;
#if __STDC_VERSION__ > 201112L
_Static_assert(sizeof(VktHeader) == 32, "Unexpected size: VktHeader");
#endif

typedef struct {
  int32_t width;
  int32_t height;
  uint64_t dataSize;
  int64_t dataOffset;
} VktMipHeader;
#if __STDC_VERSION__ > 201112L
_Static_assert(sizeof(VktMipHeader) == 24, "Unexpected size: VktMipHeader");
#endif

/*
 * There can be at most VKR_MAX_MIP_LEVELS mip levels,
 * so *header must have space for that many.
 * Returns the actual number of levels.
 */
int compute_mip_headers(int w, int h, int minRes, size_t bitsPerTexel,
    VktMipHeader *header)
{
  int n = 0;
  int mw = w;
  int mh = h;
  size_t totalTexels = 0;
  for (int i = 0; i < VKR_MAX_MIP_LEVELS; ++i) {
    ++n;
    const int mipTexels = mw * mh;
    totalTexels += mipTexels;

    header[i].width = mw;
    header[i].height = mh;
    header[i].dataSize = (mipTexels * bitsPerTexel) / 8;

    if (mw <= minRes && mh <= minRes) {
      break;
    }
    if (mw > minRes) mw /= 2;
    if (mh > minRes) mh /= 2;
  }

  int64_t dataOffset = sizeof(VktHeader) + n * sizeof(VktMipHeader);
  for (int i = 0; i < n; ++i) {
    header[i].dataOffset = dataOffset;
    dataOffset += header[i].dataSize;
  }

  return n;
}

VkrResult load_power_of_two(FILE *f, int minRes,
    int *w, int *h, int *c, float **texels, VkrErrorHandler eh)
{
  int iw = 0;
  int ih = 0;
  int ic = 0;
  float *raw = stbi_loadf_from_file(f, &iw, &ih, &ic, 0);

  if (!raw) {
    return reportError(eh, VKR_INVALID_FILE_FORMAT,
      "Unsupported input file format");
  }

  if (iw < minRes || ih < minRes) {
    stbi_image_free(raw);
    return reportError(eh, VKR_INVALID_ARGUMENT,
      "Input file must be at least %dx%d texels.",
      minRes, minRes);
  }

  const int w2 = next_power_of_two(iw);
  const int h2 = next_power_of_two(ih);
  const size_t numBytes = w2 * h2 * ic * sizeof(float);
  float *powerOfTwo = (float*)malloc(numBytes);

  if (!powerOfTwo) {
    stbi_image_free(raw);
    return reportError(eh, VKR_ALLOCATION_ERROR,
      "Unable to allocate input image buffer.");
  }

  if (w2 == iw && h2 == ih) {
    memcpy(powerOfTwo, raw, sizeof(float) * iw * ih * ic);
  } else {
    stbir_resize_float(raw, iw, ih, 0, powerOfTwo, w2, h2, 0, ic);
  }

  stbi_image_free(raw);
  *w = w2;
  *h = h2;
  *c = ic;
  *texels = powerOfTwo;
  return VKR_SUCCESS;
}

int clamp(int v, int vmin, int vmax)
{
  v = v < vmin ? vmin : v;
  return v > vmax ? vmax : v;
}

float clampf(float v, float vmin, float vmax)
{
  v = (v < vmin) ? vmin : v;
  return (v > vmax) ? vmax : v;
}

int repeat(int x, int w)
{
  return x & (w-1);
}

void init_gaussian_kernel(float sigma, int n, float *k)
{
  if (n == 1)
  {
    k[0] = 1.f;
    return;
  }

  const float fac = -1.f / (2.f * sigma * sigma);
  float sum = 0.f;
  const float center = (n-1) * 0.5f;
  for (int i = 0; i < n; ++i)
  {
    const float dist = i - center;
    k[i] = expf(fac * dist);
    sum += k[i];
  }

  const float norm = 1.f / sum;
  for (int i = 0; i < n; ++i)
    k[i] *= norm;
}

// Note: This function assumes sw, sh, tw, th to be powers of two.
// Note: Target channels (tc) can be different from source channels (sc).
//       If there is target alpha and the source does not have alpha, then
//       the output is opaque.
//       If sc == 1 and tc > 1, then missing channels will be broadcast.
//       If sc > 1 and sc != tc, then missing channels will be set to 0.
//       If sc > tc, then additional channels will be dropped silently without
//       filtering.
void downscale(float *src, int sw, int sh, int sc,
               float *tgt, int tw, int th, int tc)
{
  // Each texel in the target image corresponds to a kernelW x kernelH block
  // of texels in the source. We initialize our filter kernel to this size.
  // Note that this results in even filter sizes.
  const int kernelW = sw / tw;
  const int kernelH = sh / th;

  const float kernelRadiusX = (kernelW - 1) / 0.5f;
  const float sigmaX = kernelRadiusX / 3.f;
  float *kernelX = (float *)malloc(kernelW * sizeof(float));

  const float kernelRadiusY = (kernelH - 1) / 0.5f;
  const float sigmaY = kernelRadiusY / 3.f;
  float *kernelY = (float *)malloc(kernelH * sizeof(float));

  if (kernelX && kernelY) {
    init_gaussian_kernel(sigmaX, kernelW, kernelX);
    init_gaussian_kernel(sigmaY, kernelH, kernelY);

    float *t = tgt;
    const int broadcast = (sc == 1) && (tc > 1);
    for (int y = 0; y < th; ++y)
    for (int x = 0; x < tw; ++x, t += tc)
    {
      // Initialize, but make sure to use opaque alpha if there is no source
      // alpha.
      for (int z = 0; z < tc; ++z)
        t[z] = (z == 3 && sc < 4) ? 1.f : 0.f;

      const int baseX = x * kernelW;
      const int baseY = y * kernelH;

      for (int j = 0; j < kernelH; ++j)
      for (int i = 0; i < kernelW; ++i)
      {
        //const int srcX = clamp(baseX + i, 0, sw-1);
        //const int srcY = clamp(baseY + j, 0, sh-1);
        // Clamping works, but repeating looks better. Note that because of the
        // power-of-two sizes, we can zero high bits instead of using modulo.
        const int srcX = (baseX+i) & (sw-1);
        const int srcY = (baseY+j) & (sh-1);
        const int srcIdx = (srcY * sw + srcX) * sc;
        const float weight = kernelX[i] * kernelY[j];
        if (broadcast) {
          // Note: We do not broadcast to the alpha channel (= 3).
          for (int z = 0; (z < 3) && z < tc; ++z)
            t[z] += weight * src[srcIdx];
        } else {
          for (int z = 0; (z < sc) && (z < tc); ++z)
            t[z] += weight * src[srcIdx+z];
        }
      }
    }
  }

  free(kernelY);
  free(kernelX);
}

void extract_block_4x4(const float *src, int w, int h, int c,
                       int ox, int oy, // origin in src, in units of texels
                       uint8_t *tgt)
{
  for (int i = 0; i < 4; ++i)
  for (int j = 0; j < 4; ++j)
  for (int k = 0; k < c; ++k)
  {
    const size_t sIdx = ((oy+i) * w + (ox+j)) * c + k;
    const size_t tIdx = (i * 4 + j) * c + k;
    tgt[tIdx] = (uint8_t)(clampf(src[sIdx] * 256.f, 0.f, 255.f));
  }
}

#if defined(VKR_VKT_DEBUG_MIP_LEVELS)
/*
 * This function writes pixels into a png file, but it also tests
 * the extract_block_4x4 function at the same time!
 */
void dump(const char *filename,
          const float *pixels,
          int w, int h, int c)
{
  const int outC = (c < 4) ? 3 : 4; // Output is always at least RBG
  uint8_t *buf = (uint8_t*)malloc(w * h * outC);
  if (!buf) {
    printf("Cannot allocate buffer for dumping.");
    return;
  }

  uint8_t block[4*4*4];
  for (int oy = 0; oy < h; oy += 4)
  for (int ox = 0; ox < w; ox += 4)
  {
    extract_block_4x4(pixels, w, h, c, ox, oy, block);

    for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j)
    for (int k = 0; k < outC; ++k)
    {
      const size_t sIdx = (i * 4 + j) * c + k;
      const size_t tIdx = ((oy+i) * w + (ox+j)) * outC + k;
      buf[tIdx] = (k < c) ? block[sIdx]
                : ((k < 3) ? 0 : 1);
    }
  }

  printf("dumping %s ...\n", filename);
  stbi_write_png(filename, w, h, outC, buf, w * outC);
  free(buf);
}
#endif

// tgt must be 64 bits
void compress_bc1_noalpha(const uint8_t *src, uint8_t *tgt)
{
  stb_compress_dxt_block(tgt, src, 0, STB_DXT_HIGHQUAL);
}

// This implementation is essentially a suboptimal hack.
// A few things to consider:
//
// BC1 stores a 4 entry lookup table, and 2 bit per pixel to index it.
//
// The table is stored implicitly using two endpoints c0, c1 and then built as
//   (c0, c1, 2/3 c0 + 1/3 c1, 1/3 c0 + 2/3 c1)
//
// The stb compressor uses PCA to find an initial guess at c0, c1 and then
// optimizes a squared error over all colors.
//
// WITH 1 BIT ALPHA, the table is computed differently:
//    (c0, c1, 1/2 c0 + 1/2 c1, 0)
//
// The last entry, 0, is a black, transparent pixel.
//
// This means that ideally, we need to reimplement the optimization procedure.
//
// Instead, we replace all transparent pixels with the overall mean, which
// should make them be ignored in PCA. We then use the normal BC1 procedure,
// and finally simply refit the index.
void compress_bc1_alpha(const uint8_t *src, uint8_t *tgt)
{
  unsigned transparent[16];
  unsigned transparentMask = 0;
  unsigned meanColor[] = {0, 0, 0};
  unsigned numOpaque = 0;
  for (unsigned i = 0; i < 16; ++i) {
    transparent[i] = (src[4*i + 3] < VKR_TEXTURE_1BIT_ALPHA_THRESHOLD);
    if (transparent[i]) {
      transparentMask |= (0x3 << (2*i));
    }
    else {
      meanColor[0] += (unsigned)src[4*i];
      meanColor[1] += (unsigned)src[4*i+1];
      meanColor[2] += (unsigned)src[4*i+2];
      ++numOpaque;
    }
  }

  // No opaque pixels, so we make the whole block black and fully transparent.
  if (numOpaque == 0) {
    *((uint16_t *)(tgt))   = 0u;
    *((uint16_t *)(tgt+2)) = 0u;
    *((uint32_t *)(tgt+4)) = 0xFFFFFFFF;
    return;
  }

  meanColor[0] /= numOpaque;
  meanColor[1] /= numOpaque;
  meanColor[2] /= numOpaque;

  uint8_t msrc[16*4];
  for (unsigned i = 0; i < 16; ++i)
  {
    uint8_t *p = msrc + 4 * i;
    if (transparent[i]) {
      p[0] = (uint8_t)meanColor[0];
      p[1] = (uint8_t)meanColor[1];
      p[2] = (uint8_t)meanColor[2];
      p[3] = 0xFF;
    } else {
      p[0] = src[4*i+0];
      p[1] = src[4*i+1];
      p[2] = src[4*i+2];
      p[3] = 0xFF;
    }
  }

  stb_compress_dxt_block(tgt, src, 0, STB_DXT_HIGHQUAL);

  if (numOpaque < 16) {
    // Alpha mode is indicated by a swapped order of c0, c1.
    const uint16_t tmp = *(uint16_t *)tgt;
    *(uint16_t *)tgt = *(((uint16_t *)tgt)+1);
    *(((uint16_t *)tgt)+1) = tmp;

    uint32_t indices = *((uint32_t *)(tgt+4));

    // 00 ^ 01 = 01   (indices 0 and 1 are swapped)
    // 01 ^ 01 = 00
    // 10 ^ 01 = 11   (indices 2 and 3 are also swapped)
    // 11 ^ 01 = 10
    // 5 = 0101
    indices ^= 0x55555555;

    // Interpolated indices are 2 and 3 - higher bit is set.
    // We extract all set higher bits in a byte using 1010 1010 = 0xAA.
    unsigned interpolated = (indices & 0xAAAAAAAA);
    // Shift over to obtain a mask for the low bit of all interpolated entries.
    interpolated >>= 1;
    // Disable the lower bit if it is set.
    indices &= ~interpolated;

    // Finally, set transparent pixels to 11.
    indices |= transparentMask;

    *((uint32_t *)(tgt+4)) = indices;
  }
}

// tgt must be 128 bits.
void compress_bc3(const uint8_t *src, uint8_t *tgt)
{
  stb_compress_dxt_block(tgt, src, 1, STB_DXT_HIGHQUAL);
}

// tgt must be 128 bits.
void compress_bc5(const uint8_t *src, uint8_t *tgt)
{
  stb_compress_bc5_block(tgt, src);
}

VkrResult convert_texture_bc(
  FILE *inf, FILE *outf,
  int format, int opaqueFormat,
  VkrErrorHandler eh)
{
  assert(inf);
  assert(outf);

  int load_srgb = 0;
  switch (format)
  {
    case VKR_TEXTURE_FORMAT_BC1_RGB_SRGB_BLOCK:
    case VKR_TEXTURE_FORMAT_BC1_RGBA_SRGB_BLOCK:
    case VKR_TEXTURE_FORMAT_BC3_SRGB_BLOCK:
      load_srgb = 1;
      break;
  }

  const float gamma = load_srgb ? 2.2f : 1.0f;
  stbi_ldr_to_hdr_gamma(gamma);
  stbi_hdr_to_ldr_gamma(gamma);

  int w = 0;
  int h = 0;
  int c = 0;
  float *texels = NULL;
  VkrResult result = load_power_of_two(inf, 4, &w, &h, &c, &texels, eh);

  if (result != VKR_SUCCESS) {
    return result;
  }

  if (opaqueFormat != format) {
    if (c < 4) {
      format = opaqueFormat;
    } else {
      int opaque = 1;
      for (float *alpha = texels + 3, *alphaEnd = alpha + (int64_t) c * w * h; alpha != alphaEnd; alpha += c)
        opaque &= !(*alpha < 1.0f);
      if (opaque)
        format = opaqueFormat;
    }
  }

  int bitsPerTexel = 0;
  int targetChannels = 0;
  int srgb = 0;
  void (*compressor)(const uint8_t *, uint8_t *) = NULL;
  switch (format)
  {
    case VKR_TEXTURE_FORMAT_BC1_RGB_UNORM_BLOCK:
      targetChannels = 4;
      bitsPerTexel = 4;
      compressor = compress_bc1_noalpha;
      break;
    case VKR_TEXTURE_FORMAT_BC1_RGB_SRGB_BLOCK:
      targetChannels = 4;
      srgb = 1;
      bitsPerTexel = 4;
      compressor = compress_bc1_noalpha;
      break;
    case VKR_TEXTURE_FORMAT_BC1_RGBA_UNORM_BLOCK:
      targetChannels = 4;
      bitsPerTexel = 4;
      compressor = compress_bc1_alpha;
      break;
    case VKR_TEXTURE_FORMAT_BC1_RGBA_SRGB_BLOCK:
      targetChannels = 4;
      srgb = 1;
      bitsPerTexel = 4;
      compressor = compress_bc1_alpha;
      break;
    case VKR_TEXTURE_FORMAT_BC3_UNORM_BLOCK:
      targetChannels = 4;
      srgb = 0;
      bitsPerTexel = 8;
      compressor = compress_bc3;
      break;
    case VKR_TEXTURE_FORMAT_BC3_SRGB_BLOCK:
      targetChannels = 4;
      srgb = 1;
      bitsPerTexel = 8;
      compressor = compress_bc3;
      break;
    case VKR_TEXTURE_FORMAT_BC5_UNORM_BLOCK:
      targetChannels = 2;
      srgb = 0;
      bitsPerTexel = 8;
      compressor = compress_bc5;
      break;
    case VKR_TEXTURE_FORMAT_R8G8B8A8_UNORM:
      targetChannels = 4;
      srgb = 0;
      bitsPerTexel = 32;
      break;
    default:
      assert(-1);
      free(texels);
      return reportError(eh, VKR_INVALID_ARGUMENT,
        "Unsupported texture format %d", format);
  }
  if (load_srgb != srgb) {
    free(texels);
    return reportError(eh, VKR_INVALID_ARGUMENT,
        "Internal error: Loaded sRGB=%d but storing sRGB=%d", load_srgb, srgb);
  }

  VktMipHeader mipHeaders[VKR_MAX_MIP_LEVELS];
  const int numMipLevels
    = compute_mip_headers(w, h, 4, bitsPerTexel, mipHeaders);
  const size_t dataSize = mipHeaders[numMipLevels-1].dataOffset
                        - mipHeaders[0].dataOffset
                        + mipHeaders[numMipLevels-1].dataSize;

  const VktHeader header = {
    .magic = VKR_TEXTURE_MAGIC_NUMBER,
    .version = VKR_MAX_TEXTURE_VERSION,
    .numMipLevels = numMipLevels,
    .width = w,
    .height = h,
    .format = format,
    .dataSize = dataSize
  };

  fwrite(&header, sizeof(VktHeader), 1, outf);
  fwrite(mipHeaders, sizeof(VktMipHeader), numMipLevels, outf);

  const size_t filteredValues = w * h * targetChannels;
  const size_t filteredSize = filteredValues * sizeof(float);
  float *filtered = (float*)malloc(filteredSize);

  uint8_t *block = (uint8_t *)malloc(4*4*targetChannels);
  const size_t compressedSize = bitsPerTexel*2;/*4x4 texels / 8 bit*/
  uint8_t *compressed = (uint8_t *)malloc(compressedSize);

  if (filtered && block && compressed) {
    for (int l = 0; l < numMipLevels; ++l)
    {
      const int mw = mipHeaders[l].width;
      const int mh = mipHeaders[l].height;

      // Note: This also works for level 0, where w == mw and h == mh, and it
      //       will not blur the image.
      downscale(texels, w, h, c, filtered, mw, mh, targetChannels);

      if (srgb == 1)
      {
        // Alpha will stay linear.
        const int srgbChan = clamp(targetChannels, 0, 3);
        for (size_t i = 0; i < filteredValues; i += targetChannels)
        {
          for (int j = 0; j < srgbChan; ++j)
            filtered[i+j] = linear_to_srgb(filtered[i+j]);
        }
      }

#if defined(VKR_VKT_DEBUG_MIP_LEVELS)
      {
        char lfname[] = "mip_level_XX.png";
        sprintf(lfname, "mip_level_%02d.png", l);
        dump(lfname, filtered, mw, mh, targetChannels);
      }
#endif

      if (!compressor) {
        // note: uncompressed texel layout is not blocked
        // reinterpret as 4xN, where blocked == linear layout
        int num4WideLines = mh * mw / 4;
        for (int oy = 0; oy < num4WideLines; oy += 4) {
          extract_block_4x4(filtered, 4, num4WideLines, targetChannels, 0, oy, compressed);
          fwrite((const char*)compressed, compressedSize, 1, outf);
        }
        continue;
      }

      for (int oy = 0; oy < mh; oy += 4)
      for (int ox = 0; ox < mw; ox += 4)
      {
        extract_block_4x4(filtered, mw, mh, targetChannels, ox, oy, block);
        compressor(block, compressed);
        fwrite((const char*)compressed, compressedSize, 1, outf);
      }
    }
  }
  else {
    result = reportError(eh, VKR_ALLOCATION_ERROR,
      "Unable to allocate auxiliary buffers.");
  }

  free(compressed);
  free(block);
  free(filtered);
  free(texels);

  return result;
}

VkrResult vkr_convert_texture(
    const char *inputFile, const char *outputFile,
    VkrTextureFormat format, VkrTextureFormat opaqueFormat,
    VkrErrorHandler eh)
{
  if (!inputFile || !outputFile) {
    return reportError(eh, VKR_INVALID_ARGUMENT,
      "Invalid argument to vkr_convert_texture");
  }

  FILE *inf = fopen(inputFile, "rb");
  if (!inf) {
    return reportError(eh, VKR_INVALID_FILE_NAME,
      "Cannot open %s for reading", inputFile);
  }

  FILE *outf = fopen(outputFile, "wb");
  if (!outf) {
    fclose(inf);
    return reportError(eh, VKR_INVALID_FILE_NAME,
      "Cannot open %s for writing", outputFile);
  }

  VkrResult result = VKR_SUCCESS;

  if (format == VKR_TEXTURE_FORMAT_BC1_RGB_UNORM_BLOCK
   || format == VKR_TEXTURE_FORMAT_BC1_RGB_SRGB_BLOCK
   || format == VKR_TEXTURE_FORMAT_BC1_RGBA_UNORM_BLOCK
   || format == VKR_TEXTURE_FORMAT_BC1_RGBA_SRGB_BLOCK
   || format == VKR_TEXTURE_FORMAT_BC3_UNORM_BLOCK
   || format == VKR_TEXTURE_FORMAT_BC3_SRGB_BLOCK
   || format == VKR_TEXTURE_FORMAT_BC5_UNORM_BLOCK
   || format == VKR_TEXTURE_FORMAT_R8G8B8A8_UNORM)
  {
    result = convert_texture_bc(inf, outf, format, opaqueFormat, eh);
  }

  else {
    result = reportError(eh, VKR_INVALID_TEXTURE_FORMAT,
      "Unsupported texture format %d", format);
  }

  if (result != VKR_SUCCESS)
    result = reportError(eh, result, "%s", inputFile);

  fclose(outf);
  fclose(inf);

  return result;
}

#endif // VKR_BUILD_TOOLS

