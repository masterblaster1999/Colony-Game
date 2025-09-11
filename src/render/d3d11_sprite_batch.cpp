// d3d11_sprite_batch.cpp (sketch)
D3D11_BUFFER_DESC vbDesc{ };
vbDesc.ByteWidth = MaxVerts * sizeof(Vertex);
vbDesc.Usage = D3D11_USAGE_DYNAMIC;
vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
dev->CreateBuffer(&vbDesc, nullptr, &vb);

void SpriteBatch::flush(ID3D11DeviceContext* ctx) {
  D3D11_MAPPED_SUBRESOURCE m{};
  ctx->Map(vb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m); // new memory if GPU still reading
  memcpy(m.pData, cpuVerts.data(), vertCount*sizeof(Vertex));
  ctx->Unmap(vb.Get(), 0);
  // set pipeline and Draw
}
