Software-rendered Qt Quick scenes in a QVulkanWindow
====================================================

As of today (meaning the upcoming Qt 5.10) there is no real Vulkan support in Qt Quick.

However, for more static scenes we can use the software backend (aka 2D Renderer, built-in to Quick since Qt 5.8).

Using QQuickRenderControl and a few (for now?) private APIs we can redirect the output into a QImage. The rest is straightforward and we will end up with a VkImage.

Obviously not suitable for heavily animated, dynamic content but is good enough for some things. (dirty tracking is active though so only changed areas are pushed to texture mem)

Needs Qt 5.10 (dev branch of qtbase/qtdeclarative as of now).
