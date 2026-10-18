/* Shared by standalone Vulkan probes. Enumerate portability ICDs such as
 * MoltenVK without requiring these extensions from native Vulkan drivers. */
#ifndef GOW3_VULKAN_PORTABILITY_H
#define GOW3_VULKAN_PORTABILITY_H
#include <vulkan/vulkan.h>
#include <stdlib.h>
#include <string.h>

static inline VkResult gow3_vk_instance(const VkApplicationInfo *app, VkInstance *instance) {
    uint32_t count=0;
    VkResult result=vkEnumerateInstanceExtensionProperties(NULL,&count,NULL);
    if (result!=VK_SUCCESS) return result;
    VkExtensionProperties *props=calloc(count ? count : 1,sizeof(*props));
    if (!props) return VK_ERROR_OUT_OF_HOST_MEMORY;
    result=vkEnumerateInstanceExtensionProperties(NULL,&count,props);
    const char *extensions[2];
    uint32_t enabled=0;
    VkInstanceCreateFlags flags=0;
    if (result==VK_SUCCESS) for (uint32_t i=0;i<count;++i) {
        if (!strcmp(props[i].extensionName,VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
            extensions[enabled++]=VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
            flags|=VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        } else if (!strcmp(props[i].extensionName,VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME)) {
            extensions[enabled++]=VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
        }
    }
    free(props);
    if (result!=VK_SUCCESS) return result;
    const VkInstanceCreateInfo info={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .flags=flags,.pApplicationInfo=app,.enabledExtensionCount=enabled,
        .ppEnabledExtensionNames=extensions};
    return vkCreateInstance(&info,NULL,instance);
}

static inline VkResult gow3_vk_portability_device(VkPhysicalDevice physical, int *required) {
    uint32_t count=0;
    *required=0;
    VkResult result=vkEnumerateDeviceExtensionProperties(physical,NULL,&count,NULL);
    if (result!=VK_SUCCESS) return result;
    VkExtensionProperties *props=calloc(count ? count : 1,sizeof(*props));
    if (!props) return VK_ERROR_OUT_OF_HOST_MEMORY;
    result=vkEnumerateDeviceExtensionProperties(physical,NULL,&count,props);
    if (result==VK_SUCCESS) for (uint32_t i=0;i<count;++i)
        if (!strcmp(props[i].extensionName,"VK_KHR_portability_subset")) *required=1;
    free(props);
    return result;
}
#endif
