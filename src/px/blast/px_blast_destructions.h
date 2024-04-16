#pragma once

#include <map>
#include <unordered_set>

#include <px/core/px_physics_engine.h>
#include <rendering/main_renderer.h>
#include <px/core/px_extensions.h>
#include <application.h>
#include <scripting/native_scripting_linker.h>
#include <scene/scene.h>
#include <asset/mesh_postprocessing.h>
#include <random>
#include <iomanip>
#include <px/physics/px_joint.h>

#include <NvBlast.h>

#include <NvBlastTk.h>
#include <NvBlastExtTkSerialization.h>

#include "NvBlastGlobals.h"

#include <NvBlastExtDamageShaders.h>
#include <NvBlastExtStressSolver.h>
#include "NvBlastExtImpactDamageManager.h"
#include <NvBlastExtPxManager.h>
#include <NvBlastExtPxAsset.h>
#include <NvBlastExtPxTask.h>
#include <NvBlastExtSerialization.h>
#include <NvBlastExtPxActor.h>
#include <NvBlastExtPxFamily.h>
#include <NvBlastExtPxStressSolver.h>
#include <NvBlastExtPxListener.h>
#include "NvBlastExtAuthoring.h"
#include <NvBlastExtPxAsset.h>
#include "NvBlastExtLlSerialization.h"
#include <NvBlastExtSync.h>
#include <NvBlastExtAuthoringMesh.h>
#include <NvBlastExtAuthoringFractureTool.h>
#include <NvBlastExtAuthoringMeshCleaner.h>
#include <NvBlastExtAuthoringTypes.h>
#include <NvBlastExtAuthoringBondGenerator.h>

namespace physics
{
    struct chunkPair
    {
        entity_handle chunk1{};
        entity_handle chunk2{};

        chunkPair(uint32 c1, uint32 c2) : chunk1((entity_handle)c1), chunk2((entity_handle)c2) {}
        chunkPair(entity_handle c1, entity_handle c2) : chunk1(c1), chunk2(c2) {}
    };

    inline bool operator==(const chunkPair& lhs, const chunkPair& rhs)
    {
        return (rhs.chunk1 == lhs.chunk1 || rhs.chunk1 == lhs.chunk2) && (rhs.chunk2 == lhs.chunk2 || rhs.chunk2 == lhs.chunk1);
    }
}

namespace std
{
    template<>
    struct hash<physics::chunkPair>
    {
        size_t operator()(const physics::chunkPair& pair) const
        {
            size_t seed = 0;

            hash_combine(seed, (uint32)pair.chunk1);
            hash_combine(seed, (uint32)pair.chunk2);

            return seed;
        }
    };
}

namespace physics
{
    struct nvmesh;
    struct chunk_graph_manager;

    static inline int id = 0;

    enum class anchor : uint8_t
    {
        None = 0,
        Left = 1,
        Right = 2,
        Bottom = 4,
        Top = 8,
        Front = 16,
        Back = 32
    };

    inline anchor operator|(anchor lhs, anchor rhs)
    {
        return static_cast<anchor>(static_cast<char>(lhs) | static_cast<char>(rhs));
    }

    inline bool operator&(anchor lhs, anchor rhs)
    {
        return (static_cast<char>(lhs) & static_cast<char>(rhs));
    }

    struct bounds
    {
        vec3 center;
        vec3 extents;

        void setMinMax(const vec3& min, const vec3& max)
        {
            extents = (max - min) * 0.5f;
            center = min + extents;
        }

        void encapsulate(const vec3& point)
        {
            setMinMax(::min(center - extents, point), ::max(center + extents, point));
        }

        void encapsulate(const bounds& bounds)
        {
            encapsulate(bounds.center - bounds.extents);
            encapsulate(bounds.center + bounds.extents);
        }
    };

    inline bounds getCompositeMeshBounds(eentity& entt)
    {
        auto childs = entt.getChilds();

        ::std::vector<bounds> meshBounds;
        meshBounds.reserve(childs.size());

        if (auto mesh = entt.getComponentIfExists<mesh_component>())
        {
            auto& aabb = mesh->mesh->aabb;

            meshBounds.push_back({ (aabb.maxCorner + aabb.minCorner) / 2.0f, (aabb.maxCorner - aabb.minCorner) / 2.0f });
        }

        for (auto& c : childs)
        {
            if (auto mesh = c.getComponentIfExists<mesh_component>())
            {
                auto& aabb = mesh->mesh->aabb;

                meshBounds.push_back({ (aabb.maxCorner + aabb.minCorner) / 2.0f, (aabb.maxCorner - aabb.minCorner) / 2.0f });
            }
        }

        if (meshBounds.size() == 0)
            return {};

        if (meshBounds.size() == 1)
            return meshBounds[0];

        auto& compositeBounds = meshBounds[0];

        for (size_t i = 1; i < meshBounds.size(); i++)
        {
            compositeBounds.encapsulate(meshBounds[i]);
        }

        return compositeBounds;
    }

    struct nvmesh
    {
        ::std::vector<physx::PxVec3> vertices;
        ::std::vector<physx::PxVec3> normals;
        ::std::vector<physx::PxVec2> uvs;
        ::std::vector<uint32_t> indices;

        Nv::Blast::Mesh* mesh = nullptr;

        nvmesh(::std::vector<physx::PxVec3> verts, ::std::vector<physx::PxVec3> norms,
            ::std::vector<physx::PxVec2> uvis, ::std::vector<uint32_t> inds)
            : vertices(verts),
            normals(norms),
            uvs(uvis),
            indices(inds)
        {
            mesh = NvBlastExtAuthoringCreateMesh((NvcVec3*)vertices.data(), (NvcVec3*)normals.data(), (NvcVec2*)uvs.data(),
                vertices.size(), indices.data(), indices.size());
        }

        nvmesh()
        {
            mesh = NvBlastExtAuthoringCreateMesh((NvcVec3*)vertices.data(), (NvcVec3*)normals.data(), (NvcVec2*)uvs.data(),
                vertices.size(), indices.data(), indices.size());
        }

        nvmesh(Nv::Blast::Mesh* inMesh)
        {
            mesh = inMesh;

            size_t nbVerts = mesh->getVerticesCount();
            auto verts = mesh->getVertices();

            for (size_t i = 0; i < nbVerts; i++)
            {
                vertices.push_back(PxVec3(verts[i].p.x, verts[i].p.y, verts[i].p.z));
                uvs.push_back(PxVec2(verts[i].uv->x, verts[i].uv->y));

                normals.push_back(PxVec3(verts[i].n.x, verts[i].n.y, verts[i].n.z));
            }
        }

        void release()
        {
            vertices.clear();
            normals.clear();
            uvs.clear();
            indices.clear();

            RELEASE_PTR(mesh);
        }

        void cleanMesh()
        {
            Nv::Blast::MeshCleaner* cleaner = NvBlastExtAuthoringCreateMeshCleaner();
            mesh = cleaner->cleanMesh(mesh);
        }
    };

    struct nvmesh_chunk_component : px_physics_component_base
    {
        nvmesh_chunk_component() = default;
        nvmesh_chunk_component(nvmesh* inputMesh) : mesh(inputMesh) {}
        ~nvmesh_chunk_component() {}

        virtual void release(bool release = false) noexcept override { PX_RELEASE(mesh) }

        nvmesh* mesh = nullptr;
    };

    inline ref<submesh_asset> createRenderMesh(const physics::nvmesh& simpleMesh)
    {
        ref<submesh_asset> asset = make_ref<submesh_asset>();

        auto defaultmat = createPBRMaterialAsync({ "", "" });

        std::vector<vec3> positions; positions.reserve(1 << 16);
        std::vector<vec2> uvs; uvs.reserve(1 << 16);
        std::vector<vec3> normals; normals.reserve(1 << 16);

        std::vector<vec3> positionCache; positionCache.reserve(16);
        std::vector<vec2> uvCache; uvCache.reserve(16);
        std::vector<vec3> normalCache; normalCache.reserve(16);

        size_t nbTriangles = simpleMesh.indices.size() / 3;

        for (auto& vert : simpleMesh.vertices)
        {
            asset->positions.push_back(physx::createVec3(vert));
        }
        for (auto& uv : simpleMesh.uvs)
        {
            asset->uvs.push_back(physx::createVec2(uv));
        }

        for (auto& norm : simpleMesh.normals)
        {
            asset->normals.push_back(physx::createVec3(norm));
            //asset->tangents.push_back(getTangent(physx::createVec3(norm)));
        }

        for (uint32_t i = 0; i < simpleMesh.indices.size(); i += 3)
        {
            asset->triangles.push_back(indexed_triangle16{
                (uint16)simpleMesh.indices[i + 0],
                (uint16)simpleMesh.indices[i + 1],
                (uint16)simpleMesh.indices[i + 2] });
        }

        generateNormalsAndTangents(asset, mesh_flag_default);

        return asset;
    }

    struct fracture_tool
    {
        Nv::Blast::FractureTool* fractureTool = nullptr;

        fracture_tool()
        {
            fractureTool = NvBlastExtAuthoringCreateFractureTool();
        }
    };

    struct randomGenerator : Nv::Blast::RandomGeneratorBase
    {
        static inline int seedResult;

        virtual float getRandomValue()
        {
            ::std::random_device rd;
            ::std::mt19937 eng(rd());
            ::std::uniform_real_distribution<float> distr(0.0f, 1.0f);
            float nb = distr(eng);
            return nb;
        }

        virtual void seed(int32_t seed)
        {
            seedResult = seed;
        }
    };

    struct voronoi_sites_generator
    {
        randomGenerator* rndGen = nullptr;
        Nv::Blast::VoronoiSitesGenerator* generator = nullptr;

        voronoi_sites_generator(nvmesh* mesh)
        {
            rndGen = new randomGenerator();
            generator = NvBlastExtAuthoringCreateVoronoiSitesGenerator(mesh->mesh, rndGen);
        }

        void release()
        {
            RELEASE_PTR(rndGen)
            RELEASE_PTR(generator)
        }
    };

    static inline int maxSplitting = 1;

    inline ::std::vector<::std::pair<ref<submesh_asset>, nvmesh*>> fractureMeshesInNvblast(int totalChunks, nvmesh* nvMesh, bool replace = false)
    {
        fracture_tool* fractureTool = new fracture_tool();
        fractureTool->fractureTool->setRemoveIslands(false);
        fractureTool->fractureTool->setSourceMeshes(&nvMesh->mesh, 1);
        voronoi_sites_generator* generator = new voronoi_sites_generator(nvMesh);
        generator->generator->setBaseMesh(nvMesh->mesh);

        {
            generator->generator->uniformlyGenerateSitesInMesh(totalChunks);
            const NvcVec3* sites;
            size_t nbSites = generator->generator->getVoronoiSites(sites);
            //int result = fractureTool->fractureTool->cut(0, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, {}, false, generator->rndGen);
            int result = fractureTool->fractureTool->voronoiFracturing(0, nbSites, sites, replace);

            fractureTool->fractureTool->finalizeFracturing();
        }

        // Extract meshes
        size_t meshCount = fractureTool->fractureTool->getChunkCount();
        ::std::vector<::std::pair<ref<submesh_asset>, nvmesh*>> meshes;
        meshes.reserve(meshCount);

        std::vector<std::vector<Nv::Blast::Triangle>> chunkMeshes;
        Nv::Blast::Triangle* trigs;

        chunkMeshes.reserve(fractureTool->fractureTool->getChunkCount());
        for (uint32_t i = 1; i < fractureTool->fractureTool->getChunkCount(); ++i)
        {
            uint32_t nbTrigs = fractureTool->fractureTool->getBaseMesh(i, trigs);
            std::vector<Nv::Blast::Triangle> trigList;
            for (size_t j = 0; j < nbTrigs; j++)
            {
                trigList.push_back(trigs[j]);
            }

            chunkMeshes.push_back(trigList);
        }

        for (size_t i = 0; i < chunkMeshes.size(); i++)
        {
            std::vector<physx::PxVec3> pos;
            std::vector<physx::PxVec3> norm;
            std::vector<physx::PxVec2> tex;
            std::vector<uint32_t> indexs;

            uint32_t index = 0;
            std::vector<Nv::Blast::Triangle>& chunk = chunkMeshes[i];

            for (size_t j = 0; j < chunk.size(); ++j)
            {
                pos.push_back({ chunk[j].a.p.x, chunk[j].a.p.y, chunk[j].a.p.z });
                pos.push_back({ chunk[j].b.p.x, chunk[j].b.p.y, chunk[j].b.p.z });
                pos.push_back({ chunk[j].c.p.x, chunk[j].c.p.y, chunk[j].c.p.z });

                norm.push_back({ chunk[j].a.n.x, chunk[j].a.n.y, chunk[j].a.n.z });
                norm.push_back({ chunk[j].b.n.x, chunk[j].b.n.y, chunk[j].b.n.z });
                norm.push_back({ chunk[j].c.n.x, chunk[j].c.n.y, chunk[j].c.n.z });

                tex.push_back({ chunk[j].a.uv[0].x, chunk[j].a.uv[0].y });
                tex.push_back({ chunk[j].b.uv[0].x, chunk[j].b.uv[0].y });
                tex.push_back({ chunk[j].c.uv[0].x, chunk[j].c.uv[0].y });

                indexs.push_back(index++);
                indexs.push_back(index++);
                indexs.push_back(index++);
            }

            nvmesh* mesh = new nvmesh(pos, norm, tex, indexs);

            auto chunkMesh = createRenderMesh(*mesh);

            meshes.push_back(::std::make_pair(chunkMesh, mesh));
        }

        return { meshes };
    }

    eentity buildChunk(const trs& transform, ref<pbr_material> insideMaterial, ref<pbr_material> outsideMaterial, ::std::pair<ref<submesh_asset>, nvmesh*> mesh, float mass, uint32 generation);

    inline ::std::vector<entity_handle> buildChunks(const trs& transform, ref<pbr_material> insideMaterial, ref<pbr_material> outsideMaterial, ::std::vector<::std::pair<ref<submesh_asset>, nvmesh*>> meshes, float chunkMass, uint32 generation = 0)
    {
        ::std::vector<entity_handle> handles;

        for (size_t i = 0; i < meshes.size(); i++)
        {
            handles.push_back(buildChunk(transform, insideMaterial, outsideMaterial, meshes[i], chunkMass, generation).handle);
        }

        return handles;
    }

    static inline uint32 maxSpliteGeneration = 3U;

    struct chunk_graph_manager
    {
        struct chunk_node
        {
            ::std::unordered_set<entity_handle> neighbours;
            entity_handle* neighboursArray = nullptr;

            bool hasBrokenLinks = false;

            ::std::unordered_map<px_fixed_joint*, entity_handle> jointToChunk;
            ::std::unordered_map<entity_handle, px_fixed_joint*> chunkToJoint;

            entity_handle handle{};

            bool frozen = true;
            bool isKinematic = false;

            vec3 frozenPos{};
            quat forzenRot{};

            uint32 spliteGeneration = 0;

            chunk_node() = default;

            chunk_node(entity_handle handl, uint32 generation) : handle(handl), spliteGeneration(generation){}

            bool contains(entity_handle chunkNode) const noexcept
            {
                return neighbours.contains(chunkNode);
            }

            void onJointBreak() noexcept
            {
                hasBrokenLinks = true;
            }

            void update() noexcept
            {
                //auto enttScene = physics::physics_holder::physicsRef->app.getCurrentScene();
                //
                //eentity renderEntity{ handle, &enttScene->registry };
                //
                //if (auto rb = renderEntity.getComponentIfExists<physics::px_rigidbody_component>())
                //{
                //    rb->updateMassAndInertia(7.5f);
                //}
            }

            void setup(chunk_graph_manager* manager) noexcept
            {
                freeze();

                jointToChunk.clear();
                chunkToJoint.clear();

                if (!manager->joints.contains(handle))
                    return;

                auto enttScene = physics::physics_holder::physicsRef->app.getCurrentScene();

                for (auto joint : manager->joints[handle])
                {
                    auto chunk = *reinterpret_cast<entity_handle*>(joint->second->userData);
                    jointToChunk.emplace(joint, chunk);
                    chunkToJoint.emplace(chunk, joint);
                }

                for (auto& chunkNode : chunkToJoint)
                {
                    neighbours.emplace(chunkNode.first);

                    eentity renderEntity{ chunkNode.first, &enttScene->registry };
                    auto& chunk = renderEntity.getComponent<physics::chunk_graph_manager::chunk_node>();
                    if (chunk.contains(handle) == false)
                    {
                        chunk.neighbours.emplace(handle);
                    }
                }
            }

            void unfreeze() noexcept
            {
                if (physics_holder::physicsRef->unfreezeBlastQueue.contains((uint32)handle))
                    return;
                ::std::lock_guard<::std::mutex> lock (physics_holder::physicsRef->sync);
                uint32 value = (uint32)handle;
                physics_holder::physicsRef->unfreezeBlastQueue.emplace(value);
                frozen = false;
            }

            void remove(entity_handle chunkNode) noexcept
            {
                chunkToJoint.erase(chunkNode);
                neighbours.erase(chunkNode);
            }

            void processDamage(PxVec3 impulse) noexcept
            {
                if (spliteGeneration < maxSpliteGeneration)
                {
                    if (impulse.magnitude() > 5.0f)
                    {
                        auto enttScene = physics::physics_holder::physicsRef->app.getCurrentScene();

                        if (!enttScene->registry.size())
                            return;

                        eentity fractureGameObject = enttScene->createEntity("Fracture")
                            .addComponent<transform_component>(vec3(0.0f), quat::identity, vec3(1.f));
                        auto& graphManager = fractureGameObject.addComponent<chunk_graph_manager>().getComponent<chunk_graph_manager>();

                        eentity renderEntity{ handle, &enttScene->registry };
                        auto& mesh = renderEntity.getComponent<nvmesh_chunk_component>();

                        auto defaultmat = createPBRMaterialAsync({ "", "" });
                        defaultmat->shader = pbr_material_shader_double_sided;

                        auto meshes = fractureMeshesInNvblast(5, mesh.mesh, false);

                        auto chunks = buildChunks(renderEntity.getComponentIfExists<transform_component>() ? *renderEntity.getComponentIfExists<transform_component>() : trs::identity, defaultmat, defaultmat, meshes, 7.5f);

                        graphManager.setup(chunks, ++spliteGeneration);

                        enttScene->deleteEntity(handle);

                        // Multithreaded version. Not tested!
                        /*
                        struct fracture_data
                        {
                        escene* scene;
                        entity_handle handle;
                        uint32& generation;
                        } data{ enttScene, handle, spliteGeneration };
                    
                        lowPriorityJobQueue.createJob<fracture_data>([](fracture_data& data, job_handle)
                        {
                            CPU_PROFILE_BLOCK("Fracture step");
                    
                            if (!data.scene->registry.size())
                                return;
                    
                            physics_holder::physicsRef->lockWrite();
                            eentity fractureGameObject = data.scene->createEntity("Fracture")
                                .addComponent<transform_component>(vec3(0.0f), quat::identity, vec3(1.f));
                            auto& graphManager = fractureGameObject.addComponent<chunk_graph_manager>().getComponent<chunk_graph_manager>();
                    
                            eentity renderEntity{ data.handle, &data.scene->registry };
                            auto& mesh = renderEntity.getComponent<nvmesh_chunk_component>();
                    
                            auto defaultmat = createPBRMaterialAsync({ "", "" });
                            defaultmat->shader = pbr_material_shader_double_sided;
                    
                            auto meshes = fractureMeshesInNvblast(5, mesh.mesh, false);
                    
                            auto chunks = buildChunks(renderEntity.getComponentIfExists<transform_component>() ? *renderEntity.getComponentIfExists<transform_component>() : trs::identity, defaultmat, defaultmat, meshes, 7.5f);
                    
                            graphManager.setup(chunks, ++data.generation);
                    
                            data.scene->deleteEntity(data.handle);
                    
                            physics_holder::physicsRef->unlockWrite();
                        }, data).submitNow();*/
                    }
                }
            }

            void cleanBrokenLinks() noexcept
            {
                ::std::vector<px_fixed_joint*> brokenLinks;
                auto enttScene = physics::physics_holder::physicsRef->app.getCurrentScene();

                for (auto& joint : jointToChunk)
                {
                    if (joint.first->joint && (joint.first->joint->getConstraintFlags() & PxConstraintFlag::eBROKEN))
                    {
                        brokenLinks.push_back(joint.first);
                    }
                }

                for (auto link : brokenLinks)
                {
                    auto body = jointToChunk[link];

                    link->joint->setInvInertiaScale0(0.0f);
                    link->joint->setInvInertiaScale1(0.0f);

                    jointToChunk.erase(link);
                    chunkToJoint.erase(body);

                    eentity renderEntity{ body, &enttScene->registry };

                    renderEntity.getComponent<physics::chunk_graph_manager::chunk_node>().remove(handle);
                    neighbours.erase(body);
                }

                hasBrokenLinks = false;
            }

            void freeze() noexcept
            {
                auto enttScene = physics::physics_holder::physicsRef->app.getCurrentScene();

                eentity renderEntity{ handle, &enttScene->registry };

                auto& rb = renderEntity.getComponent<px_rigidbody_component>();

                rb.setMaxAngularVelosity(1000.0f);
                rb.setMaxLinearVelosity(1000.0f);

                rb.setAngularDamping(0.01f);
                rb.setLinearDamping(0.01f);

                auto dyn = rb.getRigidActor()->is<PxRigidDynamic>();
                
                dyn->setSolverIterationCounts(4, 16);

                dyn->setCMassLocalPose(PxTransform(PxVec3(0.0f)));

                dyn->clearTorque();
                dyn->clearForce();

                dyn->setLinearVelocity(PxVec3(0.0f));
                dyn->setAngularVelocity(PxVec3(0.0f));

                rb.updateMassAndInertia(3.0f);
            }
        };

        ::std::vector<entity_handle> nodes;
        size_t nbNodes{};

        ::std::unordered_map<entity_handle, ::std::vector<px_fixed_joint*>> joints;

        void setup(::std::vector<entity_handle> bodies, uint32 generation = 0) noexcept
        {
            nbNodes = bodies.size();
            nodes.reserve(nbNodes);

            physics_holder::physicsRef->unfreezeBlastQueue.reserve(nbNodes * 5);

            auto enttScene = physics::physics_holder::physicsRef->app.getCurrentScene();

            for (size_t i = 0; i < bodies.size(); i++)
            {
                eentity renderEntity{ bodies[i], &enttScene->registry };

                if (!renderEntity.hasComponent<chunk_node>())
                {
                    renderEntity.addComponent<chunk_node>(bodies[i], generation);
                }
            }

            for (size_t i = 0; i < bodies.size(); i++)
            {
                eentity renderEntity{ bodies[i], &enttScene->registry };

                auto newNode = renderEntity.getComponentIfExists<chunk_node>();
                newNode->setup(this);
                nodes.emplace_back(bodies[i]);
            }
        }

        void update() noexcept
        {
            /*auto enttScene = physics::physics_holder::physicsRef->app.getCurrentScene();

            bool runSearch = false;

            for (size_t i = 0; i < nbNodes; i++)
            {
                auto node = nodes[i];

                eentity renderEntity{ node, &enttScene->registry };
                auto nodeComponent = renderEntity.getComponentIfExists<chunk_node>();

                if (nodeComponent->hasBrokenLinks)
                {
                    nodeComponent->cleanBrokenLinks();

                    runSearch = true;
                }
            }

            if (runSearch)
                searchGraph(nodes);*/
        }

        void searchGraph(::std::vector<entity_handle> objects) noexcept
        {
            ::std::vector<chunk_node*> anchors;
            ::std::unordered_set<chunk_node*> search;

            for (size_t i = 0; i < nbNodes; i++)
            {
                auto enttScene = physics::physics_holder::physicsRef->app.getCurrentScene();

                eentity renderEntity{ objects[i], &enttScene->registry };
                if (auto rb = renderEntity.getComponentIfExists<px_rigidbody_component>())
                {
                    if (renderEntity.getComponentIfExists<chunk_graph_manager::chunk_node>()->isKinematic)
                        anchors.push_back(renderEntity.getComponentIfExists<chunk_graph_manager::chunk_node>());
                }

                search.emplace(renderEntity.getComponentIfExists<chunk_graph_manager::chunk_node>());
            }

            for (auto o : anchors)
            {
                if (search.contains(o))
                {
                    ::std::unordered_set<chunk_node*> subVisited;
                    traverse(o, search, subVisited);

                    ::std::unordered_set<chunk_node*> temp;

                    for (auto s : search)
                    {
                        if (!subVisited.contains(s))
                            temp.emplace(s);
                    }
                    search = temp;
                }
            }
            for (auto sub : search)
            {
                sub->unfreeze();
            }
        }

        void traverse(chunk_node* o, ::std::unordered_set<chunk_node*>& search, ::std::unordered_set<chunk_node*>& visited) noexcept
        {
            if (search.contains(o) && visited.contains(o) == false)
            {
                auto enttScene = physics::physics_holder::physicsRef->app.getCurrentScene();

                visited.emplace(o);

                for (auto n : o->neighbours)
                {
                    eentity renderEntity{ n, &enttScene->registry };
                    traverse(renderEntity.getComponentIfExists<physics::chunk_graph_manager::chunk_node>(), search, visited);
                }
            }
        }
    };

    NODISCARD inline bounds toBounds(::std::vector<vec3> vertices) noexcept
    {
        auto min = vec3(INFINITE);
        auto max = vec3(-INFINITE);

        for (int i = 0; i < vertices.size(); i++)
        {
            min = ::min(vertices[i], min);
            max = ::min(vertices[i], max);
        }

        return bounds((max - min) / 2 + min, max - min);
    }

    NODISCARD inline ::std::vector<physx::PxVec3> createStdVectorPxVec3(const ::std::vector<vec3> vec) noexcept
    {
        ::std::vector<physx::PxVec3> v;
        v.reserve(vec.size());
        for (size_t i = 0; i < vec.size(); i++)
        {
            v.push_back(physx::createPxVec3(vec[i]));
        }

        return v;
    }

    NODISCARD inline ::std::vector<physx::PxVec2> createStdVectorPxVec2(const ::std::vector<vec2> vec) noexcept
    {
        ::std::vector<physx::PxVec2> v;
        v.reserve(vec.size());
        for (size_t i = 0; i < vec.size(); i++)
        {
            v.push_back(physx::createPxVec2(vec[i]));
        }

        return v;
    }

    NODISCARD inline float signedVolumeOfTriangle(const vec3& p1, const vec3& p2, const vec3& p3) noexcept
    {
        float v321 = p3.x * p2.y * p1.z;
        float v231 = p2.x * p3.y * p1.z;
        float v312 = p3.x * p1.y * p2.z;
        float v132 = p1.x * p3.y * p2.z;
        float v213 = p2.x * p1.y * p3.z;
        float v123 = p1.x * p2.y * p3.z;
        return (1.0f / 6.0f) * (-v321 + v231 + v312 - v132 - v213 + v123);
    }

    NODISCARD inline float volumeOfMesh(ref<submesh_asset> mesh) noexcept
    {
        float volume = 0.0f;

        auto& vertices = mesh->positions;
        auto& triangles = mesh->triangles;

        for (int i = 0; i < mesh->triangles.size(); i++)
        {
            vec3 p1 = vertices[triangles[i].a];
            vec3 p2 = vertices[triangles[i].b];
            vec3 p3 = vertices[triangles[i].c];

            volume += signedVolumeOfTriangle(p1, p2, p3);
        }

        return ::abs(volume);
    }

    NODISCARD inline ::std::vector<uint32_t> generateIndices(Nv::Blast::Triangle* triangles, size_t nbTriangles) noexcept
    {
        struct vertex
        {
            vec3 position;
            vec3 normal;
            vec2 uv;

            bool operator==(const vertex& other) const
            {
                return position.x == other.position.x && position.y == other.position.y && position.z == other.position.z
                    && normal.x == other.normal.x && normal.y == other.normal.y && normal.z == other.normal.z
                    && uv.x == other.uv.x && uv.y == other.uv.y;
            }
        };

        ::std::vector<vertex> vertices;
        ::std::vector<uint32_t> indices;

        for (size_t i = 0; i < nbTriangles; i++)
        {
            auto& triangle = triangles[i];

            {
                vertex vertex = {
                    vec3(triangle.a.p.x, triangle.a.p.y, triangle.a.p.z),
                    vec3(triangle.a.n.x, triangle.a.n.y, triangle.a.n.z),
                    vec2(triangle.a.uv->x, triangle.a.uv->y) };

                auto it = std::find(vertices.begin(), vertices.end(), vertex);
                if (it != vertices.end())
                {
                    // Vertex already exists in the vertices vector, so we just need to add the index
                    indices.push_back(std::distance(vertices.begin(), it));
                }
                else
                {
                    // New vertex, so we need to add it to the vertices vector and add the index
                    vertices.push_back(vertex);
                    indices.push_back(vertices.size() - 1);
                }
            }

            {
                vertex vertex = {
                    vec3(triangle.b.p.x, triangle.b.p.y, triangle.b.p.z),
                    vec3(triangle.b.n.x, triangle.b.n.y, triangle.b.n.z),
                    vec2(triangle.b.uv->x, triangle.b.uv->y) };

                auto it = std::find(vertices.begin(), vertices.end(), vertex);
                if (it != vertices.end())
                {
                    // Vertex already exists in the vertices vector, so we just need to add the index
                    indices.push_back(std::distance(vertices.begin(), it));
                }
                else
                {
                    // New vertex, so we need to add it to the vertices vector and add the index
                    vertices.push_back(vertex);
                    indices.push_back(vertices.size() - 1);
                }
            }

            {
                vertex vertex = {
                    vec3(triangle.c.p.x, triangle.c.p.y, triangle.c.p.z),
                    vec3(triangle.c.n.x, triangle.c.n.y, triangle.c.n.z),
                    vec2(triangle.c.uv->x, triangle.c.uv->y) };

                auto it = std::find(vertices.begin(), vertices.end(), vertex);
                if (it != vertices.end())
                {
                    // Vertex already exists in the vertices vector, so we just need to add the index
                    indices.push_back(std::distance(vertices.begin(), it));
                }
                else
                {
                    // New vertex, so we need to add it to the vertices vector and add the index
                    vertices.push_back(vertex);
                    indices.push_back(vertices.size() - 1);
                }
            }
        }

        return indices;
    }

    struct fracture
    {
        ::std::unordered_set<chunkPair> jointPairs;

        entity_handle fractureGameObject(ref<submesh_asset> meshAsset, const eentity& gameObject, anchor anchor, int seed, int totalChunks, ref<pbr_material> insideMaterial, ref<pbr_material> outsideMaterial, float jointBreakForce, float density)
        {
            // Translate all meshes to one world mesh
            //auto mesh = getWorldMesh(gameObject);

            if (totalChunks <= 0)
                return null_entity;

            auto enttScene = physics::physics_holder::physicsRef->app.getCurrentScene();

            eentity fractureGameObject = enttScene->createEntity("Fracture")
                .addComponent<transform_component>(vec3(0.0f), quat::identity, vec3(1.f));
            auto& graphManager = fractureGameObject.addComponent<chunk_graph_manager>().getComponent<chunk_graph_manager>();

            randomGenerator::seedResult = seed;

            ::std::vector<uint32_t> indices;
            for (size_t i = 0; i < meshAsset->triangles.size(); i++)
            {
                indices.push_back(meshAsset->triangles[i].a);
                indices.push_back(meshAsset->triangles[i].b);
                indices.push_back(meshAsset->triangles[i].c);
            }

            auto nvMesh = new nvmesh(
                createStdVectorPxVec3(meshAsset->positions),
                createStdVectorPxVec3(meshAsset->normals),
                createStdVectorPxVec2(meshAsset->uvs),
                indices
            );

            ::std::vector<::std::pair<ref<submesh_asset>, nvmesh*>> meshes;

            if (totalChunks == 1)
                meshes.push_back({ meshAsset, nvMesh });
            else
                meshes = fractureMeshesInNvblast(totalChunks, nvMesh);

            // Build chunks gameobjects
            float chunkMass = volumeOfMesh(meshAsset) * density / totalChunks;
            auto chunks = buildChunks(gameObject.getComponent<transform_component>(), insideMaterial, outsideMaterial, meshes, chunkMass);

            // Connect blocks that are touching with fixed joints
            for (size_t i = 0; i < chunks.size(); i++)
            {
                connectTouchingChunks(graphManager, meshes[i].first, chunks[i], jointBreakForce);
            }

            for (auto chunk : chunks)
            {
                eentity renderEntity{ chunk, &enttScene->registry };
                renderEntity.setParent(fractureGameObject);
            }

            anchorChunks(fractureGameObject.handle, anchor);

            // Graph manager freezes/unfreezes blocks depending on whether they are connected to the graph or not
            graphManager.setup(chunks);

            return fractureGameObject.handle;
        }

        void anchorChunks(entity_handle gameObject, anchor anchor)
        {
            if (anchor == anchor::None)
                return;

            auto enttScene = physics::physics_holder::physicsRef->app.getCurrentScene();

            eentity entt{ gameObject, &enttScene->registry };

            auto& transform = entt.getComponent<transform_component>();
            auto bounds = getCompositeMeshBounds(entt);
            auto anchoredColliders = getAnchoredColliders(anchor, transform, bounds);

            for (auto& collider : anchoredColliders)
            {
                eentity coll{ collider, &enttScene->registry };

                coll.getComponent<physics::px_rigidbody_component>().setKinematic(true);
            }
        }

        ::std::unordered_set<entity_handle> getAnchoredColliders(anchor anchor, trs meshTransform, bounds bounds)
        {
            ::std::unordered_set<entity_handle> anchoredChunks;
            float frameWidth = 0.01f;

            auto meshWorldCenter = localToWorld(bounds.center, meshTransform);
            auto meshWorldExtents = bounds.extents * meshTransform.scale;

            if (anchor & anchor::Left)
            {
                auto right = transformDirection(meshTransform, vec3(1.0f, 0.0f, 0.0f));

                auto center = meshWorldCenter - right * meshWorldExtents.x;
                auto abs = ::abs(meshWorldExtents);
                vec3 halfExtents = { frameWidth , abs.y, abs.z };

                px_overlap_info overlapResult = physics::physics_holder::physicsRef->
                    overlapBox(center, halfExtents, meshTransform.rotation);

                for (auto& res : overlapResult.results)
                {
                    anchoredChunks.emplace((entity_handle)res);
                }
            }

            if (anchor & anchor::Right)
            {
                auto right = transformDirection(meshTransform, vec3(1.0f, 0.0f, 0.0f));

                auto center = meshWorldCenter + right * meshWorldExtents.x;
                auto abs = ::abs(meshWorldExtents);
                vec3 halfExtents = { frameWidth , abs.y, abs.z };

                px_overlap_info overlapResult = physics::physics_holder::physicsRef->
                    overlapBox(center, halfExtents, meshTransform.rotation);

                for (auto& res : overlapResult.results)
                {
                    anchoredChunks.emplace((entity_handle)res);
                }
            }

            if (anchor & anchor::Bottom)
            {
                auto up = transformDirection(meshTransform, vec3(0.0f, 1.0f, 0.0f));

                auto center = meshWorldCenter - up * meshWorldExtents.y;
                auto abs = ::abs(meshWorldExtents);
                vec3 halfExtents = { abs.x, frameWidth, abs.z };

                px_overlap_info overlapResult = physics::physics_holder::physicsRef->
                    overlapBox(center, halfExtents, meshTransform.rotation);

                for (auto& res : overlapResult.results)
                {
                    anchoredChunks.emplace((entity_handle)res);
                }
            }

            if (anchor & anchor::Top)
            {
                auto up = transformDirection(meshTransform, vec3(0.0f, 1.0f, 0.0f));

                auto center = meshWorldCenter + up * meshWorldExtents.y;
                auto abs = ::abs(meshWorldExtents);
                vec3 halfExtents = { abs.x, frameWidth, abs.z };

                px_overlap_info overlapResult = physics::physics_holder::physicsRef->
                    overlapBox(center, halfExtents, meshTransform.rotation);

                for (auto& res : overlapResult.results)
                {
                    anchoredChunks.emplace((entity_handle)res);
                }
            }

            if (anchor & anchor::Front)
            {
                auto forward = transformDirection(meshTransform, vec3(0.0f, 0.0f, 1.0f));

                auto center = meshWorldCenter - forward * meshWorldExtents.z;
                auto abs = ::abs(meshWorldExtents);
                vec3 halfExtents = { abs.x, abs.y, frameWidth };

                px_overlap_info overlapResult = physics::physics_holder::physicsRef->
                    overlapBox(center, halfExtents, meshTransform.rotation);

                for (auto& res : overlapResult.results)
                {
                    anchoredChunks.emplace((entity_handle)res);
                }
            }

            if (anchor & anchor::Back)
            {
                auto forward = transformDirection(meshTransform, vec3(0.0f, 0.0f, 1.0f));

                auto center = meshWorldCenter + forward * meshWorldExtents.z;
                auto abs = ::abs(meshWorldExtents);
                vec3 halfExtents = { abs.x, abs.y, frameWidth };

                px_overlap_info overlapResult = physics::physics_holder::physicsRef->
                    overlapBox(center, halfExtents, meshTransform.rotation);

                for (auto& res : overlapResult.results)
                {
                    anchoredChunks.emplace((entity_handle)res);
                }
            }

            return anchoredChunks;
        }

        bool validateMesh(ref<submesh_asset> mesh)
        {
            if (!mesh->positions.size())
            {
                LOG_ERROR("Blast> Mesh does not have any vertices.");
                return false;
            }

            if (!mesh->uvs.size())
            {
                LOG_ERROR("Blast> Mesh does not have any vertices.");
                return false;
            }

            return true;
        }

        void connectTouchingChunks(chunk_graph_manager& manager, ref<submesh_asset> asset, entity_handle chunk, float jointBreakForce, float touchRadius = .01f)
        {
            auto enttScene = physics::physics_holder::physicsRef->app.getCurrentScene();
            eentity entt{ chunk, &enttScene->registry };

            auto& rb = entt.getComponent<physics::px_rigidbody_component>();
            auto& transform = entt.getComponent<transform_component>();
            auto& mesh = entt.getComponent<mesh_component>().mesh;

            ::std::unordered_set<entity_handle> overlaps;

            auto& vertices = asset->positions;

            for (size_t i = 0; i < vertices.size(); i++)
            {
                trs worldPosition = transform * vertices[i];

                px_overlap_info overlapResult = physics::physics_holder::physicsRef->
                    overlapSphere(worldPosition.position, touchRadius);

                for (size_t j = 0; j < overlapResult.results.size(); j++)
                {
                    overlaps.emplace((entity_handle)overlapResult.results[j]);
                }
            }

            for (auto overlap : overlaps)
            {
                if (overlap != chunk)
                {
                    {
                        eentity body{ overlap, &enttScene->registry };

                        auto& rbOverlap = body.getComponent<physics::px_rigidbody_component>();

                        std::vector<PxFilterData> fd1 = getFilterData(rb.getRigidActor());
                        std::vector<PxFilterData> fd2 = getFilterData(rbOverlap.getRigidActor());

                        px_fixed_joint* joint = new px_fixed_joint(px_fixed_joint_desc{ 0.1f, 0.1f, 2000.0f, 1000.0f }, rb.getRigidActor(), rbOverlap.getRigidActor());
                        
                        joint->joint->setInvInertiaScale0(0.0f);
                        joint->joint->setInvInertiaScale1(0.0f);
                        joint->joint->setConstraintFlag(PxConstraintFlag::eCOLLISION_ENABLED, false);

                        setFilterData(rb.getRigidActor(), fd1);
                        setFilterData(rbOverlap.getRigidActor(), fd2);

                        if (manager.joints.contains(chunk))
                        {
                            manager.joints[chunk].push_back(joint);
                        }
                        else
                        {
                            ::std::vector<px_fixed_joint*> jointVec;
                            jointVec.push_back(joint);
                            manager.joints.emplace(chunk, jointVec);
                        }
                    }
                }
            }
        }
    };
}