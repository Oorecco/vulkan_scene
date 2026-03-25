// VulkanPipeline.cpp — Pipeline and render pass creation. Heavy lifting.
#include "VulkanPipeline.h"
#include <fstream>
#include <stdexcept>
#include <shaderc/shaderc.hpp>  // from Vulkan SDK

VulkanPipeline::~VulkanPipeline() {}

// ── GLSL → SPIR-V compilation via shaderc ────────────────────────────────
std::vector<uint32_t> VulkanPipeline::compileSPIRV(
    const std::string& glslPath,
    VkShaderStageFlagBits stage) const
{
    // Read GLSL source
    std::ifstream file(glslPath);
    if (!file.is_open())
        throw std::runtime_error("Cannot open shader: " + glslPath);
    std::string src((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());

    shaderc_shader_kind kind;
    switch (stage) {
        case VK_SHADER_STAGE_VERTEX_BIT:   kind = shaderc_vertex_shader;   break;
        case VK_SHADER_STAGE_FRAGMENT_BIT: kind = shaderc_fragment_shader; break;
        default: kind = shaderc_glsl_infer_from_source;
    }

    shaderc::Compiler compiler;
    shaderc::CompileOptions opts;
    opts.SetTargetEnvironment(shaderc_target_env_vulkan,
                              shaderc_env_version_vulkan_1_1);
    opts.SetOptimizationLevel(shaderc_optimization_level_performance);

    auto result = compiler.CompileGlslToSpv(src, kind,
        glslPath.c_str(), "main", opts);

    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        throw std::runtime_error("Shader compile error [" + glslPath + "]:\n"
            + result.GetErrorMessage());
    }

    return std::vector<uint32_t>(result.cbegin(), result.cend());
}

VkShaderModule VulkanPipeline::createModule(
    VkDevice device,
    const std::vector<uint32_t>& spv) const
{
    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = spv.size() * sizeof(uint32_t);
    ci.pCode    = spv.data();
    VkShaderModule mod;
    VK_CHECK(vkCreateShaderModule(device, &ci, nullptr, &mod));
    return mod;
}

// ── Public create / destroy ───────────────────────────────────────────────
void VulkanPipeline::create(const VulkanContext& ctx,
                             VkExtent2D swapExtent,
                             VkFormat swapFormat,
                             const std::string& shaderDir,
                             uint32_t shadowDim)
{
    m_ctx       = &ctx;
    m_shaderDir = shaderDir;
    m_shadowDim = shadowDim;

    createRenderPasses(ctx.device(), swapFormat);
    createDescriptorLayouts(ctx.device());
    createDescriptorPool(ctx.device());

    // Create shadow depth image + framebuffer
    m_shadowDepth = VulkanImage::createDepth(ctx, shadowDim, shadowDim);
    createShadowFramebuffer(ctx);

    // Compile and create all pipelines
    createShadowPipeline(ctx.device(), shaderDir);
    createScenePipeline(ctx.device(), swapExtent, shaderDir);
    createUIPipeline(ctx.device(), swapExtent, shaderDir);

    LOG_INFO("All pipelines created successfully.");
}

void VulkanPipeline::destroy(VkDevice device) {
    if (m_shadowFB)       vkDestroyFramebuffer(device, m_shadowFB, nullptr);
    m_shadowDepth.destroy(device);
    if (m_uiPipeline)     vkDestroyPipeline(device, m_uiPipeline, nullptr);
    if (m_shadowPipeline) vkDestroyPipeline(device, m_shadowPipeline, nullptr);
    if (m_scenePipeline)  vkDestroyPipeline(device, m_scenePipeline, nullptr);
    if (m_uiLayout)       vkDestroyPipelineLayout(device, m_uiLayout, nullptr);
    if (m_sceneLayout)    vkDestroyPipelineLayout(device, m_sceneLayout, nullptr);
    if (m_descPool)          vkDestroyDescriptorPool(device, m_descPool, nullptr);
    if (m_diffuseDescLayout) vkDestroyDescriptorSetLayout(device, m_diffuseDescLayout, nullptr);
    if (m_uiDescLayout)      vkDestroyDescriptorSetLayout(device, m_uiDescLayout, nullptr);
    if (m_sceneDescLayout)   vkDestroyDescriptorSetLayout(device, m_sceneDescLayout, nullptr);
    if (m_shadowRenderPass) vkDestroyRenderPass(device, m_shadowRenderPass, nullptr);
    if (m_mainRenderPass)   vkDestroyRenderPass(device, m_mainRenderPass, nullptr);
}

// ── Render passes ─────────────────────────────────────────────────────────
void VulkanPipeline::createRenderPasses(VkDevice device, VkFormat swapFmt) {
    // ── Main render pass (color + depth) ──────────────────────────────────
    {
        std::array<VkAttachmentDescription, 2> atts{};
        // Color
        atts[0].format         = swapFmt;
        atts[0].samples        = VK_SAMPLE_COUNT_1_BIT;
        atts[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        atts[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        atts[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        atts[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        atts[0].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        atts[0].finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        // Depth
        atts[1].format         = m_ctx->findDepthFormat();
        atts[1].samples        = VK_SAMPLE_COUNT_1_BIT;
        atts[1].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        atts[1].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        atts[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        atts[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        atts[1].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        atts[1].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkAttachmentReference depRef{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

        VkSubpassDescription sub{};
        sub.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount    = 1;
        sub.pColorAttachments       = &colRef;
        sub.pDepthStencilAttachment = &depRef;

        VkSubpassDependency dep{};
        dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass    = 0;
        dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rci{};
        rci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rci.attachmentCount = (uint32_t)atts.size();
        rci.pAttachments    = atts.data();
        rci.subpassCount    = 1;
        rci.pSubpasses      = &sub;
        rci.dependencyCount = 1;
        rci.pDependencies   = &dep;
        VK_CHECK(vkCreateRenderPass(device, &rci, nullptr, &m_mainRenderPass));
    }

    // ── Shadow render pass (depth only) ───────────────────────────────────
    {
        VkAttachmentDescription depAtt{};
        depAtt.format         = m_ctx->findDepthFormat();
        depAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
        depAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depAtt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        depAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        depAtt.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkAttachmentReference depRef{ 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
        VkSubpassDescription sub{};
        sub.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.pDepthStencilAttachment = &depRef;

        std::array<VkSubpassDependency, 2> deps{};
        deps[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass    = 0;
        deps[0].srcStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[0].dstStageMask  = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        deps[1].srcSubpass    = 0;
        deps[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask  = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        deps[1].dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo rci{};
        rci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rci.attachmentCount = 1;
        rci.pAttachments    = &depAtt;
        rci.subpassCount    = 1;
        rci.pSubpasses      = &sub;
        rci.dependencyCount = (uint32_t)deps.size();
        rci.pDependencies   = deps.data();
        VK_CHECK(vkCreateRenderPass(device, &rci, nullptr, &m_shadowRenderPass));
    }
}

// ── Descriptor layouts ────────────────────────────────────────────────────
void VulkanPipeline::createDescriptorLayouts(VkDevice device) {
    // Scene: binding 0 = UBO, binding 1 = shadow map sampler
    {
        std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
        bindings[0].binding         = 0;
        bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT |
                                      VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[1].binding         = 1;
        bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo ci{};
        ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        ci.bindingCount = (uint32_t)bindings.size();
        ci.pBindings    = bindings.data();
        VK_CHECK(vkCreateDescriptorSetLayout(device, &ci, nullptr, &m_sceneDescLayout));
    }
    // Diffuse texture (set 1): binding 0 = combined image sampler
    // Used for per-draw texture binding in WorldDecorations and future prop types.
    // Binding a 1x1 white texture = vertex color passthrough — no shader changes needed.
    {
        VkDescriptorSetLayoutBinding b{};
        b.binding         = 0;
        b.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.descriptorCount = 1;
        b.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo ci{};
        ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        ci.bindingCount = 1;
        ci.pBindings    = &b;
        VK_CHECK(vkCreateDescriptorSetLayout(device, &ci, nullptr, &m_diffuseDescLayout));
    }
    // UI: binding 0 = font atlas sampler
    {
        VkDescriptorSetLayoutBinding b{};
        b.binding         = 0;
        b.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.descriptorCount = 1;
        b.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo ci{};
        ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        ci.bindingCount = 1;
        ci.pBindings    = &b;
        VK_CHECK(vkCreateDescriptorSetLayout(device, &ci, nullptr, &m_uiDescLayout));
    }
}

void VulkanPipeline::createDescriptorPool(VkDevice device) {
    // Pool for: scene sets + UI sets + plenty of diffuse texture sets for world props.
    // 128 diffuse slots is generous for our prop count. If you add 200 unique prop
    // types, you'll need to bump this. You won't add 200 prop types. Probably.
    std::array<VkDescriptorPoolSize, 2> sizes{};
    sizes[0].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    sizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT * 2;
    sizes[1].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sizes[1].descriptorCount = MAX_FRAMES_IN_FLIGHT * 4 + 128; // +128 for diffuse sets

    VkDescriptorPoolCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    ci.poolSizeCount = (uint32_t)sizes.size();
    ci.pPoolSizes    = sizes.data();
    ci.maxSets       = MAX_FRAMES_IN_FLIGHT * 4 + 128;
    VK_CHECK(vkCreateDescriptorPool(device, &ci, nullptr, &m_descPool));
}

// ── Shadow framebuffer ─────────────────────────────────────────────────────
void VulkanPipeline::createShadowFramebuffer(const VulkanContext& ctx) {
    VkFramebufferCreateInfo ci{};
    ci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    ci.renderPass      = m_shadowRenderPass;
    ci.attachmentCount = 1;
    ci.pAttachments    = &m_shadowDepth.view;
    ci.width           = m_shadowDim;
    ci.height          = m_shadowDim;
    ci.layers          = 1;
    VK_CHECK(vkCreateFramebuffer(ctx.device(), &ci, nullptr, &m_shadowFB));
}

// ── Descriptor set allocation ─────────────────────────────────────────────
VkDescriptorSet VulkanPipeline::allocateSceneSet(
    VkDevice device,
    VkBuffer uboBuffer, VkDeviceSize uboSize,
    const VulkanImage& shadowMap, VkSampler shadowSampler) const
{
    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = m_descPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &m_sceneDescLayout;

    VkDescriptorSet set;
    VK_CHECK(vkAllocateDescriptorSets(device, &ai, &set));

    VkDescriptorBufferInfo bi{ uboBuffer, 0, uboSize };
    VkDescriptorImageInfo ii{
        shadowSampler, shadowMap.view,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
    };

    std::array<VkWriteDescriptorSet, 2> writes{};
    writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet          = set;
    writes[0].dstBinding      = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo     = &bi;
    writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet          = set;
    writes[1].dstBinding      = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo      = &ii;

    vkUpdateDescriptorSets(device, (uint32_t)writes.size(), writes.data(), 0, nullptr);
    return set;
}

VkDescriptorSet VulkanPipeline::allocateUISet(
    VkDevice device, VkImageView fontView, VkSampler fontSampler) const
{
    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = m_descPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &m_uiDescLayout;

    VkDescriptorSet set;
    VK_CHECK(vkAllocateDescriptorSets(device, &ai, &set));

    VkDescriptorImageInfo ii{
        fontSampler, fontView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
    VkWriteDescriptorSet w{};
    w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet          = set;
    w.dstBinding      = 0;
    w.descriptorCount = 1;
    w.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo      = &ii;
    vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
    return set;
}

// ── Pipeline helper: vertex input for scene meshes ────────────────────────
static VkPipelineVertexInputStateCreateInfo sceneVertexInput() {
    static VkVertexInputBindingDescription bind{};
    bind.binding   = 0;
    bind.stride    = sizeof(Vertex3D);
    bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    // pos, normal, color, uv — all four attributes declared; shader picks what it needs
    static std::array<VkVertexInputAttributeDescription, 4> attrs{};
    attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, pos) };
    attrs[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, normal) };
    attrs[2] = { 2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex3D, color) };
    attrs[3] = { 3, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(Vertex3D, uv) };

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount   = 1;
    vi.pVertexBindingDescriptions      = &bind;
    vi.vertexAttributeDescriptionCount = (uint32_t)attrs.size();
    vi.pVertexAttributeDescriptions    = attrs.data();
    return vi;
}

// ── Scene pipeline ────────────────────────────────────────────────────────
void VulkanPipeline::createScenePipeline(VkDevice device,
                                          VkExtent2D ext,
                                          const std::string& sd)
{
    auto vsSpv = compileSPIRV(sd + "/scene.vert", VK_SHADER_STAGE_VERTEX_BIT);
    auto fsSpv = compileSPIRV(sd + "/scene.frag", VK_SHADER_STAGE_FRAGMENT_BIT);
    VkShaderModule vsM = createModule(device, vsSpv);
    VkShaderModule fsM = createModule(device, fsSpv);

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vsM;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fsM;
    stages[1].pName  = "main";

    auto vi = sceneVertexInput();

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{ 0, 0, (float)ext.width, (float)ext.height, 0.0f, 1.0f };
    VkRect2D scissor{ {0,0}, ext };
    VkPipelineViewportStateCreateInfo vs{};
    vs.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vs.viewportCount = 1; vs.pViewports = &viewport;
    vs.scissorCount  = 1; vs.pScissors  = &scissor;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode             = VK_POLYGON_MODE_FILL;
    rs.cullMode                = VK_CULL_MODE_NONE; // double-sided (for ground)
    rs.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth               = 1.0f;
    rs.depthBiasEnable         = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable  = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp   = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState blendAtt{};
    blendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAtt.blendEnable    = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments    = &blendAtt;

    // Layout: descriptor set 0 + push constants
    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcr.offset     = 0;
    pcr.size       = sizeof(PushConst);

    // Two descriptor set layouts: set 0 = scene UBO+shadow, set 1 = diffuse texture
    std::array<VkDescriptorSetLayout, 2> sceneSets = {
        m_sceneDescLayout, m_diffuseDescLayout
    };
    VkPipelineLayoutCreateInfo lci{};
    lci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    lci.setLayoutCount         = (uint32_t)sceneSets.size();
    lci.pSetLayouts            = sceneSets.data();
    lci.pushConstantRangeCount = 1;
    lci.pPushConstantRanges    = &pcr;
    VK_CHECK(vkCreatePipelineLayout(device, &lci, nullptr, &m_sceneLayout));

    VkGraphicsPipelineCreateInfo pci{};
    pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount          = (uint32_t)stages.size();
    pci.pStages             = stages.data();
    pci.pVertexInputState   = &vi;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState      = &vs;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState   = &ms;
    pci.pDepthStencilState  = &ds;
    pci.pColorBlendState    = &cb;
    pci.layout              = m_sceneLayout;
    pci.renderPass          = m_mainRenderPass;
    pci.subpass             = 0;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pci,
        nullptr, &m_scenePipeline));

    vkDestroyShaderModule(device, vsM, nullptr);
    vkDestroyShaderModule(device, fsM, nullptr);
}

// ── Shadow pipeline ───────────────────────────────────────────────────────
void VulkanPipeline::createShadowPipeline(VkDevice device,
                                           const std::string& sd)
{
    auto vsSpv = compileSPIRV(sd + "/shadow.vert", VK_SHADER_STAGE_VERTEX_BIT);
    auto fsSpv = compileSPIRV(sd + "/shadow.frag", VK_SHADER_STAGE_FRAGMENT_BIT);
    VkShaderModule vsM = createModule(device, vsSpv);
    VkShaderModule fsM = createModule(device, fsSpv);

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vsM, "main" };
    stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fsM, "main" };

    auto vi = sceneVertexInput(); // same vertex layout

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport vp{ 0, 0, (float)m_shadowDim, (float)m_shadowDim, 0.0f, 1.0f };
    VkRect2D   sc{ {0,0}, {m_shadowDim, m_shadowDim} };
    VkPipelineViewportStateCreateInfo vs{};
    vs.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vs.viewportCount = 1; vs.pViewports = &vp;
    vs.scissorCount  = 1; vs.pScissors  = &sc;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType            = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode      = VK_POLYGON_MODE_FILL;
    rs.cullMode         = VK_CULL_MODE_BACK_BIT;
    rs.frontFace        = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth        = 1.0f;
    rs.depthBiasEnable  = VK_TRUE;
    rs.depthBiasConstantFactor = 1.25f;
    rs.depthBiasSlopeFactor    = 1.75f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable  = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 0; // no color attachment in shadow pass

    VkPushConstantRange pcr{ VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConst) };
    VkPipelineLayoutCreateInfo lci{};
    lci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    lci.setLayoutCount         = 1;
    lci.pSetLayouts            = &m_sceneDescLayout; // same UBO layout for lightVP
    lci.pushConstantRangeCount = 1;
    lci.pPushConstantRanges    = &pcr;
    // Reuse m_sceneLayout for the shadow pass (same push constant + UBO)
    // (Already created in createScenePipeline which must run first)

    VkGraphicsPipelineCreateInfo pci{};
    pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount          = (uint32_t)stages.size();
    pci.pStages             = stages.data();
    pci.pVertexInputState   = &vi;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState      = &vs;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState   = &ms;
    pci.pDepthStencilState  = &ds;
    pci.pColorBlendState    = &cb;
    pci.layout              = m_sceneLayout; // shared layout
    pci.renderPass          = m_shadowRenderPass;
    pci.subpass             = 0;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pci,
        nullptr, &m_shadowPipeline));

    vkDestroyShaderModule(device, vsM, nullptr);
    vkDestroyShaderModule(device, fsM, nullptr);
}

// ── UI pipeline ───────────────────────────────────────────────────────────
void VulkanPipeline::createUIPipeline(VkDevice device,
                                       VkExtent2D ext,
                                       const std::string& sd)
{
    auto vsSpv = compileSPIRV(sd + "/ui.vert", VK_SHADER_STAGE_VERTEX_BIT);
    auto fsSpv = compileSPIRV(sd + "/ui.frag", VK_SHADER_STAGE_FRAGMENT_BIT);
    VkShaderModule vsM = createModule(device, vsSpv);
    VkShaderModule fsM = createModule(device, fsSpv);

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vsM, "main" };
    stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fsM, "main" };

    // UI vertex input
    static VkVertexInputBindingDescription uiBind{};
    uiBind.binding   = 0;
    uiBind.stride    = sizeof(VertexUI);
    uiBind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    static std::array<VkVertexInputAttributeDescription, 3> uiAttrs{};
    uiAttrs[0] = { 0, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(VertexUI, pos) };
    uiAttrs[1] = { 1, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(VertexUI, uv) };
    uiAttrs[2] = { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VertexUI, color) };

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount   = 1;
    vi.pVertexBindingDescriptions      = &uiBind;
    vi.vertexAttributeDescriptionCount = (uint32_t)uiAttrs.size();
    vi.pVertexAttributeDescriptions    = uiAttrs.data();

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{ 0, 0, (float)ext.width, (float)ext.height, 0.0f, 1.0f };
    VkRect2D   scissor{ {0,0}, ext };
    VkPipelineViewportStateCreateInfo vs{};
    vs.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vs.viewportCount = 1; vs.pViewports = &viewport;
    vs.scissorCount  = 1; vs.pScissors  = &scissor;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType     = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // No depth test for UI — always on top
    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType           = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_FALSE;

    // Alpha blend
    VkPipelineColorBlendAttachmentState blendAtt{};
    blendAtt.blendEnable         = VK_TRUE;
    blendAtt.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAtt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAtt.colorBlendOp        = VK_BLEND_OP_ADD;
    blendAtt.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAtt.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAtt.alphaBlendOp        = VK_BLEND_OP_ADD;
    blendAtt.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                   VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments    = &blendAtt;

    // UI push constant: int mode (0=solid, 1=text)
    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.offset     = 0;
    pcr.size       = sizeof(UIPushConst);

    VkPipelineLayoutCreateInfo lci{};
    lci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    lci.setLayoutCount         = 1;
    lci.pSetLayouts            = &m_uiDescLayout;
    lci.pushConstantRangeCount = 1;
    lci.pPushConstantRanges    = &pcr;
    VK_CHECK(vkCreatePipelineLayout(device, &lci, nullptr, &m_uiLayout));

    VkGraphicsPipelineCreateInfo pci{};
    pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount          = (uint32_t)stages.size();
    pci.pStages             = stages.data();
    pci.pVertexInputState   = &vi;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState      = &vs;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState   = &ms;
    pci.pDepthStencilState  = &ds;
    pci.pColorBlendState    = &cb;
    pci.layout              = m_uiLayout;
    pci.renderPass          = m_mainRenderPass;
    pci.subpass             = 0;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pci,
        nullptr, &m_uiPipeline));

    vkDestroyShaderModule(device, vsM, nullptr);
    vkDestroyShaderModule(device, fsM, nullptr);
}

// ── Diffuse texture descriptor set ────────────────────────────────────────
VkDescriptorSet VulkanPipeline::allocateDiffuseSet(
    VkDevice device, VkImageView diffuseView, VkSampler sampler) const
{
    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = m_descPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &m_diffuseDescLayout;

    VkDescriptorSet set;
    VK_CHECK(vkAllocateDescriptorSets(device, &ai, &set));

    VkDescriptorImageInfo ii{ sampler, diffuseView,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

    VkWriteDescriptorSet w{};
    w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet          = set;
    w.dstBinding      = 0;
    w.descriptorCount = 1;
    w.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo      = &ii;
    vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
    return set;
}
