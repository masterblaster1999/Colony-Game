// PoissonDisk_cs.hlsl (SM5, D3D11 compute)
// Parallel sample-elimination Poisson-disk generator.
//   mode=0: seed per-cell candidates
//   mode=1: eliminate conflicts (ping-pong In/Out buffers), keep highest priority
//   mode=2: compact survivors to AppendStructuredBuffer<float2> OutPoints
//
// Theory background:
//   - Bridson (2007): Poisson disk sampling basics (blue-noise, min spacing r). 
//   - Wei (2008): Parallel Poisson disk sampling via elimination (keep best candidate per neighborhood).
// Buffers & API notes:
//   - AppendStructuredBuffer requires UAV with APPEND flag; read back count with CopyStructureCount.
//   - This shader targets cs_5_0 (FXC) for D3D11.  :contentReference[oaicite:1]{index=1}

cbuffer PoissonParams : register(b0)
{
    float2 domainMin;       // world/texture-space min (e.g., 0,0)
    float2 domainSize;      // world/texture-space size (W,H)
    float  radius;          // desired minimal spacing
    float  cellSize;        // usually radius * 1/sqrt(2)  (precompute on CPU)
    uint   gridW;           // ceil(W / cellSize)
    uint   gridH;           // ceil(H / cellSize)
    uint   seed;            // RNG seed
    uint   mode;            // 0=seed, 1=eliminate, 2=compact
    uint   useMask;         // 0 or 1 (optional acceptance mask)
    float  maskThreshold;   // mask >= threshold => accept
    float2 maskInvSize;     // if mask is in same space as domain: uv = (p - domainMin) * maskInvSize
    // 16 * 4 bytes aligned
};

// Optional: binary/float grayscale mask (bind only if useMask==1)
Texture2D<float> AcceptMask : register(t1);
SamplerState     PointClamp : register(s0);

// Candidate structure (16 bytes)
struct Candidate
{
    float2 pos;     // position in domain space (x,y)
    uint   prio;    // random priority for conflict resolution
    uint   alive;   // 1=alive candidate, 0=dead/empty
};

// For mode=1 read from In (SRV) and write to Out (UAV); for mode=0 write Out only; for mode=2 read In only
StructuredBuffer<Candidate>    CandidatesIn  : register(t0);
RWStructuredBuffer<Candidate>  CandidatesOut : register(u0);

// Final survivors go here (mode=2). Create UAV with APPEND flag.
AppendStructuredBuffer<float2> OutPoints     : register(u1);

// ------------------------------------ RNG utilities ------------------------------------
uint xorshift32(inout uint s)
{
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return s;
}
float rand01(inout uint s)
{
    // Convert 24 LSBs to [0,1)
    s = xorshift32(s);
    return (s & 0x00FFFFFFu) * (1.0 / 16777216.0);
}

// Hash 2D cell coords + seed into a 32-bit state
uint cellSeed(uint cx, uint cy, uint base)
{
    uint v = cx * 747796405u + cy * 2891336453u + base * 104395301u;
    // Jenkins/Wang-ish avalanche
    v ^= v >> 16; v *= 2246822519u;
    v ^= v >> 13; v *= 3266489917u;
    v ^= v >> 16;
    return v;
}

// ------------------------------------ Helpers ------------------------------------------
bool inBounds(float2 p)
{
    return all(p >= domainMin) && all(p < (domainMin + domainSize));
}

bool maskAccept(float2 p)
{
    if (useMask == 0) return true;
    // Map to [0,1] using given inverse size (assumes domain aligned with mask)
    float2 uv = (p - domainMin) * maskInvSize;
    float m = AcceptMask.SampleLevel(PointClamp, uv, 0.0).r;
    return (m >= maskThreshold);
}

float dist2(float2 a, float2 b)
{
    float2 d = a - b; 
    return dot(d, d);
}

// Given a grid cell (cx,cy), produce a random candidate inside that cell.
Candidate makeCandidate(uint cx, uint cy)
{
    Candidate c;
    uint st = cellSeed(cx, cy, seed);

    // Random position inside the cell [0,cellSize)^2
    float2 jitter = float2(rand01(st), rand01(st)) * cellSize;

    c.pos   = domainMin + float2(cx * cellSize, cy * cellSize) + jitter;
    c.prio  = xorshift32(st); // random priority
    c.alive = (inBounds(c.pos) && maskAccept(c.pos)) ? 1u : 0u;
    return c;
}

// Neighborhood radius in cells for min spacing r with cell = r/sqrt(2) ≈ r * 0.7071.
// r / cellSize ≈ sqrt(2) ⇒ need to inspect up to 2 cells away in each axis.
static const int kNeighborRange = 2;

// ------------------------------------ main ---------------------------------------------
[numthreads(8,8,1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    // 2D dispatch covering the grid of cells
    if (DTid.x >= gridW || DTid.y >= gridH) return;
    const uint idx = DTid.y * gridW + DTid.x;

    if (mode == 0)
    {
        // --- SEED: one candidate per cell ---
        CandidatesOut[idx] = makeCandidate(DTid.x, DTid.y);
        return;
    }
    else if (mode == 1)
    {
        // --- ELIMINATE: keep highest priority among neighbors closer than r ---
        Candidate me = CandidatesIn[idx];

        if (me.alive == 0u)
        {
            CandidatesOut[idx] = me;
            return;
        }

        const float r2 = radius * radius;

        // Compare against neighbors in a (2*range+1)^2 window
        // Keep the candidate with the highest priority; break ties by index.
        bool keep = true;

        [unroll]
        for (int oy = -kNeighborRange; oy <= kNeighborRange; ++oy)
        {
            int ny = int(DTid.y) + oy;
            if (ny < 0 || ny >= int(gridH)) continue;

            [unroll]
            for (int ox = -kNeighborRange; ox <= kNeighborRange; ++ox)
            {
                int nx = int(DTid.x) + ox;
                if (nx < 0 || nx >= int(gridW)) continue;

                uint nidx = uint(ny) * gridW + uint(nx);
                if (nidx == idx) continue;

                Candidate other = CandidatesIn[nidx];
                if (other.alive == 0u) continue;

                if (dist2(me.pos, other.pos) < r2)
                {
                    // If neighbor has >= priority, current loses. Break ties by index to avoid oscillation.
                    if ( (other.prio > me.prio) || (other.prio == me.prio && nidx > idx) )
                    {
                        keep = false;
                        // Early-out is okay (deterministic given read-only In buffer)
                        oy = kNeighborRange + 1;
                        break;
                    }
                }
            }
        }

        me.alive = keep ? 1u : 0u;
        CandidatesOut[idx] = me;
        return;
    }
    else // mode == 2
    {
        // --- COMPACT SURVIVORS TO APPEND BUFFER ---
        Candidate me = CandidatesIn[idx];
        if (me.alive == 1u)
        {
            OutPoints.Append(me.pos);
        }
    }
}
