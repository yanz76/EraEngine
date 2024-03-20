// AMD Cauldron code
//
// Copyright(c) 2023 Advanced Micro Devices, Inc.All rights reserved.
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sub-license, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
#pragma once

#include "core/contentmanager.h"
#include "core/components/cameracomponent.h"
#include "core/components/lightcomponent.h"
#include "misc/helpers.h"
#include "render/animation.h"
#include "render/mesh.h"

#include "json/json.h"
using json = nlohmann::ordered_json;

namespace cauldron
{
    /**
     * @struct GLTFDataRep
     *
     * GLTF data representation that persists throughout the loading process to facilitate data sharing across
     * multiple asynchronous loading jobs.
     *
     * @ingroup CauldronLoaders
     */
    struct GLTFDataRep
    {
        json*                                   pGLTFJsonData;                  ///< The json GLTF data instance.
        std::vector<std::vector<char>>          GLTFBufferData;                 ///< The GLTF buffer data entries.
        std::wstring                            GLTFFilePath;                   ///< The GLTF file path.

        std::vector<LightComponentData>         LightData;                      ///< Loaded <c><i>LightComponentData</i></c>.
        std::vector<CameraComponentData>        CameraData;                     ///< Loaded <c><i>CameraComponentData</i></c>.

        // To synchronize data loading and initialization
        bool                                    BuffersLoaded = false;          ///< Buffer load status. True if buffer loading is completed.
        bool                                    TexturesLoaded = false;         ///< Texture load status. True if buffer loading is completed.

        std::mutex                              CriticalSection;                ///< Mutex for syncing structure data changes.
        std::condition_variable                 BufferCV;                       ///< Condition variable for syncing structure buffer data changes.
        std::condition_variable                 TextureCV;                      ///< Condition variable for syncing structure texture data changes.

        // Content block being built up as we are loading various things
        ContentBlock*                           pLoadedContentRep = nullptr;    ///< The <c><i>ContentBlock</i></c> built by the loading processes.

        ~GLTFDataRep()
        {
            delete pGLTFJsonData;
        }
    };

    class UploadContext;

    /**
     * @class GLTFLoader
     *
     * GLTF loader class. Handles asynchronous GLTF scene loading.
     *
     * @ingroup CauldronLoaders
     */
    class GLTFLoader : public ContentLoader
    {
    public:

        /**
         * @brief   Constructor with default behavior.
         */
        GLTFLoader() = default;

        /**
         * @brief   Destructor with default behavior.
         */
        virtual ~GLTFLoader() = default;

        /**
         * @brief   Loads a single GLTF scene file asynchronously.
         */
        virtual void LoadAsync(void* loadParams) override;

        /**
         * @brief   Functionality not yet supported, and will assert.
         */
        virtual void LoadMultipleAsync(void* loadParams) override;

    private:
        NO_COPY(GLTFLoader)
        NO_MOVE(GLTFLoader)

        // Handler to load all glTF related assets and content
        void LoadGLTFContent(void* pParam);
        static void LoadGLTFTexturesCompleted(const std::vector<const Texture*>& textureList, void* pCallbackParams);

        static void LoadGLTFBuffer(void* pParam);
        static void LoadGLTFBuffersCompleted(void* pParam);
        static void LoadGLTFMesh(void* pParam);
        static void LoadGLTFAnimation(void* pParam);
        static void GLTFAllBufferAssetLoadsCompleted(void* pParam);

        // Parameter struct for Buffer-related loads
        struct GLTFBufferLoadParams
        {
            GLTFDataRep* pGLTFData = nullptr;
            uint32_t     BufferIndex = 0;
            std::wstring BufferName = L"";
            UploadContext* pUploadCtx = nullptr;
        };

        static const json* LoadVertexBuffer(const json& attributes, const char* attributeName, const json& accessors, const json& bufferViews, const json& buffers, const GLTFBufferLoadParams& params, VertexBufferInformation& info, bool forceConversionToFloat);
        static void LoadIndexBuffer(const json& primitive, const json& accessors, const json& bufferViews, const json& buffers, const GLTFBufferLoadParams& params, IndexBufferInformation& info);
        static void LoadAnimInterpolants(AnimChannel* pAnimChannel, AnimChannel::ComponentSampler samplerType, int32_t samplerIndex, GLTFBufferLoadParams* pBufferLoadParams);
        static void BuildBLAS(std::vector<Mesh*> meshes);

        void PostGLTFContentLoadCompleted(void* pParam);
    };

} // namespace cauldron
