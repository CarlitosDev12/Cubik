#initializer / entry point para Android NDK
#include <jni.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>
#include <vulkan/vulkan.hpp>
#include <iostream>
#include <vector>

struct vkEngine {
    struct android_app* app = nullptr;
    vk::Instance vkInstance;
    vk::SurfaceKHR vkSurface;
    vk::PhysicalDevice vkPhysicalDevice;
    vk::Device vkDevice;
    vk::Queue vkQueue;
    vk::CommandPool vkCommandPool;
    vk::CommandBuffer vkCommandBuffer;
    vk::SwapchainKHR vkSwapchain;
    std::vector<vk::Image> vkSwapchainImages;
    std::vector<vk::ImageView> vkSwapchainImageViews;
};

bool vkInit(struct vkEngine* vkEng) {
    // 1. Obtener las extensiones requeridas por Android para Vulkan
    std::vector<const char*> vkSurfaceExtensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_ANDROID_SURFACE_EXTENSION_NAME
    };

    vk::ApplicationInfo vkAppInfo("CubikAndroid", VK_MAKE_VERSION(1, 0, 0), "vkEngine", VK_MAKE_VERSION(1, 0, 0), VK_API_VERSION_1_0);
    vk::InstanceCreateInfo vkInstanceCreateInfo({}, &vkAppInfo, 0, nullptr, static_cast<uint32_t>(vkSurfaceExtensions.size()), vkSurfaceExtensions.data());
    
    try {
        vkEng->vkInstance = vk::createInstance(vkInstanceCreateInfo);
    } catch (const vk::SystemError& e) {
        std::cerr << "Error al crear la instancia de Vulkan: " << e.what() << "\n";
        return false;
    }
    std::cout << "1. Instancia de Vulkan creada (GPU física Android).\n";

    // 2. Esperar a que la ventana nativa de Android esté lista
    while (!vkEng->app->window) {
        int events;
        struct android_poll_source* source;
        while (ALooper_pollAll(0, nullptr, &events, (void**)&source) >= 0) {
            if (source) {
                source->process(vkEng->app, source);
            }
            if (vkEng->app->destroyRequested) {
                return false;
            }
        }
    }

    // 3. Crear la superficie de Vulkan usando ANativeWindow
    vk::AndroidSurfaceCreateInfoKHR androidSurfaceCreateInfo({}, vkEng->app->window);
    try {
        vkEng->vkSurface = vkEng->vkInstance.createAndroidSurfaceKHR(androidSurfaceCreateInfo);
    } catch (const vk::SystemError& e) {
        std::cerr << "Error al crear AndroidSurfaceKHR: " << e.what() << "\n";
        return false;
    }
    std::cout << "2. VkSurfaceKHR de Android creada con éxito.\n";

    // 4. Seleccionar el Dispositivo Físico (¡Aquí sí usará tu Mali-G52 real!)
    auto physicalDevices = vkEng->vkInstance.enumeratePhysicalDevices();
    if (physicalDevices.empty()) {
        std::cerr << "Error: No se encontraron GPUs con soporte Vulkan.\n";
        return false;
    }

    int selectedQueueFamily = -1;
    for (const auto& pd : physicalDevices) {
        auto queueFamilies = pd.getQueueFamilyProperties();
        for (uint32_t i = 0; i < queueFamilies.size(); ++i) {
            if ((queueFamilies[i].queueFlags & vk::QueueFlagBits::eGraphics) &&
                pd.getSurfaceSupportKHR(i, vkEng->vkSurface)) {
                vkEng->vkPhysicalDevice = pd;
                selectedQueueFamily = i;
                break;
            }
        }
        if (selectedQueueFamily != -1) break;
    }

    if (selectedQueueFamily == -1) {
        std::cerr << "Error: No se encontró una cola compatible con gráficos y superficie.\n";
        return false;
    }
    std::cout << "3. GPU física (Mali) seleccionada correctamente.\n";

    // 5. Crear el Dispositivo Lógico
    float queuePriority = 1.0f;
    vk::DeviceQueueCreateInfo queueInfo({}, static_cast<uint32_t>(selectedQueueFamily), 1, &queuePriority);

    std::vector<const char*> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    vk::DeviceCreateInfo deviceInfo({}, 1, &queueInfo, 0, nullptr, static_cast<uint32_t>(deviceExtensions.size()), deviceExtensions.data());
    
    vkEng->vkDevice = vkEng->vkPhysicalDevice.createDevice(deviceInfo);
    vkEng->vkQueue = vkEng->vkDevice.getQueue(selectedQueueFamily, 0);
    std::cout << "4. Dispositivo lógico creado.\n";

    // 6. Crear la Swapchain
    vk::SurfaceCapabilitiesKHR capabilities = vkEng->vkPhysicalDevice.getSurfaceCapabilitiesKHR(vkEng->vkSurface);
    std::vector<vk::SurfaceFormatKHR> formats = vkEng->vkPhysicalDevice.getSurfaceFormatsKHR(vkEng->vkSurface);
    vk::SurfaceFormatKHR surfaceFormat = formats[0];

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    vk::SwapchainCreateInfoKHR swapchainInfo(
        {}, vkEng->vkSurface, imageCount, surfaceFormat.format, surfaceFormat.colorSpace,
        capabilities.currentExtent, 1, vk::ImageUsageFlagBits::eColorAttachment,
        vk::SharingMode::eExclusive, 0, nullptr, capabilities.currentTransform,
        vk::CompositeAlphaFlagBitsKHR::eInherit, vk::PresentModeKHR::eFifo, VK_TRUE, nullptr
    );

    vkEng->vkSwapchain = vkEng->vkDevice.createSwapchainKHR(swapchainInfo);
    vkEng->vkSwapchainImages = vkEng->vkDevice.getSwapchainImagesKHR(vkEng->vkSwapchain);
    std::cout << "5. Swapchain creada con " << vkEng->vkSwapchainImages.size() << " imágenes.\n";

    // 7. Image Views
    vkEng->vkSwapchainImageViews.resize(vkEng->vkSwapchainImages.size());
    for (size_t i = 0; i < vkEng->vkSwapchainImages.size(); i++) {
        vk::ImageViewCreateInfo colorImageViewInfo(
            {}, vkEng->vkSwapchainImages[i], vk::ImageViewType::e2D, surfaceFormat.format,
            {vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity},
            {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
        );
        vkEng->vkSwapchainImageViews[i] = vkEng->vkDevice.createImageView(colorImageViewInfo);
    }
    std::cout << "6. Image Views creadas.\n";

    // 8. Command Pool y Buffer
    vk::CommandPoolCreateInfo poolInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer, static_cast<uint32_t>(selectedQueueFamily));
    vkEng->vkCommandPool = vkEng->vkDevice.createCommandPool(poolInfo);

    vk::CommandBufferAllocateInfo allocInfo(vkEng->vkCommandPool, vk::CommandBufferLevel::ePrimary, 1);
    vkEng->vkCommandBuffer = vkEng->vkDevice.allocateCommandBuffers(allocInfo)[0];
    std::cout << "7. Command Buffer listo.\n";

    return true;
}

void vkCleanup(struct vkEngine* vkEng) {
    if (vkEng->vkDevice) {
        vkEng->vkDevice.waitIdle();
        if (vkEng->vkCommandPool) vkEng->vkDevice.destroyCommandPool(vkEng->vkCommandPool);
        for (auto iv : vkEng->vkSwapchainImageViews) vkEng->vkDevice.destroyImageView(iv);
        if (vkEng->vkSwapchain) vkEng->vkDevice.destroySwapchainKHR(vkEng->vkSwapchain);
        vkEng->vkDevice.destroy();
    }
    if (vkEng->vkSurface) vkEng->vkInstance.destroySurfaceKHR(vkEng->vkSurface);
    if (vkEng->vkInstance) vkEng->vkInstance.destroy();
    std::cout << "Recursos liberados correctamente.\n";
}

// Punto de entrada principal requerido por app_glue en Android
void android_main(struct android_app* app) {
    // Asegurar que glue esté vinculado
    app_dummy();

    vkEngine engine{};
    engine.app = app;

    std::cout << "Iniciando motor gráfico con android_native_app_glue...\n";

    if (!vkInit(&engine)) {
        std::cerr << "Fallo la inicialización de Vulkan nativa.\n";
        return;
    }

    bool engineRunning = true;
    while (engineRunning) {
        int events;
        struct android_poll_source* source;
        
        // Procesar eventos de Android (toques, ciclo de vida de la Activity, etc.)
        while (ALooper_pollAll(0, nullptr, &events, (void**)&source) >= 0) {
            if (source) {
                source->process(app, source);
            }
            if (app->destroyRequested) {
                engineRunning = false;
                break;
            }
        }
    }

    vkCleanup(&engine);
}
