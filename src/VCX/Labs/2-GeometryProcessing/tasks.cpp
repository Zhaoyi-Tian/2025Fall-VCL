#include <cstddef>
#include <glm/fwd.hpp>
#include <glm/geometric.hpp>
#include <unordered_map>
#include <unordered_set>

#include <glm/gtc/matrix_inverse.hpp>
#include <spdlog/spdlog.h>

#include "Labs/2-GeometryProcessing/DCEL.hpp"
#include "Labs/2-GeometryProcessing/tasks.h"

namespace VCX::Labs::GeometryProcessing {

#include "Labs/2-GeometryProcessing/marching_cubes_table.h"

    /******************* 1. Mesh Subdivision *****************/
    void SubdivisionMesh(Engine::SurfaceMesh const & input, Engine::SurfaceMesh & output, std::uint32_t numIterations) {
        Engine::SurfaceMesh curr_mesh = input;
        // We do subdivison iteratively.
        for (std::uint32_t it = 0; it < numIterations; ++it) {
            // During each iteration, we first move curr_mesh into prev_mesh.
            Engine::SurfaceMesh prev_mesh;
            prev_mesh.Swap(curr_mesh);
            // Then we create doubly connected edge list.
            DCEL G(prev_mesh);
            if (! G.IsManifold()) {
                spdlog::warn("VCX::Labs::GeometryProcessing::SubdivisionMesh(..): Non-manifold mesh.");
                return;
            }
            // Note that here curr_mesh has already been empty.
            // We reserve memory first for efficiency.
            curr_mesh.Positions.reserve(prev_mesh.Positions.size() * 3 / 2);
            curr_mesh.Indices.reserve(prev_mesh.Indices.size() * 4);
            // Then we iteratively update currently existing vertices.
            for (std::size_t i = 0; i < prev_mesh.Positions.size(); ++i) {
                // Update the currently existing vetex v from prev_mesh.Positions.
                // Then add the updated vertex into curr_mesh.Positions.
                auto v         = G.Vertex(i);
                auto neighbors = v->Neighbors();
                // your code here:
                int       n        = neighbors.size();
                float     u        = (n == 3) ? 3.0f / 16.0f : 3.0f / (8.0f * n);
                glm::vec3 v_update = float((1 - n * u)) * prev_mesh.Positions[i];
                for (std::size_t j = 0; j < n; j++) {
                    v_update += prev_mesh.Positions[neighbors[j]] * u;
                }
                curr_mesh.Positions.push_back(v_update);
            }
            // We create an array to store indices of the newly generated vertices.
            // Note: newIndices[i][j] is the index of vertex generated on the "opposite edge" of j-th
            //       vertex in the i-th triangle.
            std::vector<std::array<std::uint32_t, 3U>> newIndices(prev_mesh.Indices.size() / 3, { ~0U, ~0U, ~0U });
            // Iteratively process each halfedge.
            for (auto e : G.Edges()) {
                // newIndices[face index][vertex index] = index of the newly generated vertex
                newIndices[G.IndexOf(e->Face())][e->EdgeLabel()] = curr_mesh.Positions.size();
                auto eTwin                                       = e->TwinEdgeOr(nullptr);
                // eTwin stores the twin halfedge.
                if (! eTwin) {
                    // When there is no twin halfedge (so, e is a boundary edge):
                    // your code here: generate the new vertex and add it into curr_mesh.Positions.
                    glm::vec3 v_new;
                    v_new = (prev_mesh.Positions[e->From()] + prev_mesh.Positions[e->To()]) / 2.0f;
                    curr_mesh.Positions.push_back(v_new);

                } else {
                    // When the twin halfedge exists, we should also record:
                    //     newIndices[face index][vertex index] = index of the newly generated vertex
                    // Because G.Edges() will only traverse once for two halfedges,
                    //     we have to record twice.
                    newIndices[G.IndexOf(eTwin->Face())][e->TwinEdge()->EdgeLabel()] = curr_mesh.Positions.size();
                    // your code here: generate the new vertex and add it into curr_mesh.Positions.
                    glm::vec3 v_new;
                    v_new = (prev_mesh.Positions[e->From()] + prev_mesh.Positions[e->To()]) * 3.0f / 8.0f;
                    v_new += (prev_mesh.Positions[e->OppositeVertex()] + prev_mesh.Positions[eTwin->OppositeVertex()]) / 8.0f;
                    curr_mesh.Positions.push_back(v_new);
                }
            }

            // Here we've already build all the vertices.
            // Next, it's time to reconstruct face indices.
            for (std::size_t i = 0; i < prev_mesh.Indices.size(); i += 3U) {
                // For each face F in prev_mesh, we should create 4 sub-faces.
                // v0,v1,v2 are indices of vertices in F.
                // m0,m1,m2 are generated vertices on the edges of F.
                auto v0           = prev_mesh.Indices[i + 0U];
                auto v1           = prev_mesh.Indices[i + 1U];
                auto v2           = prev_mesh.Indices[i + 2U];
                auto [m0, m1, m2] = newIndices[i / 3U];
                // Note: m0 is on the opposite edge (v1-v2) to v0.
                // Please keep the correct indices order (consistent with order v0-v1-v2)
                //     when inserting new face indices.
                // toInsert[i][j] stores the j-th vertex index of the i-th sub-face.
                std::uint32_t toInsert[4][3] = {
                    // your code here:
                    { v0, m2, m1 },
                    { v1, m0, m2 },
                    { v2, m1, m0 },
                    { m0, m1, m2 }
                };
                // Do insertion.
                curr_mesh.Indices.insert(
                    curr_mesh.Indices.end(),
                    reinterpret_cast<std::uint32_t *>(toInsert),
                    reinterpret_cast<std::uint32_t *>(toInsert) + 12U);
            }

            if (curr_mesh.Positions.size() == 0) {
                spdlog::warn("VCX::Labs::GeometryProcessing::SubdivisionMesh(..): Empty mesh.");
                output = input;
                return;
            }
        }
        // Update output.
        output.Swap(curr_mesh);
    }

    /******************* 2. Mesh Parameterization *****************/
    void Parameterization(Engine::SurfaceMesh const & input, Engine::SurfaceMesh & output, const std::uint32_t numIterations) {
        // Copy.
        output = input;
        // Reset output.TexCoords.
        output.TexCoords.resize(input.Positions.size(), glm::vec2 { 0 });

        // Build DCEL.
        DCEL G(input);
        if (! G.IsManifold()) {
            spdlog::warn("VCX::Labs::GeometryProcessing::Parameterization(..): non-manifold mesh.");
            return;
        }

        // Set boundary UVs for boundary vertices.
        // your code here: directly edit output.TexCoords
        DCEL::VertexProxy const * v_begin;
        DCEL::VertexProxy const * v_current;
        std::vector<size_t>       Boundary;
        for (std::size_t i = 0; i < input.Positions.size(); ++i) {
            v_begin = G.Vertex(i);
            if (v_begin->OnBoundary()) {
                Boundary.push_back(i);
                break;
            }
        }
        v_current = G.Vertex(v_begin->BoundaryNeighbors().first);
        Boundary.push_back(v_begin->BoundaryNeighbors().first);
        while (true) {
            auto BoundaryPairs = v_current->BoundaryNeighbors();
            if (std::find(Boundary.begin(), Boundary.end(), BoundaryPairs.first) == Boundary.end()) {
                v_current = G.Vertex(BoundaryPairs.first);
                Boundary.push_back(BoundaryPairs.first);
            } else if (std::find(Boundary.begin(), Boundary.end(), BoundaryPairs.second) == Boundary.end()) {
                v_current = G.Vertex(BoundaryPairs.second);
                Boundary.push_back(BoundaryPairs.second);
            } else {
                break;
            }
        }
        size_t n    = Boundary.size();
        size_t base = n / 4;
        size_t re   = n % 4;
        size_t edgeCounts[4];
        for (int e = 0; e < 4; ++e) {
            edgeCounts[e] = base + (e < re ? 1 : 0);
        }
        size_t idx = 0;
        for (int e = 0; e < 4; ++e) {
            for (size_t k = 0; k < edgeCounts[e]; ++k, ++idx) {
                float     t = edgeCounts[e] > 1 ? float(k) / (edgeCounts[e] - 1) : 0.0f;
                glm::vec2 uv;
                if (e == 0) uv = glm::vec2(t, 0.0f);             // 边一
                else if (e == 1) uv = glm::vec2(1.0f, t);        // 边二
                else if (e == 2) uv = glm::vec2(1.0f - t, 1.0f); // 边三
                else uv = glm::vec2(0.0f, 1.0f - t);             // 边四
                output.TexCoords[Boundary[idx]] = uv;
            }
        }

        // Solve equation via Gauss-Seidel Iterative Method.
        for (int k = 0; k < numIterations; ++k) {
            // your code here:
            for (std::size_t i = 0; i < input.Positions.size(); ++i) {
                auto v = G.Vertex(i);
                if (v->OnBoundary()) continue;
                auto      Neighbors = v->Neighbors();
                size_t    n         = Neighbors.size();
                glm::vec2 sum(0.0f);
                for (auto point : Neighbors) {
                    sum += output.TexCoords[point];
                }
                sum /= n;
                output.TexCoords[i] = sum;
            }
        }
    }

    /******************* 3. Mesh Simplification *****************/
    void SimplifyMesh(Engine::SurfaceMesh const & input, Engine::SurfaceMesh & output, float simplification_ratio) {
        DCEL G(input);
        if (! G.IsManifold()) {
            spdlog::warn("VCX::Labs::GeometryProcessing::SimplifyMesh(..): Non-manifold mesh.");
            return;
        }
        // We only allow watertight mesh.
        if (! G.IsWatertight()) {
            spdlog::warn("VCX::Labs::GeometryProcessing::SimplifyMesh(..): Non-watertight mesh.");
            return;
        }

        // Copy.
        output = input;

        // Compute Kp matrix of the face f.
        auto UpdateQ {
            [&G, &output](DCEL::Triangle const * f) -> glm::mat4 {
                glm::mat4 Kp;
                // your code here:
                auto v1  = output.Positions[f->VertexIndex(0)];
                auto v2  = output.Positions[f->VertexIndex(1)];
                auto v3  = output.Positions[f->VertexIndex(2)];
                auto v12 = v1 - v2;
                auto v13 = v1 - v3;
                auto v   = glm::cross(v12, v13);
                v /= glm::length(v);
                float     d = -(v[0] * v1[0] + v[1] * v1[1] + v[2] * v1[2]);
                glm::vec4 p { v, d };
                Kp = glm::outerProduct(p, p);
                return Kp;
            }
        };

        // The struct to record contraction info.
        struct ContractionPair {
            DCEL::HalfEdge const * edge;           // which edge to contract; if $edge == nullptr$, it means this pair is no longer valid
            glm::vec4              targetPosition; // the targetPosition $v$ for vertex $edge->From()$ to move to
            float                  cost;           // the cost $v.T * Qbar * v$
        };

        // Given an edge (v1->v2), the positions of its two endpoints (p1, p2) and the Q matrix (Q1+Q2),
        //     return the ContractionPair struct.
        static constexpr auto MakePair {
            [](DCEL::HalfEdge const * edge,
               glm::vec3 const &      p1,
               glm::vec3 const &      p2,
               glm::mat4 const &      Q) -> ContractionPair {
                // your code here:
                glm::mat3 q_3x3(Q);
                glm::vec3 q_col4(Q[3]);
                glm::vec3 target_p;
                float     det = glm::determinant(q_3x3);
                if (std::abs(det) > 1e-3f) {
                    target_p = glm::inverse(q_3x3) * (-q_col4);
                } else {
                    target_p = (p1 + p2) / 2.0f;
                }
                glm::vec4 targetPosition(target_p, 1.0f);
                float     cost = glm::dot(targetPosition, Q * targetPosition);
                return { edge, targetPosition, cost };
            }
        };

        // pair_map: map EdgeIdx to index of $pairs$
        // pairs:    store ContractionPair
        // Qv:       $Qv[idx]$ is the Q matrix of vertex with index $idx$
        // Kf:       $Kf[idx]$ is the Kp matrix of face with index $idx$
        std::unordered_map<DCEL::EdgeIdx, std::size_t> pair_map;
        std::vector<ContractionPair>                   pairs;
        std::vector<glm::mat4>                         Qv(G.NumOfVertices(), glm::mat4(0));
        std::vector<glm::mat4>                         Kf(G.NumOfFaces(), glm::mat4(0));

        // Initially, we compute Q matrix for each faces and it accumulates at each vertex.
        for (auto f : G.Faces()) {
            auto Q = UpdateQ(f);
            Qv[f->VertexIndex(0)] += Q;
            Qv[f->VertexIndex(1)] += Q;
            Qv[f->VertexIndex(2)] += Q;
            Kf[G.IndexOf(f)] = Q;
        }

        pair_map.reserve(G.NumOfFaces() * 3);
        pairs.reserve(G.NumOfFaces() * 3 / 2);

        // Initially, we make pairs from all the contractable edges.
        for (auto e : G.Edges()) {
            if (! G.IsContractable(e)) continue;
            auto v1                            = e->From();
            auto v2                            = e->To();
            auto pair                          = MakePair(e, input.Positions[v1], input.Positions[v2], Qv[v1] + Qv[v2]);
            pair_map[G.IndexOf(e)]             = pairs.size();
            pair_map[G.IndexOf(e->TwinEdge())] = pairs.size();
            pairs.emplace_back(pair);
        }

        // Loop until the number of vertices is less than $simplification_ratio * initial_size$.
        while (G.NumOfVertices() > simplification_ratio * Qv.size()) {
            // Find the contractable pair with minimal cost.
            std::size_t min_idx = ~0;
            for (std::size_t i = 1; i < pairs.size(); ++i) {
                if (! pairs[i].edge) continue;
                if (! ~min_idx || pairs[i].cost < pairs[min_idx].cost) {
                    if (G.IsContractable(pairs[i].edge)) min_idx = i;
                    else pairs[i].edge = nullptr;
                }
            }
            if (! ~min_idx) break;

            // top:    the contractable pair with minimal cost
            // v1:     the reserved vertex
            // v2:     the removed vertex
            // result: the contract result
            // ring:   the edge ring of vertex v1
            ContractionPair & top    = pairs[min_idx];
            auto              v1     = top.edge->From();
            auto              v2     = top.edge->To();
            auto              result = G.Contract(top.edge);
            auto              ring   = G.Vertex(v1)->Ring();

            top.edge             = nullptr;            // The contraction has already been done, so the pair is no longer valid. Mark it as invalid.
            output.Positions[v1] = top.targetPosition; // Update the positions.

            // We do something to repair $pair_map$ and $pairs$ because some edges and vertices no longer exist.
            for (int i = 0; i < 2; ++i) {
                DCEL::EdgeIdx removed           = G.IndexOf(result.removed_edges[i].first);
                DCEL::EdgeIdx collapsed         = G.IndexOf(result.collapsed_edges[i].second);
                pairs[pair_map[removed]].edge   = result.collapsed_edges[i].first;
                pairs[pair_map[collapsed]].edge = nullptr;
                pair_map[collapsed]             = pair_map[G.IndexOf(result.collapsed_edges[i].first)];
            }

            // For the two wing vertices, each of them lose one incident face.
            // So, we update the Q matrix.
            Qv[result.removed_faces[0].first] -= Kf[G.IndexOf(result.removed_faces[0].second)];
            Qv[result.removed_faces[1].first] -= Kf[G.IndexOf(result.removed_faces[1].second)];

            // For the vertex v1, Q matrix should be recomputed.
            // And as the position of v1 changed, all the vertices which are on the ring of v1 should update their Q matrix as well.
            Qv[v1] = glm::mat4(0);
            std::unordered_set<DCEL::VertexIdx> affected_neighbors; // 记录受影响的邻居顶点
            for (auto e : ring) {
                // your code here:
                //     1. Compute the new Kp matrix for $e->Face()$.
                //     2. According to the difference between the old Kp (in $Kf$) and the new Kp (computed in step 1),
                //        update Q matrix of each vertex on the ring (update $Qv$).
                //     3. Update Q matrix of vertex v1 as well (update $Qv$).
                //     4. Update $Kf$.
                auto f = e->Face();
                if (! f) continue;
                auto f_idx = G.IndexOf(f);
                // step1
                glm::mat4 new_Kp = UpdateQ(f);
                // step2
                glm::mat4 delta_Kp = new_Kp - Kf[f_idx];
                for (int i = 0; i < 3; ++i) {
                    auto v_idx_on_face = f->VertexIndex(i);
                    if (v_idx_on_face != v1) {
                        Qv[v_idx_on_face] += delta_Kp;
                        affected_neighbors.insert(v_idx_on_face);
                    }
                }
                // step3
                Qv[v1] += new_Kp;
                // step4
                Kf[f_idx] = new_Kp;
            }

            // Finally, as the Q matrix changed, we should update the relative $ContractionPair$ in $pairs$.
            // Any pair with the Q matrix of its endpoints changed, should be remade by $MakePair$.
            // your code here:
            std::unordered_set<DCEL::VertexIdx> all_affected_vertices = affected_neighbors;
            all_affected_vertices.insert(v1);
            for (auto const & v_idx : all_affected_vertices) {
                for (auto const & e : G.Vertex(v_idx)->Ring()) {
                    if (pair_map.count(G.IndexOf(e))) {
                        auto pair_idx = pair_map[G.IndexOf(e)];
                        if (pairs[pair_idx].edge) {
                            auto end_v1_idx = e->From();
                            auto end_v2_idx = e->To();
                            pairs[pair_idx] = MakePair(
                                e,
                                output.Positions[end_v1_idx],
                                output.Positions[end_v2_idx],
                                Qv[end_v1_idx] + Qv[end_v2_idx]);
                        }
                    }
                }
            }
        }

        // In the end, we check if the result mesh is watertight and manifold.
        if (! G.DebugWatertightManifold()) {
            spdlog::warn("VCX::Labs::GeometryProcessing::SimplifyMesh(..): Result is not watertight manifold.");
        }

        auto exported = G.ExportMesh();
        output.Indices.swap(exported.Indices);
    }

    /******************* 4. Mesh Smoothing *****************/
    void SmoothMesh(Engine::SurfaceMesh const & input, Engine::SurfaceMesh & output, std::uint32_t numIterations, float lambda, bool useUniformWeight) {
        // Define function to compute cotangent value of the angle v1-vAngle-v2
        // static constexpr auto GetCotangent {
        //     [](glm::vec3 vAngle, glm::vec3 v1, glm::vec3 v2) -> float {
        //         // your code here:
        //         return 0.0f;
        //     }
        // };

        DCEL G(input);
        if (! G.IsManifold()) {
            spdlog::warn("VCX::Labs::GeometryProcessing::SmoothMesh(..): Non-manifold mesh.");
            return;
        }
        // We only allow watertight mesh.
        if (! G.IsWatertight()) {
            spdlog::warn("VCX::Labs::GeometryProcessing::SmoothMesh(..): Non-watertight mesh.");
            return;
        }

        Engine::SurfaceMesh prev_mesh;
        prev_mesh.Positions = input.Positions;
        for (std::uint32_t iter = 0; iter < numIterations; ++iter) {
            Engine::SurfaceMesh curr_mesh = prev_mesh;
            for (std::size_t i = 0; i < input.Positions.size(); ++i) {
                // your code here: curr_mesh.Positions[i] = ...
                auto      v              = G.Vertex(i);
                auto      Neighbors      = v->Neighbors();
                auto      opposite_edges = v->Ring();
                glm::vec3 v_update(0.0f);
                float     w_sum = 0;
                if (useUniformWeight) {
                    for (auto neighbor : Neighbors) {
                        v_update += prev_mesh.Positions[neighbor];
                        w_sum += 1;
                    }
                    v_update /= w_sum;
                    curr_mesh.Positions[i] = (1 - lambda) * prev_mesh.Positions[i] + lambda * v_update;
                } else {
                    for (auto neighbor : Neighbors) {
                        std::vector<unsigned int> l;
                        for (auto e : opposite_edges) {
                            if (neighbor == e->From()) {
                                l.push_back(e->To());
                            } else if (neighbor == e->To()) {
                                l.push_back(e->From());
                            }
                        }
                        float w(0.0f);
                        auto  vec_i           = prev_mesh.Positions[i];
                        auto  vec_j           = prev_mesh.Positions[neighbor];
                        auto  vec_k           = prev_mesh.Positions[l[0]];
                        auto  vec_l           = prev_mesh.Positions[l[1]];
                        auto  vec_ki          = vec_i - vec_k;
                        auto  vec_kj          = vec_j - vec_k;
                        auto  vec_li          = vec_i - vec_l;
                        auto  vec_lj          = vec_j - vec_l;
                        float dot_alpha       = glm::dot(vec_ki, vec_kj);
                        float cross_mag_alpha = glm::length(glm::cross(vec_ki, vec_kj));
                        float cot_alpha       = (cross_mag_alpha > 1e-12) ? (dot_alpha / cross_mag_alpha) : 0.0;
                        float dot_beta        = glm::dot(vec_li, vec_lj);
                        float cross_mag_beta  = glm::length(glm::cross(vec_li, vec_lj));
                        float cot_beta        = (cross_mag_beta > 1e-12) ? (dot_beta / cross_mag_beta) : 0.0;
                        float wij             = cot_alpha + cot_beta;

                        v_update += prev_mesh.Positions[neighbor] * wij;
                        w_sum += wij;
                    }
                    v_update /= w_sum;
                    curr_mesh.Positions[i] = (1 - lambda) * prev_mesh.Positions[i] + lambda * v_update;
                }
            }
            // Move curr_mesh to prev_mesh.
            prev_mesh.Swap(curr_mesh);
        }
        // Move prev_mesh to output.
        output.Swap(prev_mesh);
        // Copy indices from input.
        output.Indices = input.Indices;
    }

    /******************* 5. Marching Cubes *****************/
    void MarchingCubes(Engine::SurfaceMesh & output, const std::function<float(const glm::vec3 &)> & sdf, const glm::vec3 & grid_min, const float dx, const int n) {
        // your code here:
        using EdgeKey = std::tuple<int, int, int, int>;
        std::map<EdgeKey, int> edge_to_vertex_idx;

        const int c_EdgeToVert[12][2] = {
            { 0, 1 },
            { 2, 3 },
            { 4, 5 },
            { 6, 7 },
            { 0, 2 },
            { 4, 6 },
            { 1, 3 },
            { 5, 7 },
            { 0, 4 },
            { 1, 5 },
            { 2, 6 },
            { 3, 7 }
        };

        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < n; ++j) {
                for (int k = 0; k < n; ++k) {
                    glm::vec3 v0_pos   = grid_min + glm::vec3(i * dx, j * dx, k * dx);
                    uint8_t   case_idx = 0;
                    float     sdf_values[8];
                    glm::vec3 vertex_pos[8];
                    for (int m = 0; m < 8; m++) {
                        glm::vec3 vi  = v0_pos + glm::vec3((m & 1) * dx, ((m >> 1) & 1) * dx, (m >> 2) * dx);
                        sdf_values[m] = sdf(vi);
                        vertex_pos[m] = vi;
                        if (sdf_values[m] < 0.0f) {
                            case_idx |= (1 << m);
                        }
                    }
                    uint16_t edge_state = c_EdgeStateTable[case_idx];
                    if (edge_state == 0) continue;

                    uint32_t cube_vertex_indices[12];
                    for (int edge_idx = 0; edge_idx < 12; ++edge_idx) {
                        if ((edge_state >> edge_idx) & 1) {
                            EdgeKey key = std::make_tuple(i + (((edge_idx >> 2) + 2) % 3 == 0 ? ((edge_idx >> 1) & 1) : (((edge_idx >> 2) + 1) % 3 == 0 ? (edge_idx & 1) : 0)), j + (((edge_idx >> 2) + 2) % 3 == 1 ? ((edge_idx >> 1) & 1) : (((edge_idx >> 2) + 1) % 3 == 1 ? (edge_idx & 1) : 0)), k + (((edge_idx >> 2) + 2) % 3 == 2 ? ((edge_idx >> 1) & 1) : (((edge_idx >> 2) + 1) % 3 == 2 ? (edge_idx & 1) : 0)), edge_idx >> 2);

                            if (edge_to_vertex_idx.count(key)) {
                                cube_vertex_indices[edge_idx] = edge_to_vertex_idx[key];
                            } else {
                                int       v_idx1   = c_EdgeToVert[edge_idx][0];
                                int       v_idx2   = c_EdgeToVert[edge_idx][1];
                                glm::vec3 p1       = vertex_pos[v_idx1];
                                glm::vec3 p2       = vertex_pos[v_idx2];
                                float     s1       = sdf_values[v_idx1];
                                float     s2       = sdf_values[v_idx2];
                                float     t        = -s1 / (s2 - s1);
                                glm::vec3 vert_pos = p1 + t * (p2 - p1);

                                output.Positions.push_back(vert_pos);
                                uint32_t new_idx              = output.Positions.size() - 1;
                                edge_to_vertex_idx[key]       = new_idx;
                                cube_vertex_indices[edge_idx] = new_idx;
                            }
                        }
                    }

                    for (int tri_v = 0; c_EdgeOrdsTable[case_idx][tri_v] != -1; tri_v += 3) {
                        uint32_t v0 = cube_vertex_indices[c_EdgeOrdsTable[case_idx][tri_v]];
                        uint32_t v1 = cube_vertex_indices[c_EdgeOrdsTable[case_idx][tri_v + 1]];
                        uint32_t v2 = cube_vertex_indices[c_EdgeOrdsTable[case_idx][tri_v + 2]];
                        output.Indices.push_back(v0);
                        output.Indices.push_back(v1);
                        output.Indices.push_back(v2);
                    }
                }
            }
        }
    }
} // namespace VCX::Labs::GeometryProcessing

// namespace VCX::Labs::GeometryProcessing
