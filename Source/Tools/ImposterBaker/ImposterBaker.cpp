#include "ImposterBaker.h"
#include "LTreeGenerator.h"

#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/Core/ProcessUtils.h>
#include <Urho3D/Core/StringUtils.h>
#include <Urho3D/Engine/Engine.h>
#include <Urho3D/Engine/EngineDefs.h>
#include <Urho3D/Graphics/Camera.h>
#include <Urho3D/Graphics/Graphics.h>
#include <Urho3D/Graphics/Model.h>
#include <Urho3D/Graphics/Octree.h>
#include <Urho3D/Graphics/Renderer.h>
#include <Urho3D/Graphics/StaticModel.h>
#include <Urho3D/Graphics/Technique.h>
#include <Urho3D/Graphics/Zone.h>
#include <Urho3D/Graphics/Material.h>
#include <Urho3D/Graphics/Viewport.h>
#include <Urho3D/GraphicsAPI/Texture2D.h>
#include <Urho3D/Math/BoundingBox.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/Resource/Image.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>

#include <SDL/SDL.h>
#include <cstdlib>

#ifdef URHO3D_VULKAN
#include <Urho3D/GraphicsAPI/Vulkan/VulkanGraphicsImpl.h>
#endif

URHO3D_DEFINE_APPLICATION_MAIN(ImposterBaker);

ImposterBaker::ImposterBaker(Context* context) :
    Application(context)
{
}

bool ImposterBaker::ParseArgs()
{
    const Vector<String>& args = GetArguments();

    for (unsigned i = 0; i < args.Size(); ++i)
    {
        String arg = args[i].ToLower();

        if (arg == "-model" && i + 1 < args.Size())
            modelPath_ = args[++i];
        else if (arg == "-material" && i + 1 < args.Size())
            materialPath_ = args[++i];
        else if (arg == "-views" && i + 1 < args.Size())
            numViews_ = Max(1, atoi(args[++i].CString()));
        else if (arg == "-size" && i + 1 < args.Size())
        {
            String sizeStr = args[++i];
            unsigned xPos = sizeStr.Find('x');
            if (xPos != String::NPOS)
            {
                outputWidth_ = Max(1, atoi(sizeStr.Substring(0, xPos).CString()));
                outputHeight_ = Max(1, atoi(sizeStr.Substring(xPos + 1).CString()));
            }
        }
        else if (arg == "-output" && i + 1 < args.Size())
            outputPrefix_ = args[++i];
        else if (arg == "-ltree" && i + 1 < args.Size())
            ltreeSpecies_ = args[++i].ToLower();
        else if (arg == "-savemodel" && i + 1 < args.Size())
            saveModelPath_ = args[++i];
    }

    if (modelPath_.Empty() && ltreeSpecies_.Empty())
    {
        PrintLine("Usage: ImposterBaker -model <path> [-material <path>] [-views N] [-size WxH] [-output prefix]");
        PrintLine("       ImposterBaker -ltree <oak|pine|eucalyptus> [-views N] [-size WxH] [-output prefix]");
        PrintLine("");
        PrintLine("  -model     Model file relative to resource dirs");
        PrintLine("  -ltree     Generate L-system tree (oak, pine, eucalyptus)");
        PrintLine("  -material  Material override (model mode only)");
        PrintLine("  -views     Number of snapshots around Y axis (default: 8)");
        PrintLine("  -size      Output image dimensions WxH (default: 512x1024)");
        PrintLine("  -output    Output filename prefix (default: imposter_)");
        return false;
    }

    return true;
}

void ImposterBaker::Setup()
{
    if (!ParseArgs())
    {
        exitCode_ = EXIT_FAILURE;
        return;
    }

    engineParameters_[EP_WINDOW_TITLE] = "ImposterBaker";
    engineParameters_[EP_WINDOW_WIDTH] = outputWidth_;
    engineParameters_[EP_WINDOW_HEIGHT] = outputHeight_;
    engineParameters_[EP_FULL_SCREEN] = false;
    engineParameters_[EP_LOG_NAME] = "ImposterBaker.log";
    engineParameters_[EP_RESOURCE_PATHS] = "CoreData;Data";
    engineParameters_[EP_SOUND] = false;
}

void ImposterBaker::Start()
{
    // Keep window visible during baking — hidden windows produce blank captures
    // on many GPU drivers. Window is minimized instead to stay out of the way.
    auto* graphics = GetSubsystem<Graphics>();
    SDL_MinimizeWindow(graphics->GetWindow());

    auto* cache = GetSubsystem<ResourceCache>();

    SharedPtr<Model> model;
    SharedPtr<Material> barkMat;
    SharedPtr<Material> leafMat;
    bool isLTree = !ltreeSpecies_.Empty();

    if (isLTree)
    {
        // Generate L-system tree model + materials
        model = GenerateLTree(barkMat, leafMat);
        if (!model)
        {
            PrintLine("ERROR: Unknown L-system species: " + ltreeSpecies_);
            PrintLine("  Valid species: oak, pine, eucalyptus");
            exitCode_ = EXIT_FAILURE;
            engine_->Exit();
            return;
        }
        PrintLine("Generated L-system tree: " + ltreeSpecies_);

        // Save model to disk if requested
        if (!saveModelPath_.Empty())
        {
            File outFile(context_, saveModelPath_, FILE_WRITE);
            if (model->Save(outFile))
                PrintLine("Saved model: " + saveModelPath_);
            else
                PrintLine("ERROR: Failed to save model: " + saveModelPath_);
            engine_->Exit();
            return;
        }
    }
    else
    {
        // Load model from file
        model = cache->GetResource<Model>(modelPath_);
        if (!model)
        {
            PrintLine("ERROR: Failed to load model: " + modelPath_);
            exitCode_ = EXIT_FAILURE;
            engine_->Exit();
            return;
        }

        // Load optional material
        if (!materialPath_.Empty())
        {
            barkMat = cache->GetResource<Material>(materialPath_);
            if (!barkMat)
                PrintLine("WARNING: Failed to load material: " + materialPath_ + ", using default");
        }
    }

    // Create scene
    SharedPtr<Scene> scene(new Scene(context_));
    scene->CreateComponent<Octree>();

    // Zone — ambient-only lighting, transparent clear
    Node* zoneNode = scene->CreateChild("Zone");
    auto* zone = zoneNode->CreateComponent<Zone>();
    zone->SetBoundingBox(BoundingBox(-1000.0f, 1000.0f));
    zone->SetAmbientColor(Color(0.8f, 0.8f, 0.8f));
    zone->SetFogColor(Color(0.0f, 0.0f, 0.0f, 0.0f));

    // Model node at origin
    Node* modelNode = scene->CreateChild("Model");
    auto* staticModel = modelNode->CreateComponent<StaticModel>();
    staticModel->SetModel(model, true);  // allow oversized — L-system trees can exceed 10 units
    if (barkMat)
        staticModel->SetMaterial(0, barkMat);
    if (leafMat)
        staticModel->SetMaterial(1, leafMat);

    // Compute camera orbit from bounding box
    BoundingBox bbox = model->GetBoundingBox();
    Vector3 center = bbox.Center();
    float diagonal = (bbox.max_ - bbox.min_).Length();

    // Camera node
    Node* cameraNode = scene->CreateChild("Camera");
    auto* camera = cameraNode->CreateComponent<Camera>();
    float fov = camera->GetFov();

    // Distance to fit the full model in frame
    float halfAngle = fov * 0.5f * M_DEGTORAD;
    float distance = (diagonal * 0.5f) / tanf(halfAngle);
    distance *= 1.1f;  // 10% margin

    // Set up viewport with transparent clear
    auto* renderer = GetSubsystem<Renderer>();
    SharedPtr<Viewport> viewport(new Viewport(context_, scene, camera));
    renderer->SetViewport(0, viewport);
    // Clear color is driven by the Zone's fog color (transparent black)

    // Ensure output directory exists
    String outputDir = GetParentPath(outputPrefix_);
    if (!outputDir.Empty())
    {
        auto* fs = GetSubsystem<FileSystem>();
        if (!fs->DirExists(outputDir))
        {
            PrintLine("Output directory does not exist: " + outputDir);
            PrintLine("Create it? [y/N] ");
            char response = (char)getchar();
            if (response != 'y' && response != 'Y')
            {
                PrintLine("Aborted.");
                engine_->Exit();
                return;
            }
            if (!fs->CreateDir(outputDir))
            {
                PrintLine("ERROR: Failed to create directory: " + outputDir);
                exitCode_ = EXIT_FAILURE;
                engine_->Exit();
                return;
            }
            PrintLine("Created: " + outputDir);
        }
    }

    PrintLine("ImposterBaker: " + modelPath_);
    PrintLine("  Views: " + String(numViews_) + "  Size: " + String(outputWidth_) + "x" + String(outputHeight_));
    PrintLine("  BBox center: " + center.ToString() + "  Distance: " + String(distance));

    // Warm-up frame — ensures scene, materials, and GPU state are fully initialized
    engine_->RunFrame();

    // Capture loop
    float angleStep = 360.0f / (float)numViews_;

    for (int i = 0; i < numViews_; ++i)
    {
        float angle = i * angleStep * M_DEGTORAD;
        float cx = center.x_ + distance * sinf(angle);
        float cz = center.z_ + distance * cosf(angle);
        float cy = center.y_;

        cameraNode->SetPosition(Vector3(cx, cy, cz));
        cameraNode->LookAt(center);

        // Render two frames — first updates transforms, second produces final image
        engine_->RunFrame();
        engine_->RunFrame();

        // Capture RGBA screenshot
        Image image(context_);
        if (TakeScreenShotRGBA(image))
        {
            String filename = outputPrefix_ + String(i) + ".png";
            if (image.SavePNG(filename))
                PrintLine("  [" + String(i + 1) + "/" + String(numViews_) + "] Saved: " + filename);
            else
                PrintLine("  [" + String(i + 1) + "/" + String(numViews_) + "] ERROR saving: " + filename);
        }
        else
        {
            PrintLine("  [" + String(i + 1) + "/" + String(numViews_) + "] ERROR capturing screenshot");
        }
    }

    PrintLine("Done.");
    engine_->Exit();
}

SharedPtr<Model> ImposterBaker::GenerateLTree(SharedPtr<Material>& barkMat, SharedPtr<Material>& leafMat)
{
    TreePreset preset;
    String barkTexPath = "Textures/Bark_NormalTree.png";
    String leafTexPath = "Textures/Leaves_NormalTree_C.png";

    if (ltreeSpecies_ == "oak")
        preset = LTreeGenerator::Oak();
    else if (ltreeSpecies_ == "pine")
    {
        preset = LTreeGenerator::Pine();
        leafTexPath = "Textures/Leaf_Pine_C.png";
    }
    else if (ltreeSpecies_ == "eucalyptus")
    {
        preset = LTreeGenerator::Eucalyptus();
        barkTexPath = "Textures/Bark_TwistedTree.png";
    }
    else if (ltreeSpecies_ == "acacia")
    {
        preset = LTreeGenerator::Acacia();
        barkTexPath = "Textures/Bark_DeadTree.png";
    }
    else if (ltreeSpecies_ == "willow")
        preset = LTreeGenerator::Willow();
    else if (ltreeSpecies_ == "sheoak")
    {
        preset = LTreeGenerator::SheOak();
        barkTexPath = "Textures/Bark_TwistedTree.png";
        leafTexPath = "Textures/Leaf_Pine_C.png";
    }
    else if (ltreeSpecies_ == "fishingrod")
    {
        preset = LTreeGenerator::FishingRod();
    }
    else
        return nullptr;

    SharedPtr<Model> model = LTreeGenerator::Generate(context_, preset);
    if (!model)
        return nullptr;

    auto* cache = GetSubsystem<ResourceCache>();
    auto* diffTech = cache->GetResource<Technique>("Techniques/Diff.xml");

    barkMat = new Material(context_);
    barkMat->SetTechnique(0, diffTech);
    barkMat->SetTexture(TU_DIFFUSE, cache->GetResource<Texture2D>(barkTexPath));
    barkMat->SetShaderParameter("MatDiffColor", Color::WHITE);
    barkMat->SetShaderParameter("MatSpecColor", Vector4(0.15f, 0.15f, 0.15f, 8.0f));

    leafMat = new Material(context_);
    leafMat->SetTechnique(0, diffTech);
    leafMat->SetTexture(TU_DIFFUSE, cache->GetResource<Texture2D>(leafTexPath));
    leafMat->SetShaderParameter("MatDiffColor", Color::WHITE);
    leafMat->SetShaderParameter("MatSpecColor", Vector4(0.1f, 0.1f, 0.1f, 4.0f));

    return model;
}

void ImposterBaker::Stop()
{
}

bool ImposterBaker::TakeScreenShotRGBA(Image& destImage)
{
    auto* graphics = GetSubsystem<Graphics>();
    if (!graphics)
        return false;

#ifdef URHO3D_VULKAN
    // Vulkan path: same as TakeScreenShot_Vulkan but preserving 4-component RGBA
    VulkanGraphicsImpl* vkImpl = graphics->GetImpl_Vulkan();
    if (!vkImpl)
        return false;

    VkDevice device = vkImpl->GetDevice();
    VkQueue queue = vkImpl->GetGraphicsQueue();
    VmaAllocator allocator = vkImpl->GetAllocator();

    if (!device || !queue || !allocator)
        return false;

    vkDeviceWaitIdle(device);

    const Vector<VkImage>& swapchainImages = vkImpl->GetSwapchainImages();
    uint32_t imageIndex = vkImpl->GetCurrentImageIndex();
    if (imageIndex >= swapchainImages.Size())
        return false;

    VkImage srcImage = swapchainImages[imageIndex];
    VkFormat format = vkImpl->GetSwapchainFormat();

    unsigned bpp = 4;
    int w = outputWidth_;
    int h = outputHeight_;
    VkDeviceSize bufferSize = (VkDeviceSize)w * h * bpp;

    // Create staging buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;
    VmaAllocationInfo stagingAllocInfo;

    if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &stagingBuffer, &stagingAllocation, &stagingAllocInfo) != VK_SUCCESS)
        return false;

    // One-shot command buffer
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = vkImpl->GetCommandPool();
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuffer;
    if (vkAllocateCommandBuffers(device, &cmdAllocInfo, &cmdBuffer) != VK_SUCCESS)
    {
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
        return false;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmdBuffer, &beginInfo);

    // PRESENT_SRC -> TRANSFER_SRC
    vkImpl->TransitionImageLayout(cmdBuffer, srcImage, format,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 1);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {(uint32_t)w, (uint32_t)h, 1};

    vkCmdCopyImageToBuffer(cmdBuffer, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           stagingBuffer, 1, &region);

    // TRANSFER_SRC -> PRESENT_SRC
    vkImpl->TransitionImageLayout(cmdBuffer, srcImage, format,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 1);

    vkEndCommandBuffer(cmdBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;

    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    // Read pixels — 4-component RGBA preserving alpha
    destImage.SetSize(w, h, 4);
    unsigned char* src = static_cast<unsigned char*>(stagingAllocInfo.pMappedData);
    unsigned char* dest = destImage.GetData();

    bool isBGR = (format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB);
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            unsigned idx = (y * w + x) * bpp;
            unsigned outIdx = (y * w + x) * 4;
            if (isBGR)
            {
                dest[outIdx + 0] = src[idx + 2];  // R from B
                dest[outIdx + 1] = src[idx + 1];  // G
                dest[outIdx + 2] = src[idx + 0];  // B from R
            }
            else
            {
                dest[outIdx + 0] = src[idx + 0];
                dest[outIdx + 1] = src[idx + 1];
                dest[outIdx + 2] = src[idx + 2];
            }
            dest[outIdx + 3] = src[idx + 3];      // Alpha preserved
        }
    }

    vkFreeCommandBuffers(device, vkImpl->GetCommandPool(), 1, &cmdBuffer);
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
    return true;

#else
    // OpenGL fallback: use standard 3-component screenshot, then expand to 4
    // Alpha won't be meaningful from GL swapchain, but tool structure is preserved
    Image rgbImage(context_);
    if (!graphics->TakeScreenShot(rgbImage))
        return false;

    int w = rgbImage.GetWidth();
    int h = rgbImage.GetHeight();
    destImage.SetSize(w, h, 4);
    unsigned char* src = rgbImage.GetData();
    unsigned char* dest = destImage.GetData();

    for (int i = 0; i < w * h; ++i)
    {
        dest[i * 4 + 0] = src[i * 3 + 0];
        dest[i * 4 + 1] = src[i * 3 + 1];
        dest[i * 4 + 2] = src[i * 3 + 2];
        // Black pixels = background = transparent
        if (src[i * 3 + 0] == 0 && src[i * 3 + 1] == 0 && src[i * 3 + 2] == 0)
            dest[i * 4 + 3] = 0;
        else
            dest[i * 4 + 3] = 255;
    }
    return true;
#endif
}
