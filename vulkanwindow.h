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

#ifndef VULKANWINDOW_H
#define VULKANWINDOW_H

#include <QVulkanWindow>
#include <QImage>

class QQuickRenderControl;
class QQuickWindow;
class QQmlEngine;
class QQmlComponent;
class QQuickItem;

class VulkanWindowWithSwQuick;

class VulkanRenderer : public QVulkanWindowRenderer
{
public:
    VulkanRenderer(VulkanWindowWithSwQuick *w);

    void initResources() override;
    void initSwapChainResources() override;
    void releaseSwapChainResources() override;
    void releaseResources() override;

    void startNextFrame() override;

    QMatrix4x4 modelView() const { return m_modelView; }
    QMatrix4x4 projection() const { return m_projection; }

private:
    bool createTextureImage(int count, const QSize &size, VkImage *image, VkDeviceMemory *mem,
                            VkImageTiling tiling, VkImageUsageFlags usage, uint32_t memIndex);
    bool createTextureImageView(VkImage image, VkImageView *view) const;
    bool writeLinearImage(const QImage &img, VkImage image, VkDeviceMemory memory,
                          int offset, const QRegion &dirtyRegion) const;
    void releaseTex();
    VkShaderModule createShader(const QString &name);

    VulkanWindowWithSwQuick *m_window;
    QVulkanDeviceFunctions *m_devFuncs;

    VkDeviceMemory m_texMem = VK_NULL_HANDLE;
    QRegion m_texDirty[QVulkanWindow::MAX_CONCURRENT_FRAME_COUNT];
    VkImage m_texImage[QVulkanWindow::MAX_CONCURRENT_FRAME_COUNT];
    VkImageView m_texView[QVulkanWindow::MAX_CONCURRENT_FRAME_COUNT];
    QSize m_texSize;
    VkDeviceSize m_oneImageSize;
    QImage *m_source;

    VkDeviceMemory m_vertexBufMem = VK_NULL_HANDLE;
    VkBuffer m_vertexBuf = VK_NULL_HANDLE;

    VkDescriptorPool m_descPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet m_descSet[QVulkanWindow::MAX_CONCURRENT_FRAME_COUNT];
    bool m_descDirty[QVulkanWindow::MAX_CONCURRENT_FRAME_COUNT];

    VkPipelineCache m_pipelineCache = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    VkSampler m_sampler = VK_NULL_HANDLE;

    QMatrix4x4 m_modelView;
    QMatrix4x4 m_projection;
    QMatrix4x4 m_mvp;
};

class VulkanWindowWithSwQuick : public QVulkanWindow
{
public:
    VulkanWindowWithSwQuick();
    ~VulkanWindowWithSwQuick();

    QVulkanWindowRenderer *createRenderer() override;

    void startQuick(const QString &filename);
    bool isQuickStarted() const { return m_quickStarted; }
    bool isQuickRunning() const { return m_quickRunning; }

    bool hasQuickSceneChanged() const { return m_quickSceneChanged; }

    QImage *renderQuickImage(QRegion *dirtyRegion);

private slots:
    void createQuickImage();
    void resizeQuickImage();
    void onScreenChanged();
    void runQuick();

private:
    //void resizeEvent(QResizeEvent *) override;
    void updateQuickSizes();

    bool event(QEvent *) override;

    VulkanRenderer *m_renderer;
    QQuickRenderControl *m_renderControl;
    QQuickWindow *m_quickWindow;
    QQmlEngine *m_qmlEngine;
    QQmlComponent *m_qmlComponent = nullptr;
    QQuickItem *m_rootItem = nullptr;
    qreal m_dpr;
    QImage m_quickImage;
    bool m_quickRunning = false;
    bool m_quickStarted = false;
    bool m_quickSceneChanged = false;
};

#endif
