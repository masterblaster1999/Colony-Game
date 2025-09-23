// Domain-warped fBM + Worley helpers (callable from VS/PS/CS)
float ValueNoise(float2 p);
float fBM(float2 p, int oct, float lac, float gain);
float WorleyF1(float2 p);
float DomainWarpedFBM(float2 p) {
    float2 w = float2(fBM(p*0.35, 4, 2.0, 0.5), fBM(p*0.35+31.4, 4, 2.0, 0.5));
    return fBM(p + 2.0*w, 5, 2.0, 0.5);
}
