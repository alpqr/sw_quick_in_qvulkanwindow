/****************************************************************************
**
** Copyright (C) 2017 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the examples of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:BSD$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** BSD License Usage
** Alternatively, you may use this file under the terms of the BSD license
** as follows:
**
** "Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are
** met:
**   * Redistributions of source code must retain the above copyright
**     notice, this list of conditions and the following disclaimer.
**   * Redistributions in binary form must reproduce the above copyright
**     notice, this list of conditions and the following disclaimer in
**     the documentation and/or other materials provided with the
**     distribution.
**   * Neither the name of The Qt Company Ltd nor the names of its
**     contributors may be used to endorse or promote products derived
**     from this software without specific prior written permission.
**
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
** OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
** LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "vulkanwindow.h"
#include <QVulkanFunctions>
#include <QMatrix4x4>
#include <QScreen>
#include <QFile>
#include <QQuickRenderControl>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQmlEngine>
#include <QQmlComponent>

#include <QtQuick/private/qquickwindow_p.h>
#include <QtQuick/private/qsgsoftwarerenderer_p.h>

static const int QUICK_W = 512;
static const int QUICK_H = 512;

static float vertexData[] = {
    // x, y, z, u, v
    -1, -1, 0, 0, 1,
    -1,  1, 0, 0, 0,
     1, -1, 0, 1, 1,
     1,  1, 0, 1, 0
};

static inline VkDeviceSize aligned(VkDeviceSize v, VkDeviceSize byteAlign)
{
    return (v + byteAlign - 1) & ~(byteAlign - 1);
}

class RenderControl : public QQuickRenderControl
{
public:
    RenderControl(QWindow *w) : m_window(w) { }
    QWindow *renderWindow(QPoint *offset) override;

private:
    QWindow *m_window;
};

QWindow *RenderControl::renderWindow(QPoint *offset)
{
    if (offset)
        *offset = QPoint(0, 0);
    return m_window;
}

VulkanWindowWithSwQuick::VulkanWindowWithSwQuick()
{
    // The key: force the software backend.
    QQuickWindow::setSceneGraphBackend(QSGRendererInterface::Software);

    m_renderControl = new RenderControl(this);

    m_quickWindow = new QQuickWindow(m_renderControl);

    m_qmlEngine = new QQmlEngine;
    if (!m_qmlEngine->incubationController())
        m_qmlEngine->setIncubationController(m_quickWindow->incubationController());

    connect(m_renderControl, &QQuickRenderControl::renderRequested, [this] { m_quickSceneChanged = true; });
    connect(m_renderControl, &QQuickRenderControl::sceneChanged, [this] { m_quickSceneChanged = true; });
    connect(this, &QWindow::screenChanged, this, &VulkanWindowWithSwQuick::onScreenChanged);
}

VulkanWindowWithSwQuick::~VulkanWindowWithSwQuick()
{
    delete m_renderControl;
    delete m_qmlComponent;
    delete m_quickWindow;
    delete m_qmlEngine;
}

void VulkanWindowWithSwQuick::createQuickImage()
{
    if (!m_quickImage.isNull())
        return;

    m_dpr = devicePixelRatio();
    m_quickImage = QImage(QSize(QUICK_W, QUICK_H) * m_dpr, QImage::Format_ARGB32_Premultiplied);
    m_quickImage.setDevicePixelRatio(m_dpr);
    qDebug() << "Created" << m_quickImage;
}

QImage *VulkanWindowWithSwQuick::renderQuickImage(QRegion *dirtyRegion)
{
    createQuickImage();

    m_renderControl->polishItems();
    m_renderControl->sync();

    QQuickWindowPrivate *wd = QQuickWindowPrivate::get(m_quickWindow);
    QSGSoftwareRenderer *r = static_cast<QSGSoftwareRenderer *>(wd->renderer);
    r->setCurrentPaintDevice(&m_quickImage);

    m_renderControl->render();

    if (dirtyRegion)
        *dirtyRegion = r->flushRegion();

    m_quickSceneChanged = false;
    return &m_quickImage;
}

void VulkanWindowWithSwQuick::runQuick()
{
    disconnect(m_qmlComponent, &QQmlComponent::statusChanged, this, &VulkanWindowWithSwQuick::runQuick);

    if (m_qmlComponent->isError()) {
        const QList<QQmlError> errorList = m_qmlComponent->errors();
        for (const QQmlError &error : errorList)
            qWarning() << error.url() << error.line() << error;
        return;
    }

    QObject *rootObject = m_qmlComponent->create();
    if (m_qmlComponent->isError()) {
        const QList<QQmlError> errorList = m_qmlComponent->errors();
        for (const QQmlError &error : errorList)
            qWarning() << error.url() << error.line() << error;
        return;
    }

    m_rootItem = qobject_cast<QQuickItem *>(rootObject);
    if (!m_rootItem) {
        qWarning("run: Not a QQuickItem");
        delete rootObject;
        return;
    }

    m_rootItem->setParentItem(m_quickWindow->contentItem());

    updateQuickSizes();

    m_quickSceneChanged = true;
    m_quickRunning = true;
}

void VulkanWindowWithSwQuick::updateQuickSizes()
{
    // Behave like SizeRootObjectToView.
    m_rootItem->setWidth(QUICK_W);
    m_rootItem->setHeight(QUICK_H);

    m_quickWindow->setGeometry(0, 0, QUICK_W, QUICK_H);
}

void VulkanWindowWithSwQuick::startQuick(const QString &filename)
{
    m_quickStarted = true;

    m_qmlComponent = new QQmlComponent(m_qmlEngine, QUrl(filename));
    if (m_qmlComponent->isLoading())
        connect(m_qmlComponent, &QQmlComponent::statusChanged, this, &VulkanWindowWithSwQuick::runQuick);
    else
        runQuick();
}

void VulkanWindowWithSwQuick::resizeQuickImage()
{
    if (m_rootItem) {
        m_quickImage = QImage();
        createQuickImage();
        updateQuickSizes();
    }
}

// Use a static (QUICK_W, QUICK_H) size for now
//void VulkanWindowWithSwQuick::resizeEvent(QResizeEvent *)
//{
//    if (!m_quickImage.isNull() && m_quickImage.size() != size() * devicePixelRatio())
//        resizeQuickImage();
//}

void VulkanWindowWithSwQuick::onScreenChanged()
{
    if (!m_quickImage.isNull() && m_dpr != devicePixelRatio())
        resizeQuickImage();
}

QVulkanWindowRenderer *VulkanWindowWithSwQuick::createRenderer()
{
    return new VulkanRenderer(this);
}

VulkanRenderer::VulkanRenderer(VulkanWindowWithSwQuick *w)
    : m_window(w)
{
    for (int i = 0; i < QVulkanWindow::MAX_CONCURRENT_FRAME_COUNT; ++i) {
        m_texImage[i] = VK_NULL_HANDLE;
        m_texView[i] = VK_NULL_HANDLE;
        m_descDirty[i] = false;
    }
}

void VulkanRenderer::initResources()
{
    qDebug("initResources");

    VkDevice dev = m_window->device();
    m_devFuncs = m_window->vulkanInstance()->deviceFunctions(dev);

    const int concurrentFrameCount = m_window->concurrentFrameCount();

    VkSamplerCreateInfo samplerInfo;
    memset(&samplerInfo, 0, sizeof(samplerInfo));
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VkResult err = m_devFuncs->vkCreateSampler(dev, &samplerInfo, nullptr, &m_sampler);
    if (err != VK_SUCCESS)
        qFatal("Failed to create sampler: %d", err);

    VkBufferCreateInfo bufInfo;
    memset(&bufInfo, 0, sizeof(bufInfo));
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = sizeof(vertexData);
    bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    err = m_devFuncs->vkCreateBuffer(dev, &bufInfo, nullptr, &m_vertexBuf);
    if (err != VK_SUCCESS)
        qFatal("Failed to create buffer: %d", err);

    VkMemoryRequirements memReq;
    m_devFuncs->vkGetBufferMemoryRequirements(dev, m_vertexBuf, &memReq);

    VkMemoryAllocateInfo memAllocInfo = {
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        nullptr,
        memReq.size,
        m_window->hostVisibleMemoryIndex()
    };

    err = m_devFuncs->vkAllocateMemory(dev, &memAllocInfo, nullptr, &m_vertexBufMem);
    if (err != VK_SUCCESS)
        qFatal("Failed to allocate memory: %d", err);

    err = m_devFuncs->vkBindBufferMemory(dev, m_vertexBuf, m_vertexBufMem, 0);
    if (err != VK_SUCCESS)
        qFatal("Failed to bind buffer memory: %d", err);

    quint8 *p;
    err = m_devFuncs->vkMapMemory(dev, m_vertexBufMem, 0, memReq.size, 0, reinterpret_cast<void **>(&p));
    if (err != VK_SUCCESS)
        qFatal("Failed to map memory: %d", err);
    memcpy(p, vertexData, sizeof(vertexData));
    m_devFuncs->vkUnmapMemory(dev, m_vertexBufMem);

    // Pipeline.
    VkVertexInputBindingDescription vertexBindingDesc = {
        0, // binding
        5 * sizeof(float),
        VK_VERTEX_INPUT_RATE_VERTEX
    };
    VkVertexInputAttributeDescription vertexAttrDesc[] = {
        { // position
            0, // location
            0, // binding
            VK_FORMAT_R32G32B32_SFLOAT,
            0
        },
        { // texcoord
            1,
            0,
            VK_FORMAT_R32G32_SFLOAT,
            3 * sizeof(float)
        }
    };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo;
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.pNext = nullptr;
    vertexInputInfo.flags = 0;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &vertexBindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = 2;
    vertexInputInfo.pVertexAttributeDescriptions = vertexAttrDesc;

    VkPipelineCacheCreateInfo pipelineCacheInfo;
    memset(&pipelineCacheInfo, 0, sizeof(pipelineCacheInfo));
    pipelineCacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    err = m_devFuncs->vkCreatePipelineCache(dev, &pipelineCacheInfo, nullptr, &m_pipelineCache);
    if (err != VK_SUCCESS)
        qFatal("Failed to create pipeline cache: %d", err);

    VkDescriptorPoolSize descPoolSizes = {
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, uint32_t(concurrentFrameCount)
    };
    VkDescriptorPoolCreateInfo descPoolInfo;
    memset(&descPoolInfo, 0, sizeof(descPoolInfo));
    descPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descPoolInfo.maxSets = concurrentFrameCount;
    descPoolInfo.poolSizeCount = 1;
    descPoolInfo.pPoolSizes = &descPoolSizes;
    err = m_devFuncs->vkCreateDescriptorPool(dev, &descPoolInfo, nullptr, &m_descPool);
    if (err != VK_SUCCESS)
        qFatal("Failed to create descriptor pool: %d", err);

    VkDescriptorSetLayoutBinding layoutBinding = {
        0, // binding
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        1, // descriptorCount
        VK_SHADER_STAGE_FRAGMENT_BIT,
        nullptr
    };

    VkDescriptorSetLayoutCreateInfo descLayoutInfo = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        nullptr,
        0,
        1, // bindingCount
        &layoutBinding
    };
    err = m_devFuncs->vkCreateDescriptorSetLayout(dev, &descLayoutInfo, nullptr, &m_descSetLayout);
    if (err != VK_SUCCESS)
        qFatal("Failed to create descriptor set layout: %d", err);

    for (int i = 0; i < concurrentFrameCount; ++i) {
        VkDescriptorSetAllocateInfo descSetAllocInfo = {
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            nullptr,
            m_descPool,
            1,
            &m_descSetLayout
        };
        err = m_devFuncs->vkAllocateDescriptorSets(dev, &descSetAllocInfo, &m_descSet[i]);
        if (err != VK_SUCCESS)
            qFatal("Failed to allocate descriptor set: %d", err);
    }

    VkPushConstantRange pcr = {
        VK_SHADER_STAGE_VERTEX_BIT,
        0,
        64
    };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo;
    memset(&pipelineLayoutInfo, 0, sizeof(pipelineLayoutInfo));
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pcr;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_descSetLayout;
    err = m_devFuncs->vkCreatePipelineLayout(dev, &pipelineLayoutInfo, nullptr, &m_pipelineLayout);
    if (err != VK_SUCCESS)
        qFatal("Failed to create pipeline layout: %d", err);

    VkShaderModule vertShaderModule = createShader(QStringLiteral(":/texture_vert.spv"));
    VkShaderModule fragShaderModule = createShader(QStringLiteral(":/texture_frag.spv"));

    VkGraphicsPipelineCreateInfo pipelineInfo;
    memset(&pipelineInfo, 0, sizeof(pipelineInfo));
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;

    VkPipelineShaderStageCreateInfo shaderStages[2] = {
        {
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            nullptr,
            0,
            VK_SHADER_STAGE_VERTEX_BIT,
            vertShaderModule,
            "main",
            nullptr
        },
        {
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            nullptr,
            0,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            fragShaderModule,
            "main",
            nullptr
        }
    };
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;

    pipelineInfo.pVertexInputState = &vertexInputInfo;

    VkPipelineInputAssemblyStateCreateInfo ia;
    memset(&ia, 0, sizeof(ia));
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    pipelineInfo.pInputAssemblyState = &ia;

    // The viewport and scissor will be set dynamically via vkCmdSetViewport/Scissor.
    // This way the pipeline does not need to be touched when resizing the window.
    VkPipelineViewportStateCreateInfo vp;
    memset(&vp, 0, sizeof(vp));
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    pipelineInfo.pViewportState = &vp;

    VkPipelineRasterizationStateCreateInfo rs;
    memset(&rs, 0, sizeof(rs));
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_BACK_BIT;
    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rs.lineWidth = 1.0f;
    pipelineInfo.pRasterizationState = &rs;

    VkPipelineMultisampleStateCreateInfo ms;
    memset(&ms, 0, sizeof(ms));
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    pipelineInfo.pMultisampleState = &ms;

    VkPipelineDepthStencilStateCreateInfo ds;
    memset(&ds, 0, sizeof(ds));
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    pipelineInfo.pDepthStencilState = &ds;

    VkPipelineColorBlendStateCreateInfo cb;
    memset(&cb, 0, sizeof(cb));
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    // assume pre-multiplied alpha, blend, write out all of rgba
    VkPipelineColorBlendAttachmentState att;
    memset(&att, 0, sizeof(att));
    att.colorWriteMask = 0xF;
    att.blendEnable = VK_TRUE;
    att.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    att.colorBlendOp = VK_BLEND_OP_ADD;
    att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    att.alphaBlendOp = VK_BLEND_OP_ADD;
    cb.attachmentCount = 1;
    cb.pAttachments = &att;
    pipelineInfo.pColorBlendState = &cb;

    VkDynamicState dynEnable[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn;
    memset(&dyn, 0, sizeof(dyn));
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = sizeof(dynEnable) / sizeof(VkDynamicState);
    dyn.pDynamicStates = dynEnable;
    pipelineInfo.pDynamicState = &dyn;

    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = m_window->defaultRenderPass();

    err = m_devFuncs->vkCreateGraphicsPipelines(dev, m_pipelineCache, 1, &pipelineInfo, nullptr, &m_pipeline);
    if (err != VK_SUCCESS)
        qFatal("Failed to create graphics pipeline: %d", err);

    if (vertShaderModule)
        m_devFuncs->vkDestroyShaderModule(dev, vertShaderModule, nullptr);
    if (fragShaderModule)
        m_devFuncs->vkDestroyShaderModule(dev, fragShaderModule, nullptr);
}

void VulkanRenderer::initSwapChainResources()
{
    qDebug("initSwapChainResources");

    m_mvp = m_window->clipCorrectionMatrix();
    const QSize sz = m_window->swapChainImageSize();
    m_mvp.perspective(45.0f, sz.width() / (float) sz.height(), 0.01f, 100.0f);
    m_mvp.translate(0, 0, -4);
}

void VulkanRenderer::releaseSwapChainResources()
{
    qDebug("releaseSwapChainResources");
}

void VulkanRenderer::releaseResources()
{
    qDebug("releaseResources");

    releaseTex();

    VkDevice dev = m_window->device();

    if (m_sampler) {
        m_devFuncs->vkDestroySampler(dev, m_sampler, nullptr);
        m_sampler = VK_NULL_HANDLE;
    }

    if (m_descSetLayout) {
        m_devFuncs->vkDestroyDescriptorSetLayout(dev, m_descSetLayout, nullptr);
        m_descSetLayout = VK_NULL_HANDLE;
    }

    if (m_descPool) {
        m_devFuncs->vkDestroyDescriptorPool(dev, m_descPool, nullptr);
        m_descPool = VK_NULL_HANDLE;
    }

    if (m_pipeline) {
        m_devFuncs->vkDestroyPipeline(dev, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }

    if (m_pipelineLayout) {
        m_devFuncs->vkDestroyPipelineLayout(dev, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }

    if (m_pipelineCache) {
        m_devFuncs->vkDestroyPipelineCache(dev, m_pipelineCache, nullptr);
        m_pipelineCache = VK_NULL_HANDLE;
    }

    if (m_vertexBuf) {
        m_devFuncs->vkDestroyBuffer(dev, m_vertexBuf, nullptr);
        m_vertexBuf = VK_NULL_HANDLE;
    }

    if (m_vertexBufMem) {
        m_devFuncs->vkFreeMemory(dev, m_vertexBufMem, nullptr);
        m_vertexBufMem = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::releaseTex()
{
    VkDevice dev = m_window->device();

    for (int i = 0; i < m_window->concurrentFrameCount(); ++i) {
        if (m_texView[i]) {
            m_devFuncs->vkDestroyImageView(dev, m_texView[i], nullptr);
            m_texView[i] = VK_NULL_HANDLE;
        }

        if (m_texImage[i]) {
            m_devFuncs->vkDestroyImage(dev, m_texImage[i], nullptr);
            m_texImage[i] = VK_NULL_HANDLE;
        }
    }

    if (m_texMem) {
        m_devFuncs->vkFreeMemory(dev, m_texMem, nullptr);
        m_texMem = VK_NULL_HANDLE;
    }

    m_texSize = QSize();
}

void VulkanRenderer::startNextFrame()
{
    VkDevice dev = m_window->device();

    // Here we go. If Quick has not yet been initialized, do it.
    if (!m_window->isQuickStarted())
        m_window->startQuick(QStringLiteral("qrc:/rotatingsquare.qml"));

    // When the (potentially async) init is done, and there was a change in the
    // scene (due to animations f.ex.), then polish, sync and render into the QImage.
    if (m_window->isQuickRunning() && m_window->hasQuickSceneChanged()) {
        const int concurrentFrameCount = m_window->concurrentFrameCount();
        QRegion dirtyRegion;
        m_source = m_window->renderQuickImage(&dirtyRegion);
        for (int i = 0; i < concurrentFrameCount; ++i)
            m_texDirty[i] += dirtyRegion;

        if (!m_source->isNull()) {
            if (m_texSize != m_source->size()) {
                // Keep it simple for now... just block, in order to avoid
                // touching the potentially still-in-use image, descriptors,
                // etc. This is infrequent anyways.
                m_devFuncs->vkDeviceWaitIdle(dev);

                releaseTex();
                // Let's assume sampling from linear tiling is supported.
                if (!createTextureImage(concurrentFrameCount, m_source->size(), m_texImage, &m_texMem,
                                        VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_SAMPLED_BIT,
                                        m_window->hostVisibleMemoryIndex()))
                {
                    qWarning("Failed to create texture");
                    return;
                }
                for (int i = 0; i < concurrentFrameCount; ++i) {
                    if (!createTextureImageView(m_texImage[i], &m_texView[i])) {
                        qWarning("Failed to create image view");
                        return;
                    }
                    m_descDirty[i] = true;
                }
                m_texSize = m_source->size();
            }
        }
    }

    // Keep a per-concurrent-frame image+memory(+descriptor set) in order to
    // avoid potentially disturbing the previous, in-flight frame(s).
    int frame = m_window->currentFrame();
    if (m_descDirty[frame]) {
        m_descDirty[frame] = false;
        VkWriteDescriptorSet descWrite;
        memset(&descWrite, 0, sizeof(descWrite));
        VkDescriptorImageInfo descImageInfo = {
            m_sampler,
            m_texView[m_window->currentFrame()],
            VK_IMAGE_LAYOUT_GENERAL
        };
        descWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descWrite.dstSet = m_descSet[frame];
        descWrite.dstBinding = 0;
        descWrite.descriptorCount = 1;
        descWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descWrite.pImageInfo = &descImageInfo;
        m_devFuncs->vkUpdateDescriptorSets(dev, 1, &descWrite, 0, nullptr);
    }

    // Now copy the actual pixel data, but only the dirty areas.
    if (!m_texDirty[frame].isEmpty()) {
        if (!writeLinearImage(*m_source, m_texImage[frame], m_texMem, frame * m_oneImageSize, m_texDirty[frame]))
            qWarning("Failed to write image to host visible memory");

        m_texDirty[frame] = QRegion();
    }

    VkCommandBuffer cb = m_window->currentCommandBuffer();
    const QSize sz = m_window->swapChainImageSize();

    static float g = 0.0f;
    g += 0.01f;
    if (g > 1.0f)
        g = 0.0f;
    VkClearColorValue clearColor = { 0, g, 0, 1 };
    VkClearDepthStencilValue clearDS = { 1, 0 };
    VkClearValue clearValues[2];
    memset(clearValues, 0, sizeof(clearValues));
    clearValues[0].color = clearColor;
    clearValues[1].depthStencil = clearDS;

    VkRenderPassBeginInfo rpBeginInfo;
    memset(&rpBeginInfo, 0, sizeof(rpBeginInfo));
    rpBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBeginInfo.renderPass = m_window->defaultRenderPass();
    rpBeginInfo.framebuffer = m_window->currentFramebuffer();
    rpBeginInfo.renderArea.extent.width = sz.width();
    rpBeginInfo.renderArea.extent.height = sz.height();
    rpBeginInfo.clearValueCount = 2;
    rpBeginInfo.pClearValues = clearValues;
    VkCommandBuffer cmdBuf = m_window->currentCommandBuffer();
    m_devFuncs->vkCmdBeginRenderPass(cmdBuf, &rpBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    m_devFuncs->vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    m_devFuncs->vkCmdPushConstants(cb, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, 64, m_mvp.constData());
    m_devFuncs->vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1,
                                        &m_descSet[m_window->currentFrame()], 0, nullptr);
    VkDeviceSize vbOffset = 0;
    m_devFuncs->vkCmdBindVertexBuffers(cb, 0, 1, &m_vertexBuf, &vbOffset);

    VkViewport viewport;
    viewport.x = viewport.y = 0;
    viewport.width = sz.width();
    viewport.height = sz.height();
    viewport.minDepth = 0;
    viewport.maxDepth = 1;
    m_devFuncs->vkCmdSetViewport(cb, 0, 1, &viewport);

    VkRect2D scissor;
    scissor.offset.x = scissor.offset.y = 0;
    scissor.extent.width = viewport.width;
    scissor.extent.height = viewport.height;
    m_devFuncs->vkCmdSetScissor(cb, 0, 1, &scissor);

    m_devFuncs->vkCmdDraw(cb, 4, 1, 0, 0);

    m_devFuncs->vkCmdEndRenderPass(cmdBuf);

    m_window->frameReady();

    m_window->requestUpdate();
}

bool VulkanRenderer::createTextureImage(int count, const QSize &size, VkImage *image, VkDeviceMemory *mem,
                                        VkImageTiling tiling, VkImageUsageFlags usage, uint32_t memIndex)
{
    VkDevice dev = m_window->device();
    for (int i = 0; i < count; ++i) {
        VkImageCreateInfo imageInfo;
        memset(&imageInfo, 0, sizeof(imageInfo));
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
        imageInfo.extent.width = size.width();
        imageInfo.extent.height = size.height();
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = tiling;
        imageInfo.usage = usage;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;

        VkResult err = m_devFuncs->vkCreateImage(dev, &imageInfo, nullptr, &image[i]);
        if (err != VK_SUCCESS) {
            qWarning("Failed to create linear image for texture: %d", err);
            return false;
        }

        VkMemoryRequirements memReq;
        m_devFuncs->vkGetImageMemoryRequirements(dev, image[i], &memReq);

        if (i == 0) {
            m_oneImageSize = aligned(memReq.size, memReq.alignment);
            const VkDeviceSize size = m_oneImageSize * count;
            VkMemoryAllocateInfo allocInfo = {
                VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                nullptr,
                size,
                memIndex
            };
            qDebug("allocating %u bytes for texture image", uint32_t(size));

            err = m_devFuncs->vkAllocateMemory(dev, &allocInfo, nullptr, mem);
            if (err != VK_SUCCESS) {
                qWarning("Failed to allocate memory for linear image: %d", err);
                return false;
            }
        }

        err = m_devFuncs->vkBindImageMemory(dev, image[i], *mem, m_oneImageSize * i);
        if (err != VK_SUCCESS) {
            qWarning("Failed to bind linear image memory: %d", err);
            return false;
        }
    }

    return true;
}

bool VulkanRenderer::createTextureImageView(VkImage image, VkImageView *view) const
{
    VkDevice dev = m_window->device();

    VkImageViewCreateInfo viewInfo;
    memset(&viewInfo, 0, sizeof(viewInfo));
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
    viewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
    viewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
    viewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
    viewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = viewInfo.subresourceRange.layerCount = 1;

    VkResult err = m_devFuncs->vkCreateImageView(dev, &viewInfo, nullptr, view);
    if (err != VK_SUCCESS) {
        qWarning("Failed to create image view for texture: %d", err);
        return false;
    }

    return true;
}

bool VulkanRenderer::writeLinearImage(const QImage &img, VkImage image, VkDeviceMemory memory,
                                      int offset, const QRegion &dirtyRegion) const
{
    VkDevice dev = m_window->device();

    VkImageSubresource subres = {
        VK_IMAGE_ASPECT_COLOR_BIT,
        0, // mip level
        0
    };
    VkSubresourceLayout layout;
    m_devFuncs->vkGetImageSubresourceLayout(dev, image, &subres, &layout);

    uchar *p;
    VkResult err = m_devFuncs->vkMapMemory(dev, memory, layout.offset + offset,
                                           layout.size, 0, reinterpret_cast<void **>(&p));
    if (err != VK_SUCCESS) {
        qWarning("Failed to map memory for linear image: %d", err);
        return false;
    }

    for (const QRect &r : dirtyRegion) {
        const int bpp = 4;
        const int preamble = r.x() * bpp;
        for (int y = r.y(); y < r.y() + r.height(); ++y) {
            const uchar *line = img.constScanLine(y);
            memcpy(p + layout.rowPitch * y + preamble, line + preamble, r.width() * bpp);
        }
    }

    m_devFuncs->vkUnmapMemory(dev, memory);
    return true;
}

VkShaderModule VulkanRenderer::createShader(const QString &name)
{
    QFile file(name);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("Failed to read shader %s", qPrintable(name));
        return VK_NULL_HANDLE;
    }
    QByteArray blob = file.readAll();
    file.close();

    VkShaderModuleCreateInfo shaderInfo;
    memset(&shaderInfo, 0, sizeof(shaderInfo));
    shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderInfo.codeSize = blob.size();
    shaderInfo.pCode = reinterpret_cast<const uint32_t *>(blob.constData());
    VkShaderModule shaderModule;
    VkResult err = m_devFuncs->vkCreateShaderModule(m_window->device(), &shaderInfo, nullptr, &shaderModule);
    if (err != VK_SUCCESS) {
        qWarning("Failed to create shader module: %d", err);
        return VK_NULL_HANDLE;
    }

    return shaderModule;
}
