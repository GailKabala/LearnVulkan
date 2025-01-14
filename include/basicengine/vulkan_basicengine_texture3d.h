/*
* LearnVulkan BasicEngine
*
* Copyright (C) by engineer1109 - https://github.com/engineer1109/LearnVulkan
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/
#ifndef VULKAN_BASICENGINE_TEXTURE_H
#define VULKAN_BASICENGINE_TEXTURE_H
#ifdef WIN32
    #define gli glm
#endif
#include <iostream>
#include "VulkanTexture.hpp"
#include "stb_image_aug.h"
namespace vks {

struct Texture3D: public Texture{

    VkSamplerAddressMode m_addressMode=VK_SAMPLER_ADDRESS_MODE_REPEAT;

    void setAddressMode(VkSamplerAddressMode mode){
        m_addressMode=mode;
    }

    void loadFromArray(
        uint8_t* imgData,
        uint32_t width,
        uint32_t height,
        uint32_t depth,
        VkFormat format,
        vks::VulkanDevice *device,
        VkQueue copyQueue,
        VkImageUsageFlags imageUsageFlags = VK_IMAGE_USAGE_SAMPLED_BIT,
        VkImageLayout imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        bool forceLinear = false)
    {

        this->device = device;
        uint32_t size=width*height*depth*sizeof(uint8_t);
        mipLevels = 1;

        // Get device properites for the requested texture format
        VkFormatProperties formatProperties;
        vkGetPhysicalDeviceFormatProperties(device->physicalDevice, format, &formatProperties);

        // Only use linear tiling if requested (and supported by the device)
        // Support for linear tiling is mostly limited, so prefer to use
        // optimal tiling instead
        // On most implementations linear tiling will only support a very
        // limited amount of formats and features (mip maps, cubemaps, arrays, etc.)
        VkBool32 useStaging = !forceLinear;

        VkMemoryAllocateInfo memAllocInfo = vks::initializers::memoryAllocateInfo();
        VkMemoryRequirements memReqs;

        // Use a separate command buffer for texture loading
        VkCommandBuffer copyCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

        if (useStaging)
        {
            // Create a host-visible staging buffer that contains the raw image data
            VkBuffer stagingBuffer;
            VkDeviceMemory stagingMemory;

            VkBufferCreateInfo bufferCreateInfo = vks::initializers::bufferCreateInfo();
            bufferCreateInfo.size = size;
            // This buffer is used as a transfer source for the buffer copy
            bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VK_CHECK_RESULT(vkCreateBuffer(device->logicalDevice, &bufferCreateInfo, nullptr, &stagingBuffer));

            // Get memory requirements for the staging buffer (alignment, memory type bits)
            vkGetBufferMemoryRequirements(device->logicalDevice, stagingBuffer, &memReqs);

            memAllocInfo.allocationSize = memReqs.size;
            // Get memory type index for a host visible buffer
            memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

            VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &stagingMemory));
            VK_CHECK_RESULT(vkBindBufferMemory(device->logicalDevice, stagingBuffer, stagingMemory, 0));

            // Copy texture data into staging buffer
            uint8_t *data;
            VK_CHECK_RESULT(vkMapMemory(device->logicalDevice, stagingMemory, 0, memReqs.size, 0, (void **)&data));
            memcpy(data, imgData, size);
            vkUnmapMemory(device->logicalDevice, stagingMemory);

            // Setup buffer copy regions for each mip level
            std::vector<VkBufferImageCopy> bufferCopyRegions;
            uint32_t offset = 0;

            for (uint32_t i = 0; i < mipLevels; i++)
            {
                VkBufferImageCopy bufferCopyRegion = {};
                bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                bufferCopyRegion.imageSubresource.mipLevel = i;
                bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
                bufferCopyRegion.imageSubresource.layerCount = 1;
                bufferCopyRegion.imageExtent.width = width;
                bufferCopyRegion.imageExtent.height = height;
                bufferCopyRegion.imageExtent.depth = depth;
                bufferCopyRegion.bufferOffset = offset;

                bufferCopyRegions.push_back(bufferCopyRegion);

                offset += static_cast<uint32_t>(size);
            }

            // Create optimal tiled target image
            VkImageCreateInfo imageCreateInfo = vks::initializers::imageCreateInfo();
            imageCreateInfo.imageType = VK_IMAGE_TYPE_3D;
            imageCreateInfo.format = format;
            imageCreateInfo.mipLevels = mipLevels;
            imageCreateInfo.arrayLayers = 1;
            imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageCreateInfo.extent = { width, height, 1 };
            imageCreateInfo.usage = imageUsageFlags;
            // Ensure that the TRANSFER_DST bit is set for staging
            if (!(imageCreateInfo.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
            {
                imageCreateInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            }
            VK_CHECK_RESULT(vkCreateImage(device->logicalDevice, &imageCreateInfo, nullptr, &image));

            vkGetImageMemoryRequirements(device->logicalDevice, image, &memReqs);

            memAllocInfo.allocationSize = memReqs.size;

            memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &deviceMemory));
            VK_CHECK_RESULT(vkBindImageMemory(device->logicalDevice, image, deviceMemory, 0));

            VkImageSubresourceRange subresourceRange = {};
            subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            subresourceRange.baseMipLevel = 0;
            subresourceRange.levelCount = mipLevels;
            subresourceRange.layerCount = 1;

            // Image barrier for optimal image (target)
            // Optimal image will be used as destination for the copy
            vks::tools::setImageLayout(
                copyCmd,
                image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                subresourceRange);

            // Copy mip levels from staging buffer
            vkCmdCopyBufferToImage(
                copyCmd,
                stagingBuffer,
                image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                static_cast<uint32_t>(bufferCopyRegions.size()),
                bufferCopyRegions.data()
            );

            // Change texture image layout to shader read after all mip levels have been copied
            this->imageLayout = imageLayout;
            vks::tools::setImageLayout(
                copyCmd,
                image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                imageLayout,
                subresourceRange);

            device->flushCommandBuffer(copyCmd, copyQueue);

            // Clean up staging resources
            vkFreeMemory(device->logicalDevice, stagingMemory, nullptr);
            vkDestroyBuffer(device->logicalDevice, stagingBuffer, nullptr);
        }
        else
        {
            // Prefer using optimal tiling, as linear tiling
            // may support only a small set of features
            // depending on implementation (e.g. no mip maps, only one layer, etc.)

            // Check if this support is supported for linear tiling
            assert(formatProperties.linearTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);

            VkImage mappableImage;
            VkDeviceMemory mappableMemory;

            VkImageCreateInfo imageCreateInfo = vks::initializers::imageCreateInfo();
            imageCreateInfo.imageType = VK_IMAGE_TYPE_3D;
            imageCreateInfo.format = format;
            imageCreateInfo.extent = { width, height, depth };
            imageCreateInfo.mipLevels = 1;
            imageCreateInfo.arrayLayers = 1;
            imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageCreateInfo.tiling = VK_IMAGE_TILING_LINEAR;
            imageCreateInfo.usage = imageUsageFlags;
            imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

            // Load mip map level 0 to linear tiling image
            VK_CHECK_RESULT(vkCreateImage(device->logicalDevice, &imageCreateInfo, nullptr, &mappableImage));

            // Get memory requirements for this image
            // like size and alignment
            vkGetImageMemoryRequirements(device->logicalDevice, mappableImage, &memReqs);
            // Set memory allocation size to required memory size
            memAllocInfo.allocationSize = memReqs.size;

            // Get memory type that can be mapped to host memory
            memAllocInfo.memoryTypeIndex = device->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

            // Allocate host memory
            VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &mappableMemory));

            // Bind allocated image for use
            VK_CHECK_RESULT(vkBindImageMemory(device->logicalDevice, mappableImage, mappableMemory, 0));

            // Get sub resource layout
            // Mip map count, array layer, etc.
            VkImageSubresource subRes = {};
            subRes.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            subRes.mipLevel = 0;

            VkSubresourceLayout subResLayout;
            void *data;

            // Get sub resources layout
            // Includes row pitch, size offsets, etc.
            vkGetImageSubresourceLayout(device->logicalDevice, mappableImage, &subRes, &subResLayout);

            // Map image memory
            VK_CHECK_RESULT(vkMapMemory(device->logicalDevice, mappableMemory, 0, memReqs.size, 0, &data));

            // Copy image data into memory
            memcpy(data, imgData, size);

            vkUnmapMemory(device->logicalDevice, mappableMemory);

            // Linear tiled images don't need to be staged
            // and can be directly used as textures
            image = mappableImage;
            deviceMemory = mappableMemory;
            this->imageLayout = imageLayout;

            // Setup image memory barrier
            vks::tools::setImageLayout(copyCmd, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, imageLayout);

            device->flushCommandBuffer(copyCmd, copyQueue);
        }

        // Create a defaultsampler
        VkSamplerCreateInfo samplerCreateInfo = {};
        samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
        samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
        samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerCreateInfo.addressModeU = m_addressMode;
        samplerCreateInfo.addressModeV = m_addressMode;
        samplerCreateInfo.addressModeW = m_addressMode;
        samplerCreateInfo.mipLodBias = 0.0f;
        samplerCreateInfo.compareOp = VK_COMPARE_OP_NEVER;
        samplerCreateInfo.minLod = 0.0f;
        // Max level-of-detail should match mip level count
        samplerCreateInfo.maxLod = (useStaging) ? (float)mipLevels : 0.0f;
        // Only enable anisotropic filtering if enabled on the devicec
        samplerCreateInfo.maxAnisotropy = device->enabledFeatures.samplerAnisotropy ? device->properties.limits.maxSamplerAnisotropy : 1.0f;
        samplerCreateInfo.anisotropyEnable = device->enabledFeatures.samplerAnisotropy;
        samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        VK_CHECK_RESULT(vkCreateSampler(device->logicalDevice, &samplerCreateInfo, nullptr, &sampler));

        // Create image view
        // Textures are not directly accessed by the shaders and
        // are abstracted by image views containing additional
        // information and sub resource ranges
        VkImageViewCreateInfo viewCreateInfo = {};
        viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
        viewCreateInfo.format = format;
        viewCreateInfo.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
        viewCreateInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        // Linear tiling usually won't support mip maps
        // Only set mip map count if optimal tiling is used
        viewCreateInfo.subresourceRange.levelCount = (useStaging) ? mipLevels : 1;
        viewCreateInfo.image = image;
        VK_CHECK_RESULT(vkCreateImageView(device->logicalDevice, &viewCreateInfo, nullptr, &view));

        // Update descriptor image info member that can be used for setting up descriptor sets
        updateDescriptor();
    }

};

}


#endif // VULKAN_BASICENGINE_TEXTURE_H
