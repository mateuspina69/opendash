// The characters' subdivision (renderer ModelSubdivision), made on the GPU: the frame's dynamic triangles as the CPU
// sent them go in, the vertices the draws and the rays read come out. A model's triangle is split in level^2 (Phong
// tessellation, phongPoint below, over the normals its plan gave its three vertices); any other triangle
// is copied as it is. One thread per triangle that goes in.
struct Params {
    uint triCount;
    uint level;
    float amount;
    uint pad;
};
#ifdef __spirv__ // a push-constant block in Vulkan (the binding layout's PushConstants)
[[vk::push_constant]]
#endif
ConstantBuffer<Params> params : register(b0);

// nvrhi_renderer.cpp GpuVertex, 136 bytes
struct Vertex {
    float3 pos;
    float2 uv;
    float slice, shade, cull, layer;
    float3 color;
    float piece;
    float3 normal, prevPos, faceN, pairN;
    float surface;
    float4 uvRect;
    float object;
    float3 pieceN;
};
static const uint kVertexBytes = 136;

ByteAddressBuffer coarse : register(t0);
// per triangle: its first vertex in `coarse`, its first vertex in `fine`, and kTri* (nvrhi_renderer.cpp)
ByteAddressBuffer tris : register(t1);
RWByteAddressBuffer fine : register(u0);
RWStructuredBuffer<float4> attr : register(u1); // the rays' attributes (u, v, slice, object) of each vertex of `fine`

static const uint kTriSplit = 1, kTriRoundShift = 1, kTriCullOne = 16, kTriReverse = 32;

// Phong tessellation (Boubekeur & Alexa 2008) of the triangle a, b, c with unit normals na, nb, nc at the point of
// barycentric weights w of a, b, c: the point projected on the three vertices' tangent planes, blended by those
// weights, `amount` of the way from the flat triangle (0 flat, 1 whole). The vertices stay where they are, and a
// point on an edge depends only on that edge's two vertices
float3 phongPoint(float3 a, float3 b, float3 c, float3 na, float3 nb, float3 nc, float3 w, float amount) {
    const float3 flat = a * w.x + b * w.y + c * w.z;
    const float oa = dot(flat - a, na), ob = dot(flat - b, nb), oc = dot(flat - c, nc);
    return flat - amount * (w.x * oa * na + w.y * ob * nb + w.z * oc * nc);
}

void put(uint index, Vertex v) {
    fine.Store<Vertex>(index * kVertexBytes, v);
    attr[index] = float4(v.uv, v.slice, v.object);
}

void putTriangle(uint index, Vertex p, Vertex q, Vertex u, bool reverse) {
    put(index, p);
    if (reverse) {
        put(index + 1, u);
        put(index + 2, q);
    } else {
        put(index + 1, q);
        put(index + 2, u);
    }
}

// the point i along a -> b, j along a -> c of the triangle's grid; on an edge that is not round it stays on the edge
Vertex gridPoint(Vertex a, Vertex b, Vertex c, uint i, uint j, uint flags) {
    const uint level = params.level;
    const float s = float(i) / float(level), t = float(j) / float(level), r = 1.0 - s - t;
    Vertex o = a;
    o.cull = (flags & kTriCullOne) ? 1.0 : 0.0;
    o.uv = a.uv * r + b.uv * s + c.uv * t;
    o.color = a.color * r + b.color * s + c.color * t;
    o.shade = a.shade * r + b.shade * s + c.shade * t;
    const float3 n = a.normal * r + b.normal * s + c.normal * t;
    const float len = length(n);
    o.normal = len > 1e-6 ? n / len : n;
    o.pieceN = a.pieceN * r + b.pieceN * s + c.pieceN * t; // (linear over the face: not unit)
    const uint edges = flags >> kTriRoundShift; // bit e: the edge e is round
    const bool straight = (j == 0 && !(edges & 1)) || (i + j == level && !(edges & 2)) || (i == 0 && !(edges & 4));
    const float by = straight ? 0.0 : params.amount;
    o.pos = phongPoint(a.pos, b.pos, c.pos, a.normal, b.normal, c.normal, float3(r, s, t), by);
    o.prevPos = phongPoint(a.prevPos, b.prevPos, c.prevPos, a.normal, b.normal, c.normal, float3(r, s, t), by);
    return o;
}

[numthreads(64, 1, 1)] void cs_main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= params.triCount) return;
    const uint3 tri = tris.Load3(id.x * 12);
    const Vertex a = coarse.Load<Vertex>(tri.x * kVertexBytes);
    const Vertex b = coarse.Load<Vertex>((tri.x + 1) * kVertexBytes);
    const Vertex c = coarse.Load<Vertex>((tri.x + 2) * kVertexBytes);
    uint at = tri.y;
    const uint flags = tri.z;
    if (!(flags & kTriSplit)) {
        put(at, a);
        put(at + 1, b);
        put(at + 2, c);
        return;
    }
    const bool reverse = (flags & kTriReverse) != 0;
    const uint level = params.level;
    for (uint j = 0; j < level; ++j)
        for (uint i = 0; i + j < level; ++i) {
            const Vertex p = gridPoint(a, b, c, i, j, flags);
            const Vertex q = gridPoint(a, b, c, i + 1, j, flags);
            const Vertex u = gridPoint(a, b, c, i, j + 1, flags);
            putTriangle(at, p, q, u, reverse);
            at += 3;
            if (i + j + 1 < level) {
                const Vertex w = gridPoint(a, b, c, i + 1, j + 1, flags);
                putTriangle(at, q, w, u, reverse);
                at += 3;
            }
        }
}
